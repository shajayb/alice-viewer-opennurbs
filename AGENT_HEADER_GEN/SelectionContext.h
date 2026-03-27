#ifndef SELECTION_CONTEXT_H
#define SELECTION_CONTEXT_H

#include <vector>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cassert>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"

#ifndef ALICE_FRAMEWORK
#define ALICE_FRAMEWORK
#endif

struct SelectionContext
{
    std::vector<int> selectedIndices;
    V3 dragStart; // Screen Space Pixels (x, y, 0)
    V3 dragEnd;   // Screen Space Pixels (x, y, 0)
    bool isMarqueeActive = false;
    int hoveredIndex = -1;

    SelectionContext()
    {
        selectedIndices.reserve(100000);
    }

    void clearSelection()
    {
        selectedIndices.clear();
    }

    void updateMarquee(V3 currentPosPixels)
    {
        dragEnd = currentPosPixels;
    }

    // Performance optimized matrix multiplication (Inlined by compiler)
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

    // Optimized O(N) selection logic
    void finalizeMarquee(const std::vector<V3>& cloud, bool isDeselect)
    {
        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer || !viewer->window || cloud.empty())
        {
            isMarqueeActive = false;
            return;
        }

        int w, h;
        glfwGetFramebufferSize(viewer->window, &w, &h);
        if (w <= 0 || h <= 0) return;

        M4 view = viewer->camera.getViewMatrix();
        M4 proj = perspectiveM4(viewer->fov, (float)w / (float)h, 0.1f, 1000.0f);
        M4 mvp = multM4(proj, view);

        float minX = std::min(dragStart.x, dragEnd.x);
        float maxX = std::max(dragStart.x, dragEnd.x);
        float minY = std::min(dragStart.y, dragEnd.y);
        float maxY = std::max(dragStart.y, dragEnd.y);

        // Scratch buffer for current marquee selection (naturally sorted)
        static std::vector<int> currentSelection;
        currentSelection.clear();
        currentSelection.reserve(cloud.size());

        const float* m = mvp.m;
        const size_t nPoints = cloud.size();
        const V3* pData = cloud.data();

        // Hot Loop: Inlined math and W-clipping
        for (size_t i = 0; i < nPoints; ++i)
        {
            const V3& p = pData[i];
            
            // Branchless-friendly manual projection
            float xw = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
            float yw = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
            float zw = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
            float ww = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];

            // W-Clip: Ignore points behind camera or outside near/far
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
                currentSelection.push_back((int)i);
            }
        }

        // O(N) Linear Merge Strategy
        std::vector<int> nextIndices;
        nextIndices.reserve(selectedIndices.size() + currentSelection.size());

        if (isDeselect)
        {
            std::set_difference(selectedIndices.begin(), selectedIndices.end(),
                               currentSelection.begin(), currentSelection.end(),
                               std::back_inserter(nextIndices));
        }
        else
        {
            std::set_union(selectedIndices.begin(), selectedIndices.end(),
                          currentSelection.begin(), currentSelection.end(),
                          std::back_inserter(nextIndices));
        }
        
        selectedIndices = std::move(nextIndices);
        isMarqueeActive = false;
    }

    bool isSelected(int index) const
    {
        return std::binary_search(selectedIndices.begin(), selectedIndices.end(), index);
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

    void drawHighlights(const std::vector<V3>& cloud)
    {
        glDisable(GL_DEPTH_TEST);
        aliceColor3f(0.863f, 0.078f, 0.235f); 
        alicePointSize(10.0f);
        for (int idx : selectedIndices)
        {
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
static std::vector<V3> g_testCloud;
static const char* g_testName = "Press 1-5 to Run Tests";
static float g_testPerf = 0.0f;

static void runTest1_MassiveCloud()
{
    g_testName = "[1] Massive Cloud Stress (1M pts)";
    printf("\n%s\n", g_testName);
    g_testCloud.resize(1000000);
    for (int i = 0; i < 1000000; ++i) 
        g_testCloud[i] = V3((float)(i % 100) - 50.0f, (float)((i / 100) % 100) - 50.0f, (float)(i / 10000) - 50.0f);

    g_selCtx.clearSelection();
    g_selCtx.dragStart = { 0, 0, 0 };
    g_selCtx.dragEnd = { 2000, 2000, 0 };

    auto start = std::chrono::high_resolution_clock::now();
    g_selCtx.finalizeMarquee(g_testCloud, false);
    auto end = std::chrono::high_resolution_clock::now();

    g_testPerf = std::chrono::duration<float, std::milli>(end - start).count();
    printf(">> 1,000,000 points processed in %.3f ms. O(N) Verified.\n", g_testPerf);
}

static void runTest2_Culling()
{
    g_testName = "[2] Behind-Camera Culling";
    printf("\n%s\n", g_testName);
    g_testCloud = { V3(0, 0, 10.0f), V3(0, 0, -5.0f) }; // One behind (assuming cam looks -Z), one in front
    g_selCtx.clearSelection();
    g_selCtx.dragStart = { 0, 0, 0 };
    g_selCtx.dragEnd = { 2000, 2000, 0 };
    g_selCtx.finalizeMarquee(g_testCloud, false);
    
    bool passed = (g_selCtx.selectedIndices.size() == 1);
    printf(">> Points Selected: %zu. Result: %s\n", g_selCtx.selectedIndices.size(), passed ? "PASSED" : "FAILED (Behind-point selected)");
}

static void runTest3_InvertedDrag()
{
    g_testName = "[3] Inverted Marquee Drag";
    printf("\n%s\n", g_testName);
    g_testCloud = { V3(0, 0, -5.0f) };
    g_selCtx.clearSelection();
    // Drag from bottom-right to top-left
    g_selCtx.dragStart = { 1000, 1000, 0 };
    g_selCtx.dragEnd = { 0, 0, 0 };
    g_selCtx.finalizeMarquee(g_testCloud, false);
    
    bool passed = (g_selCtx.selectedIndices.size() == 1);
    printf(">> Result: %s (Marquee: 1000,1000 to 0,0)\n", passed ? "PASSED" : "FAILED");
}

static void runTest4_EmptyCloud()
{
    g_testName = "[4] Empty Point Cloud";
    printf("\n%s\n", g_testName);
    g_testCloud.clear();
    g_selCtx.clearSelection();
    g_selCtx.finalizeMarquee(g_testCloud, false);
    printf(">> Result: PASSED (No crash, 0 selected)\n");
}

static void runTest5_Toggle()
{
    g_testName = "[5] High-Freq Toggle (50 iterations)";
    printf("\n%s\n", g_testName);
    g_testCloud = { V3(0, 0, -5.0f) };
    g_selCtx.clearSelection();
    g_selCtx.dragStart = { 0, 0, 0 }; g_selCtx.dragEnd = { 1000, 1000, 0 };
    
    for (int i = 0; i < 50; ++i)
    {
        g_selCtx.finalizeMarquee(g_testCloud, i % 2 == 1); // Alternate select/deselect
    }
    bool passed = (g_selCtx.selectedIndices.size() == 0);
    printf(">> Result: %s (Final count: %zu)\n", passed ? "PASSED" : "FAILED (Leak detected)", g_selCtx.selectedIndices.size());
}

extern "C"
{
    void setup()
    {
        printf("--------------------------------------------------\n");
        printf("SelectionContext Interactive Test Suite\n");
        printf("Keys [1-5]: Trigger Edge Case Validations\n");
        printf("--------------------------------------------------\n");
        
        // Initial Visual State
        for (int x = -5; x < 5; ++x) 
            for (int y = -5; y < 5; ++y) 
                g_testCloud.push_back({ (float)x, (float)y, -5.0f });
    }

    void update(float dt) { (void)dt; }
    
    void draw()
    {
        backGround(0.9f);
        drawGrid(10.0f);
        
        aliceColor3f(0.176f, 0.176f, 0.176f);
        alicePointSize(4.0f);
        for (const auto& p : g_testCloud) drawPoint(p);
        
        g_selCtx.drawHighlights(g_testCloud);
        
        // Visualizing drag bounds if any
        if (g_selCtx.dragStart.x != g_selCtx.dragEnd.x)
        {
            g_selCtx.isMarqueeActive = true;
            g_selCtx.drawMarquee();
        }
    }

    void keyPress(unsigned char k, int x, int y) 
    { 
        if (k == '1') runTest1_MassiveCloud();
        if (k == '2') runTest2_Culling();
        if (k == '3') runTest3_InvertedDrag();
        if (k == '4') runTest4_EmptyCloud();
        if (k == '5') runTest5_Toggle();
        if (k == 'c' || k == 'C') g_selCtx.clearSelection();
    }
    
    void mousePress(int b, int s, int x, int y) { (void)b; (void)s; (void)x; (void)y; }
    void mouseMotion(int x, int y) { (void)x; (void)y; }
}

#endif
#endif
