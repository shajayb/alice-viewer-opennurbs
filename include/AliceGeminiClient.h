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
        glDrawElements(GL_TRIANGLES, g_Sphere.vertexCount, GL_UNSIGNED_INT, 0);
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

        char apiKeyUI[128] = "AIzaSyBfcxqzvArHxpzAfeTJ8S0lmJPrg9W0qKE";

        // Metrics
        std::atomic<double> phase1LatencyMs{0.0};
        std::atomic<double> phase2LatencyMs{0.0};
        std::atomic<double> phase3LatencyMs{0.0};
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
                    
                    // Phase 2: Downsample & Encode
                    auto p2Start = std::chrono::high_resolution_clock::now();
                    
                    Alice::g_Arena.reset();
                    int w = g_State.currentW;
                    int h = g_State.currentH;
                    stbi_flip_vertically_on_write(1);

                    size_t beautyStart = Alice::g_Arena.offset;
                    stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 4, g_State.beautyData, w * 4);
                    size_t beautySize = Alice::g_Arena.offset - beautyStart;

                    size_t segStart = Alice::g_Arena.offset;
                    stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 3, g_State.segData, w * 3);
                    size_t segSize = Alice::g_Arena.offset - segStart;

                    uint8_t* depth8 = (uint8_t*)Alice::g_Arena.allocate(w * h);
                    size_t depthPngStart = Alice::g_Arena.offset; 
                    if (depth8) {
                        float n = 0.1f, f = 1000.0f;
                        for (int i = 0; i < w * h; ++i) {
                            float d = g_State.depthData[i];
                            if (!std::isfinite(d)) d = 1.0f;
                            if (d >= 0.9999f) depth8[i] = 255; 
                            else {
                                float ndc = d * 2.0f - 1.0f;
                                float linearDepth = (2.0f * n * f) / (f + n - ndc * (f - n));
                                depth8[i] = (uint8_t)std::clamp((linearDepth / 100.0f) * 255.0f, 0.0f, 255.0f);
                            }
                        }
                        stbi_write_png_to_func(stbiWriteToArena, &Alice::g_Arena, w, h, 1, depth8, w);
                    }
                    size_t depthPngSize = Alice::g_Arena.offset - depthPngStart;

                    size_t b64BeautyLen = ((beautySize + 2) / 3) * 4;
                    char* b64BeautyStr = (char*)Alice::g_Arena.allocate(b64BeautyLen + 1);
                    size_t b64SegLen = ((segSize + 2) / 3) * 4;
                    char* b64SegStr = (char*)Alice::g_Arena.allocate(b64SegLen + 1);
                    size_t b64DepthLen = ((depthPngSize + 2) / 3) * 4;
                    char* b64DepthStr = (char*)Alice::g_Arena.allocate(b64DepthLen + 1);

                    if (b64BeautyStr && b64SegStr && b64DepthStr) {
                        base64_encode(Alice::g_Arena.memory + beautyStart, beautySize, b64BeautyStr);
                        base64_encode(Alice::g_Arena.memory + segStart, segSize, b64SegStr);
                        base64_encode(Alice::g_Arena.memory + depthPngStart, depthPngSize, b64DepthStr);
                    }

                    auto p2End = std::chrono::high_resolution_clock::now();
                    g_State.phase2LatencyMs = std::chrono::duration<double, std::milli>(p2End - p2Start).count();
                    std::cout << "[Profiler] Phase 2 (Downsample & Base64): " << std::fixed << std::setprecision(2) << g_State.phase2LatencyMs << " ms" << std::endl;

                    // Phase 3: Network Transfer
                    auto p3Start = std::chrono::high_resolution_clock::now();

                    std::string apiKey = g_State.apiKeyUI;
                    if (apiKey.empty()) {
                        const char* env = std::getenv("GEMINI_API_KEY");
                        if (env) apiKey = env;
                    }

                    std::string prompt;
                    if (g_State.activeTask == 1) prompt = "Analyze these 3 images (Beauty, Seg, Depth). Describe the 3D scene content, geometry layout, and colors. Mention the red sphere specifically.";
                    else if (g_State.activeTask == 2) prompt = "Analyze these CAD buffers. Replace each cube by a different building style and the sphere by a tree, and the ground plane by water, all to be rendered in a Salvador Dali style.";
                    else if (g_State.activeTask == 3) prompt = "Render a photorealistic architectural scene based on these stencils (Beauty/Seg/Depth). Use high-end materials and atmospheric lighting.";

                    std::string payload;
                    std::string url;
                    if (g_State.activeTask == 2) {
                        url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-image:generateContent?key=" + apiKey;
                        payload = "{\"contents\": [{\"parts\": [{\"text\": \"" + prompt + "\"},";
                        payload += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64BeautyStr) + "\"}},";
                        payload += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64SegStr) + "\"}},";
                        payload += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64DepthStr) + "\"}}]} ]";
                        payload += ",\"generationConfig\": {\"responseModalities\": [\"TEXT\", \"IMAGE\"]}";
                        payload += "}";
                    } else {
                        url = (g_State.activeTask == 3) ? "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:generateContent" : "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent";
                        url += "?key=" + apiKey;
                        payload = "{\"contents\": [{\"parts\": [{\"text\": \"" + prompt + "\"},";
                        payload += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64BeautyStr) + "\"}},";
                        payload += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64SegStr) + "\"}},";
                        payload += "{\"inlineData\": {\"mimeType\": \"image/png\", \"data\": \"" + std::string(b64DepthStr) + "\"}}]} ]";
                        if (g_State.activeTask == 3) payload += ",\"generationConfig\": {\"responseModalities\": [\"TEXT\", \"IMAGE\"]}";
                        payload += "}";
                    }

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
                        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
                        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

                        CURLcode res = curl_easy_perform(curl);
                        if (res != CURLE_OK) {
                            std::cout << "[GeminiClient] Curl Error: " << curl_easy_strerror(res) << std::endl;
                        } else {
                            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
                        }

                        curl_slist_free_all(headers);
                        curl_easy_cleanup(curl);
                    }

                    {
                        std::cout << "[GeminiClient] POST Status: " << httpCode << std::endl;
                        if (httpCode == 200) {
                            if (g_State.activeTask == 2) {
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
                                            std::string outPath = std::filesystem::absolute("dali_render.png").string();
                                            std::ofstream outFile(outPath, std::ios::binary);
                                            if (outFile.is_open()) {
                                                outFile.write((const char*)decoded, outLen);
                                                outFile.close();
                                                std::cout << "[GeminiClient] Saved Dali render to: " << outPath << " (" << outLen << " bytes)" << std::endl;
                                            }
                                        }
                                    }
                                }
                            }

                            // Clean Parsing for TEXT chunks
                            std::string cleanText;
                            size_t tPos = 0;
                            while ((tPos = responseBody.find("\"text\": \"", tPos)) != std::string::npos) {
                                tPos += 9;
                                size_t ePos = responseBody.find("\"", tPos);
                                if (ePos != std::string::npos) {
                                    std::string chunk = responseBody.substr(tPos, ePos - tPos);
                                    size_t nPos = 0;
                                    while((nPos = chunk.find("\\n", nPos)) != std::string::npos) { chunk.replace(nPos, 2, "\n"); nPos += 1; }
                                    nPos = 0;
                                    while((nPos = chunk.find("\\\"", nPos)) != std::string::npos) { chunk.replace(nPos, 2, "\""); nPos += 1; }
                                    cleanText += chunk + "\n";
                                    tPos = ePos + 1;
                                } else break;
                            }

                            if (!cleanText.empty()) {
                                std::cout << "[GeminiClient] Success. Parsed Text:" << std::endl;
                                std::cout << cleanText << std::endl;

                                std::ofstream logFile("api_log.txt", std::ios::app);
                                if (logFile.is_open()) {
                                    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                                    logFile << "--- [" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] Task " << g_State.activeTask << " ---\n";
                                    logFile << cleanText << "\n\n";
                                    logFile.close();
                                }
                            }

                            if (g_State.activeTask == 3) {
                                size_t dPos = responseBody.find("\"data\": \"");
                                if (dPos != std::string::npos) {
                                    dPos += 9; size_t ePos = responseBody.find("\"", dPos);
                                    if (ePos != std::string::npos) {
                                        std::string b64 = responseBody.substr(dPos, ePos - dPos);
                                        size_t outLen = 0;
                                        uint8_t* decoded = base64_decode(b64.c_str(), b64.size(), &outLen);
                                        if (decoded) {
                                            std::string outPath = std::filesystem::absolute("ai_render_output.png").string();
                                            std::ofstream outFile(outPath, std::ios::binary);
                                            if (outFile.is_open()) {
                                                outFile.write((const char*)decoded, outLen);
                                                outFile.close();
                                                std::cout << "[GeminiClient] Saved render to: " << outPath << " (" << outLen << " bytes)" << std::endl;
                                            }
                                        }
                                    }
                                }
                            }
                        } else std::cout << "[GeminiClient] Error: " << responseBody << std::endl;
                    }

                    auto p3End = std::chrono::high_resolution_clock::now();
                    g_State.phase3LatencyMs = std::chrono::duration<double, std::milli>(p3End - p3Start).count();
                    std::cout << "[Profiler] Phase 3 (API Transfer): " << std::fixed << std::setprecision(2) << g_State.phase3LatencyMs << " ms" << std::endl;

                    g_State.workerBusy = false;
                    g_State.newResultReady = true;
                } catch (...) { g_State.workerBusy = false; }
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
        
        ImGui::Begin("Gemini API Configuration");
        ImGui::InputText("API Key", g_State.apiKeyUI, sizeof(g_State.apiKeyUI), ImGuiInputTextFlags_Password);
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
        
        ImGui::End();

        AliceViewer* viewer = AliceViewer::instance();
        if (viewer) {
            int w, h; glfwGetFramebufferSize(viewer->window, &w, &h);
            float asp = (h == 0) ? 1.0f : (float)w / h;
            M4 proj = perspective_local(viewer->fov, asp, 0.1f, 1000.0f);
            M4 view = viewer->camera.getViewMatrix();
            M4 mvp = mult_local(proj, view);
            drawSolidFloor(50.0f, {0.8f, 0.8f, 0.8f}, mvp);
            drawSolidCube({-10, 0, 5}, 5.0f, {0.8f, 0.2f, 0.2f}, mvp);
            drawSolidCube({10, 0, 5}, 5.0f, {0.2f, 0.8f, 0.2f}, mvp);
            drawSolidCube({0, 15, 8}, 8.0f, {0.2f, 0.2f, 0.8f}, mvp);
            drawSolidSphere({0, 0, 10}, 4.0f, {0.8f, 0.1f, 0.1f}, mvp);
        }
    }

    void keyPress(unsigned char key, int x, int y) {
        using namespace Alice::Alg::GeminiClient;
        if (key == '1' || key == '2' || key == '3') {
            if (!g_State.workerBusy) {
                auto p1Start = std::chrono::high_resolution_clock::now();
                AliceViewer* viewer = AliceViewer::instance();
                if (viewer) {
                    glBindFramebuffer(GL_FRAMEBUFFER, g_State.fbo);
                    glViewport(0, 0, 3840, 2160);
                    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    
                    M4 proj = perspective_local(viewer->fov, 3840.0f / 2160.0f, 0.1f, 1000.0f);
                    M4 view = viewer->camera.getViewMatrix();
                    M4 mvp = mult_local(proj, view);
                    
                    drawSolidFloor(50.0f, {0.8f, 0.8f, 0.8f}, mvp);
                    drawSolidCube({-10, 0, 5}, 5.0f, {0.8f, 0.2f, 0.2f}, mvp);
                    drawSolidCube({10, 0, 5}, 5.0f, {0.2f, 0.8f, 0.2f}, mvp);
                    drawSolidCube({0, 15, 8}, 8.0f, {0.2f, 0.2f, 0.8f}, mvp);
                    drawSolidSphere({0, 0, 10}, 4.0f, {0.8f, 0.1f, 0.1f}, mvp);
                    glFinish();

                    // --- 4K Extraction ---
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    stbi_flip_vertically_on_write(1);

                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glReadPixels(0, 0, 3840, 2160, GL_RGBA, GL_UNSIGNED_BYTE, g_State.beautyData);
                    stbi_write_png("beauty_4k.png", 3840, 2160, 4, g_State.beautyData, 3840 * 4);

                    glReadBuffer(GL_COLOR_ATTACHMENT1);
                    glReadPixels(0, 0, 3840, 2160, GL_RGB, GL_UNSIGNED_BYTE, g_State.segData);
                    stbi_write_png("seg_4k.png", 3840, 2160, 3, g_State.segData, 3840 * 3);

                    glReadPixels(0, 0, 3840, 2160, GL_DEPTH_COMPONENT, GL_FLOAT, g_State.depthData);
                    // Convert float depth to 8-bit for preview
                    {
                        uint8_t* d8 = (uint8_t*)malloc(3840 * 2160);
                        for(int i=0; i<3840*2160; ++i) d8[i] = (uint8_t)(std::clamp(g_State.depthData[i], 0.0f, 1.0f) * 255.0f);
                        stbi_write_png("depth_4k.png", 3840, 2160, 1, d8, 3840);
                        free(d8);
                    }

                    std::cout << "[GeminiClient] 4K Buffers Saved: beauty_4k.png, seg_4k.png, depth_4k.png" << std::endl;

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
                    glBindFramebuffer(GL_FRAMEBUFFER, g_State.transferFbo);
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glReadPixels(0, 0, 512, 288, GL_RGBA, GL_UNSIGNED_BYTE, g_State.beautyData);
                    stbi_write_png("beauty_512.png", 512, 288, 4, g_State.beautyData, 512 * 4);

                    glReadBuffer(GL_COLOR_ATTACHMENT1);
                    glReadPixels(0, 0, 512, 288, GL_RGB, GL_UNSIGNED_BYTE, g_State.segData);
                    stbi_write_png("seg_512.png", 512, 288, 3, g_State.segData, 512 * 3);

                    glReadPixels(0, 0, 512, 288, GL_DEPTH_COMPONENT, GL_FLOAT, g_State.depthData);
                    {
                        uint8_t* d8 = (uint8_t*)malloc(512 * 288);
                        for(int i=0; i<512*288; ++i) d8[i] = (uint8_t)(std::clamp(g_State.depthData[i], 0.0f, 1.0f) * 255.0f);
                        stbi_write_png("depth_512.png", 512, 288, 1, d8, 512);
                        free(d8);
                    }
                    
                    std::cout << "[GeminiClient] 512p Buffers Saved: beauty_512.png, seg_512.png, depth_512.png" << std::endl;
                    std::cout << "[Paths] " << std::filesystem::absolute("beauty_4k.png") << std::endl;
                    std::cout << "[Paths] " << std::filesystem::absolute("beauty_512.png") << std::endl;

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
