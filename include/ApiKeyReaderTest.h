#ifndef ALICE_API_KEY_READER_TEST_H
#define ALICE_API_KEY_READER_TEST_H

#include <cstdio>
#include <cstring>
#include "AliceViewer.h"
#include "ApiKeyReader.h"

namespace Alice
{
    struct ApiKeyReaderTest
    {
        char maskedKey[64];

        void init()
        {
            char rawKey[128];
            memset(rawKey, 0, sizeof(rawKey));
            memset(maskedKey, 0, sizeof(maskedKey));

            if (ApiKeyReader::GetGoogleKey(rawKey, sizeof(rawKey)))
            {
                size_t len = strlen(rawKey);
                if (len > 4)
                {
                    strncpy(maskedKey, rawKey, 4);
                    strcat(maskedKey, "****");
                }
                else
                {
                    strcpy(maskedKey, "****");
                }
                printf("[ApiKeyReaderTest] Google API Key found: %s\n", maskedKey);
            }
            else
            {
                printf("[ApiKeyReaderTest] Google API Key NOT found in API_KEYS.txt\n");
                strcpy(maskedKey, "NOT_FOUND");
            }

            // CAMERA FRAMING MANDATE: Set up a default viewport
            AliceViewer* av = AliceViewer::instance();
            if (av)
            {
                av->camera.focusPoint = { 0.0f, 0.0f, 0.0f };
                av->camera.distance = 5.0f;
                av->camera.pitch = 0.4f;
                av->camera.yaw = 0.4f;
            }
        }

        void draw()
        {
            // Set background to 0.9 as per aesthetics mandate
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

static Alice::ApiKeyReaderTest g_KeyTest;

extern "C" void setup()
{
    if (!Alice::g_Arena.memory) Alice::g_Arena.init(16 * 1024 * 1024);
    g_KeyTest.init();
}

extern "C" void update(float dt) { (void)dt; }

extern "C" void draw()
{
    g_KeyTest.draw();
}

extern "C" void keyPress(unsigned char key, int x, int y) { (void)key; (void)x; (void)y; }

#endif // ALICE_API_KEY_READER_TEST_H
