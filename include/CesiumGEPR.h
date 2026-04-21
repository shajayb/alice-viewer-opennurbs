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
#include <curl/curl.h>

#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
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
        char contentUri[256];
        Tile** children;
        uint32_t childrenCount;
        uint8_t* payload;
        size_t payloadSize;
        bool isLoaded;
        RenderResources* rendererResources;
    };

    struct Tileset {
        Tile* root;
        char baseUrl[512];
        char token[2048];
        Tile** renderList;
        uint32_t renderListCount;
        uint32_t renderListCapacity;

        void init() {
            root = nullptr; baseUrl[0] = '\0'; token[0] = '\0';
            renderListCapacity = 4096;
            renderList = (Tile**)Alice::g_Arena.allocate(renderListCapacity * sizeof(Tile*));
            renderListCount = 0;
        }

        void traverse(Tile* node) {
            if (node->contentUri[0] != '\0' && !node->isLoaded) {
                std::vector<uint8_t> buffer; long sc = 0;
                std::string url = std::string(baseUrl) + node->contentUri;
                if (CesiumNetwork::Fetch(url.c_str(), buffer, &sc, token)) {
                    if (strstr(node->contentUri, ".json")) {
                        auto j = AliceJson::parse(buffer.data(), buffer.size());
                        if (j.type != AliceJson::J_NULL) parseNode(node, j["root"], node->transform);
                    } else {
                        node->payloadSize = buffer.size();
                        node->payload = (uint8_t*)Alice::g_Arena.allocate(node->payloadSize);
                        memcpy(node->payload, buffer.data(), node->payloadSize);
                    }
                    node->isLoaded = true;
                }
            }
            if (node->isLoaded && node->payload) {
                if (renderListCount < renderListCapacity) renderList[renderListCount++] = node;
            }
            for (uint32_t i = 0; i < node->childrenCount; ++i) traverse(node->children[i]);
        }

        void parseNode(Tile* tile, const AliceJson::JsonValue& jNode, const DMat4& parentTransform) {
            DMat4 localTransform = dmat4_identity();
            if (jNode.contains("transform")) {
                auto arr = jNode["transform"];
                for (int i = 0; i < 16; ++i) localTransform.m[i] = (double)arr[i];
            }
            tile->transform = dmat4_mul(parentTransform, localTransform);
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
                    parseNode(tile->children[i], childrenArr[i], tile->transform);
                }
            }
        }
    };

    static Tileset g_Tileset;
    static DVec3 g_OriginEcef;
    static double g_EnuMatrix[16];
    static GLuint g_Program = 0;
    static uint32_t g_FrameCount = 0;
    static long long g_TotalFrustumVertices = 0;
    static V3 g_MinBound = {1e10f, 1e10f, 1e10f};
    static V3 g_MaxBound = {-1e10f, -1e10f, -1e10f};

    static void* cgltf_alloc(void* user, size_t size) { return Alice::g_Arena.allocate(size); }
    static void cgltf_free_cb(void* user, void* ptr) {}

    static void initShaders() {
        const char* vs = "#version 400 core\nlayout(location=0)in vec3 p;uniform mat4 uMVP;uniform mat4 uV;out vec3 vVP;void main(){vVP=(uV*vec4(p,1.0)).xyz;gl_Position=uMVP*vec4(p,1.0);}";
        const char* fs = "#version 400 core\nout vec4 f;in vec3 vVP;void main(){vec3 N=normalize(cross(dFdx(vVP),dFdy(vVP)));if(!gl_FrontFacing)N=-N;float d=max(dot(N,normalize(vec3(0.5,0.8,0.6))),0.2);f=vec4(vec3(0.85)*d,1.0);}";
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
        g_Program=glCreateProgram(); glAttachShader(g_Program,v); glAttachShader(g_Program,f); glLinkProgram(g_Program);
    }

    static void processNode(cgltf_node* node, const DMat4& transform, float* vbo, uint32_t* ebo, uint32_t& vIdx, uint32_t& eIdx) {
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
                uint32_t offset = vIdx;
                for (size_t k=0; k<pa->count; ++k) {
                    float pos[3]; cgltf_accessor_read_float(pa, k, pos, 3);
                    DVec4 wp = dmat4_mul_vec4(m, {(double)pos[0], (double)pos[1], (double)pos[2], 1.0});
                    double dx = wp.x - g_OriginEcef.x, dy = wp.y - g_OriginEcef.y, dz = wp.z - g_OriginEcef.z;
                    float lx = (float)(g_EnuMatrix[0]*dx + g_EnuMatrix[1]*dy + g_EnuMatrix[2]*dz);
                    float ly = (float)(g_EnuMatrix[4]*dx + g_EnuMatrix[5]*dy + g_EnuMatrix[6]*dz);
                    float lz = (float)(g_EnuMatrix[8]*dx + g_EnuMatrix[9]*dy + g_EnuMatrix[10]*dz);
                    vbo[vIdx*6+0] = lx; vbo[vIdx*6+1] = ly; vbo[vIdx*6+2] = lz;
                    g_MinBound.x = (std::min)(g_MinBound.x, lx); g_MinBound.y = (std::min)(g_MinBound.y, ly); g_MinBound.z = (std::min)(g_MinBound.z, lz);
                    g_MaxBound.x = (std::max)(g_MaxBound.x, lx); g_MaxBound.y = (std::max)(g_MaxBound.y, ly); g_MaxBound.z = (std::max)(g_MaxBound.z, lz);
                    vIdx++;
                }
                if (p->indices) for (size_t k=0; k<p->indices->count; ++k) ebo[eIdx++] = (uint32_t)cgltf_accessor_read_index(p->indices, k) + offset;
            }
        }
        for (size_t i=0; i<node->children_count; ++i) processNode(node->children[i], m, vbo, ebo, vIdx, eIdx);
    }

    static void setup() {
        printf("[GEPR] setup started\n"); fflush(stdout);
        if (!Alice::g_Arena.memory) Alice::g_Arena.init(512*1024*1024);
        initShaders(); g_Tileset.init();
        double lat = -22.951916 * 0.0174532925, lon = -43.210487 * 0.0174532925;
        g_OriginEcef = lla_to_ecef(lat, lon, 0.0); denu_matrix(g_EnuMatrix, lat, lon);
        const char* ionToken = std::getenv("CESIUM_ION_TOKEN");
        if (!ionToken) { printf("[GEPR] No CESIUM_ION_TOKEN\n"); return; }
        printf("[GEPR] Fetching handshake...\n"); fflush(stdout);
        std::vector<uint8_t> handshake; long sc=0;
        char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/2275207/endpoint?access_token=%s", ionToken);
        if (CesiumNetwork::Fetch(url, handshake, &sc)) {
            printf("[GEPR] Handshake success, status: %ld, size: %zu\n", sc, handshake.size());
            std::string hsStr(handshake.begin(), handshake.end());
            
            auto find_val = [&](const char* key) -> std::string {
                size_t pos = hsStr.find(key);
                if (pos == std::string::npos) return "";
                pos = hsStr.find(":", pos);
                if (pos == std::string::npos) return "";
                size_t start = hsStr.find("\"", pos);
                if (start == std::string::npos) return "";
                size_t end = hsStr.find("\"", start + 1);
                if (end == std::string::npos) return "";
                return hsStr.substr(start + 1, end - start - 1);
            };

            std::string turl = find_val("\"url\"");
            std::string atok = find_val("\"accessToken\"");
            if (atok.empty()) {
                printf("[GEPR] [INFO] No accessToken in handshake, using original Ion token.\n");
                atok = ionToken;
            }
            
            strncpy(g_Tileset.token, atok.c_str(), 2047);
            g_Tileset.token[2047] = '\0';
            
            if (!turl.empty()) {
                printf("[GEPR] Tileset URL: %s\n", turl.c_str()); fflush(stdout);
                strncpy(g_Tileset.baseUrl, turl.substr(0, turl.find_last_of('/')+1).c_str(), 511);
                g_Tileset.baseUrl[511] = '\0';
                
                std::vector<uint8_t> ts; 
                printf("[GEPR] Fetching tileset JSON...\n"); fflush(stdout);
                if (CesiumNetwork::Fetch(turl.c_str(), ts, &sc, g_Tileset.token)) {
                    printf("[GEPR] Tileset JSON fetch complete, status: %ld, size: %zu\n", sc, ts.size()); fflush(stdout);
                    auto jts = AliceJson::parse(ts.data(), ts.size());
                    if (jts.type != AliceJson::J_NULL) {
                        g_Tileset.root = (Tile*)Alice::g_Arena.allocate(sizeof(Tile)); memset(g_Tileset.root, 0, sizeof(Tile));
                        g_Tileset.parseNode(g_Tileset.root, jts["root"], dmat4_identity());
                        printf("[GEPR] Tileset root parsed.\n"); fflush(stdout);
                    } else {
                        printf("[GEPR] Tileset JSON parse failed.\n"); fflush(stdout);
                    }
                } else {
                    printf("[GEPR] Tileset JSON fetch FAILED, status: %ld\n", sc); fflush(stdout);
                }
            } else {
                printf("[GEPR] [ERROR] Handshake Response missing url\n");
                return;
            }
        } else {
            printf("[GEPR] Handshake fetch FAILED, status: %ld\n", sc); fflush(stdout);
        }
        printf("[GEPR] setup finished\n"); fflush(stdout);
    }

    static void update(float dt) {
        AliceViewer* av = AliceViewer::instance();
        if (g_FrameCount == 0) {
            av->camera.focusPoint = {0,0,0}; av->camera.distance = 500.0f; av->camera.pitch = 0.5f; av->camera.yaw = 0.0f;
        }
        g_Tileset.renderListCount = 0; if (g_Tileset.root) g_Tileset.traverse(g_Tileset.root);
    }

    static void draw() {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClearDepth(0.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); AliceViewer* av = AliceViewer::instance();
        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        M4 v = av->camera.getViewMatrix(), p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w/h, 0.1f);
        float mvp[16]; for(int i=0;i<4;++i)for(int j=0;j<4;++j){mvp[i+j*4]=0;for(int k=0;k<4;++k)mvp[i+j*4]+=p.m[i+k*4]*v.m[k+j*4];}
        glUseProgram(g_Program); glUniformMatrix4fv(glGetUniformLocation(g_Program, "uMVP"), 1, 0, mvp);
        g_TotalFrustumVertices = 0;
        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            Tile* t = g_Tileset.renderList[i];
            if (!t->rendererResources && t->payload) {
                cgltf_options opt = {cgltf_file_type_glb}; opt.memory.alloc_func = cgltf_alloc; opt.memory.free_func = cgltf_free_cb;
                cgltf_data* data = 0; if (cgltf_parse(&opt, t->payload, t->payloadSize, &data) == cgltf_result_success) {
                    cgltf_load_buffers(&opt, data, 0);
                    t->rendererResources = (RenderResources*)Alice::g_Arena.allocate(sizeof(RenderResources));
                    float* vbo = (float*)Alice::g_Arena.allocate(100000*6*4); uint32_t* ebo = (uint32_t*)Alice::g_Arena.allocate(200000*4);
                    uint32_t vIdx=0, eIdx=0; DMat4 yup = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
                    if (data->scene) for (size_t k=0; k<data->scene->nodes_count; ++k) processNode(data->scene->nodes[k], dmat4_mul(t->transform, yup), vbo, ebo, vIdx, eIdx);
                    glGenVertexArrays(1, &t->rendererResources->vao); glBindVertexArray(t->rendererResources->vao);
                    glGenBuffers(1, &t->rendererResources->vbo); glBindBuffer(GL_ARRAY_BUFFER, t->rendererResources->vbo); glBufferData(GL_ARRAY_BUFFER, vIdx*6*4, vbo, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                    glGenBuffers(1, &t->rendererResources->ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->rendererResources->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, eIdx*4, ebo, GL_STATIC_DRAW);
                    t->rendererResources->count = eIdx; cgltf_free(data);
                }
            }
            if (t->rendererResources) { glBindVertexArray(t->rendererResources->vao); glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); g_TotalFrustumVertices += t->rendererResources->count; }
        }
        if (g_FrameCount == 100) { // Zoom to fit after some tiles load
            av->camera.focusPoint = {(g_MinBound.x+g_MaxBound.x)*0.5f, (g_MinBound.y+g_MaxBound.y)*0.5f, (g_MinBound.z+g_MaxBound.z)*0.5f};
            av->camera.distance = (std::max)(g_MaxBound.x-g_MinBound.x, g_MaxBound.y-g_MinBound.y) * 1.5f;
        }
        if (++g_FrameCount == 300) {
            unsigned char* px = (unsigned char*)malloc(w*h*3); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px);
            long long hits = 0; for(int i=0; i<w*h; ++i) if(px[i*3]>0||px[i*3+1]>0||px[i*3+2]>0) hits++;
            printf("frustum_vertex_count: %lld\n", g_TotalFrustumVertices);
            printf("pixel_coverage_percentage: %.2f%%\n", (double)hits/(w*h)*100.0);
            stbi_flip_vertically_on_write(1); stbi_write_png("framebuffer.png", w, h, 3, px, w*3); free(px);
            glfwSetWindowShouldClose(av->window, 1);
        }
    }
}

#ifdef CESIUM_GEPR_RUN_TEST
extern "C" void setup() { CesiumGEPR::setup(); }
extern "C" void update(float dt) { CesiumGEPR::update(dt); }
extern "C" void draw() { CesiumGEPR::draw(); }
#endif

#endif // CESIUM_GEPR_H
