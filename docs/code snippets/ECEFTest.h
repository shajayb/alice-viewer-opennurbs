#ifndef ECEF_TEST_H
#define ECEF_TEST_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cmath>

#include "AliceMath.h"
#include "MeshPrimitive.h"
#include "AliceViewer.h"
#include "NormalShader.h"
#include "GLBLoader.h"

struct ECEFTest
{
    NormalShader shader;
    MeshPrimitive mesh;
    bool loaded = false;

    void setup()
    {
        if(!Alice::g_Arena.memory) Alice::g_Arena.init(128 * 1024 * 1024);
        if(!shader.init()) { fprintf(stderr, "Failed to init shader\n"); return; }

        Math::Vec3 bmin, bmax;
        // Mock URL triggers ECEF mock logic in GLBLoader
        if(GLBLoader::loadECEF("mock://ecef_tile", mesh, bmin, bmax))
        {
            loaded = true;
            printf("[ECEFTest] Successfully loaded mock ECEF tile\n");
            printf("[ECEFTest] Local BBox: (%f, %f, %f) to (%f, %f, %f)\n", bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);

            AliceViewer* av = AliceViewer::instance();
            if(av)
            {
                Math::Vec3 center = { (bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f };
                float dx = bmax.x - bmin.x;
                float dy = bmax.y - bmin.y;
                float dz = bmax.z - bmin.z;
                float radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;

                av->camera.focusPoint = {center.x, center.y, center.z};
                av->camera.distance = radius * 3.0f;
                av->camera.pitch = 0.5f;
                av->camera.yaw = 0.5f;
                
                printf("[ECEFTest] Camera framed: Center(%f, %f, %f), Distance(%f)\n", center.x, center.y, center.z, av->camera.distance);
            }
        }
        else
        {
            fprintf(stderr, "[ECEFTest] Failed to load ECEF tile\n");
        }
    }

    void draw()
    {
        if(!loaded) return;

        AliceViewer* av = AliceViewer::instance();
        if(!av) return;

        glEnable(GL_DEPTH_TEST);
        backGround(0.9f);
        drawGrid(100.0f);

        int w, h;
        glfwGetFramebufferSize(av->window, &w, &h);
        float aspect = (float)w / (float)h;

        float p[16], v[16], pv[16];
        Math::mat4_perspective(p, av->fov, aspect, 0.1f, 10000.0f);
        M4 viewMat = av->camera.getViewMatrix();
        for(int i=0; i<16; ++i) v[i] = viewMat.m[i];
        Math::mat4_mul(pv, p, v);

        shader.use();

        float m[16], mvp[16], n[9];
        Math::mat4_identity(m);
        Math::mat4_mul(mvp, pv, m);
        Math::mat3_normal(n, m);
        
        shader.setMVP(mvp);
        shader.setNormalMatrix(n);
        
        mesh.draw();
    }
};

#ifdef ECEF_TEST_RUN_TEST
static ECEFTest g_ECEFTest;

extern "C" void setup() { g_ECEFTest.setup(); }
extern "C" void update(float dt) { (void)dt; }
extern "C" void draw() { g_ECEFTest.draw(); }
#endif

#endif
