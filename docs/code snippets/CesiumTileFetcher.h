#ifndef CESIUM_TILE_FETCHER_H
#define CESIUM_TILE_FETCHER_H

#include <string>
#include <vector>
#include <iostream>

/**
 * @file CesiumTileFetcher.h
 * @brief Minimal, dependency-free C++ logic to communicate with Cesium ion, traverse a tileset, 
 *        and filter tiles for a 500x500m region around St Paul's Cathedral in London.
 * 
 * SUMMARY & DECOUPLING STRATEGY:
 * - Ion Endpoint Resolution: Extracted the URL format `https://api.cesium.com/v1/assets/{id}/endpoint`.
 *   The response provides the root `tileset.json` URL and an access token to append to all subsequent requests.
 * - Spatial Traversal: Avoided the complex OBB/Culling/Frustum systems in `cesium-native`. 
 *   Instead, implemented a direct 2D geographic bounding region intersection (min/max Radians) 
 *   to filter tiles that fall within the target 500x500m area.
 * - Dependency Injection: Network (HTTP GET) and JSON parsing are abstracted behind minimal 
 *   interfaces (`IHttpClient` and `IJsonNode`), completely decoupling the logic from `CesiumAsync`, 
 *   `libcurl`, and `rapidjson`.
 */

// --- Geographic Target (St Paul's Cathedral, London) ---
// Lat: 51.5138 N, Lon: 0.0984 W
// 500x500m Area (+/- 250m from center). Earth Radius ~ 6371000m.
// dLat = 250 / 6371000 = 0.000039 rad
// dLon = 250 / (6371000 * cos(51.5138 deg)) = 0.000063 rad
struct TargetRegion {
    double west  = -0.001717 - 0.000063; // -0.00178 rad
    double south =  0.899080 - 0.000039; //  0.89904 rad
    double east  = -0.001717 + 0.000063; // -0.00165 rad
    double north =  0.899080 + 0.000039; //  0.89912 rad
};

// --- Minimal Interfaces for Decoupling ---
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    // Returns raw response body (e.g., JSON string or binary payload)
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

// --- Tile Fetching & Traversal Logic ---
class CesiumTileFetcher {
private:
    IHttpClient* _httpClient;
    IJsonParser* _jsonParser;
    std::string _ionAccessToken;
    TargetRegion _target;
    std::vector<std::string> _collectedTileUrls;

public:
    CesiumTileFetcher(IHttpClient* httpClient, IJsonParser* jsonParser) 
        : _httpClient(httpClient), _jsonParser(jsonParser) {}

    /**
     * @brief 1. Resolves the Ion Asset ID to a root tileset URL.
     */
    void fetchAssetAndTraverse(uint32_t assetId, const std::string& token) {
        std::string endpointUrl = "https://api.cesium.com/v1/assets/" + 
                                  std::to_string(assetId) + 
                                  "/endpoint?access_token=" + token;

        std::string response = _httpClient->get(endpointUrl);
        IJsonNode* root = _jsonParser->parse(response);

        if (root && root->hasKey("url")) {
            std::string tilesetUrl = root->getString("url");
            // The endpoint returns a specific access token for the asset's bucket
            _ionAccessToken = root->hasKey("accessToken") ? root->getString("accessToken") : token;
            
            // 2. Fetch the root tileset.json
            std::string tilesetJsonUrl = tilesetUrl;
            if (tilesetJsonUrl.find('?') == std::string::npos) {
                tilesetJsonUrl += "?access_token=" + _ionAccessToken;
            } else {
                tilesetJsonUrl += "&access_token=" + _ionAccessToken;
            }

            std::string baseUrl = tilesetUrl.substr(0, tilesetUrl.find_last_of('/') + 1);
            fetchTileset(tilesetJsonUrl, baseUrl);
        }
    }

    const std::vector<std::string>& getCollectedTileUrls() const {
        return _collectedTileUrls;
    }

private:
    /**
     * @brief 2. Fetches a tileset.json and begins traversal.
     */
    void fetchTileset(const std::string& tilesetUrl, const std::string& baseUrl) {
        std::string response = _httpClient->get(tilesetUrl);
        IJsonNode* tileset = _jsonParser->parse(response);

        if (tileset && tileset->hasKey("root")) {
            traverseTile(tileset->getObject("root"), baseUrl);
        }
    }

    /**
     * @brief 3. Recursively checks tile intersections and collects URIs.
     */
    void traverseTile(IJsonNode* tile, const std::string& baseUrl) {
        if (!tile) return;

        // Check intersection
        IJsonNode* boundingVolume = tile->getObject("boundingVolume");
        if (boundingVolume && boundingVolume->hasKey("region")) {
            std::vector<double> region = boundingVolume->getDoubleArray("region");
            // region: [west, south, east, north, minHeight, maxHeight]
            if (region.size() >= 4 && !intersectsTarget(region[0], region[1], region[2], region[3])) {
                // Cull this tile and its children as it doesn't overlap St Paul's 500x500m area
                return; 
            }
        }

        // If it intersects, check if it has content (a payload to download)
        if (tile->hasKey("content")) {
            IJsonNode* content = tile->getObject("content");
            if (content->hasKey("uri")) {
                std::string uri = content->getString("uri");
                std::string fullUrl = baseUrl + uri;
                
                // Append Ion access token for downloading the payload (.b3dm / .glb / child .json)
                if (fullUrl.find('?') == std::string::npos) {
                    fullUrl += "?access_token=" + _ionAccessToken;
                } else {
                    fullUrl += "&access_token=" + _ionAccessToken;
                }

                // If the URI points to another tileset, traverse it. Otherwise, collect the payload.
                if (uri.find(".json") != std::string::npos) {
                    std::string newBaseUrl = fullUrl.substr(0, fullUrl.find_last_of('/') + 1);
                    fetchTileset(fullUrl, newBaseUrl);
                } else {
                    // It's a binary payload (b3dm, i3dm, glb, etc.)
                    _collectedTileUrls.push_back(fullUrl);
                }
            }
        }

        // Recursively traverse children
        if (tile->hasKey("children")) {
            std::vector<IJsonNode*> children = tile->getArray("children");
            for (IJsonNode* child : children) {
                traverseTile(child, baseUrl);
            }
        }
    }

    /**
     * @brief 4. 2D Geographic Bounding Box Intersection.
     */
    bool intersectsTarget(double tileWest, double tileSouth, double tileEast, double tileNorth) const {
        // Standard AABB intersection in radians. 
        // (Assuming neither bounding box crosses the antimeridian, which is true for London).
        if (tileWest > _target.east || _target.west > tileEast) {
            return false;
        }
        if (tileSouth > _target.north || _target.south > tileNorth) {
            return false;
        }
        return true;
    }
};

#endif // CESIUM_TILE_FETCHER_H
