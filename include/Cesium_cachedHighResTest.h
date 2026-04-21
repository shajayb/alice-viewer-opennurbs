#ifndef CESIUM_CACHED_HIGH_RES_TEST_H
#define CESIUM_CACHED_HIGH_RES_TEST_H

#include <cstdio>
#include <vector>
#include <string>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"
#include "AliceMemory.h"

#include <thread>
#include <chrono>

namespace CesiumCachedTest {
    static uint32_t g_FrameCount = 0;
    static bool g_Loaded = false;
    static bool g_CaptureStarted = false;
    static GLuint g_VAO = 0, g_VBO = 0, g_EBO = 0;
    static int g_IndexCount = 0;

    static void loadCache() {
        const char* cachePath = "./build/bin/cesium_mesh_cache.bin";
        FILE* f = fopen(cachePath, "rb");
        if (!f) {
            cachePath = "./cesium_mesh_cache.bin";
            f = fopen(cachePath, "rb");
        }
        
        if (f) {
            uint32_t vCount, iCount;
            if (fread(&vCount, sizeof(uint32_t), 1, f) == 1 &&
                fread(&iCount, sizeof(uint32_t), 1, f) == 1) {
                std::vector<float> vbo(vCount);
                std::vector<uint32_t> ebo(iCount);
                fread(vbo.data(), sizeof(float), vCount, f);
                fread(ebo.data(), sizeof(uint32_t), iCount, f);
                fclose(f);

                glGenVertexArrays(1, &g_VAO); glBindVertexArray(g_VAO);
                glGenBuffers(1, &g_VBO); glGenBuffers(1, &g_EBO);
                glBindBuffer(GL_ARRAY_BUFFER, g_VBO); glBufferData(GL_ARRAY_BUFFER, vbo.size() * 4, vbo.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, 24, 0);
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, 24, (void*)12);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_EBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size() * 4, ebo.data(), GL_STATIC_DRAW);
                g_IndexCount = (int)iCount;
                g_Loaded = true;
                printf("[CesiumCachedTest] Loaded mesh cache: %d indices\n", g_IndexCount); fflush(stdout);
            } else {
                fclose(f);
                printf("[CesiumCachedTest] [ERROR] Invalid mesh cache file format.\n"); fflush(stdout);
            }
        } else {
            printf("[CesiumCachedTest] [ERROR] Could not find cesium_mesh_cache.bin\n"); fflush(stdout);
        }
    }

    static void setup() {
        printf("[CesiumCachedTest] Setup starting...\n"); fflush(stdout);
        loadCache();
        
        AliceViewer* av = AliceViewer::instance();
        // Matching camera from CesiumMinimalTest's initial state
        av->camera.focusPoint = {0, -40, 60}; 
        av->camera.distance = 450; 
        av->camera.pitch = 0.55f; 
        av->camera.yaw = 4.2f;
        av->fov = 0.8f;
        
        printf("[CesiumCachedTest] Setup complete.\n"); fflush(stdout);
    }

    static void update(float dt) {
        // No streaming logic needed, just static cache
    }

    static void draw() {
        if (!g_Loaded) setup();

        AliceViewer* av = AliceViewer::instance();
        if (av->m_computeAABB) {
            // Only render geometry, don't trigger state-dependent logic
            if (g_Loaded) {
                glBindVertexArray(g_VAO);
                glDrawElements(GL_TRIANGLES, g_IndexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }
            return;
        }

        int w, h; glfwGetFramebufferSize(av->window, &w, &h);
        
        // Ensure matrices are tracked for the viewer's batch renderer
        M4 v = av->camera.getViewMatrix();
        M4 p = AliceViewer::makeInfiniteReversedZProjRH(av->fov, (float)w / h, 0.1f);
        av->m_currentView = v;
        av->m_currentProj = p;

        // Clear and render
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); 
        glClearDepth(0.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); 
        glDepthFunc(GL_GEQUAL);

        if (g_Loaded) {
            glUseProgram(av->shaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(av->shaderProgram, "u_ModelView"), 1, 0, v.m);
            glUniformMatrix4fv(glGetUniformLocation(av->shaderProgram, "u_Projection"), 1, 0, p.m);
            glUniform1f(glGetUniformLocation(av->shaderProgram, "u_AmbientIntensity"), av->ambientIntensity);
            glUniform1f(glGetUniformLocation(av->shaderProgram, "u_DiffuseIntensity"), av->diffuseIntensity);

            glBindVertexArray(g_VAO);
            glDrawElements(GL_TRIANGLES, g_IndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }

        if (g_FrameCount == 20) {
            printf("[CesiumCachedTest] Frame %u: Triggering Zoom Extents...\n", g_FrameCount); fflush(stdout);
            av->frameScene();
        }

        if (g_FrameCount == 100) {
            printf("[CesiumCachedTest] Triggering 4K high-res capture...\n"); fflush(stdout);
            av->captureHighResStencils("prod_4k");
            g_CaptureStarted = true;
        }

        if (g_CaptureStarted && g_FrameCount >= 110) {
            printf("[CesiumCachedTest] Waiting for capture to finish...\n"); fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            printf("[CesiumCachedTest] Capture sequence finished. Exiting.\n"); fflush(stdout);
            exit(0);
        }

        g_FrameCount++;
    }
}

#ifdef CESIUM_CACHED_HIGH_RES_TEST_RUN_TEST
extern "C" void setup() { CesiumCachedTest::setup(); }
extern "C" void update(float dt) { CesiumCachedTest::update(dt); }
extern "C" void draw() { CesiumCachedTest::draw(); }
#endif

#endif // CESIUM_CACHED_HIGH_RES_TEST_H
