# Google Earth Asset Retrieval from Cesium ion (Asset 2275207)

This document summarizes the minimal necessary code and logic required to retrieve the Google Photorealistic 3D Tiles (Asset ID 2275207) from Cesium ion, based on the `cesium-native` codebase.

## 1. Network Discovery: Endpoint Request

The discovery process starts with a request to the Cesium ion API to find the asset's streaming endpoint and obtain a session-specific access token.

- **Endpoint URL**: `https://api.cesium.com/v1/assets/2275207/endpoint?access_token=<YOUR_ION_TOKEN>`
- **Method**: `GET`

## 2. JSON Parsing: Endpoint Response

The response is a JSON object containing the asset's actual URL, a session-specific `accessToken`, and the asset type.

### Example Response
```json
{
  "type": "3DTILES",
  "url": "https://assets.cesium.com/2275207/tileset.json",
  "accessToken": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "attributions": [
    {
      "html": "Map data \u00a92023 Google",
      "collapsible": false
    }
  ]
}
```

### Minimal Parsing Logic (C++)
The codebase uses `rapidjson` for parsing. The primary logic is found in `Cesium3DTilesSelection/src/CesiumIonTilesetLoader.cpp`.

```cpp
#include <rapidjson/document.h>

// Parse the ion response data
rapidjson::Document ionResponse;
ionResponse.Parse(reinterpret_cast<const char*>(data.data()), data.size());

if (!ionResponse.HasParseError()) {
    std::string type = ionResponse["type"].GetString();
    std::string url = ionResponse["url"].GetString();
    std::string sessionToken = ionResponse["accessToken"].GetString();
}
```

## 3. Metadata Retrieval: tileset.json

Once the URL and session token are extracted, the client requests the asset's metadata (e.g., `tileset.json`).

- **URL**: `url` (from the endpoint response)
- **Method**: `GET`
- **Header**: `Authorization: Bearer <sessionToken>`

## 4. Tileset Parsing

The `TilesetJsonLoader` class (specifically `TilesetJsonLoader::createLoader`) handles the retrieval and parsing of the `tileset.json` file.

### Minimal Tileset Parsing Logic
```cpp
rapidjson::Document tilesetDoc;
tilesetDoc.Parse(reinterpret_cast<const char*>(tilesetData.data()), tilesetData.size());

if (tilesetDoc.HasMember("root")) {
    const auto& root = tilesetDoc["root"];
    // Navigate the tile hierarchy
    if (root.HasMember("content")) {
        std::string contentUri = root["content"]["uri"].GetString();
        // Request tile content using the same session token
    }
}
```

## Key Architectural Components

1.  **`CesiumIonTilesetLoader`**: Orchestrates the endpoint discovery and manages the transition to the asset-specific tileset URL.
2.  **`CesiumIonAssetAccessor`**: A specialized asset accessor that handles the inclusion of the session token in all requests and automatically refreshes it if a `401 Unauthorized` error is received.
3.  **`TilesetJsonLoader`**: Responsible for the recursive parsing of the `tileset.json` and managing the resulting tile tree.
4.  **`rapidjson`**: The primary library used for fast and safe JSON parsing throughout the discovery and metadata phases.
------------------------------

# C++ Specification: Cesium ion Asset Retrieval (Asset 2275207)

This document provides a dependency-free C++ pseudo-code sketch to instruct an agent on how to retrieve the Google Photorealistic 3D Tiles asset from Cesium ion.

## 1. High-Level Requirements
*   **Asset ID**: `2275207`
*   **Initial Token**: Your Cesium ion Access Token.
*   **Discovery Base**: `https://api.cesium.com/v1`

## 2. Implementation Sketch (C++ Pseudo-Code)

```cpp
#include <string>
#include <vector>
#include <iostream>

// --- Abstract Interfaces (Assumed available in target environment) ---
struct HttpResponse { int statusCode; std::string body; };
struct Json { /* Generic JSON Accessor */ };
HttpResponse http_get(std::string url, std::vector<std::pair<std::string, std::string>> headers = {});
Json parse_json(std::string body);
std::string resolve_uri(std::string base, std::string relative);

// --- Core Retrieval Logic ---

struct AssetSession {
    std::string streamingUrl;
    std::string sessionToken;
};

// Step 1: Discover the Asset Endpoint
AssetSession discoverAsset(int64_t assetId, std::string ionToken) {
    std::string discoveryUrl = "https://api.cesium.com/v1/assets/" + 
                               std::to_string(assetId) + 
                               "/endpoint?access_token=" + ionToken;
    
    HttpResponse resp = http_get(discoveryUrl);
    Json data = parse_json(resp.body);

    // CRITICAL: Extract the specific accessToken for THIS session
    return { 
        data["url"].asString(), 
        data["accessToken"].asString() 
    };
}

// Step 2: Recursive Traversal of Tileset
void processTile(const Json& tile, const std::string& baseUrl, const std::string& sessionToken) {
    auto authHeader = { std::make_pair("Authorization", "Bearer " + sessionToken) };

    // A. Handle Content (Geometry/Textures)
    if (tile.contains("content")) {
        std::string uri = tile["content"]["uri"].asString();
        std::string absoluteUrl = resolve_uri(baseUrl, uri);

        // If content is an external tileset (.json), recurse into its root
        if (uri.find(".json") != std::string::npos) {
            HttpResponse subResp = http_get(absoluteUrl, authHeader);
            Json subTileset = parse_json(subResp.body);
            processTile(subTileset["root"], absoluteUrl, sessionToken);
        } else {
            // Fetch binary payload (glTF, B3DM, etc.)
            HttpResponse binary = http_get(absoluteUrl, authHeader);
            save_to_disk(uri, binary.body);
        }
    }

    // B. Handle Children
    if (tile.contains("children")) {
        for (const auto& child : tile["children"].asArray()) {
            processTile(child, baseUrl, sessionToken);
        }
    }
}

int main() {
    std::string ionToken = "YOUR_TOKEN_HERE";
    AssetSession session = discoverAsset(2275207, ionToken);

    // Initial metadata fetch
    HttpResponse rootResp = http_get(session.streamingUrl, 
        {{"Authorization", "Bearer " + session.sessionToken}});
    Json rootTileset = parse_json(rootResp.body);

    processTile(rootTileset["root"], session.streamingUrl, session.sessionToken);
    
    return 0;
}
```

## 3. Critical Implementation Notes

1.  **Authorization Swap**: The `ionToken` is used **only** for the first discovery call. For every subsequent request (including the initial `tileset.json` and all `.glb` or `.json` sub-files), you must use the `accessToken` from the discovery response as a `Bearer` token.
2.  **Relative URI Resolution**: Inside the JSON, most paths are relative. Use the URL of the current `.json` file as the base for resolving the next URI.
3.  **Google Attribution**: The discovery response contains a `type` (usually `3DTILES`) and an `attributions` array. To be compliant, any client must display the HTML strings provided in that array (e.g., "Map data ©202X Google").
4.  **Implicit Tiling**: Be aware that modern 3D Tiles (like Google's) may use `implicitTiling`. If the root tile contains an `implicitTiling` object instead of explicit `children`, the agent must implement Quadtree/Octree traversal logic to generate tile URLs programmatically.
