/**
 * @file CesiumMinimalPipeline.h
 * @brief A dependency-free, synchronous, and data-oriented C++ implementation of the 4-step Cesium Ion tile pipeline.
 *
 * # ARCHITECTURAL FINDINGS AND DESIGN DECISIONS
 *
 * ## 1. Authentication & Discovery
 * The discovery process transitions from an Ion Asset ID to a root tileset URL. This requires an HTTP request to
 * `https://api.cesium.com/v1/assets/{assetId}/endpoint`. The response contains a `url` (the tileset.json location) 
 * and an `accessToken`. This implementation abstracts HTTP via the `IHttpClient` interface.
 *
 * ## 2. Spatial Traversal & Culling
 * Traversal uses a Recursive Bounding Volume Hierarchy (BVH) approach.
 * - **View-Frustum Culling**: Performed by testing bounding volumes (Sphere, OBB, Region) against the 4-6 planes 
 *   of the camera frustum.
 * - **Geometric Error Evaluation**: Screen Space Error (SSE) is calculated as: 
 *   `SSE = (geometricError * viewportHeight) / (distanceToCamera * 2 * tan(fovy / 2))`.
 *   If `SSE > maximumScreenSpaceError`, the tile is refined (children are visited).
 *
 * ## 3. Fetching & Parsing
 * 3D Tiles (B3DM) are binary wrappers around glTF. The extraction logic identifies the header, 
 * calculates the offsets for Feature Table and Batch Table, and identifies the start of the GLB chunk.
 *
 * ## 4. Rendering Math
 * - **ECEF to ENU**: Uses the geodetic surface normal at a given longitude/latitude/height to establish a local
 *   tangent plane.
 * - **Coordinate Transforms**: Standard 4x4 matrix operations (GLM-compatible) are used for camera pose 
 *   and projection.
 *
 * ## 5. Dependency Management
 * All heavy dependencies (CesiumAsync, CesiumUtility, spdlog) have been stripped. 
 * External systems must provide:
 * - An `IHttpClient` implementation.
 * - `glm` (for vector/matrix math).
 */

#ifndef CESIUM_MINIMAL_PIPELINE_H
#define CESIUM_MINIMAL_PIPELINE_H

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <variant>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace CesiumMinimal {

// ============================================================================
// STEP 1: AUTHENTICATION & DISCOVERY (INTERFACES)
// ============================================================================

struct HttpResponse {
    int statusCode;
    std::vector<char> data;
    std::string error;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers) = 0;
};

struct IonAssetEndpoint {
    std::string url;
    std::string accessToken;
};

class IonDiscovery {
public:
    static std::optional<IonAssetEndpoint> discover(IHttpClient& client, int64_t assetId, const std::string& ionToken) {
        std::string url = "https://api.cesium.com/v1/assets/" + std::to_string(assetId) + "/endpoint?access_token=" + ionToken;
        HttpResponse response = client.get(url, {});
        
        if (response.statusCode != 200) return std::nullopt;
        
        // Minimal manual JSON parsing for "url" and "accessToken"
        std::string body(response.data.begin(), response.data.end());
        auto findKey = [&](const std::string& key) -> std::string {
            size_t pos = body.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            size_t start = body.find("\"", body.find(":", pos) + 1);
            size_t end = body.find("\"", start + 1);
            if (start == std::string::npos || end == std::string::npos) return "";
            return body.substr(start + 1, end - start - 1);
        };

        IonAssetEndpoint endpoint;
        endpoint.url = findKey("url");
        endpoint.accessToken = findKey("accessToken");
        return endpoint;
    }
};

// ============================================================================
// STEP 4: RENDERING MATH (FUNDAMENTALS)
// ============================================================================

struct Constants {
    static constexpr double WGS84_A = 6378137.0;
    static constexpr double WGS84_B = 6356752.3142451793;
    static constexpr double WGS84_F = 1.0 / 298.257223563;
    static constexpr double WGS84_E2 = WGS84_F * (2.0 - WGS84_F);
};

class GlobeMath {
public:
    static glm::dvec3 geodeticSurfaceNormal(const glm::dvec3& ecef) {
        return glm::normalize(ecef / glm::dvec3(1.0, 1.0, 1.0 - Constants::WGS84_E2));
    }

    static glm::dmat4 eastNorthUpToFixedFrame(const glm::dvec3& origin) {
        glm::dvec3 up = geodeticSurfaceNormal(origin);
        glm::dvec3 east = glm::normalize(glm::dvec3(-origin.y, origin.x, 0.0));
        glm::dvec3 north = glm::cross(up, east);

        return glm::dmat4(
            glm::dvec4(east, 0.0),
            glm::dvec4(north, 0.0),
            glm::dvec4(up, 0.0),
            glm::dvec4(origin, 1.0)
        );
    }
};

// ============================================================================
// STEP 2: SPATIAL TRAVERSAL & CULLING
// ============================================================================

struct Plane {
    glm::dvec3 normal;
    double distance;

    Plane(const glm::dvec3& n, double d) : normal(n), distance(d) {}

    double getDistanceToPoint(const glm::dvec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

struct Frustum {
    Plane planes[6] = {
        {{0,0,0},0}, {{0,0,0},0}, {{0,0,0},0}, 
        {{0,0,0},0}, {{0,0,0},0}, {{0,0,0},0}
    };
    
    // Simplified frustum creation from projection * view matrix
    static Frustum fromMatrix(const glm::dmat4& m) {
        Frustum f;
        // Left
        f.planes[0] = Plane(glm::dvec3(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0]), m[3][3] + m[3][0]);
        // Right
        f.planes[1] = Plane(glm::dvec3(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0]), m[3][3] - m[3][0]);
        // Bottom
        f.planes[2] = Plane(glm::dvec3(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1]), m[3][3] + m[3][1]);
        // Top
        f.planes[3] = Plane(glm::dvec3(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1]), m[3][3] - m[3][1]);
        // Near
        f.planes[4] = Plane(glm::dvec3(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2]), m[3][3] + m[3][2]);
        // Far
        f.planes[5] = Plane(glm::dvec3(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2]), m[3][3] - m[3][2]);

        for (int i = 0; i < 6; ++i) {
            double length = glm::length(f.planes[i].normal);
            f.planes[i].normal /= length;
            f.planes[i].distance /= length;
        }
        return f;
    }
};

struct BoundingSphere {
    glm::dvec3 center;
    double radius;

    bool isVisible(const Frustum& f) const {
        for (const auto& plane : f.planes) {
            if (plane.getDistanceToPoint(center) < -radius) return false;
        }
        return true;
    }

    double computeDistanceSquared(const glm::dvec3& position) const {
        return glm::max(0.0, glm::distance(center, position) - radius);
    }
};

struct ViewState {
    glm::dvec3 position;
    Frustum frustum;
    double viewportHeight;
    double fovY;

    double computeSSE(double geometricError, double distance) const {
        distance = glm::max(distance, 1e-7);
        return (geometricError * viewportHeight) / (2.0 * distance * std::tan(fovY * 0.5));
    }
};

struct Tile {
    BoundingSphere bounds;
    double geometricError;
    std::string contentUri;
    std::vector<std::unique_ptr<Tile>> children;
    bool isLoaded = false;
    std::vector<char> glbData;

    void traverse(const ViewState& view, double maxSSE, std::vector<Tile*>& result) {
        if (!bounds.isVisible(view.frustum)) return;

        double distance = std::sqrt(bounds.computeDistanceSquared(view.position));
        double sse = view.computeSSE(geometricError, distance);

        if (sse <= maxSSE || children.empty()) {
            result.push_back(this);
        } else {
            for (auto& child : children) {
                child->traverse(view, maxSSE, result);
            }
        }
    }
};

// ============================================================================
// STEP 3: FETCHING & PARSING
// ============================================================================

struct B3dmHeader {
    char magic[4];
    uint32_t version;
    uint32_t byteLength;
    uint32_t featureTableJsonByteLength;
    uint32_t featureTableBinaryByteLength;
    uint32_t batchTableJsonByteLength;
    uint32_t batchTableBinaryByteLength;
};

class B3dmParser {
public:
    static std::optional<std::pair<const char*, size_t>> extractGlb(const std::vector<char>& data) {
        if (data.size() < sizeof(B3dmHeader)) return std::nullopt;
        
        const B3dmHeader* header = reinterpret_cast<const B3dmHeader*>(data.data());
        if (std::strnc_view(header->magic, 4) != "b3dm") return std::nullopt;

        size_t glbStart = sizeof(B3dmHeader) + 
                          header->featureTableJsonByteLength + 
                          header->featureTableBinaryByteLength + 
                          header->batchTableJsonByteLength + 
                          header->batchTableBinaryByteLength;
        
        if (glbStart >= data.size()) return std::nullopt;
        
        return std::make_pair(data.data() + glbStart, data.size() - glbStart);
    }
};

// ============================================================================
// HIGH-LEVEL PIPELINE EXECUTION
// ============================================================================

class MinimalPipeline {
public:
    static void run(IHttpClient& http, int64_t assetId, const std::string& ionToken, const ViewState& view, double maxSSE) {
        // 1. Discovery
        auto endpoint = IonDiscovery::discover(http, assetId, ionToken);
        if (!endpoint) return;

        // 2. Traversal (Mocking a pre-loaded root for brevity)
        // In a real scenario, you'd fetch tileset.json and build the Tile tree first.
        Tile root; 
        root.geometricError = 1000.0;
        root.bounds = { {0,0,0}, 6378137.0 }; // Global scale
        
        std::vector<Tile*> visibleTiles;
        root.traverse(view, maxSSE, visibleTiles);

        // 3. Fetch & Parse
        for (auto* tile : visibleTiles) {
            if (!tile->contentUri.empty() && !tile->isLoaded) {
                std::string fullUrl = endpoint->url + "/" + tile->contentUri; // Simplified resolving
                HttpResponse res = http.get(fullUrl, {{"Authorization", "Bearer " + endpoint->accessToken}});
                
                if (res.statusCode == 200) {
                    auto glb = B3dmParser::extractGlb(res.data);
                    if (glb) {
                        tile->glbData.assign(glb->first, glb->first + glb->second);
                        tile->isLoaded = true;
                    }
                }
            }
        }
    }
};

} // namespace CesiumMinimal

#endif // CESIUM_MINIMAL_PIPELINE_H
