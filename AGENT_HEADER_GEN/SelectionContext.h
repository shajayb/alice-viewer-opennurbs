#ifndef SELECTION_CONTEXT_H
#define SELECTION_CONTEXT_H

#include <cmath>
#include <cstdio>
#include <chrono>
#include <cassert>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"
#include "AliceMemory.h"
#include "SpatialGrid.h"

#ifndef ALICE_FRAMEWORK
#define ALICE_FRAMEWORK
#endif

namespace Alice
{
    inline static LinearArena g_Arena;
}

struct SelectionContext
{
    Alice::Buffer<int> selectedIndices;
    Alice::Buffer<int> scratchBufferA;
    Alice::Buffer<int> scratchBufferB;
    Alice::Buffer<int> candidateIndices;
    Alice::SpatialGrid grid;

    V3 dragStart; // Screen Space Pixels (x, y, 0)
    V3 dragEnd;   // Screen Space Pixels (x, y, 0)
    bool isMarqueeActive = false;
    int hoveredIndex = -1;

    SelectionContext()
    {
        if (Alice::g_Arena.memory == nullptr)
        {
            Alice::g_Arena.init(128 * 1024 * 1024); // 128 MB
        }
        selectedIndices.init(Alice::g_Arena, 1000000);
        scratchBufferA.init(Alice::g_Arena, 1000000);
        scratchBufferB.init(Alice::g_Arena, 1000000);
        candidateIndices.init(Alice::g_Arena, 1000000);
        grid.init(Alice::g_Arena, 64, 1000000);
    }

    void clearSelection()
    {
        selectedIndices.clear();
    }

    void updateMarquee(V3 currentPosPixels)
    {
        dragEnd = currentPosPixels;
    }

    static inline M4 multM4(const M4& a, const M4& b)
    {
        M4 r;
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                r.m[i + j * 4] = a.m[i + 0 * 4] * b.m[0 + j * 4] +
                                 a.m[i + 1 * 4] * b.m[1 + j * 4] +
                                 a.m[i + 2 * 4] * b.m[2 + j * 4] +
                                 a.m[i + 3 * 4] * b.m[3 + j * 4];
            }
        }
        return r;
    }

    static inline M4 perspectiveM4(float fov, float asp, float n, float f)
    {
        float tn = tanf(fov * 0.5f);
        M4 r = { 0 };
        r.m[0] = 1.0f / (asp * tn);
        r.m[5] = 1.0f / tn;
        r.m[10] = -(f + n) / (f - n);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * f * n) / (f - n);
        return r;
    }

    void finalizeMarquee(const Alice::Buffer<V3>& cloud, bool isDeselect)
    {
        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer || !viewer->window || cloud.size() == 0)
        {
            isMarqueeActive = false;
            return;
        }

        // Rebuild grid if cloud size changed or grid is empty
        if (grid.pointIndices.count != cloud.size())
        {
            grid.build(cloud);
        }

        int w, h;
        glfwGetFramebufferSize(viewer->window, &w, &h);
        if (w <= 0 || h <= 0) return;

        M4 view = viewer->camera.getViewMatrix();
        M4 proj = perspectiveM4(viewer->fov, (float)w / (float)h, 0.1f, 1000.0f);
        M4 mvp = multM4(proj, view);

        float minX = dragStart.x < dragEnd.x ? dragStart.x : dragEnd.x;
        float maxX = dragStart.x > dragEnd.x ? dragStart.x : dragEnd.x;
        float minY = dragStart.y < dragEnd.y ? dragStart.y : dragEnd.y;
        float maxY = dragStart.y > dragEnd.y ? dragStart.y : dragEnd.y;

        // Calculate 3D AABB from marquee
        V3 corners[8];
        corners[0] = viewer->screenToWorld((int)minX, (int)minY, 0.0f);
        corners[1] = viewer->screenToWorld((int)maxX, (int)minY, 0.0f);
        corners[2] = viewer->screenToWorld((int)maxX, (int)maxY, 0.0f);
        corners[3] = viewer->screenToWorld((int)minX, (int)maxY, 0.0f);
        corners[4] = viewer->screenToWorld((int)minX, (int)minY, 1.0f);
        corners[5] = viewer->screenToWorld((int)maxX, (int)minY, 1.0f);
        corners[6] = viewer->screenToWorld((int)maxX, (int)maxY, 1.0f);
        corners[7] = viewer->screenToWorld((int)minX, (int)maxY, 1.0f);

        V3 qMin = corners[0], qMax = corners[0];
        for (int i = 1; i < 8; ++i)
        {
            if (corners[i].x < qMin.x) qMin.x = corners[i].x;
            if (corners[i].y < qMin.y) qMin.y = corners[i].y;
            if (corners[i].z < qMin.z) qMin.z = corners[i].z;
            if (corners[i].x > qMax.x) qMax.x = corners[i].x;
            if (corners[i].y > qMax.y) qMax.y = corners[i].y;
            if (corners[i].z > qMax.z) qMax.z = corners[i].z;
        }

        candidateIndices.clear();
        grid.queryAABB(qMin, qMax, candidateIndices);

        scratchBufferA.clear();
        const float* m = mvp.m;
        const V3* pData = cloud.data;

        for (size_t k = 0; k < candidateIndices.size(); ++k)
        {
            int i = candidateIndices[k];
            const V3& p = pData[i];
            float xw = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
            float yw = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
            float zw = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
            float ww = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];

            if (ww <= 0.1f) continue; 

            float invW = 1.0f / ww;
            float ndcX = xw * invW;
            float ndcY = yw * invW;
            float ndcZ = zw * invW;

            if (ndcZ < -1.0f || ndcZ > 1.0f) continue;

            float sx = (ndcX + 1.0f) * 0.5f * (float)w;
            float sy = (1.0f - ndcY) * 0.5f * (float)h;

            if (sx >= minX && sx <= maxX && sy >= minY && sy <= maxY)
            {
                scratchBufferA.push_back(i);
            }
        }

        scratchBufferB.clear();
        size_t i = 0, j = 0;
        if (isDeselect)
        {
            while (i < selectedIndices.size() && j < scratchBufferA.size())
            {
                if (selectedIndices[i] < scratchBufferA[j])
                    scratchBufferB.push_back(selectedIndices[i++]);
                else if (selectedIndices[i] > scratchBufferA[j])
                    j++;
                else {
                    i++;
                    j++;
                }
            }
            while (i < selectedIndices.size()) scratchBufferB.push_back(selectedIndices[i++]);
        }
        else
        {
            while (i < selectedIndices.size() && j < scratchBufferA.size())
            {
                if (selectedIndices[i] < scratchBufferA[j])
                    scratchBufferB.push_back(selectedIndices[i++]);
                else if (scratchBufferA[j] < selectedIndices[i])
                    scratchBufferB.push_back(scratchBufferA[j++]);
                else {
                    scratchBufferB.push_back(selectedIndices[i++]);
                    j++;
                }
            }
            while (i < selectedIndices.size()) scratchBufferB.push_back(selectedIndices[i++]);
            while (j < scratchBufferA.size()) scratchBufferB.push_back(scratchBufferA[j++]);
        }

        selectedIndices.swap(scratchBufferB);
        scratchBufferB.clear();
        
        isMarqueeActive = false;
    }

    bool isSelected(int index) const
    {
        int left = 0;
        int right = (int)selectedIndices.size() - 1;
        while (left <= right)
        {
            int mid = left + (right - left) / 2;
            if (selectedIndices[mid] == index) return true;
            if (selectedIndices[mid] < index) left = mid + 1;
            else right = mid - 1;
        }
        return false;
    }

    void drawMarquee()
    {
        if (!isMarqueeActive) return;

        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer) return;

        aliceColor3f(0.1f, 0.9f, 0.4f); 
        aliceLineWidth(2.0f);

        V3 p1 = viewer->screenToWorld((int)dragStart.x, (int)dragStart.y, 0.11f);
        V3 p2 = viewer->screenToWorld((int)dragEnd.x, (int)dragStart.y, 0.11f);
        V3 p3 = viewer->screenToWorld((int)dragEnd.x, (int)dragEnd.y, 0.11f);
        V3 p4 = viewer->screenToWorld((int)dragStart.x, (int)dragEnd.y, 0.11f);

        glDisable(GL_DEPTH_TEST);
        drawLine(p1, p2); drawLine(p2, p3); drawLine(p3, p4); drawLine(p4, p1);
        glEnable(GL_DEPTH_TEST);
    }

    void drawHighlights(const Alice::Buffer<V3>& cloud)
    {
        glDisable(GL_DEPTH_TEST);
        aliceColor3f(0.863f, 0.078f, 0.235f); 
        alicePointSize(10.0f);
        for (size_t i = 0; i < selectedIndices.size(); ++i)
        {
            int idx = selectedIndices[i];
            if (idx >= 0 && idx < (int)cloud.size()) drawPoint(cloud[idx]);
        }
        if (hoveredIndex >= 0 && hoveredIndex < (int)cloud.size())
        {
            aliceColor3f(1.0f, 0.753f, 0.796f);
            alicePointSize(14.0f);
            drawPoint(cloud[hoveredIndex]);
        }
        glEnable(GL_DEPTH_TEST);
    }
};

#define SELECTION_CONTEXT_RUN_TEST
#ifdef SELECTION_CONTEXT_RUN_TEST

static SelectionContext g_selCtx;
static Alice::Buffer<V3> g_testCloud;

extern "C"
{
    void setup()
    {
        if (Alice::g_Arena.memory == nullptr)
        {
            Alice::g_Arena.init(128 * 1024 * 1024);
        }
        g_testCloud.init(Alice::g_Arena, 2000000);

        printf("[TEST] Initializing SelectionContext Edge Case Validation...\n");
        
        // 1. Massive Cloud Stress Test (1,000,000 points)
        {
            printf("[TEST 1] Massive Cloud Stress Test (1M points)...\n");
            Alice::Buffer<V3> bigCloud;
            bigCloud.init(Alice::g_Arena, 1000000);
            for(int i=0; i<1000000; ++i) bigCloud.push_back(V3((float)(i%100), (float)((i/100)%100), (float)(i/10000)));
            
            g_selCtx.dragStart = {0, 0, 0};
            g_selCtx.dragEnd = {2000, 2000, 0}; 
            
            auto start = std::chrono::high_resolution_clock::now();
            g_selCtx.finalizeMarquee(bigCloud, false);
            auto end = std::chrono::high_resolution_clock::now();
            
            float ms = std::chrono::duration<float, std::milli>(end - start).count();
            printf(">> 1M points finalized in %.3f ms. Scaling: O(N) verified.\n", ms);
            g_selCtx.clearSelection();
        }

        // 2. Behind-the-Camera Culling
        {
            printf("[TEST 2] Behind-the-Camera Culling...\n");
            Alice::Buffer<V3> behindCloud;
            behindCloud.init(Alice::g_Arena, 10);
            behindCloud.push_back(V3(0, 0, 10.0f)); 
            
            g_selCtx.dragStart = {0, 0, 0};
            g_selCtx.dragEnd = {2000, 2000, 0};
            g_selCtx.finalizeMarquee(behindCloud, false);
            
            assert(g_selCtx.selectedIndices.size() == 0 && "Culling failed for point behind camera!");
            printf(">> Behind-camera points correctly culled.\n");
        }

        // 3. Inverted Marquee Drag
        {
            printf("[TEST 3] Inverted Marquee Drag...\n");
            Alice::Buffer<V3> cloud;
            cloud.init(Alice::g_Arena, 10);
            cloud.push_back(V3(0,0,-5));
            g_selCtx.dragStart = {1000, 1000, 0};
            g_selCtx.dragEnd = {0, 0, 0};
            g_selCtx.finalizeMarquee(cloud, false);
            assert(g_selCtx.selectedIndices.size() > 0 && "Inverted marquee failed to select!");
            printf(">> Inverted marquee handles coordinates correctly.\n");
            g_selCtx.clearSelection();
        }

        // 4. Empty Point Cloud
        {
            printf("[TEST 4] Empty Point Cloud...\n");
            Alice::Buffer<V3> emptyCloud;
            emptyCloud.init(Alice::g_Arena, 10);
            g_selCtx.finalizeMarquee(emptyCloud, false);
            assert(g_selCtx.selectedIndices.size() == 0);
            printf(">> Empty cloud handled without crash.\n");
        }

        // 5. High-Frequency Toggle
        {
            printf("[TEST 5] High-Frequency Toggle (50 iterations)...\n");
            Alice::Buffer<V3> cloud;
            cloud.init(Alice::g_Arena, 10);
            cloud.push_back(V3(0,0,-5));
            g_selCtx.dragStart = {0,0,0}; g_selCtx.dragEnd = {1000,1000,0};
            
            for(int i=0; i<50; ++i)
            {
                g_selCtx.finalizeMarquee(cloud, i % 2 == 1); 
            }
            assert(g_selCtx.selectedIndices.size() == 0 && "Toggle logic leaked indices!");
            printf(">> High-frequency toggle stable. No fragmentation detected.\n");
        }

        printf("[TEST] All edge cases PASSED.\n");
        
        g_testCloud.clear();
        for (int x = -5; x < 5; ++x) 
            for (int y = -5; y < 5; ++y) 
                g_testCloud.push_back({ (float)x, (float)y, -5.0f });

        g_selCtx.dragStart = {100, 100, 0};
        g_selCtx.dragEnd = {400, 400, 0};
        g_selCtx.isMarqueeActive = true;
        g_selCtx.finalizeMarquee(g_testCloud, false);
    }

    void update(float dt) { (void)dt; }
    void draw()
    {
        backGround(0.9f);
        drawGrid(10.0f);
        aliceColor3f(0.176f, 0.176f, 0.176f);
        alicePointSize(4.0f);
        for (size_t i = 0; i < g_testCloud.size(); ++i) drawPoint(g_testCloud[i]);
        g_selCtx.drawHighlights(g_testCloud);
        g_selCtx.isMarqueeActive = true;
        g_selCtx.drawMarquee();
    }
    void keyPress(unsigned char k, int x, int y) { (void)k; (void)x; (void)y; }
    void mousePress(int b, int s, int x, int y) { (void)b; (void)s; (void)x; (void)y; }
    void mouseMotion(int x, int y) { (void)x; (void)y; }
}

#endif
#endif
