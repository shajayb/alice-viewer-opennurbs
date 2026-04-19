#ifndef CESIUM_ST_PAULS_TEST_H
#define CESIUM_ST_PAULS_TEST_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "AliceViewer.h"
#include "AliceMath.h"
#include "AliceNetwork.h"
#include "NormalShader.h"

#include "stb_image_write.h"

namespace Alice {

struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    int count = 0;
    void draw() { if(vao) { glBindVertexArray(vao); glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0); } }
};

// --- DOD PRIMITIVE STORAGE ---
static const int MAX_NODES = 65536;
static double g_NodeTransforms[MAX_NODES][16];
static double g_NodeCenters[MAX_NODES][3];
static double g_NodeRadii[MAX_NODES];
static char g_NodeUris[MAX_NODES][128];
static bool g_NodeLoaded[MAX_NODES];
static bool g_NodeLoading[MAX_NODES];
static bool g_NodeHasContent[MAX_NODES];
static Mesh g_NodeMeshes[MAX_NODES];
static size_t g_Count = 0;

static Math::DVec3 g_TgtEcef;
static char g_RootUrl[1024];
static char g_AssetTok[2048];
static uint32_t g_FrameCount = 0;
static bool g_Started = false;
static bool g_ResInited = false;
static bool g_Framed = false;
static double g_CurrentMinD = 1e20;
static NormalShader g_NormalShader;
static Math::Vec3 g_Light = {0.5f, 0.8f, -0.4f};

static void mat4d_identity_col(double* m) {
    memset(m, 0, 16 * sizeof(double)); m[0] = m[5] = m[10] = m[15] = 1.0;
}
static void mat4d_mul_col(double* out, const double* a, const double* b) {
    double res[16];
    for(int c=0; c<4; ++c) {
        for(int r=0; r<4; ++r) {
            res[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1] + a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
        }
    }
    memcpy(out, res, 16 * sizeof(double));
}
static void mat4d_transform_point_col(Math::DVec3& p, const double* m) {
    double x = p.x, y = p.y, z = p.z;
    p.x = m[0]*x + m[4]*y + m[8]*z + m[12];
    p.y = m[1]*x + m[5]*y + m[9]*z + m[13];
    p.z = m[2]*x + m[6]*y + m[10]*z + m[14];
}

struct JSONParser {
    static const char* find_key(const char* json, const char* key, const char* end) {
        if(!json) return nullptr;
        size_t klen = strlen(key); const char* p = json;
        while(p + klen + 2 < end) {
            if(p[0] == '"' && strncmp(p+1, key, klen) == 0 && p[klen+1] == '"') return p + klen + 2;
            p++;
        }
        return nullptr;
    }
    static bool get_string(const char* json, const char* key, const char* end, char* out, size_t maxLen) {
        const char* p = find_key(json, key, end); if(!p) return false;
        while(p < end && *p != ':') p++;
        if(p >= end - 1) return false;
        p++; while(p < end && (*p == ' ' || *p == '"')) p++;
        if(p >= end) return false;
        const char* s = p; while(p < end && *p != '"') p++;
        size_t len = p - s; if(len >= maxLen) len = maxLen - 1;
        strncpy(out, s, len); out[len] = '\0'; return true;
    }
};

struct Work { const char* start; const char* end; int selfIdx; double pTransform[16]; };

static void parseNodeFlat(const char* startJSON, const char* endJSON, int rIdx, const double* pTransform) {
    std::vector<Work> stack; stack.reserve(1024);
    Work w; w.start = startJSON; w.end = endJSON; w.selfIdx = rIdx; memcpy(w.pTransform, pTransform, 16*sizeof(double));
    stack.push_back(w);
    while(!stack.empty()) {
        Work curr = stack.back(); stack.pop_back(); int sIdx = curr.selfIdx;
        double lTransform[16]; mat4d_identity_col(lTransform);
        const char* tr = JSONParser::find_key(curr.start, "transform", curr.end);
        if(tr) {
            const char* sb = strchr(tr, '[');
            if(sb && sb < curr.end) {
                const char* p = sb + 1;
                for(int i=0; i<16; ++i) { lTransform[i] = atof(p); p = strchr(p, ','); if(!p || p >= curr.end) break; p++; }
            }
        }
        mat4d_mul_col(g_NodeTransforms[sIdx], curr.pTransform, lTransform);
        const char* bv = JSONParser::find_key(curr.start, "boundingVolume", curr.end);
        if(bv) {
            const char* reg = JSONParser::find_key(bv, "region", curr.end);
            const char* box = JSONParser::find_key(bv, "box", curr.end);
            if(reg) {
                const char* rb = strchr(reg, '[');
                if(rb && rb < curr.end) {
                    double rw, rs, re, rn;
                    if(sscanf(rb+1, "%lf , %lf , %lf , %lf", &rw, &rs, &re, &rn) >= 4) {
                        Math::DVec3 c = Math::lla_to_ecef((rs+rn)*0.5, (rw+re)*0.5, 0.0);
                        g_NodeCenters[sIdx][0] = c.x; g_NodeCenters[sIdx][1] = c.y; g_NodeCenters[sIdx][2] = c.z;
                        double latD = (rn-rs)*6378137.0; double lonD = (re-rw)*6378137.0*cos((rs+rn)*0.5);
                        g_NodeRadii[sIdx] = sqrt(latD*latD + lonD*lonD)*0.5;
                    }
                }
            } else if(box) {
                const char* bb = strchr(box, '[');
                if(bb && bb < curr.end) {
                    double bx, by, bz;
                    if(sscanf(bb+1, "%lf , %lf , %lf", &bx, &by, &bz) >= 3) {
                        Math::DVec3 c = {bx, by, bz}; mat4d_transform_point_col(c, g_NodeTransforms[sIdx]);
                        g_NodeCenters[sIdx][0] = c.x; g_NodeCenters[sIdx][1] = c.y; g_NodeCenters[sIdx][2] = c.z;
                        g_NodeRadii[sIdx] = 2000.0;
                    }
                }
            }
        }
        if(JSONParser::get_string(JSONParser::find_key(curr.start, "content", curr.end), "uri", curr.end, g_NodeUris[sIdx], 128)) g_NodeHasContent[sIdx] = true;
        const char* ch = JSONParser::find_key(curr.start, "children", curr.end);
        if(ch) {
            const char* cb = strchr(ch, '[');
            if(cb && cb < curr.end) {
                const char* p = cb + 1;
                while(p < curr.end && *p != ']' && g_Count < MAX_NODES - 1) {
                    while(p < curr.end && *p != '{' && *p != ']') p++; if(*p == ']') break;
                    const char* cs = p; int depthCount = 0; while(p < curr.end) { if(*p == '{') depthCount++; else if(*p == '}') { depthCount--; if(depthCount == 0) break; } p++; }
                    const char* ce = (p < curr.end) ? (p + 1) : curr.end;
                    double cc[3] = {0,0,0}; double cr = 1e10;
                    const char* cbv = JSONParser::find_key(cs, "boundingVolume", ce);
                    if(cbv) {
                        const char* creg = JSONParser::find_key(cbv, "region", ce);
                        if(creg) {
                            const char* crb = strchr(creg, '[');
                            if(crb) {
                                double crw, crs, cre, crn;
                                if(sscanf(crb+1, "%lf , %lf , %lf , %lf", &crw, &crs, &cre, &crn) >= 4) {
                                    Math::DVec3 c = Math::lla_to_ecef((crs+crn)*0.5, (crw+cre)*0.5, 0.0);
                                    cc[0]=c.x; cc[1]=c.y; cc[2]=c.z;
                                    double latD = (crn-crs)*6378137.0; double lonD = (cre-crw)*6378137.0*cos((crs+crn)*0.5);
                                    cr = sqrt(latD*latD + lonD*lonD)*0.5;
                                }
                            }
                        }
                    }
                    double cd = sqrt(pow(cc[0]-g_TgtEcef.x,2)+pow(cc[1]-g_TgtEcef.y,2)+pow(cc[2]-g_TgtEcef.z,2));
                    if(cd < cr + 50000.0 || cr > 1000000.0) {
                        int cIdx = (int)g_Count++;
                        Work childWork; childWork.start = cs; childWork.end = ce; childWork.selfIdx = cIdx;
                        memcpy(childWork.pTransform, g_NodeTransforms[sIdx], 16*sizeof(double)); stack.push_back(childWork);
                    }
                    p++; while(p < curr.end && *p != ',' && *p != ']') p++; if(*p == ',') p++;
                }
            }
        }
    }
}

static void uploadMesh(int idx, cgltf_data* data, Math::DVec3 rtc) {
    AliceViewer* av = AliceViewer::instance();
    for(size_t ni=0; ni<data->nodes_count; ++ni) {
        cgltf_node* node = &data->nodes[ni];
        if(!node->mesh) continue;
        double nm[16]; mat4d_identity_col(nm);
        if(node->has_matrix) { for(int i=0; i<16; ++i) nm[i] = (double)node->matrix[i]; }
        else {
            // Simple TRS if needed, but B3DM usually uses matrix
            if(node->has_translation) { nm[12] = node->translation[0]; nm[13] = node->translation[1]; nm[14] = node->translation[2]; }
        }
        double finalM[16]; mat4d_mul_col(finalM, g_NodeTransforms[idx], nm);
        
        // --- AXIS SWAP: glTF Y-up to 3D Tiles Z-up ---
        double ecefM[16];
        double y2z[16] = { 1,0,0,0,  0,0,1,0,  0,-1,0,0,  0,0,0,1 };
        mat4d_mul_col(ecefM, y2z, finalM);
        
        for(size_t j=0; j<node->mesh->primitives_count; ++j) {
            cgltf_primitive* prim = &node->mesh->primitives[j];
            cgltf_accessor* posAcc = nullptr;
            for(size_t k=0; k<prim->attributes_count; ++k) { if(prim->attributes[k].type == cgltf_attribute_type_position) { posAcc = prim->attributes[k].data; break; } }
            if(!posAcc) continue;
            size_t vCount = posAcc->count; float* v = (float*)malloc(vCount * 8 * sizeof(float));
            if (idx % 500 == 0) {
                printf("[Audit] Node %d FinalM Trans: %.2f %.2f %.2f\n", idx, ecefM[12], ecefM[13], ecefM[14]);
                fflush(stdout);
            }
            for(size_t n=0; n<vCount; ++n) {
                float lp[3]; cgltf_accessor_read_float(posAcc, n, lp, 3);
                Math::DVec3 p = { (double)lp[0], (double)lp[1], (double)lp[2] };
                double px=p.x, py=p.y, pz=p.z;
                p.x = ecefM[0]*px + ecefM[4]*py + ecefM[8]*pz + ecefM[12]; 
                p.y = ecefM[1]*px + ecefM[5]*py + ecefM[9]*pz + ecefM[13]; 
                p.z = ecefM[2]*px + ecefM[6]*py + ecefM[10]*pz + ecefM[14];
                
                double dx=p.x-g_TgtEcef.x, dy=p.y-g_TgtEcef.y, dz=p.z-g_TgtEcef.z;
                double lat=51.5138*Math::DEG2RAD, lon=-0.0984*Math::DEG2RAD;
                double sL=sin(lon), cL=cos(lon), sP=sin(lat), cP=cos(lat);
                double ex = -sL*dx + cL*dy, ey = -sP*cL*dx - sP*sL*dy + cP*dz, ez = cP*cL*dx + cP*sL*dy + sP*dz;
                v[n*8+0] = (float)ex; v[n*8+1] = (float)ey; v[n*8+2] = (float)ez;
                v[n*8+3] = 0.0f; v[n*8+4] = 1.0f; v[n*8+5] = 0.0f;
                v[n*8+6] = 0.0f; v[n*8+7] = 0.0f;
                av->m_sceneAABB.expand({(float)ex, (float)ey, (float)ez});
            }
            size_t iCount = prim->indices->count; uint32_t* idxs = (uint32_t*)malloc(iCount * 4);
            for(size_t n=0; n<iCount; ++n) idxs[n] = (uint32_t)cgltf_accessor_read_index(prim->indices, n);
            glGenVertexArrays(1, &g_NodeMeshes[idx].vao); glGenBuffers(1, &g_NodeMeshes[idx].vbo); glGenBuffers(1, &g_NodeMeshes[idx].ebo);
            glBindVertexArray(g_NodeMeshes[idx].vao); glBindBuffer(GL_ARRAY_BUFFER, g_NodeMeshes[idx].vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vCount*8*4), v, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_NodeMeshes[idx].ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(iCount*4), idxs, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*4, (void*)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*4, (void*)(3*4));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*4, (void*)(6*4));
            g_NodeMeshes[idx].count = (int)iCount; free(v); free(idxs);
        }
    }
}

static void loadTile(int idx, double d) {
    g_NodeLoading[idx] = true; 
    char url[2048]; char oldRoot[1024]; strcpy(oldRoot, g_RootUrl);
    const char* ls = strrchr(g_RootUrl, '/'); size_t bl = ls ? (ls - g_RootUrl + 1) : 0;
    strncpy(url, g_RootUrl, bl); url[bl] = '\0'; strncat(url, g_NodeUris[idx], 2048 - strlen(url));
    std::vector<uint8_t> pl;
    if(Network::Fetch(url, pl, 0, 0, 0, 10, g_AssetTok)) {
        if(pl.size() >= 4 && strncmp((const char*)pl.data(), "b3dm", 4) == 0) {
            uint32_t ftjl = *(uint32_t*)(pl.data() + 12); uint32_t ftbl = *(uint32_t*)(pl.data() + 16);
            uint32_t btjl = *(uint32_t*)(pl.data() + 20); uint32_t btbl = *(uint32_t*)(pl.data() + 24);
            if (idx % 500 == 0) { printf("[Audit] B3DM Header: ftjl:%u ftbl:%u btjl:%u btbl:%u\n", ftjl, ftbl, btjl, btbl); fflush(stdout); }
            Math::DVec3 rtc = {0,0,0};
            if(ftjl > 0 && pl.size() >= 28 + ftjl) {
                const char* f = (const char*)(pl.data() + 28);
                const char* k = JSONParser::find_key(f, "RTC_CENTER", f + ftjl);
                if(k) {
                    const char* b = strchr(k, '[');
                    if(b && b < f + ftjl) {
                        const char* p = b + 1;
                        rtc.x = atof(p); p = strchr(p, ','); if(p) { rtc.y = atof(p+1); p = strchr(p+1, ','); if(p) rtc.z = atof(p+1); }
                    }
                    else {
                        const char* bo = strstr(k, "\"byteOffset\"");
                        if(bo && bo < f + ftjl) {
                            bo = strchr(bo, ':');
                            if(bo) {
                                int offset = atoi(bo + 1);
                                if(ftbl >= (uint32_t)offset + 24) {
                                    const double* drtc = (const double*)(pl.data() + 28 + ftjl + offset);
                                    rtc.x = drtc[0]; rtc.y = drtc[1]; rtc.z = drtc[2];
                                }
                            }
                        }
                    }
                }
            }
            uint8_t* gd = (uint8_t*)pl.data() + 28 + ftjl + ftbl + btjl + btbl;
            if(gd < pl.data() + pl.size()) {
                cgltf_options o = {cgltf_file_type_glb}; cgltf_data* data = nullptr;
                if(cgltf_parse(&o, gd, pl.size() - (gd - pl.data()), &data) == cgltf_result_success) {
                    cgltf_load_buffers(&o, data, nullptr);
                    for(size_t i=0; i<data->data_extensions_count; ++i) {
                        if(strcmp(data->data_extensions[i].name, "CESIUM_RTC") == 0) {
                            const char* ext = data->data_extensions[i].data;
                            if(ext) {
                                const char* ctr = strstr(ext, "\"center\"");
                                if(ctr) {
                                    const char* cb = strchr(ctr, '[');
                                    if(cb) {
                                        const char* p = cb + 1;
                                        rtc.x = atof(p); p = strchr(p, ','); if(p) { rtc.y = atof(p+1); p = strchr(p+1, ','); if(p) rtc.z = atof(p+1); }
                                    }
                                }
                            }
                        }
                    }
                    if (rtc.x != 0.0 || rtc.y != 0.0 || rtc.z != 0.0) {
                        double* m = g_NodeTransforms[idx];
                        double nx = m[0]*rtc.x + m[4]*rtc.y + m[8]*rtc.z + m[12];
                        double ny = m[1]*rtc.x + m[5]*rtc.y + m[9]*rtc.z + m[13];
                        double nz = m[2]*rtc.x + m[6]*rtc.y + m[10]*rtc.z + m[14];
                        m[12] = nx; m[13] = ny; m[14] = nz;
                    }
                    uploadMesh(idx, data, rtc); cgltf_free(data);
                }
            }
        } else if(pl.size() > 0 && pl[0] == '{') {
            strncpy(g_RootUrl, url, 1024); const char* json = (const char*)pl.data();
            const char* root = JSONParser::find_key(json, "root", json + pl.size());
            if(root && g_Count < MAX_NODES - 1) {
                int nrIdx = (int)g_Count++;
                parseNodeFlat(root, json + pl.size(), nrIdx, g_NodeTransforms[idx]);
            }
            strcpy(g_RootUrl, oldRoot);
        }
        g_NodeLoaded[idx] = true;
    }
    g_NodeLoading[idx] = false;
}

static void update() {
    if(!g_Started) return;
    double minD = 1e20;
    for(size_t i=0; i<g_Count; ++i) {
        Math::DVec3 wc = { g_NodeCenters[i][0], g_NodeCenters[i][1], g_NodeCenters[i][2] };
        double d = sqrt(pow(wc.x - g_TgtEcef.x, 2) + pow(wc.y - g_TgtEcef.y, 2) + pow(wc.z - g_TgtEcef.z, 2));
        if(d < minD) minD = d;
        if(g_NodeLoaded[i] || g_NodeLoading[i]) continue;
        if(i > 0 && g_NodeRadii[i] > 0 && d > g_NodeRadii[i] + 5000.0) { g_NodeLoaded[i] = true; continue; }
        if(g_NodeHasContent[i]) {
            bool isExt = strstr(g_NodeUris[i], ".json") != nullptr;
            if(d < 5000.0 || isExt || (i == 0)) loadTile((int)i, d);
            else { g_NodeLoaded[i] = true; }
        }
    }
    g_CurrentMinD = minD;
    if(g_FrameCount % 50 == 0) { printf("[Audit] frame:%u minD: %.1f count:%zu\n", g_FrameCount, minD, g_Count); fflush(stdout); }
}

static void performStart() {
    printf("[Audit] performStart Entry\n"); fflush(stdout);
    double lat = 51.5138 * Math::DEG2RAD, lon = -0.0984 * Math::DEG2RAD;
    g_TgtEcef = Math::lla_to_ecef(lat, lon, 10.0);
    const char* ionToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiIyZmI0ZWVlYi02Y2FjLTRkYjYtYmIyMy1mMGNmODliZGEwNTEiLCJpZCI6NDE5MDUyLCJpYXQiOjE3NzYzMzQ0MTN9.NuD0nngJ4DbKFrQsjId4Nea_0wQ-LH0mfOIZqDh7aj0";
    char endpointUrl[1024]; snprintf(endpointUrl, 1024, "https://api.cesium.com/v1/assets/96188/endpoint?access_token=%s", ionToken);
    std::vector<uint8_t> response;
    if(Network::Fetch(endpointUrl, response)) {
        const char* js = (const char*)response.data(); const char* je = js + response.size();
        JSONParser::get_string(js, "url", je, g_RootUrl, 1024);
        JSONParser::get_string(js, "accessToken", je, g_AssetTok, 2048);
        std::vector<uint8_t> td;
        if(Network::Fetch(g_RootUrl, td, 0, 0, 0, 10, g_AssetTok)) {
            int rootIdx = (int)g_Count++; mat4d_identity_col(g_NodeTransforms[rootIdx]);
            double ident[16]; mat4d_identity_col(ident);
            const char* end = (const char*)td.data() + td.size();
            const char* r = JSONParser::find_key((const char*)td.data(), "root", end);
            if(r) parseNodeFlat(r, end, rootIdx, ident);
            if(g_NodeRadii[rootIdx] < 1.0) g_NodeRadii[rootIdx] = 10000000.0;
            printf("[Audit] Root parsed. Count:%zu\n", g_Count); fflush(stdout);
        }
    }
    g_Started = true;
}

static void draw() {
    if(!g_Started) performStart();
    update();
    AliceViewer* av = AliceViewer::instance();
    if(!g_ResInited) { g_NormalShader.init(); av->camera.focusPoint = {0,0,0}; av->camera.distance = 500.0f; g_ResInited = true; }
    
    if(g_CurrentMinD < 1000.0 && !g_Framed && av->m_sceneAABB.initialized) { 
        printf("[Audit] AABB: %.1f,%.1f,%.1f to %.1f,%.1f,%.1f\n", av->m_sceneAABB.m_min.x, av->m_sceneAABB.m_min.y, av->m_sceneAABB.m_min.z, av->m_sceneAABB.m_max.x, av->m_sceneAABB.m_max.y, av->m_sceneAABB.m_max.z);
        fflush(stdout);
        av->frameScene(); g_Framed = true; 
    }
    
    glClearColor(0.1f, 0.15f, 0.2f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST);
    int w, h; glfwGetFramebufferSize(av->window, &w, &h); if(h < 1) h = 1;
    M4 view = av->camera.getViewMatrix(); M4 proj; Math::mat4_perspective(proj.m, av->fov, (float)w/h, 0.1f, 10000.0f);
    float mvp[16]; Math::mat4_mul(mvp, proj.m, view.m);
    float ident3[9] = {1,0,0, 0,1,0, 0,0,1};
    g_NormalShader.use(); g_NormalShader.setMVP(mvp);
    g_NormalShader.setNormalMatrix(ident3);
    g_NormalShader.setLightDir(g_Light.x, g_Light.y, g_Light.z); g_NormalShader.setLightColor(1, 1, 1);
    glDisableVertexAttribArray(3); glVertexAttrib3f(3, 0, 0, 0); 
    GLuint q; glGenQueries(1, &q); glBeginQuery(GL_SAMPLES_PASSED, q);
    for(size_t i=0; i<g_Count; ++i) { if(g_NodeLoaded[i]) g_NodeMeshes[i].draw(); }
    glEndQuery(GL_SAMPLES_PASSED); GLuint sp = 0; glGetQueryObjectuiv(q, GL_QUERY_RESULT, &sp); glDeleteQueries(1, &q);
    if(g_FrameCount % 50 == 0) printf("[CesiumStPauls] Samples: %u, Nodes: %zu\n", sp, g_Count);
    if(g_FrameCount == 200) {
        unsigned char* px = (unsigned char*)malloc(w * h * 3); glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
        stbi_flip_vertically_on_write(true); stbi_write_png("framebuffer.png", w, h, 3, px, w * 3);
        if(sp > 0) printf("[CesiumStPauls] SUCCESS: Final Frame 200 with %u samples.\n", sp);
        else printf("[CesiumStPauls] FAILURE: Final Frame 200 with 0 samples.\n");
        free(px); glfwSetWindowShouldClose(av->window, true);
    }
    g_FrameCount++;
}

}

#ifdef CESIUM_ST_PAULS_TEST_RUN_TEST
extern "C" void setup() { printf("[Audit] setup Entry\n"); fflush(stdout); }
extern "C" void update(float dt) { (void)dt; }
extern "C" void draw() { Alice::draw(); }
#endif

#endif
