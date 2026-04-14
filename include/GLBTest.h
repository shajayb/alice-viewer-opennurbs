#ifndef GLB_TEST_H
#define GLB_TEST_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>

#include "AliceMath.h"
#include "MeshPrimitive.h"
#include "AliceViewer.h"
#include "NormalShader.h"
#include "GLBLoader.h"

struct GLBTest
{
    NormalShader shader;
    MeshPrimitive mesh;
    bool loaded = false;

    void setup()
    {
        if(!Alice::g_Arena.memory) Alice::g_Arena.init(128 * 1024 * 1024); // 128MB for GLB parsing
        if(!shader.init()) { fprintf(stderr, "Failed to init shader\n"); return; }

        const char* url = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/master/2.0/Duck/glTF-Binary/Duck.glb";
        Math::Vec3 bmin, bmax;
        if(GLBLoader::load(url, mesh, bmin, bmax))
        {
            loaded = true;
            printf("[GLBTest] Successfully loaded GLB from %s\n", url);
            printf("[GLBTest] Bounding Box: (%f, %f, %f) to (%f, %f, %f)\n", bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);

            AliceViewer* av = AliceViewer::instance();
            if(av)
            {
                // glTF (Y-up) to Alice (Z-up) rotation: X'=X, Y'=-Z, Z'=Y
                Math::Vec3 amin = { bmin.x, -bmax.z, bmin.y };
                Math::Vec3 amax = { bmax.x, -bmin.z, bmax.y };
                
                Math::Vec3 center = { (amin.x + amax.x) * 0.5f, (amin.y + amax.y) * 0.5f, (amin.z + amax.z) * 0.5f };
                float dx = amax.x - amin.x;
                float dy = amax.y - amin.y;
                float dz = amax.z - amin.z;
                float radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;

                av->camera.focusPoint = {center.x, center.y, center.z};
                av->camera.distance = radius * 2.5f; // Frame with some padding
                av->camera.pitch = 0.3f;
                av->camera.yaw = 0.7f;
                
                printf("[GLBTest] Camera framed: Center(%f, %f, %f), Distance(%f)\n", center.x, center.y, center.z, av->camera.distance);
            }
        }
        else
        {
            fprintf(stderr, "[GLBTest] Failed to load GLB\n");
        }
    }

    void draw()
    {
        if(!loaded) return;

        AliceViewer* av = AliceViewer::instance();
        if(!av) return;

        glEnable(GL_DEPTH_TEST);
        backGround(0.9f);
        drawGrid(10.0f);

        int w, h;
        glfwGetFramebufferSize(av->window, &w, &h);
        float aspect = (float)w / (float)h;

        float p[16], v[16], pv[16];
        Math::mat4_perspective(p, av->fov, aspect, 0.1f, 1000.0f);
        M4 viewMat = av->camera.getViewMatrix();
        for(int i=0; i<16; ++i) v[i] = viewMat.m[i];
        Math::mat4_mul(pv, p, v);

        shader.use();

        float m[16], mvp[16], n[9];
        Math::mat4_identity(m);
        
        // Adjust for Duck orientation/scale if needed. 
        // Standard Duck is small and Z-up in glTF? (Wait, glTF is Y-up).
        // Our AliceViewer is Z-up. 
        // Rotate 90 deg around X to bring Y-up to Z-up.
        float r[16];
        Math::mat4_identity(r);
        float angle = 1.570796f; // 90 deg
        float s = sinf(angle), c = cosf(angle);
        r[5] = c; r[6] = s; r[9] = -s; r[10] = c;
        Math::mat4_mul(m, m, r);

        Math::mat4_mul(mvp, pv, m);
        Math::mat3_normal(n, m);
        shader.setMVP(mvp);
        shader.setNormalMatrix(n);
        
        mesh.draw();
    }
};

#ifdef GLB_TEST_RUN_TEST
static GLBTest g_GLBTest;

extern "C" void setup() { g_GLBTest.setup(); }
extern "C" void update(float dt) { (void)dt; }
extern "C" void draw() { g_GLBTest.draw(); }
#endif

#endif
