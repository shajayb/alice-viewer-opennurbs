#ifndef CESIUM_TILE_PARSER_H
#define CESIUM_TILE_PARSER_H

#include <cstdint>
#include <cstring>
#include <optional>

/**
 * @file CesiumTileParser.h
 * @brief Minimal, dependency-free C++ implementation to parse raw Cesium 3D Tiles (B3DM) and GLB containers.
 * 
 * SUMMARY & DECOUPLING STRATEGY:
 * - B3DM Extraction: Skips the 28-byte B3DM header and associated metadata tables (Feature/Batch) 
 *   to isolate the embedded GLB payload, as identified in `B3dmToGltfConverter.cpp`.
 * - GLB Structure: Validates the GLB header and iterates through JSON (0x4E4F534A) and BIN (0x004E4942) 
 *   chunks, avoiding the full `CesiumGltfReader` class hierarchy.
 * - Data-Oriented Access: Provides zero-allocation pointer arithmetic to access vertex and index 
 *   attributes directly from the binary chunk.
 * - Stripped Systems: All async tasks (`CesiumAsync`), HTTP clients, and spatial tree traversal 
 *   logic have been removed to ensure a bare-metal implementation.
 */

struct B3dmHeader {
    char magic[4];
    uint32_t version;
    uint32_t byteLength;
    uint32_t featureTableJsonByteLength;
    uint32_t featureTableBinaryByteLength;
    uint32_t batchTableJsonByteLength;
    uint32_t batchTableBinaryByteLength;
};

struct GlbHeader {
    uint32_t magic;   // "glTF" (0x46546C67)
    uint32_t version;
    uint32_t length;
};

struct ChunkHeader {
    uint32_t length;
    uint32_t type;    // 0x4E4F534A (JSON) or 0x004E4942 (BIN)
};

struct TileData {
    const char* jsonChunk;
    size_t jsonLength;
    const uint8_t* binaryChunk;
    size_t binaryLength;
};

/**
 * @brief A lightweight parser to extract the glTF components from a raw B3DM/GLB buffer.
 */
class TileGeometryParser {
public:
    /**
     * @brief Parses a raw byte buffer containing a B3DM or GLB tile.
     * @param buffer The raw bytes of the tile.
     * @param size The size of the buffer in bytes.
     * @return A TileData structure with pointers to JSON and BINARY chunks, or nullopt if invalid.
     */
    static std::optional<TileData> parse(const uint8_t* buffer, size_t size) {
        if (size < 12) return std::nullopt;

        const uint8_t* glbStart = buffer;
        size_t glbSize = size;

        // 1. Handle B3DM Wrapper (Skip B3DM header and metadata tables)
        if (memcmp(buffer, "b3dm", 4) == 0) {
            if (size < sizeof(B3dmHeader)) return std::nullopt;
            
            const auto* h = reinterpret_cast<const B3dmHeader*>(buffer);
            size_t offset = 28 + h->featureTableJsonByteLength + 
                            h->featureTableBinaryByteLength + 
                            h->batchTableJsonByteLength + 
                            h->batchTableBinaryByteLength;
            
            if (offset >= size) return std::nullopt;
            glbStart = buffer + offset;
            glbSize = size - offset;
        }

        // 2. Parse GLB Container
        if (glbSize < sizeof(GlbHeader)) return std::nullopt;
        
        const auto* glb = reinterpret_cast<const GlbHeader*>(glbStart);
        if (glb->magic != 0x46546C67) return std::nullopt;

        TileData result = {nullptr, 0, nullptr, 0};
        const uint8_t* current = glbStart + sizeof(GlbHeader);
        const uint8_t* end = glbStart + glb->length;

        // Iterate through GLB chunks to find JSON and BINARY data
        while (current + sizeof(ChunkHeader) <= end) {
            const auto* chunk = reinterpret_cast<const ChunkHeader*>(current);
            const uint8_t* chunkData = current + sizeof(ChunkHeader);
            
            if (chunk->type == 0x4E4F534A) { // JSON Chunk
                result.jsonChunk = reinterpret_cast<const char*>(chunkData);
                result.jsonLength = chunk->length;
            } else if (chunk->type == 0x004E4942) { // BINARY Chunk
                result.binaryChunk = chunkData;
                result.binaryLength = chunk->length;
            }
            
            current = chunkData + chunk->length;
        }

        return result;
    }

    /**
     * @brief Direct pointer arithmetic to access vertex/index attributes.
     * @param tile The parsed TileData.
     * @param bufferViewOffset Offset from the bufferView JSON entry.
     * @param accessorOffset Offset from the accessor JSON entry.
     * @return Pointer to the raw attribute data.
     */
    static const uint8_t* getAttributePointer(const TileData& tile, 
                                             size_t bufferViewOffset, 
                                             size_t accessorOffset) {
        if (!tile.binaryChunk) return nullptr;
        return tile.binaryChunk + bufferViewOffset + accessorOffset;
    }
};

#endif // CESIUM_TILE_PARSER_H
