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

        static void checkShader(unsigned int id, const char* type)
        {
            int status;
            glGetShaderiv(id, GL_COMPILE_STATUS, &status);
            if (!status)
            {
                char log[1024];
                glGetShaderInfoLog(id, 1024, NULL, log);
                printf("[ERROR] Shader %s: %s\n", type, log);
            }
        }

        static void checkProgram(unsigned int id)
        {
            int status;
            glGetProgramiv(id, GL_LINK_STATUS, &status);
            if (!status)
            {
                char log[1024];
                glGetProgramInfoLog(id, 1024, NULL, log);
                printf("[ERROR] Program Link: %s\n", log);
            }
        }

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
            checkShader(vs, "G-VS");

            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            checkShader(fs, "G-FS");

            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            checkProgram(program);

            uMVP = glGetUniformLocation(program, "uMVP");
            uMV = glGetUniformLocation(program, "uMV");
            uColor = glGetUniformLocation(program, "uColor");
        }
    } gs;

    struct PShader
    {
        unsigned int program;
        int uP, uMV, uColor, uMode, uRes, uThickness;

        static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec4 aInstanceData;
out vec3 vPos;
out vec2 vUV;
uniform mat4 uP;
uniform mat4 uMV;
uniform vec2 uRes;
uniform float uThickness;
void main(){
    vec4 viewCenter = uMV * vec4(aInstanceData.xyz, 1.0);
    vec4 clipCenter = uP * viewCenter;
    
    // Constant screen-space pixel size
    vec2 offset = aPos.xy * uThickness * 2.0 / uRes;
    gl_Position = clipCenter;
    gl_Position.xy += offset * clipCenter.w;
    
    // Z-Bias: Move points slightly in front of terrain
    gl_Position.z -= 0.0005 * gl_Position.w;

    vPos = viewCenter.xyz;
    vUV = aPos.xy * 2.0; 
})";

        static constexpr const char* fsrc = R"(#version 400 core
layout(location=0) out vec4 gPos;
layout(location=1) out vec4 gNorm;
layout(location=2) out vec4 gAlbedo;
in vec3 vPos;
in vec2 vUV;
uniform vec3 uColor;
uniform int uMode; // 0: Disk, 1: Cross-hair
void main(){
    float distSq = dot(vUV, vUV);
    if (uMode == 0) {
        if (distSq > 1.0) discard;
    } else {
        float thickness = 0.2;
        bool horizontal = abs(vUV.y) < thickness && abs(vUV.x) < 1.0;
        bool vertical = abs(vUV.x) < thickness && abs(vUV.y) < 1.0;
        if (!horizontal && !vertical) discard;
    }
    gPos = vec4(vPos, 1.0);
    gNorm = vec4(0.0, 0.0, 1.0, 1.0);
    gAlbedo = vec4(uColor, 1.0); // Rhino Vector Black
})";

        void init()
        {
            unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vs, 1, &vsrc, NULL);
            glCompileShader(vs);
            GShader::checkShader(vs, "P-VS");

            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            GShader::checkShader(fs, "P-FS");

            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            GShader::checkProgram(program);

            uP = glGetUniformLocation(program, "uP");
            uMV = glGetUniformLocation(program, "uMV");
            uColor = glGetUniformLocation(program, "uColor");
            uMode = glGetUniformLocation(program, "uMode");
            uRes = glGetUniformLocation(program, "uRes");
            uThickness = glGetUniformLocation(program, "uThickness");
        }
    } ps;

    struct PointRenderer
    {
        unsigned int vao, vbo, instanceVbo;
        void init()
        {
            float quad[] = { -0.5f,0.5f,0, -0.5f,-0.5f,0, 0.5f,0.5f,0, 0.5f,-0.5f,0 };
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);

            glGenBuffers(1, &instanceVbo);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
            glBufferData(GL_ARRAY_BUFFER, 10000 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
            glVertexAttribDivisor(2, 1);
            glBindVertexArray(0);
        }
        void update(int count, float* data)
        {
            glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, count * 4 * sizeof(float), data);
        }
        void draw(int count)
        {
            glBindVertexArray(vao);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
        }
    } pointRenderer;

    struct SShader
    {
        unsigned int program;
        unsigned int kernelTex;
        int uP, uRadius, uBias, uRes, uNoiseScale, uSamples, uAoType, uSkyUp;
        float kernel[2048 * 3];

        static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
void main(){ gl_Position = vec4(aPos, 1.0); })";

        static constexpr const char* fsrc = R"(#version 400 core
out float FragColor;
uniform sampler2D gPos;
uniform sampler2D gNorm;
uniform sampler1D uKernelTex;
uniform mat4 uP;
uniform float uRadius;
uniform float uBias;
uniform vec2 uRes;
uniform int uSamples;
uniform int uAoType; 
uniform vec3 uSkyUp; 

float IGN(vec2 v) {
    return fract(52.9829189 * fract(dot(v, vec2(0.06711056, 0.00583715))));
}

void main(){
    vec2 uv = gl_FragCoord.xy / uRes;
    vec3 pos = texture(gPos, uv).xyz;
    vec3 norm = texture(gNorm, uv).xyz;
    float validNorm = step(0.1, length(norm));
    
    float noise = IGN(gl_FragCoord.xy);
    float angle = noise * 6.283185;
    float s = sin(angle);
    float c = cos(angle);
    mat3 rot = mat3(
        vec3(c, s, 0),
        vec3(-s, c, 0),
        vec3(0, 0, 1)
    );

    vec3 basisUp = (uAoType == 0) ? norm : uSkyUp;
    vec3 tangent = normalize(vec3(1, 0, 0.1) - basisUp * dot(vec3(1, 0, 0.1), basisUp));
    vec3 bitangent = cross(basisUp, tangent);
    mat3 TBN = mat3(tangent, bitangent, basisUp) * rot;

    float effectiveBias = uBias * abs(pos.z) * 0.001;
    float occlusion = 0.0;
    for(int i = 0; i < uSamples; ++i){
        vec3 kernelSample = texelFetch(uKernelTex, i, 0).xyz;
        vec3 samplePos = TBN * kernelSample;
        samplePos = pos + samplePos * uRadius;

        vec4 offset = vec4(samplePos, 1.0);
        offset = uP * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sampleDepth = texture(gPos, offset.xy).z;
        float dist = abs(pos.z - sampleDepth);
        float rangeCheck = exp(-dist / uRadius);
        occlusion += step(samplePos.z + effectiveBias, sampleDepth) * rangeCheck;
    }
    float ao = 1.0 - (occlusion / float(uSamples));
    FragColor = mix(1.0, ao, validNorm);
})";

        void init()
        {
            for (int i = 0; i < 2048; ++i)
            {
                float z = (float)i / 2048.0f;
                float r = sqrtf(1.0f - z * z);
                float theta = (float)i * 2.39996f;
                float scale = (float)i / 2048.0f;
                scale = 0.1f + 0.9f * scale * scale;
                kernel[i * 3 + 0] = cosf(theta) * r * scale;
                kernel[i * 3 + 1] = sinf(theta) * r * scale;
                kernel[i * 3 + 2] = z * scale;
            }

            glGenTextures(1, &kernelTex);
            glBindTexture(GL_TEXTURE_1D, kernelTex);
            glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB16F, 2048, 0, GL_RGB, GL_FLOAT, kernel);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

            unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vs, 1, &vsrc, NULL);
            glCompileShader(vs);
            GShader::checkShader(vs, "S-VS");

            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            GShader::checkShader(fs, "S-FS");

            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            GShader::checkProgram(program);

            uP = glGetUniformLocation(program, "uP");
            uRadius = glGetUniformLocation(program, "uRadius");
            uBias = glGetUniformLocation(program, "uBias");
            uRes = glGetUniformLocation(program, "uRes");
            uSamples = glGetUniformLocation(program, "uSamples");
            uAoType = glGetUniformLocation(program, "uAoType");
            uSkyUp = glGetUniformLocation(program, "uSkyUp");
        }
    } ss;

    struct BShader
    {
        unsigned int program;
        int uRes, uMode, uDir, uAoType;

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
uniform int uDir; // 0: Horizontal, 1: Vertical, 2: Final Composite
uniform int uAoType;

vec3 falseColor(float v) {
    vec3 c1 = vec3(0.0, 0.0, 0.5); // Deep Blue
    vec3 c2 = vec3(0.0, 1.0, 0.0); // Bright Green
    return mix(c1, c2, v);
}

void main(){
    vec2 uv = gl_FragCoord.xy / uRes;
    
    if (uDir < 2) {
        float centerAO = texture(sSSAO, uv).r;
        vec3 centerPos = texture(gPos, uv).xyz;
        vec3 centerNorm = texture(gNorm, uv).xyz;
        
        vec2 texelSize = 1.0 / uRes;
        vec2 dir = (uDir == 0) ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
        
        float result = 0.0;
        float weightSum = 0.0;
        for (int i = -4; i <= 4; ++i) {
            vec2 offset = dir * float(i);
            float sampleAO = texture(sSSAO, uv + offset).r;
            vec3 samplePos = texture(gPos, uv + offset).xyz;
            vec3 sampleNorm = texture(gNorm, uv + offset).xyz;
            
            float wD = 1.0 / (1.0 + abs(centerPos.z - samplePos.z) * 400.0);
            float wN = pow(max(0.0, dot(centerNorm, sampleNorm)), 16.0);
            float weight = exp(-(i*i) / 8.0) * wD * wN;
            
            result += sampleAO * weight;
            weightSum += weight;
        }
        FragColor = vec4(vec3(result / (weightSum + 0.0001)), 1.0);
        return;
    }

    vec4 albedoSample = texture(sAlbedo, uv);
    vec3 centerPos = texture(gPos, uv).xyz;
    vec3 centerNorm = texture(gNorm, uv).xyz;
    float ao = texture(sSSAO, uv).r;

    if (albedoSample.a < 0.01) {
        FragColor = vec4(0.95, 0.95, 0.95, 1.0); 
        return;
    }

    // False Color Mapping for Sky Visibility
    if (uMode == 1) {
        FragColor = vec4(falseColor(ao), 1.0);
        return;
    }

    // Arctic Mode Lighting: Hemispherical Ambient
    float hemi = centerNorm.y * 0.5 + 0.5;
    vec3 skyColor = vec3(1.0, 1.0, 1.0);
    vec3 groundColor = vec3(0.5, 0.5, 0.5);
    vec3 ambient = mix(groundColor, skyColor, hemi) * 0.95;

    float vdn = 1.0 - max(dot(centerNorm, vec3(0.0, 0.0, 1.0)), 0.0);
    float ink = smoothstep(0.5, 1.0, vdn) * 0.15;

    float finalAO = pow(ao, 3.5); 
    vec3 finalLit = (ambient * finalAO) - ink;

    if (uMode == 0) FragColor = vec4(finalLit, 1.0);
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
            GShader::checkShader(vs, "B-VS");

            unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 1, &fsrc, NULL);
            glCompileShader(fs);
            GShader::checkShader(fs, "B-FS");

            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            GShader::checkProgram(program);

            uRes = glGetUniformLocation(program, "uRes");
            uMode = glGetUniformLocation(program, "uMode");
            uDir = glGetUniformLocation(program, "uDir");
            uAoType = glGetUniformLocation(program, "uAoType");
        }
    } bs;

    struct SceneState
    {
        int mode = 0;
        int pointMode = 0; // 0: Disk, 1: Cross-hair
        int aoType = 0;    // 0: SSAO, 1: SkyVisibility
        float bias = 0.08f;
        float radius = 12.0f;
        float pointThickness = 2.0f;
        float lastViewMatrix[16] = {0};
        int staticFrames = 0;
        int interactiveSamples = 256;
        int staticSamples = 2048;
        
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

    struct RenderQueue
    {
        MeshPrimitive* meshes[1024];
        float modelMatrices[1024][16];
        float colors[1024][3];
        int instanceCounts[1024];
        bool isPointLayer[1024];
        int count = 0;

        void reset() { count = 0; }

        void add(MeshPrimitive* mesh, const float* modelMatrix, float r, float g, float b, int instanceCount = 0, bool isPoint = false)
        {
            if (count >= 1024) return;
            meshes[count] = mesh;
            if (modelMatrix)
            {
                for (int i = 0; i < 16; ++i) modelMatrices[count][i] = modelMatrix[i];
            }
            else
            {
                Math::mat4_identity(modelMatrices[count]);
            }
            colors[count][0] = r;
            colors[count][1] = g;
            colors[count][2] = b;
            instanceCounts[count] = instanceCount;
            isPointLayer[count] = isPoint;
            count++;
        }
    } queue;

    void addObject(MeshPrimitive* mesh, const float* modelMatrix, float r, float g, float b, int instanceCount = 0, bool isPoint = false)
    {
        queue.add(mesh, modelMatrix, r, g, b, instanceCount, isPoint);
    }

    void clearQueue()
    {
        queue.reset();
    }

    FBO gBuffer, ssaoFbo, pingPongFbo;

    void InitializePipeline(int width, int height)
    {
        gBuffer.init(width, height, 3, true);
        ssaoFbo.init(width, height, 1, false);
        pingPongFbo.init(width, height, 1, false);
    }

    void init(int w, int h)
    {
        gs.init();
        ps.init();
        ss.init();
        bs.init();
        quad.init();
        pointRenderer.init();
        InitializePipeline(w, h);
    }
};

#ifdef SSAO_RUN_TEST
#include <opennurbs.h>
#include "DebugRender.h"
SSAO g_SSAO;
DebugRender g_Debug;
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
        // Sphere update skipped for PC layer as it now uses pointRenderer
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
        g_Box.updateInstanced(g_SSAO.state.boxCount, g_SSAO.state.boxOffsets);
    }

    void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        const char* MODE_NAMES[] = { "LIT", "SKY_false", "AO_BLUR", "NORM", "DEPTH", "POS", "DEBUG" };
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
            case GLFW_KEY_LEFT_BRACKET: g_SSAO.state.staticSamples = fmaxf(1, g_SSAO.state.staticSamples - 128); break;
            case GLFW_KEY_RIGHT_BRACKET: g_SSAO.state.staticSamples = fminf(2048, g_SSAO.state.staticSamples + 128); break;
            case GLFW_KEY_K: g_SSAO.state.pointMode = (g_SSAO.state.pointMode + 1) % 2; break;
            case GLFW_KEY_S: g_SSAO.state.aoType = (g_SSAO.state.aoType + 1) % 2; break;
            case GLFW_KEY_9: g_SSAO.state.pointThickness = fmaxf(0.1f, g_SSAO.state.pointThickness - 0.5f); break;
            case GLFW_KEY_0: g_SSAO.state.pointThickness += 0.5f; break;
            case GLFW_KEY_D: 
                g_SSAO.state.mode = (g_SSAO.state.mode + 1) % 7; 
                printf("[DEBUG] Mode Switched: %s\n", MODE_NAMES[g_SSAO.state.mode]);
                break;
        }
        if (key != GLFW_KEY_D) {
            printf("[DEBUG] Bias: %.2f, Radius: %.2f, Static Samples: %d, Mode: %s, PtMode: %d, PtThick: %.1f\n", 
                g_SSAO.state.bias, g_SSAO.state.radius, g_SSAO.state.staticSamples, MODE_NAMES[g_SSAO.state.mode], g_SSAO.state.pointMode, g_SSAO.state.pointThickness);
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
    g_Debug.init();
    glfwSetWindowSizeCallback(av->window, SSAO_Test::window_size_callback);
    glfwSetKeyCallback(av->window, SSAO_Test::key_callback);

    // Isometric Camera Configuration
    av->fov = 25.0f * (3.14159f / 180.0f); 
    av->camera.distance = 280.0f;
    av->camera.yaw = 0.785398f;
    av->camera.pitch = 0.615472f;
    av->camera.focusPoint = { 0.0f, 0.0f, 0.0f };

    // Primitive and Instancing Initialization
    g_Plane.initPlane(120.0f);
    g_Sphere.initSphere(16, 8);
    g_Box.initBox();

    g_SSAO.state.sphereOffsets = (float*)Alice::g_Arena.allocate(MAX_INSTANCES * 4 * sizeof(float));
    g_SSAO.state.boxOffsets = (float*)Alice::g_Arena.allocate(MAX_INSTANCES * 4 * sizeof(float));
    
    g_Box.initInstanced(MAX_INSTANCES, g_SSAO.state.boxOffsets);

    // Rhino Integration: Filter for "TERRAIN" mesh and "PC" point cloud
    ONX_Model model;
    const char* paths[] = { "test.3dm", "build/bin/test.3dm", "../test.3dm" };
    bool loaded = false;
    for (const char* p : paths)
    {
        if (model.Read(p))
        {
            printf("[INFO] Model %s loaded successfully.\n", p);
            loaded = true;
            break;
        }
    }

    if (loaded)
    {
        ON_3dPoint terrainCenter(0,0,0);
        float terrainScale = 1.0f;
        float s_bottomElevation = 0.0f;
        bool terrainFound = false;

        // Pass 1: Find TERRAIN and calculate normalization
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
                        printf("[INFO] TERRAIN mesh found with %d vertices.\n", mesh->m_V.Count());
                        int vcount = mesh->m_V.Count();
                        ON_3dPoint minPt(1e18, 1e18, 1e18), maxPt(-1e18, -1e18, -1e18);
                        for (int i = 0; i < vcount; ++i)
                        {
                            ON_3dPoint p = mesh->m_V[i];
                            if (p.x < minPt.x) minPt.x = p.x; if (p.y < minPt.y) minPt.y = p.y; if (p.z < minPt.z) minPt.z = p.z;
                            if (p.x > maxPt.x) maxPt.x = p.x; if (p.y > maxPt.y) maxPt.y = p.y; if (p.z > maxPt.z) maxPt.z = p.z;
                        }
                        terrainCenter = (minPt + maxPt) * 0.5;
                        ON_3dVector extent = maxPt - minPt;
                        double maxExtent = fmax(extent.x, fmax(extent.y, extent.z));
                        if (maxExtent < 1e-6) maxExtent = 1.0;
                        terrainScale = 100.0f / (float)maxExtent;
                        s_bottomElevation = (float)(minPt.z - terrainCenter.z) * terrainScale;
                        terrainFound = true;

                        int icount = 0;
                        for (int i = 0; i < mesh->m_F.Count(); ++i) icount += mesh->m_F[i].IsQuad() ? 6 : 3;

                        // Contiguous Memory Allocation for Terrain
                        float* vdata = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));
                        unsigned int* idata = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
                        
                        const ON_3fPoint* V = mesh->m_V.Array();
                        const ON_3fVector* N = mesh->m_N.Array();
                        const bool hasNormals = mesh->HasVertexNormals();

                        for (int i = 0; i < vcount; ++i)
                        {
                            const ON_3fPoint& p = V[i];
                            vdata[i * 6 + 0] = (float)(p.x - terrainCenter.x) * terrainScale;
                            vdata[i * 6 + 1] = (float)(p.y - terrainCenter.y) * terrainScale;
                            vdata[i * 6 + 2] = (float)(p.z - terrainCenter.z) * terrainScale;
                            
                            if (hasNormals)
                            {
                                const ON_3fVector& n = N[i];
                                vdata[i * 6 + 3] = (float)n.x;
                                vdata[i * 6 + 4] = (float)n.y;
                                vdata[i * 6 + 5] = (float)n.z;
                            }
                            else
                            {
                                vdata[i * 6 + 3] = 0.0f; vdata[i * 6 + 4] = 0.0f; vdata[i * 6 + 5] = 1.0f;
                            }
                        }
                        
                        const ON_MeshFace* F = mesh->m_F.Array();
                        int idx = 0;
                        for (int i = 0; i < mesh->m_F.Count(); ++i) {
                            const ON_MeshFace& face = F[i];
                            idata[idx++] = (unsigned int)face.vi[0];
                            idata[idx++] = (unsigned int)face.vi[1];
                            idata[idx++] = (unsigned int)face.vi[2];
                            if (face.IsQuad()) {
                                idata[idx++] = (unsigned int)face.vi[0];
                                idata[idx++] = (unsigned int)face.vi[2];
                                idata[idx++] = (unsigned int)face.vi[3];
                            }
                        }
                        g_TerrainMesh.initFromRaw(vcount, vdata, icount, idata);

                        g_SSAO.clearQueue();
                        float identity[16]; Math::mat4_identity(identity);
                        g_SSAO.addObject(&g_TerrainMesh, identity, 0.9f, 0.9f, 0.9f); // Matte White

                        float planeMatrix[16];
                        Math::mat4_identity(planeMatrix);
                        planeMatrix[14] = s_bottomElevation; 
                        g_SSAO.addObject(&g_Plane, planeMatrix, 0.95f, 0.95f, 0.95f);
                        break;
                    }
                }
            }
        }

        // Pass 2: Find PC and normalize
        if (terrainFound)
        {
            ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
            const ON_ModelComponent* comp = nullptr;
            while ((comp = it.NextComponent()) != nullptr)
            {
                const ON_ModelGeometryComponent* geometry = ON_ModelGeometryComponent::Cast(comp);
                if (!geometry) continue;
                const ON_3dmObjectAttributes* attributes = geometry->Attributes(nullptr);
                if (attributes && attributes->m_name == L"PC")
                {
                    const ON_Geometry* geom = geometry->Geometry(nullptr);
                    const ON_PointCloud* pc = ON_PointCloud::Cast(geom);
                    if (pc)
                    {
                        printf("[INFO] PC point cloud found with %d points.\n", pc->m_P.Count());
                        int pcount = pc->m_P.Count();
                        if (pcount > MAX_INSTANCES) pcount = MAX_INSTANCES;

                        // Use pre-allocated state buffer
                        for (int i = 0; i < pcount; ++i)
                        {
                            ON_3dPoint p = pc->m_P[i];
                            g_SSAO.state.sphereOffsets[i * 4 + 0] = (float)(p.x - terrainCenter.x) * terrainScale;
                            g_SSAO.state.sphereOffsets[i * 4 + 1] = (float)(p.y - terrainCenter.y) * terrainScale;
                            g_SSAO.state.sphereOffsets[i * 4 + 2] = (float)(p.z - terrainCenter.z) * terrainScale;
                            g_SSAO.state.sphereOffsets[i * 4 + 3] = 0.08f; // Fine dots
                        }

                        g_SSAO.state.sphereCount = pcount;
                        // Diverse False-Color Points
                        g_SSAO.addObject(nullptr, nullptr, 0.05f, 0.05f, 0.05f, g_SSAO.state.sphereCount, true); 
                        g_SSAO.addObject(nullptr, nullptr, 0.8f, 0.1f, 0.1f, 100, true); 
                        g_SSAO.addObject(nullptr, nullptr, 0.1f, 0.1f, 0.8f, 100, true); 
                    }
                }
            }
        }
        else
        {
            printf("[WARN] TERRAIN mesh NOT found in model.\n");
        }
    }
    else
    {
        printf("[CRITICAL ERROR] Failed to read model test.3dm from any attempted path.\n");
    }
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

    // Adaptive Logic
    int isCameraMoving = 0;
    for (int i = 0; i < 16; ++i) {
        isCameraMoving |= (v[i] != g_SSAO.state.lastViewMatrix[i]);
        g_SSAO.state.lastViewMatrix[i] = v[i];
    }
    g_SSAO.state.staticFrames = (g_SSAO.state.staticFrames + 1) * (!isCameraMoving);
    int isStatic = (g_SSAO.state.staticFrames > 15);
    int activeSamples = isStatic * g_SSAO.state.staticSamples + (!isStatic) * g_SSAO.state.interactiveSamples;

    if (g_SSAO.state.sphereCount > 0)
    {
        g_SSAO.pointRenderer.update(g_SSAO.state.sphereCount, g_SSAO.state.sphereOffsets);
    }

    // Pass 1: G-Buffer
    {
        g_SSAO.gBuffer.bind();
        glClearColor(0.95f, 0.95f, 0.95f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        
        for (int i = 0; i < g_SSAO.queue.count; ++i)
        {
            if (g_SSAO.queue.isPointLayer[i])
            {
                glUseProgram(g_SSAO.ps.program);
                glUniformMatrix4fv(g_SSAO.ps.uP, 1, GL_FALSE, p);
                glUniformMatrix4fv(g_SSAO.ps.uMV, 1, GL_FALSE, v);
                glUniform3fv(g_SSAO.ps.uColor, 1, g_SSAO.queue.colors[i]); 
                glUniform1i(g_SSAO.ps.uMode, g_SSAO.state.pointMode);
                glUniform2f(g_SSAO.ps.uRes, (float)w, (float)h);
                glUniform1f(g_SSAO.ps.uThickness, g_SSAO.state.pointThickness);
                g_SSAO.pointRenderer.draw(g_SSAO.queue.instanceCounts[i]);
            }
            else
            {
                glUseProgram(g_SSAO.gs.program);
                float mv[16], mvp[16];
                Math::mat4_mul(mv, v, g_SSAO.queue.modelMatrices[i]);
                Math::mat4_mul(mvp, p, mv);
                glUniformMatrix4fv(g_SSAO.gs.uMV, 1, GL_FALSE, mv);
                glUniformMatrix4fv(g_SSAO.gs.uMVP, 1, GL_FALSE, mvp);
                glUniform3fv(g_SSAO.gs.uColor, 1, g_SSAO.queue.colors[i]);
                g_SSAO.queue.meshes[i]->drawInstanced(g_SSAO.queue.instanceCounts[i] ? g_SSAO.queue.instanceCounts[i] : 1);
            }
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
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[1]);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_1D, g_SSAO.ss.kernelTex);
        glUniform1i(glGetUniformLocation(g_SSAO.ss.program, "gPos"), 0);
        glUniform1i(glGetUniformLocation(g_SSAO.ss.program, "gNorm"), 1);
        glUniform1i(glGetUniformLocation(g_SSAO.ss.program, "uKernelTex"), 2);
        glUniformMatrix4fv(g_SSAO.ss.uP, 1, GL_FALSE, p);
        glUniform1f(g_SSAO.ss.uRadius, g_SSAO.state.radius);
        glUniform1f(g_SSAO.ss.uBias, g_SSAO.state.bias);
        glUniform2f(g_SSAO.ss.uRes, (float)w, (float)h);
        glUniform1i(g_SSAO.ss.uSamples, activeSamples);
        glUniform1i(g_SSAO.ss.uAoType, g_SSAO.state.aoType);
        float skyUp[3] = { v[1], v[5], v[9] };
        glUniform3fv(g_SSAO.ss.uSkyUp, 1, skyUp);
        g_SSAO.quad.draw();
        g_SSAO.ssaoFbo.unbind();
    }

    // Pass 3: Separable Bilateral Blur
    {
        glUseProgram(g_SSAO.bs.program);
        glUniform2f(g_SSAO.bs.uRes, (float)w, (float)h);
        glUniform1i(g_SSAO.bs.uMode, g_SSAO.state.mode);
        glUniform1i(g_SSAO.bs.uAoType, g_SSAO.state.aoType);
        
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[0]); // gPos
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[1]); // gNorm
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "gPos"), 1);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "gNorm"), 2);

        // Horizontal
        g_SSAO.pingPongFbo.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_SSAO.ssaoFbo.textures[0]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "sSSAO"), 0);
        glUniform1i(g_SSAO.bs.uDir, 0);
        g_SSAO.quad.draw();
        g_SSAO.pingPongFbo.unbind();

        // Vertical
        g_SSAO.ssaoFbo.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_SSAO.pingPongFbo.textures[0]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "sSSAO"), 0);
        glUniform1i(g_SSAO.bs.uDir, 1);
        g_SSAO.quad.draw();
        g_SSAO.ssaoFbo.unbind();
    }

    // Pass 4: Composite
    {
        glViewport(0, 0, w, h);
        glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(g_SSAO.bs.program);
        glUniform1i(g_SSAO.bs.uDir, 2);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_SSAO.ssaoFbo.textures[0]);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[2]);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[0]);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, g_SSAO.gBuffer.textures[1]);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "sSSAO"), 0);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "sAlbedo"), 1);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "gPos"), 2);
        glUniform1i(glGetUniformLocation(g_SSAO.bs.program, "gNorm"), 3);
        g_SSAO.quad.draw();
    }

    g_FrameCount++;
}
#endif
#endif
