#include <cstdio>
#include <string>
#include <fstream>
#include <filesystem>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int main()
{
    std::filesystem::remove("executor_console.log");
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, "https://raw.githubusercontent.com/CesiumGS/3d-tiles-samples/main/1.0/TilesetWithDiscreteLOD/tileset.json");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return 1;
        }

        try
        {
            json tileset = json::parse(readBuffer);
            if (tileset.contains("root") && tileset["root"].contains("geometricError"))
            {
                double geometricError = tileset["root"]["geometricError"];
                
                std::ofstream logFile("executor_console.log", std::ios::app);
                if (logFile.is_open()) {
                    logFile << "[GEOMETRIC_ERROR_EXTRACTED]: " << geometricError << "\n";
                    logFile.close();
                } else {
                    fprintf(stderr, "Failed to open executor_console.log for writing.\n");
                }
                
                printf("[GEOMETRIC_ERROR_EXTRACTED]: %f\n", geometricError);
            }
            else
            {
                fprintf(stderr, "JSON does not contain root.geometricError\n");
                return 1;
            }
        }
        catch (json::parse_error& e)
        {
            fprintf(stderr, "JSON parse error: %s\n", e.what());
            return 1;
        }
    }
    return 0;
}
