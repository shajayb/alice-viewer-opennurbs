#pragma once

#ifdef HIGH_RES_CAPTURE_TEST

#include "AliceViewer.h"
#include <vector>
#include <cstdio>

namespace HighResCaptureTest {

static GLuint g_CachedVAO = 0;
static GLuint g_CachedVBO = 0;
static GLuint g_CachedEBO = 0;
static GLsizei g_CachedCount = 0;
static int g_FrameCount = 0;

void setup() {
    printf("[HighResCaptureTest] Setup\n");
    printf("[HighResCaptureTest] Loading cached mesh...\n");
    FILE* f = fopen("./build/bin/cesium_mesh_cache.bin", "rb");
    if (!f) {
        printf("[ERROR] Failed to open cesium_mesh_cache.bin\n");
        exit(1);
    }

    uint32_t vCount, iCount;
    fread(&vCount, sizeof(uint32_t), 1, f);
    fread(&iCount, sizeof(uint32_t), 1, f);

    std::vector<float> vertices(vCount);
    std::vector<uint32_t> indices(iCount);

    fread(vertices.data(), sizeof(float), vCount, f);
    fread(indices.data(), sizeof(uint32_t), iCount, f);
    fclose(f);

    g_CachedCount = iCount;

    glGenVertexArrays(1, &g_CachedVAO);
    glBindVertexArray(g_CachedVAO);

    glGenBuffers(1, &g_CachedVBO);
    glBindBuffer(GL_ARRAY_BUFFER, g_CachedVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &g_CachedEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_CachedEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    printf("[HighResCaptureTest] Mesh loaded successfully.\n");
}

void draw() {
    AliceViewer* av = AliceViewer::instance();
    if (!av) return;

    if (g_CachedVAO) {
        glUseProgram(av->shaderProgram);
        glBindVertexArray(g_CachedVAO);
        glDrawElements(GL_TRIANGLES, g_CachedCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    if (g_FrameCount == 10) {
        printf("[HighResCaptureTest] Framing scene...\n");
        av->frameScene();
    }

    if (g_FrameCount == 20) {
        printf("[HighResCaptureTest] Capturing high-res stencils...\n");
        av->captureHighResStencils("prod_4k");
        printf("[HighResCaptureTest] Capture complete. Exiting.\n");
        exit(0);
    }

    g_FrameCount++;
}

void update(float dt) {}

} // namespace HighResCaptureTest

#endif // HIGH_RES_CAPTURE_TEST
