#ifndef ALICE_NETWORK_H
#define ALICE_NETWORK_H

#include <curl/curl.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

namespace Alice
{
    struct Network
    {
        static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp)
        {
            size_t realsize = size * nmemb;
            std::vector<uint8_t>* mem = (std::vector<uint8_t>*)userp;
            size_t oldsize = mem->size();
            mem->resize(oldsize + realsize);
            memcpy(mem->data() + oldsize, contents, realsize);
            return realsize;
        }

        static size_t header_callback(void* contents, size_t size, size_t nmemb, void* userp)
        {
            size_t realsize = size * nmemb;
            std::string* headers = (std::string*)userp;
            headers->append((char*)contents, realsize);
            return realsize;
        }

        static bool Fetch(const char* url, std::vector<uint8_t>& buffer, long* out_status_code = nullptr, std::string* out_body = nullptr, std::string* out_headers = nullptr, long timeout_seconds = 0)
        {
            std::string sUrl = url;
            if (sUrl.find("http://") == 0 || sUrl.find("https://") == 0)
            {
                CURL* curl = curl_easy_init();
                if (!curl) return false;

                curl_easy_setopt(curl, CURLOPT_URL, url);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buffer);
                
                if (out_headers)
                {
                    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
                    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)out_headers);
                }

                if (timeout_seconds > 0)
                {
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
                }

                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "AliceViewer/1.0");

                CURLcode res = curl_easy_perform(curl);
                
                if (out_status_code)
                {
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, out_status_code);
                }
                
                if (out_body && !buffer.empty())
                {
                    *out_body = std::string((char*)buffer.data(), buffer.size());
                }

                curl_easy_cleanup(curl);
                return (res == CURLE_OK);
            }
            else
            {
                FILE* f = fopen(url, "rb");
                if (!f) return false;
                fseek(f, 0, SEEK_END);
                size_t size = ftell(f);
                fseek(f, 0, SEEK_SET);
                buffer.resize(size);
                fread(buffer.data(), 1, size, f);
                fclose(f);
                return true;
            }
        }
    };
}

#endif // ALICE_NETWORK_H
