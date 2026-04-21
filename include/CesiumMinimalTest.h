#ifndef CESIUM_MINIMAL_TEST_H
#define CESIUM_MINIMAL_TEST_H

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <cmath>
#include <algorithm>
#include <fstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <nlohmann/json.hpp>
#include "cgltf.h"
#include "AliceViewer.h"
#include "AliceMemory.h"
#include <curl/curl.h>
#include <stb_image_write.h>

// --- Optimized DOD Math ---
namespace CesiumMath {
    struct DVec3 { double x, y, z; };
    struct DVec4 { double x, y, z, w; };
    struct DMat4 { double m[16]; };

    inline DMat4 dmat4_identity() {
        DMat4 res = {0};
        res.m[0] = res.m[5] = res.m[10] = res.m[15] = 1.0;
        return res;
    }

    inline DMat4 dmat4_mul(const DMat4& a, const DMat4& b) {
        DMat4 res;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                res.m[i + j * 4] = a.m[i + 0 * 4] * b.m[0 + j * 4] +
                                 a.m[i + 1 * 4] * b.m[1 + j * 4] +
                                 a.m[i + 2 * 4] * b.m[2 + j * 4] +
                                 a.m[i + 3 * 4] * b.m[3 + j * 4];
            }
        }
        return res;
    }

    inline DVec4 dmat4_mul_vec4(const DMat4& m, const DVec4& v) {
        return {
            m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z + m.m[12]*v.w,
            m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z + m.m[13]*v.w,
            m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14]*v.w,
            m.m[3]*v.x + m.m[7]*v.y + m.m[11]*v.z + m.m[15]*v.w
        };
    }

    inline DMat4 dmat4_translate(const DMat4& m, const DVec3& v) {
        DMat4 res = m;
        res.m[12] = m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z + m.m[12];
        res.m[13] = m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z + m.m[13];
        res.m[14] = m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14];
        res.m[15] = m.m[3]*v.x + m.m[7]*v.y + m.m[11]*v.z + m.m[15];
        return res;
    }

    inline DMat4 dmat4_scale(const DMat4& m, const DVec3& v) {
        DMat4 res = m;
        res.m[0] *= v.x; res.m[1] *= v.x; res.m[2] *= v.x; res.m[3] *= v.x;
        res.m[4] *= v.y; res.m[5] *= v.y; res.m[6] *= v.y; res.m[7] *= v.y;
        res.m[8] *= v.z; res.m[9] *= v.z; res.m[10] *= v.z; res.m[11] *= v.z;
        return res;
    }

    inline DMat4 dmat4_from_quat(const double q[4]) {
        DMat4 res = dmat4_identity();
        double x = q[0], y = q[1], z = q[2], w = q[3];
        res.m[0] = 1.0 - 2.0*y*y - 2.0*z*z; res.m[4] = 2.0*x*y - 2.0*w*z; res.m[8] = 2.0*x*z + 2.0*w*y;
        res.m[1] = 2.0*x*y + 2.0*w*z; res.m[5] = 1.0 - 2.0*x*x - 2.0*z*z; res.m[9] = 2.0*y*z - 2.0*w*x;
        res.m[2] = 2.0*x*z - 2.0*w*y; res.m[6] = 2.0*y*z + 2.0*w*x; res.m[10] = 1.0 - 2.0*x*x - 2.0*y*y;
        return res;
    }

    inline DVec3 lla_to_ecef(double lat, double lon, double alt) {
        const double a = 6378137.0;
        const double e2 = 0.0066943799901413165; 
        double sLat = sin(lat), cLat = cos(lat);
        double sLon = sin(lon), cLon = cos(lon);
        double N = a / sqrt(1.0 - e2 * sLat * sLat);
        return { (N + alt) * cLat * cLon, (N + alt) * cLat * sLon, (N * (1.0 - e2) + alt) * sLat };
    }

    inline void denu_matrix(double* m, double lat, double lon) {
        double sLat = sin(lat), cLat = cos(lat);
        double sLon = sin(lon), cLon = cos(lon);
        m[0] = -sLon; m[1] = cLon; m[2] = 0.0; m[3] = 0.0;
        m[4] = -sLat * cLon; m[5] = -sLat * sLon; m[6] = cLat; m[7] = 0.0;
        m[8] = cLat * cLon; m[9] = cLat * sLon; m[10] = sLat; m[11] = 0.0;
        m[12] = 0.0; m[13] = 0.0; m[14] = 0.0; m[15] = 1.0;
    }

    inline void ecef_to_lla(const DVec3& p, double& lat, double& lon, double& alt) {
        const double a = 6378137.0;
        const double e2 = 0.0066943799901413165; 
        double p_dist = sqrt(p.x * p.x + p.y * p.y);
        lat = atan2(p.z, p_dist * (1.0 - e2));
        for (int i = 0; i < 5; ++i) {
            double sLat = sin(lat);
            double N = a / sqrt(1.0 - e2 * sLat * sLat);
            lat = atan2(p.z + e2 * N * sLat, p_dist);
        }
        lon = atan2(p.y, p.x);
        double sLat = sin(lat);
        double N = a / sqrt(1.0 - e2 * sLat * sLat);
        alt = p_dist / cos(lat) - N;
    }

    inline void dmat4_inverse_to_float(float* res, const double* m) {
        double inv[16], det;
        inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
        inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
        inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
        inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
        inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
        inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
        inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
        inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
        inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
        inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
        inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
        inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
        inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
        inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
        inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
        inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
        det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
        if (det != 0) { det = 1.0 / det; for (int i = 0; i < 16; i++) res[i] = (float)(inv[i] * det); }
    }

    inline void mat4_inverse(float* res, const float* m) {
        float inv[16], det;
        inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
        inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
        inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
        inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
        inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
        inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
        inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
        inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
        inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
        inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
        inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
        inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
        inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
        inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
        inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
        inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
        det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
        if (det != 0) { det = 1.0f / det; for (int i = 0; i < 16; i++) res[i] = inv[i] * det; }
    }
}

// --- Absorbed Network ---
namespace CesiumNetwork {
    static size_t wc(void* p, size_t s, size_t n, void* u) {
        size_t r = s * n; auto* m = (std::vector<uint8_t>*)u;
        size_t o = m->size(); m->resize(o + r);
        memcpy(m->data() + o, p, r); return r;
    }
    static bool Fetch(const char* u, std::vector<uint8_t>& b, long* sc = 0, const char* bt = 0) {
        CURL* c = curl_easy_init(); if (!c) return 0;
        curl_easy_setopt(c, CURLOPT_URL, u); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wc); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L); curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "Alice/1.0");
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        struct curl_slist* headers = NULL;
        if (bt) { char auth[2048]; snprintf(auth, 2048, "Authorization: Bearer %s", bt); headers = curl_slist_append(headers, auth); curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers); }
        CURLcode r = curl_easy_perform(c); if (sc) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, sc);
        if (headers) curl_slist_free_all(headers); curl_easy_cleanup(c); return r == CURLE_OK;
    }
}

// --- Absorbed API Key ---
namespace CesiumApiKey {
    static bool GetCesiumToken(char* outBuffer, size_t bufferSize) {
        printf("[Cesium] Checking for API Key...\n"); fflush(stdout);
        if (const char* envToken = std::getenv("CESIUM_ION_TOKEN")) {
            if (strlen(envToken) < bufferSize) { 
                strncpy(outBuffer, envToken, bufferSize); outBuffer[bufferSize - 1] = '\0'; 
                printf("[Cesium] Using token from Environment Variable.\n"); fflush(stdout);
                return true; 
            }
        }
        const char* paths[] = { "API_KEYS.txt", "../API_KEYS.txt" };
        for (const char* path : paths) {
            std::ifstream file(path);
            if (!file.is_open()) {
                printf("[Cesium] Could not open %s\n", path); fflush(stdout);
                continue;
            }
            printf("[Cesium] Reading %s...\n", path); fflush(stdout);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            size_t keyPos = content.find("CESIUM_ION_KEYS");
            if (keyPos != std::string::npos) {
                size_t eqPos = content.find('=', keyPos);
                if (eqPos != std::string::npos) {
                    size_t firstQuote = content.find('"', eqPos);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = content.find('"', firstQuote + 1);
                        if (secondQuote != std::string::npos) {
                            std::string token = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                            // Strip any whitespace/newlines from the token
                            token.erase(std::remove(token.begin(), token.end(), '\n'), token.end());
                            token.erase(std::remove(token.begin(), token.end(), '\r'), token.end());
                            if (token.length() > 10 && token.length() < bufferSize) { 
                                strncpy(outBuffer, token.c_str(), bufferSize); outBuffer[bufferSize - 1] = '\0'; 
                                printf("[Cesium] Token successfully extracted from %s (starts with %.4s..., length %zu)\n", path, outBuffer, token.length()); fflush(stdout);
                                return true; 
                            }
                        }
                    }
                }
            }
        }
        printf("[Cesium] [ERROR] No valid Cesium Ion Token found in environment or API_KEYS.txt\n"); fflush(stdout);
        return false;
    }
}

namespace CesiumMinimal {
    using namespace CesiumMath;

    struct HttpResponse { int statusCode; std::vector<uint8_t> data; std::string error; };

    class AliceCurlClient {
    public:
        HttpResponse get(const std::string& url, const std::string& token = "") {
            HttpResponse res; long sc = 0;
            bool ok = CesiumNetwork::Fetch(url.c_str(), res.data, &sc, token.empty() ? nullptr : token.c_str());
            res.statusCode = (int)sc; 
            if (!ok) {
                res.error = "CURL Fetch Failed";
                printf("[Cesium] [NET_ERROR] URL: %s | Status: %ld\n", url.c_str(), sc); fflush(stdout);
            } else if (sc >= 400) {
                res.error = "HTTP Error " + std::to_string(sc);
                printf("[Cesium] [HTTP_FAIL] URL: %s | Status: %ld\n", url.c_str(), sc); fflush(stdout);
            }
            return res;
        }
    };

    struct IonAssetEndpoint { std::string url; std::string accessToken; };

    class IonDiscovery {
    public:
        static std::optional<IonAssetEndpoint> discover(AliceCurlClient& client, int64_t assetId, const std::string& ionToken) {
            std::string url = "https://api.cesium.com/v1/assets/" + std::to_string(assetId) + "/endpoint?access_token=" + ionToken;
            printf("[Cesium] Handshake starting for Asset %lld...\n", assetId); fflush(stdout);
            HttpResponse response = client.get(url);
            if (response.statusCode != 200) {
                printf("[Cesium] [HANDSHAKE_FAILED] Asset %lld | Status: %d | Response: %s\n", assetId, response.statusCode, response.data.empty() ? "EMPTY" : (const char*)response.data.data());
                fflush(stdout);
                g_NetStats.apiConnected = false;
                return std::nullopt;
            }
            try {
                auto j = nlohmann::json::parse(response.data.begin(), response.data.end());
                IonAssetEndpoint endpoint; endpoint.url = j["url"]; endpoint.accessToken = j["accessToken"];
                printf("[Cesium] Handshake Success. Tileset URL: %s\n", endpoint.url.c_str()); fflush(stdout);
                g_NetStats.apiConnected = true;
                return endpoint;
            } catch (...) { 
                printf("[Cesium] [PARSE_ERROR] Failed to parse Ion discovery JSON.\n"); fflush(stdout);
                g_NetStats.apiConnected = false;
                return std::nullopt; 
            }
        }
    };

    struct BoundingVolume { DVec3 centerECEF; double radius; bool valid = false; };

    struct RenderResources {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        int count = 0;
    };

    struct Tile {
        BoundingVolume bounds;
        char contentUri[256];
        Tile** children;
        uint32_t childrenCount;
        uint8_t* payload;
        size_t payloadSize;
        bool isLoaded;
        RenderResources* rendererResources;
        DMat4 transform;
    };

    struct Tileset {
        Tile* root;
        char baseUrl[512];
        char token[2048];
        AliceCurlClient* client;
        Tile** renderList;
        uint32_t renderListCount;
        uint32_t renderListCapacity;

        void init(AliceCurlClient* c) {
            root = nullptr;
            baseUrl[0] = '\0';
            token[0] = '\0';
            client = c;
            renderListCapacity = 2048;
            renderList = (Tile**)Alice::g_Arena.allocate(renderListCapacity * sizeof(Tile*));
            renderListCount = 0;
        }

        void updateView(const DVec3& camPos, double radiusLimit) {
            renderListCount = 0;
            if (root) traverse(root, camPos, radiusLimit);
        }

        void traverse(Tile* node, const DVec3& camPos, double radiusLimit) {
            if (node->bounds.valid) {
                double dx = node->bounds.centerECEF.x - camPos.x;
                double dy = node->bounds.centerECEF.y - camPos.y;
                double dz = node->bounds.centerECEF.z - camPos.z;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                if (dist > node->bounds.radius + radiusLimit) return;
            }
            if (node->contentUri[0] != '\0' && !node->isLoaded) {
                printf("[Alice] Fetching: %s\n", node->contentUri); fflush(stdout);
                auto res = client->get(std::string(baseUrl) + node->contentUri, token);
                if (res.statusCode == 200) {
                    if (strstr(node->contentUri, ".json")) {
                        try {
                            auto j = nlohmann::json::parse(res.data.begin(), res.data.end());
                            parseNode(node, j["root"], baseUrl, node->transform);
                        } catch (...) {}
                    } else {
                        node->payloadSize = res.data.size();
                        node->payload = (uint8_t*)Alice::g_Arena.allocate(node->payloadSize);
                        memcpy(node->payload, res.data.data(), node->payloadSize);
                    }
                    node->isLoaded = true;
                }
            }
            if (node->isLoaded && node->payload) {
                if (renderListCount < renderListCapacity) renderList[renderListCount++] = node;
            }
            for (uint32_t i = 0; i < node->childrenCount; ++i) traverse(node->children[i], camPos, radiusLimit);
        }

        void parseNode(Tile* tile, const nlohmann::json& jNode, const char* currentBase, const DMat4& parentTransform) {
            DMat4 localTransform = CesiumMath::dmat4_identity();
            if (jNode.contains("transform")) {
                auto arr = jNode["transform"];
                for (int i = 0; i < 16; ++i) localTransform.m[i] = (double)arr[i];
            }
            tile->transform = CesiumMath::dmat4_mul(parentTransform, localTransform);

            if (jNode.contains("boundingVolume")) {
                auto bv = jNode["boundingVolume"];
                if (bv.contains("region")) {
                    auto arr = bv["region"];
                    double rw = arr[0], rs = arr[1], re = arr[2], rn = arr[3];
                    tile->bounds.centerECEF = lla_to_ecef((rs+rn)*0.5, (rw+re)*0.5, 0.0);
                    double latD = (rn-rs)*6378137.0; double lonD = (re-rw)*6378137.0*cos((rs+rn)*0.5);
                    tile->bounds.radius = sqrt(latD*latD + lonD*lonD)*0.5;
                    tile->bounds.valid = true;
                } else if (bv.contains("box")) {
                    auto arr = bv["box"];
                    DVec4 localCenter = {arr[0], arr[1], arr[2], 1.0};
                    DVec4 worldCenter = dmat4_mul_vec4(tile->transform, localCenter);
                    tile->bounds.centerECEF = {worldCenter.x, worldCenter.y, worldCenter.z};
                    double ux = arr[3], uy = arr[4], uz = arr[5];
                    tile->bounds.radius = sqrt(ux*ux + uy*uy + uz*uz) * 2.0;
                    tile->bounds.valid = true;
                }
            }
            if (jNode.contains("content")) {
                std::string uri;
                if (jNode["content"].contains("uri")) uri = jNode["content"]["uri"].get<std::string>();
                else if (jNode["content"].contains("url")) uri = jNode["content"]["url"].get<std::string>();
                if (!uri.empty()) strncpy(tile->contentUri, uri.c_str(), 255);
            }
            if (jNode.contains("children")) {
                auto childrenArr = jNode["children"];
                tile->childrenCount = (uint32_t)childrenArr.size();
                tile->children = (Tile**)Alice::g_Arena.allocate(tile->childrenCount * sizeof(Tile*));
                for (uint32_t i = 0; i < tile->childrenCount; ++i) {
                    tile->children[i] = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                    memset(tile->children[i], 0, sizeof(Tile));
                    tile->children[i]->transform = CesiumMath::dmat4_identity();
                    parseNode(tile->children[i], childrenArr[i], currentBase, tile->transform);
                }
            }
        }
    };
}

// --- Integration & Test ---
namespace Alice {
    struct TestLocation {
        int64_t assetId;
        double lat, lon;
        V3 focusPoint;
        float cameraDistance;
        float pitch, yaw;
        const char* name;
    };

    static TestLocation g_Locations[] = {
        { 96188,  51.5138, -0.0984, {0, -40, 60}, 450.0f, 0.55f, 4.2f, "St. Paul's" },
        { 96188,  48.8584,  2.2945, {0, -50, 150}, 800.0f, 0.40f, 3.1f, "Eiffel Tower" },
        { 96188,  40.6892, -74.0445, {0, -20, 60}, 400.0f, 0.35f, 1.2f, "Statue of Liberty" }
    };
    static int g_CurrentLocation = -1;
    static uint32_t g_FrameCount = 0;
    static uint32_t g_LastTeleportFrame = 0;
    static bool g_Started = false;
    static CesiumMinimal::AliceCurlClient g_Client;
    static CesiumMinimal::Tileset g_Tileset;
    static CesiumMath::DVec3 g_OriginEcef;
    static double g_EnuMatrix[16];
    static GLuint g_Program = 0;
    static char g_IonToken[2048] = {0};

    static void* cgltf_alloc(void* user, size_t size) { return Alice::g_Arena.allocate(size); }
    static void cgltf_free_cb(void* user, void* ptr) {}

    static void initShaders() {
        const char* vs = "#version 400 core\nlayout(location=0)in vec3 p;uniform mat4 uMVP;uniform mat4 uV;out vec3 vVP;void main(){vVP=(uV*vec4(p,1.0)).xyz;gl_Position=uMVP*vec4(p,1.0);}";
        const char* fs = "#version 400 core\nout vec4 f;in vec3 vVP;void main(){vec3 N=normalize(cross(dFdx(vVP),dFdy(vVP)));if(!gl_FrontFacing)N=-N;float d=max(dot(N,normalize(vec3(0.5,0.8,0.6))),0.2);f=vec4(vec3(0.85)*d,1.0);}";
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(cgltf_node* node, const CesiumMath::DMat4& transform, const CesiumMath::DVec3& rtc, std::vector<float>& vbo, std::vector<uint32_t>& ebo) {
        CesiumMath::DMat4 m = transform;
        if (node->has_matrix) { CesiumMath::DMat4 nm; for(int i=0;i<16;++i) nm.m[i] = (double)node->matrix[i]; m = CesiumMath::dmat4_mul(transform, nm); }
        else {
            if (node->has_translation) m = CesiumMath::dmat4_translate(m, {(double)node->translation[0], (double)node->translation[1], (double)node->translation[2]});
            if (node->has_rotation) m = CesiumMath::dmat4_mul(m, CesiumMath::dmat4_from_quat((double*)node->rotation));
            if (node->has_scale) m = CesiumMath::dmat4_scale(m, {(double)node->scale[0], (double)node->scale[1], (double)node->scale[2]});
        }
        if (node->mesh) {
            for (size_t i=0; i<node->mesh->primitives_count; ++i) {
                cgltf_primitive* p = &node->mesh->primitives[i];
                cgltf_accessor *pa=0;
                for (size_t k=0; k<p->attributes_count; ++k) {
                    if (p->attributes[k].type == cgltf_attribute_type_position) pa = p->attributes[k].data;
                }
                uint32_t offset = (uint32_t)(vbo.size()/6);
                for (size_t k=0; k<pa->count; ++k) {
                    float pos[3]; cgltf_accessor_read_float(pa, k, pos, 3);
                    CesiumMath::DVec4 wp = CesiumMath::dmat4_mul_vec4(m, {(double)pos[0] + rtc.x, (double)pos[1] + rtc.y, (double)pos[2] + rtc.z, 1.0});
                    double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                    vbo.push_back((float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz));
                    vbo.push_back((float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz));
                    vbo.push_back((float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz));
                    vbo.push_back(0.0f); vbo.push_back(0.0f); vbo.push_back(0.0f);
                }
                if (p->indices) for (size_t k=0; k<p->indices->count; ++k) ebo.push_back((uint32_t)cgltf_accessor_read_index(p->indices, k) + offset);
            }
        }
        for (size_t i=0; i<node->children_count; ++i) processNode(node->children[i], m, rtc, vbo, ebo);
    }

    static void teleportTo(int index) {
        if (index < 0 || index >= 3) return;
        TestLocation& loc = g_Locations[index];
        printf("[Alice] Teleporting to Location %d: %s (Asset %lld)\n", index, loc.name, loc.assetId);
        fflush(stdout);

        bool locationChanged = (g_CurrentLocation != index);
        g_CurrentLocation = index;
        g_LastTeleportFrame = g_FrameCount;

        if (locationChanged) {
            printf("[Alice] Location changed. Resetting Arena and re-initializing.\n");
            fflush(stdout);
            
            double latRad = loc.lat * 0.0174532925, lonRad = loc.lon * 0.0174532925;
            g_OriginEcef = CesiumMath::lla_to_ecef(latRad, lonRad, 0.0);
            CesiumMath::denu_matrix(g_EnuMatrix, latRad, lonRad);

            g_Arena.reset();
            g_Tileset.init(&g_Client);

            auto endpoint = CesiumMinimal::IonDiscovery::discover(g_Client, loc.assetId, g_IonToken);
            if (endpoint) {
                auto res = g_Client.get(endpoint->url, endpoint->accessToken);
                if (res.statusCode == 200) {
                    auto j = nlohmann::json::parse(res.data.begin(), res.data.end());
                    std::string base = endpoint->url.substr(0, endpoint->url.find_last_of('/')+1);
                    strncpy(g_Tileset.baseUrl, base.c_str(), 511);
                    strncpy(g_Tileset.token, endpoint->accessToken.c_str(), 2047);
                    g_Tileset.root = (CesiumMinimal::Tile*)g_Arena.allocate(sizeof(CesiumMinimal::Tile));
                    memset(g_Tileset.root, 0, sizeof(CesiumMinimal::Tile));
                    g_Tileset.root->transform = CesiumMath::dmat4_identity();
                    g_Tileset.parseNode(g_Tileset.root, j["root"], g_Tileset.baseUrl, CesiumMath::dmat4_identity());
                }
            }
        }

        AliceViewer* av = AliceViewer::instance();
        av->camera.focusPoint = loc.focusPoint;
        av->camera.distance = loc.cameraDistance;
        av->camera.pitch = loc.pitch;
        av->camera.yaw = loc.yaw;
    }

    static void setup() {
        printf("[Alice] setup started\n"); fflush(stdout);
        if (!g_Arena.memory) g_Arena.init(512*1024*1024);
        initShaders();
        CesiumApiKey::GetCesiumToken(g_IonToken, 2048);
        teleportTo(0);
        g_Started = true;
        printf("[Alice] setup complete\n"); fflush(stdout);
    }

    static void update(float dt) {
        if (!g_Started) return;
        AliceViewer* av = AliceViewer::instance();
        M4 view = av->camera.getViewMatrix();
        float inv[16]; CesiumMath::mat4_inverse(inv, view.m);
        CesiumMath::DVec3 camEcef = {
            g_OriginEcef.x + g_EnuMatrix[0]*inv[12] + g_EnuMatrix[4]*inv[13] + g_EnuMatrix[8]*inv[14],
            g_OriginEcef.y + g_EnuMatrix[1]*inv[12] + g_EnuMatrix[5]*inv[13] + g_EnuMatrix[9]*inv[14],
            g_OriginEcef.z + g_EnuMatrix[2]*inv[12] + g_EnuMatrix[6]*inv[13] + g_EnuMatrix[10]*inv[14]
        };
        g_Tileset.updateView(camEcef, 2000.0);
    }

    static void draw() {
        if (!g_Started) setup();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClearDepth(0.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        AliceViewer* av = AliceViewer::instance(); int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        M4 v = av->camera.getViewMatrix(), p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        glUseProgram(g_Program); glUniformMatrix4fv(glGetUniformLocation(g_Program, "uMVP"), 1, 0, mvp); glUniformMatrix4fv(glGetUniformLocation(g_Program, "uV"), 1, 0, v.m);
        for (uint32_t i = 0; i < g_Tileset.renderListCount; ++i) {
            auto* tile = g_Tileset.renderList[i];
            if (!tile->rendererResources && tile->payload) {
                int glbOff = -1;
                for(size_t k=0; k<tile->payloadSize-4; ++k) if(memcmp(tile->payload+k, "glTF", 4) == 0) { glbOff = (int)k; break; }
                if (glbOff >= 0) {
                    cgltf_options opt = {cgltf_file_type_glb}; opt.memory.alloc_func = cgltf_alloc; opt.memory.free_func = cgltf_free_cb;
                    cgltf_data* data = 0; if (cgltf_parse(&opt, tile->payload+glbOff, tile->payloadSize-glbOff, &data) == cgltf_result_success) {
                        cgltf_load_buffers(&opt, data, 0);
                        tile->rendererResources = (CesiumMinimal::RenderResources*)g_Arena.allocate(sizeof(CesiumMinimal::RenderResources));
                        memset(tile->rendererResources, 0, sizeof(CesiumMinimal::RenderResources));
                        glGenVertexArrays(1, &tile->rendererResources->vao); glBindVertexArray(tile->rendererResources->vao);
                        std::vector<float> vbo; std::vector<uint32_t> ebo;
                        CesiumMath::DMat4 yup = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
                        if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], CesiumMath::dmat4_mul(tile->transform, yup), {0,0,0}, vbo, ebo);
                        if (!vbo.empty()) {
                            glGenBuffers(1, &tile->rendererResources->vbo); glGenBuffers(1, &tile->rendererResources->ebo);
                            glBindBuffer(GL_ARRAY_BUFFER, tile->rendererResources->vbo); glBufferData(GL_ARRAY_BUFFER, vbo.size()*4, vbo.data(), GL_STATIC_DRAW);
                            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, 24, (void*)12);
                            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile->rendererResources->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size()*4, ebo.data(), GL_STATIC_DRAW);
                            tile->rendererResources->count = (int)ebo.size();
                            printf("[Alice] Tile ready for GPU: %s (%d vertices)\n", tile->contentUri, (int)(vbo.size()/6)); fflush(stdout);
                        }
                        cgltf_free(data);
                    }
                }
            }
            if (tile->rendererResources && tile->rendererResources->vao) {
                glBindVertexArray(tile->rendererResources->vao);
                glDrawElements(GL_TRIANGLES, tile->rendererResources->count, GL_UNSIGNED_INT, 0);
            }
        }
        
        g_FrameCount++;
        uint32_t framesSinceTeleport = g_FrameCount - g_LastTeleportFrame;
        
        if (framesSinceTeleport == 600) {
            char filename[256];
            snprintf(filename, 256, "framebuffer_%d.png", g_CurrentLocation);
            unsigned char* px = (unsigned char*)malloc(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
            
            // Mandatory Pixel Coverage Check
            int coveredPixels = 0;
            for (int i = 0; i < w * h; ++i) {
                if (px[i*3] != 25 || px[i*3+1] != 25 || px[i*3+2] != 25) coveredPixels++;
            }
            float coverage = (float)coveredPixels / (float)(w * h) * 100.0f;
            printf("[CesiumMinimal] Pixel Coverage: %.2f%%\n", coverage);
            
            stbi_flip_vertically_on_write(1); stbi_write_png(filename, w, h, 3, px, w*3); free(px);
            printf("[CesiumMinimal] Saved %s\n", filename); fflush(stdout);
            
            if (g_CurrentLocation < 2) {
                teleportTo(g_CurrentLocation + 1);
            } else {
                printf("[CesiumMinimal] All captures complete.\n"); fflush(stdout);
                glfwSetWindowShouldClose(av->window, 1);
            }
        }
    }
}

#ifdef CESIUM_MINIMAL_TEST_RUN_TEST
extern "C" void setup() { Alice::setup(); }
extern "C" void update(float dt) { Alice::update(dt); }
extern "C" void draw() { Alice::draw(); }
#endif

#endif // CESIUM_MINIMAL_TEST_H
