#ifndef TILESET_LOADER_H
#define TILESET_LOADER_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "AliceNetwork.h"
#include "ApiKeyReader.h"
#include "AliceMath.h"
#include "AliceMemory.h"
#include "MeshPrimitive.h"
#include "cgltf.h"

using json = nlohmann::json;

namespace Alice
{
    struct TileNode
    {
        Math::DVec3 sphereCenter;
        double sphereRadius;
        float geometricError;
        double transform[16];
        const char* contentUri;
        const char* baseUrl;
        int firstChild;
        int childCount;
        MeshPrimitive mesh;
        bool isLoaded;
        bool isExternal;
        bool hasContent;
        bool refineAdd; // true for ADD, false for REPLACE
    };

    struct TilesetLoader
    {
        static std::string ConstructGoogleTilesetURL()
        {
            char key[256];
            if (!ApiKeyReader::GetGoogleKey(key, 256)) return "";
            std::string url = "https://tile.googleapis.com/v1/3dtiles/root.json?key=";
            url += key;
            return url;
        }

        static bool FetchRootTileset(const std::string& url, json& out_tileset)
        {
            std::vector<uint8_t> buffer;
            if (!Network::Fetch(url.c_str(), buffer)) return false;
            try
            {
                out_tileset = json::parse(buffer.begin(), buffer.end());
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        static int ParseRecursive(const json& t, Buffer<TileNode>& nodes, LinearArena& arena, const std::string& baseUrl)
        {
            if (nodes.count >= nodes.capacity) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: Nodes buffer capacity exceeded (%d)\n", (int)nodes.capacity);
                return -1;
            }
            int idx = (int)nodes.count++;
            
            TileNode& node = nodes[idx];
            memset(&node, 0, sizeof(TileNode));

            // Default transform to identity
            for (int i = 0; i < 16; ++i) node.transform[i] = (i % 5 == 0) ? 1.0 : 0.0;
            if (t.contains("transform") && t["transform"].is_array())
            {
                for (int i = 0; i < 16; ++i) node.transform[i] = t["transform"][i].get<double>();
            }

            // Store baseUrl
            char* bBuf = (char*)arena.allocate(baseUrl.size() + 1);
            if (bBuf)
            {
                memcpy(bBuf, baseUrl.c_str(), baseUrl.size() + 1);
                node.baseUrl = bBuf;
            }

            if (t.contains("boundingVolume"))
            {
                if (t["boundingVolume"].contains("sphere"))
                {
                    auto s = t["boundingVolume"]["sphere"];
                    node.sphereCenter = { s[0].get<double>(), s[1].get<double>(), s[2].get<double>() };
                    node.sphereRadius = s[3].get<double>();
                }
                else if (t["boundingVolume"].contains("box"))
                {
                    auto b = t["boundingVolume"]["box"];
                    node.sphereCenter = { b[0].get<double>(), b[1].get<double>(), b[2].get<double>() };
                    double hx = b[3].get<double>(), hy = b[7].get<double>(), hz = b[11].get<double>();
                    node.sphereRadius = sqrt(hx*hx + hy*hy + hz*hz);
                }
            }

            node.geometricError = t.value("geometricError", 0.0f);
            node.refineAdd = (t.value("refine", "") == "ADD");

            if (t.contains("content"))
            {
                std::string uri = t["content"].value("uri", "");
                if (uri.empty()) uri = t["content"].value("url", "");
                if (!uri.empty())
                {
                    char* buf = (char*)arena.allocate(uri.size() + 1);
                    if (buf)
                    {
                        memcpy(buf, uri.c_str(), uri.size() + 1);
                        node.contentUri = buf;
                        node.hasContent = true;
                        node.isExternal = (uri.find(".json") != std::string::npos);
                    }
                }
            }

            if (t.contains("children") && t["children"].is_array())
            {
                node.childCount = (int)t["children"].size();
                node.firstChild = (int)nodes.count;
                for (const auto& c : t["children"])
                {
                    if (ParseRecursive(c, nodes, arena, baseUrl) == -1) return -1;
                }
            }

            return idx;
        }

        static void FetchAndGraft(int nodeIdx, Buffer<TileNode>& nodes, LinearArena& arena)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count) return;
            if (!nodes[nodeIdx].isExternal || nodes[nodeIdx].isLoaded) return;

            std::string url = nodes[nodeIdx].baseUrl;
            std::string uri = nodes[nodeIdx].contentUri;
            if (uri.find("http") == 0) url = uri;
            else url += uri;

            std::vector<uint8_t> buffer;
            if (!Network::Fetch(url.c_str(), buffer)) return;

            json external;
            try { external = json::parse(buffer.begin(), buffer.end()); } catch(...) { return; }

            if (external.contains("root"))
            {
                std::string newBaseUrl = url;
                size_t pos = newBaseUrl.find_last_of('/');
                if (pos != std::string::npos) newBaseUrl = newBaseUrl.substr(0, pos + 1);

                int externalRootIdx = ParseRecursive(external["root"], nodes, arena, newBaseUrl);
                
                nodes[nodeIdx].firstChild = externalRootIdx;
                nodes[nodeIdx].childCount = 1;
                nodes[nodeIdx].isLoaded = true;
                nodes[nodeIdx].isExternal = false; 
            }
        }

        static void TraverseAndGraft(int nodeIdx, Buffer<TileNode>& nodes, Math::DVec3 target, Buffer<int>& activeNodes, LinearArena& arena)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count) return;
            
            double dx = nodes[nodeIdx].sphereCenter.x - target.x;
            double dy = nodes[nodeIdx].sphereCenter.y - target.y;
            double dz = nodes[nodeIdx].sphereCenter.z - target.z;
            double distSq = dx*dx + dy*dy + dz*dz;

            // Target radius for St. Paul's cluster (approx 500m)
            const double targetRadius = 500.0;

            // Decision: Descend if we are coarse OR if we are close to the target
            bool shouldDescend = (nodes[nodeIdx].geometricError > 50.0f) || (distSq < nodes[nodeIdx].sphereRadius * nodes[nodeIdx].sphereRadius);
            
            // Only descend if there are children to descend into
            if (shouldDescend && nodes[nodeIdx].isExternal)
            {
                FetchAndGraft(nodeIdx, nodes, arena);
            }

            bool hasChildren = (nodes[nodeIdx].childCount > 0);
            
            if (shouldDescend && hasChildren)
            {
                // If it's ADD, we add ourselves AND descend
                if (nodes[nodeIdx].refineAdd && nodes[nodeIdx].hasContent && !nodes[nodeIdx].isExternal)
                {
                    activeNodes.push_back(nodeIdx);
                }

                int first = nodes[nodeIdx].firstChild;
                int count = nodes[nodeIdx].childCount;
                for (int i = 0; i < count; ++i)
                {
                    TraverseAndGraft(first + i, nodes, target, activeNodes, arena);
                }
            }
            else
            {
                // Terminal node or we decided not to descend: add if it has content
                if (nodes[nodeIdx].hasContent && !nodes[nodeIdx].isExternal)
                {
                    activeNodes.push_back(nodeIdx);
                }
            }
        }

        static bool LoadGLBWithENU(const char* url, MeshPrimitive& out_mesh, Math::DVec3 centerEcef, double* enuMat, const double* transform, Math::Vec3& out_min, Math::Vec3& out_max, LinearArena& arena)
        {
            std::vector<uint8_t> buffer;
            if (!Network::Fetch(url, buffer)) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: Fetch failed for %s\n", url);
                return false;
            }

            cgltf_options options = {};
            cgltf_data* data = NULL;
            cgltf_result result = cgltf_parse(&options, buffer.data(), buffer.size(), &data);
            if (result != cgltf_result_success) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: cgltf_parse failed for %s\n", url);
                return false;
            }

            result = cgltf_load_buffers(&options, data, NULL);
            if (result != cgltf_result_success) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: cgltf_load_buffers failed for %s\n", url);
                cgltf_free(data); 
                return false; 
            }

            if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: No meshes in %s\n", url);
                cgltf_free(data); 
                return false; 
            }

            cgltf_primitive* prim = &data->meshes[0].primitives[0];
            cgltf_accessor *pos_acc = NULL, *norm_acc = NULL;

            for (int i = 0; i < (int)prim->attributes_count; ++i)
            {
                if (prim->attributes[i].type == cgltf_attribute_type_position) pos_acc = prim->attributes[i].data;
                if (prim->attributes[i].type == cgltf_attribute_type_normal) norm_acc = prim->attributes[i].data;
            }

            if (!pos_acc) { cgltf_free(data); return false; }

            size_t vcount = pos_acc->count;
            float* verts = (float*)arena.allocate(vcount * 6 * sizeof(float));

            out_min = { 1e30f, 1e30f, 1e30f };
            out_max = { -1e30f, -1e30f, -1e30f };

            for (size_t i = 0; i < vcount; ++i)
            {
                float p[3], n[3] = { 0,0,1 };
                cgltf_accessor_read_float(pos_acc, i, p, 3);
                if (norm_acc) cgltf_accessor_read_float(norm_acc, i, n, 3);

                // 1. Positions: Multiply raw vertex 'p' by 'transform' to get absolute ECEF
                // 3DTiles transform is 4x4 column-major
                double px = transform[0] * p[0] + transform[4] * p[1] + transform[8] * p[2] + transform[12];
                double py = transform[1] * p[0] + transform[5] * p[1] + transform[9] * p[2] + transform[13];
                double pz = transform[2] * p[0] + transform[6] * p[1] + transform[10] * p[2] + transform[14];

                // 2. Compute the delta from 'centerEcef'
                double dx = px - centerEcef.x;
                double dy = py - centerEcef.y;
                double dz = pz - centerEcef.z;

                // 3. Project to ENU
                float lx = (float)(enuMat[0] * dx + enuMat[1] * dy + enuMat[2] * dz);
                float ly = (float)(enuMat[4] * dx + enuMat[5] * dy + enuMat[6] * dz);
                float lz = (float)(enuMat[8] * dx + enuMat[9] * dy + enuMat[10] * dz);

                verts[i * 6 + 0] = lx; verts[i * 6 + 1] = ly; verts[i * 6 + 2] = lz;

                if (lx < out_min.x) out_min.x = lx;
                if (ly < out_min.y) out_min.y = ly;
                if (lz < out_min.z) out_min.z = lz;
                if (lx > out_max.x) out_max.x = lx;
                if (ly > out_max.y) out_max.y = ly;
                if (lz > out_max.z) out_max.z = lz;

                // 4. Normals: Apply the rotation part of the 'transform' to 'n'
                double nx_rot = transform[0] * n[0] + transform[4] * n[1] + transform[8] * n[2];
                double ny_rot = transform[1] * n[0] + transform[5] * n[1] + transform[9] * n[2];
                double nz_rot = transform[2] * n[0] + transform[6] * n[1] + transform[10] * n[2];

                // 5. Apply the ENU rotation to the rotated normal
                float nx = (float)(enuMat[0] * nx_rot + enuMat[1] * ny_rot + enuMat[2] * nz_rot);
                float ny = (float)(enuMat[4] * nx_rot + enuMat[5] * ny_rot + enuMat[6] * nz_rot);
                float nz = (float)(enuMat[8] * nx_rot + enuMat[9] * ny_rot + enuMat[10] * nz_rot);

                float ilen = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz + 1e-12f);
                verts[i * 6 + 3] = nx * ilen; verts[i * 6 + 4] = ny * ilen; verts[i * 6 + 5] = nz * ilen;
            }

            size_t icount = prim->indices ? prim->indices->count : 0;
            unsigned int* indices = (unsigned int*)arena.allocate(icount * sizeof(unsigned int));
            for (size_t i = 0; i < icount; ++i) indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);

            out_mesh.instanceVbo = 0;
            out_mesh.count = (int)icount;
            glGenVertexArrays(1, &out_mesh.vao);
            glGenBuffers(1, &out_mesh.vbo);
            glGenBuffers(1, &out_mesh.ebo);
            glBindVertexArray(out_mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, out_mesh.vbo);
            glBufferData(GL_ARRAY_BUFFER, vcount * 6 * sizeof(float), verts, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out_mesh.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), indices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

            cgltf_free(data);
            return true;
        }
    };
}

#endif
