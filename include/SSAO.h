#ifndef SSAO_H
#define SSAO_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <cstdlib>

#include "AliceMemory.h"
#include "PhongShader.h"

namespace Alice { extern LinearArena g_Arena; }

struct SSAO
{
    struct FBO
    {
        unsigned int fbo = 0;
        unsigned int textures[4] = { 0 }; 
        unsigned int rbo = 0;
        int w = 0, h = 0;

        void release()
        {
            if (fbo) glDeleteFramebuffers(1, &fbo);
            if (rbo) glDeleteRenderbuffers(1, &rbo);
            for (int i = 0; i < 4; ++i) if (textures[i]) glDeleteTextures(1, &textures[i]);
            fbo = rbo = 0;
        }

        void init(int width, int height, int attachmentCount, bool depth = true)
        {
            release();
            w = width; h = height;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);

            unsigned int drawBuffers[4];
            for (int i = 0; i < attachmentCount; ++i)
            {
                glGenTextures(1, &textures[i]);
                glBindTexture(GL_TEXTURE_2D, textures[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, textures[i], 0);
                drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
            }
            glDrawBuffers(attachmentCount, drawBuffers);

            if (depth)
            {
                glGenRenderbuffers(1, &rbo);
                glBindRenderbuffer(GL_RENDERBUFFER, rbo);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
            }

            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void bind() { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h); }
        void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    };

    struct GShader
    {
        unsigned int program;
        int uMVP, uMV, uColor;

        static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aInstanceData;
out vec3 vPos;
out vec3 vNorm;
uniform mat4 uMVP;
uniform mat4 uMV;
void main(){
    vec3 worldPos = (aPos * aInstanceData.w) + aInstanceData.xyz;
    vec4 viewPos = uMV * vec4(worldPos, 1.0);
    vPos = viewPos.xyz;
    vNorm = normalize(mat3(uMV) * aNormal);
    gl_Position = uMVP * vec4(worldPos, 1.0);
})";

        static constexpr const char* fsrc = R"(#version 400 core
layout(location=0) out vec4 gPos;
layout(location=1) out vec4 gNorm;
layout(location=2) out vec4 gAlbedo;
in vec3 vPos;
in vec3 vNorm;
uniform vec3 uColor;
void main(){
    gPos = vec4(vPos, 1.0);
    gNorm = vec4(normalize(vNorm), 1.0);
    gAlbedo = vec4(uColor, 1.0);
})";

        void init()
        {
            unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vs, 1, &vsrc, NULL);
            glCompileShader(vs);
            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            uMVP = glGetUniformLocation(program, "uMVP");
            uMV = glGetUniformLocation(program, "uMV");
            uColor = glGetUniformLocation(program, "uColor");
        }
    } gs;

    struct SShader
    {
        unsigned int program;
        unsigned int noiseTex;
        int uP, uRadius, uBias, uRes, uNoiseScale, uSamples;
        float kernel[128 * 3];

        static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
void main(){ gl_Position = vec4(aPos, 1.0); })";

        static constexpr const char* fsrc = R"(#version 400 core
out float FragColor;
uniform sampler2D gPos;
uniform sampler2D gNorm;
uniform sampler2D uNoise;
uniform vec3 uKernel[128];
uniform mat4 uP;
uniform float uRadius;
uniform float uBias;
uniform vec2 uRes;
uniform vec2 uNoiseScale;
uniform int uSamples;

void main(){
    vec2 uv = gl_FragCoord.xy / uRes;
    vec3 pos = texture(gPos, uv).xyz;
    vec3 norm = texture(gNorm, uv).xyz;
    float validNorm = step(0.1, length(norm));
    
    vec3 randomVec = texture(uNoise, uv * uNoiseScale).xyz;
    vec3 tangent = normalize(randomVec - norm * dot(randomVec, norm));
    vec3 bitangent = cross(norm, tangent);
    mat3 TBN = mat3(tangent, bitangent, norm);

    float occlusion = 0.0;
    for(int i = 0; i < uSamples; ++i){
        vec3 samplePos = TBN * uKernel[i];
        samplePos = pos + samplePos * uRadius;

        vec4 offset = vec4(samplePos, 1.0);
        offset = uP * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sampleDepth = texture(gPos, offset.xy).z;
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / abs(pos.z - sampleDepth));
        occlusion += step(samplePos.z + uBias, sampleDepth) * rangeCheck;
    }
    float ao = 1.0 - (occlusion / float(uSamples));
    FragColor = mix(1.0, ao, validNorm);
})";

        void init()
        {
            for (int i = 0; i < 128; ++i)
            {
                float z = (float)i / 128.0f;
                float r = sqrtf(1.0f - z * z);
                float theta = (float)i * 2.39996f;
                float scale = (float)i / 128.0f;
                scale = 0.1f + 0.9f * scale * scale;
                kernel[i * 3 + 0] = cosf(theta) * r * scale;
                kernel[i * 3 + 1] = sinf(theta) * r * scale;
                kernel[i * 3 + 2] = z * scale;
            }

            float noiseData[16 * 3];
            for (int i = 0; i < 16; ++i)
            {
                noiseData[i * 3 + 0] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                noiseData[i * 3 + 1] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
                noiseData[i * 3 + 2] = 0.0f;
            }
            glGenTextures(1, &noiseTex);
            glBindTexture(GL_TEXTURE_2D, noiseTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noiseData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vs, 1, &vsrc, NULL);
            glCompileShader(vs);
            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            uP = glGetUniformLocation(program, "uP");
            uRadius = glGetUniformLocation(program, "uRadius");
            uBias = glGetUniformLocation(program, "uBias");
            uRes = glGetUniformLocation(program, "uRes");
            uNoiseScale = glGetUniformLocation(program, "uNoiseScale");
            uSamples = glGetUniformLocation(program, "uSamples");
        }
    } ss;

    struct BShader
    {
        unsigned int program;
        int uRes, uMode;

        static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
void main(){ gl_Position = vec4(aPos, 1.0); })";

        static constexpr const char* fsrc = R"(#version 400 core
out vec4 FragColor;
uniform sampler2D sSSAO;
uniform sampler2D sAlbedo;
uniform sampler2D gPos;
uniform sampler2D gNorm;
uniform vec2 uRes;
uniform int uMode;

void main(){
    vec2 uv = gl_FragCoord.xy / uRes;
    vec4 albedoSample = texture(sAlbedo, uv);
    vec3 centerPos = texture(gPos, uv).xyz;
    vec3 centerNorm = texture(gNorm, uv).xyz;
    float ssaoRaw = texture(sSSAO, uv).r;

    if (albedoSample.a < 0.01) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec2 texelSize = 1.0 / uRes;
    float result = 0.0;
    float weightSum = 0.0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float sampleAO = texture(sSSAO, uv + offset).r;
            vec3 samplePos = texture(gPos, uv + offset).xyz;
            vec3 sampleNorm = texture(gNorm, uv + offset).xyz;
            
            float wD = 1.0 / (1.0 + abs(centerPos.z - samplePos.z) * 100.0);
            float wN = max(0.0, dot(centerNorm, sampleNorm));
            float weight = exp(-(x*x + y*y) / 2.0) * wD * wN;
            
            result += sampleAO * weight;
            weightSum += weight;
        }
    }
    float ao = result / (weightSum + 0.0001);

    // Lambertian Lighting (View Space)
    vec3 L = normalize(vec3(0.5, 0.5, 1.0));
    float d = max(dot(centerNorm, L), 0.0);
    
    // White Clay Aesthetic
    vec3 ambient = albedoSample.rgb * 0.65;
    vec3 diffuse = albedoSample.rgb * 0.35 * d;
    float finalAO = pow(ao, 4.0);
    vec3 finalLit = (ambient + diffuse) * finalAO;

    if (uMode == 0) FragColor = vec4(finalLit, 1.0);
    else if (uMode == 1) FragColor = vec4(vec3(ssaoRaw), 1.0);
    else if (uMode == 2) FragColor = vec4(vec3(ao), 1.0);
    else if (uMode == 3) FragColor = vec4(centerNorm * 0.5 + 0.5, 1.0);
    else if (uMode == 4) FragColor = vec4(vec3(clamp((-centerPos.z - 20.0) / 100.0, 0.0, 1.0)), 1.0);
    else if (uMode == 5) FragColor = vec4(abs(centerPos) / 50.0, 1.0);
    else FragColor = vec4(finalLit, 1.0);
})";

        void init()
        {
            unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vs, 1, &vsrc, NULL);
            glCompileShader(vs);
            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            uRes = glGetUniformLocation(program, "uRes");
            uMode = glGetUniformLocation(program, "uMode");
        }
    } bs;

    struct SceneState
    {
        int mode = 0;
        float bias = 0.25f;
        float radius = 5.0f;
        int samples = 32;
        
        int sphereCount = 0;
        int boxCount = 0;
        float* sphereOffsets = nullptr;
        float* boxOffsets = nullptr;

        void reset() { sphereCount = boxCount = 0; }
    } state;

    struct Quad
    {
        unsigned int vao, vbo;
        void init()
        {
            float q[] = { -1,1,0, -1,-1,0, 1,1,0, 1,-1,0 };
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        }
        void draw() { glBindVertexArray(vao); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); }
    } quad;

    struct RenderItem
    {
        MeshPrimitive* mesh;
        float modelMatrix[16];
        float color[3];
    };

    RenderItem renderQueue[1024];
    int itemCount = 0;

    void addObject(MeshPrimitive* mesh, const float* modelMatrix, float r, float g, float b)
    {
        if (itemCount >= 1024) return;
        RenderItem& item = renderQueue[itemCount++];
        item.mesh = mesh;
        if (modelMatrix)
        {
            for (int i = 0; i < 16; ++i) item.modelMatrix[i] = modelMatrix[i];
        }
        else
        {
            Math::mat4_identity(item.modelMatrix);
        }
        item.color[0] = r;
        item.color[1] = g;
        item.color[2] = b;
    }

    void clearQueue()
    {
        itemCount = 0;
    }

    FBO gBuffer, ssaoFbo;

    void InitializePipeline(int width, int height)
    {
        gBuffer.init(width, height, 3, true);
        ssaoFbo.init(width, height, 1, false);
    }

    void init(int w, int h)
    {
        gs.init();
        ss.init();
        bs.init();
        quad.init();
        InitializePipeline(w, h);
    }
};

#ifdef SSAO_RUN_TEST
#include <opennurbs.h>
SSAO g_SSAO;
MeshPrimitive g_Sphere, g_Box, g_Plane, g_TerrainMesh;
int g_FrameCount = 0;
const int MAX_INSTANCES = 10000;

namespace SSAO_Test
{
    void Action_AddRandomSphere()
    {
        if (g_SSAO.state.sphereCount >= MAX_INSTANCES) return;
        int idx = g_SSAO.state.sphereCount * 4;
        g_SSAO.state.sphereOffsets[idx + 0] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
        g_SSAO.state.sphereOffsets[idx + 1] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
        g_SSAO.state.sphereOffsets[idx + 2] = ((float)rand() / RAND_MAX) * 30.0f + 1.0f;
        g_SSAO.state.sphereOffsets[idx + 3] = 1.0f;
        g_SSAO.state.sphereCount++;
        g_Sphere.updateInstanced(g_SSAO.state.sphereCount, g_SSAO.state.sphereOffsets);
    }

    void Action_GridSpheres()
    {
        g_SSAO.state.reset();
        int side = 10;
        for (int x = 0; x < side; ++x) {
            for (int y = 0; y < side; ++y) {
                for (int z = 0; z < side; ++z) {
                    if (g_SSAO.state.sphereCount >= MAX_INSTANCES) break;
                    int idx = g_SSAO.state.sphereCount * 4;
                    g_SSAO.state.sphereOffsets[idx + 0] = (float)(x - side/2) * 5.0f;
                    g_SSAO.state.sphereOffsets[idx + 1] = (float)(y - side/2) * 5.0f;
                    g_SSAO.state.sphereOffsets[idx + 2] = (float)z * 5.0f + 2.0f;
                    g_SSAO.state.sphereOffsets[idx + 3] = 1.0f;
                    g_SSAO.state.sphereCount++;
                }
            }
        }
        g_Sphere.updateInstanced(g_SSAO.state.sphereCount, g_SSAO.state.sphereOffsets);
    }

    void Action_RandomBoxes()
    {
        g_SSAO.state.reset();
        for (int i = 0; i < 500; ++i) {
            if (g_SSAO.state.boxCount >= MAX_INSTANCES) break;
            int idx = g_SSAO.state.boxCount * 4;
            g_SSAO.state.boxOffsets[idx + 0] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
            g_SSAO.state.boxOffsets[idx + 1] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
            g_SSAO.state.boxOffsets[idx + 2] = ((float)rand() / RAND_MAX) * 29.0f + 1.0f;
            g_SSAO.state.boxOffsets[idx + 3] = 1.0f + ((float)rand() / RAND_MAX) * 3.0f;
            g_SSAO.state.boxCount++;
        }
        g_Box.updateInstanced(g_SSAO.state.boxCount, g_SSAO.state.boxOffsets);
    }

    void Action_MixedScene()
    {
        g_SSAO.state.reset();
        for (int i = 0; i < 200; ++i) {
            if (g_SSAO.state.sphereCount < MAX_INSTANCES) {
                int idxS = g_SSAO.state.sphereCount * 4;
                g_SSAO.state.sphereOffsets[idxS + 0] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
                g_SSAO.state.sphereOffsets[idxS + 1] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
                g_SSAO.state.sphereOffsets[idxS + 2] = ((float)rand() / RAND_MAX) * 29.0f + 1.0f;
                g_SSAO.state.sphereOffsets[idxS + 3] = 1.0f + ((float)rand() / RAND_MAX) * 3.0f;
                g_SSAO.state.sphereCount++;
            }

            if (g_SSAO.state.boxCount < MAX_INSTANCES) {
                int idxB = g_SSAO.state.boxCount * 4;
                g_SSAO.state.boxOffsets[idxB + 0] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
                g_SSAO.state.boxOffsets[idxB + 1] = ((float)rand() / RAND_MAX - 0.5f) * 80.0f;
                g_SSAO.state.boxOffsets[idxB + 2] = ((float)rand() / RAND_MAX) * 29.0f + 1.0f;
                g_SSAO.state.boxOffsets[idxB + 3] = 1.0f + ((float)rand() / RAND_MAX) * 3.0f;
                g_SSAO.state.boxCount++;
            }
        }
        g_Sphere.updateInstanced(g_SSAO.state.sphereCount, g_SSAO.state.sphereOffsets);
        g_Box.updateInstanced(g_SSAO.state.boxCount, g_SSAO.state.boxOffsets);
    }

    void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        const char* MODE_NAMES[] = { "LIT", "AO_RAW", "AO_BLUR", "NORM", "DEPTH", "POS", "DELTA", "SAMPLES" };
        switch (key) {
            case GLFW_KEY_B: Action_GridSpheres(); break;
            case GLFW_KEY_N: Action_RandomBoxes(); break;
            case GLFW_KEY_M: Action_MixedScene(); break;
            case GLFW_KEY_EQUAL: Action_AddRandomSphere(); break;
            case GLFW_KEY_KP_ADD: Action_AddRandomSphere(); break;
            case GLFW_KEY_1: g_SSAO.state.bias = fmaxf(0.0f, g_SSAO.state.bias - 0.05f); break;
            case GLFW_KEY_2: g_SSAO.state.bias += 0.05f; break;
            case GLFW_KEY_3: g_SSAO.state.radius = fmaxf(0.1f, g_SSAO.state.radius - 0.5f); break;
            case GLFW_KEY_4: g_SSAO.state.radius += 0.5f; break;
            case GLFW_KEY_LEFT_BRACKET: g_SSAO.state.samples = fmaxf(1, g_SSAO.state.samples - 16); break;
            case GLFW_KEY_RIGHT_BRACKET: g_SSAO.state.samples = fminf(128, g_SSAO.state.samples + 16); break;
            case GLFW_KEY_D: 
                g_SSAO.state.mode = (g_SSAO.state.mode + 1) % 6; 
                printf("[DEBUG] Mode Switched: %s\n", MODE_NAMES[g_SSAO.state.mode]);
                break;
        }
        if (key != GLFW_KEY_D) {
            printf("[DEBUG] Bias: %.2f, Radius: %.2f, Samples: %d, Mode: %s\n", 
                g_SSAO.state.bias, g_SSAO.state.radius, g_SSAO.state.samples, MODE_NAMES[g_SSAO.state.mode]);
        }
        fflush(stdout);
    }

    void window_size_callback(GLFWwindow* window, int width, int height)
    {
        if (width > 0 && height > 0) {
            g_SSAO.InitializePipeline(width, height);
            glViewport(0, 0, width, height);
        }
    }
}

extern "C" void setup()
{
    if(!Alice::g_Arena.memory) Alice::g_Arena.init(128 * 1024 * 1024);
    
    AliceViewer* av = AliceViewer::instance();
    int w, h;
    glfwGetFramebufferSize(av->window, &w, &h);
    g_SSAO.init(w, h);
    glfwSetWindowSizeCallback(av->window, SSAO_Test::window_size_callback);
    glfwSetKeyCallback(av->window, SSAO_Test::key_callback);

    // Rhino Integration: Filter for "TERRAIN" mesh
    ONX_Model model;
    if (model.Read("test.3dm"))
    {
        ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
        const ON_ModelComponent* comp = nullptr;
        while ((comp = it.NextComponent()) != nullptr)
        {
            const ON_ModelGeometryComponent* geometry = ON_ModelGeometryComponent::Cast(comp);
            if (!geometry) continue;

            const ON_3dmObjectAttributes* attributes = geometry->Attributes(nullptr);
            if (attributes && attributes->m_name == L"TERRAIN")
            {
                const ON_Geometry* geom = geometry->Geometry(nullptr);
                const ON_Mesh* mesh = ON_Mesh::Cast(geom);
                if (mesh)
                {
                    int vcount = mesh->m_V.Count();
                    int icount = mesh->m_F.Count() * 3;
                    float* vdata = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));
                    unsigned int* idata = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
                    for (int i = 0; i < vcount; ++i)
                    {
                        vdata[i * 6 + 0] = (float)mesh->m_V[i].x;
                        vdata[i * 6 + 1] = (float)mesh->m_V[i].y;
                        vdata[i * 6 + 2] = (float)mesh->m_V[i].z;
                        if (mesh->HasVertexNormals())
                        {
                            vdata[i * 6 + 3] = (float)mesh->m_N[i].x;
                            vdata[i * 6 + 4] = (float)mesh->m_N[i].y;
                            vdata[i * 6 + 5] = (float)mesh->m_N[i].z;
                        }
                        else
                        {
                            vdata[i * 6 + 3] = 0; vdata[i * 6 + 4] = 0; vdata[i * 6 + 5] = 1;
                        }
                    }
                    for (int i = 0; i < mesh->m_F.Count(); ++i)
                    {
                        idata[i * 3 + 0] = (unsigned int)mesh->m_F[i].vi[0];
                        idata[i * 3 + 1] = (unsigned int)mesh->m_F[i].vi[1];
                        idata[i * 3 + 2] = (unsigned int)mesh->m_F[i].vi[2];
                    }
                    g_TerrainMesh.initFromRaw(vcount, vdata, icount, idata);
                }
            }
        }
    }

    g_Plane.initPlane(2000.0f);
    g_Sphere.initSphere(16, 8);
    g_Box.initBox();

    g_SSAO.state.sphereOffsets = (float*)Alice::g_Arena.allocate(MAX_INSTANCES * 4 * sizeof(float));
    g_SSAO.state.boxOffsets = (float*)Alice::g_Arena.allocate(MAX_INSTANCES * 4 * sizeof(float));
    
    g_Sphere.initInstanced(MAX_INSTANCES, g_SSAO.state.sphereOffsets);
    g_Box.initInstanced(MAX_INSTANCES, g_SSAO.state.boxOffsets);

    // Initial Queue Population
    g_SSAO.clearQueue();
    float identity[16]; Math::mat4_identity(identity);
    g_SSAO.addObject(&g_TerrainMesh, identity, 0.45f, 0.28f, 0.14f);
    g_SSAO.addObject(&g_Plane, identity, 0.95f, 0.95f, 0.95f);
}

extern "C" void update(float dt) {}

extern "C" void draw()
{
    AliceViewer* av = AliceViewer::instance();
    int w, h;
    glfwGetFramebufferSize(av->window, &w, &h);
    float aspect = (float)w / (float)h;

    float p[16], v[16], pv[16];
    Math::mat4_perspective(p, av->fov, aspect, 0.1f, 5000.0f);
    M4 viewMat = av->camera.getViewMatrix();
    for(int i=0; i<16; ++i) v[i] = viewMat.m[i];
    Math::mat4_mul(pv, p, v);

    // Pass 1: G-Buffer
    {
        g_SSAO.gBuffer.bind();
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glUseProgram(g_SSAO.gs.program);
        
        for (int i = 0; i < g_SSAO.itemCount; ++i)
        {
            auto& item = g_SSAO.renderQueue[i];
            float mv[16], mvp[16];
            Math::mat4_mul(mv, v, item.modelMatrix);
            Math::mat4_mul(mvp, p, mv);
            glUniformMatrix4fv(g_SSAO.gs.uMV, 1, GL_FALSE, mv);
            glUniformMatrix4fv(g_SSAO.gs.uMVP, 1, GL_FALSE, mvp);
            glUniform3f(g_SSAO.gs.uColor, item.color[0], item.color[1], item.color[2]);
            item.mesh->draw();
        }
        g_SSAO.gBuffer.unbind();
    }

    // Pass 2: SSAO Compute
    {
        g_SSAO.ssaoFbo.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(g_SSAO.ss.program);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[0]);
        glUniform1i(glGetUniformLocation(g_SSAO.ss.program, "gPos"), 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[1]);
        glUniform1i(glGetUniformLocation(g_SSAO.ss.program, "gNorm"), 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_SSAO.ss.noiseTex);
        glUniform1i(glGetUniformLocation(g_SSAO.ss.program, "uNoise"), 2);
        glUniform3fv(glGetUniformLocation(g_SSAO.ss.program, "uKernel"), 128, g_SSAO.ss.kernel);
        glUniformMatrix4fv(g_SSAO.ss.uP, 1, GL_FALSE, p);
        glUniform1f(g_SSAO.ss.uRadius, g_SSAO.state.radius);
        glUniform1f(g_SSAO.ss.uBias, g_SSAO.state.bias);
        glUniform2f(g_SSAO.ss.uRes, (float)w, (float)h);
        glUniform2f(g_SSAO.ss.uNoiseScale, (float)w / 4.0f, (float)h / 4.0f);
        glUniform1i(g_SSAO.ss.uSamples, g_SSAO.state.samples);
        g_SSAO.quad.draw();
        g_SSAO.ssaoFbo.unbind();
    }

    // Pass 3: Composite
    {
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(g_SSAO.bs.program);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_SSAO.ssaoFbo.textures[0]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "sSSAO"), 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[2]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "sAlbedo"), 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[0]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "gPos"), 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[1]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "gNorm"), 3);
        glUniform2f(g_SSAO.bs.uRes, (float)w, (float)h);
        glUniform1i(g_SSAO.bs.uMode, g_SSAO.state.mode);
        g_SSAO.quad.draw();
    }
    g_FrameCount++;
}
#endif
#endif
