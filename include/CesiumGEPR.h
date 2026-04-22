#ifndef CESIUM_GEPR_H
#define CESIUM_GEPR_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

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

#include "AliceJson.h"
#include "cgltf.h"
#include "AliceViewer.h"
#include "AliceMemory.h"
#include "ApiKeyReader.h"
#include <curl/curl.h>

#include <stb_image_write.h>

// --- Optimized DOD Math ---
namespace CesiumMath {
    struct DVec3 { double x, y, z; };
    struct DVec4 { double x, y, z, w; };
    struct DMat4 { double m[16]; };

    struct Plane {
        double a, b, c, d;
        void normalize() {
            double mag = sqrt(a * a + b * b + c * c);
            if (mag > 1e-9) { a /= mag; b /= mag; c /= mag; d /= mag; }
        }
        double distance(const DVec3& p) const { return a * p.x + b * p.y + c * p.z + d; }
    };

    struct Frustum {
        Plane planes[6];
        bool intersects(const AABB& box) const {
            if (!box.initialized) return true;
            for (int i = 0; i < 6; ++i) {
                DVec3 positive;
                positive.x = (planes[i].a >= 0) ? box.m_max.x : box.m_min.x;
                positive.y = (planes[i].b >= 0) ? box.m_max.y : box.m_min.y;
                positive.z = (planes[i].c >= 0) ? box.m_max.z : box.m_min.z;
                if (planes[i].distance(positive) < 0) return false;
            }
            return true;
        }
    };

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
        // ENU Matrix: East=Row0, North=Row1, Up=Row2
        m[0] = -sLon; m[1] = cLon; m[2] = 0.0; m[3] = 0.0;
        m[4] = -sLat * cLon; m[5] = -sLat * sLon; m[6] = cLat; m[7] = 0.0;
        m[8] = cLat * cLon; m[9] = cLat * sLon; m[10] = sLat; m[11] = 0.0;
        m[12] = 0.0; m[13] = 0.0; m[14] = 0.0; m[15] = 1.0;
    }
}

namespace CesiumNetwork {
    struct FetchBuffer { uint8_t* data; size_t size; size_t capacity; };
    static size_t wc(void* p, size_t s, size_t n, void* u) {
        size_t r = s * n;
        FetchBuffer* fb = (FetchBuffer*)u;
        if (fb->size + r > fb->capacity) return 0; // OOM protect
        memcpy(fb->data + fb->size, p, r);
        fb->size += r;
        return r;
    }
    static bool Fetch(const char* u, FetchBuffer& b, long* sc = 0, const char* bt = 0) {
        b.size = 0;
        CURL* c = curl_easy_init(); if (!c) return 0;
        curl_easy_setopt(c, CURLOPT_URL, u); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wc); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L); curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "Alice/1.0"); curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        struct curl_slist* headers = NULL;
        if (bt) { char auth[2048]; snprintf(auth, 2048, "Authorization: Bearer %s", bt); headers = curl_slist_append(headers, auth); curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers); }
        CURLcode r = curl_easy_perform(c); if (sc) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, sc);
        if (headers) curl_slist_free_all(headers); curl_easy_cleanup(c); return r == CURLE_OK;
    }
}

namespace CesiumGEPR {
    using namespace CesiumMath;

    struct RenderResources { GLuint vao, vbo, ebo; int count; };

    struct Tile {
        DMat4 transform;
        char contentUri[512];
        char baseUrl[512];
        Tile** children;
        uint32_t childrenCount;
        uint8_t* payload;
        size_t payloadSize;
        bool isLoaded;
        bool isCulled;
        double radius;
        double centerDist;
        double geometricError;
        AABB boundingAABB;
        AABB localAABB;
        RenderResources* rendererResources;
    };

    static uint32_t g_TilesLoadedThisFrame = 0;
    static DVec3 g_OriginEcef;
    static double g_EnuMatrix[16];

    struct Tileset {
        Tile* root;
        char baseUrl[1024];
        char token[2048];
        char apiKey[256];
        char sessionToken[1024];
        Tile** renderList;
        uint32_t renderListCount;
        uint32_t renderListCapacity;
        Frustum currentFrustum;

        void init() {
            root = nullptr; baseUrl[0] = '\0'; token[0] = '\0'; apiKey[0] = '\0'; sessionToken[0] = '\0';
            renderListCapacity = 65536;
            renderList = (Tile**)Alice::g_Arena.allocate(renderListCapacity * sizeof(Tile*));
            if (!renderList) { printf("[GEPR] FATAL: Failed to allocate renderList\n"); exit(1); }
            renderListCount = 0;
        }

        static void resolveUri(char* outUrl, size_t outSize, const char* base, const char* uri, const char* key, const char* session, const char* token) {
            char b[1024]; strncpy(b, base, 1023); b[1023] = '\0';
            char* q = strchr(b, '?'); if (q) *q = '\0';
            
            if (strstr(uri, "://")) { strncpy(outUrl, uri, outSize-1); outUrl[outSize-1] = '\0'; }
            else if (!uri[0]) { strncpy(outUrl, b, outSize-1); outUrl[outSize-1] = '\0'; }
            else if (uri[0] == '/') {
                char* hostEnd = strchr(b + 8, '/');
                if (hostEnd) { size_t hostLen = hostEnd - b; snprintf(outUrl, outSize, "%.*s%s", (int)hostLen, b, uri); }
                else { snprintf(outUrl, outSize, "%s%s", b, uri); }
            } else {
                size_t blen = strlen(b);
                if (blen > 0 && b[blen-1] != '/') snprintf(outUrl, outSize, "%s/%s", b, uri);
                else snprintf(outUrl, outSize, "%s%s", b, uri);
            }

            if (key && key[0] && !strstr(outUrl, "key=")) {
                strncat(outUrl, strchr(outUrl, '?') ? "&key=" : "?key=", outSize - strlen(outUrl) - 1);
                strncat(outUrl, key, outSize - strlen(outUrl) - 1);
            }
            if (session && session[0] && !strstr(outUrl, "session=")) {
                strncat(outUrl, strchr(outUrl, '?') ? "&session=" : "?session=", outSize - strlen(outUrl) - 1);
                strncat(outUrl, session, outSize - strlen(outUrl) - 1);
            }
            if (token && token[0] && !strstr(outUrl, "access_token=")) {
                strncat(outUrl, strchr(outUrl, '?') ? "&access_token=" : "?access_token=", outSize - strlen(outUrl) - 1);
                strncat(outUrl, token, outSize - strlen(outUrl) - 1);
            }
        }

        void traverse(Tile* node, int depth, bool parentRendered = false) {
            if (!node || depth > 100) return;

            // --- Dynamic Frustum Culling ---
            AABB testBox = node->localAABB.initialized ? node->localAABB : node->boundingAABB;
            bool visible = currentFrustum.intersects(testBox);

            // MANDATE: Christ the Redeemer (origin {0,0,0} in ENU) must NEVER be culled.
            if (testBox.initialized) {
                float d2 = testBox.center().x * testBox.center().x + testBox.center().y * testBox.center().y + testBox.center().z * testBox.center().z;
                if (d2 < 40000.0f) visible = true; // 200m radius
            }
            if (!visible) return;

            // --- Screen-Space Error Calculation ---
            bool shouldRefine = false;
            double sse = 0.0;
            if (node->geometricError > 0.0) {
                V3 center_v3 = node->boundingAABB.center();
                DVec3 bc = { (double)center_v3.x, (double)center_v3.y, (double)center_v3.z };
                AliceViewer* av = AliceViewer::instance();
                int w, h; glfwGetFramebufferSize(av->window, &w, &h);
                M4 v_cam = av->camera.getViewMatrix();
                DMat4 vMat = {v_cam.m[0],v_cam.m[1],v_cam.m[2],v_cam.m[3],v_cam.m[4],v_cam.m[5],v_cam.m[6],v_cam.m[7],v_cam.m[8],v_cam.m[9],v_cam.m[10],v_cam.m[11],v_cam.m[12],v_cam.m[13],v_cam.m[14],v_cam.m[15]};
                DVec4 viewPos = dmat4_mul_vec4(vMat, {bc.x, bc.y, bc.z, 1.0});
                double distance = std::max(-viewPos.z, 1e-7);
                M4 p_cam = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
                DMat4 projMat = {p_cam.m[0],p_cam.m[1],p_cam.m[2],p_cam.m[3],p_cam.m[4],p_cam.m[5],p_cam.m[6],p_cam.m[7],p_cam.m[8],p_cam.m[9],p_cam.m[10],p_cam.m[11],p_cam.m[12],p_cam.m[13],p_cam.m[14],p_cam.m[15]};
                DVec4 centerNdc = dmat4_mul_vec4(projMat, {0.0, 0.0, -distance, 1.0});
                centerNdc.x /= centerNdc.w; centerNdc.y /= centerNdc.w; centerNdc.z /= centerNdc.w; centerNdc.w = 1.0;
                DVec4 errorOffsetNdc = dmat4_mul_vec4(projMat, {0.0, node->geometricError, -distance, 1.0});
                errorOffsetNdc.x /= errorOffsetNdc.w; errorOffsetNdc.y /= errorOffsetNdc.w; errorOffsetNdc.z /= errorOffsetNdc.w; errorOffsetNdc.w = 1.0;
                double ndcError = errorOffsetNdc.y - centerNdc.y;
                sse = std::abs(ndcError) * h / 2.0;
                if (sse > 16.0) shouldRefine = true;
            } else if (node->childrenCount > 0) {
                shouldRefine = true;
            }
            if (depth > 20) shouldRefine = false; // Safety clamp

            bool traverseChildren = false;
            bool anyVisibleChild = false;
            bool allVisibleChildrenLoaded = true;

            if (shouldRefine && node->childrenCount > 0) {
                traverseChildren = true;
                for (uint32_t i=0; i<node->childrenCount; ++i) {
                    if (currentFrustum.intersects(node->children[i]->boundingAABB)) {
                        anyVisibleChild = true;
                        if (!node->children[i]->isLoaded) allVisibleChildrenLoaded = false;
                    }
                }
            }

            bool skipPayload = false;
            if (shouldRefine && node->childrenCount > 0 && !strstr(node->contentUri, ".json")) {
                skipPayload = true; // Aggressively skip straight to the last available level
                node->isLoaded = true; // Mark as loaded so parent LOD can be released
            }

            if (!skipPayload && node->contentUri[0] != '\0' && !node->isLoaded && g_TilesLoadedThisFrame < 2) {
                static uint8_t fetchRawBuffer[16 * 1024 * 1024]; // 16MB max per tile
                CesiumNetwork::FetchBuffer buffer = { fetchRawBuffer, 0, sizeof(fetchRawBuffer) };
                long sc = 0;
                char url[1024];
                resolveUri(url, 1024, node->baseUrl, node->contentUri, apiKey, sessionToken, token);
                if (depth < 6) { printf("[GEPR] Fetching (depth %d)...\n", depth); fflush(stdout); }
                if (CesiumNetwork::Fetch(url, buffer, &sc, token[0] ? token : nullptr)) {
                    g_TilesLoadedThisFrame++;
                    if (sc >= 200 && sc < 300) {
                        if (buffer.size > 0 && buffer.data[0] == '{') {
                            auto j = AliceJson::parse(buffer.data, buffer.size);
                            if (j.type != AliceJson::J_NULL) {
                                char nextBase[1024];
                                resolveUri(nextBase, 1024, node->baseUrl, node->contentUri, nullptr, nullptr, nullptr);
                                char* q = strchr(nextBase, '?'); if (q) *q = '\0';
                                char* lastSlash = strrchr(nextBase, '/');
                                if (lastSlash) *(lastSlash + 1) = '\0';
                                parseNode(node, j["root"], node->transform, nextBase, depth);
                            }
                        } else {
                            node->payloadSize = buffer.size;
                            node->payload = (uint8_t*)Alice::g_Arena.allocate(node->payloadSize);
                            if (node->payload) memcpy(node->payload, buffer.data, node->payloadSize);
                        }
                    }
                    node->isLoaded = true;
                }
            }

            bool renderThis = (node->isLoaded && node->payload);
            
            // LOD Conflict Resolution
            if (traverseChildren && anyVisibleChild && allVisibleChildrenLoaded) {
                renderThis = false; // Children will fully replace this
            }
            if (parentRendered) {
                renderThis = false; // Prevent Z-fighting when a parent is actively rendered
            }

            if (renderThis) {
                if (renderListCount < renderListCapacity) renderList[renderListCount++] = node;
            }

            if (traverseChildren) {
                for (uint32_t i = 0; i < node->childrenCount; ++i) {
                    traverse(node->children[i], depth + 1, parentRendered || renderThis);
                }
            }
        }

        void parseNode(Tile* tile, const AliceJson::JsonValue& jNode, DMat4 parentTransform, const char* currentBase, int depth) {
            if (depth > 100) return;
            memset(tile, 0, sizeof(Tile));
            strncpy(tile->baseUrl, currentBase, 511); tile->baseUrl[511] = '\0';          tile->localAABB = AABB();
            tile->boundingAABB = AABB();
            DMat4 localTransform = dmat4_identity();
            if (jNode.contains("transform")) {
                auto arr = jNode["transform"];
                for (int i = 0; i < 16; ++i) localTransform.m[i] = (double)arr[i];
            }
            tile->transform = dmat4_mul(parentTransform, localTransform);
            
            if (jNode.contains("geometricError")) {
                tile->geometricError = (double)jNode["geometricError"];
            } else {
                tile->geometricError = 0.0;
            }
            
            if (jNode.contains("boundingVolume")) {
                if (jNode["boundingVolume"].contains("region")) {
                    auto r = jNode["boundingVolume"]["region"];
                    double west = (double)r[0], south = (double)r[1], east = (double)r[2], north = (double)r[3];
                    double minAlt = (double)r[4], maxAlt = (double)r[5];
                    // Roughly approximate region with corners
                    double lats[2] = {south, north}, lons[2] = {west, east}, alts[2] = {minAlt, maxAlt};
                    for(int i=0;i<2;++i)for(int j=0;j<2;++j)for(int k=0;k<2;++k) {
                        DVec3 ecef = lla_to_ecef(lats[i], lons[j], alts[k]);
                        double dx = ecef.x - g_OriginEcef.x, dy = ecef.y - g_OriginEcef.y, dz = ecef.z - g_OriginEcef.z;
                        float ex = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                        float ny = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                        float uz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                        tile->boundingAABB.expand({ex, ny, uz});
                    }
                } else if (jNode["boundingVolume"].contains("box") || jNode["boundingVolume"].contains("sphere")) {
                    if (jNode["boundingVolume"].contains("box")) {
                        auto b = jNode["boundingVolume"]["box"];
                        DVec3 center = {(double)b[0], (double)b[1], (double)b[2]};
                        DVec3 axes[3] = {{(double)b[3], (double)b[4], (double)b[5]}, {(double)b[6], (double)b[7], (double)b[8]}, {(double)b[9], (double)b[10], (double)b[11]}};
                        for (int i=0; i<8; ++i) {
                            DVec4 lp = {center.x, center.y, center.z, 1.0};
                            lp.x += ((i&1)?1:-1)*axes[0].x + ((i&2)?1:-1)*axes[1].x + ((i&4)?1:-1)*axes[2].x;
                            lp.y += ((i&1)?1:-1)*axes[0].y + ((i&2)?1:-1)*axes[1].y + ((i&4)?1:-1)*axes[2].y;
                            lp.z += ((i&1)?1:-1)*axes[0].z + ((i&2)?1:-1)*axes[1].z + ((i&4)?1:-1)*axes[2].z;
                            DVec4 wp = dmat4_mul_vec4(tile->transform, lp);
                            double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                            float ex = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                            float ny = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                            float uz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                            tile->boundingAABB.expand({ex, ny, uz});
                        }
                    } else {
                        auto s = jNode["boundingVolume"]["sphere"];
                        DVec4 center = {(double)s[0], (double)s[1], (double)s[2], 1.0};
                        double radius = (double)s[3];
                        DVec4 wp = dmat4_mul_vec4(tile->transform, center);
                        double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                        float ex = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                        float ny = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                        float uz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                        tile->boundingAABB.expand({ex - (float)radius, ny - (float)radius, uz - (float)radius});
                        tile->boundingAABB.expand({ex + (float)radius, ny + (float)radius, uz + (float)radius});
                    }
                }
            }

            if (jNode.contains("content")) {
                std::string uri;
                if (jNode["content"].contains("uri")) uri = jNode["content"]["uri"].get<std::string>();
                else if (jNode["content"].contains("url")) uri = jNode["content"]["url"].get<std::string>();
                if (!uri.empty()) {
                    strncpy(tile->contentUri, uri.c_str(), 511);
                    if (sessionToken[0] == '\0') {
                        size_t sPos = uri.find("session=");
                        if (sPos != std::string::npos) {
                            size_t sEnd = uri.find('&', sPos);
                            std::string sess = uri.substr(sPos + 8, sEnd == std::string::npos ? std::string::npos : sEnd - (sPos + 8));
                            strncpy(sessionToken, sess.c_str(), 1023);
                        }
                    }
                } else {
                    tile->isLoaded = true; // No payload needed
                }
            } else {
                tile->isLoaded = true; // No content node
            }
            if (jNode.contains("children")) {
                auto childrenArr = jNode["children"];
                tile->childrenCount = (uint32_t)childrenArr.size();
                tile->children = (Tile**)Alice::g_Arena.allocate(tile->childrenCount * sizeof(Tile*));
                if (tile->children) {
                    for (uint32_t i = 0; i < tile->childrenCount; ++i) {
                        tile->children[i] = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                        if (tile->children[i]) {
                            memset(tile->children[i], 0, sizeof(Tile));
                            parseNode(tile->children[i], childrenArr[i], tile->transform, currentBase, depth + 1);
                        }
                    }
                }
            }
        }
    };

    static Tileset g_Tileset;
    static GLuint g_Program = 0;
    static uint32_t g_FrameCount = 0;
    static uint64_t g_TotalFrustumVertices = 0;
    static float* g_ScratchVbo = nullptr;
    static uint32_t* g_ScratchEbo = nullptr;

    static void* cgltf_alloc(void* user, size_t size) { return Alice::g_Arena.allocate(size); }
    static void cgltf_free_cb(void* user, void* ptr) {}

    static void initShaders() {
        const char* vs = "#version 400 core\nlayout(location=0)in vec3 p;uniform mat4 uMVP;uniform mat4 uV;out vec3 vVP;void main(){vVP=(uV*vec4(p,1.0)).xyz;gl_Position=uMVP*vec4(p,1.0);}";
        const char* fs = "#version 400 core\nout vec4 f;in vec3 vVP;void main(){vec3 N=normalize(cross(dFdx(vVP),dFdy(vVP)));if(!gl_FrontFacing)N=-N;float d=max(dot(N,normalize(vec3(0.5,0.8,0.6))),0.0);f=vec4(vec3(0.85)*d + vec3(0.15),1.0);}";
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(cgltf_node* node, const DMat4& tileTransform, const DMat4& gltfTransform, const DVec3& rtcCenter, float* vbo, uint32_t* ebo, uint32_t& vIdx, uint32_t& eIdx) {
        DMat4 m = gltfTransform;
        if (node->has_matrix) { DMat4 nm; for(int i=0;i<16;++i) nm.m[i] = (double)node->matrix[i]; m = dmat4_mul(gltfTransform, nm); }
        else {
            if (node->has_translation) m = dmat4_translate(m, {(double)node->translation[0], (double)node->translation[1], (double)node->translation[2]});
            if (node->has_rotation) m = dmat4_mul(m, dmat4_from_quat((double*)node->rotation));
            if (node->has_scale) m = dmat4_scale(m, {(double)node->scale[0], (double)node->scale[1], (double)node->scale[2]});
        }
        if (node->mesh) {
            for (size_t i=0; i<node->mesh->primitives_count; ++i) {
                cgltf_primitive* p = &node->mesh->primitives[i];
                if (p->has_draco_mesh_compression) { printf("DRACO SKIPPED\n"); continue; }
                cgltf_accessor *pa=0; for (size_t k=0; k<p->attributes_count; ++k) if (p->attributes[k].type == cgltf_attribute_type_position) pa = p->attributes[k].data;
                if (!pa) continue;
                uint32_t offset = vIdx;
                for (size_t k=0; k<pa->count; ++k) {
                    float pos[3]; cgltf_accessor_read_float(pa, k, pos, 3);
                    DVec4 lp = dmat4_mul_vec4(m, {(double)pos[0], (double)pos[1], (double)pos[2], 1.0});
                    
                    // glTF is Y-Up by default, 3D Tiles is Z-Up.
                    // Y_UP_TO_Z_UP: x -> x, y -> -z, z -> y
                    double up_x = lp.x;
                    double up_y = -lp.z;
                    double up_z = lp.y;
                    
                    DVec4 wp = dmat4_mul_vec4(tileTransform, {up_x + rtcCenter.x, up_y + rtcCenter.y, up_z + rtcCenter.z, 1.0});
                    double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                    float ex = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                    float ny = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                    float uz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                    vbo[vIdx*6+0] = ex; vbo[vIdx*6+1] = ny; vbo[vIdx*6+2] = uz; 
                    vIdx++;
                }
                if (p->type == cgltf_primitive_type_triangle_strip) {
                    size_t icount = p->indices ? p->indices->count : pa->count;
                    for (size_t k = 0; k + 2 < icount; ++k) {
                        uint32_t i0 = p->indices ? (uint32_t)cgltf_accessor_read_index(p->indices, k) : (uint32_t)k;
                        uint32_t i1 = p->indices ? (uint32_t)cgltf_accessor_read_index(p->indices, k+1) : (uint32_t)(k+1);
                        uint32_t i2 = p->indices ? (uint32_t)cgltf_accessor_read_index(p->indices, k+2) : (uint32_t)(k+2);
                        if (i0 == i1 || i1 == i2 || i2 == i0) continue;
                        ebo[eIdx++] = i0 + offset;
                        if (k % 2 == 0) { ebo[eIdx++] = i1 + offset; ebo[eIdx++] = i2 + offset; }
                        else { ebo[eIdx++] = i2 + offset; ebo[eIdx++] = i1 + offset; }
                    }
                } else {
                    if (p->indices) for (size_t k=0; k<p->indices->count; ++k) ebo[eIdx++] = (uint32_t)cgltf_accessor_read_index(p->indices, k) + offset;
                    else for (size_t k=0; k<pa->count; ++k) ebo[eIdx++] = (uint32_t)k + offset;
                }
            }
        }
        for (size_t i=0; i<node->children_count; ++i) processNode(node->children[i], tileTransform, m, rtcCenter, vbo, ebo, vIdx, eIdx);
    }

    static void setup() {
        printf("[GEPR] setup started\n"); fflush(stdout);
        if (!Alice::g_Arena.memory) Alice::g_Arena.init(2048ULL*1024ULL*1024ULL); 
        initShaders(); g_Tileset.init();
        
        if (!g_ScratchVbo) g_ScratchVbo = (float*)malloc(120000*6*4);
        if (!g_ScratchEbo) g_ScratchEbo = (uint32_t*)malloc(250000*4);
        
        double lat = -22.951916 * 0.0174532925, lon = -43.210487 * 0.0174532925;
        g_OriginEcef = lla_to_ecef(lat, lon, 710.0); denu_matrix(g_EnuMatrix, lat, lon);
        
        char ionToken[2048];
        if (!Alice::ApiKeyReader::GetCesiumToken(ionToken, 2048)) { printf("[GEPR] No Cesium Token\n"); return; }
        
        char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/2275207/endpoint?access_token=%s", ionToken);
        static uint8_t hsBuf[1024*1024]; CesiumNetwork::FetchBuffer handshake = { hsBuf, 0, sizeof(hsBuf) };
        long sc=0;
        if (CesiumNetwork::Fetch(url, handshake, &sc)) {
            auto jhs = AliceJson::parse(handshake.data, handshake.size);
            std::string turl, atok;
            if (jhs.contains("url")) turl = jhs["url"].get<std::string>();
            else if (jhs.contains("options") && jhs["options"].contains("url")) turl = jhs["options"]["url"].get<std::string>();
            if (jhs.contains("accessToken")) atok = jhs["accessToken"].get<std::string>();
            if (!atok.empty()) strncpy(g_Tileset.token, atok.c_str(), 2047);
            if (!turl.empty()) {
                size_t kPos = turl.find("key=");
                if (kPos != std::string::npos) {
                    size_t kEnd = turl.find('&', kPos);
                    std::string key = turl.substr(kPos + 4, kEnd == std::string::npos ? std::string::npos : kEnd - (kPos + 4));
                    strncpy(g_Tileset.apiKey, key.c_str(), 255);
                }
                strncpy(g_Tileset.baseUrl, turl.substr(0, turl.find_last_of('/')+1).c_str(), 1023);
                static uint8_t tsBuf[16*1024*1024]; CesiumNetwork::FetchBuffer ts = { tsBuf, 0, sizeof(tsBuf) };
                if (CesiumNetwork::Fetch(turl.c_str(), ts, &sc, g_Tileset.token[0] ? g_Tileset.token : nullptr)) {
                    auto jts = AliceJson::parse(ts.data, ts.size);
                    if (jts.type != AliceJson::J_NULL) {
                        g_Tileset.root = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                        memset(g_Tileset.root, 0, sizeof(Tile));
                        g_Tileset.parseNode(g_Tileset.root, jts["root"], dmat4_identity(), g_Tileset.baseUrl, 0);
                    }
                }
            }
        }
        printf("[GEPR] setup finished\n"); fflush(stdout);
    }

    static void update(float dt) {
        AliceViewer* av = AliceViewer::instance();
        if (g_FrameCount == 0) {
            av->camera.focusPoint = {0,0,0}; av->camera.distance = 500.0f; av->camera.pitch = 0.2f; av->camera.yaw = 0.0f;
            printf("[GEPR] Camera Reset: distance=500, pitch=0.2\n"); fflush(stdout);
        }

        // --- Frustum Extraction ---
        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        M4 v = av->camera.getViewMatrix(), p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        
        Frustum& f = g_Tileset.currentFrustum;
        f.planes[0] = {mvp[3]+mvp[0], mvp[7]+mvp[4], mvp[11]+mvp[8], mvp[15]+mvp[12]}; // Left
        f.planes[1] = {mvp[3]-mvp[0], mvp[7]-mvp[4], mvp[11]-mvp[8], mvp[15]-mvp[12]}; // Right
        f.planes[2] = {mvp[3]+mvp[1], mvp[7]+mvp[5], mvp[11]+mvp[9], mvp[15]+mvp[13]}; // Bottom
        f.planes[3] = {mvp[3]-mvp[1], mvp[7]-mvp[5], mvp[11]-mvp[9], mvp[15]-mvp[13]}; // Top
        f.planes[4] = {mvp[3]+mvp[2], mvp[7]+mvp[6], mvp[11]+mvp[10], mvp[15]+mvp[14]}; // Near
        f.planes[5] = {mvp[3]-mvp[2], mvp[7]-mvp[6], mvp[11]-mvp[10], mvp[15]-mvp[14]}; // Far
        for(int i=0; i<6; ++i) f.planes[i].normalize();

        g_TilesLoadedThisFrame = 0;
        g_Tileset.renderListCount = 0; if (g_Tileset.root) g_Tileset.traverse(g_Tileset.root, 0);
    }

    static void draw() {
        uint32_t frameIdx = g_FrameCount++;
        if (g_FrameCount > 1000) { printf("[GEPR] Timeout. Exiting.\n"); exit(0); }
        AliceViewer* av = AliceViewer::instance();
        
        // --- AABB Accumulation ---
        av->m_sceneAABB = AABB();
        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            AABB& tb = g_Tileset.renderList[i]->localAABB.initialized ? g_Tileset.renderList[i]->localAABB : g_Tileset.renderList[i]->boundingAABB;
            if (tb.initialized) {
                av->m_sceneAABB.expand(tb.m_min);
                av->m_sceneAABB.expand(tb.m_max);
            }
        }

        av->backColor = {0,0,0};
        if (frameIdx % 20 == 0) { printf("[GEPR] Frame %u, RenderList: %u\n", frameIdx, g_Tileset.renderListCount); fflush(stdout); }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClearDepth(0.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL);
        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        M4 v = av->camera.getViewMatrix(), p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        glUseProgram(g_Program); glUniformMatrix4fv(glGetUniformLocation(g_Program, "uMVP"), 1, 0, mvp);
        glUniformMatrix4fv(glGetUniformLocation(g_Program, "uV"), 1, 0, v.m);
        
        g_TotalFrustumVertices = 0;
        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            Tile* t = g_Tileset.renderList[i];
            if (!t->rendererResources && t->payload) {
                cgltf_options opt = {cgltf_file_type_glb}; opt.memory.alloc_func = cgltf_alloc; opt.memory.free_func = cgltf_free_cb;
                cgltf_data* data = 0;
                uint8_t* glbPayload = t->payload; size_t glbSize = t->payloadSize;
                DVec3 rtcCenter = {0,0,0};
                if (glbSize >= 28 && memcmp(glbPayload, "b3dm", 4) == 0) {
                    uint32_t ftj = *(uint32_t*)(glbPayload + 12), ftb = *(uint32_t*)(glbPayload + 16);
                    uint32_t btj = *(uint32_t*)(glbPayload + 20), btb = *(uint32_t*)(glbPayload + 24);
                    if (ftj > 0) {
                        auto ft = AliceJson::parse(glbPayload + 28, ftj);
                        if (ft.contains("RTC_CENTER")) {
                            auto rtc = ft["RTC_CENTER"];
                            rtcCenter = {(double)rtc[0], (double)rtc[1], (double)rtc[2]};
                            printf("[DEBUG] RTC_CENTER = %f, %f, %f\n", rtcCenter.x, rtcCenter.y, rtcCenter.z);
                        }
                    }
                    uint32_t headerLen = 28 + ftj + ftb + btj + btb;
                    if (headerLen < glbSize) { glbPayload += headerLen; glbSize -= headerLen; }
                }
                if (cgltf_parse(&opt, glbPayload, glbSize, &data) == cgltf_result_success) {
                    cgltf_load_buffers(&opt, data, 0);
                    RenderResources* res = (RenderResources*)Alice::g_Arena.allocate(sizeof(RenderResources));
                    if (res && g_ScratchVbo && g_ScratchEbo) {
                        memset(res, 0, sizeof(RenderResources));
                        uint32_t vIdx=0, eIdx=0;
                        if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], t->transform, dmat4_identity(), rtcCenter, g_ScratchVbo, g_ScratchEbo, vIdx, eIdx);
                        for (uint32_t vIdx2 = 0; vIdx2 < vIdx; ++vIdx2) t->localAABB.expand({g_ScratchVbo[vIdx2*6+0], g_ScratchVbo[vIdx2*6+1], g_ScratchVbo[vIdx2*6+2]});
                        glGenVertexArrays(1, &res->vao); glBindVertexArray(res->vao);
                        glGenBuffers(1, &res->vbo); glBindBuffer(GL_ARRAY_BUFFER, res->vbo); glBufferData(GL_ARRAY_BUFFER, vIdx*6*4, g_ScratchVbo, GL_STATIC_DRAW);
                        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                        glGenBuffers(1, &res->ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, eIdx*4, g_ScratchEbo, GL_STATIC_DRAW);
                        res->count = eIdx; t->rendererResources = res;
                    }
                    cgltf_free(data);
                }
            }
            if (t->rendererResources) { glBindVertexArray(t->rendererResources->vao); glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); g_TotalFrustumVertices += (uint64_t)t->rendererResources->count; }
        }
        // Removed frameScene() call to maintain focus on the statue
        if (frameIdx == 600) {
            unsigned char* px = (unsigned char*)malloc(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
            long long hits = 0; for(int i=0; i<w*h; ++i) if(px[i*3]>15||px[i*3+1]>15||px[i*3+2]>15) hits++;
            printf("frustum_vertex_count: %llu\n", g_TotalFrustumVertices);
            printf("pixel_coverage_percentage: %.2f%%\n", (double)hits/(w*h)*100.0);
            stbi_flip_vertically_on_write(1); stbi_write_png("production_framebuffer.png", w, h, 3, px, w*3);
            printf("[GEPR] Mission Accomplished. Exiting.\n"); fflush(stdout);
            free(px); exit(0);
        }
    }
}

#ifdef CESIUM_GEPR_RUN_TEST
extern "C" void setup() { CesiumGEPR::setup(); }
extern "C" void update(float dt) { CesiumGEPR::update(dt); }
extern "C" void draw() { CesiumGEPR::draw(); }
#endif

#endif // CESIUM_GEPR_H
