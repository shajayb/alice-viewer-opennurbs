#pragma once

#ifdef ALICEGEMINICLIENT_RUN_TEST

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "secur32.lib")
#endif

#include <curl/curl.h>
#include <imgui.h>

#ifndef STB_IMAGE_WRITE_IMPLEMENTATION_DEFINED
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION_DEFINED
#include <stb_image_write.h>
#endif

#ifndef STB_IMAGE_IMPLEMENTATION_DEFINED
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION_DEFINED
#include <stb_image.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"
#include "AliceMemory.h"

#include <thread>
#include <atomic>
#include <string>
#include <string_view>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <cmath>

#ifndef ALICE_PI
#define ALICE_PI 3.14159265358979323846f
#endif

namespace Alice { extern LinearArena g_Arena; }

namespace Alice::Alg::GeminiClient {

    static V3 g_CubePos[3];
    static V3 g_SpherePos;

    struct Color { uint8_t r, g, b; };
    inline bool colorMatch(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2) {
        return (std::abs((int)r1 - (int)r2) < 4) && (std::abs((int)g1 - (int)g2) < 4) && (std::abs((int)b1 - (int)b2) < 4);
    }

    inline float evaluateStructuralFit(const uint8_t* origDepth, const uint8_t* aiRender, int w, int h) {
        auto get_edges = [&](const uint8_t* img, uint8_t* edges, int ch) {
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    float gx = 0, gy = 0;
                    auto get_v = [&](int ox, int oy) {
                        int idx = ((y + oy) * w + (x + ox)) * ch;
                        if (ch == 1) return (float)img[idx];
                        return (img[idx] * 0.299f + img[idx + 1] * 0.587f + img[idx + 2] * 0.114f);
                    };
                    gx = -1 * get_v(-1, -1) + 1 * get_v(1, -1) +
                         -2 * get_v(-1,  0) + 2 * get_v(1,  0) +
                         -1 * get_v(-1,  1) + 1 * get_v(1,  1);
                    gy = -1 * get_v(-1, -1) - 2 * get_v(0, -1) - 1 * get_v(1, -1) +
                          1 * get_v(-1,  1) + 2 * get_v(0,  1) + 1 * get_v(1,  1);
                    float mag = sqrtf(gx * gx + gy * gy);
                    edges[y * w + x] = (mag > 25.0f) ? 255 : 0;
                }
            }
        };

        uint8_t* edgesOrig = (uint8_t*)Alice::g_Arena.allocate(w * h);
        uint8_t* edgesAI = (uint8_t*)Alice::g_Arena.allocate(w * h);
        if (!edgesOrig || !edgesAI) return 0.0f;

        get_edges(origDepth, edgesOrig, 1);
        get_edges(aiRender, edgesAI, 3);

        stbi_write_png("eval_mask_source.png", w, h, 1, edgesOrig, w);
        stbi_write_png("eval_mask_generated.png", w, h, 1, edgesAI, w);

        int matchRecall = 0, totalOrig = 0;
        int matchPrecision = 0, totalAI = 0;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int i = y * w + x;
                if (edgesOrig[i] == 255) {
                    totalOrig++;
                    bool found = false;
                    for (int dy = -1; dy <= 1 && !found; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = x + dx, ny = y + dy;
                            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                                if (edgesAI[ny * w + nx] == 255) { found = true; break; }
                            }
                        }
                    }
                    if (found) matchRecall++;
                }

                if (edgesAI[i] == 255) {
                    totalAI++;
                    bool found = false;
                    for (int dy = -1; dy <= 1 && !found; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = x + dx, ny = y + dy;
                            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                                if (edgesOrig[ny * w + nx] == 255) { found = true; break; }
                            }
                        }
                    }
                    if (found) matchPrecision++;
                }
            }
        }
        
        float recall = (totalOrig == 0) ? 1.0f : (float)matchRecall / (float)totalOrig;
        float precision = (totalAI == 0) ? 1.0f : (float)matchPrecision / (float)totalAI;
        float f1 = (recall + precision == 0.0f) ? 0.0f : 2.0f * (precision * recall) / (precision + recall);

        std::ofstream log("api_log.txt", std::ios::app);
        if (log.is_open()) {
            log << "[Evaluation] Precision: " << precision << ", Recall: " << recall << ", F1: " << f1 << "\n";
            log.close();
        }

        return f1;
    }


    inline M4 perspective_local(float fov, float asp, float n, float f) {
        float tn = tanf(fov * 0.5f);
        M4 r = { 0 };
        r.m[0] = 1.0f / (asp * tn);
        r.m[5] = 1.0f / tn;
        r.m[10] = -(f + n) / (f - n);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * f * n) / (f - n);
        return r;
    }

    inline M4 mult_local(M4 a, M4 b) {
        M4 r = { 0 };
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                r.m[j * 4 + i] = a.m[0 * 4 + i] * b.m[j * 4 + 0] +
                                 a.m[1 * 4 + i] * b.m[j * 4 + 1] +
                                 a.m[2 * 4 + i] * b.m[j * 4 + 2] +
                                 a.m[3 * 4 + i] * b.m[j * 4 + 3];
            }
        }
        return r;
    }

    inline void base64_encode(const uint8_t* input, size_t input_len, char* output) {
        static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t i = 0, j = 0;
        for (; i + 2 < input_len; i += 3) {
            output[j++] = lookup[input[i] >> 2];
            output[j++] = lookup[((input[i] & 3) << 4) | (input[i + 1] >> 4)];
            output[j++] = lookup[((input[i + 1] & 15) << 2) | (input[i + 2] >> 6)];
            output[j++] = lookup[input[i + 2] & 63];
        }
        if (i < input_len) {
            output[j++] = lookup[input[i] >> 2];
            if (i + 1 == input_len) {
                output[j++] = lookup[(input[i] & 3) << 4];
                output[j++] = '=';
                output[j++] = '=';
            } else {
                output[j++] = lookup[((input[i] & 3) << 4) | (input[i + 1] >> 4)];
                output[j++] = lookup[(input[i + 1] & 15) << 2];
                output[j++] = '=';
            }
        }
        output[j] = '\0';
    }

    inline uint8_t* base64_decode(const char* input, size_t input_len, size_t* out_len) {
        static const int L[] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
        };
        uint8_t* output = (uint8_t*)Alice::g_Arena.allocate((input_len * 3) / 4 + 4);
        if (!output) return nullptr;
        size_t j = 0;
        int v = 0, vb = -8;
        for (size_t i = 0; i < input_len; ++i) {
            uint8_t c = (uint8_t)input[i];
            if (c < 128 && L[c] != -1) {
                v = (v << 6) + L[c];
                vb += 6;
                if (vb >= 0) {
                    output[j++] = (uint8_t)((v >> vb) & 0xFF);
                    vb -= 8;
                }
            }
        }
        *out_len = j;
        return output;
    }

    inline void drawSolidFloor(float s, V3 col, const M4& mvp) {
        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer) return;
        float vData[] = {
            -s, -s, 0.0f, col.x, col.y, col.z,
             s, -s, 0.0f, col.x, col.y, col.z,
             s,  s, 0.0f, col.x, col.y, col.z,
            -s, -s, 0.0f, col.x, col.y, col.z,
             s,  s, 0.0f, col.x, col.y, col.z,
            -s,  s, 0.0f, col.x, col.y, col.z
        };
        GLuint vao, vbo;
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vData), vData, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glUseProgram(viewer->shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(viewer->shaderProgram, "mvp"), 1, 0, mvp.m);
        glUniform3f(glGetUniformLocation(viewer->shaderProgram, "color_override"), col.x, col.y, col.z);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glUniform3f(glGetUniformLocation(viewer->shaderProgram, "color_override"), -1, -1, -1);
        glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
    }

    inline void drawSolidCube(V3 c, float s, V3 col, const M4& mvp) {
        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer) return;
        float h = s * 0.5f;
        float vData[] = {
            -h,-h, h, col.x,col.y,col.z,  h,-h, h, col.x,col.y,col.z,  h, h, h, col.x,col.y,col.z,
            -h,-h, h, col.x,col.y,col.z,  h, h, h, col.x,col.y,col.z, -h, h, h, col.x,col.y,col.z,
            -h,-h,-h, col.x,col.y,col.z, -h, h,-h, col.x,col.y,col.z,  h, h,-h, col.x,col.y,col.z,
            -h,-h,-h, col.x,col.y,col.z,  h, h,-h, col.x,col.y,col.z,  h,-h,-h, col.x,col.y,col.z,
            -h,-h,-h, col.x,col.y,col.z, -h,-h, h, col.x,col.y,col.z, -h, h, h, col.x,col.y,col.z,
            -h,-h,-h, col.x,col.y,col.z, -h, h, h, col.x,col.y,col.z, -h, h,-h, col.x,col.y,col.z,
             h,-h,-h, col.x,col.y,col.z,  h, h,-h, col.x,col.y,col.z,  h, h, h, col.x,col.y,col.z,
             h,-h,-h, col.x,col.y,col.z,  h, h, h, col.x,col.y,col.z,  h,-h, h, col.x,col.y,col.z,
            -h, h,-h, col.x,col.y,col.z, -h, h, h, col.x,col.y,col.z,  h, h, h, col.x,col.y,col.z,
            -h, h,-h, col.x,col.y,col.z,  h, h, h, col.x,col.y,col.z,  h, h,-h, col.x,col.y,col.z,
            -h,-h,-h, col.x,col.y,col.z,  h,-h,-h, col.x,col.y,col.z,  h,-h, h, col.x,col.y,col.z,
            -h,-h,-h, col.x,col.y,col.z,  h,-h, h, col.x,col.y,col.z, -h,-h, h, col.x,col.y,col.z
        };
        for (int i = 0; i < 36; ++i) {
            vData[i*6 + 0] += c.x;
            vData[i*6 + 1] += c.y;
            vData[i*6 + 2] += c.z;
        }
        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vData), vData, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glUseProgram(viewer->shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(viewer->shaderProgram, "mvp"), 1, 0, mvp.m);
        glUniform3f(glGetUniformLocation(viewer->shaderProgram, "color_override"), col.x, col.y, col.z);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glUniform3f(glGetUniformLocation(viewer->shaderProgram, "color_override"), -1, -1, -1);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }

    struct SphereData {
        GLuint vao = 0;
        GLuint vbo = 0;
        int vertexCount = 0;
    };

    inline SphereData g_Sphere;

    inline void initSphere(int latLines = 20, int longLines = 20) {
        std::vector<float> v;
        for (int i = 0; i <= latLines; ++i) {
            float lat = ALICE_PI * i / latLines;
            for (int j = 0; j <= longLines; ++j) {
                float lon = 2.0f * ALICE_PI * j / longLines;
                float x = cosf(lon) * sinf(lat);
                float y = sinf(lon) * sinf(lat);
                float z = cosf(lat);
                v.push_back(x); v.push_back(y); v.push_back(z);
            }
        }
        std::vector<GLuint> indices;
        for (int i = 0; i < latLines; ++i) {
            for (int j = 0; j < longLines; ++j) {
                int first = i * (longLines + 1) + j;
                int second = first + longLines + 1;
                indices.push_back(first); indices.push_back(second); indices.push_back(first + 1);
                indices.push_back(second); indices.push_back(second + 1); indices.push_back(first + 1);
            }
        }
        glGenVertexArrays(1, &g_Sphere.vao);
        glGenBuffers(1, &g_Sphere.vbo);
        GLuint ebo; glGenBuffers(1, &ebo);
        glBindVertexArray(g_Sphere.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_Sphere.vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
        g_Sphere.vertexCount = (int)indices.size();
    }

    inline void drawSolidSphere(V3 c, float r, V3 col, const M4& mvp) {
        AliceViewer* viewer = AliceViewer::instance();
        if (!viewer || g_Sphere.vao == 0) return;
        glUseProgram(viewer->shaderProgram);
        M4 model = { 0 };
        model.m[0] = r; model.m[5] = r; model.m[10] = r; model.m[15] = 1.0f;
        model.m[12] = c.x; model.m[13] = c.y; model.m[14] = c.z;
        M4 finalMvp = mult_local(mvp, model);
        glUniformMatrix4fv(glGetUniformLocation(viewer->shaderProgram, "mvp"), 1, 0, finalMvp.m);
        glUniform3f(glGetUniformLocation(viewer->shaderProgram, "color_override"), col.x, col.y, col.z);
        glBindVertexArray(g_Sphere.vao);
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, col.x, col.y, col.z);
        glDrawElements(GL_TRIANGLES, g_Sphere.vertexCount, GL_UNSIGNED_INT, 0);
        glEnableVertexAttribArray(1);
        glUniform3f(glGetUniformLocation(viewer->shaderProgram, "color_override"), -1, -1, -1);
    }

    struct State {
        GLuint fbo = 0;
        GLuint texBeauty = 0;
        GLuint texDepth = 0;
        GLuint texSeg = 0;

        GLuint transferFbo = 0;
        GLuint texBeautySmall = 0;
        GLuint texSegSmall = 0;
        GLuint texDepthSmall = 0;

        std::atomic<bool> captureRequested{false};
        std::atomic<bool> workerBusy{false};
        std::atomic<bool> newResultReady{false};

        uint8_t* beautyData = nullptr;
        float* depthData = nullptr;
        uint8_t* segData = nullptr;

        std::thread workerThread;

        int activeTask = 0;
        int currentW = 3840;
        int currentH = 2160;
        int frameCounter = 0;

        char apiKeyBuffer[256] = "";
        std::atomic<bool> authError{false};

        // Metrics
        std::atomic<double> phase1LatencyMs{0.0};
        std::atomic<double> phase2LatencyMs{0.0};
        std::atomic<double> phase3LatencyMs{0.0};
        std::atomic<float> structuralFitScore{0.0f};
    };

    inline State g_State;

    inline void stbiWriteToArena(void *context, void *data, int size) {
        Alice::LinearArena* arena = (Alice::LinearArena*)context;
        if (arena->offset + size > arena->capacity || arena->memory == nullptr) return; 
        void* dest = arena->allocate(size);
        if (dest) memcpy(dest, data, size);
    }

    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    inline void workerThreadFunc() {
        curl_global_init(CURL_GLOBAL_ALL);
        while (true) {
            if (g_State.captureRequested.exchange(false)) {
                try {
                    g_State.workerBusy = true;
                    auto p2Start = std::chrono::high_resolution_clock::now();
                    
                    Alice::g_Arena.reset();
                    int w = g_State.currentW;
                    int h = g_State.currentH;
                    stbi_flip_vertically_on_write(1);

                    // Pre-calculate inverted depth (Near=255, Far=50, BG=0)
                    uint8_t* fullDepth8 = (uint8_t*)Alice::g_Arena.allocate(w * h);
                    if (fullDepth8) {
                        float n = 0.1f, f = 1000.0f;
                        for (int i = 0; i < w * h; ++i) {
                            float d = g_State.depthData[i];
                            if (!std::isfinite(d)) d = 1.0f;
                            if (d >= 0.999f) fullDepth8[i] = 0;
                            else {
                                float ndc = d * 2.0f - 1.0f;
                                float linearDepth = (2.0f * n * f) / (f + n - ndc * (f - n));
                                float d_norm = std::clamp((linearDepth - n) / (40.0f - n), 0.0f, 1.0f);
                                fullDepth8[i] = (uint8_t)(50.0f + (1.0f - d_norm) * 205.0f);
                            }
                        }
                    }

                    std::string payloadParts;
                    if (g_State.activeTask == 2) {
                        // Strategy A: Single Master Depth Map
                        size_t dStart = Alice::g_Arena.offset;
                        stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 1, fullDepth8, w);
                        size_t dSize = Alice::g_Arena.offset - dStart;
                        char* b64D = (char*)Alice::g_Arena.allocate(((dSize + 2) / 3) * 4 + 1);
                        if (b64D) {
                            base64_encode(Alice::g_Arena.memory + dStart, dSize, b64D);
                            payloadParts += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64D) + "\"}}";
                        }
                    } else {
                        // Task 1 or 3: Beauty, Seg, Depth
                        size_t bStart = Alice::g_Arena.offset;
                        stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 4, g_State.beautyData, w * 4);
                        size_t bSize = Alice::g_Arena.offset - bStart;
                        char* b64B = (char*)Alice::g_Arena.allocate(((bSize + 2) / 3) * 4 + 1);
                        base64_encode(Alice::g_Arena.memory + bStart, bSize, b64B);
                        payloadParts += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64B) + "\"}},";

                        size_t sStart = Alice::g_Arena.offset;
                        stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 3, g_State.segData, w * 3);
                        size_t sSize = Alice::g_Arena.offset - sStart;
                        char* b64S = (char*)Alice::g_Arena.allocate(((sSize + 2) / 3) * 4 + 1);
                        base64_encode(Alice::g_Arena.memory + sStart, sSize, b64S);
                        payloadParts += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64S) + "\"}},";

                        size_t dStart = Alice::g_Arena.offset;
                        stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 1, fullDepth8, w);
                        size_t dSize = Alice::g_Arena.offset - dStart;
                        char* b64D = (char*)Alice::g_Arena.allocate(((dSize + 2) / 3) * 4 + 1);
                        base64_encode(Alice::g_Arena.memory + dStart, dSize, b64D);
                        payloadParts += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64D) + "\"}},";
                    }

                    auto p2End = std::chrono::high_resolution_clock::now();
                    g_State.phase2LatencyMs = std::chrono::duration<double, std::milli>(p2End - p2Start).count();
                    
                    // Phase 3: Network
                    auto p3Start = std::chrono::high_resolution_clock::now();
                    std::string apiKey = std::getenv("GEMINI_API_KEY") ? std::getenv("GEMINI_API_KEY") : g_State.apiKeyBuffer;
                    std::string prompt;
                    if (g_State.activeTask == 1) prompt = "Analyze these 3 images (Beauty, Seg, Depth). Describe the 3D scene content.";
                    else if (g_State.activeTask == 2) prompt = "I am providing 5 structural stencils. IMAGE 1: The ground plane (water). IMAGE 2: A cube at mid-left (Gothic cathedral). IMAGE 3: A cube at mid-right (Brutalist concrete). IMAGE 4: A large cube at the back-center (futuristic glass). IMAGE 5: A sphere in foreground-center (twisting tree). Render a SINGLE Salvador Dali masterpiece where each object PERFECTLY match its corresponding stencil. MANDATORY: Treat the provided images as absolute physical constraints. Do not draw lines where there are no lines in the provided masks. SILHOUETTE PRESERVATION IS MANDATORY. Do not add structural elements outside the masks.";
                    else if (g_State.activeTask == 3) prompt = "Render photorealistic architecture based on these stencils.";

                    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/";
                    if (g_State.activeTask == 2) url += "gemini-2.5-flash-image";
                    else if (g_State.activeTask == 3) url += "gemini-2.0-flash-exp";
                    else url += "gemini-flash-latest";
                    url += ":generateContent?key=" + apiKey;

                    std::string payload = "{\"contents\": [{\"parts\": [{\"text\": \"" + prompt + "\"}," + payloadParts;
                    if (payload.back() == ',') payload.pop_back();
                    payload += "]}]";
                    payload += ",\"generationConfig\": {";
                    if (g_State.activeTask >= 2) payload += "\"responseModalities\": [\"TEXT\", \"IMAGE\"],";
                    payload += "\"aspectRatio\": \"16:9\"";
                    payload += "}}";

                    CURL* curl = curl_easy_init();
                    std::string responseBody;
                    long httpCode = 0;
                    if (curl) {
                        struct curl_slist* headers = NULL;
                        headers = curl_slist_append(headers, "Content-Type: application/json");
                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
                        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
                        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); 
                        curl_easy_perform(curl);
                        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
                        curl_easy_cleanup(curl);
                    }

                    if (httpCode != 200) {
                        std::cout << "[GeminiClient] API ERROR: " << httpCode << std::endl;
                        std::cout << "[GeminiClient] Response: " << responseBody << std::endl;
                        if (httpCode == 400 || httpCode == 401 || httpCode == 403 || httpCode == 429) {
                            g_State.authError = true;
                        }
                    } else {
                        g_State.authError = false;
                        size_t dPos = responseBody.find("\"bytesBase64Encoded\": \"");
                        if (dPos == std::string::npos) dPos = responseBody.find("\"inlineData\": {\"data\": \"");
                        if (dPos == std::string::npos) dPos = responseBody.find("\"data\": \"");
                        if (dPos != std::string::npos) {
                            if (responseBody[dPos+1] == 'b') dPos += 23; 
                            else if (responseBody[dPos+1] == 'i') dPos += 24;
                            else dPos += 9;
                            size_t ePos = responseBody.find("\"", dPos);
                            if (ePos != std::string::npos) {
                                std::string b64 = responseBody.substr(dPos, ePos - dPos);
                                size_t outLen = 0;
                                uint8_t* decoded = base64_decode(b64.c_str(), b64.size(), &outLen);
                                if (decoded) {
                                    std::string outPath = (g_State.activeTask == 2) ? "dali_render.png" : "ai_render_output.png";
                                    std::ofstream outFile(outPath, std::ios::binary);
                                    if (outFile.is_open()) {
                                        outFile.write((const char*)decoded, outLen);
                                        outFile.close();
                                    }
                                    if (g_State.activeTask == 2) {
                                        int aiW, aiH, aiC;
                                        uint8_t* aiPixelsRaw = stbi_load_from_memory(decoded, (int)outLen, &aiW, &aiH, &aiC, 3);
                                        if (aiPixelsRaw) {
                                            // Output Resolution Parity: Downsample to 512x288
                                            uint8_t* aiPixelsScaled = (uint8_t*)Alice::g_Arena.allocate(512 * 288 * 3);
                                            if (aiPixelsScaled) {
                                                for (int y = 0; y < 288; ++y) {
                                                    for (int x = 0; x < 512; ++x) {
                                                        int srcX = (x * aiW) / 512;
                                                        int srcY = (y * aiH) / 288;
                                                        int srcIdx = (srcY * aiW + srcX) * 3;
                                                        int dstIdx = (y * 512 + x) * 3;
                                                        aiPixelsScaled[dstIdx + 0] = aiPixelsRaw[srcIdx + 0];
                                                        aiPixelsScaled[dstIdx + 1] = aiPixelsRaw[srcIdx + 1];
                                                        aiPixelsScaled[dstIdx + 2] = aiPixelsRaw[srcIdx + 2];
                                                    }
                                                }
                                                float score = evaluateStructuralFit(fullDepth8, aiPixelsScaled, 512, 288);
                                                g_State.structuralFitScore = score;
                                                std::cout << "[GeminiClient] Structural Fit Score: " << std::fixed << std::setprecision(3) << score << std::endl;
                                            }
                                            stbi_image_free(aiPixelsRaw);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    auto p3End = std::chrono::high_resolution_clock::now();
                    g_State.phase3LatencyMs = std::chrono::duration<double, std::milli>(p3End - p3Start).count();
                    g_State.workerBusy = false;
                    g_State.newResultReady = true;
                } catch (const std::exception& e) { 
                    std::cout << "[GeminiClient] Exception: " << e.what() << std::endl;
                    g_State.workerBusy = false; 
                } catch (...) { 
                    std::cout << "[GeminiClient] Unknown Exception" << std::endl;
                    g_State.workerBusy = false; 
                }
            } else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        curl_global_cleanup();
    }
}

extern "C" {
    void setup() {
        using namespace Alice::Alg::GeminiClient;

        if (Alice::g_Arena.capacity < 1024 * 1024 * 256) {
            if (Alice::g_Arena.memory) free(Alice::g_Arena.memory);
            Alice::g_Arena.init(1024 * 1024 * 256); 
        }

        // Procedural Scene Randomization
        srand((unsigned int)time(NULL));
        for (int i = 0; i < 3; ++i) {
            g_CubePos[i] = { (float)(rand() % 40 - 20), (float)(rand() % 40 - 20), (float)(rand() % 10 + 2) };
        }
        g_SpherePos = { (float)(rand() % 20 - 10), (float)(rand() % 20 - 10), (float)(rand() % 10 + 5) };

        // Load API Key
        std::ifstream keyFile("API_KEYS.txt");
        if (keyFile.is_open()) {
            keyFile.getline(g_State.apiKeyBuffer, sizeof(g_State.apiKeyBuffer));
            keyFile.close();
        }

        glGenFramebuffers(1, &g_State.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_State.fbo);

        glGenTextures(1, &g_State.texBeauty);
        glBindTexture(GL_TEXTURE_2D, g_State.texBeauty);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 3840, 2160, 0, GL_RGBA, GL_FLOAT, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_State.texBeauty, 0);

        glGenTextures(1, &g_State.texSeg);
        glBindTexture(GL_TEXTURE_2D, g_State.texSeg);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 3840, 2160, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, g_State.texSeg, 0);

        glGenTextures(1, &g_State.texDepth);
        glBindTexture(GL_TEXTURE_2D, g_State.texDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 3840, 2160, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_State.texDepth, 0);

        GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, drawBuffers);

        glGenFramebuffers(1, &g_State.transferFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_State.transferFbo);

        glGenTextures(1, &g_State.texBeautySmall);
        glBindTexture(GL_TEXTURE_2D, g_State.texBeautySmall);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 288, 0, GL_RGBA, GL_FLOAT, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_State.texBeautySmall, 0);

        glGenTextures(1, &g_State.texSegSmall);
        glBindTexture(GL_TEXTURE_2D, g_State.texSegSmall);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 512, 288, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, g_State.texSegSmall, 0);

        glGenTextures(1, &g_State.texDepthSmall);
        glBindTexture(GL_TEXTURE_2D, g_State.texDepthSmall);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 512, 288, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_State.texDepthSmall, 0);

        glDrawBuffers(2, drawBuffers);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        g_State.beautyData = (uint8_t*)calloc(1, 3840 * 2160 * 4);
        g_State.segData = (uint8_t*)calloc(1, 3840 * 2160 * 3);
        g_State.depthData = (float*)calloc(1, 3840 * 2160 * sizeof(float));

        initSphere();

        g_State.workerThread = std::thread(workerThreadFunc);
        g_State.workerThread.detach();

        g_State.activeTask = 1; 
        std::cout << "[GeminiClient] Initialized 4K G-Buffer Pipeline." << std::endl;
    }

    void update(float dt) {}

    void draw() {
        using namespace Alice::Alg::GeminiClient;

        g_State.frameCounter++;
        if (g_State.frameCounter == 100) {
            std::cout << "[GeminiClient] Auto-triggering Task 2 at frame 100" << std::endl;
            keyPress('2', 0, 0);
        }

        ImGui::Begin("Gemini API Configuration");
        ImGui::InputText("API Key", g_State.apiKeyBuffer, sizeof(g_State.apiKeyBuffer), ImGuiInputTextFlags_Password);
        
        if (g_State.authError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            ImGui::TextWrapped("[ERROR] Invalid or Expired Key. Please update API_KEYS.txt or enter a valid key below.");
            ImGui::PopStyleColor();
        }

        ImGui::Text("Tasks: 1:Analysis, 2:Dali, 3:Render");
        if (ImGui::Button("Dispatch Analysis (1)")) keyPress('1', 0, 0);
        ImGui::SameLine();
        if (ImGui::Button("Dispatch Dali (2)")) keyPress('2', 0, 0);
        ImGui::SameLine();
        if (ImGui::Button("Dispatch Render (3)")) keyPress('3', 0, 0);
        
        if (g_State.workerBusy) ImGui::Text("Status: DISPATCHING...");
        else ImGui::Text("Status: IDLE");

        ImGui::Text("P1 (Capture): %.2f ms", (double)g_State.phase1LatencyMs);
        ImGui::Text("P2 (Encode): %.2f ms", (double)g_State.phase2LatencyMs);
        ImGui::Text("P3 (Network): %.2f ms", (double)g_State.phase3LatencyMs);
        ImGui::Text("Structural Fit Score: %.3f", (float)g_State.structuralFitScore);
        
        ImGui::End();

        AliceViewer* viewer = AliceViewer::instance();
        if (viewer) {
            int w, h; glfwGetFramebufferSize(viewer->window, &w, &h);
            float asp = (h == 0) ? 1.0f : (float)w / h;
            M4 proj = perspective_local(viewer->fov, asp, 0.1f, 1000.0f);
            M4 view = viewer->camera.getViewMatrix();
            M4 mvp = mult_local(proj, view);
            drawSolidFloor(50.0f, {0.8f, 0.8f, 0.8f}, mvp);
            drawSolidCube(g_CubePos[0], 5.0f, {0.8f, 0.2f, 0.2f}, mvp);
            drawSolidCube(g_CubePos[1], 5.0f, {0.2f, 0.8f, 0.2f}, mvp);
            drawSolidCube(g_CubePos[2], 8.0f, {0.2f, 0.2f, 0.8f}, mvp);
            drawSolidSphere(g_SpherePos, 4.0f, {0.8f, 0.1f, 0.1f}, mvp);
        }
    }


    void keyPress(unsigned char key, int x, int y) {
        using namespace Alice::Alg::GeminiClient;
        if (key == '1' || key == '2' || key == '3') {
            if (!g_State.workerBusy) {
                auto p1Start = std::chrono::high_resolution_clock::now();
                AliceViewer* viewer = AliceViewer::instance();
                if (viewer) {
                    Alice::g_Arena.reset();

                    glBindFramebuffer(GL_FRAMEBUFFER, g_State.fbo);
                    glViewport(0, 0, 3840, 2160);
                    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    
                    M4 proj = perspective_local(viewer->fov, 3840.0f / 2160.0f, 0.1f, 1000.0f);
                    M4 view = viewer->camera.getViewMatrix();
                    M4 mvp = mult_local(proj, view);
                    
                    drawSolidFloor(50.0f, {0.8f, 0.8f, 0.8f}, mvp);
                    drawSolidCube(g_CubePos[0], 5.0f, {0.8f, 0.2f, 0.2f}, mvp);
                    drawSolidCube(g_CubePos[1], 5.0f, {0.2f, 0.8f, 0.2f}, mvp);
                    drawSolidCube(g_CubePos[2], 8.0f, {0.2f, 0.2f, 0.8f}, mvp);
                    drawSolidSphere(g_SpherePos, 4.0f, {0.8f, 0.1f, 0.1f}, mvp);
                    glFinish();

                    // --- 512p Downsample ---
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_State.fbo);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_State.transferFbo);
                    glReadBuffer(GL_COLOR_ATTACHMENT0); glDrawBuffer(GL_COLOR_ATTACHMENT0);
                    glBlitFramebuffer(0, 0, 3840, 2160, 0, 0, 512, 288, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                    glReadBuffer(GL_COLOR_ATTACHMENT1); glDrawBuffer(GL_COLOR_ATTACHMENT1);
                    glBlitFramebuffer(0, 0, 3840, 2160, 0, 0, 512, 288, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    glBlitFramebuffer(0, 0, 3840, 2160, 0, 0, 512, 288, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
                    glFinish();

                    // --- 512p Extraction ---
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    stbi_flip_vertically_on_write(1);
                    glBindFramebuffer(GL_FRAMEBUFFER, g_State.transferFbo);
                    
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glReadPixels(0, 0, 512, 288, GL_RGBA, GL_UNSIGNED_BYTE, g_State.beautyData);
                    stbi_write_png("beauty_512.png", 512, 288, 4, g_State.beautyData, 512 * 4);

                    glReadBuffer(GL_COLOR_ATTACHMENT1);
                    glReadPixels(0, 0, 512, 288, GL_RGB, GL_UNSIGNED_BYTE, g_State.segData);
                    stbi_write_png("seg_512.png", 512, 288, 3, g_State.segData, 512 * 3);

                    // Multi-class Segmentation Stencils
                    uint8_t* rMask = (uint8_t*)Alice::g_Arena.allocate(512 * 288);
                    uint8_t* gMask = (uint8_t*)Alice::g_Arena.allocate(512 * 288);
                    uint8_t* bMask = (uint8_t*)Alice::g_Arena.allocate(512 * 288);
                    if (rMask && gMask && bMask) {
                        for (int i = 0; i < 512 * 288; ++i) {
                            uint8_t r = g_State.segData[i * 3 + 0];
                            uint8_t g = g_State.segData[i * 3 + 1];
                            uint8_t b = g_State.segData[i * 3 + 2];
                            rMask[i] = (r > 200 && g < 100 && b < 100) ? 255 : 0;
                            gMask[i] = (g > 200 && r < 100 && b < 100) ? 255 : 0;
                            bMask[i] = (b > 200 && r < 100 && g < 100) ? 255 : 0;
                        }
                        stbi_write_png("stencil_red.png", 512, 288, 1, rMask, 512);
                        stbi_write_png("stencil_green.png", 512, 288, 1, gMask, 512);
                        stbi_write_png("stencil_blue.png", 512, 288, 1, bMask, 512);
                    }

                    glReadPixels(0, 0, 512, 288, GL_DEPTH_COMPONENT, GL_FLOAT, g_State.depthData);
                    {
                        uint8_t* d8 = (uint8_t*)Alice::g_Arena.allocate(512 * 288);
                        if (d8) {
                            float n = 0.1f, f = 1000.0f;
                            for(int i=0; i<512*288; ++i) {
                                float d = g_State.depthData[i];
                                if (!std::isfinite(d)) d = 1.0f;
                                if (d >= 0.999f) d8[i] = 0;
                                else {
                                    float ndc = d * 2.0f - 1.0f;
                                    float linearDepth = (2.0f * n * f) / (f + n - ndc * (f - n));
                                    float d_norm = std::clamp((linearDepth - n) / (40.0f - n), 0.0f, 1.0f);
                                    d8[i] = (uint8_t)(50.0f + (1.0f - d_norm) * 205.0f);
                                }
                            }
                            stbi_write_png("depth_512.png", 512, 288, 1, d8, 512);
                        }
                    }
                    
                    std::cout << "[GeminiClient] 512p Buffers Saved: beauty_512.png, seg_512.png, depth_512.png, stencil_red.png, etc." << std::endl;

                    g_State.activeTask = (key == '1') ? 1 : ((key == '2') ? 2 : 3);
                    g_State.currentW = 512;
                    g_State.currentH = 288;

                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    auto p1End = std::chrono::high_resolution_clock::now();
                    g_State.phase1LatencyMs = std::chrono::duration<double, std::milli>(p1End - p1Start).count();
                    g_State.captureRequested = true;
                }
            }
        }
    }



    void mousePress(int button, int state, int x, int y) {}
    void mouseMotion(int x, int y) {}
}
#endif
