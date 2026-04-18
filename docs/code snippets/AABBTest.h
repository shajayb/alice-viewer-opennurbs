#ifndef AABB_TEST_H
#define AABB_TEST_H

#include <cstdio>
#include <cstdlib>
#include "AliceViewer.h"
#include "AliceMemory.h"
#include "SelectionContext.h"
#include "AABBDraw.h"

#ifdef AABB_RUN_TEST

static AABBCompute g_aabb;

extern "C"
{
    void setup()
    {
        if (Alice::g_Arena.memory == nullptr)
        {
            Alice::g_Arena.init(128 * 1024 * 1024);
        }
        g_testCloud.init(Alice::g_Arena, 2000000);

        // Populate g_testCloud with some random points
        printf("[AABB] Initializing test cloud...\n");
        g_testCloud.clear();
        for (int x = -10; x <= 10; ++x)
        {
            for (int y = -10; y <= 10; ++y)
            {
                for (int z = -5; z <= 5; ++z)
                {
                    g_testCloud.push_back({ (float)x * 2.0f, (float)y * 2.0f, (float)z * 2.0f });
                }
            }
        }

        // Compute AABB
        printf("[AABB] Computing O(n) AABB...\n");
        g_aabb.compute(g_testCloud);
        printf("[AABB] Min: (%.2f, %.2f, %.2f) Max: (%.2f, %.2f, %.2f)\n", 
            g_aabb.min.x, g_aabb.min.y, g_aabb.min.z,
            g_aabb.max.x, g_aabb.max.y, g_aabb.max.z);

        // Frame the cloud
        AliceViewer* v = AliceViewer::instance();
        if (v)
        {
            V3 center = (g_aabb.min + g_aabb.max) * 0.5f;
            float radius = (g_aabb.max - g_aabb.min).length() * 0.5f;
            v->camera.focusPoint = center;
            v->camera.distance = radius * 2.5f;
        }
    }

    void update(float dt)
    {
        (void)dt;
    }

    void draw()
    {
        backGround(0.9f);
        drawGrid(20.0f);

        // Draw points in Charcoal (#2D2D2D -> 0.176, 0.176, 0.176)
        aliceColor3f(0.176f, 0.176f, 0.176f);
        alicePointSize(2.0f);
        const V3* pts = g_testCloud.data;
        size_t n = g_testCloud.size();
        for (size_t i = 0; i < n; ++i)
        {
            drawPoint(pts[i]);
        }

        // Draw AABB
        g_aabb.draw();
    }

    void keyPress(unsigned char key, int x, int y) { (void)key; (void)x; (void)y; }
    void mousePress(int button, int state, int x, int y) { (void)button; (void)state; (void)x; (void)y; }
    void mouseMotion(int x, int y) { (void)x; (void)y; }
}

#endif
#endif
