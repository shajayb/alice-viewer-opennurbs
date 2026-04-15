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
#include "TilesetLoader.h"
#include "AliceViewer.h"
#include "ApiKeyReader.h"
#include "NormalShader.h"
#include "SSAO.h"
#include "MockCathedral.h"

namespace Alice
{
    struct ShadowMap
    {
        unsigned int fbo, depthTex;
        unsigned int program;
        int width = 2048, height = 2048;
        float lightProj[16], lightView[16], lightSpaceMat[16];

        void init()
        {
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &depthTex);
            glBindTexture(GL_TEXTURE_2D, depthTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            const char* vs = "#version 430 core\nlayout(location=0) in vec3 aPos;\nuniform mat4 uLightSpaceMatrix;\nvoid main(){ gl_Position = uLightSpaceMatrix * vec4(aPos, 1.0); }";
            const char* fs = "#version 430 core\nvoid main(){}";
            unsigned int v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, 0); glCompileShader(v);
            unsigned int f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, 0); glCompileShader(f);
            program = glCreateProgram(); glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        }

        void update(const Buffer<int>& activeIndices, const Buffer<TileNode>& nodes, V3 lightDir)
        {
            if (activeIndices.count == 0) return;

            V3 f = lightDir;
            V3 worldUp = (fabsf(f.z) > 0.9f) ? V3(1,0,0) : V3(0,0,1);
            V3 s = crs(worldUp, f);
            float slen = sqrtf(s.x*s.x + s.y*s.y + s.z*s.z); s.x/=slen; s.y/=slen; s.z/=slen;
            V3 u = crs(f, s);

            V3 focus = {0,0,0};
            float totalWeight = 0;
            for(int i=0; i<(int)activeIndices.count; ++i)
            {
                int nIdx = activeIndices[i];
                focus.x += (float)nodes[nIdx].sphereCenter.x;
                focus.y += (float)nodes[nIdx].sphereCenter.y;
                focus.z += (float)nodes[nIdx].sphereCenter.z;
                totalWeight += 1.0f;
            }
            focus.x /= totalWeight; focus.y /= totalWeight; focus.z /= totalWeight;

            V3 eye = focus - f * 2000.0f;
            
            memset(lightView, 0, sizeof(lightView));
            lightView[0] = s.x; lightView[4] = s.y; lightView[8] = s.z;
            lightView[1] = u.x; lightView[5] = u.y; lightView[9] = u.z;
            lightView[2] = -f.x; lightView[6] = -f.y; lightView[10] = -f.z;
            lightView[12] = -(s.x*eye.x + s.y*eye.y + s.z*eye.z);
            lightView[13] = -(u.x*eye.x + u.y*eye.y + u.z*eye.z);
            lightView[14] = (f.x*eye.x + f.y*eye.y + f.z*eye.z);
            lightView[15] = 1.0f;

            float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, minZ = 1e30f, maxZ = -1e30f;
            for(int i=0; i<(int)activeIndices.count; ++i)
            {
                int nIdx = activeIndices[i];
                V3 c = { (float)nodes[nIdx].sphereCenter.x, (float)nodes[nIdx].sphereCenter.y, (float)nodes[nIdx].sphereCenter.z };
                float r = (float)nodes[nIdx].sphereRadius;

                float cx = lightView[0]*c.x + lightView[4]*c.y + lightView[8]*c.z + lightView[12];
                float cy = lightView[1]*c.x + lightView[5]*c.y + lightView[9]*c.z + lightView[13];
                float cz = lightView[2]*c.x + lightView[6]*c.y + lightView[10]*c.z + lightView[14];

                if(cx - r < minX) minX = cx - r; if(cx + r > maxX) maxX = cx + r;
                if(cy - r < minY) minY = cy - r; if(cy + r > maxY) maxY = cy + r;
                if(cz - r < minZ) minZ = cz - r; if(cz + r > maxZ) maxZ = cz + r;
            }

            Math::mat4_ortho(lightProj, minX, maxX, minY, maxY, -maxZ, -minZ);
            Math::mat4_mul(lightSpaceMat, lightProj, lightView);
        }
    };

    struct TilesetTestApp
    {
        Buffer<TileNode> nodes;
        Buffer<int> activeNodeIndices;
        Buffer<uint32_t> nodeLastFrameActive;
        Math::Vec3 meshMin, meshMax;
        int meshCount = 0;
        bool initialized = false;
        uint32_t currentFrame = 0;

        Math::DVec3 targetEcef;
        double enuMat[16];
        int rootIdx = -1;
        bool isFallback = false;
        bool isGoogle = false;

        SSAO ssao;
        ShadowMap shadow;
        V3 lightDir = { 0.5f, 0.8f, -0.4f };

        // Async Loading
        std::queue<TilesetLoader::AsyncLoadRequest*> requestQueue;
        std::vector<TilesetLoader::AsyncLoadRequest*> completedRequests;
        std::mutex queueMutex;
        std::thread workerThread;
        bool stopWorker = false;
        int activeRequests = 0;

        void workerFunc()
        {
            while (!stopWorker)
            {
                TilesetLoader::AsyncLoadRequest* req = nullptr;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    if (!requestQueue.empty())
                    {
                        req = requestQueue.front();
                        requestQueue.pop();
                    }
                }

                if (req)
                {
                    TilesetLoader::ProcessAsyncLoad(req);
                    std::lock_guard<std::mutex> lock(queueMutex);
                    completedRequests.push_back(req);
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }

        Math::DVec3 getEyeEcef(const ArcballCamera& camera)
        {
            float cp = cosf(camera.pitch), sp = sinf(camera.pitch);
            float cy = cosf(camera.yaw), sy = sinf(camera.yaw);
            float gx = camera.focusPoint.x + camera.distance * cp * sy;       
            float gy = camera.focusPoint.y + camera.distance * sp;            
            float gz = camera.focusPoint.z - camera.distance * cp * cy;       
            
            double east = (double)gx;
            double north = (double)(-gz); 
            double up = (double)gy;
            
            Math::DVec3 eyeEcef;
            eyeEcef.x = targetEcef.x + east * enuMat[0] + north * enuMat[4] + up * enuMat[8];
            eyeEcef.y = targetEcef.y + east * enuMat[1] + north * enuMat[5] + up * enuMat[9];
            eyeEcef.z = targetEcef.z + east * enuMat[2] + north * enuMat[6] + up * enuMat[10];
            
            return eyeEcef;
        }

        void init()
        {
            printf("[TilesetTest] Initializing Async Tileset + Shadows (London Focus)...\n");

            if (!g_Arena.memory) g_Arena.init(128 * 1024 * 1024);
            else g_Arena.reset();

            nodes.init(g_Arena, 32768);
            activeNodeIndices.init(g_Arena, 32768);
            nodeLastFrameActive.init(g_Arena, 32768);
            memset(nodeLastFrameActive.data, 0, 32768 * sizeof(uint32_t));

            AliceViewer* av = AliceViewer::instance();
            int w, h;
            glfwGetFramebufferSize(av->window, &w, &h);
            ssao.init(w, h);
            shadow.init();

            lightDir.normalise();

            std::string rootUrl = TilesetLoader::ConstructGoogleTilesetURL();   
            isFallback = false;
            isGoogle = false;

            if (rootUrl.empty()) isFallback = true;
            else
            {
                json rootJson;
                if (!TilesetLoader::FetchRootTileset(rootUrl, rootJson) || rootJson.contains("error")) isFallback = true;
                else
                {
                    std::string baseUrl = rootUrl;
                    size_t lastSlash = baseUrl.find_last_of("/\\");
                    if (lastSlash != std::string::npos) baseUrl = baseUrl.substr(0, lastSlash + 1);

                    rootIdx = TilesetLoader::ParseRecursive(rootJson["root"], nodes, g_Arena, baseUrl);
                    if (rootIdx != -1)
                    {
                        isGoogle = true;
                        double targetLat = 51.5138 * Math::DEG2RAD; // London
                        double targetLon = -0.0983 * Math::DEG2RAD;
                        targetEcef = Math::lla_to_ecef(targetLat, targetLon, 0.0);
                        Math::denu_matrix(enuMat, targetLat, targetLon);        
                    }
                    else isFallback = true;
                }
            }

            if (isFallback)
            {
                nodes.clear(); activeNodeIndices.clear();
                rootUrl = "test_tileset.json";
                json rootJson;
                if (TilesetLoader::FetchRootTileset(rootUrl, rootJson))
                {
                    rootIdx = TilesetLoader::ParseRecursive(rootJson["root"], nodes, g_Arena, "./");
                    if (rootIdx != -1)
                    {
                        targetEcef = nodes[rootIdx].sphereCenter;   
                        Math::LLA lla = Math::ecef_to_lla(targetEcef);
                        Math::denu_matrix(enuMat, lla.lat, lla.lon);
                    }
                }
            }

            if (isGoogle)
            {
                printf("[TilesetTest] Google Tileset Root Loaded. targetEcef: %.2f, %.2f, %.2f\n", targetEcef.x, targetEcef.y, targetEcef.z);
            }
            if (isFallback)
            {
                printf("[TilesetTest] FALLBACK: Using %s\n", rootUrl.c_str());
            }

            if (rootIdx != -1)
            {
                printf("[TilesetTest] Root node parsed. Total nodes: %d\n", (int)nodes.count);
                meshMin = { -100, -100, -100 }; meshMax = { 100, 100, 100 };
                av->camera.focusPoint = { 0, 0, 0 };
                av->camera.distance = 450.0f;
                av->camera.pitch = -0.65f;
                av->camera.yaw = 0.25f;
            }
            else
            {
                printf("[TilesetTest] ERROR: Failed to load any tileset root.\n");
            }

            workerThread = std::thread(&TilesetTestApp::workerFunc, this);
            initialized = true;
        }

        void updateAsyncLoading()
        {
            std::vector<TilesetLoader::AsyncLoadRequest*> ready;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                ready = completedRequests;
                completedRequests.clear();
            }

            int uploadBudget = 2; // Max 2 GLB GPU uploads per frame
            for (auto req : ready)
            {
                if (req->success && req->isJson)
                {
                    TileNode& node = nodes[req->nodeIdx];
                    json ext;
                    try {
                        ext = json::parse(req->jsonBuffer, req->jsonBuffer + req->jsonBufferSize);
                    } catch(...) { req->success = false; }

                    if (req->success && ext.contains("root"))
                    {
                        std::string newBaseUrl = req->url;
                        size_t pos = newBaseUrl.find_last_of('/');
                        if (pos != std::string::npos) newBaseUrl = newBaseUrl.substr(0, pos + 1);

                        int extRoot = TilesetLoader::ParseRecursive(ext["root"], nodes, g_Arena, newBaseUrl, node.worldTransform);
                        node.firstChild = extRoot;
                        node.childCount = 1;
                        node.isLoaded = true;
                        node.isExternal = false;
                        printf("[Async] JSON Grafted: %s (Root Idx: %d)\n", req->url, extRoot);
                    }
                    node.isLoading = false;
                    activeRequests--;
                    free(req->jsonBuffer);
                    delete req;
                    continue;
                }

                if (uploadBudget > 0 && req->success)
                {
                    TileNode& node = nodes[req->nodeIdx];
                    node.mesh.instanceVbo = 0;
                    node.mesh.count = (int)req->icount;
                    glGenVertexArrays(1, &node.mesh.vao);
                    glGenBuffers(1, &node.mesh.vbo);
                    glGenBuffers(1, &node.mesh.ebo);
                    glBindVertexArray(node.mesh.vao);
                    glBindBuffer(GL_ARRAY_BUFFER, node.mesh.vbo);
                    glBufferData(GL_ARRAY_BUFFER, req->vcount * 8 * sizeof(float), req->verts, GL_STATIC_DRAW);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, node.mesh.ebo);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, req->icount * sizeof(unsigned int), req->indices, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
                    glEnableVertexAttribArray(2);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
                    
                    node.isLoaded = true;
                    node.isLoading = false;
                    activeRequests--;
                    uploadBudget--;

                    free(req->verts);
                    free(req->indices);
                    delete req;
                }
                else if (!req->success)
                {
                    nodes[req->nodeIdx].isLoading = false;
                    activeRequests--;
                    delete req;
                }
                else
                {
                    // Push back to ready queue for next frame
                    std::lock_guard<std::mutex> lock(queueMutex);
                    completedRequests.push_back(req);
                    break;
                }
            }

            char key[256];
            bool hasKey = ApiKeyReader::GetGoogleKey(key, 256);
            for (int i = 0; i < (int)activeNodeIndices.count; ++i)
            {
                int nIdx = activeNodeIndices[i];
                if (nodes[nIdx].isLoading || nodes[nIdx].isLoaded) continue;
                if (!nodes[nIdx].hasContent && !nodes[nIdx].isExternal) continue;
                if (activeRequests >= 8) break;

                if (nodes[nIdx].hasContent && std::string(nodes[nIdx].contentUri) == "MOCK_CATHEDRAL")
                {
                    MockCathedral::Generate(nodes[nIdx].mesh, g_Arena);
                    nodes[nIdx].isLoaded = true;
                    continue;
                }

                auto req = new TilesetLoader::AsyncLoadRequest();
                req->nodeIdx = nIdx;
                req->isJson = nodes[nIdx].isExternal;

                std::string url = nodes[nIdx].contentUri ? nodes[nIdx].contentUri : "";
                if (url.find("http") == std::string::npos) url = std::string(nodes[nIdx].baseUrl) + url;
                if (hasKey && url.find("googleapis.com") != std::string::npos)
                {
                    url += (url.find('?') == std::string::npos ? "?" : "&");
                    url += "key="; url += key;
                }
                strncpy(req->url, url.c_str(), 1024);
                req->centerEcef = targetEcef;
                memcpy(req->enuMat, enuMat, sizeof(enuMat));
                memcpy(req->transform, nodes[nIdx].worldTransform, sizeof(req->transform));
                req->success = false; req->completed = false;
                req->jsonBuffer = nullptr;

                nodes[nIdx].isLoading = true;
                activeRequests++;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    requestQueue.push(req);
                }
            }
        }

        void unloadOldMeshes()
        {
            for (int i = 0; i < (int)nodes.count; ++i)
            {
                if (nodes[i].isLoaded && (currentFrame - nodeLastFrameActive[i] > 300))
                {
                    nodes[i].mesh.cleanup();
                    nodes[i].isLoaded = false;
                }
            }
        }

        void draw()
        {
            if (!initialized || rootIdx == -1) { backGround(0.9f); return; }
            currentFrame++;
            AliceViewer* av = AliceViewer::instance();
            if(!av) return;

            int w, h;
            glfwGetFramebufferSize(av->window, &w, &h);
            float aspect = (float)w / (float)h;

            double mEcefToEnu[16];
            Math::mat4d_identity(mEcefToEnu);
            // Rotation part: Row 0 = East, Row 1 = North, Row 2 = Up
            mEcefToEnu[0] = enuMat[0]; mEcefToEnu[4] = enuMat[1]; mEcefToEnu[8] = enuMat[2];
            mEcefToEnu[1] = enuMat[4]; mEcefToEnu[5] = enuMat[5]; mEcefToEnu[9] = enuMat[6];
            mEcefToEnu[2] = enuMat[8]; mEcefToEnu[6] = enuMat[9]; mEcefToEnu[10] = enuMat[10];
            // Translation part: T = -R * targetEcef
            mEcefToEnu[12] = -(enuMat[0]*targetEcef.x + enuMat[1]*targetEcef.y + enuMat[2]*targetEcef.z);
            mEcefToEnu[13] = -(enuMat[4]*targetEcef.x + enuMat[5]*targetEcef.y + enuMat[6]*targetEcef.z);
            mEcefToEnu[14] = -(enuMat[8]*targetEcef.x + enuMat[9]*targetEcef.y + enuMat[10]*targetEcef.z);

            double mEnuToGl[16] = {0};
            mEnuToGl[0] = 1.0;  mEnuToGl[6] = -1.0; mEnuToGl[9] = 1.0; mEnuToGl[15] = 1.0;

            M4 vMat = av->camera.getViewMatrix();
            double dvMat[16]; for(int i=0; i<16; ++i) dvMat[i] = (double)vMat.m[i];   
            M4 projM4 = AliceViewer::makeInfiniteReversedZProjRH(av->fov, aspect, 0.1f);
            float pMat[16]; memcpy(pMat, projM4.m, sizeof(pMat));
            double dpMat[16]; for(int i=0; i<16; ++i) dpMat[i] = (double)pMat[i];     

            double mTemp[16], mTemp2[16], mFinal[16];
            Math::mat4d_mul(mTemp, dvMat, mEnuToGl);
            Math::mat4d_mul(mTemp2, mTemp, mEcefToEnu);
            Math::mat4d_mul(mFinal, dpMat, mTemp2);

            float fFinal[16], planes[6][4];
            for(int i=0; i<16; ++i) fFinal[i] = (float)mFinal[i];   
            TilesetLoader::ExtractFrustumPlanes(fFinal, planes);    

            activeNodeIndices.clear();
            int nodesVisited = 0;
            TilesetLoader::TraverseAndGraft(rootIdx, nodes, getEyeEcef(av->camera), (float)h, av->fov, 16.0f, planes, activeNodeIndices, g_Arena, 0, nodesVisited, 32768);

            for(int i=0; i<(int)activeNodeIndices.count; ++i) nodeLastFrameActive[activeNodeIndices[i]] = currentFrame;

            updateAsyncLoading();
            unloadOldMeshes();

            shadow.update(activeNodeIndices, nodes, lightDir);

            // Pass 1: Shadow Map
            glViewport(0, 0, shadow.width, shadow.height);
            glBindFramebuffer(GL_FRAMEBUFFER, shadow.fbo);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glUseProgram(shadow.program);
            glUniformMatrix4fv(glGetUniformLocation(shadow.program, "uLightSpaceMatrix"), 1, GL_FALSE, shadow.lightSpaceMat);
            for (int i = 0; i < (int)activeNodeIndices.count; ++i)
            {
                int nIdx = activeNodeIndices[i];
                if (nodes[nIdx].isLoaded) nodes[nIdx].mesh.draw();
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // SSAO G-Buffer Pass
            glViewport(0, 0, w, h);
            ssao.clearQueue();
            float ident[16]; Math::mat4_identity(ident);
            for (int i = 0; i < (int)activeNodeIndices.count; ++i)
            {
                int nIdx = activeNodeIndices[i];
                if (nodes[nIdx].isLoaded) ssao.addObject(&nodes[nIdx].mesh, ident, 0.7f, 0.7f, 0.7f);
            }

            float fvMat[16]; for(int i=0; i<16; ++i) fvMat[i] = (float)dvMat[i];
            ssao.gBuffer.bind();
            glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            for (int i = 0; i < ssao.queue.count; ++i)
            {
                glUseProgram(ssao.gs.program);
                float mv[16], mvp[16];
                Math::mat4_mul(mv, fvMat, ssao.queue.modelMatrices[i]);
                Math::mat4_mul(mvp, pMat, mv);
                glUniformMatrix4fv(ssao.gs.uMV, 1, GL_FALSE, mv);
                glUniformMatrix4fv(ssao.gs.uMVP, 1, GL_FALSE, mvp);
                glUniform3fv(ssao.gs.uColor, 1, ssao.queue.colors[i]);
                ssao.queue.meshes[i]->draw();
            }
            ssao.gBuffer.unbind();

            // SSAO Pass
            ssao.ssaoFbo.bind();
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(ssao.ss.program);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[0]);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[1]);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_1D, ssao.ss.kernelTex);
            glUniformMatrix4fv(ssao.ss.uP, 1, GL_FALSE, pMat);
            glUniform1f(ssao.ss.uRadius, 25.0f);
            glUniform2f(ssao.ss.uRes, (float)w, (float)h);
            ssao.quad.draw();
            ssao.ssaoFbo.unbind();

            // Final Composite with Shadow
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUseProgram(ssao.bs.program);
            
            // Need to update SSAO blur shader to handle shadow mapping composite
            // For now, we reuse composite but with shadow map bound to unit 4
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ssao.ssaoFbo.textures[0]);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[2]);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[0]);
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[1]);
            glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shadow.depthTex);
            
            glUniform1i(glGetUniformLocation(ssao.bs.program, "sSSAO"), 0);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "sAlbedo"), 1);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "gPos"), 2);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "gNorm"), 3);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "sShadow"), 4);
            glUniformMatrix4fv(glGetUniformLocation(ssao.bs.program, "uLightSpaceMatrix"), 1, GL_FALSE, shadow.lightSpaceMat);
            glUniform3fv(glGetUniformLocation(ssao.bs.program, "uLightDir"), 1, &lightDir.x);
            glUniform1i(ssao.bs.uDir, 2); 
            ssao.quad.draw();
        }

        ~TilesetTestApp()
        {
            stopWorker = true;
            if (workerThread.joinable()) workerThread.join();
        }
    };
}

#ifdef TILESET_TEST_RUN_TEST
static Alice::TilesetTestApp g_TilesetTest;
extern "C" void setup() { g_TilesetTest.init(); }
extern "C" void update(float dt) { (void)dt; }
extern "C" void draw() { g_TilesetTest.draw(); }
#endif

#endif
