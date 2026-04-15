#ifndef TILESET_TEST_H
#define TILESET_TEST_H

#include <cstdio>
#include "TilesetLoader.h"
#include "AliceViewer.h"
#include "ApiKeyReader.h"

namespace Alice
{
    struct TilesetTestApp
    {
        Buffer<TileNode> nodes;
        Buffer<int> activeNodeIndices;
        Buffer<MeshPrimitive> accumulatedMeshes;
        Math::Vec3 meshMin, meshMax;
        int meshCount = 0;
        bool initialized = false;

        void init()
        {
            printf("[TilesetTest] Starting Targeted BVH Traversal (St. Paul's)...\n");
            
            if (!g_Arena.memory) 
            {
                g_Arena.init(128 * 1024 * 1024);
                if (!g_Arena.memory) { printf("[TilesetTest] FATAL: Failed to init arena\n"); exit(1); }
            }
            nodes.init(g_Arena, 16384);
            activeNodeIndices.init(g_Arena, 4096);
            accumulatedMeshes.init(g_Arena, 4096);

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
                if (!TilesetLoader::FetchRootTileset(rootUrl, rootJson) || rootJson.contains("error"))
                {
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

                        printf("[TilesetTest] Traversing BVH toward St. Paul's...\n");
                        TilesetLoader::TraverseAndGraft(rootIdx, nodes, targetEcef, activeNodeIndices, g_Arena);
                        
                        double enuMat[16];
                        Math::denu_matrix(enuMat, targetLat, targetLon);
                        loadMeshes(targetEcef, enuMat);
                    }
                }
            }

            if (isFallback)
            {
                rootUrl = "test_tileset.json";
                json rootJson;
                if (TilesetLoader::FetchRootTileset(rootUrl, rootJson))
                {
                    int rootIdx = TilesetLoader::ParseRecursive(rootJson["root"], nodes, g_Arena, "./");
                    if (rootIdx != -1)
                    {
                        // Use center from mock tileset for fallback
                        Math::DVec3 targetEcef = nodes[rootIdx].sphereCenter;
                        Math::LLA lla = Math::ecef_to_lla(targetEcef);
                        double enuMat[16];
                        Math::denu_matrix(enuMat, lla.lat, lla.lon);

                        printf("[TilesetTest] Loading fallback meshes...\n");
                        activeNodeIndices.push_back(rootIdx); // Force load root for mock
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

                av->camera.distance = radius * 2.5f; 
                av->camera.yaw = 0.785f; // 45 degrees
                av->camera.pitch = 0.523f; // 30 degrees
                printf("[TilesetTest] Camera framed at (%.2f, %.2f, %.2f) with distance %.2f\n", 
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
                if (TilesetLoader::LoadGLBWithENU(glbUrl.c_str(), node.mesh, targetEcef, enuMat, node.transform, mMin, mMax, g_Arena))
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

            aliceColor3f(0.176f, 0.176f, 0.176f); // Deep Charcoal
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
