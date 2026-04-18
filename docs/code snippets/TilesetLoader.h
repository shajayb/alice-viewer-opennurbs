#ifndef TILESET_LOADER_H
#define TILESET_LOADER_H
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "AliceNetwork.h"
#include "ApiKeyReader.h"
#include "AliceMath.h"
#include "AliceMemory.h"
#include "MeshPrimitive.h"
#include "cgltf.h"
#include "stb_image.h"
using json = nlohmann::json;
namespace Alice {
struct TileNode { Math::DVec3 sphereCenter; double sphereRadius; Math::Vec3 glCenter, aabbMin, aabbMax; float geometricError; double transform[16], worldTransform[16]; const char *contentUri, *baseUrl; int firstChild, childCount; MeshPrimitive mesh; bool isLoaded, isExternal, isLoading, hasContent, refineAdd; };
struct ActiveNode { int index; float sse; };
struct TilesetLoader {
    static bool FetchWithRetry(const std::string& u, std::vector<uint8_t>& b, long* s=0, int r=3, const char* t=0) {
        long st=0; for(int i=0; i<=r; ++i) { b.clear(); if(Network::Fetch(u.c_str(), b, &st, 0, 0, 5, t)) { if(s)*s=st; if(st==200)return true; } else if(s)*s=st; if(i<r)std::this_thread::sleep_for(std::chrono::milliseconds(100)); } return false;
    }
    static bool FetchCesiumAssetUrl(const char* t, int id, std::string& u, std::string* at=0) {
        printf("[CESIUM] Fetching Asset URL for ID: %d...\n", id); fflush(stdout);
        char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/%d/endpoint", id);
        std::vector<uint8_t> b; long s=0; 
        if(FetchWithRetry(url, b, &s, 3, t)) { 
            printf("[CESIUM] Asset fetch success (Size: %zu). Parsing JSON...\n", b.size()); fflush(stdout);
            if(!b.empty()) { std::string body(b.begin(), b.end()); printf("[CESIUM] Raw JSON: %s\n", body.c_str()); fflush(stdout); }
            try { 
                auto j=json::parse(b.begin(), b.end()); 
                if(j.contains("url")) { 
                    u=j["url"].get<std::string>(); 
                    if(at && j.contains("accessToken"))*at=j["accessToken"].get<std::string>(); 
                    return true; 
                } 
            } catch(...) { printf("[CESIUM] JSON Parse Error.\n"); fflush(stdout); } 
        }
        printf("[CESIUM] Asset fetch failed. ID: %d, Status: %ld\n", id, s);
        if(!b.empty()) { std::string body(b.begin(), b.end()); printf("[CESIUM] Response: %s\n", body.c_str()); }
        fflush(stdout);
        return false;
    }
    static void CalculateAABB(int idx, Buffer<TileNode>& nodes, Math::DVec3 target, double* enu) {
        if(idx<0||idx>=(int)nodes.count)return; TileNode& n=nodes[idx];
        double dx=n.sphereCenter.x-target.x, dy=n.sphereCenter.y-target.y, dz=n.sphereCenter.z-target.z;
        float lx=(float)(enu[0]*dx+enu[1]*dy+enu[2]*dz), ly=(float)(enu[4]*dx+enu[5]*dy+enu[6]*dz), lz=(float)(enu[8]*dx+enu[9]*dy+enu[10]*dz);
        float r=(float)n.sphereRadius; n.glCenter={lx,lz,-ly}; n.aabbMin={lx-r,lz-r,-ly-r}; n.aabbMax={lx+r,lz+r,-ly+r};
        if(idx % 1000 == 0 || (idx > 40000 && idx % 100 == 0)) {
            printf("[DEBUG] Node %d GL: %.1f, %.1f, %.1f (E:%.1f, N:%.1f, U:%.1f), Radius: %.1f\n", idx, n.glCenter.x, n.glCenter.y, n.glCenter.z, lx, ly, lz, r); fflush(stdout);
        }
        for(int i=0; i<n.childCount; ++i) { int c=n.firstChild+i; CalculateAABB(c, nodes, target, enu); if(nodes[c].aabbMin.x<n.aabbMin.x)n.aabbMin.x=nodes[c].aabbMin.x; if(nodes[c].aabbMin.y<n.aabbMin.y)n.aabbMin.y=nodes[c].aabbMin.y; if(nodes[c].aabbMin.z<n.aabbMin.z)n.aabbMin.z=nodes[c].aabbMin.z; if(nodes[c].aabbMax.x>n.aabbMax.x)n.aabbMax.x=nodes[c].aabbMax.x; if(nodes[c].aabbMax.y>n.aabbMax.y)n.aabbMax.y=nodes[c].aabbMax.y; if(nodes[c].aabbMax.z>n.aabbMax.z)n.aabbMax.z=nodes[c].aabbMax.z; }
    }
    static bool ExtractFrustumPlanes(const float* m, float p[6][4]) {
        for(int i=0;i<4;++i){
            p[0][i]=m[3+i*4]+m[0+i*4]; // Left
            p[1][i]=m[3+i*4]-m[0+i*4]; // Right
            p[2][i]=m[3+i*4]+m[1+i*4]; // Bottom
            p[3][i]=m[3+i*4]-m[1+i*4]; // Top
            p[4][i]=m[2+i*4];          // Near (Z >= 0)
            p[5][i]=m[3+i*4]-m[2+i*4]; // Far (Z <= W)
        }
        for(int i=0;i<6;++i){float l=sqrtf(p[i][0]*p[i][0]+p[i][1]*p[i][1]+p[i][2]*p[i][2]); p[i][0]/=l;p[i][1]/=l;p[i][2]/=l;p[i][3]/=l;} return true;
    }
    static bool TraverseAndGraft(int idx, Buffer<TileNode>& nodes, Math::DVec3 cam, Math::DVec3 target, float vh, float fov, float thr, const float planes[6][4], Buffer<ActiveNode>& active, LinearArena& arena, int depth, int& visited, int maxNodes) {
        if(idx<0||idx>=(int)nodes.count || visited >= maxNodes) return false;
        TileNode& n = nodes[idx]; visited++;
        double dx=n.sphereCenter.x-cam.x, dy=n.sphereCenter.y-cam.y, dz=n.sphereCenter.z-cam.z;
        float dist = (float)sqrt(dx*dx+dy*dy+dz*dz); if(dist < 1.0f) dist = 1.0f;
        // Distance cull from target (London)
        double tx=n.sphereCenter.x-target.x, ty=n.sphereCenter.y-target.y, tz=n.sphereCenter.z-target.z;
        float dist2t = (float)sqrt(tx*tx+ty*ty+tz*tz);
        bool containsTarget = dist2t <= n.sphereRadius + 10.0f;
        if(idx > 0 && depth > 10 && !containsTarget) return false;
        for(int i=0;i<6;++i) if(planes[i][0]*n.glCenter.x+planes[i][1]*n.glCenter.y+planes[i][2]*n.glCenter.z+planes[i][3] < -n.sphereRadius) return false;

        float sse = (n.geometricError * vh) / (2.0f * dist * (std::max)(0.0001f, tanf(fov * 0.5f)));
        if((sse <= thr && !n.isExternal) || n.childCount == 0) { active.push_back({idx, sse}); return true; }
        bool childrenVisible = false;
        int childIndices[1024]; int cc = (std::min)(n.childCount, 1024);
        for(int i=0; i<cc; ++i) childIndices[i] = n.firstChild + i;
        std::sort(childIndices, childIndices + cc, [&](int a, int b) {
            double txA=nodes[a].sphereCenter.x-target.x, tyA=nodes[a].sphereCenter.y-target.y, tzA=nodes[a].sphereCenter.z-target.z;
            double txB=nodes[b].sphereCenter.x-target.x, tyB=nodes[b].sphereCenter.y-target.y, tzB=nodes[b].sphereCenter.z-target.z;
            return (txA*txA+tyA*tyA+tzA*tzA) < (txB*txB+tyB*tyB+tzB*tzB);
        });
        for(int i=0; i<cc; ++i) if(TraverseAndGraft(childIndices[i], nodes, cam, target, vh, fov, thr, planes, active, arena, depth+1, visited, maxNodes)) childrenVisible = true;
        if(!childrenVisible) active.push_back({idx, sse});
        return true;
    }
    struct AsyncLoadRequest { int nodeIdx; char url[1024], token[2048]; Math::DVec3 center; double enu[16], transform[16]; float *verts; unsigned int *indices; size_t maxV, maxI, vcount, icount; Math::Vec3 min, max; bool isJson; uint8_t *jsonBuffer; size_t jsonSize, texW, texH, texC; uint8_t *texData; char texKey[1024]; bool hasTex; float color[4], emissive[3]; bool success, completed; };
    static void ProcessAsyncLoad(AsyncLoadRequest* req) {
        std::vector<uint8_t> buffer; long status=0;
        if(!FetchWithRetry(req->url, buffer, &status, 3, req->token)) { 
            printf("[ASYNC] Fetch Failed. Status: %ld, URL: %s\n", status, req->url); fflush(stdout);
            req->success=0; req->completed=1; return; 
        }
        printf("[ASYNC] Fetched URL. Size: %zu, Status: %ld, First 4 bytes: %c%c%c%c\n", buffer.size(), status, buffer.size()>0?buffer[0]:'?', buffer.size()>1?buffer[1]:'?', buffer.size()>2?buffer[2]:'?', buffer.size()>3?buffer[3]:'?'); 
        if(buffer.size() > 0) {
            printf("[ASYNC] Header: ");
            for(int i=0; i<(std::min)((int)buffer.size(), 16); ++i) printf("%02X ", buffer[i]);
            printf("\n");
        }
        if(buffer.size() < 1024) { std::string s(buffer.begin(), buffer.end()); printf("[ASYNC] Small Payload: %s\n", s.c_str()); }
        fflush(stdout);
        const uint8_t* data = buffer.data(); size_t size = buffer.size();
        if(size > 5 && (std::string(req->url).find(".json") != std::string::npos || data[0] == '{')) {
            req->isJson = true; req->jsonBuffer = (uint8_t*)malloc(size); memcpy(req->jsonBuffer, data, size); req->jsonSize = size;
            req->success = 1; req->completed = 1; return;
        }
        if(size > 28 && memcmp(data, "b3dm", 4) == 0) {
            uint32_t ftj = *(uint32_t*)(data+12), ftb = *(uint32_t*)(data+16), btj = *(uint32_t*)(data+20), btb = *(uint32_t*)(data+24);
            const char* ftj_ptr = (const char*)data + 28;
            double rtc[3] = {0,0,0};
            if(ftj > 0) {
                std::string ft_str(ftj_ptr, ftj);
                // Simple search for RTC_CENTER in the raw JSON string if it's there
                size_t pos = ft_str.find("RTC_CENTER");
                if(pos != std::string::npos) {
                    try {
                        auto ft = json::parse(ftj_ptr, ftj_ptr + ftj);
                        if(ft.contains("RTC_CENTER")) {
                            auto r = ft["RTC_CENTER"];
                            rtc[0] = r[0].get<double>(); rtc[1] = r[1].get<double>(); rtc[2] = r[2].get<double>();
                            printf("[ASYNC] RTC_CENTER found: %.1f, %.1f, %.1f\n", rtc[0], rtc[1], rtc[2]); fflush(stdout);
                        }
                    } catch(...) {
                        // Fallback: manually parse RTC_CENTER if nlohmann fails on some binary-heavy JSON
                        size_t start = ft_str.find("[", pos);
                        size_t end = ft_str.find("]", start);
                        if(start != std::string::npos && end != std::string::npos) {
                            sscanf(ft_str.substr(start+1).c_str(), "%lf,%lf,%lf", &rtc[0], &rtc[1], &rtc[2]);
                            printf("[ASYNC] RTC_CENTER manual fallback: %.1f, %.1f, %.1f\n", rtc[0], rtc[1], rtc[2]); fflush(stdout);
                        }
                    }
                }
            }
            data += 28 + ftj + ftb + btj + btb; size -= (28 + ftj + ftb + btj + btb);
            req->transform[12] += rtc[0]; req->transform[13] += rtc[1]; req->transform[14] += rtc[2];
        }
 else if (size > 4 && memcmp(data, "glTF", 4) == 0) {
            // Already glTF/GLB, do nothing
        }
        cgltf_options opt = {cgltf_file_type_glb}; cgltf_data* out = 0;
        if(cgltf_parse(&opt, data, size, &out) == cgltf_result_success) {
            // Check for CESIUM_RTC extension in glTF
            for(int i=0; i<(int)out->extensions_used_count; ++i) {
                if(strcmp(out->extensions_used[i], "CESIUM_RTC") == 0) {
                    // This is hard to get from cgltf without manual JSON parsing of the extension
                    // But we can find it in the raw buffer
                    std::string raw((const char*)data, size < 4096 ? size : 4096);
                    size_t pos = raw.find("CESIUM_RTC");
                    if(pos != std::string::npos) {
                        size_t cpos = raw.find("\"center\"", pos);
                        if(cpos != std::string::npos) {
                            size_t bpos = raw.find("[", cpos);
                            double rtc[3];
                            if(sscanf(raw.substr(bpos+1).c_str(), "%lf,%lf,%lf", &rtc[0], &rtc[1], &rtc[2]) == 3) {
                                printf("[ASYNC] CESIUM_RTC found: %.1f, %.1f, %.1f\n", rtc[0], rtc[1], rtc[2]); fflush(stdout);
                                req->transform[12] += rtc[0]; req->transform[13] += rtc[1]; req->transform[14] += rtc[2];
                            }
                        }
                    }
                }
            }
            if(cgltf_load_buffers(&opt, out, req->url) == cgltf_result_success) {
                cgltf_mesh* m = out->meshes_count > 0 ? &out->meshes[0] : 0;
                if(m && m->primitives_count > 0) {
                    cgltf_primitive* p = &m->primitives[0];
                    cgltf_accessor *pos = 0, *nrm = 0, *tex = 0;
                    for(int i=0; i<(int)p->attributes_count; ++i) {
                        if(p->attributes[i].type == cgltf_attribute_type_position) pos = p->attributes[i].data;
                        if(p->attributes[i].type == cgltf_attribute_type_normal) nrm = p->attributes[i].data;
                        if(p->attributes[i].type == cgltf_attribute_type_texcoord) tex = p->attributes[i].data;
                    }
                    if(pos) {
                        req->vcount = (size_t)pos->count; if(req->vcount > req->maxV) req->vcount = req->maxV;
                        req->min = {1e30f,1e30f,1e30f}; req->max = {-1e30f,-1e30f,-1e30f};
                        for(int i=0; i<(int)req->vcount; ++i) {
                            float p[3]={0}, n[3]={0,0,1}, t[2]={0};
                            cgltf_accessor_read_float(pos, i, p, 3);
                            if(nrm) cgltf_accessor_read_float(nrm, i, n, 3);
                            if(tex) cgltf_accessor_read_float(tex, i, t, 2);
                            double wx = req->transform[0]*p[0] + req->transform[4]*p[1] + req->transform[8]*p[2] + req->transform[12];
                            double wy = req->transform[1]*p[0] + req->transform[5]*p[1] + req->transform[9]*p[2] + req->transform[13];
                            double wz = req->transform[2]*p[0] + req->transform[6]*p[1] + req->transform[10]*p[2] + req->transform[14];
                            if(i == 0 && (req->nodeIdx % 100 == 0 || req->nodeIdx == 940)) { 
                                printf("[DEBUG] Node %d Transform: [%.1f %.1f %.1f %.1f] [%.1f %.1f %.1f %.1f] [%.1f %.1f %.1f %.1f] [%.1f %.1f %.1f %.1f]\n", 
                                    req->nodeIdx, req->transform[0], req->transform[4], req->transform[8], req->transform[12],
                                    req->transform[1], req->transform[5], req->transform[9], req->transform[13],
                                    req->transform[2], req->transform[6], req->transform[10], req->transform[14],
                                    req->transform[3], req->transform[7], req->transform[11], req->transform[15]);
                                printf("[DEBUG] Node %d, V0 Raw: %.1f, %.1f, %.1f, World: %.1f, %.1f, %.1f\n", req->nodeIdx, p[0], p[1], p[2], wx, wy, wz); 
                                fflush(stdout); 
                            }
                            double dx = wx - req->center.x, dy = wy - req->center.y, dz = wz - req->center.z;
                            // Emergency Fix: If coordinates are near earth center, assume they are relative to target center if we are in London
                            if(wx*wx+wy*wy+wz*wz < 1000000.0*1000000.0) {
                                dx = wx; dy = wy; dz = wz; 
                            }
                            float lx = (float)(req->enu[0]*dx + req->enu[1]*dy + req->enu[2]*dz);
                            float ly = (float)(req->enu[4]*dx + req->enu[5]*dy + req->enu[6]*dz);
                            float lz = (float)(req->enu[8]*dx + req->enu[9]*dy + req->enu[10]*dz);
                            float gx = lx, gy = lz, gz = -ly;
                            req->verts[i*8+0]=gx; req->verts[i*8+1]=gy; req->verts[i*8+2]=gz;
                            if(gx<req->min.x)req->min.x=gx; if(gy<req->min.y)req->min.y=gy; if(gz<req->min.z)req->min.z=gz;
                            if(gx>req->max.x)req->max.x=gx; if(gy>req->max.y)req->max.y=gy; if(gz>req->max.z)req->max.z=gz;
                            double nwx = req->transform[0]*n[0] + req->transform[4]*n[1] + req->transform[8]*n[2];
                            double nwy = req->transform[1]*n[0] + req->transform[5]*n[1] + req->transform[9]*n[2];
                            double nwz = req->transform[2]*n[0] + req->transform[6]*n[1] + req->transform[10]*n[2];
                            float nlx = (float)(req->enu[0]*nwx + req->enu[1]*nwy + req->enu[2]*nwz);
                            float nly = (float)(req->enu[4]*nwx + req->enu[5]*nwy + req->enu[6]*nwz);
                            float nlz = (float)(req->enu[8]*nwx + req->enu[9]*nwy + req->enu[10]*nwz);
                            float ngx = nlx, ngy = nlz, ngz = -nly;
                            float ilen = 1.0f/sqrtf(ngx*ngx+ngy*ngy+ngz*ngz+1e-12f);
                            req->verts[i*8+3]=ngx*ilen; req->verts[i*8+4]=ngy*ilen; req->verts[i*8+5]=ngz*ilen;
                            req->verts[i*8+6]=t[0]; req->verts[i*8+7]=t[1];
                        }
                        if(p->indices) {
                            req->icount = (size_t)p->indices->count; if(req->icount > req->maxI) req->icount = req->maxI;
                            for(int i=0; i<(int)req->icount; ++i) req->indices[i] = (unsigned int)cgltf_accessor_read_index(p->indices, i);
                        }
                        req->success = 1;
                    }
                }
            }
            cgltf_free(out);
        } else {
            printf("[ASYNC] cgltf_parse failed.\n"); fflush(stdout);
        }
        req->completed = 1;
    }
    static int ParseRecursive(const json& t, Buffer<TileNode>& nodes, LinearArena& arena, const std::string& baseUrl, const double* pt=0) {
        try {
            if(nodes.count>=nodes.capacity) { printf("[ERROR] Node buffer capacity reached!\n"); return -1; }
            int idx=(int)nodes.count++; 
            if(idx % 100 == 0) { printf("[PARSE] Node %d...\n", idx); fflush(stdout); }
            TileNode& n=nodes[idx]; memset(&n,0,sizeof(n)); n.firstChild=-1;
            double l[16]; Math::mat4d_identity(l); if(t.contains("transform"))for(int i=0;i<16;++i)l[i]=t["transform"][i].get<double>();
            if(pt)Math::mat4d_mul(n.worldTransform,pt,l); else memcpy(n.worldTransform,l,16*sizeof(double));
            if(t.contains("boundingVolume")){
                auto& bv = t["boundingVolume"];
                bool isGlobal = false;
                if(bv.contains("sphere")){auto s=bv["sphere"]; n.sphereCenter={s[0].get<double>(),s[1].get<double>(),s[2].get<double>()}; n.sphereRadius=s[3].get<double>();}
                else if(bv.contains("box")){
                    auto b=bv["box"]; n.sphereCenter={b[0].get<double>(),b[1].get<double>(),b[2].get<double>()};
                    double x=b[3].get<double>(),y=b[4].get<double>(),z=b[5].get<double>();
                    double ux=b[6].get<double>(),uy=b[7].get<double>(),uz=b[8].get<double>();
                    double vx=b[9].get<double>(),vy=b[10].get<double>(),vz=b[11].get<double>();
                    n.sphereRadius=sqrt(x*x+y*y+z*z + ux*ux+uy*uy+uz*uz + vx*vx+vy*vy+vz*vz);
                }
                else if(bv.contains("region")){
                    auto r=bv["region"]; double w=r[0].get<double>(), s=r[1].get<double>(), e=r[2].get<double>(), m_n=r[3].get<double>(), minH=r[4].get<double>(), maxH=r[5].get<double>();
                    double midLat = (s + m_n) * 0.5, midLon = (w + e) * 0.5;
                    if(e < w) midLon += 3.14159265358979323846; 
                    n.sphereCenter = Math::lla_to_ecef(midLat, midLon, (minH + maxH) * 0.5);
                    Math::DVec3 corner = Math::lla_to_ecef(s, w, minH);
                    double dx=corner.x-n.sphereCenter.x, dy=corner.y-n.sphereCenter.y, dz=corner.z-n.sphereCenter.z;
                    n.sphereRadius = sqrt(dx*dx+dy*dy+dz*dz);
                    isGlobal = true;
                }
                if(!isGlobal) Math::mat4d_transform_point(n.sphereCenter,n.worldTransform);
            }
            if(idx % 1000 == 0 || (t.contains("content") && idx < 5000)) {
                printf("[DEBUG] Node %d ECEF: %.1f, %.1f, %.1f\n", idx, n.sphereCenter.x, n.sphereCenter.y, n.sphereCenter.z); fflush(stdout);
            }
            n.geometricError=t.value("geometricError",0.0f); n.hasContent=false;
            if(t.contains("content")) {
                std::string uri = t["content"].value("uri", "");
                if(uri.empty()) uri = t["content"].value("url", "");
                if(!uri.empty()) {
                    char* u = (char*)arena.allocate(uri.length()+1); strcpy(u, uri.c_str()); n.contentUri = u;
                    char* b = (char*)arena.allocate(baseUrl.length()+1); strcpy(b, baseUrl.c_str()); n.baseUrl = b;
                    n.hasContent = true;
                }
            }
            if(t.contains("children")){ n.childCount=(int)t["children"].size(); n.firstChild=(int)nodes.count; for(const auto& c:t["children"])ParseRecursive(c,nodes,arena,baseUrl,n.worldTransform); }
            return idx;
        } catch (const std::exception& e) {
            printf("[ERROR] ParseRecursive Node Exception: %s\n", e.what()); fflush(stdout);
            return -1;
        }
    }
};
}
#endif
