#ifndef TILESET_TEST_H
#define TILESET_TEST_H

#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <thread>
#include <mutex>
#include <queue>
#include <algorithm>

#include "TilesetLoader.h"
#include "AliceViewer.h"
#include "ApiKeyReader.h"
#include "NormalShader.h"
#include "stb_image_write.h"

namespace Alice {

struct TilesetTestApp {
    Buffer<TileNode> nodes;
    Buffer<ActiveNode> activeNodeIndices;
    Buffer<int> pendingIndices;
    Buffer<TilesetLoader::AsyncLoadRequest*> completedRequests;

    int activeRequests = 0;
    bool initialized = 0;
    uint32_t currentFrame = 0;
    Math::DVec3 targetEcef;
    double enuMat[16];
    int rootIdx = -1;
    char cesiumToken[2048];
    
    NormalShader normalShader;
    V3 lightDir = {0.5f, 0.8f, -0.4f};

    std::queue<TilesetLoader::AsyncLoadRequest*> requestQueue;
    std::mutex queueMutex;
    std::thread workerThread;
    bool stopWorker = 0;
    
    LinearArena meshArena;
    uint8_t* requestSubBlocks[256];
    TilesetLoader::AsyncLoadRequest requestPool[256];
    bool requestPoolInUse[256];

    TilesetLoader::AsyncLoadRequest* allocateRequest() {
        for(int i=0; i<256; ++i) {
            if(!requestPoolInUse[i]) {
                requestPoolInUse[i] = 1;
                auto r = &requestPool[i];
                memset(r, 0, sizeof(*r));
                r->verts = (float*)requestSubBlocks[i];
                r->indices = (unsigned int*)(requestSubBlocks[i] + 6*1024*1024);
                r->maxV = (6*1024*1024) / (8*4);
                r->maxI = (2*1024*1024) / 4;
                return r;
            }
        }
        return 0;
    }

    void freeRequest(TilesetLoader::AsyncLoadRequest* r) {
        if(!r) return;
        int i = (int)(r - &requestPool[0]);
        if(i >= 0 && i < 256) requestPoolInUse[i] = 0;
    }

    void workerFuncSyncOnce() {
        TilesetLoader::AsyncLoadRequest* r = 0;
        {
            std::lock_guard<std::mutex> l(queueMutex);
            if(!requestQueue.empty()) {
                r = requestQueue.front();
                requestQueue.pop();
            }
        }
        if(r) {
            TilesetLoader::ProcessAsyncLoad(r);
            {
                std::lock_guard<std::mutex> l(queueMutex);
                completedRequests.push_back(r);
            }
        }
    }

    void workerFunc() {
        while(!stopWorker) {
            workerFuncSyncOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    Math::DVec3 getEyeEcef(const ArcballCamera& cam) {
        float cp=cosf(cam.pitch),sp=sinf(cam.pitch),cy=cosf(cam.yaw),sy=sinf(cam.yaw);
        float gx=cam.focusPoint.x+cam.distance*cp*sy, gy=cam.focusPoint.y+cam.distance*sp, gz=cam.focusPoint.z-cam.distance*cp*cy;
        return {targetEcef.x+gx*enuMat[0]-gz*enuMat[4]+gy*enuMat[8], 
                targetEcef.y+gx*enuMat[1]-gz*enuMat[5]+gy*enuMat[9], 
                targetEcef.z+gx*enuMat[2]-gz*enuMat[6]+gy*enuMat[10]};
    }

    void init() {
        setvbuf(stdout, NULL, _IONBF, 0);
        printf("[TilesetTest] Starting Init...\n");
        AliceViewer* av = AliceViewer::instance();
        av->fov = 0.785f;
        av->m_headlessCapture = 1;

        if(!ApiKeyReader::GetCesiumToken(cesiumToken, 2048)) {
            printf("[FATAL] Could not read CESIUM_ION_TOKEN from API_KEYS.txt\n");
            fflush(stdout);
            exit(1);
        }
        printf("[TilesetTest] Token found (length: %zu)\n", strlen(cesiumToken));

        if(!g_Arena.memory) g_Arena.init(128*1024*1024); else g_Arena.reset();
        meshArena.init(2048*1024*1024ULL); // 2GB
        for(int i=0; i<32; ++i) requestSubBlocks[i] = (uint8_t*)meshArena.allocate(8*1024*1024);

        nodes.init(meshArena, 65536);
        activeNodeIndices.init(meshArena, 32768);
        pendingIndices.init(meshArena, 1024);
        completedRequests.init(meshArena, 1024);

        memset(requestPoolInUse, 0, 256);
        normalShader.init();
        lightDir.normalise();

        // London
        targetEcef = Math::lla_to_ecef(51.5138*Math::DEG2RAD, -0.0983*Math::DEG2RAD, 10.0);
        Math::denu_matrix(enuMat, 51.5138*Math::DEG2RAD, -0.0983*Math::DEG2RAD);

        std::string rootUrl, assetToken;
        if(TilesetLoader::FetchCesiumAssetUrl(cesiumToken, 96188, rootUrl, &assetToken)) {
            printf("[TilesetTest] Asset Discovery Success. URL: %s\n", rootUrl.c_str()); fflush(stdout);
            if(!assetToken.empty()) {
                printf("[TilesetTest] Using scoped asset token.\n"); fflush(stdout);
                strncpy(cesiumToken, assetToken.c_str(), 2048);
            }
            std::vector<uint8_t> tilesetData;
            long st = 0;
            printf("[TilesetTest] Fetching tileset.json...\n"); fflush(stdout);
            if(TilesetLoader::FetchWithRetry(rootUrl, tilesetData, &st, 3, cesiumToken)) {
                printf("[TilesetTest] tileset.json fetched (Size: %zu). Parsing...\n", tilesetData.size()); fflush(stdout);
                json rootJson;
                try {
                    rootJson = json::parse(tilesetData.begin(), tilesetData.end());
                    printf("[TilesetTest] JSON Parsed successfully.\n"); fflush(stdout);
                } catch (const std::exception& e) {
                    printf("[FATAL] JSON Parse Error: %s\n", e.what()); fflush(stdout);
                    exit(1);
                }
                std::string baseUrl = rootUrl.substr(0, rootUrl.find_last_of("/\\") + 1);
                printf("[TilesetTest] Starting ParseRecursive...\n"); fflush(stdout);
                rootIdx = TilesetLoader::ParseRecursive(rootJson["root"], nodes, meshArena, baseUrl);
                printf("[TilesetTest] Root Content URI: %s\n", nodes[rootIdx].contentUri ? nodes[rootIdx].contentUri : "NONE");
                TilesetLoader::CalculateAABB(rootIdx, nodes, targetEcef, enuMat);
                
                auto& root = nodes[rootIdx];
                printf("[TilesetTest] Root Loaded. Index: %d, Radius: %.1f, GL Center: %.1f, %.1f, %.1f\n", 
                    rootIdx, root.sphereRadius, root.glCenter.x, root.glCenter.y, root.glCenter.z);
                fflush(stdout);

                // Frame London
                av->camera.focusPoint = V3(0, 0, 0);
                av->camera.distance = 1500.0f;
                av->camera.pitch = 0.5f;
                av->camera.yaw = 1.0f;
            } else {
                printf("[FATAL] Failed to fetch tileset.json. Status: %ld\n", st); fflush(stdout); exit(1);
            }
        } else {
            printf("[FATAL] Cesium Asset Discovery Failed for 96188\n"); fflush(stdout); exit(1);
        }

        // NO Background thread for the first few attempts to ensure logs are captured
        // workerThread = std::thread(&TilesetTestApp::workerFunc, this);
        
        initialized = 1;
        printf("[TilesetTest] Initialized. Camera Focus: %.1f, %.1f, %.1f, Dist: %.1f\n", 
            av->camera.focusPoint.x, av->camera.focusPoint.y, av->camera.focusPoint.z, av->camera.distance);
        fflush(stdout);
    }

    void updateAsyncLoading() {
        // Sync worker execution
        if(currentFrame < 300) {
            for(int i=0; i<10; ++i) workerFuncSyncOnce();
        }

        TilesetLoader::AsyncLoadRequest* ready[1024];
        int readyCount = 0;
        {
            std::lock_guard<std::mutex> l(queueMutex);
            readyCount = (int)completedRequests.count;
            for(int i=0; i<readyCount; ++i) ready[i] = completedRequests[i];
            completedRequests.clear();
        }

        for(int i=0; i<readyCount; ++i) {
            auto r = ready[i];
            TileNode& n = nodes[r->nodeIdx];
            if(r->success) {
                if(r->isJson) {
                    try {
                        auto j = json::parse(r->jsonBuffer, r->jsonBuffer + r->jsonSize);
                        std::string baseUrl = std::string(r->url).substr(0, std::string(r->url).find_last_of("/\\") + 1);
                        int cIdx = TilesetLoader::ParseRecursive(j["root"], nodes, meshArena, baseUrl, n.worldTransform);
                        if(cIdx >= 0) {
                            n.firstChild = cIdx; n.childCount = 1;
                            TilesetLoader::CalculateAABB(cIdx, nodes, targetEcef, enuMat);
                            n.isLoaded = 1;
                            printf("[ASYNC] Sub-Tileset Loaded: Node %d\n", r->nodeIdx);
                        }
                    } catch(...) { printf("[ASYNC] JSON Parse error in sub-tileset\n"); }
                    free(r->jsonBuffer);
                } else {
                    n.mesh.count = (int)r->icount;
                    glGenVertexArrays(1, &n.mesh.vao);
                    glGenBuffers(1, &n.mesh.vbo);
                    glGenBuffers(1, &n.mesh.ebo);
                    glBindVertexArray(n.mesh.vao);
                    glBindBuffer(GL_ARRAY_BUFFER, n.mesh.vbo);
                    glBufferData(GL_ARRAY_BUFFER, r->vcount * 8 * 4, r->verts, GL_STATIC_DRAW);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, n.mesh.ebo);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, r->icount * 4, r->indices, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 8*4, 0);
                    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, 8*4, (void*)(3*4));
                    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, 0, 8*4, (void*)(6*4));
                    n.aabbMin = r->min; n.aabbMax = r->max;
                    n.glCenter = {(r->min.x+r->max.x)*0.5f, (r->min.y+r->max.y)*0.5f, (r->min.z+r->max.z)*0.5f};
                    n.isLoaded = 1;
                    printf("[ASYNC] Mesh Loaded: Node %d, Verts: %zu, GL Center: %.1f, %.1f, %.1f\n", 
                        r->nodeIdx, r->vcount, n.glCenter.x, n.glCenter.y, n.glCenter.z);
                }
            }
            n.isLoading = 0; activeRequests--; freeRequest(r);
        }
        fflush(stdout);

        // Queue new requests
        for(int i=0; i<(int)activeNodeIndices.count && activeRequests < 20; ++i) {
            int nIdx = activeNodeIndices[i].index;
            if(!nodes[nIdx].isLoaded && !nodes[nIdx].isLoading && nodes[nIdx].hasContent) {
                auto r = allocateRequest();
                if(!r) break;
                r->nodeIdx = nIdx;
                if(nodes[nIdx].contentUri[0] == 'h') strncpy(r->url, nodes[nIdx].contentUri, 1024);
                else { strncpy(r->url, nodes[nIdx].baseUrl, 1024); strncat(r->url, nodes[nIdx].contentUri, 1024-strlen(r->url)); }
                strncpy(r->token, cesiumToken, 2048);
                r->center = targetEcef; memcpy(r->enu, enuMat, 128); memcpy(r->transform, nodes[nIdx].worldTransform, 128);
                nodes[nIdx].isLoading = 1; activeRequests++;
                { std::lock_guard<std::mutex> l(queueMutex); requestQueue.push(r); }
            }
        }
    }

    void draw() {
        if(!initialized || rootIdx == -1) { backGround(0.9f); return; }
        currentFrame++;
        AliceViewer* av = AliceViewer::instance();
        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        
        updateAsyncLoading();

        float asp = (float)w/h;
        M4 vMat = av->camera.getViewMatrix();
        M4 pMat = AliceViewer::makeInfiniteReversedZProjRH(av->fov, asp, 0.1f);
        float fvp[16], pl[6][4]; Math::mat4_mul(fvp, pMat.m, vMat.m);
        TilesetLoader::ExtractFrustumPlanes(fvp, pl);

        activeNodeIndices.clear();
        int nv = 0;
        TilesetLoader::TraverseAndGraft(rootIdx, nodes, getEyeEcef(av->camera), targetEcef, (float)h, av->fov, 1.0f, pl, activeNodeIndices, g_Arena, 0, nv, 4096);

        glClearColor(0.2f, 0.3f, 0.4f, 1.0f); // Dark blue background
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); glClearDepth(0.0f);

        normalShader.use();
        normalShader.setLightDir(lightDir.x, lightDir.y, lightDir.z);
        normalShader.setLightColor(1, 1, 1);
        normalShader.setHasTexture(false);

        int loadedCount = 0;
        for(int i=0; i<(int)activeNodeIndices.count; ++i) {
            int nIdx = activeNodeIndices[i].index;
            if(nodes[nIdx].isLoaded) {
                loadedCount++;
                float mvp[16], nmat[9] = {1,0,0, 0,1,0, 0,0,1};
                Math::mat4_mul(mvp, pMat.m, vMat.m);
                normalShader.setMVP(mvp);
                normalShader.setNormalMatrix(nmat);
                nodes[nIdx].mesh.draw();
            }
        }

        if(currentFrame % 30 == 0) {
            printf("[TilesetTest] Frame %u, Active: %d, Loaded: %d, Camera Focus: %.1f, %.1f, %.1f\n", 
                currentFrame, (int)activeNodeIndices.count, loadedCount, av->camera.focusPoint.x, av->camera.focusPoint.y, av->camera.focusPoint.z);
            fflush(stdout);
        }

        if(av->m_headlessCapture && currentFrame == 400) {
            unsigned char* pb = (unsigned char*)malloc(w*h*3);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pb);
            stbi_flip_vertically_on_write(1);
            stbi_write_png("production_cesium.png", w, h, 3, pb, w*3);
            printf("[HEADLESS] production_cesium.png saved at frame 400. Loaded nodes: %d\n", loadedCount);
            fflush(stdout);
            free(pb);
        }
    }

    ~TilesetTestApp() { stopWorker = 1; if(workerThread.joinable()) workerThread.join(); }
};

}

#ifdef TILESET_TEST_RUN_TEST
static Alice::TilesetTestApp g_T;
extern "C" void setup() { g_T.init(); }
extern "C" void update(float dt) { (void)dt; }
extern "C" void draw() { g_T.draw(); }
#endif

#endif
