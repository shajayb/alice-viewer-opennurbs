#ifndef CESIUM_GEPR_H
#define CESIUM_GEPR_H

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
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "cgltf.h"
#include "AliceViewer.h"
#include "AliceMemory.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace CesiumMath {
    struct DVec3 { double x, y, z; };
    struct DVec4 { double x, y, z, w; };
    struct DMat4 { double m[16]; };

    inline DMat4 dmat4_identity() { DMat4 r = {0}; r.m[0]=1; r.m[5]=1; r.m[10]=1; r.m[15]=1; return r; }
    inline DVec4 dmat4_mul_vec4(const DMat4& m, const DVec4& v) {
        return {
            m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z + m.m[12]*v.w,
            m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z + m.m[13]*v.w,
            m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14]*v.w,
            m.m[3]*v.x + m.m[7]*v.y + m.m[11]*v.z + m.m[15]*v.w
        };
    }
    inline DMat4 dmat4_mul(const DMat4& a, const DMat4& b) {
        DMat4 r = {0};
        for(int i=0; i<4; ++i) for(int j=0; j<4; ++j)
            for(int k=0; k<4; ++k) r.m[j*4+i] += a.m[k*4+i] * b.m[j*4+k];
        return r;
    }
    inline DMat4 dmat4_translate(const DMat4& m, const DVec3& v) {
        DMat4 r = m;
        r.m[12] = m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z + m.m[12];
        r.m[13] = m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z + m.m[13];
        r.m[14] = m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14];
        r.m[15] = m.m[3]*v.x + m.m[7]*v.y + m.m[11]*v.z + m.m[15];
        return r;
    }
    inline DMat4 dmat4_scale(const DMat4& m, const DVec3& s) {
        DMat4 r = m;
        for(int i=0; i<4; ++i) { r.m[i]*=s.x; r.m[4+i]*=s.y; r.m[8+i]*=s.z; }
        return r;
    }
    inline DMat4 dmat4_from_quat(const double* q) {
        DMat4 res = dmat4_identity();
        double x=q[0], y=q[1], z=q[2], w=q[3];
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

namespace CesiumNetwork {
    static size_t wc(void* p, size_t s, size_t n, void* u) {
        size_t r = s * n; auto* m = (std::vector<uint8_t>*)u;
        size_t o = m->size(); m->resize(o + r);
        memcpy(m->data() + o, p, r); return r;
    }
    static bool Fetch(const char* u, std::vector<uint8_t>& b, long* sc = 0, const char* bt = 0) {
        CURL* c = curl_easy_init(); if (!c) return 0;
        curl_easy_setopt(c, CURLOPT_URL, u); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wc); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L); curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L); 
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "Alice/1.0");
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        struct curl_slist* headers = NULL;
        if (bt && bt[0] != '\0') { char auth[2048]; snprintf(auth, 2048, "Authorization: Bearer %s", bt); headers = curl_slist_append(headers, auth); curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers); }
        CURLcode r = curl_easy_perform(c); if (sc) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, sc);
        if (r != CURLE_OK) { /* Silenced per directive: printf("[CesiumNetwork] CURL Error: %s (URL: %s)\n", curl_easy_strerror(r), u); */ }
        if (headers) curl_slist_free_all(headers); curl_easy_cleanup(c); return r == CURLE_OK;
    }
}

namespace CesiumApiKey {
    static bool GetCesiumToken(char* outBuffer, size_t bufferSize) {
        const char* token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiIzYThhNjI4NS0xOTIxLTRhZjMtYmZiNS1iYmFjMzkxNzFkMDIiLCJpZCI6NDE5MDUyLCJpYXQiOjE3NzY2MjcxNDh9.fwQc5dZ44EUlMDWwylbD1rrApiqEPZbHRJRcyz4jApc";
        if (strlen(token) < bufferSize) { strncpy(outBuffer, token, bufferSize); outBuffer[bufferSize - 1] = '\0'; return true; }
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
            bool ok = CesiumNetwork::Fetch(url.c_str(), res.data, &sc, token.c_str());
            res.statusCode = (int)sc; if (!ok || sc >= 400) res.error = "Fetch Failed";
            return res;
        }
    };
    struct IonAssetEndpoint { std::string url; std::string accessToken; };
    class IonDiscovery {
    public:
        static std::optional<IonAssetEndpoint> discover(AliceCurlClient& client, int64_t assetId, const std::string& ionToken) {
            std::string url = "https://api.cesium.com/v1/assets/" + std::to_string(assetId) + "/endpoint?access_token=" + ionToken;
            HttpResponse response = client.get(url);
            if (response.statusCode != 200) return std::nullopt;
            try {
                auto j = nlohmann::json::parse(response.data.begin(), response.data.end());
                std::string u = j.value("url", "");
                std::string t = j.value("accessToken", "");
                if (u.empty() && j.contains("options")) u = j["options"].value("url", "");
                return IonAssetEndpoint{u, t};
            } catch (...) { return std::nullopt; }
        }
    };

    struct BoundingVolume { DVec3 centerECEF; double radius; bool valid = false; };
    struct RenderResources { GLuint vao=0, vbo=0, ebo=0; int count=0; float* cpuVbo=nullptr; size_t cpuVboSize=0; };
    struct Tile { BoundingVolume bounds; char contentUri[256]; Tile** children; uint32_t childrenCount; uint8_t* payload; size_t payloadSize; bool isLoaded; RenderResources* rendererResources; DMat4 transform; };

    struct Tileset {
        Tile* root; char baseUrl[512]; char token[2048]; AliceCurlClient* client;
        Tile** renderList; uint32_t renderListCount, renderListCapacity;
        void init(AliceCurlClient* c) { root=nullptr; baseUrl[0]='\0'; token[0]='\0'; client=c; renderListCapacity=4096; renderList=(Tile**)Alice::g_Arena.allocate(renderListCapacity*sizeof(Tile*)); renderListCount=0; }
        void updateView(const DVec3& camPos, double radiusLimit) { renderListCount=0; if(root) traverse(root, camPos, radiusLimit); }
        void traverse(Tile* node, const DVec3& camPos, double radiusLimit) {
            if (node->bounds.valid) {
                double dx=node->bounds.centerECEF.x-camPos.x, dy=node->bounds.centerECEF.y-camPos.y, dz=node->bounds.centerECEF.z-camPos.z;
                if (sqrt(dx*dx+dy*dy+dz*dz) > node->bounds.radius + radiusLimit) return;
            }
            if (node->contentUri[0] != '\0' && !node->isLoaded) {
                char fullUrl[1024];
                if (strncmp(node->contentUri, "http", 4) == 0) {
                    strncpy(fullUrl, node->contentUri, 1023);
                } else if (node->contentUri[0] == '/') {
                    // Find host part of baseUrl
                    const char* p = strstr(baseUrl, "://");
                    if (p) {
                        const char* he = strchr(p + 3, '/');
                        if (he) {
                            size_t hostLen = he - baseUrl;
                            if (hostLen < 1023) {
                                memcpy(fullUrl, baseUrl, hostLen);
                                strncpy(fullUrl + hostLen, node->contentUri, 1023 - hostLen);
                            }
                        } else {
                            strncpy(fullUrl, baseUrl, 1023);
                            strncat(fullUrl, node->contentUri, 1023 - strlen(fullUrl));
                        }
                    } else {
                        strncpy(fullUrl, node->contentUri, 1023);
                    }
                } else {
                    snprintf(fullUrl, 1024, "%s%s", baseUrl, node->contentUri);
                }
                
                // Canonicalize // to / (excluding protocol)
                char* p_prot = strstr(fullUrl, "://");
                if (p_prot) {
                    char* p_scan = p_prot + 3;
                    while (*p_scan) {
                        if (*p_scan == '/' && *(p_scan+1) == '/') {
                            char* p_move = p_scan;
                            while (*p_move) { *p_move = *(p_move+1); p_move++; }
                        } else {
                            p_scan++;
                        }
                    }
                }

                auto res = client->get(fullUrl, token);
                if (res.statusCode == 200) {
                    if (strstr(node->contentUri, ".json")) {
                        try { auto j=nlohmann::json::parse(res.data.begin(), res.data.end()); parseNode(node, j["root"], baseUrl, node->transform); } catch(...) {}
                    } else {
                        node->payloadSize=res.data.size(); node->payload=(uint8_t*)Alice::g_Arena.allocate(node->payloadSize); memcpy(node->payload, res.data.data(), node->payloadSize);
                    }
                    node->isLoaded = true;
                }
            }
            if (node->isLoaded && node->payload && renderListCount < renderListCapacity) renderList[renderListCount++] = node;
            for (uint32_t i=0; i<node->childrenCount; ++i) traverse(node->children[i], camPos, radiusLimit);
        }

        void parseNode(Tile* tile, const nlohmann::json& jNode, const char* currentBase, const DMat4& parentTransform) {
            DMat4 localTransform = dmat4_identity();
            if (jNode.contains("transform")) { auto arr=jNode["transform"]; for(int i=0; i<16; ++i) localTransform.m[i]=(double)arr[i]; }
            tile->transform = dmat4_mul(parentTransform, localTransform);
            if (jNode.contains("boundingVolume")) {
                auto bv = jNode["boundingVolume"];
                if (bv.contains("region")) {
                    auto arr=bv["region"]; double rs=arr[1], rn=arr[3], rw=arr[0], re=arr[2];
                    tile->bounds.centerECEF = lla_to_ecef((rs+rn)*0.5, (rw+re)*0.5, 0.0);
                    double latD=(rn-rs)*6378137.0, lonD=(re-rw)*6378137.0*cos((rs+rn)*0.5);
                    tile->bounds.radius = sqrt(latD*latD+lonD*lonD)*0.5; tile->bounds.valid=true;
                } else if (bv.contains("box")) {
                    auto arr=bv["box"]; DVec4 lc={arr[0],arr[1],arr[2],1.0}, wc=dmat4_mul_vec4(tile->transform, lc);
                    tile->bounds.centerECEF={wc.x,wc.y,wc.z}; double ux=arr[3], uy=arr[4], uz=arr[5];
                    tile->bounds.radius=sqrt(ux*ux+uy*uy+uz*uz)*2.0; tile->bounds.valid=true;
                }
            }
            if (jNode.contains("content")) {
                std::string uri;
                if(jNode["content"].contains("uri")) uri=jNode["content"]["uri"].get<std::string>();
                else if(jNode["content"].contains("url")) uri=jNode["content"]["url"].get<std::string>();
                if(!uri.empty()) strncpy(tile->contentUri, uri.c_str(), 255);
            }
            if (jNode.contains("children")) {
                auto childrenArr = jNode["children"]; tile->childrenCount=(uint32_t)childrenArr.size();
                tile->children=(Tile**)Alice::g_Arena.allocate(tile->childrenCount*sizeof(Tile*));
                for (uint32_t i=0; i<tile->childrenCount; ++i) {
                    tile->children[i]=(Tile*)Alice::g_Arena.allocate(sizeof(Tile)); memset(tile->children[i],0,sizeof(Tile));
                    tile->children[i]->transform=dmat4_identity(); parseNode(tile->children[i], childrenArr[i], currentBase, tile->transform);
                }
            }
        }
    };
}

namespace Alice {
    static uint32_t g_FrameCount=0; static bool g_Started=false;
    static CesiumMinimal::AliceCurlClient g_Client; static CesiumMinimal::Tileset g_Tileset;
    static CesiumMath::DVec3 g_OriginEcef; static double g_EnuMatrix[16]; static GLuint g_Program=0;

    static void* cgltf_alloc(void* user, size_t size) { return Alice::g_Arena.allocate(size); }
    static void cgltf_free_cb(void* user, void* ptr) {}

    static void initShaders() {
        const char* vs="#version 400 core\nlayout(location=0)in vec3 p;layout(location=1)in vec3 n;uniform mat4 uMVP;uniform mat4 uV;out vec3 vN;void main(){vN=(uV*vec4(n,0.0)).xyz;gl_Position=uMVP*vec4(p,1.0);}";
        const char* fs="#version 400 core\nout vec4 f;in vec3 vN;void main(){vec3 N=normalize(vN);if(!gl_FrontFacing)N=-N;float d=max(dot(N,normalize(vec3(0.5,0.8,0.6))),0.2);f=vec4(vec3(0.85)*d,1.0);}";
        GLuint v=glCreateShader(GL_VERTEX_SHADER), f=glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v,1,&vs,0); glCompileShader(v); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(cgltf_node* node, const CesiumMath::DMat4& transform, Alice::Buffer<float>& vbo, Alice::Buffer<uint32_t>& ebo) {
        CesiumMath::DMat4 m = transform;
        if (node->has_matrix) { CesiumMath::DMat4 nm; for(int i=0;i<16;++i) nm.m[i]=node->matrix[i]; m=CesiumMath::dmat4_mul(transform, nm); }
        else {
            if(node->has_translation) m=CesiumMath::dmat4_translate(m, {(double)node->translation[0],(double)node->translation[1],(double)node->translation[2]});
            if(node->has_rotation) m=CesiumMath::dmat4_mul(m, CesiumMath::dmat4_from_quat((double*)node->rotation));
            if(node->has_scale) m=CesiumMath::dmat4_scale(m, {(double)node->scale[0],(double)node->scale[1],(double)node->scale[2]});
        }
        if (node->mesh) {
            for (size_t i=0; i<node->mesh->primitives_count; ++i) {
                cgltf_primitive* p=&node->mesh->primitives[i]; uint32_t start=(uint32_t)(vbo.size()/6);
                cgltf_accessor *pa=nullptr, *na=nullptr;
                for(size_t k=0; k<p->attributes_count; ++k) {
                    if(p->attributes[k].type==cgltf_attribute_type_position) pa=p->attributes[k].data;
                    if(p->attributes[k].type==cgltf_attribute_type_normal) na=p->attributes[k].data;
                }
                if(pa) {
                    for(size_t k=0; k<pa->count; ++k) {
                        float v[3]; cgltf_accessor_read_float(pa, k, v, 3);
                        CesiumMath::DVec4 lp={(double)v[0],(double)v[1],(double)v[2],1.0}, wp=CesiumMath::dmat4_mul_vec4(m, lp);
                        CesiumMath::DVec3 enu = {
                            g_EnuMatrix[0]*(wp.x-g_OriginEcef.x) + g_EnuMatrix[4]*(wp.y-g_OriginEcef.y) + g_EnuMatrix[8]*(wp.z-g_OriginEcef.z),
                            g_EnuMatrix[1]*(wp.x-g_OriginEcef.x) + g_EnuMatrix[5]*(wp.y-g_OriginEcef.y) + g_EnuMatrix[9]*(wp.z-g_OriginEcef.z),
                            g_EnuMatrix[2]*(wp.x-g_OriginEcef.x) + g_EnuMatrix[6]*(wp.y-g_OriginEcef.y) + g_EnuMatrix[10]*(wp.z-g_OriginEcef.z)
                        };
                        vbo.push_back((float)enu.x); vbo.push_back((float)enu.y); vbo.push_back((float)enu.z);
                        if(na) { float n[3]; cgltf_accessor_read_float(na, k, n, 3); vbo.push_back(n[0]); vbo.push_back(n[1]); vbo.push_back(n[2]); }
                        else { vbo.push_back(0); vbo.push_back(0); vbo.push_back(1); }
                    }
                }
                if (p->indices) for(size_t k=0; k<p->indices->count; ++k) ebo.push_back(start+(uint32_t)cgltf_accessor_read_index(p->indices, k));
                else for(size_t k=0; k<pa->count; ++k) ebo.push_back(start+(uint32_t)k);
            }
        }
        for (size_t i=0; i<node->children_count; ++i) processNode(node->children[i], m, vbo, ebo);
    }

    static void setup() {
        printf("[CesiumGEPR] setup started\n"); fflush(stdout);
        if (!g_Arena.memory) g_Arena.init(512*1024*1024);
        g_Tileset.init(&g_Client); char token[2048]; CesiumApiKey::GetCesiumToken(token, 2048);
        auto endpoint = CesiumMinimal::IonDiscovery::discover(g_Client, 2275207, token);
        if (endpoint) {
            printf("[CesiumGEPR] Handshake Success\n"); fflush(stdout);
            auto res = g_Client.get(endpoint->url, endpoint->accessToken);
            if (res.statusCode == 200) {
                auto j = nlohmann::json::parse(res.data.begin(), res.data.end());
                std::string base = endpoint->url.substr(0, endpoint->url.find_last_of('/')+1);
                strncpy(g_Tileset.baseUrl, base.c_str(), 511); strncpy(g_Tileset.token, endpoint->accessToken.c_str(), 2047);
                g_Tileset.root = (CesiumMinimal::Tile*)g_Arena.allocate(sizeof(CesiumMinimal::Tile)); memset(g_Tileset.root, 0, sizeof(CesiumMinimal::Tile));
                g_Tileset.root->transform = CesiumMath::dmat4_identity();
                g_Tileset.parseNode(g_Tileset.root, j["root"], g_Tileset.baseUrl, CesiumMath::dmat4_identity());
            }
        }
        double lat = -22.951916 * 0.0174532925, lon = -43.210487 * 0.0174532925;
        g_OriginEcef = CesiumMath::lla_to_ecef(lat, lon, 0.0); CesiumMath::denu_matrix(g_EnuMatrix, lat, lon);
        initShaders(); g_Started = true;
        AliceViewer* av = AliceViewer::instance(); av->camera.focusPoint = {0, 0, 0}; av->camera.distance = 500.0f; av->camera.pitch = 0.5f; av->camera.yaw = 0.8f;
        printf("[CesiumGEPR] setup complete\n"); fflush(stdout);
    }

    static void update(float dt) {
        if (!g_Started) return;
        AliceViewer* av = AliceViewer::instance(); M4 view = av->camera.getViewMatrix(); float inv[16]; CesiumMath::mat4_inverse(inv, view.m);
        CesiumMath::DVec3 camEcef = {
            g_OriginEcef.x + g_EnuMatrix[0]*inv[12] + g_EnuMatrix[4]*inv[13] + g_EnuMatrix[8]*inv[14],
            g_OriginEcef.y + g_EnuMatrix[1]*inv[12] + g_EnuMatrix[5]*inv[13] + g_EnuMatrix[9]*inv[14],
            g_OriginEcef.z + g_EnuMatrix[2]*inv[12] + g_EnuMatrix[6]*inv[13] + g_EnuMatrix[10]*inv[14]
        };
        g_Tileset.updateView(camEcef, 5000000.0);
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
                int glbOff = -1; for(size_t k=0; k<tile->payloadSize-4; ++k) if(memcmp(tile->payload+k, "glTF", 4) == 0) { glbOff = (int)k; break; }
                if (glbOff >= 0) {
                    cgltf_options opt = {cgltf_file_type_glb}; opt.memory.alloc_func = cgltf_alloc; opt.memory.free_func = cgltf_free_cb;
                    cgltf_data* data = 0; if (cgltf_parse(&opt, tile->payload+glbOff, tile->payloadSize-glbOff, &data) == cgltf_result_success) {
                        cgltf_load_buffers(&opt, data, 0); tile->rendererResources = (CesiumMinimal::RenderResources*)g_Arena.allocate(sizeof(CesiumMinimal::RenderResources));
                        memset(tile->rendererResources, 0, sizeof(CesiumMinimal::RenderResources));
                        
                        static float s_vboData[1000000]; static uint32_t s_eboData[1000000];
                        Alice::Buffer<float> vbo; vbo.data = s_vboData; vbo.count = 0; vbo.capacity = 1000000;
                        Alice::Buffer<uint32_t> ebo; ebo.data = s_eboData; ebo.count = 0; ebo.capacity = 1000000;
                        
                        CesiumMath::DMat4 yup = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
                        if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], CesiumMath::dmat4_mul(tile->transform, yup), vbo, ebo);
                        if (vbo.size() > 0) {
                            glGenVertexArrays(1, &tile->rendererResources->vao); glBindVertexArray(tile->rendererResources->vao);
                            glGenBuffers(1, &tile->rendererResources->vbo); glGenBuffers(1, &tile->rendererResources->ebo);
                            glBindBuffer(GL_ARRAY_BUFFER, tile->rendererResources->vbo); glBufferData(GL_ARRAY_BUFFER, vbo.size()*4, vbo.data, GL_STATIC_DRAW);
                            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, 24, (void*)12);
                            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile->rendererResources->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size()*4, ebo.data, GL_STATIC_DRAW);
                            tile->rendererResources->count = (int)ebo.size(); tile->rendererResources->cpuVboSize = vbo.size();
                            tile->rendererResources->cpuVbo = (float*)Alice::g_Arena.allocate(vbo.size() * sizeof(float)); memcpy(tile->rendererResources->cpuVbo, vbo.data, vbo.size() * sizeof(float));
                        }
                        cgltf_free(data);
                    }
                }
            }
            if (tile->rendererResources && tile->rendererResources->vao) {
                glBindVertexArray(tile->rendererResources->vao); glDrawElements(GL_TRIANGLES, tile->rendererResources->count, GL_UNSIGNED_INT, 0);
            }
        }

        static bool g_FrameSceneTriggered = false;
        if (g_FrameCount == 300 && !g_FrameSceneTriggered && !av->m_computeAABB) {
            g_FrameSceneTriggered = true; printf("[CesiumGEPR] Zooming...\n"); fflush(stdout); av->frameScene();
        }

        if (g_FrameCount >= 600 && !av->m_computeAABB) {
            uint64_t fvc = 0;
            for (uint32_t i = 0; i < g_Tileset.renderListCount; ++i) {
                auto* tile = g_Tileset.renderList[i];
                if (tile->rendererResources && tile->rendererResources->cpuVbo) {
                    for (size_t k = 0; k < tile->rendererResources->cpuVboSize; k += 6) {
                        float px = tile->rendererResources->cpuVbo[k], py = tile->rendererResources->cpuVbo[k+1], pz = tile->rendererResources->cpuVbo[k+2];
                        float clip[4] = {0,0,0,0}; for(int r=0; r<4; ++r) clip[r] = mvp[r+0*4]*px + mvp[r+1*4]*py + mvp[r+2*4]*pz + mvp[r+3*4]*1.0f;
                        if (clip[3] > 0) {
                            float nx=clip[0]/clip[3], ny=clip[1]/clip[3], nz=clip[2]/clip[3];
                            if (nx>=-1 && nx<=1 && ny>=-1 && ny<=1 && nz>=0 && nz<=1) fvc++;
                        }
                    }
                }
            }
            static unsigned char s_px[4096*4096*3]; glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,s_px);
            unsigned char br=s_px[0], bg=s_px[1], bb=s_px[2]; uint64_t cov = 0;
            for(int i=0; i<w*h; ++i) if(abs((int)s_px[i*3]-br)>5 || abs((int)s_px[i*3+1]-bg)>5 || abs((int)s_px[i*3+2]-bb)>5) cov++;
            printf("frustum_vertex_count: %llu\n", (unsigned long long)fvc);
            printf("pixel_coverage_percentage: %.2f%%\n", (float)cov/(w*h)*100.0f);
            stbi_flip_vertically_on_write(1); stbi_write_png("production_framebuffer.png", w, h, 3, s_px, w*3);
            fflush(stdout); exit(0);
        }
        if (!av->m_computeAABB) g_FrameCount++;
    }
}

#ifdef CESIUM_GEPR_RUN_TEST
extern "C" void setup() { Alice::setup(); }
extern "C" void update(float dt) { Alice::update(dt); }
extern "C" void draw() { Alice::draw(); }
#endif

#endif
