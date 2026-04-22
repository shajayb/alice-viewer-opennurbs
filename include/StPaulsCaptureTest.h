#pragma once

#include <cstdio>
#include <vector>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"
#include "AliceMemory.h"

#include <stb_image_write.h>

namespace StPaulsTest {

    static GLuint g_VAO = 0;
    static GLuint g_VBO = 0;
    static GLuint g_EBO = 0;
    static uint32_t g_ICount = 0;
    static uint32_t g_FrameCount = 0;
    static bool g_Initialized = false;

    static void setup() {
        if (g_Initialized) return;

        printf("[StPaulsTest] Loading cached mesh exactly per guide...\n");
        const char* binPath = "docs/code snippets/mesh loading/cesium_mesh_cache.bin";
        FILE* f = fopen(binPath, "rb");
        if (!f) {
            binPath = "../../docs/code snippets/mesh loading/cesium_mesh_cache.bin";
            f = fopen(binPath, "rb");
        }

        if (f) {
            uint32_t vCount, iCount;
            if (fread(&vCount, sizeof(uint32_t), 1, f) != 1) { printf("[ERROR] vCount read failed\n"); fclose(f); return; }
            if (fread(&iCount, sizeof(uint32_t), 1, f) != 1) { printf("[ERROR] iCount read failed\n"); fclose(f); return; }

            std::vector<float> vertexBufferData(vCount);
            std::vector<uint32_t> indexBufferData(iCount);
            fread(vertexBufferData.data(), sizeof(float), vCount, f);
            fread(indexBufferData.data(), sizeof(uint32_t), iCount, f);
            fclose(f);

            // Compute Center-of-Mass for translation
            double mx = 0, my = 0, mz = 0;
            uint32_t numV = vCount / 6;
            for (uint32_t i = 0; i < vCount; i += 6) {
                mx += vertexBufferData[i + 0];
                my += vertexBufferData[i + 1];
                mz += vertexBufferData[i + 2];
            }
            double ox = mx / numV, oy = my / numV, oz = mz / numV;
            printf("[StPaulsTest] Center-of-Mass: {%.2f,%.2f,%.2f}\n", ox, oy, oz);

            std::vector<float> transformedVBO;
            for (uint32_t i = 0; i < vCount; i += 6) {
                transformedVBO.push_back((float)(vertexBufferData[i + 0] - ox));
                transformedVBO.push_back((float)(vertexBufferData[i + 1] - oy));
                transformedVBO.push_back((float)(vertexBufferData[i + 2] - oz));
                
                transformedVBO.push_back(vertexBufferData[i + 3]); // Normal
                transformedVBO.push_back(vertexBufferData[i + 4]);
                transformedVBO.push_back(vertexBufferData[i + 5]);
            }

            printf("[StPaulsTest] Translation complete. Uploading %d vertices.\n", (int)numV);

            glGenVertexArrays(1, &g_VAO);
            glBindVertexArray(g_VAO);
            glGenBuffers(1, &g_VBO); glBindBuffer(GL_ARRAY_BUFFER, g_VBO);
            glBufferData(GL_ARRAY_BUFFER, transformedVBO.size() * 4, transformedVBO.data(), GL_STATIC_DRAW);
            glGenBuffers(1, &g_EBO); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBufferData.size() * 4, indexBufferData.data(), GL_STATIC_DRAW);
            
            const GLsizei stride = 6 * sizeof(float);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
            glBindVertexArray(0);

            g_ICount = iCount;
            g_Initialized = true;

            AliceViewer* av = AliceViewer::instance();
            av->m_sceneAABB = AABB();
            V3 tMin = {1e20f,1e20f,1e20f}, tMax = {-1e20f,-1e20f,-1e20f};
            for(size_t i=0; i<transformedVBO.size(); i+=6) {
                V3 p = {transformedVBO[i], transformedVBO[i+1], transformedVBO[i+2]};
                av->m_sceneAABB.expand(p);
                if(p.x<tMin.x)tMin.x=p.x; if(p.y<tMin.y)tMin.y=p.y; if(p.z<tMin.z)tMin.z=p.z;
                if(p.x>tMax.x)tMax.x=p.x; if(p.y>tMax.y)tMax.y=p.y; if(p.z>tMax.z)tMax.z=p.z;
            }
            printf("[StPaulsTest] Translated Bounds: Min{%.2f,%.2f,%.2f} Max{%.2f,%.2f,%.2f}\n", tMin.x, tMin.y, tMin.z, tMax.x, tMax.y, tMax.z);
            av->frameScene();
            av->camera.distance = 500.0f;
            glDisable(GL_CULL_FACE); // Ensure visibility regardless of winding
        } else {
            printf("[ERROR] Failed to open %s\n", binPath);
        }
    }

    static void draw() {
        if (!g_Initialized) setup();

        if (g_VAO && g_ICount > 0) {
            glBindVertexArray(g_VAO);
            glDrawElements(GL_TRIANGLES, g_ICount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }

        AliceViewer* av = AliceViewer::instance();
        g_FrameCount++;

        if (g_FrameCount == 50) {
            int w, h;
            glfwGetFramebufferSize(av->window, &w, &h);
            unsigned char* pixels = (unsigned char*)::Alice::g_Arena.allocate(w * h * 3);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
            stbi_flip_vertically_on_write(true);
            stbi_write_png("framebuffer.png", w, h, 3, pixels, w * 3);
            printf("[StPaulsTest] Frame 50: Screen-res capture saved.\n");
        }

        if (g_FrameCount == 51) {
            printf("[StPaulsTest] Frame 51: Triggering 4K High-Res Capture...\n");
            av->captureHighResStencils("st_pauls_4k");
        }

        if (g_FrameCount == 60) {
            printf("[StPaulsTest] Unit test sequence complete. Visuals ready for inspection.\n");
        }
    }
}
