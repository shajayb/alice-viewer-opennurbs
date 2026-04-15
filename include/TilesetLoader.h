#ifndef TILESET_LOADER_H
#define TILESET_LOADER_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "AliceNetwork.h"
#include "ApiKeyReader.h"
#include "AliceMath.h"

using json = nlohmann::json;

namespace Alice
{
    struct TilesetLoader
    {
        static std::string ConstructGoogleTilesetURL()
        {
            char key[256];
            if (!ApiKeyReader::GetGoogleKey(key, 256))
            {
                return "";
            }
            std::string url = "https://tile.googleapis.com/v1/3dtiles/root.json?key=";
            url += key;
            return url;
        }

        static bool FetchRootTileset(const std::string& url, json& out_tileset)
        {
            std::vector<uint8_t> buffer;
            if (!Network::Fetch(url.c_str(), buffer))
            {
                return false;
            }

            try
            {
                out_tileset = json::parse(buffer.begin(), buffer.end());
                return true;
            }
            catch (const std::exception& e)
            {
                fprintf(stderr, "[TilesetLoader] JSON parse error: %s\n", e.what());
                return false;
            }
        }

        struct RootMetadata
        {
            double transform[16];
            double geometricError;
            bool hasTransform;
        };

        static bool ExtractRootMetadata(const json& tileset, RootMetadata& out_meta)
        {
            if (!tileset.contains("root")) return false;

            const auto& root = tileset["root"];
            out_meta.geometricError = root.value("geometricError", 0.0);
            out_meta.hasTransform = false;

            if (root.contains("transform"))
            {
                const auto& trans = root["transform"];
                if (trans.is_array() && trans.size() == 16)
                {
                    for (int i = 0; i < 16; ++i)
                    {
                        out_meta.transform[i] = trans[i].get<double>();
                    }
                    out_meta.hasTransform = true;
                }
            }

            return true;
        }
    };
}

#endif // TILESET_LOADER_H
