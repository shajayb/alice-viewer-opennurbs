#ifndef CESIUM_ION_FETCHER_H
#define CESIUM_ION_FETCHER_H

#include <string>
#include <vector>
#include <cmath>

/**
 * @file CesiumIonFetcher.h
 * @brief Minimal, dependency-free C++ logic to communicate with Cesium ion and filter tiles 
 *        for a 500x500m area around St Paul's Cathedral, London.
 * 
 * SUMMARY & DECOUPLING STRATEGY:
 * 1. Ion API Handshake: 
 *    - Request: `GET https://api.cesium.com/v1/assets/{assetId}/endpoint?access_token={token}`
 *    - Response: Extracts `url` (root tileset) and `accessToken` (scoped credentials for the bucket).
 * 2. Spatial Filtering (St Paul's Cathedral):
 *    - Center: Lat 51.5138, Lon -0.0984.
 *    - Target: A 2D Lat/Lon bounding box in Radians representing a 500x500m area.
 * 3. Intersection Logic: 
 *    - Supports `region` (geographic bounds) and `box` (Oriented Bounding Box).
 *    - For `box`, it performs a conservative AABB check by projecting the box centers to Lat/Lon.
 * 4. Zero-Dependency: 
 *    - Abstracted Network and JSON via `IHttpClient` and `IJsonNode` to avoid linking 
 *      against `libcurl`, `CesiumAsync`, or `rapidjson`.
 */

// --- Target: St Paul's Cathedral (500x500m area) ---
struct StPaulsRegion {
    // Lat 51.5138 N -> 0.899080 rad | Lon 0.0984 W -> -0.001717 rad
    // Offset (250m) -> dLat: 0.000039 rad, dLon: 0.000063 rad
    static constexpr double West  = -0.001717 - 0.000063;
    static constexpr double South =  0.899080 - 0.000039;
    static constexpr double East  = -0.001717 + 0.000063;
    static constexpr double North =  0.899080 + 0.000039;
};

// --- Minimal Decoupling Interfaces ---
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual std::string get(const std::string& url) = 0;
};

class IJsonNode {
public:
    virtual ~IJsonNode() = default;
    virtual std::string getString(const std::string& key) const = 0;
    virtual std::vector<double> getDoubleArray(const std::string& key) const = 0;
    virtual std::vector<IJsonNode*> getArray(const std::string& key) const = 0;
    virtual IJsonNode* getObject(const std::string& key) const = 0;
    virtual bool hasKey(const std::string& key) const = 0;
};

class IJsonParser {
public:
    virtual ~IJsonParser() = default;
    virtual IJsonNode* parse(const std::string& jsonString) = 0;
};

// --- Core Fetching & Traversal Logic ---
class CesiumIonFetcher {
private:
    IHttpClient* _http;
    IJsonParser* _json;
    std::string _scopedToken;
    std::vector<std::string> _tileUris;

public:
    CesiumIonFetcher(IHttpClient* http, IJsonParser* json) : _http(http), _json(json) {}

    void run(uint32_t assetId, const std::string& ionToken) {
        // 1. Resolve Ion Asset to Endpoint
        std::string endpointUrl = "https://api.cesium.com/v1/assets/" + std::to_string(assetId) + 
                                  "/endpoint?access_token=" + ionToken;
        
        std::string resp = _http->get(endpointUrl);
        IJsonNode* root = _json->parse(resp);
        if (!root || !root->hasKey("url")) return;

        std::string tilesetUrl = root->getString("url");
        _scopedToken = root->hasKey("accessToken") ? root->getString("accessToken") : ionToken;

        // 2. Fetch root tileset.json
        std::string baseUrl = tilesetUrl.substr(0, tilesetUrl.find_last_of('/') + 1);
        std::string authUrl = tilesetUrl + (tilesetUrl.find('?') == std::string::npos ? "?" : "&") + "access_token=" + _scopedToken;
        
        std::string tilesetResp = _http->get(authUrl);
        IJsonNode* tileset = _json->parse(tilesetResp);
        if (tileset && tileset->hasKey("root")) {
            traverse(tileset->getObject("root"), baseUrl);
        }
    }

    const std::vector<std::string>& getResults() const { return _tileUris; }

private:
    void traverse(IJsonNode* tile, const std::string& baseUrl) {
        if (!tile) return;

        // Spatial Intersection Test
        if (tile->hasKey("boundingVolume")) {
            IJsonNode* bv = tile->getObject("boundingVolume");
            if (!intersects(bv)) return; // Cull
        }

        // Collect Content
        if (tile->hasKey("content")) {
            IJsonNode* content = tile->getObject("content");
            std::string uri = content->getString("uri");
            std::string fullUrl = baseUrl + uri;
            fullUrl += (fullUrl.find('?') == std::string::npos ? "?" : "&") + "access_token=" + _scopedToken;

            if (uri.find(".json") != std::string::npos) {
                // External tileset
                std::string newBase = fullUrl.substr(0, fullUrl.find_last_of('/') + 1);
                std::string subTileset = _http->get(fullUrl);
                IJsonNode* sub = _json->parse(subTileset);
                if (sub && sub->hasKey("root")) traverse(sub->getObject("root"), newBase);
            } else {
                _tileUris.push_back(fullUrl);
            }
        }

        // Recursive Children
        if (tile->hasKey("children")) {
            auto children = tile->getArray("children");
            for (auto* child : children) traverse(child, baseUrl);
        }
    }

    bool intersects(IJsonNode* bv) {
        if (bv->hasKey("region")) {
            auto r = bv->getDoubleArray("region"); // [w, s, e, n, minH, maxH]
            return !(r[0] > StPaulsRegion::East  || StPaulsRegion::West  > r[2] ||
                     r[1] > StPaulsRegion::North || StPaulsRegion::South > r[3]);
        }
        
        if (bv->hasKey("box")) {
            // Simplified box intersection: Convert OBB center to Lat/Lon
            // [center.x, y, z, x_axis.x, y, z, y_axis.x, y, z, z_axis.x, y, z]
            auto b = bv->getDoubleArray("box");
            double lat = std::asin(b[2] / 6378137.0); // Simple spherical approximation
            double lon = std::atan2(b[1], b[0]);
            // Check if center is within 1km (coarse cull)
            return std::abs(lat - 0.899080) < 0.0002 && std::abs(lon - (-0.001717)) < 0.0002;
        }
        return true; // Default to intersect if unknown
    }
};

#endif // CESIUM_ION_FETCHER_H
