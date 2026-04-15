#ifndef CAD_PIPELINE_TEST_H
#define CAD_PIPELINE_TEST_H

#include "AliceViewer.h"
#include "Tileset.h"
#include "NormalShader.h"

struct CADPipelineTest
{
    Alice::Alg::Tileset tileset;
    Math::Vec3 min, max;
    NormalShader shader;

    void frame()
    {
        if(!tileset.root) return;
        min = tileset.root->min;
        max = tileset.root->max;

        float dx = max.x - min.x, dy = max.y - min.y, dz = max.z - min.z;
        float radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;
        if (radius < 1e-3f) radius = 10.0f; // Prevent division by zero or tiny framing

        Math::Vec3 center = { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f };
        
        AliceViewer* av = AliceViewer::instance();
        if(av)
        {
            av->camera.focusPoint = { center.x, center.y, center.z };
            float dist = radius / tanf(av->fov * 0.5f);
            av->camera.distance = dist * 2.5f; // Add padding to see the full tile
            av->camera.pitch = 0.8f; // Perspective angle
            av->camera.yaw = 0.8f;
        }
    }

    void init()
    {
        // 1. PHASE 3: Hierarchical Tileset Traversal
        const char* url = "test_tileset.json";
        if(!tileset.load(url))
        {
            fprintf(stderr, "[CADPipelineTest] Failed to load tileset: %s\n", url);
        }

        shader.init();

        // 2. PHASE 2: Dynamic Camera Framing
        frame();
    }

    void draw()
    {
        AliceViewer* av = AliceViewer::instance();
        if(!av) return;

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int w, h;
        glfwGetFramebufferSize(av->window, &w, &h);
        if(w <= 0 || h <= 0) return;
        float aspect = (float)w / (float)h;

        M4 projMat = av->makeInfiniteReversedZProjRH(av->fov, aspect, av->nearClip);
        M4 viewMat = av->camera.getViewMatrix();
        
        float pv[16];
        Math::mat4_mul(pv, projMat.m, viewMat.m);

        shader.use();

        float m[16], mvp[16], n[9];
        Math::mat4_identity(m);
        Math::mat4_mul(mvp, pv, m);
        Math::mat3_normal(n, m);
        
        shader.setMVP(mvp);
        shader.setNormalMatrix(n);
        
        tileset.draw();
    }
};

static CADPipelineTest g_CADTest;

extern "C" void setup()
{
    if(!Alice::g_Arena.memory) Alice::g_Arena.init(128 * 1024 * 1024);
    g_CADTest.init();
}

extern "C" void update(float dt) { (void)dt; }

extern "C" void draw()
{
    g_CADTest.draw();
}

extern "C" void keyPress(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if(key == 'f' || key == 'F')
    {
        g_CADTest.frame();
    }
}

#endif
