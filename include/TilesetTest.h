#ifndef TILESET_TEST_H
#define TILESET_TEST_H

#include <cstdio>
#include "TilesetLoader.h"
#include "AliceViewer.h"

namespace Alice
{
    struct TilesetTest
    {
        static void Run()
        {
            printf("[TilesetTest] Starting Network Handshake Test...\n");

            std::string url = TilesetLoader::ConstructGoogleTilesetURL();
            if (url.empty())
            {
                fprintf(stderr, "[TilesetTest] ERROR: API Key missing or Google URL construction failed.\n");
                return;
            }

            printf("[TilesetTest] Fetching root tileset from Google...\n");
            json tileset;
            if (!TilesetLoader::FetchRootTileset(url, tileset))
            {
                fprintf(stderr, "[TilesetTest] ERROR: Failed to fetch root tileset. Check network/API key.\n");
                return;
            }

            printf("[TilesetTest] SUCCESS: Root tileset.json fetched and parsed.\n");

            TilesetLoader::RootMetadata meta;
            if (TilesetLoader::ExtractRootMetadata(tileset, meta))
            {
                printf("[TilesetTest] Root Tile Geometric Error: %f\n", meta.geometricError);
                if (meta.hasTransform)
                {
                    printf("[TilesetTest] Root Tile Transform found.\n");
                    for (int i = 0; i < 4; ++i)
                    {
                        printf("  [%f, %f, %f, %f]\n", meta.transform[i*4+0], meta.transform[i*4+1], meta.transform[i*4+2], meta.transform[i*4+3]);
                    }
                }
                else
                {
                    printf("[TilesetTest] Root Tile Transform NOT found (using identity).\n");
                }
            }
            else
            {
                fprintf(stderr, "[TilesetTest] ERROR: Failed to extract root metadata. JSON content:\n%s\n", tileset.dump(2).c_str());
            }
        }
    };
}

#ifdef TILESET_TEST_RUN_TEST

namespace Alice
{
    struct TilesetTestApp
    {
        void init()
        {
            TilesetTest::Run();
            
            // CAMERA FRAMING MANDATE
            AliceViewer* av = AliceViewer::instance();
            if (av)
            {
                av->camera.focusPoint = { 0, 0, 0 };
                av->camera.distance = 25.0f;
                av->camera.yaw = 0.8f;
                av->camera.pitch = 0.6f;
            }
        }

        void draw()
        {
            backGround(0.9f);

            // Draw a simple charcoal box to verify rendering
            aliceColor3f(0.176f, 0.176f, 0.176f); // #2D2D2D

            V3 p[8] = {
                {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}
            };

            // Draw edges of a unit cube
            drawLine(p[0], p[1]); drawLine(p[1], p[2]); drawLine(p[2], p[3]); drawLine(p[3], p[0]);
            drawLine(p[4], p[5]); drawLine(p[5], p[6]); drawLine(p[6], p[7]); drawLine(p[7], p[4]);
            drawLine(p[0], p[4]); drawLine(p[1], p[5]); drawLine(p[2], p[6]); drawLine(p[3], p[7]);
        }
    };
}

static Alice::TilesetTestApp g_TilesetTest;

extern "C" void setup()
{
    if (!Alice::g_Arena.memory) Alice::g_Arena.init(16 * 1024 * 1024);
    g_TilesetTest.init();
}

extern "C" void update(float dt) { (void)dt; }

extern "C" void draw()
{
    g_TilesetTest.draw();
}

extern "C" void keyPress(unsigned char key, int x, int y) { (void)key; (void)x; (void)y; }

#endif

#endif // TILESET_TEST_H
