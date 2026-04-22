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

    inline DVec3 ecef_to_lla(double x, double y, double z) {
        const double a = 6378137.0;
        const double e2 = 0.0066943799901413165;
        double b = sqrt(a * a * (1.0 - e2));
        double ep2 = (a * a - b * b) / (b * b);
        double p = sqrt(x * x + y * y);
        double th = atan2(a * z, b * p);
        double lon = atan2(y, x);
        double lat = atan2(z + ep2 * b * pow(sin(th), 3), p - e2 * a * pow(cos(th), 3));
        double N = a / sqrt(1.0 - e2 * sin(lat) * sin(lat));
        double alt = p / cos(lat) - N;
        return {lat, lon, alt};
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
        char contentUri[512];
        char baseUrl[512];
        Tile** children;
        uint32_t childrenCount;
        uint8_t* payload;
        size_t payloadSize;
        bool isLoaded;
        bool isCulled;
        RenderResources* rendererResources;
        // Implicit Tiling
        bool isImplicit;
        int level, x, y, z;
        char templateUri[512];
        double region[6]; // west, south, east, north, minAlt, maxAlt
        double geometricError;
        double radius;
        DVec4 centerEcef;
    };

    static void replaceString(std::string& s, const std::string& f, const std::string& t) {
        size_t p = 0; while ((p = s.find(f, p)) != std::string::npos) { s.replace(p, f.length(), t); p += t.length(); }
    }

    static uint32_t g_TilesLoadedThisFrame = 0;
    static DVec3 g_OriginEcef;
    static DVec3 g_TargetEcef;
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

        void init() {
            root = nullptr; baseUrl[0] = '\0'; token[0] = '\0'; apiKey[0] = '\0'; sessionToken[0] = '\0';
            renderListCapacity = 32768;
            renderList = (Tile**)Alice::g_Arena.allocate(renderListCapacity * sizeof(Tile*));
            renderListCount = 0;
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

        void traverse(Tile* node, const std::string& currentBase, int depth) {
            if (!node || depth > 22 || node->isCulled) return;
            std::string myBase = node->baseUrl[0] ? node->baseUrl : currentBase;
            
            // Subdivide if necessary (Implicit Tiling)
            if (node->isImplicit && node->childrenCount == 0 && depth < 22) {
                bool isOctree = (std::string(node->templateUri).find("{z}") != std::string::npos);
                node->childrenCount = isOctree ? 8 : 4;
                node->children = (Tile**)Alice::g_Arena.allocate(node->childrenCount * sizeof(Tile*));
                double midLon = (node->region[0] + node->region[2]) * 0.5, midLat = (node->region[1] + node->region[3]) * 0.5;
                double midAlt = (node->region[4] + node->region[5]) * 0.5;

                for (uint32_t i = 0; i < node->childrenCount; ++i) {
                    Tile* c = (Tile*)Alice::g_Arena.allocate(sizeof(Tile)); memset(c, 0, sizeof(Tile));
                    c->isImplicit = true; c->level = node->level + 1;
                    c->x = node->x * 2 + (i % 2);
                    c->y = node->y * 2 + ((i / 2) % 2);
                    c->z = isOctree ? (node->z * 2 + (i / 4)) : 0;
                    c->geometricError = node->geometricError * 0.5;
                    strncpy(c->baseUrl, myBase.c_str(), 511); strncpy(c->templateUri, node->templateUri, 511);

                    if (i % 2 == 0) { c->region[0]=node->region[0]; c->region[2]=midLon; } else { c->region[0]=midLon; c->region[2]=node->region[2]; }
                    if ((i / 2) % 2 == 0) { c->region[1]=node->region[1]; c->region[3]=midLat; } else { c->region[1]=midLat; c->region[3]=node->region[3]; }
                    if (isOctree) {
                        if (i / 4 == 0) { c->region[4]=node->region[4]; c->region[5]=midAlt; } else { c->region[4]=midAlt; c->region[5]=node->region[5]; }
                    } else { c->region[4] = node->region[4]; c->region[5] = node->region[5]; }

                    std::string uri = c->templateUri;
                    replaceString(uri, "{level}", std::to_string(c->level));
                    replaceString(uri, "{x}", std::to_string(c->x));
                    replaceString(uri, "{y}", std::to_string(c->y));
                    if (isOctree) replaceString(uri, "{z}", std::to_string(c->z));
                    strncpy(c->contentUri, uri.c_str(), 511);
                    c->transform = node->transform;

                    double subAvgLon = (c->region[0] + c->region[2]) * 0.5, subAvgLat = (c->region[1] + c->region[3]) * 0.5;
                    DVec3 cc = lla_to_ecef(subAvgLat, subAvgLon, (c->region[4] + c->region[5]) * 0.5);
                    c->centerEcef = {cc.x, cc.y, cc.z, 1.0};
                    DVec3 corner = lla_to_ecef(c->region[3], c->region[2], c->region[5]);
                    double cdx = cc.x - corner.x, cdy = cc.y - corner.y, cdz = cc.z - corner.z;
                    c->radius = sqrt(cdx*cdx + cdy*cdy + cdz*cdz);
                    node->children[i] = c;
                }
            }

            // LOAD
            if (node->contentUri[0] != '\0' && !node->isLoaded) {
                std::vector<uint8_t> buffer; long sc = 0; std::string url = resolveUri(myBase, node->contentUri, apiKey, sessionToken);
                if (CesiumNetwork::Fetch(url.c_str(), buffer, &sc, token[0] ? token : nullptr)) {
                    if (strstr(node->contentUri, ".json")) {
                        auto j = AliceJson::parse(buffer.data(), buffer.size());
                        if (j.type != AliceJson::J_NULL) {
                            std::string b = url; size_t q = b.find('?'); if (q != std::string::npos) b = b.substr(0, q);
                            std::string nextBase = b.substr(0, b.find_last_of('/') + 1);
                            
                            Tile temp; memset(&temp, 0, sizeof(Tile));
                            parseNode(&temp, j["root"], node->transform, nextBase, depth);
                            
                            node->childrenCount = temp.childrenCount;
                            node->children = temp.children;
                            node->geometricError = temp.geometricError;
                            if (temp.radius > 0 && temp.radius < 9999999.0) {
                                node->centerEcef = temp.centerEcef;
                                node->radius = temp.radius;
                                for(int i=0;i<6;++i) node->region[i] = temp.region[i];
                            }
                            node->payload = nullptr; 
                            node->contentUri[0] = '\0'; 
                        }
                    } else {
                        node->payload = (uint8_t*)Alice::g_Arena.allocate(buffer.size());
                        memcpy(node->payload, buffer.data(), buffer.size());
                        node->payloadSize = buffer.size();
                    }
                    node->isLoaded = true;
                }
            }

            bool childrenHandled = false;
            if (node->childrenCount > 0 && depth < 20) {
                for (uint32_t i = 0; i < node->childrenCount; ++i) {
                    if (!node->children[i]->isCulled) {
                        traverse(node->children[i], myBase, depth + 1);
                        childrenHandled = true;
                    }
                }
            }

            if (!childrenHandled && (node->contentUri[0] != '\0' || node->payload)) {
                if (renderListCount < renderListCapacity) {
                    renderList[renderListCount++] = node;
                }
            }
        }

        void parseNode(Tile* tile, const AliceJson::JsonValue& jNode, const DMat4& parentTransform, const std::string& currentBase, int depth) {
            if (depth > 25) return;
            strncpy(tile->baseUrl, currentBase.c_str(), 511);
            DMat4 localTransform = dmat4_identity();
            if (jNode.contains("transform")) {
                auto arr = jNode["transform"];
                for (int i = 0; i < 16; ++i) localTransform.m[i] = (double)arr[i];
            }
            tile->transform = dmat4_mul(parentTransform, localTransform);
            if (jNode.contains("geometricError")) tile->geometricError = (double)jNode["geometricError"];
            
            if (jNode.contains("content") && jNode["content"].contains("uri")) {
                strncpy(tile->contentUri, jNode["content"]["uri"].get<std::string>().c_str(), 511);
            } else if (jNode.contains("content") && jNode["content"].contains("url")) {
                strncpy(tile->contentUri, jNode["content"]["url"].get<std::string>().c_str(), 511);
            }

            if (jNode.contains("implicitTiling") || (jNode.contains("extensions") && jNode["extensions"].contains("3DTILES_implicit_tiling"))) {
                tile->isImplicit = true;
                if (jNode.contains("content") && jNode["content"].contains("uri")) {
                    strncpy(tile->templateUri, jNode["content"]["uri"].get<std::string>().c_str(), 511);
                } else if (jNode.contains("extensions") && jNode["extensions"].contains("3DTILES_implicit_tiling")) {
                    auto imp = jNode["extensions"]["3DTILES_implicit_tiling"];
                    if (imp.contains("subdivisionScheme") && imp["subdivisionScheme"].get<std::string>() == "OCTREE") {
                         // Default template if missing but extension present
                    }
                }
            }

            if (jNode.contains("boundingVolume") && jNode["boundingVolume"].contains("region")) {
                auto r = jNode["boundingVolume"]["region"];
                for (int i = 0; i < 6; ++i) tile->region[i] = (double)r[i];
                double avgLon = (tile->region[0] + tile->region[2]) * 0.5, avgLat = (tile->region[1] + tile->region[3]) * 0.5;
                DVec3 c = lla_to_ecef(avgLat, avgLon, (tile->region[4] + tile->region[5]) * 0.5);
                tile->centerEcef = {c.x, c.y, c.z, 1.0};
                DVec3 corner = lla_to_ecef(tile->region[3], tile->region[2], tile->region[5]);
                double dx = c.x - corner.x, dy = c.y - corner.y, dz = c.z - corner.z;
                tile->radius = sqrt(dx*dx + dy*dy + dz*dz);
            } else if (jNode.contains("boundingVolume") && jNode["boundingVolume"].contains("box")) {
                auto b = jNode["boundingVolume"]["box"];
                tile->region[0] = -180.0; // sentinel
                DVec4 lc = { (double)b[0], (double)b[1], (double)b[2], 1.0 };
                tile->centerEcef = dmat4_mul_vec4(tile->transform, lc);
                double u = sqrt((double)b[3]*(double)b[3]+(double)b[4]*(double)b[4]+(double)b[5]*(double)b[5]);
                double v = sqrt((double)b[6]*(double)b[6]+(double)b[7]*(double)b[7]+(double)b[8]*(double)b[8]);
                double w = sqrt((double)b[9]*(double)b[9]+(double)b[10]*(double)b[10]+(double)b[11]*(double)b[11]);
                tile->radius = u + v + w;
            } else if (jNode.contains("boundingVolume") && jNode["boundingVolume"].contains("sphere")) {
                auto s = jNode["boundingVolume"]["sphere"];
                tile->region[0] = -180.0;
                DVec4 lc = { (double)s[0], (double)s[1], (double)s[2], 1.0 };
                tile->centerEcef = dmat4_mul_vec4(tile->transform, lc);
                tile->radius = (double)s[3];
            } else {
                // If it really doesn't have a bounding volume, default
                tile->region[0] = -180.0;
                tile->centerEcef = dmat4_mul_vec4(tile->transform, {0,0,0,1});
                tile->radius = 10000000.0; // very large default
            }

            double cdx = tile->centerEcef.x - g_OriginEcef.x, cdy = tile->centerEcef.y - g_OriginEcef.y, cdz = tile->centerEcef.z - g_OriginEcef.z;
            float dist = (float)sqrt(cdx*cdx + cdy*cdy + cdz*cdz);
            if ((dist - tile->radius) > 10000.0f) {
                tile->isCulled = true;
            }

            if (jNode.contains("children")) {
                auto children = jNode["children"];
                tile->childrenCount = children.size();
                tile->children = (Tile**)Alice::g_Arena.allocate(tile->childrenCount * sizeof(Tile*));
                for (uint32_t i = 0; i < tile->childrenCount; ++i) {
                    tile->children[i] = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                    memset(tile->children[i], 0, sizeof(Tile));
                    // Pass current level as depth + 1
                    parseNode(tile->children[i], children[i], tile->transform, currentBase, depth + 1);
                    tile->children[i]->level = tile->level + 1; // Explicit level setting
                }
            }
        }
    };

    static Tileset g_Tileset;
    static GLuint g_Program = 0;
    static uint32_t g_FrameCount = 0;
    static uint64_t g_TotalFrustumVertices = 0;

    static void* cgltf_alloc(void* user, size_t size) { return Alice::g_Arena.allocate(size); }
    static void cgltf_free_cb(void* user, void* ptr) {}

    static void initShaders() {
        const char* vs = "#version 400 core\n"
                         "layout(location=0) in vec3 p;\n"
                         "layout(location=1) in vec3 n;\n"
                         "uniform mat4 uMVP;\n"
                         "uniform mat4 uV;\n"
                         "out vec3 vN;\n"
                         "void main() {\n"
                         "    vN = normalize((uV * vec4(n, 0.0)).xyz);\n"
                         "    gl_Position = uMVP * vec4(p, 1.0);\n"
                         "}\n";
        const char* fs = "#version 400 core\n"
                         "out vec4 f;\n"
                         "in vec3 vN;\n"
                         "void main() {\n"
                         "    vec3 N = normalize(vN);\n"
                         "    if (!gl_FrontFacing) N = -N;\n"
                         "    vec3 L = normalize(vec3(0.5, 0.8, 0.6));\n"
                         "    float d = max(dot(N, L), 0.0);\n"
                         "    vec3 color = vec3(0.9, 0.85, 0.8) * d + vec3(0.15);\n" // Warmer light, lower ambient
                         "    f = vec4(color, 1.0);\n"
                         "}\n";
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(cgltf_node* node, const DMat4& transform, std::vector<float>& vbo, std::vector<uint32_t>& ebo) {
        if (vbo.size() > 6000000 || ebo.size() > 2000000) return; // Safety bounds
        DMat4 m = transform;
        if (node->has_matrix) { DMat4 nm; for(int i=0;i<16;++i) nm.m[i] = (double)node->matrix[i]; m = dmat4_mul(transform, nm); }
        else {
            if (node->has_translation) m = dmat4_translate(m, {(double)node->translation[0], (double)node->translation[1], (double)node->translation[2]});
            if (node->has_rotation) m = dmat4_mul(m, dmat4_from_quat((double*)node->rotation));
            if (node->has_scale) m = dmat4_scale(m, {(double)node->scale[0], (double)node->scale[1], (double)node->scale[2]});
        }
        if (node->mesh) {
            for (size_t i=0; i<node->mesh->primitives_count; ++i) {
                cgltf_primitive* p = &node->mesh->primitives[i];
                cgltf_accessor *pa=0; for (size_t k=0; k<p->attributes_count; ++k) if (p->attributes[k].type == cgltf_attribute_type_position) pa = p->attributes[k].data;
                cgltf_accessor *na=0; for (size_t k=0; k<p->attributes_count; ++k) if (p->attributes[k].type == cgltf_attribute_type_normal) na = p->attributes[k].data;
                if (!pa) continue;
                uint32_t offset = (uint32_t)(vbo.size() / 6);
                if (na) {
                    for (size_t k=0; k<pa->count; ++k) {
                        float pos[3], nrm[3]; cgltf_accessor_read_float(pa, k, pos, 3); cgltf_accessor_read_float(na, k, nrm, 3);
                        DVec4 wp = dmat4_mul_vec4(m, {(double)pos[0], (double)pos[1], (double)pos[2], 1.0});
                        DVec4 wn = dmat4_mul_vec4(m, {(double)nrm[0], (double)nrm[1], (double)nrm[2], 0.0});
                        double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                        float ex = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                        float ny = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                        float uz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                        vbo.push_back(ex); vbo.push_back(uz); vbo.push_back(-ny);
                        float nx = (float)(g_EnuMatrix[0]*wn.x + g_EnuMatrix[1]*wn.y + g_EnuMatrix[2]*wn.z);
                        float nny = (float)(g_EnuMatrix[4]*wn.x + g_EnuMatrix[5]*wn.y + g_EnuMatrix[6]*wn.z);
                        float nuz = (float)(g_EnuMatrix[8]*wn.x + g_EnuMatrix[9]*wn.y + g_EnuMatrix[10]*wn.z);
                        vbo.push_back(nx); vbo.push_back(nuz); vbo.push_back(-nny);
                    }
                    if (p->indices) for (size_t k=0; k<p->indices->count; ++k) ebo.push_back((uint32_t)cgltf_accessor_read_index(p->indices, k) + offset);
                    else for (size_t k=0; k<pa->count; ++k) ebo.push_back((uint32_t)k + offset);
                } else {
                    size_t count = p->indices ? p->indices->count : pa->count;
                    for (size_t k=0; k+2<count; k+=3) {
                        uint32_t i[3]; 
                        if (p->indices) for(int j=0;j<3;++j) i[j]=(uint32_t)cgltf_accessor_read_index(p->indices, k+j);
                        else for(int j=0;j<3;++j) i[j]=(uint32_t)k+j;
                        float pts[3][3]; for(int j=0;j<3;++j) cgltf_accessor_read_float(pa, i[j], pts[j], 3);
                        DVec3 v[3]; for(int j=0;j<3;++j) {
                            DVec4 wp = dmat4_mul_vec4(m, {(double)pts[j][0], (double)pts[j][1], (double)pts[j][2], 1.0});
                            double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                            v[j] = {(double)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz), (double)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz), -(double)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz)};
                        }
                        double d1x = v[1].x-v[0].x, d1y = v[1].y-v[0].y, d1z = v[1].z-v[0].z;
                        double d2x = v[2].x-v[0].x, d2y = v[2].y-v[0].y, d2z = v[2].z-v[0].z;
                        double nx = d1y*d2z-d1z*d2y, ny = d1z*d2x-d1x*d2z, nz = d1x*d2y-d1y*d2x;
                        double len = sqrt(nx*nx+ny*ny+nz*nz); if(len>1e-9){nx/=len;ny/=len;nz/=len;}
                        for(int j=0;j<3;++j){
                            uint32_t vIdx = (uint32_t)(vbo.size() / 6);
                            vbo.push_back((float)v[j].x); vbo.push_back((float)v[j].y); vbo.push_back((float)v[j].z);
                            vbo.push_back((float)nx); vbo.push_back((float)ny); vbo.push_back((float)nz);
                            ebo.push_back(vIdx);
                        }
                    }
                }
            }
        }
        for (size_t i=0; i<node->children_count; ++i) processNode(node->children[i], m, vbo, ebo);
    }

    static void setup() {
        printf("[GEPR] setup started\n"); fflush(stdout);
        if (!Alice::g_Arena.memory) Alice::g_Arena.init(4096ULL*1024ULL*1024ULL); // 4GB
        if (!Alice::g_Arena.memory) { printf("[FATAL] Arena allocation failed\n"); exit(1); }
        initShaders(); g_Tileset.init();
        double lat = -22.951916 * 0.0174532925, lon = -43.210487 * 0.0174532925;
        g_OriginEcef = lla_to_ecef(lat, lon, 0.0);
        g_TargetEcef = lla_to_ecef(lat, lon, 710.0); // Target the mountain top
        denu_matrix(g_EnuMatrix, lat, lon);
        
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
        g_TilesLoadedThisFrame = 0;
        g_Tileset.renderListCount = 0; if (g_Tileset.root) g_Tileset.traverse(g_Tileset.root, g_Tileset.baseUrl, 0);

        // Find AABB of highest level loaded geometry
        float minX = 1e9, minY = 1e9, minZ = 1e9;
        float maxX = -1e9, maxY = -1e9, maxZ = -1e9;
        int leafCount = 0;
        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            Tile* t = g_Tileset.renderList[i];
            if (t->level > 12 && t->rendererResources) {
                glBindBuffer(GL_ARRAY_BUFFER, t->rendererResources->vbo);
                int size; glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
                if (size > 0) {
                    std::vector<float> vbo(size/sizeof(float));
                    glGetBufferSubData(GL_ARRAY_BUFFER, 0, size, vbo.data());
                    for (size_t k = 0; k < vbo.size(); k += 6) {
                        float x = vbo[k], y = vbo[k+1], z = vbo[k+2];
                        if (x < minX) minX = x; if (y < minY) minY = y; if (z < minZ) minZ = z;
                        if (x > maxX) maxX = x; if (y > maxY) maxY = y; if (z > maxZ) maxZ = z;
                    }
                    leafCount++;
                }
            }
        }

        // Snap camera to geometry AABB
        if (leafCount > 0) {
            float cx = (minX + maxX) * 0.5f;
            float cy = (minY + maxY) * 0.5f;
            float cz = (minZ + maxZ) * 0.5f;
            
            // Auto distance
            float rad = fmax(fmax(maxX - cx, maxY - cy), maxZ - cz);
            if (rad < 1.0f) rad = 100.0f; // Default if too small
            
            // Fix camera rotation to look directly at it, from slightly above and away
            av->camera.focusPoint = {cx, cy, cz};
            av->camera.distance = rad * 3.0f; // Ensure it fits
            av->camera.pitch = 0.5f; // Look slightly down
            av->camera.yaw = 3.14159f * 0.25f; // 45 degrees angle

            if (g_FrameCount % 60 == 0) {
                printf("[CAMERA SNAP] Focus=(%.1f, %.1f, %.1f) Dist=%.1f Rad=%.1f\n", cx, cy, cz, av->camera.distance, rad);
            }
        } else if (g_FrameCount == 0) {
            av->camera.focusPoint = {0.0f, 710.0f, 0.0f}; av->camera.distance = 400.0f; av->camera.pitch = 0.6f; av->camera.yaw = 0.0f;
        }
    }

    static void draw() {
        uint32_t frameIdx = g_FrameCount++;
        AliceViewer* av = AliceViewer::instance();
        av->backColor = {0,0,0};
        if (frameIdx % 100 == 0) { printf("[GEPR] Frame %u, RenderList: %u\n", frameIdx, g_Tileset.renderListCount); fflush(stdout); }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClearDepth(1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); // Depth was 0.0f! Needs to be 1.0f for GL_LESS or GL_LEQUAL unless reversed-Z
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); // Revert to standard depth test for debugging

        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        
        // Custom explicit LookAt
        DVec3 camPos = {av->camera.focusPoint.x, av->camera.focusPoint.y + av->camera.distance * sin(av->camera.pitch), av->camera.focusPoint.z + av->camera.distance * cos(av->camera.pitch)};
        DVec3 fwd = {av->camera.focusPoint.x - camPos.x, av->camera.focusPoint.y - camPos.y, av->camera.focusPoint.z - camPos.z};
        double fLen = sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z); if(fLen>0) { fwd.x/=fLen; fwd.y/=fLen; fwd.z/=fLen; }
        DVec3 right = {fwd.y*0 - fwd.z*1, fwd.z*0 - fwd.x*0, fwd.x*1 - fwd.y*0}; // Cross with Up(0,1,0)
        double rLen = sqrt(right.x*right.x + right.y*right.y + right.z*right.z); if(rLen>0) { right.x/=rLen; right.y/=rLen; right.z/=rLen; }
        DVec3 up = {right.y*fwd.z - right.z*fwd.y, right.z*fwd.x - right.x*fwd.z, right.x*fwd.y - right.y*fwd.x};
        
        M4 v;
        v.m[0]=(float)right.x; v.m[4]=(float)right.y; v.m[8]=(float)right.z; v.m[12]=-(float)(right.x*camPos.x+right.y*camPos.y+right.z*camPos.z);
        v.m[1]=(float)up.x; v.m[5]=(float)up.y; v.m[9]=(float)up.z; v.m[13]=-(float)(up.x*camPos.x+up.y*camPos.y+up.z*camPos.z);
        v.m[2]=-(float)fwd.x; v.m[6]=-(float)fwd.y; v.m[10]=-(float)fwd.z; v.m[14]=(float)(fwd.x*camPos.x+fwd.y*camPos.y+fwd.z*camPos.z);
        v.m[3]=0; v.m[7]=0; v.m[11]=0; v.m[15]=1;

        // Standard perspective
        float aspect = (float)w/h; float fov = av->fov; float nearP = 1.0f; float farP = 100000.0f;
        float f = 1.0f / tan(fov * 0.5f);
        M4 p = {0};
        p.m[0] = f / aspect; p.m[5] = f; p.m[10] = (farP + nearP) / (nearP - farP); p.m[11] = -1.0f; p.m[14] = (2.0f * farP * nearP) / (nearP - farP);

        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        glUseProgram(g_Program); glUniformMatrix4fv(glGetUniformLocation(g_Program, "uMVP"), 1, 0, mvp);
        glUniformMatrix4fv(glGetUniformLocation(g_Program, "uV"), 1, 0, v.m);
        
        g_TotalFrustumVertices = 0;
        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            Tile* t = g_Tileset.renderList[i];
            if (!t->rendererResources && t->payload) {
                cgltf_options opt = {cgltf_file_type_glb};
                cgltf_data* data = 0;
                
                uint8_t* glbPayload = t->payload;
                size_t glbSize = t->payloadSize;
                if (glbSize >= 28 && memcmp(glbPayload, "b3dm", 4) == 0) {
                    uint32_t ftj = *(uint32_t*)(glbPayload + 12);
                    uint32_t ftb = *(uint32_t*)(glbPayload + 16);
                    uint32_t btj = *(uint32_t*)(glbPayload + 20);
                    uint32_t btb = *(uint32_t*)(glbPayload + 24);
                    uint32_t headerLen = 28 + ftj + ftb + btj + btb;
                    if (headerLen < glbSize) {
                        glbPayload += headerLen;
                        glbSize -= headerLen;
                    }
                }
                
                if (cgltf_parse(&opt, glbPayload, glbSize, &data) == cgltf_result_success) {
                    cgltf_load_buffers(&opt, data, 0);
                    RenderResources* res = (RenderResources*)Alice::g_Arena.allocate(sizeof(RenderResources));
                    if (res) {
                        memset(res, 0, sizeof(RenderResources));
                        std::vector<float> vbo; std::vector<uint32_t> ebo;
                        vbo.reserve(1000000); ebo.reserve(500000);
                        if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], t->transform, vbo, ebo);
                        glGenVertexArrays(1, &res->vao); glBindVertexArray(res->vao);
                        glGenBuffers(1, &res->vbo); glBindBuffer(GL_ARRAY_BUFFER, res->vbo); glBufferData(GL_ARRAY_BUFFER, vbo.size()*4, vbo.data(), GL_STATIC_DRAW);
                        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, (void*)0);
                        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, 24, (void*)12);
                        glGenBuffers(1, &res->ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size()*4, ebo.data(), GL_STATIC_DRAW);
                        res->count = (int)ebo.size();
                        t->rendererResources = res; // Atomic commit
                    }
                    cgltf_free(data);
                }
            }
            if (t->rendererResources) { glBindVertexArray(t->rendererResources->vao); glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); g_TotalFrustumVertices += (uint64_t)t->rendererResources->count; }
        }
        if (frameIdx == 590) {
            float minX = 1e9, minY = 1e9, minZ = 1e9;
            float maxX = -1e9, maxY = -1e9, maxZ = -1e9;
            int highLevelCount = 0;
            float sumX = 0, sumY = 0, sumZ = 0;
            for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
                Tile* t = g_Tileset.renderList[i];
                if (t->level > 14 && t->rendererResources) {
                    glBindBuffer(GL_ARRAY_BUFFER, t->rendererResources->vbo);
                    int size; glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
                    std::vector<float> vbo(size/sizeof(float));
                    glGetBufferSubData(GL_ARRAY_BUFFER, 0, size, vbo.data());
                    for (size_t k = 0; k < vbo.size(); k += 6) {
                        float x = vbo[k], y = vbo[k+1], z = vbo[k+2];
                        if (x < minX) minX = x; if (y < minY) minY = y; if (z < minZ) minZ = z;
                        if (x > maxX) maxX = x; if (y > maxY) maxY = y; if (z > maxZ) maxZ = z;
                        sumX += x; sumY += y; sumZ += z;
                        highLevelCount++;
                    }
                }
            }
            if (highLevelCount > 0) {
                printf("[DEBUG] High Level Tiles AABB: Min(%.1f, %.1f, %.1f) Max(%.1f, %.1f, %.1f)\n", minX, minY, minZ, maxX, maxY, maxZ);
                printf("[DEBUG] High Level Tiles Center: (%.1f, %.1f, %.1f)\n", sumX/highLevelCount, sumY/highLevelCount, sumZ/highLevelCount);
            } else {
                printf("[DEBUG] No tiles > level 14 rendered.\n");
            }
            fflush(stdout);
        }
        if (frameIdx == 600) {
            unsigned char* px = (unsigned char*)Alice::g_Arena.allocate(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
            long long hits = 0; for(int i=0; i<w*h; ++i) if(px[i*3]>10||px[i*3+1]>10||px[i*3+2]>10) hits++;
            printf("frustum_vertex_count: %llu\n", g_TotalFrustumVertices);
            printf("pixel_coverage_percentage: %.2f%%\n", (double)hits/(w*h)*100.0);
            stbi_flip_vertically_on_write(1); stbi_write_png("production_framebuffer.png", w, h, 3, px, w*3);
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
