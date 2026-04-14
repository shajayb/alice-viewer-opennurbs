#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// --- CURL WRAPPER (RAII) ---
class CurlHandle {
public:
    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle() { if (handle) curl_easy_cleanup(handle); }
    CURL* get() { return handle; }
private:
    CURL* handle;
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int main() {
    std::string apiKey = "AIzaSyAfA9eDGu_QOZx1wJjxTA3OroJXZFR26F0";
    curl_global_init(CURL_GLOBAL_ALL);

    CurlHandle curl;
    if (curl.get()) {
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;
        
        json payload = {
            {"contents", {{
                {"parts", {
                    {{"text", "Respond with 'Sandbox Test Success' if you receive this."}}
                } }
            }}}
        };

        std::string jsonStr = payload.dump();
        std::cout << "[APITest] Payload: " << jsonStr << std::endl;
        
        std::string responseBuffer;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, jsonStr.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBuffer);

        CURLcode res = curl_easy_perform(curl.get());
        
        long httpCode = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
        std::cout << "\n--- API RESPONSE (HTTP: " << httpCode << ") ---\n" << responseBuffer << "\n------------------------\n";

        if (res != CURLE_OK) {
            std::cerr << "[CURL ERROR] " << curl_easy_strerror(res) << std::endl;
        }
        
        curl_slist_free_all(headers);
    }

    curl_global_cleanup();
    return 0;
}
