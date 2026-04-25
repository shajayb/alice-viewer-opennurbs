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
#include <functional>
#include <filesystem>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "AliceJson.h"
#include "cgltf.h"
#include "AliceViewer.h"
#include "AliceMemory.h"
#include "ApiKeyReader.h"
#include <curl/curl.h>
#include <opennurbs.h>

#include <stb_image_write.h>
#include <stb_image.h>

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
        if (bt && bt[0]) { char auth[2048]; snprintf(auth, 2048, "Authorization: Bearer %s", bt); headers = curl_slist_append(headers, auth); curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers); }
        CURLcode r = curl_easy_perform(c); if (sc) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, sc);
        if (headers) curl_slist_free_all(headers); curl_easy_cleanup(c); return r == CURLE_OK;
    }

    struct AsyncRequest {
        CURL* handle; curl_slist* headers;
        uint8_t* bufferData; size_t bufferCapacity; FetchBuffer buffer;
        std::function<void(long, const FetchBuffer&)> callback;
    };
    static CURLM* g_Multi = nullptr;
    static std::vector<AsyncRequest*> g_AsyncRequests;

    static void FetchAsync(const char* u, size_t maxBytes, const char* bt, std::function<void(long, const FetchBuffer&)> cb) {
        if (!g_Multi) g_Multi = curl_multi_init();
        AsyncRequest* req = new AsyncRequest();
        req->bufferData = (uint8_t*)malloc(maxBytes);
        req->bufferCapacity = maxBytes;
        req->buffer = { req->bufferData, 0, maxBytes };
        req->callback = cb;
        req->handle = curl_easy_init();
        
        curl_easy_setopt(req->handle, CURLOPT_URL, u);
        curl_easy_setopt(req->handle, CURLOPT_WRITEFUNCTION, wc);
        curl_easy_setopt(req->handle, CURLOPT_WRITEDATA, &req->buffer);
        curl_easy_setopt(req->handle, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(req->handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(req->handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(req->handle, CURLOPT_USERAGENT, "Alice/1.0");
        curl_easy_setopt(req->handle, CURLOPT_ACCEPT_ENCODING, "");
        req->headers = NULL;
        if (bt && bt[0]) {
            char auth[2048]; snprintf(auth, 2048, "Authorization: Bearer %s", bt);
            req->headers = curl_slist_append(req->headers, auth);
            curl_easy_setopt(req->handle, CURLOPT_HTTPHEADER, req->headers);
        }
        curl_easy_setopt(req->handle, CURLOPT_PRIVATE, req);
        curl_multi_add_handle(g_Multi, req->handle);
        g_AsyncRequests.push_back(req);
    }

    static void UpdateAsync() {
        if (!g_Multi) return;
        int still_running = 0;
        curl_multi_perform(g_Multi, &still_running);
        int msgs_left = 0; CURLMsg* msg;
        while ((msg = curl_multi_info_read(g_Multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* e = msg->easy_handle;
                AsyncRequest* req = nullptr;
                curl_easy_getinfo(e, CURLINFO_PRIVATE, &req);
                if (req) {
                    long sc = 0; curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &sc);
                    req->callback(sc, req->buffer);
                    if (req->headers) curl_slist_free_all(req->headers);
                    curl_multi_remove_handle(g_Multi, e);
                    curl_easy_cleanup(e); free(req->bufferData);
                    auto it = std::find(g_AsyncRequests.begin(), g_AsyncRequests.end(), req);
                    if (it != g_AsyncRequests.end()) g_AsyncRequests.erase(it);
                    delete req;
                }
            }
        }
    }
}
namespace CesiumGEPROSM {
    using namespace CesiumMath;

    enum TestState { STATE_STREAMING, STATE_STREAMING_STENCIL_WAIT, STATE_AGGREGATE, STATE_LOAD_CACHED, STATE_CACHED_STENCIL_WAIT, STATE_VERIFY, STATE_DONE };
    static TestState g_CurrentState = STATE_STREAMING;
    static uint32_t g_StateFrameStart = 0;
    static GLuint g_CachedVAO = 0, g_CachedVBO = 0, g_CachedEBO = 0;
    static int g_CachedCount = 0;
    
    struct TestView { V3 focusPoint; float distance; float pitch; float yaw; };
    static TestView g_Views[4] = {
        { {0, 0, 50}, 150.0f, 0.2f, 0.0f },      // 0: Front full body
        { {0, 0, 50}, 250.0f, 0.4f, 1.57f },     // 1: Side environment
        { {0, 0, 80}, 400.0f, 0.6f, -0.78f },    // 2: Wide Angle Aerial
        { {0, 0, 50}, 200.0f, 0.2f, 3.14f }      // 3: Back Angle
    };
    static int g_CurrentViewIndex = 0;

    struct TestLocation {
        char name[128];
        double lat;
        double lon;
        double alt;
        char semantic[256];
        char image_description[512];
        bool export_bin;
        bool export_3dm;
        bool export_framebuffer;
        bool export_stencils;
        bool load_gepr;
        bool load_osm;
        std::vector<TestView> views;
    };
    static std::vector<TestLocation> g_Locations;
    static int g_CurrentLocationIndex = 0;



    static float evaluateStructuralFit(const uint8_t* img1, const uint8_t* img2, int w, int h) {
        auto get_edges = [&](const uint8_t* img, uint8_t* edges) {
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    auto get_v = [&](int ox, int oy) {
                        int idx = ((y + oy) * w + (x + ox)) * 3;
                        return (img[idx] * 0.299f + img[idx + 1] * 0.587f + img[idx + 2] * 0.114f);
                    };
                    float gx = -1*get_v(-1,-1) + 1*get_v(1,-1) - 2*get_v(-1,0) + 2*get_v(1,0) - 1*get_v(-1,1) + 1*get_v(1,1);
                    float gy = -1*get_v(-1,-1) - 2*get_v(0,-1) - 1*get_v(1,-1) + 1*get_v(-1,1) + 2*get_v(0,1) + 1*get_v(1,1);
                    edges[y*w + x] = (sqrtf(gx*gx + gy*gy) > 25.0f) ? 255 : 0;
                }
            }
        };
        std::vector<uint8_t> e1(w*h), e2(w*h); get_edges(img1, e1.data()); get_edges(img2, e2.data());
        int match = 0, total = 0;
        for (int i=0; i<w*h; ++i) { if (e1[i] == 255) { total++; if (e2[i] == 255) match++; } }
        return (total == 0) ? 1.0f : (float)match / (float)total;
    }

    static uint32_t g_TotalLoadedTiles = 0;
    static uint32_t g_TotalVRAMAllocations = 0;
    static uint32_t g_MaxDepthReached = 0;

    struct RenderResources { GLuint vao, vbo, ebo; int count; bool inUse; };

    struct PayloadBlock { uint8_t* data; bool inUse; };
    static const int PAYLOAD_POOL_SIZE = 2500;
    static const int RESOURCE_POOL_SIZE = 5000;
    static const size_t PAYLOAD_BLOCK_SIZE = 4 * 1024 * 1024; // 4MB per block
    
    static PayloadBlock g_PayloadPool[PAYLOAD_POOL_SIZE];
    static RenderResources g_ResourcePool[RESOURCE_POOL_SIZE];

    static RenderResources* allocResource() {
        for (int i=0; i<RESOURCE_POOL_SIZE; ++i) {
            if (!g_ResourcePool[i].inUse) {
                g_ResourcePool[i].inUse = true;
                memset(&g_ResourcePool[i], 0, sizeof(RenderResources));
                g_ResourcePool[i].inUse = true;
                return &g_ResourcePool[i];
            }
        }
        return nullptr;
    }
    static void freeResource(RenderResources* res) {
        if (!res) return;
        if (res->vao) glDeleteVertexArrays(1, &res->vao);
        if (res->vbo) glDeleteBuffers(1, &res->vbo);
        if (res->ebo) glDeleteBuffers(1, &res->ebo);
        res->vao = res->vbo = res->ebo = 0;
        res->inUse = false;
    }

    static uint8_t* allocPayload(size_t size) {
        if (size > PAYLOAD_BLOCK_SIZE) return nullptr;
        for (int i=0; i<PAYLOAD_POOL_SIZE; ++i) {
            if (!g_PayloadPool[i].inUse) {
                g_PayloadPool[i].inUse = true;
                return g_PayloadPool[i].data;
            }
        }
        return nullptr;
    }
    static void freePayload(uint8_t* p) {
        if (!p) return;
        for (int i=0; i<PAYLOAD_POOL_SIZE; ++i) {
            if (g_PayloadPool[i].data == p) {
                g_PayloadPool[i].inUse = false;
                return;
            }
        }
    }

    struct Tile {
        DMat4 transform;
        char contentUri[512];
        char baseUrl[512];
        bool isLoaded;
        bool isLoading; // Async protection
        Tile** children;
        uint32_t childrenCount;
        uint8_t* payload;
        size_t payloadSize;
        bool isCulled;
        double radius;
        double centerDist;
        double geometricError;
        AABB boundingAABB;
        AABB localAABB;
        RenderResources* rendererResources;
        uint32_t lastAccessedFrame;
        bool isRefined;
    };

    static uint32_t g_TilesLoadedThisFrame = 0;
    static DVec3 g_OriginEcef;
    static double g_EnuMatrix[16];

    static uint32_t g_FrameCount = 0;

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

        void evictStaleTiles(Tile* node, int depth = 0) {
            if (!node || depth > 100) return;
            if (node->isLoaded && !node->isLoading && node->lastAccessedFrame < g_FrameCount - 10) {
                if (node->rendererResources || node->payload) {
                    static uint32_t lastLogFrame = 0;
                    if (g_FrameCount > lastLogFrame + 100) {
                        printf("[GC] Evicting tile (Last: %u, Cur: %u)\n", node->lastAccessedFrame, g_FrameCount);
                        lastLogFrame = g_FrameCount;
                    }
                    if (node->rendererResources) {
                        freeResource(node->rendererResources);
                        node->rendererResources = nullptr;
                    }
                    if (node->payload) {
                        freePayload(node->payload);
                        node->payload = nullptr;
                    }
                    node->isLoaded = false;
                }
            }
            for (uint32_t i = 0; i < node->childrenCount; ++i) {
                evictStaleTiles(node->children[i], depth + 1);
            }
        }

        int traverse(Tile* node, int depth) {
            if (!node || depth > 100) return 1;
            node->lastAccessedFrame = g_FrameCount;
            if (depth > (int)g_MaxDepthReached) g_MaxDepthReached = (uint32_t)depth;

            // --- Dynamic Frustum Culling ---
            AABB testBox = node->localAABB.initialized ? node->localAABB : node->boundingAABB;
            bool visible = (depth < 8) ? true : currentFrustum.intersects(testBox);

            // MANDATE: Christ the Redeemer (origin {0,0,0} in ENU) must NEVER be culled.
            if (testBox.initialized) {
                float d2 = testBox.center().x * testBox.center().x + testBox.center().y * testBox.center().y + testBox.center().z * testBox.center().z;
                if (d2 < 250000.0f) visible = true; // 500m radius
                
                float dx = std::max(0.0f, std::max(testBox.m_min.x, -testBox.m_max.x));
                float dy = std::max(0.0f, std::max(testBox.m_min.y, -testBox.m_max.y));
                float dz = std::max(0.0f, std::max(testBox.m_min.z, -testBox.m_max.z));
                float distToBox2 = dx*dx + dy*dy + dz*dz;
                if (depth > 8 && distToBox2 > 25000000.0f) return 1; // HARD CULL BEYOND 5KM
            }
            if (!visible) return 1;

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

                double targetSSE = 0.0; // Extremely aggressive to force highest LOD
                if (sse > targetSSE) shouldRefine = true;
            } else if (node->childrenCount > 0) {
                shouldRefine = true;
            }

            if (testBox.initialized) {
                float distToOrigin = sqrtf(testBox.center().x * testBox.center().x + testBox.center().y * testBox.center().y + testBox.center().z * testBox.center().z);
                if (distToOrigin < 2000.0f) shouldRefine = true;
            }

            if (depth > 30) shouldRefine = false; // Safety clamp

            // Trigger fetch if not loaded
            if (node->contentUri[0] != '\0' && !node->isLoaded && !node->isLoading) {
                if (depth == 0 && g_FrameCount % 100 == 0) printf("[GEPR] Depth 0 fetching %s\n", node->contentUri); fflush(stdout);
                if (CesiumNetwork::g_AsyncRequests.size() < 20) {
                    node->isLoading = true;
                    char url[1024];
                    resolveUri(url, 1024, node->baseUrl, node->contentUri, apiKey, sessionToken, token);
                    // if (depth < 6) { printf("[GEPR] Async Fetch (depth %d)...\n", depth); fflush(stdout); }
                    
                    std::string nextBaseStr;
                    {
                        char nextBase[1024];
                        resolveUri(nextBase, 1024, node->baseUrl, node->contentUri, nullptr, nullptr, nullptr);
                        char* q = strchr(nextBase, '?'); if (q) *q = '\0';
                        char* lastSlash = strrchr(nextBase, '/');
                        if (lastSlash) *(lastSlash + 1) = '\0';
                        nextBaseStr = nextBase;
                    }

                    CesiumNetwork::FetchAsync(url, 16 * 1024 * 1024, token[0] ? token : nullptr, 
                    [this, node, nextBaseStr, depth](long sc, const CesiumNetwork::FetchBuffer& buffer) {
                        if (sc >= 200 && sc < 300) {
                            if (buffer.size > 0 && buffer.data[0] == '{') {
                                auto j = AliceJson::parse(buffer.data, buffer.size);
                                if (j.type != AliceJson::J_NULL) {
                                    parseNode(node, j["root"], node->transform, nextBaseStr.c_str(), depth);
                                }
                                Alice::g_JsonArena.reset();
                            } else {
                                node->payloadSize = buffer.size;
                                node->payload = allocPayload(node->payloadSize);
                                if (node->payload) {
                                    memcpy(node->payload, buffer.data, node->payloadSize);
                                    g_TotalLoadedTiles++;
                                }
                                node->isLoaded = true;
                            }
                        } else {
                            node->isLoaded = true;
                        }
                        node->isLoading = false;
                    });
                }
            }

            bool childrenLoaded = true;
            if (node->childrenCount > 0) {
                for (uint32_t i = 0; i < node->childrenCount; ++i) {
                    if (!node->children[i] || !node->children[i]->isLoaded) childrenLoaded = false;
                }
            } else {
                childrenLoaded = false;
            }
            node->isRefined = shouldRefine && childrenLoaded;

            if (node->isLoaded && node->payload && !node->isRefined) {
                if (renderListCount < 8192) renderList[renderListCount++] = node;
            }

            if (shouldRefine) {
                for (uint32_t i = 0; i < node->childrenCount; ++i) {
                    traverse(node->children[i], depth + 1);
                }
            }

            return 0;
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
                tile->boundingAABB.initialized = true;
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
                tile->children = (Tile**)Alice::g_Arena.allocate(childrenArr.size() * sizeof(Tile*));
                if (tile->children) {
                    tile->childrenCount = (uint32_t)childrenArr.size();
                    memset(tile->children, 0, tile->childrenCount * sizeof(Tile*));
                    for (uint32_t i = 0; i < tile->childrenCount; ++i) {
                        tile->children[i] = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                        if (tile->children[i]) {
                            memset(tile->children[i], 0, sizeof(Tile));
                            parseNode(tile->children[i], childrenArr[i], tile->transform, tile->baseUrl, depth + 1);
                        }
                    }
                } else {
                    tile->childrenCount = 0;
                }
            }
        }
    };

    static Tileset g_Tileset;
    static GLuint g_Program = 0;
    static uint64_t g_TotalFrustumVertices = 0;
    static float* g_ScratchVbo = nullptr;
    static uint32_t* g_ScratchEbo = nullptr;

    static void loadLocationData() {
        std::ifstream f("locations.json");
        if (f.is_open()) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto j = AliceJson::parse((const uint8_t*)content.c_str(), content.size());
            if (j.contains("locations")) {
                auto locs = j["locations"];
                for (size_t i=0; i<locs.size(); ++i) {
                    TestLocation loc;
                    strncpy(loc.name, locs[(uint32_t)i]["name"].get<std::string>().c_str(), 127);
                    loc.lat = (double)locs[(uint32_t)i]["lat"];
                    loc.lon = (double)locs[(uint32_t)i]["lon"];
                    loc.alt = (double)locs[(uint32_t)i]["alt"];
                    strncpy(loc.semantic, locs[(uint32_t)i]["semantic_criteria"].get<std::string>().c_str(), 255);
                    if (locs[(uint32_t)i].contains("image_description")) {
                        strncpy(loc.image_description, locs[(uint32_t)i]["image_description"].get<std::string>().c_str(), 511);
                    } else {
                        loc.image_description[0] = '\0';
                    }
                    loc.export_bin = locs[(uint32_t)i].contains("export_bin") && locs[(uint32_t)i]["export_bin"].type == AliceJson::J_BOOL ? locs[(uint32_t)i]["export_bin"].boolean : true;
                    loc.export_3dm = locs[(uint32_t)i].contains("export_3dm") && locs[(uint32_t)i]["export_3dm"].type == AliceJson::J_BOOL ? locs[(uint32_t)i]["export_3dm"].boolean : false;
                    loc.export_framebuffer = locs[(uint32_t)i].contains("export_framebuffer") && locs[(uint32_t)i]["export_framebuffer"].type == AliceJson::J_BOOL ? locs[(uint32_t)i]["export_framebuffer"].boolean : true;
                    loc.export_stencils = locs[(uint32_t)i].contains("export_stencils") && locs[(uint32_t)i]["export_stencils"].type == AliceJson::J_BOOL ? locs[(uint32_t)i]["export_stencils"].boolean : true;
                    loc.load_gepr = locs[(uint32_t)i].contains("load_gepr") && locs[(uint32_t)i]["load_gepr"].type == AliceJson::J_BOOL ? locs[(uint32_t)i]["load_gepr"].boolean : true;
                    loc.load_osm = locs[(uint32_t)i].contains("load_osm") && locs[(uint32_t)i]["load_osm"].type == AliceJson::J_BOOL ? locs[(uint32_t)i]["load_osm"].boolean : false;
                    if (locs[(uint32_t)i].contains("views") && locs[(uint32_t)i]["views"].type == AliceJson::J_ARRAY) {
                        auto vws = locs[(uint32_t)i]["views"];
                        for(size_t v=0; v<vws.size(); ++v) {
                            TestView tv;
                            tv.focusPoint = {(float)vws[(uint32_t)v]["focusPoint"][0], (float)vws[(uint32_t)v]["focusPoint"][1], (float)vws[(uint32_t)v]["focusPoint"][2]};
                            tv.distance = (float)vws[(uint32_t)v]["distance"];
                            tv.pitch = (float)vws[(uint32_t)v]["pitch"];
                            tv.yaw = (float)vws[(uint32_t)v]["yaw"];
                            loc.views.push_back(tv);
                        }
                    } else {
                        for(int v=0; v<4; ++v) loc.views.push_back(g_Views[v]);
                    }
                    g_Locations.push_back(loc);
                    printf("[GEPR] Location Loaded. GEPR: %d, OSM: %d\n", loc.load_gepr, loc.load_osm);
                }
            }
            Alice::g_JsonArena.reset();
        }
        if (g_Locations.empty()) {
            printf("[GEPR] No locations found, defaulting to Rio.\n");
            g_Locations.push_back({"Christ The Redeemer", -22.951916, -43.210487, 710.0, ""});
        }
    }

    static void loadTilesetForCurrentLocation() {
        double lat = g_Locations[g_CurrentLocationIndex].lat * 0.0174532925;
        double lon = g_Locations[g_CurrentLocationIndex].lon * 0.0174532925;
        double alt = g_Locations[g_CurrentLocationIndex].alt;
        g_OriginEcef = lla_to_ecef(lat, lon, alt); 
        printf("[GEPR] Origin ECEF: %f, %f, %f\n", g_OriginEcef.x, g_OriginEcef.y, g_OriginEcef.z);
        denu_matrix(g_EnuMatrix, lat, lon);
        
        char ionToken[2048];
        if (!Alice::ApiKeyReader::GetCesiumToken(ionToken, 2048)) {
            const char* fallbackToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiIzYThhNjI4NS0xOTIxLTRhZjMtYmZiNS1iYmFjMzkxNzFkMDIiLCJpZCI6NDE5MDUyLCJpYXQiOjE3NzY2MjcxNDh9.fwQc5dZ44EUlMDWwylbD1rrApiqEPZbHRJRcyz4jApc";
            strncpy(ionToken, fallbackToken, 2047);
            ionToken[2047] = '\0';
        }
        uint32_t assetId = 96188; // Default to OSM
        if (g_Locations[g_CurrentLocationIndex].load_gepr) {
            assetId = 2275207;
        } else if (g_Locations[g_CurrentLocationIndex].load_osm) {
            assetId = 96188;
        }
        char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/%u/endpoint?access_token=%s", assetId, ionToken);
        static uint8_t hsBuf[1024*1024]; CesiumNetwork::FetchBuffer handshake = { hsBuf, 0, sizeof(hsBuf) };
        long sc=0;
        printf("[GEPR] Fetching endpoint: %s\n", url);
        if (CesiumNetwork::Fetch(url, handshake, &sc)) {
            printf("[GEPR] Handshake success (SC: %ld)\n", sc);
            auto jhs = AliceJson::parse(handshake.data, handshake.size);
            std::string turl, atok;
            if (jhs.contains("url")) turl = jhs["url"].get<std::string>();
            else if (jhs.contains("options") && jhs["options"].contains("url")) turl = jhs["options"]["url"].get<std::string>();
            if (jhs.contains("accessToken")) atok = jhs["accessToken"].get<std::string>();
            if (!atok.empty()) strncpy(g_Tileset.token, atok.c_str(), 2047);
            if (!turl.empty()) {
                printf("[GEPR] Tileset URL: %s\n", turl.c_str());
                size_t kPos = turl.find("key=");
                if (kPos != std::string::npos) {
                    size_t kEnd = turl.find('&', kPos);
                    std::string key = turl.substr(kPos + 4, kEnd == std::string::npos ? std::string::npos : kEnd - (kPos + 4));
                    strncpy(g_Tileset.apiKey, key.c_str(), 255);
                }
                strncpy(g_Tileset.baseUrl, turl.substr(0, turl.find_last_of('/')+1).c_str(), 1023);
                static uint8_t tsBuf[16*1024*1024]; CesiumNetwork::FetchBuffer ts = { tsBuf, 0, sizeof(tsBuf) };
                if (CesiumNetwork::Fetch(turl.c_str(), ts, &sc, g_Tileset.token[0] ? g_Tileset.token : nullptr)) {
                    printf("[GEPR] Tileset JSON fetched (Size: %zu)\n", ts.size);
                    auto jts = AliceJson::parse(ts.data, ts.size);
                    if (jts.type != AliceJson::J_NULL) {
                        g_Tileset.root = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                        memset(g_Tileset.root, 0, sizeof(Tile));
                        g_Tileset.parseNode(g_Tileset.root, jts["root"], dmat4_identity(), g_Tileset.baseUrl, 0);
                        printf("[GEPR] Tileset root parsed successfully.\n");
                    }
                } else {
                    printf("[GEPR] ERROR: Failed to fetch tileset JSON (SC: %ld)\n", sc);
                }
            } else {
                printf("[GEPR] ERROR: Handshake JSON missing URL.\n");
            }
            Alice::g_JsonArena.reset();
        } else {
            printf("[GEPR] ERROR: Handshake failed (SC: %ld)\n", sc);
        }
        printf("[GEPR] Switched Location to %s\n", g_Locations[g_CurrentLocationIndex].name);
    }
    static void* cgltf_alloc(void* user, size_t size) { return malloc(size); }
    static void cgltf_free_cb(void* user, void* ptr) { free(ptr); }

    static void initShaders() {
        const char* vs = 
            "#version 430 core\n"
            "layout(location=0) in vec3 p;\n"
            "uniform mat4 uMVP;\n"
            "uniform mat4 uV;\n"
            "out vec3 vVP;\n"
            "void main() {\n"
            "    vVP = (uV * vec4(p, 1.0)).xyz;\n"
            "    gl_Position = uMVP * vec4(p, 1.0);\n"
            "}";
            
        const char* fs = 
            "#version 430 core\n"
            "uniform int uPass;\n"
            "layout(location = 0) out vec4 f_Color;\n"
            "layout(location = 1) out vec4 f_Seg;\n"
            "layout(location = 2) out float f_Depth;\n"
            "in vec3 vVP;\n"
            "void main() {\n"
            "    vec3 N = normalize(cross(dFdx(vVP), dFdy(vVP)));\n"
            "    if(!gl_FrontFacing) N = -N;\n"
            "    float d = max(dot(N, normalize(vec3(0.5, 0.8, 0.6))), 0.2);\n"
            "    f_Color = vec4(vec3(0.85) * d, 1.0);\n"
            "    f_Seg = vec4(1.0, 1.0, 1.0, 1.0);\n"
            "    float near = 0.1;\n"
            "    float far = 5000.0;\n"
            "    float dist = -vVP.z;\n"
            "    f_Depth = clamp(1.0 - (dist - near) / (far - near), 0.0, 1.0);\n"
            "}";
            
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(cgltf_node* node, const DMat4& transform, const DVec3& rtcCenter, float* vbo, uint32_t* ebo, uint32_t& vIdx, uint32_t& eIdx) {
        DMat4 m = transform;
        if (node->has_matrix) { 
            DMat4 nm; for(int i=0;i<16;++i) nm.m[i] = (double)node->matrix[i]; 
            m = dmat4_mul(transform, nm); 
            static bool loggedM = false; if (!loggedM) {
                printf("[GEPR] Final Matrix m: "); for(int i=0;i<16;++i) printf("%f ", m.m[i]); printf("\n");
                loggedM = true;
            }
        } else {
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
                    static bool loggedPos = false; if (!loggedPos) {
                        printf("[GEPR] Raw Node Translation: %f, %f, %f\n", node->translation[0], node->translation[1], node->translation[2]);
                        printf("[GEPR] Raw Vertex 0 Pos: %f, %f, %f\n", pos[0], pos[1], pos[2]);
                        printf("[GEPR] Transform[12-14]: %f, %f, %f\n", transform.m[12], transform.m[13], transform.m[14]);
                        loggedPos = true;
                    }
                    DVec4 wp = dmat4_mul_vec4(m, {(double)pos[0] + rtcCenter.x, (double)pos[1] + rtcCenter.y, (double)pos[2] + rtcCenter.z, 1.0});
                    double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                    float ex = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                    float ny = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                    float uz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                    vbo[vIdx*6+0] = ex; vbo[vIdx*6+1] = ny; vbo[vIdx*6+2] = uz; 
                    vbo[vIdx*6+3] = 0.0f; vbo[vIdx*6+4] = 0.0f; vbo[vIdx*6+5] = 0.0f;
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
        for (size_t i=0; i<node->children_count; ++i) processNode(node->children[i], m, rtcCenter, vbo, ebo, vIdx, eIdx);
    }

    static void setup() {
        printf("[GEPR] setup started\n"); fflush(stdout);
        if (!Alice::g_Arena.memory) Alice::g_Arena.init(2048ULL*1024ULL*1024ULL); 
        if (!Alice::g_JsonArena.memory) Alice::g_JsonArena.init(256 * 1024 * 1024);
        
        // Initialize Pools
        for (int i=0; i<PAYLOAD_POOL_SIZE; ++i) {
            g_PayloadPool[i].data = (uint8_t*)malloc(PAYLOAD_BLOCK_SIZE);
            if (!g_PayloadPool[i].data) { printf("[GEPR] FATAL: Payload Pool Allocation Failed\n"); exit(1); }
            g_PayloadPool[i].inUse = false;
        }
        for (int i=0; i<RESOURCE_POOL_SIZE; ++i) g_ResourcePool[i].inUse = false;

        initShaders(); 
        g_Tileset.init();
        
        if (!g_ScratchVbo) g_ScratchVbo = (float*)malloc(1000000*6*4);
        if (!g_ScratchEbo) g_ScratchEbo = (uint32_t*)malloc(2000000*4);
        
        loadLocationData();
        loadTilesetForCurrentLocation();
        
        std::filesystem::create_directories("build/bin/OUTPUT");
        
        FILE* outJson = fopen("build/bin/OUTPUT/output.json", "w");
        if (outJson) {
            fprintf(outJson, "{\n  \"renders\": [\n");
            fclose(outJson);
        }
        
        printf("[GEPR] setup finished\n"); fflush(stdout);
    }

    static void update(float dt) {
        CesiumNetwork::UpdateAsync();
        AliceViewer* av = AliceViewer::instance();
        
        if (g_FrameCount == 0 && !g_Locations.empty()) {
            av->camera.focusPoint = g_Locations[g_CurrentLocationIndex].views[0].focusPoint; 
            av->camera.distance = g_Locations[g_CurrentLocationIndex].views[0].distance;
            av->camera.pitch = g_Locations[g_CurrentLocationIndex].views[0].pitch;
            av->camera.yaw = g_Locations[g_CurrentLocationIndex].views[0].yaw;
            printf("[GEPR] Camera Reset to Location %d (%s), View %d\n", g_CurrentLocationIndex, g_Locations[g_CurrentLocationIndex].name, 0); fflush(stdout);
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
        AliceViewer* av = AliceViewer::instance();

        // Evict stale tiles
        if (g_Tileset.root && (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT)) g_Tileset.evictStaleTiles(g_Tileset.root);
        
        // --- AABB Accumulation ---
        av->m_sceneAABB = AABB();
        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            AABB& tb = g_Tileset.renderList[i]->localAABB.initialized ? g_Tileset.renderList[i]->localAABB : g_Tileset.renderList[i]->boundingAABB;
            if (tb.initialized) {
                av->m_sceneAABB.expand(tb.m_min);
                av->m_sceneAABB.expand(tb.m_max);
            }
        }

        av->backColor = {0.0f, 0.0f, 0.0f};
        if (frameIdx % 100 == 0) { printf("[GEPR] Frame %u, RenderList: %u\n", frameIdx, g_Tileset.renderListCount); fflush(stdout); }
        
        if (av->m_isRenderingOffscreen) {
            float cColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
            float zero[] = {0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, cColor);
            glClearBufferfv(GL_COLOR, 1, zero);
            glClearBufferfv(GL_COLOR, 2, zero);
            glClearDepth(0.0f); glClear(GL_DEPTH_BUFFER_BIT);

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClearDepth(0.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_GEQUAL);
        }

        glEnable(GL_CULL_FACE);
        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        M4 v = av->camera.getViewMatrix(), p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        glUseProgram(g_Program); glUniformMatrix4fv(glGetUniformLocation(g_Program, "uMVP"), 1, 0, mvp);
        glUniformMatrix4fv(glGetUniformLocation(g_Program, "uV"), 1, 0, v.m);
        
        g_TotalFrustumVertices = 0;
        
        if (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT) {
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
                                static bool loggedRtc = false; if (!loggedRtc) { printf("[GEPR] Parsed RTC_CENTER: %f, %f, %f\n", rtcCenter.x, rtcCenter.y, rtcCenter.z); loggedRtc = true; }
                            }
                        }
                        uint32_t headerLen = 28 + ftj + ftb + btj + btb;
                        if (headerLen < glbSize) { glbPayload += headerLen; glbSize -= headerLen; }
                    }
                    if (cgltf_parse(&opt, glbPayload, glbSize, &data) == cgltf_result_success) {
                        cgltf_load_buffers(&opt, data, 0);
                        RenderResources* res = allocResource();
                        if (res && g_ScratchVbo && g_ScratchEbo) {
                            uint32_t vIdx=0, eIdx=0;
                            DMat4 yup = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
                            DMat4 mTransform = dmat4_mul(t->transform, yup);
                            if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], mTransform, rtcCenter, g_ScratchVbo, g_ScratchEbo, vIdx, eIdx);
                            for (uint32_t vIdx2 = 0; vIdx2 < vIdx; ++vIdx2) t->localAABB.expand({g_ScratchVbo[vIdx2*6+0], g_ScratchVbo[vIdx2*6+1], g_ScratchVbo[vIdx2*6+2]});
                            glGenVertexArrays(1, &res->vao); glBindVertexArray(res->vao);
                            glGenBuffers(1, &res->vbo); glBindBuffer(GL_ARRAY_BUFFER, res->vbo); glBufferData(GL_ARRAY_BUFFER, vIdx*6*4, g_ScratchVbo, GL_STATIC_DRAW);
                            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                            glGenBuffers(1, &res->ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, eIdx*4, g_ScratchEbo, GL_STATIC_DRAW);
                            res->count = eIdx; t->rendererResources = res;
                            g_TotalVRAMAllocations++;
                        }
                        cgltf_free(data);
                    }
                }
            }
            GLint locPass = glGetUniformLocation(g_Program, "uPass");
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glUniform1i(locPass, 0); }
                else { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glUniform1i(locPass, 1); }
                
                for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
                    Tile* t = g_Tileset.renderList[i];
                    if (t->rendererResources) { 
                        glBindVertexArray(t->rendererResources->vao); 
                        glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); 
                        if (pass == 0) g_TotalFrustumVertices += (uint64_t)t->rendererResources->count; 
                    }
                }
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_POLYGON_OFFSET_FILL);


            static int settledFrames = 0;
            if (CesiumNetwork::g_AsyncRequests.size() == 0 && g_Tileset.renderListCount > 0) {
                settledFrames++;
            } else {
                settledFrames = 0;
            }

            if (frameIdx - g_StateFrameStart >= 1000 || (settledFrames > 60 && frameIdx - g_StateFrameStart > 100)) {
                uint32_t activePayloads = 0; for(int i=0; i<PAYLOAD_POOL_SIZE; ++i) if(g_PayloadPool[i].inUse) activePayloads++;
                uint32_t activeResources = 0; for(int i=0; i<RESOURCE_POOL_SIZE; ++i) if(g_ResourcePool[i].inUse) activeResources++;
                printf("[GEPR] Frame %u, Depth: %u, RenderList: %u, Vertices: %llu, Payloads: %u/%d, Resources: %u/%d\n", frameIdx, g_MaxDepthReached, g_Tileset.renderListCount, g_TotalFrustumVertices, activePayloads, PAYLOAD_POOL_SIZE, activeResources, RESOURCE_POOL_SIZE);
                for (uint32_t i=0; i<std::min((uint32_t)5, g_Tileset.renderListCount); ++i) {
                    Tile* t = g_Tileset.renderList[i];
                    AABB& b = t->localAABB.initialized ? t->localAABB : t->boundingAABB;
                    printf("[GEPR] Tile %u (URI: %s) AABB: min(%f, %f, %f) max(%f, %f, %f)\n", i, (t->contentUri[0] != '\0') ? t->contentUri : "null", b.m_min.x, b.m_min.y, b.m_min.z, b.m_max.x, b.m_max.y, b.m_max.z);
                }
                if (g_Locations[g_CurrentLocationIndex].export_framebuffer) {
                    unsigned char* px = (unsigned char*)malloc(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
                    stbi_flip_vertically_on_write(1); 
                    char path[256]; snprintf(path, 256, "build/bin/OUTPUT/GEPR_loc%d_view%d_stream_framebuffer.png", g_CurrentLocationIndex, g_CurrentViewIndex);
                    stbi_write_png(path, w, h, 3, px, w*3); free(px);
                }
                
                if (g_Locations[g_CurrentLocationIndex].export_stencils) {
                    g_CurrentState = STATE_STREAMING_STENCIL_WAIT; g_StateFrameStart = frameIdx;
                    char prefix[256]; snprintf(prefix, 256, "build/bin/OUTPUT/GEPR_loc%d_view%d_stream_stencil", g_CurrentLocationIndex, g_CurrentViewIndex);
                    av->captureHighResStencils(prefix);
                } else {
                    g_CurrentState = STATE_AGGREGATE; g_StateFrameStart = frameIdx;
                }
                
                printf("[Test] View %d STREAMING Complete. Proceeding to %s.\n", g_CurrentViewIndex, g_Locations[g_CurrentLocationIndex].export_stencils ? "STREAMING_STENCIL_WAIT" : "AGGREGATE"); fflush(stdout);
                settledFrames = 0; // Reset settled frames for the next state/view
            }
        } 
        
        if (g_CurrentState == STATE_STREAMING_STENCIL_WAIT) {
            if (av->m_pendingCaptures.load() == 0 && frameIdx - g_StateFrameStart > 10) {
                g_CurrentState = STATE_AGGREGATE; g_StateFrameStart = frameIdx;
                printf("[Test] View %d STREAMING_STENCIL_WAIT Complete. Proceeding to AGGREGATE.\n", g_CurrentViewIndex); fflush(stdout);
            }
        } else if (g_CurrentState == STATE_AGGREGATE) {
            std::vector<float> totalVBO; std::vector<uint32_t> totalEBO;
            uint32_t tilesProcessed = 0;
            uint32_t tilesWithPayload = 0;
            uint32_t cgltfSuccesses = 0;

            for (uint32_t i = 0; i < g_Tileset.renderListCount; ++i) {
                Tile* t = g_Tileset.renderList[i];
                tilesProcessed++;
                if (t->payload) {
                    tilesWithPayload++;
                    cgltf_data* data = 0;
                    uint8_t* glbPayload = t->payload; size_t glbSize = t->payloadSize;
                    DVec3 rtcCenter = {0,0,0};
                    if (glbSize >= 28 && memcmp(glbPayload, "b3dm", 4) == 0) {
                        uint32_t ftj = *(uint32_t*)(glbPayload + 12), ftb = *(uint32_t*)(glbPayload + 16);
                        uint32_t btj = *(uint32_t*)(glbPayload + 20), btb = *(uint32_t*)(glbPayload + 24);
                        if (i == 0) printf("[Aggregate] Tile 0 B3DM: ftj=%u, ftb=%u, btj=%u, btb=%u, totalSize=%zu\n", ftj, ftb, btj, btb, glbSize);
                        if (ftj > 0) {
                            auto ft = AliceJson::parse(glbPayload + 28, ftj);
                            if (ft.contains("RTC_CENTER")) {
                                auto rtc = ft["RTC_CENTER"]; rtcCenter = {(double)rtc[0], (double)rtc[1], (double)rtc[2]};
                            }
                            Alice::g_JsonArena.reset();
                        }
                        uint32_t headerLen = 28 + ftj + ftb + btj + btb;
                        if (headerLen < glbSize) { glbPayload += headerLen; glbSize -= headerLen; }
                    }
                    if (i < 5) printf("[Aggregate] Tile %u GLB Magic: %.4s, Size: %zu, RTC_CENTER: %f, %f, %f\n", i, glbPayload, glbSize, rtcCenter.x, rtcCenter.y, rtcCenter.z);
                    cgltf_options opt = {cgltf_file_type_glb}; opt.memory.alloc_func = cgltf_alloc; opt.memory.free_func = cgltf_free_cb;
                    cgltf_result res = cgltf_parse(&opt, glbPayload, glbSize, &data);
                    if (res == cgltf_result_success) {
                        cgltf_load_buffers(&opt, data, 0);
                        cgltfSuccesses++;
                        if (g_ScratchVbo && g_ScratchEbo) {
                            uint32_t vIdx=0, eIdx=0;
                            DMat4 yup = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
                            DMat4 mTransform = dmat4_mul(t->transform, yup);
                            if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], mTransform, rtcCenter, g_ScratchVbo, g_ScratchEbo, vIdx, eIdx);
                            
                            uint32_t vCount = vIdx;
                            uint32_t eCount = eIdx;
                            uint32_t offset = (uint32_t)(totalVBO.size()/6);
                            
                            size_t oldVSize = totalVBO.size();
                            totalVBO.resize(oldVSize + vCount * 6);
                            memcpy(totalVBO.data() + oldVSize, g_ScratchVbo, vCount * 6 * sizeof(float));
                            
                            size_t oldESize = totalEBO.size();
                            totalEBO.resize(oldESize + eCount);
                            for(uint32_t e=0; e<eCount; ++e) totalEBO[oldESize + e] = g_ScratchEbo[e] + offset;
                        }
                        cgltf_free(data);
                    } else {
                        printf("[Aggregate] cgltf_parse FAILED: %d for tile %u\n", (int)res, i);
                    }
                }
            }
            printf("[Aggregate] Processed: %u, WithPayload: %u, CGLTFSuccess: %u, TotalVerts: %zu\n", tilesProcessed, tilesWithPayload, cgltfSuccesses, totalVBO.size()/6);
            if (g_Locations[g_CurrentLocationIndex].export_bin) {
                char binPath[256]; snprintf(binPath, 256, "build/bin/OUTPUT/GEPR_loc%d_cache_binary.bin", g_CurrentLocationIndex);
                FILE* f = fopen(binPath, "wb");
                if (f) {
                    uint32_t vCount = (uint32_t)totalVBO.size(), iCount = (uint32_t)totalEBO.size();
                    fwrite(&vCount, sizeof(uint32_t), 1, f); fwrite(&iCount, sizeof(uint32_t), 1, f);
                    fwrite(totalVBO.data(), sizeof(float), vCount, f); fwrite(totalEBO.data(), sizeof(uint32_t), iCount, f); fclose(f);
                }
            }
            
            if (g_CurrentViewIndex == (int)g_Locations[g_CurrentLocationIndex].views.size() - 1 && g_Locations[g_CurrentLocationIndex].export_3dm) {
                char dmpPath[256]; snprintf(dmpPath, 256, "build/bin/OUTPUT/GEPR_loc%d_3dm.3dm", g_CurrentLocationIndex);
                ONX_Model model;
                ON_Mesh* mesh = new ON_Mesh();
                
                int vCount = (int)(totalVBO.size() / 6);
                mesh->m_V.Reserve(vCount);
                for (int i=0; i<vCount; i++) {
                    mesh->SetVertex(i, ON_3dPoint(totalVBO[i*6+0], totalVBO[i*6+1], totalVBO[i*6+2]));
                }
                
                int fCount = (int)(totalEBO.size() / 3);
                mesh->m_F.Reserve(fCount);
                for (int i=0; i<fCount; i++) {
                    mesh->SetTriangle(i, totalEBO[i*3], totalEBO[i*3+1], totalEBO[i*3+2]);
                }
                mesh->ComputeVertexNormals();
                model.AddManagedModelGeometryComponent(mesh, nullptr);
                model.Write(dmpPath, 7, nullptr);
                printf("[Test] View 0 Exported 3DM to %s\n", dmpPath); fflush(stdout);
                
                ONX_Model testModel;
                if (testModel.Read(dmpPath)) {
                    printf("[Test] VERIFIED 3DM read back successfully for %s\n", dmpPath); fflush(stdout);
                } else {
                    printf("[Test] ERROR: Could not read back 3DM %s\n", dmpPath); fflush(stdout);
                }
            }
            
            g_CurrentState = STATE_LOAD_CACHED; g_StateFrameStart = frameIdx;
            printf("[Test] View %d AGGREGATE Complete. Proceeding to LOAD_CACHED.\n", g_CurrentViewIndex); fflush(stdout);
            
        } else if (g_CurrentState == STATE_LOAD_CACHED || g_CurrentState == STATE_CACHED_STENCIL_WAIT) {
            if (g_CachedVAO == 0) {
                char binPath[256]; snprintf(binPath, 256, "build/bin/OUTPUT/GEPR_loc%d_cache_binary.bin", g_CurrentLocationIndex);
                FILE* f = fopen(binPath, "rb");
                if (f) {
                    uint32_t vCount, iCount; fread(&vCount, sizeof(uint32_t), 1, f); fread(&iCount, sizeof(uint32_t), 1, f);
                    std::vector<float> vbo(vCount); std::vector<uint32_t> ebo(iCount);
                    fread(vbo.data(), sizeof(float), vCount, f); fread(ebo.data(), sizeof(uint32_t), iCount, f); fclose(f);
                    glGenVertexArrays(1, &g_CachedVAO); glBindVertexArray(g_CachedVAO);
                    glGenBuffers(1, &g_CachedVBO); glGenBuffers(1, &g_CachedEBO);
                    glBindBuffer(GL_ARRAY_BUFFER, g_CachedVBO); glBufferData(GL_ARRAY_BUFFER, vbo.size()*4, vbo.data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_CachedEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size()*4, ebo.data(), GL_STATIC_DRAW);
                    g_CachedCount = (int)iCount;
                }
            }
            GLint locPass = glGetUniformLocation(g_Program, "uPass");
            if (g_CachedVAO) {
                for (int pass = 0; pass < 2; ++pass) {
                    if (pass == 0) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glUniform1i(locPass, 0); }
                    else { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glUniform1i(locPass, 1); }
                    glBindVertexArray(g_CachedVAO); 
                    glDrawElements(GL_TRIANGLES, g_CachedCount, GL_UNSIGNED_INT, 0);
                }
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            if (g_CurrentState == STATE_LOAD_CACHED && frameIdx - g_StateFrameStart > 10) {
                if (g_Locations[g_CurrentLocationIndex].export_framebuffer && g_Locations[g_CurrentLocationIndex].export_bin) {
                    unsigned char* px = (unsigned char*)malloc(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
                    stbi_flip_vertically_on_write(1); 
                    char path[256]; snprintf(path, 256, "build/bin/OUTPUT/GEPR_loc%d_view%d_cache_framebuffer.png", g_CurrentLocationIndex, g_CurrentViewIndex);
                    stbi_write_png(path, w, h, 3, px, w*3); free(px);
                }
                
                bool doStencils = g_Locations[g_CurrentLocationIndex].export_stencils && g_Locations[g_CurrentLocationIndex].export_bin;
                if (doStencils) {
                    g_CurrentState = STATE_CACHED_STENCIL_WAIT; g_StateFrameStart = frameIdx;
                    char prefix[256]; snprintf(prefix, 256, "build/bin/OUTPUT/GEPR_loc%d_view%d_cache_stencil", g_CurrentLocationIndex, g_CurrentViewIndex);
                    av->captureHighResStencils(prefix);
                } else {
                    g_CurrentState = STATE_VERIFY; g_StateFrameStart = frameIdx;
                }
                
                printf("[Test] View %d LOAD_CACHED Complete. Proceeding to %s.\n", g_CurrentViewIndex, doStencils ? "CACHED_STENCIL_WAIT" : "VERIFY"); fflush(stdout);
            }
        } 
        
        if (g_CurrentState == STATE_CACHED_STENCIL_WAIT) {
            if (av->m_pendingCaptures.load() == 0 && frameIdx - g_StateFrameStart > 10) {
                g_CurrentState = STATE_VERIFY; g_StateFrameStart = frameIdx;
                printf("[Test] View %d CACHED_STENCIL_WAIT Complete. Proceeding to VERIFY.\n", g_CurrentViewIndex); fflush(stdout);
            }
        } else if (g_CurrentState == STATE_VERIFY) {
            char p1[256], p2[256]; 
            snprintf(p1, 256, "build/bin/OUTPUT/GEPR_loc%d_view%d_stream_framebuffer.png", g_CurrentLocationIndex, g_CurrentViewIndex);
            snprintf(p2, 256, "build/bin/OUTPUT/GEPR_loc%d_view%d_cache_framebuffer.png", g_CurrentLocationIndex, g_CurrentViewIndex);
            int w1, h1, c1, w2, h2, c2;
            uint8_t *img1 = stbi_load(p1, &w1, &h1, &c1, 3), *img2 = stbi_load(p2, &w2, &h2, &c2, 3);
            if (img1 && img2) {
                float score = evaluateStructuralFit(img1, img2, w1, h1);
                printf("[Verify] Cached GEPR Loc %d View %d F1 Score: %.4f\n", g_CurrentLocationIndex, g_CurrentViewIndex, score);
                stbi_image_free(img1); stbi_image_free(img2);
            } else {
                printf("[Verify] Error loading images for verification.\n");
            }
            
            FILE* outJson = fopen("build/bin/OUTPUT/output.json", "a");
            if (outJson) {
                if (g_CurrentLocationIndex > 0 || g_CurrentViewIndex > 0) fprintf(outJson, ",\n");
                fprintf(outJson, "    {\n");
                fprintf(outJson, "      \"image\": \"%s\",\n", p1);
                fprintf(outJson, "      \"location\": \"%s\",\n", g_Locations[g_CurrentLocationIndex].name);
                fprintf(outJson, "      \"lat\": %.6f,\n", g_Locations[g_CurrentLocationIndex].lat);
                fprintf(outJson, "      \"lon\": %.6f,\n", g_Locations[g_CurrentLocationIndex].lon);
                fprintf(outJson, "      \"alt\": %.1f,\n", g_Locations[g_CurrentLocationIndex].alt);
                fprintf(outJson, "      \"semantic_criteria\": \"%s\",\n", g_Locations[g_CurrentLocationIndex].semantic);
                fprintf(outJson, "      \"image_description\": \"PENDING SEMANTIC ANALYSIS\",\n");
                fprintf(outJson, "      \"camera\": {\n");
                fprintf(outJson, "        \"focusPoint\": [%.1f, %.1f, %.1f],\n", g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].focusPoint.x, g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].focusPoint.y, g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].focusPoint.z);
                fprintf(outJson, "        \"distance\": %.1f,\n", g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].distance);
                fprintf(outJson, "        \"pitch\": %.2f,\n", g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].pitch);
                fprintf(outJson, "        \"yaw\": %.2f,\n", g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].yaw);
                fprintf(outJson, "        \"fov\": %.1f\n", av->fov);
                fprintf(outJson, "      }\n");
                fprintf(outJson, "    }");
                fclose(outJson);
            }
            
            // Clean up cached VAO for the next view
            glDeleteVertexArrays(1, &g_CachedVAO); glDeleteBuffers(1, &g_CachedVBO); glDeleteBuffers(1, &g_CachedEBO);
            g_CachedVAO = 0; g_CachedVBO = 0; g_CachedEBO = 0; g_CachedCount = 0;
            
            g_CurrentViewIndex++;
            if (g_CurrentViewIndex < (int)g_Locations[g_CurrentLocationIndex].views.size()) {
                g_CurrentState = STATE_STREAMING; g_StateFrameStart = frameIdx;
                av->camera.focusPoint = g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].focusPoint; 
                av->camera.distance = g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].distance;
                av->camera.pitch = g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].pitch;
                av->camera.yaw = g_Locations[g_CurrentLocationIndex].views[g_CurrentViewIndex].yaw;
                printf("[GEPR] Camera Update to View %d\n", g_CurrentViewIndex); fflush(stdout);
            } else {
                g_CurrentState = STATE_DONE; g_StateFrameStart = frameIdx;
            }
            
        } else if (g_CurrentState == STATE_DONE) {
            printf("[Test] Loc %d GEPR ALL %d VIEWS Caching verification complete.\n", g_CurrentLocationIndex, (int)g_Locations[g_CurrentLocationIndex].views.size()); fflush(stdout);
            g_CurrentLocationIndex++;
            if (g_CurrentLocationIndex < g_Locations.size()) {
                g_CurrentState = STATE_STREAMING;
                g_StateFrameStart = frameIdx;
                g_CurrentViewIndex = 0;
                
                // Full cleanup of memory to reload new location
                for(int i=0; i<RESOURCE_POOL_SIZE; ++i) if(g_ResourcePool[i].inUse) { freeResource(&g_ResourcePool[i]); }
                for(int i=0; i<PAYLOAD_POOL_SIZE; ++i) { g_PayloadPool[i].inUse = false; }
                
                Alice::g_Arena.offset = 0; 
                g_Tileset.init(); // Re-allocate root pointers cleanly
                loadTilesetForCurrentLocation();
                
                AliceViewer* av = AliceViewer::instance();
                av->camera.focusPoint = g_Locations[g_CurrentLocationIndex].views[0].focusPoint; 
                av->camera.distance = g_Locations[g_CurrentLocationIndex].views[0].distance;
                av->camera.pitch = g_Locations[g_CurrentLocationIndex].views[0].pitch;
                av->camera.yaw = g_Locations[g_CurrentLocationIndex].views[0].yaw;
            } else {
                FILE* outJson = fopen("build/bin/OUTPUT/output.json", "a");
                if (outJson) {
                    fprintf(outJson, "\n  ]\n}\n");
                    fclose(outJson);
                }
                printf("[Test] ALL LOCATIONS VERIFIED. Exiting.\n"); fflush(stdout);
                exit(0);
            }
        }
    }
}

#ifdef ALICE_TEST_CESIUM_GEPR
extern "C" void setup() { CesiumGEPROSM::setup(); }
extern "C" void update(float dt) { CesiumGEPROSM::update(dt); }
extern "C" void draw() { CesiumGEPROSM::draw(); }
#endif

#endif // CESIUM_GEPR_H

