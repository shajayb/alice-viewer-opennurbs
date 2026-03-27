#ifndef SELECTION_CONTEXT_H
#define SELECTION_CONTEXT_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"

struct SelectionContext
{
    std::vector<int> selectedIndices;
    V3 dragStart; // Screen Space Pixels (x, y, 0)
    V3 dragEnd;   // Screen Space Pixels (x, y, 0)
    bool isMarqueeActive = false;
    int hoveredIndex = -1;

    SelectionContext()
    {
    }

    void clearSelection()
    {
        selectedIndices.clear();
    }

    void updateMarquee(V3 currentPosPixels)
    {
        dragEnd = currentPosPixels;
    }

    static M4 multM4(const M4& a, const M4& b)
    {
        M4 r;
        const float* A = a.m;
        const float* B = b.m;
        float* R = r.m;

        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                R[i + j * 4] = A[i + 0 * 4] * B[0 + j * 4] +
                               A[i + 1 * 4] * B[1 + j * 4] +
                               A[i + 2 * 4] * B[2 + j * 4] +
                               A[i + 3 * 4] * B[3 + j * 4];
            }
        }
        return r;
    }

    static M4 perspectiveM4(float fov, float asp, float n, float f)
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

    static V3 projectToNDC(const V3& p, const M4& mvp)
    {
        float v[4] = { p.x, p.y, p.z, 1.0f };
        float r[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                r[i] += mvp.m[j * 4 + i] * v[j];
            }
        }
        return V3(r[0] / r[3], r[1] / r[3], r[2] / r[3]);
    }

    void finalizeMarquee(const std::vector<V3>& cloud, bool isDeselect)
    {
        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer || !viewer->window)
        {
            return;
        }

        int w, h;
        glfwGetFramebufferSize(viewer->window, &w, &h);
        if (w <= 0 || h <= 0)
        {
            return;
        }

        M4 view = viewer->camera.getViewMatrix();
        M4 proj = perspectiveM4(viewer->fov, (float)w / (float)h, 0.1f, 1000.0f);
        M4 mvp = multM4(proj, view);

        float minX = std::min(dragStart.x, dragEnd.x);
        float maxX = std::max(dragStart.x, dragEnd.x);
        float minY = std::min(dragStart.y, dragEnd.y);
        float maxY = std::max(dragStart.y, dragEnd.y);

        for (int i = 0; i < (int)cloud.size(); ++i)
        {
            V3 ndc = projectToNDC(cloud[i], mvp);
            
            // Project NDC to Screen Space (pixels)
            float sx = (ndc.x + 1.0f) * 0.5f * (float)w;
            float sy = (1.0f - ndc.y) * 0.5f * (float)h;

            if (sx >= minX && sx <= maxX && sy >= minY && sy <= maxY)
            {
                auto it = std::lower_bound(selectedIndices.begin(), selectedIndices.end(), i);
                if (isDeselect)
                {
                    if (it != selectedIndices.end() && *it == i)
                    {
                        selectedIndices.erase(it);
                    }
                }
                else
                {
                    if (it == selectedIndices.end() || *it != i)
                    {
                        selectedIndices.insert(it, i);
                    }
                }
            }
        }
        
        isMarqueeActive = false;
    }

    bool isSelected(int index) const
    {
        return std::binary_search(selectedIndices.begin(), selectedIndices.end(), index);
    }

    void drawMarquee()
    {
        if (!isMarqueeActive)
        {
            return;
        }

        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer)
        {
            return;
        }

        aliceColor3f(0.1f, 0.9f, 0.4f); // Neon Green
        aliceLineWidth(2.0f);

        // Project 2D pixels to world space on a plane very near the camera to simulate 2D overlay
        V3 p1 = viewer->screenToWorld((int)dragStart.x, (int)dragStart.y, 0.1f);
        V3 p2 = viewer->screenToWorld((int)dragEnd.x, (int)dragStart.y, 0.1f);
        V3 p3 = viewer->screenToWorld((int)dragEnd.x, (int)dragEnd.y, 0.1f);
        V3 p4 = viewer->screenToWorld((int)dragStart.x, (int)dragEnd.y, 0.1f);

        glDisable(GL_DEPTH_TEST);
        drawLine(p1, p2);
        drawLine(p2, p3);
        drawLine(p3, p4);
        drawLine(p4, p1);
        glEnable(GL_DEPTH_TEST);
    }

    void drawHighlights(const std::vector<V3>& cloud)
    {
        glDisable(GL_DEPTH_TEST);
        
        // Selected points in Crimson
        aliceColor3f(0.863f, 0.078f, 0.235f); 
        alicePointSize(10.0f);
        for (int idx : selectedIndices)
        {
            if (idx >= 0 && idx < (int)cloud.size())
            {
                drawPoint(cloud[idx]);
            }
        }

        // Hovered point in Pink
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
static int g_frameCount = 0;

extern "C"
{
    void setup()
    {
        AliceViewer* viewer = AliceViewer::instance();
        if (viewer)
        {
            viewer->camera.setBookmark("Perspective");
        }

        g_testCloud.clear();
        // Generate a 3D volumetric grid 10x10x10
        for (int z = -5; z < 5; ++z)
        {
            for (int y = -5; y < 5; ++y)
            {
                for (int x = -5; x < 5; ++x)
                {
                    g_testCloud.push_back({ (float)x, (float)y, (float)z });
                }
            }
        }

        int w, h;
        glfwGetFramebufferSize(viewer->window, &w, &h);
        
        // Simulate a 2D marquee drag in the center of the screen (50% area)
        g_selCtx.dragStart = { (float)w * 0.25f, (float)h * 0.25f, 0.0f };
        g_selCtx.dragEnd = { (float)w * 0.75f, (float)h * 0.75f, 0.0f };
        g_selCtx.isMarqueeActive = true;
        
        // Finalize to select points within the screen-space box
        g_selCtx.finalizeMarquee(g_testCloud, false);
        
        // Hover a specific point for verification
        g_selCtx.hoveredIndex = 500; // Middle of the grid
    }

    void update(float dt)
    {
        (void)dt;
        g_frameCount++;
        if (g_frameCount == 10)
        {
            int w, h;
            glfwGetFramebufferSize(AliceViewer::instance()->window, &w, &h);
            unsigned char* buffer = new unsigned char[w * h * 3];
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buffer);

            std::ofstream ppm("AGENT_HEADER_GEN/tmp/selection_verify.ppm");
            ppm << "P3\n" << w << " " << h << "\n255\n";
            for (int i = h - 1; i >= 0; --i)
            {
                for (int j = 0; j < w; ++j)
                {
                    int idx = (i * w + j) * 3;
                    ppm << (int)buffer[idx] << " " << (int)buffer[idx + 1] << " " << (int)buffer[idx + 2] << " ";
                }
                ppm << "\n";
            }
            ppm.close();
            delete[] buffer;
            printf("Validation snapshot saved to AGENT_HEADER_GEN/tmp/selection_verify.ppm\n");
            exit(0);
        }
    }

    void draw()
    {
        backGround(0.9f); // CAD Light Grey
        drawGrid(10.0f);

        // Structural geometry in Deep Charcoal #2D2D2D
        aliceColor3f(0.176f, 0.176f, 0.176f);
        alicePointSize(4.0f);
        for (const auto& p : g_testCloud)
        {
            drawPoint(p);
        }

        g_selCtx.drawHighlights(g_testCloud);
        
        // Keep marquee visible for test verification
        g_selCtx.isMarqueeActive = true;
        g_selCtx.drawMarquee();
    }

    void keyPress(unsigned char key, int x, int y) { (void)key; (void)x; (void)y; }
    void mousePress(int button, int state, int x, int y) { (void)button; (void)state; (void)x; (void)y; }
    void mouseMotion(int x, int y) { (void)x; (void)y; }
}

#endif // SELECTION_CONTEXT_RUN_TEST

#endif // SELECTION_CONTEXT_H
