#ifndef TILESET_TEST_H
#define TILESET_TEST_H

#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include "TilesetLoader.h"
#include "AliceViewer.h"
#include "ApiKeyReader.h"
#include "NormalShader.h"
#include "SSAO.h"
#include "MockCathedral.h"

namespace Alice
{
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
            printf("[TilesetTest] Initializing Dynamic BVH Traversal with SSAO...\n");

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
                        double targetLat = 51.5138 * Math::DEG2RAD;
                        double targetLon = -0.0983 * Math::DEG2RAD;
                        targetEcef = Math::lla_to_ecef(targetLat, targetLon, 0.0);
                        Math::denu_matrix(enuMat, targetLat, targetLon);        
                    }
                    else isFallback = true;
                }
            }

            if (isFallback)
            {
                nodes.clear();
                activeNodeIndices.clear();
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

            if (rootIdx != -1)
            {
                float planes[6][4];
                memset(planes, 0, sizeof(planes));
                int nodesVisited = 0;
                activeNodeIndices.clear();
                TilesetLoader::TraverseAndGraft(rootIdx, nodes, targetEcef, (float)h, 0.8f, 1.0f, planes, activeNodeIndices, g_Arena, 0, nodesVisited, 32768);
                
                meshMin = { 1e30f, 1e30f, 1e30f }; meshMax = { -1e30f, -1e30f, -1e30f };
                loadMeshes();

                if (av && meshCount > 0)
                {
                    av->camera.focusPoint = { (meshMin.x + meshMax.x) * 0.5f, (meshMin.y + meshMax.y) * 0.5f, (meshMin.z + meshMax.z) * 0.5f };
                    float dx = meshMax.x - meshMin.x, dy = meshMax.y - meshMin.y, dz = meshMax.z - meshMin.z;
                    float radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;
                    av->camera.distance = radius * 1.8f; 
                    av->camera.yaw = 0.6f; av->camera.pitch = 0.4f; 
                }
            }
            initialized = true;
        }

        void loadMeshes()
        {
            char key[256];
            bool hasKey = ApiKeyReader::GetGoogleKey(key, 256);
            
            for (int i = 0; i < (int)activeNodeIndices.count; ++i)
            {
                int nodeIdx = activeNodeIndices[i];
                TileNode& node = nodes[nodeIdx];
                if (!node.hasContent || node.isExternal || node.isLoaded) continue;

                if (std::string(node.contentUri) == "MOCK_CATHEDRAL")
                {
                    MockCathedral::Generate(node.mesh, g_Arena);
                    node.isLoaded = true;
                    meshCount++;
                    continue;
                }

                std::string glbUrl = node.contentUri;
                if (glbUrl.find("http") == std::string::npos) glbUrl = std::string(node.baseUrl) + glbUrl;
                if (hasKey && glbUrl.find("googleapis.com") != std::string::npos)
                {
                    glbUrl += (glbUrl.find('?') == std::string::npos ? "?" : "&");
                    glbUrl += "key="; glbUrl += key;
                }

                Math::Vec3 mMin, mMax;
                if (TilesetLoader::LoadGLBWithENU(glbUrl.c_str(), node.mesh, targetEcef, enuMat, node.worldTransform, mMin, mMax, g_Arena))
                {
                    node.isLoaded = true;
                    if (meshCount == 0) { meshMin = mMin; meshMax = mMax; }
                    else
                    {
                        if (mMin.x < meshMin.x) meshMin.x = mMin.x; if (mMin.y < meshMin.y) meshMin.y = mMin.y; if (mMin.z < meshMin.z) meshMin.z = mMin.z;
                        if (mMax.x > meshMax.x) meshMax.x = mMax.x; if (mMax.y > meshMax.y) meshMax.y = mMax.y; if (mMax.z > meshMax.z) meshMax.z = mMax.z;
                    }
                    meshCount++;
                }
            }
        }

        void unloadOldMeshes()
        {
            for (int i = 0; i < (int)nodes.count; ++i)
            {
                if (nodes[i].isLoaded && (currentFrame - nodeLastFrameActive[i] > 120))
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
            mEcefToEnu[0] = enuMat[0]; mEcefToEnu[1] = enuMat[1]; mEcefToEnu[2] = enuMat[2];
            mEcefToEnu[4] = enuMat[4]; mEcefToEnu[5] = enuMat[5]; mEcefToEnu[6] = enuMat[6];
            mEcefToEnu[8] = enuMat[8]; mEcefToEnu[9] = enuMat[9]; mEcefToEnu[10] = enuMat[10];
            mEcefToEnu[12] = -(enuMat[0]*targetEcef.x + enuMat[1]*targetEcef.y + enuMat[2]*targetEcef.z);
            mEcefToEnu[13] = -(enuMat[4]*targetEcef.x + enuMat[5]*targetEcef.y + enuMat[6]*targetEcef.z);
            mEcefToEnu[14] = -(enuMat[8]*targetEcef.x + enuMat[9]*targetEcef.y + enuMat[10]*targetEcef.z);

            double mEnuToGl[16] = {0};
            mEnuToGl[0] = 1.0;  mEnuToGl[6] = -1.0; mEnuToGl[9] = 1.0; mEnuToGl[15] = 1.0;

            M4 vMat = av->camera.getViewMatrix();
            double dvMat[16]; for(int i=0; i<16; ++i) dvMat[i] = (double)vMat.m[i];   
            float pMat[16]; Math::mat4_perspective(pMat, av->fov, aspect, 0.1f, 10000.0f);
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
            TilesetLoader::TraverseAndGraft(rootIdx, nodes, getEyeEcef(av->camera), (float)h, av->fov, 4.0f, planes, activeNodeIndices, g_Arena, 0, nodesVisited, 32768);

            for(int i=0; i<(int)activeNodeIndices.count; ++i) nodeLastFrameActive[activeNodeIndices[i]] = currentFrame;

            loadMeshes();
            unloadOldMeshes();

            // SSAO Rendering Loop
            ssao.clearQueue();
            float ident[16]; Math::mat4_identity(ident);
            for (int i = 0; i < (int)activeNodeIndices.count; ++i)
            {
                int nIdx = activeNodeIndices[i];
                if (nodes[nIdx].isLoaded) ssao.addObject(&nodes[nIdx].mesh, ident, 0.7f, 0.7f, 0.7f);
            }

            // Draw SSAO
            float fvMat[16]; for(int i=0; i<16; ++i) fvMat[i] = (float)dvMat[i];
            
            // Pass 1: G-Buffer
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

            // Pass 2: SSAO
            ssao.ssaoFbo.bind();
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(ssao.ss.program);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[0]);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[1]);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_1D, ssao.ss.kernelTex);
            glUniform1i(glGetUniformLocation(ssao.ss.program, "gPos"), 0);
            glUniform1i(glGetUniformLocation(ssao.ss.program, "gNorm"), 1);
            glUniform1i(glGetUniformLocation(ssao.ss.program, "uKernelTex"), 2);
            glUniformMatrix4fv(ssao.ss.uP, 1, GL_FALSE, pMat);
            glUniform1f(ssao.ss.uRadius, 15.0f);
            glUniform1f(ssao.ss.uBias, 0.05f);
            glUniform2f(ssao.ss.uRes, (float)w, (float)h);
            glUniform1i(ssao.ss.uSamples, 64);
            ssao.quad.draw();
            ssao.ssaoFbo.unbind();

            // Pass 3: Blur & Composite
            glViewport(0, 0, w, h);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUseProgram(ssao.bs.program);
            glUniform1i(ssao.bs.uDir, 2); // Final composite
            glUniform1i(ssao.bs.uMode, 0); // LIT mode
            glUniform2f(ssao.bs.uRes, (float)w, (float)h);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ssao.ssaoFbo.textures[0]);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[2]);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[0]);
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssao.gBuffer.textures[1]);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "sSSAO"), 0);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "sAlbedo"), 1);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "gPos"), 2);
            glUniform1i(glGetUniformLocation(ssao.bs.program, "gNorm"), 3);
            ssao.quad.draw();
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
