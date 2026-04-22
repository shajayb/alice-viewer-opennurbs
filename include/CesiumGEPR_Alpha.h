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

    struct Plane { DVec3 n; double d; };
    struct Frustum { Plane p[6]; };

    static Frustum extractFrustum(const float* mvp) {
        Frustum f;
        f.p[0].n.x = mvp[3] + mvp[0]; f.p[0].n.y = mvp[7] + mvp[4]; f.p[0].n.z = mvp[11] + mvp[8]; f.p[0].d = mvp[15] + mvp[12];
        f.p[1].n.x = mvp[3] - mvp[0]; f.p[1].n.y = mvp[7] - mvp[4]; f.p[1].n.z = mvp[11] - mvp[8]; f.p[1].d = mvp[15] - mvp[12];
        f.p[2].n.x = mvp[3] + mvp[1]; f.p[2].n.y = mvp[7] + mvp[5]; f.p[2].n.z = mvp[11] + mvp[9]; f.p[2].d = mvp[15] + mvp[13];
        f.p[3].n.x = mvp[3] - mvp[1]; f.p[3].n.y = mvp[7] - mvp[5]; f.p[3].n.z = mvp[11] - mvp[9]; f.p[3].d = mvp[15] - mvp[13];
        f.p[4].n.x = mvp[3] + mvp[2]; f.p[4].n.y = mvp[7] + mvp[6]; f.p[4].n.z = mvp[11] + mvp[10]; f.p[4].d = mvp[15] + mvp[14];
        f.p[5].n.x = mvp[3] - mvp[2]; f.p[5].n.y = mvp[7] - mvp[6]; f.p[5].n.z = mvp[11] - mvp[10]; f.p[5].d = mvp[15] - mvp[14];
        for (int i = 0; i < 6; ++i) {
            double len = sqrt(f.p[i].n.x * f.p[i].n.x + f.p[i].n.y * f.p[i].n.y + f.p[i].n.z * f.p[i].n.z);
            f.p[i].n.x /= len; f.p[i].n.y /= len; f.p[i].n.z /= len; f.p[i].d /= len;
        }
        return f;
    }

    static bool intersect(const Frustum& f, const DVec3& center, double radius) {
        for (int i = 0; i < 6; ++i) {
            if (f.p[i].n.x * center.x + f.p[i].n.y * center.y + f.p[i].n.z * center.z + f.p[i].d <= -radius) return false;
        }
        return true;
    }

    inline DMat4 dmat4_identity() {
        DMat4 res = {0};
        res.m[0] = res.m[5] = res.m[10] = res.m[15] = 1.0;
        return res;
    }

    inline DMat4 dmat4_transpose(const DMat4& m) {
        DMat4 res;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                res.m[i + j * 4] = m.m[j + i * 4];
            }
        }
        return res;
    }

    inline DMat4 dmat4_inverse(const DMat4& m) {
        double det;
        DMat4 res;
        double a00 = m.m[0], a01 = m.m[1], a02 = m.m[2], a03 = m.m[3];
        double a10 = m.m[4], a11 = m.m[5], a12 = m.m[6], a13 = m.m[7];
        double a20 = m.m[8], a21 = m.m[9], a22 = m.m[10], a23 = m.m[11];
        double a30 = m.m[12], a31 = m.m[13], a32 = m.m[14], a33 = m.m[15];

        double b00 = a00 * a11 - a01 * a10;
        double b01 = a00 * a12 - a02 * a10;
        double b02 = a00 * a13 - a03 * a10;
        double b03 = a01 * a12 - a02 * a11;
        double b04 = a01 * a13 - a03 * a11;
        double b05 = a02 * a13 - a03 * a12;
        double b06 = a20 * a31 - a21 * a30;
        double b07 = a20 * a32 - a22 * a30;
        double b08 = a20 * a33 - a23 * a30;
        double b09 = a21 * a32 - a22 * a31;
        double b10 = a21 * a33 - a23 * a31;
        double b11 = a22 * a33 - a23 * a32;

        det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
        if (det == 0.0) return dmat4_identity();
        det = 1.0 / det;

        res.m[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det;
        res.m[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det;
        res.m[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det;
        res.m[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
        res.m[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det;
        res.m[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det;
        res.m[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det;
        res.m[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
        res.m[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det;
        res.m[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det;
        res.m[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
        res.m[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
        res.m[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
        res.m[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
        res.m[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
        res.m[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;

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
    static size_t wc(void* p, size_t s, size_t n, void* u) {
        size_t r = s * n; auto* m = (std::vector<uint8_t>*)u;
        size_t o = m->size(); m->resize(o + r);
        memcpy(m->data() + o, p, r); return r;
    }
    static bool Fetch(const char* u, std::vector<uint8_t>& b, long* sc = 0, const char* bt = 0) {
        CURL* c = curl_easy_init(); if (!c) return 0;
        curl_easy_setopt(c, CURLOPT_URL, u); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wc); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L); curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
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
        DVec3 sphereCenter;
        double sphereRadius;
        char contentUri[512];
        Tile** children;
        uint32_t childrenCount;
        uint8_t* payload;
        size_t payloadSize;
        bool isLoaded;
        RenderResources* rendererResources;
    };

    static uint32_t g_TilesLoadedThisFrame = 0;

    struct Tileset {
        Tile* root;
        char baseUrl[1024];
        char token[2048];
        char apiKey[256];
        char sessionToken[1024];
        Tile** renderList;
        uint32_t renderListCount;
        uint32_t renderListCapacity;
        DVec3 originEcef;
        double enuMatrix[16];

        void init() {
            root = nullptr; baseUrl[0] = '\0'; token[0] = '\0'; apiKey[0] = '\0'; sessionToken[0] = '\0';
            renderListCapacity = 32768;
            renderList = (Tile**)Alice::g_Arena.allocate(renderListCapacity * sizeof(Tile*));
            renderListCount = 0;
            originEcef = {0,0,0}; memset(enuMatrix, 0, sizeof(enuMatrix));
        }

        static std::string resolveUri(const std::string& base, const std::string& uri, const char* key, const char* session) {
            std::string res;
            std::string b = base; size_t q = b.find('?'); if (q != std::string::npos) b = b.substr(0, q);
            if (uri.find("://") != std::string::npos) res = uri;
            else if (uri.empty()) res = b;
            else if (uri[0] == '/') {
                size_t hostEnd = b.find('/', 8);
                if (hostEnd != std::string::npos) res = b.substr(0, hostEnd) + uri;
                else res = b + uri;
            } else res = b + uri;

            if (key && key[0] && res.find("key=") == std::string::npos) {
                res += (res.find('?') == std::string::npos ? "?" : "&");
                res += "key="; res += key;
            }
            if (session && session[0] && res.find("session=") == std::string::npos) {
                res += (res.find('?') == std::string::npos ? "?" : "&");
                res += "session="; res += session;
            }
            return res;
        }

        void traverse(Tile* node, const std::string& currentBase, int depth, const Frustum& f) {
            if (!node || depth > 15) return;

            // True Frustum Culling
            double radius = node->sphereRadius;
            if (radius <= 0.0) radius = 1000000.0; // Ensure root is processed per Architect

            DVec3 aliceCenter;
            double dx = node->sphereCenter.x - originEcef.x, dy = node->sphereCenter.y - originEcef.y, dz = node->sphereCenter.z - originEcef.z;
            aliceCenter.x = enuMatrix[0]*dx + enuMatrix[1]*dy + enuMatrix[2]*dz;
            double ny = enuMatrix[4]*dx + enuMatrix[5]*dy + enuMatrix[6]*dz;
            double uz = enuMatrix[8]*dx + enuMatrix[9]*dy + enuMatrix[10]*dz;
            aliceCenter.y = uz; aliceCenter.z = -ny;
            if (!intersect(f, aliceCenter, radius)) return;

            bool allChildrenLoaded = (node->childrenCount > 0);
            if (node->childrenCount > 0) {
                for (uint32_t i=0; i<node->childrenCount; ++i) if (!node->children[i]->isLoaded) allChildrenLoaded = false;
            }

            if (node->contentUri[0] != '\0' && !node->isLoaded && g_TilesLoadedThisFrame < 20) {
                std::vector<uint8_t> buffer; long sc = 0;
                std::string url = resolveUri(currentBase, node->contentUri, apiKey, sessionToken);
                if (CesiumNetwork::Fetch(url.c_str(), buffer, &sc, token[0] ? token : nullptr)) {
                    g_TilesLoadedThisFrame++;
                    printf("[GEPR] Loading tile: %s (Depth: %d, sc: %ld)\n", node->contentUri, depth, sc); fflush(stdout);
                    if (strstr(node->contentUri, ".json")) {
                        auto j = AliceJson::parse(buffer.data(), buffer.size());
                        if (j.type != AliceJson::J_NULL) {
                            std::string b = url; size_t q = b.find('?'); if (q != std::string::npos) b = b.substr(0, q);
                            std::string nextBase = b.substr(0, b.find_last_of('/') + 1);
                            DMat4 pt = node->transform;
                            parseNode(node, j["root"], pt, nextBase, depth);
                        }
                    } else {
                        node->payloadSize = buffer.size();
                        node->payload = (uint8_t*)Alice::g_Arena.allocate(node->payloadSize);
                        if (node->payload) memcpy(node->payload, buffer.data(), node->payloadSize);
                    }
                    node->isLoaded = true;
                }
            }

            if (node->isLoaded && node->payload && !allChildrenLoaded) {
                if (renderListCount < renderListCapacity) renderList[renderListCount++] = node;
            }
            if (node->childrenCount > 0) {
                for (uint32_t i = 0; i < node->childrenCount; ++i) traverse(node->children[i], currentBase, depth + 1, f);
            }
        }

        void parseNode(Tile* tile, const AliceJson::JsonValue& jNode, const DMat4& parentTransform, const std::string& currentBase, int depth) {
            if (depth > 15) return;
            memset(tile, 0, sizeof(Tile));
            DMat4 localTransform = dmat4_identity();
            if (jNode.contains("transform")) {
                auto arr = jNode["transform"];
                for (int i = 0; i < 16; ++i) localTransform.m[i] = (double)arr[i];
            }
            tile->transform = dmat4_mul(parentTransform, localTransform);

            if (jNode.contains("boundingVolume")) {
                auto bv = jNode["boundingVolume"];
                if (bv.contains("sphere")) {
                    auto s = bv["sphere"];
                    tile->sphereCenter = {(double)s[0], (double)s[1], (double)s[2]};
                    tile->sphereRadius = (double)s[3];
                } else if (bv.contains("box")) {
                    auto b = bv["box"];
                    tile->sphereCenter = {(double)b[0], (double)b[1], (double)b[2]};
                    double hx = (double)b[3], hy = (double)b[7], hz = (double)b[11];
                    tile->sphereRadius = sqrt(hx*hx + hy*hy + hz*hz);
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
                }
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

    static void* cgltf_alloc(void* user, size_t size) { return malloc(size); }
    static void cgltf_free_cb(void* user, void* ptr) { free(ptr); }

    static void initShaders() {
        const char* vs = "#version 400 core\nlayout(location=0)in vec3 p;uniform mat4 uMVP;out vec3 WorldPos;void main(){WorldPos=p;gl_Position=uMVP*vec4(p,1.0);}";
        const char* fs = "#version 400 core\nout vec4 FragColor;in vec3 WorldPos;void main(){vec3 dpdx=dFdx(WorldPos);vec3 dpdy=dFdy(WorldPos);vec3 N=normalize(cross(dpdx,dpdy));vec3 L=normalize(vec3(0.5,0.8,0.6));float diff=max(dot(N,L),0.0);if(diff<0.0&&dot(-N,L)>0.0){diff=max(dot(-N,L),0.0);}vec3 color=vec3(0.176)+diff*0.6;FragColor=vec4(color,1.0);}";
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(Tileset* ts, cgltf_node* node, const DMat4& transform, float* vbo, uint32_t* ebo, uint32_t& vIdx, uint32_t& eIdx, uint32_t frameIdx, const DVec3& rtc) {
        DMat4 m = transform;
        if (node->has_matrix) { DMat4 nm; for(int i=0;i<16;++i) nm.m[i] = (double)node->matrix[i]; m = dmat4_mul(transform, nm); }
        else {
            if (node->has_translation) m = dmat4_translate(m, {(double)node->translation[0], (double)node->translation[1], (double)node->translation[2]});
            if (node->has_rotation) m = dmat4_mul(m, dmat4_from_quat((double*)node->rotation));
            if (node->has_scale) m = dmat4_scale(m, {(double)node->scale[0], (double)node->scale[1], (double)node->scale[2]});
        }
        DMat4 invTrans = dmat4_transpose(dmat4_inverse(m));
        if (node->mesh) {
            for (size_t i=0; i<node->mesh->primitives_count; ++i) {
                cgltf_primitive* p = &node->mesh->primitives[i];
                cgltf_accessor *pa=0, *na=0; 
                for (size_t k=0; k<p->attributes_count; ++k) {
                    if (p->attributes[k].type == cgltf_attribute_type_position) pa = p->attributes[k].data;
                    if (p->attributes[k].type == cgltf_attribute_type_normal) na = p->attributes[k].data;
                }
                if (!pa) continue;
                uint32_t offset = vIdx;
                for (size_t k=0; k<pa->count; ++k) {
                    if (vIdx >= 250000) break; // Bounds check
                    float pos[3]; cgltf_accessor_read_float(pa, k, pos, 3);
                    
                    // ARCHITECT DIRECTIVE: wp = (m * pos) + CESIUM_RTC
                    DVec4 wp_local = dmat4_mul_vec4(m, {(double)pos[0], (double)pos[1], (double)pos[2], 1.0});
                    DVec4 wp = { wp_local.x + rtc.x, wp_local.y + rtc.y, wp_local.z + rtc.z, 1.0 };
                    
                    if (vIdx < 1 && frameIdx % 100 == 0) {
                        printf("[GEPR] Vertex %u: pos(%.1f, %.1f, %.1f), wp(%.1f, %.1f, %.1f), orig(%.1f, %.1f, %.1f)\n", 
                               vIdx, pos[0], pos[1], pos[2], wp.x, wp.y, wp.z, ts->originEcef.x, ts->originEcef.y, ts->originEcef.z);
                        double dx_dbg = wp.x - ts->originEcef.x, dy_dbg = wp.y - ts->originEcef.y, dz_dbg = wp.z - ts->originEcef.z;
                        printf("[GEPR] Delta ECEF: %.1f, %.1f, %.1f\n", dx_dbg, dy_dbg, dz_dbg);
                    }

                    double dx = wp.x - ts->originEcef.x, dy = wp.y - ts->originEcef.y, dz = wp.z - ts->originEcef.z;
                    float ex = (float)(ts->enuMatrix[0]*dx + ts->enuMatrix[1]*dy + ts->enuMatrix[2]*dz);
                    float ny = (float)(ts->enuMatrix[4]*dx + ts->enuMatrix[5]*dy + ts->enuMatrix[6]*dz);
                    float uz = (float)(ts->enuMatrix[8]*dx + ts->enuMatrix[9]*dy + ts->enuMatrix[10]*dz);
                    vbo[vIdx*6+0] = ex; vbo[vIdx*6+1] = uz; vbo[vIdx*6+2] = -ny; 

                    float nrm[3] = {0,0,1};
                    if (na) cgltf_accessor_read_float(na, k, nrm, 3);
                    DVec4 wn = dmat4_mul_vec4(invTrans, {(double)nrm[0], (double)nrm[1], (double)nrm[2], 0.0});
                    double n_ex = ts->enuMatrix[0]*wn.x + ts->enuMatrix[1]*wn.y + ts->enuMatrix[2]*wn.z;
                    double n_ny = ts->enuMatrix[4]*wn.x + ts->enuMatrix[5]*wn.y + ts->enuMatrix[6]*wn.z;
                    double n_uz = ts->enuMatrix[8]*wn.x + ts->enuMatrix[9]*wn.y + ts->enuMatrix[10]*wn.z;
                    
                    if (vIdx < 1 && frameIdx % 100 == 0) {
                        printf("[GEPR] Normal %u: raw(%.2f, %.2f, %.2f), ECEF(%.2f, %.2f, %.2f), ENU(%.2f, %.2f, %.2f)\n", 
                               vIdx, nrm[0], nrm[1], nrm[2], wn.x, wn.y, wn.z, n_ex, n_ny, n_uz);
                    }

                    vbo[vIdx*6+3] = (float)n_ex; vbo[vIdx*6+4] = (float)n_uz; vbo[vIdx*6+5] = (float)-n_ny;

                    vIdx++;
                }
                if (p->indices) {
                    for (size_t k=0; k<p->indices->count; ++k) {
                        if (eIdx >= 500000) break; // Bounds check
                        ebo[eIdx++] = (uint32_t)cgltf_accessor_read_index(p->indices, k) + offset;
                    }
                }
            }
        }
        for (size_t i=0; i<node->children_count; ++i) processNode(ts, node->children[i], m, vbo, ebo, vIdx, eIdx, frameIdx, rtc);
    }

    static void setup() {
        printf("[GEPR] setup started\n"); fflush(stdout);
        if (!Alice::g_Arena.memory) Alice::g_Arena.init(2048ULL*1024ULL*1024ULL); // 2GB
        initShaders(); g_Tileset.init();
        double lat = -22.951916 * 0.0174532925, lon = -43.210487 * 0.0174532925;
        g_Tileset.originEcef = lla_to_ecef(lat, lon, 710.0); denu_matrix(g_Tileset.enuMatrix, lat, lon);
        
        char ionToken[2048];
        if (!Alice::ApiKeyReader::GetCesiumToken(ionToken, 2048)) {
            printf("[GEPR] No Cesium Ion Token found in env or API_KEYS.txt\n"); return;
        }
        
        printf("[GEPR] Fetching asset endpoint (2275207)...\n"); fflush(stdout);
        std::vector<uint8_t> handshake; long sc=0;
        char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/2275207/endpoint?access_token=%s", ionToken);
        if (CesiumNetwork::Fetch(url, handshake, &sc)) {
            auto jhs = AliceJson::parse(handshake.data(), handshake.size());
            if (jhs.type == AliceJson::J_NULL) { printf("[GEPR] Handshake parse failed\n"); return; }
            
            std::string turl;
            if (jhs.contains("url")) turl = jhs["url"].get<std::string>();
            else if (jhs.contains("options") && jhs["options"].contains("url")) turl = jhs["options"]["url"].get<std::string>();
            
            std::string atok;
            if (jhs.contains("accessToken")) atok = jhs["accessToken"].get<std::string>();
            
            if (!atok.empty()) strncpy(g_Tileset.token, atok.c_str(), 2047);
            else g_Tileset.token[0] = '\0';

            if (!turl.empty()) {
                size_t kPos = turl.find("key=");
                if (kPos != std::string::npos) {
                    size_t kEnd = turl.find('&', kPos);
                    std::string key = turl.substr(kPos + 4, kEnd == std::string::npos ? std::string::npos : kEnd - (kPos + 4));
                    strncpy(g_Tileset.apiKey, key.c_str(), 255);
                }

                strncpy(g_Tileset.baseUrl, turl.substr(0, turl.find_last_of('/')+1).c_str(), 1023);
                printf("[GEPR] Tileset URL: %s\n", turl.c_str()); fflush(stdout);
                std::vector<uint8_t> ts;
                if (CesiumNetwork::Fetch(turl.c_str(), ts, &sc, g_Tileset.token[0] ? g_Tileset.token : nullptr)) {
                    auto jts = AliceJson::parse(ts.data(), ts.size());
                    if (jts.type != AliceJson::J_NULL) {
                        g_Tileset.root = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                        if (g_Tileset.root) {
                            memset(g_Tileset.root, 0, sizeof(Tile));
                            g_Tileset.parseNode(g_Tileset.root, jts["root"], dmat4_identity(), g_Tileset.baseUrl, 0);
                            printf("[GEPR] Tileset root parsed successfully.\n"); fflush(stdout);
                        }
                    }
                }
            }
        }
        printf("[GEPR] setup finished\n"); fflush(stdout);
    }

    static void update(float dt) {
        AliceViewer* av = AliceViewer::instance();
        if (g_FrameCount == 0) {
            av->camera.focusPoint = {0,0,0}; av->camera.distance = 500.0f; av->camera.pitch = 0.4f; av->camera.yaw = 0.7f;
        }
        g_TilesLoadedThisFrame = 0;
        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        M4 v = av->camera.getViewMatrix(), p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        Frustum f = extractFrustum(mvp);
        g_Tileset.renderListCount = 0; if (g_Tileset.root) g_Tileset.traverse(g_Tileset.root, g_Tileset.baseUrl, 0, f);
    }

    static void draw() {
        uint32_t frameIdx = g_FrameCount++;
        AliceViewer* av = AliceViewer::instance();
        av->backColor = {0,0,0};
        if (frameIdx % 100 == 0) { printf("[GEPR] Frame %u, RenderList: %u\n", frameIdx, g_Tileset.renderListCount); fflush(stdout); }
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
                if (cgltf_parse(&opt, t->payload, t->payloadSize, &data) == cgltf_result_success) {
                    cgltf_load_buffers(&opt, data, 0);
                    RenderResources* res = (RenderResources*)Alice::g_Arena.allocate(sizeof(RenderResources));
                    if (res) {
                        memset(res, 0, sizeof(RenderResources));
                        std::vector<float> vboBuffer(250000 * 6);
                        std::vector<uint32_t> eboBuffer(500000);
                        float* vbo = vboBuffer.data();
                        uint32_t* ebo = eboBuffer.data();
                        if (vbo && ebo) {
                            uint32_t vIdx=0, eIdx=0; DMat4 yup = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
                            DMat4 rootTransform = t->transform;
                            DVec3 rtc = {0,0,0};
                            
                            // Manual raw JSON scan for CESIUM_RTC
                            const char* payloadStr = (const char*)t->payload;
                            size_t searchLen = (t->payloadSize > 10000) ? 10000 : t->payloadSize;
                            const char* extLoc = nullptr;
                            for (size_t s = 0; s < searchLen - 15; ++s) {
                                if (strncmp(payloadStr + s, "\"CESIUM_RTC\"", 12) == 0) {
                                    extLoc = payloadStr + s;
                                    break;
                                }
                            }
                            
                            if (extLoc) {
                                const char* ctr = strstr(extLoc, "\"center\"");
                                if (ctr) {
                                    const char* cb = strchr(ctr, '[');
                                    if (cb) {
                                        const char* p2 = cb + 1;
                                        rtc.x = atof(p2); p2 = strchr(p2, ','); if(p2) { rtc.y = atof(p2+1); p2 = strchr(p2+1, ','); if(p2) rtc.z = atof(p2+1); }
                                    }
                                }
                            }
                            
                            if (frameIdx % 100 == 0 && vIdx < 1) {
                                printf("[GEPR] Extracted RTC: (%.1f, %.1f, %.1f)\n", rtc.x, rtc.y, rtc.z);
                            }

                            rootTransform = dmat4_mul(rootTransform, yup);
                            
                            if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) {
                                processNode(&g_Tileset, data->scene->nodes[k], rootTransform, vbo, ebo, vIdx, eIdx, frameIdx, rtc);
                            }
                            glGenVertexArrays(1, &res->vao); glBindVertexArray(res->vao);
                            glGenBuffers(1, &res->vbo); glBindBuffer(GL_ARRAY_BUFFER, res->vbo); glBufferData(GL_ARRAY_BUFFER, vIdx*6*4, vbo, GL_STATIC_DRAW);
                            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, 24, (void*)12);
                            glGenBuffers(1, &res->ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, eIdx*4, ebo, GL_STATIC_DRAW);
                            res->count = eIdx;
                            t->rendererResources = res; // Atomic commit
                        }
                    }
                    cgltf_free(data);
                }
            }
            if (t->rendererResources) { glBindVertexArray(t->rendererResources->vao); glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); g_TotalFrustumVertices += (uint64_t)t->rendererResources->count; }
        }
        if (frameIdx == 600) {
            unsigned char* px = (unsigned char*)malloc(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
            printf("[GEPR] Sample Pixels: (%d,%d,%d), (%d,%d,%d)\n", px[0], px[1], px[2], px[w*h*3/2], px[w*h*3/2+1], px[w*h*3/2+2]);
            long long hits = 0; for(int i=0; i<w*h; ++i) if(px[i*3]>50||px[i*3+1]>50||px[i*3+2]>50) hits++;
            printf("frustum_vertex_count: %llu\n", g_TotalFrustumVertices);
            printf("pixel_coverage_percentage: %.2f%%\n", (double)hits/(w*h)*100.0);
            stbi_flip_vertically_on_write(1); stbi_write_png("production_framebuffer.png", w, h, 3, px, w*3); free(px);
            printf("[GEPR] Mission Accomplished. Exiting.\n"); fflush(stdout);
            exit(0);
        }
    }
}


#ifdef CESIUM_GEPR_RUN_TEST
extern "C" void setup() { CesiumGEPR::setup(); }
extern "C" void update(float dt) { CesiumGEPR::update(dt); }
extern "C" void draw() { CesiumGEPR::draw(); }
#endif

#endif // CESIUM_GEPR_H