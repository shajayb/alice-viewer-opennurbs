#ifndef TILESET_TEST_H
#define TILESET_TEST_H

#include <cstdio>
#include "TilesetLoader.h"
#include "AliceViewer.h"
#include "ApiKeyReader.h"
#include "NormalShader.h"

namespace Alice
{
    struct TilesetTestApp
    {
        Buffer<TileNode> nodes;
        Buffer<int> activeNodeIndices;
        Buffer<MeshPrimitive> accumulatedMeshes;
        Math::Vec3 meshMin, meshMax;
        NormalShader normalShader;
        int meshCount = 0;
        bool initialized = false;

        void init()
        {
            printf("[TilesetTest] Starting Optimized BVH Traversal (St. Paul's)...\n");
            
            if (!g_Arena.memory) 
            {
                g_Arena.init(128 * 1024 * 1024);
            }
            else
            {
                g_Arena.reset();
            }
            
            if (!g_Arena.memory) { printf("[TilesetTest] FATAL: Failed to init arena\n"); exit(1); }
            if (!normalShader.init()) { printf("[TilesetTest] FATAL: Failed to init NormalShader\n"); exit(1); }

            nodes.init(g_Arena, 32768);
            activeNodeIndices.init(g_Arena, 32768);
            accumulatedMeshes.init(g_Arena, 32768);

            std::string rootUrl = TilesetLoader::ConstructGoogleTilesetURL();
            bool isFallback = false;

            if (rootUrl.empty()) 
            {
                fprintf(stderr, "[TilesetTest] WARNING: No API Key found. Using fallback.\n");
                isFallback = true;
            }
            else
            {
                printf("[TilesetTest] Fetching root from Google 3D Tiles API...\n");
                json rootJson;
                bool fetchSuccess = TilesetLoader::FetchRootTileset(rootUrl, rootJson);
                if (!fetchSuccess || rootJson.contains("error"))
                {
                    if (rootJson.contains("error")) 
                    {
                        fprintf(stderr, "[TilesetTest] Google API ERROR: %s\n", rootJson["error"].dump(2).c_str());
                    }
                    fprintf(stderr, "[TilesetTest] WARNING: Google API Fetch failed. Using fallback.\n");
                    isFallback = true;
                }
                else
                {
                    std::string baseUrl = rootUrl;
                    size_t lastSlash = baseUrl.find_last_of('/');
                    if (lastSlash == std::string::npos) lastSlash = baseUrl.find_last_of('\\');
                    if (lastSlash != std::string::npos) baseUrl = baseUrl.substr(0, lastSlash + 1);

                    int rootIdx = TilesetLoader::ParseRecursive(rootJson["root"], nodes, g_Arena, baseUrl);
                    if (rootIdx != -1)
                    {
                        // St. Paul's Cathedral (Lat: 51.5138, Lon: -0.0983)
                        double targetLat = 51.5138 * Math::DEG2RAD;
                        double targetLon = -0.0983 * Math::DEG2RAD;
                        Math::DVec3 targetEcef = Math::lla_to_ecef(targetLat, targetLon, 0.0);

                        double enuMat[16];
                        Math::denu_matrix(enuMat, targetLat, targetLon);

                        printf("[TilesetTest] Traversing BVH toward St. Paul's with Frustum Culling...\n");
                        
                        AliceViewer* av = AliceViewer::instance();
                        float vHeight = 720.0f;
                        float fov = 0.8f;
                        float aspect = 16.0f / 9.0f;
                        if (av)
                        {
                            int w, h;
                            glfwGetFramebufferSize(av->window, &w, &h);
                            vHeight = (float)h;
                            fov = av->fov;
                            aspect = (float)w / (float)h;
                        }

                        // Extract Frustum Planes in ECEF Space
                        // 1. ECEF to ENU
                        double mEcefToEnu[16];
                        for(int i=0; i<16; ++i) mEcefToEnu[i] = enuMat[i];
                        mEcefToEnu[12] = -(enuMat[0]*targetEcef.x + enuMat[1]*targetEcef.y + enuMat[2]*targetEcef.z);
                        mEcefToEnu[13] = -(enuMat[4]*targetEcef.x + enuMat[5]*targetEcef.y + enuMat[6]*targetEcef.z);
                        mEcefToEnu[14] = -(enuMat[8]*targetEcef.x + enuMat[9]*targetEcef.y + enuMat[10]*targetEcef.z);

                        // 2. ENU to GL (X=E, Y=U, Z=-N)
                        double mEnuToGl[16] = {0};
                        mEnuToGl[0] = 1.0;  // X = E
                        mEnuToGl[6] = -1.0; // Z = -N
                        mEnuToGl[9] = 1.0;  // Y = U
                        mEnuToGl[15] = 1.0;

                        // 3. View (GL to View)
                        M4 vMat = av->camera.getViewMatrix();
                        double dvMat[16];
                        for(int i=0; i<16; ++i) dvMat[i] = (double)vMat.m[i];

                        // 4. Projection (View to Clip)
                        float pMat[16];
                        Math::mat4_perspective(pMat, fov, aspect, 0.1f, 10000.0f);
                        double dpMat[16];
                        for(int i=0; i<16; ++i) dpMat[i] = (double)pMat[i];

                        // Combine: P * V * EnuToGl * EcefToEnu
                        double mTemp[16], mTemp2[16], mFinal[16];
                        Math::mat4d_mul(mTemp, dvMat, mEnuToGl);
                        Math::mat4d_mul(mTemp2, mTemp, mEcefToEnu);
                        Math::mat4d_mul(mFinal, dpMat, mTemp2);

                        float fFinal[16], planes[6][4];
                        for(int i=0; i<16; ++i) fFinal[i] = (float)mFinal[i];
                        TilesetLoader::ExtractFrustumPlanes(fFinal, planes);

                        int nodesVisited = 0;
                        TilesetLoader::TraverseAndGraft(rootIdx, nodes, targetEcef, vHeight, fov, 4.0f, planes, activeNodeIndices, g_Arena, 0, nodesVisited, 32768);
                        
                        printf("[TilesetTest] Calling loadMeshes with targetEcef: (%.2f, %.2f, %.2f)\n", targetEcef.x, targetEcef.y, targetEcef.z);
                        loadMeshes(targetEcef, enuMat);
                    }
                }
            }

            if (isFallback)
            {
                nodes.clear();
                activeNodeIndices.clear();
                accumulatedMeshes.clear();

                rootUrl = "test_tileset.json";
                json rootJson;
                if (TilesetLoader::FetchRootTileset(rootUrl, rootJson))
                {
                    int rootIdx = TilesetLoader::ParseRecursive(rootJson["root"], nodes, g_Arena, "./");
                    if (rootIdx != -1)
                    {
                        Math::DVec3 targetEcef = nodes[rootIdx].sphereCenter;
                        Math::LLA lla = Math::ecef_to_lla(targetEcef);
                        double enuMat[16];
                        Math::denu_matrix(enuMat, lla.lat, lla.lon);

                        float planes[6][4];
                        for(int i=0; i<6; ++i) { planes[i][0]=0; planes[i][1]=0; planes[i][2]=0; planes[i][3]=1e30f; } 

                        printf("[TilesetTest] Loading fallback meshes via TraverseAndGraft...\n");
                        int nodesVisited = 0;
                        TilesetLoader::TraverseAndGraft(rootIdx, nodes, targetEcef, 1000.0f, 0.8f, 1.0f, planes, activeNodeIndices, g_Arena, 0, nodesVisited, 16384);
                        
                        printf("[TilesetTest] Calling loadMeshes (Fallback) with targetEcef: (%.2f, %.2f, %.2f)\n", targetEcef.x, targetEcef.y, targetEcef.z);
                        loadMeshes(targetEcef, enuMat);
                    }
                }
            }

            // CAMERA FRAMING MANDATE
            AliceViewer* av = AliceViewer::instance();
            if (av && meshCount > 0)
            {
                av->camera.focusPoint.x = (meshMin.x + meshMax.x) * 0.5f;
                av->camera.focusPoint.y = (meshMin.y + meshMax.y) * 0.5f;
                av->camera.focusPoint.z = (meshMin.z + meshMax.z) * 0.5f;

                float dx = meshMax.x - meshMin.x;
                float dy = meshMax.y - meshMin.y;
                float dz = meshMax.z - meshMin.z;
                float radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;

                av->camera.distance = radius * 1.8f; 
                av->camera.yaw = 0.6f;   
                av->camera.pitch = 0.4f; 
                printf("[TilesetTest] Cinematic camera framed at (%.2f, %.2f, %.2f) with distance %.2f\n", 
                    av->camera.focusPoint.x, av->camera.focusPoint.y, av->camera.focusPoint.z, av->camera.distance);
            }
            initialized = true;
        }

        void loadMeshes(Math::DVec3 targetEcef, double* enuMat)
        {
            char key[256];
            bool hasKey = ApiKeyReader::GetGoogleKey(key, 256);
            meshMin = { 1e30f, 1e30f, 1e30f };
            meshMax = { -1e30f, -1e30f, -1e30f };
            meshCount = 0;

            for (int i = 0; i < (int)activeNodeIndices.count; ++i)
            {
                int nodeIdx = activeNodeIndices[i];
                TileNode& node = nodes[nodeIdx];
                if (!node.hasContent || node.isExternal) continue;

                std::string glbUrl = node.contentUri;
                if (glbUrl.find("http") == std::string::npos) 
                {
                    glbUrl = std::string(node.baseUrl) + glbUrl;
                }

                if (hasKey && glbUrl.find("googleapis.com") != std::string::npos)
                {
                    glbUrl += (glbUrl.find('?') == std::string::npos ? "?" : "&");
                    glbUrl += "key="; glbUrl += key;
                }

                Math::Vec3 mMin, mMax;
                if (TilesetLoader::LoadGLBWithENU(glbUrl.c_str(), node.mesh, targetEcef, enuMat, node.worldTransform, mMin, mMax, g_Arena))
                {
                    node.isLoaded = true;
                    accumulatedMeshes.push_back(node.mesh);

                    if (meshCount == 0) { meshMin = mMin; meshMax = mMax; }
                    else
                    {
                        if (mMin.x < meshMin.x) meshMin.x = mMin.x;
                        if (mMin.y < meshMin.y) meshMin.y = mMin.y;
                        if (mMin.z < meshMin.z) meshMin.z = mMin.z;
                        if (mMax.x > meshMax.x) meshMax.x = mMax.x;
                        if (mMax.y > meshMax.y) meshMax.y = mMax.y;
                        if (mMax.z > meshMax.z) meshMax.z = mMax.z;
                    }
                    meshCount++;
                }
            }
            printf("[TilesetTest] Accumulated %d meshes.\n", meshCount);
        }

        void draw()
        {
            backGround(0.9f);
            if (!initialized) return;

            AliceViewer* av = AliceViewer::instance();
            if(!av) return;

            glEnable(GL_DEPTH_TEST);
            
            int w, h;
            glfwGetFramebufferSize(av->window, &w, &h);
            float aspect = (float)w / (float)h;

            float p[16], v[16], pv[16];
            Math::mat4_perspective(p, av->fov, aspect, 0.1f, 10000.0f);
            M4 viewMat = av->camera.getViewMatrix();
            for(int i=0; i<16; ++i) v[i] = viewMat.m[i];
            Math::mat4_mul(pv, p, v);

            normalShader.use();
            
            float m[16], mvp[16], n[9];
            Math::mat4_identity(m); 
            Math::mat4_mul(mvp, pv, m);
            Math::mat3_normal(n, m);
            
            normalShader.setMVP(mvp);
            normalShader.setNormalMatrix(n);

            for (int i = 0; i < (int)accumulatedMeshes.count; ++i)
            {
                accumulatedMeshes[i].draw();
            }
        }
    };
}

#ifdef TILESET_TEST_RUN_TEST

static Alice::TilesetTestApp g_TilesetTest;

extern "C" void setup()
{
    g_TilesetTest.init();
}

extern "C" void update(float dt) { (void)dt; }

extern "C" void draw()
{
    g_TilesetTest.draw();
}

extern "C" void keyPress(unsigned char key, int x, int y) { (void)key; (void)x; (void)y; }

#endif // TILESET_TEST_RUN_TEST

#endif // TILESET_TEST_H
