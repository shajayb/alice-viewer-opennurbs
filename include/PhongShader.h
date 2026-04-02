#ifndef PHONG_SHADER_H
#define PHONG_SHADER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <cstdlib>

#include "AliceMemory.h"

namespace Alice { extern LinearArena g_Arena; }

struct ScopedTimer
{
    const char* label;
    double start;
    ScopedTimer(const char* l) : label(l), start(glfwGetTime()) {}
    ~ScopedTimer()
    {
        if (label)
        {
            double elapsed = (glfwGetTime() - start) * 1000.0;
            printf("[METRIC] %s: %.4f ms (%.0f us)\n", label, elapsed, elapsed * 1000.0);
        }
    }
};

struct PhongShader
{
    unsigned int program;
    int uMVP, uModel, uNormalMatrix, uLightPos, uViewPos, uMaterialColor;

    static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aInstanceOffset;
out vec3 FragPos;
out vec3 Normal;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
void main(){
    vec3 worldPos = aPos + aInstanceOffset;
    FragPos = vec3(uModel * vec4(worldPos, 1.0));
    Normal = normalize(uNormalMatrix * aNormal);
    gl_Position = uMVP * vec4(worldPos, 1.0);
})";

    static constexpr const char* fsrc = R"(#version 400 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
uniform vec3 uLightPos;
uniform vec3 uViewPos;
uniform vec3 uMaterialColor;
void main(){
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * lightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    float specularStrength = 1.0;
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128);
    vec3 specular = specularStrength * spec * lightColor;
    vec3 result = (ambient + diffuse + specular) * uMaterialColor;
    FragColor = vec4(result, 1.0);
})";

    bool init()
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
        glDeleteShader(vs);
        glDeleteShader(fs);
        uMVP = glGetUniformLocation(program, "uMVP");
        uModel = glGetUniformLocation(program, "uModel");
        uNormalMatrix = glGetUniformLocation(program, "uNormalMatrix");
        uLightPos = glGetUniformLocation(program, "uLightPos");
        uViewPos = glGetUniformLocation(program, "uViewPos");
        uMaterialColor = glGetUniformLocation(program, "uMaterialColor");
        return true;
    }

    void use() const { glUseProgram(program); }
    void setMVP(const float* m) const { glUniformMatrix4fv(uMVP, 1, GL_FALSE, m); }
    void setModel(const float* m) const { glUniformMatrix4fv(uModel, 1, GL_FALSE, m); }
    void setNormalMatrix(const float* m) const { glUniformMatrix3fv(uNormalMatrix, 1, GL_FALSE, m); }
    void setLightPos(float x, float y, float z) const { glUniform3f(uLightPos, x, y, z); }
    void setViewPos(float x, float y, float z) const { glUniform3f(uViewPos, x, y, z); }
    void setMaterialColor(float r, float g, float b) const { glUniform3f(uMaterialColor, r, g, b); }
};

namespace Math
{
    struct Vec3 { float x, y, z; };

    inline Vec3 normalize(Vec3 v) { float l = 1.0f / sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); return {v.x*l, v.y*l, v.z*l}; }
    inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
    inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

    inline void mat4_identity(float* m)
    {
        m[0]=1.0f; m[1]=0.0f; m[2]=0.0f; m[3]=0.0f;
        m[4]=0.0f; m[5]=1.0f; m[6]=0.0f; m[7]=0.0f;
        m[8]=0.0f; m[9]=0.0f; m[10]=1.0f; m[11]=0.0f;
        m[12]=0.0f; m[13]=0.0f; m[14]=0.0f; m[15]=1.0f;
    }

    inline void mat4_zero(float* m)
    {
        m[0]=0.0f; m[1]=0.0f; m[2]=0.0f; m[3]=0.0f;
        m[4]=0.0f; m[5]=0.0f; m[6]=0.0f; m[7]=0.0f;
        m[8]=0.0f; m[9]=0.0f; m[10]=0.0f; m[11]=0.0f;
        m[12]=0.0f; m[13]=0.0f; m[14]=0.0f; m[15]=0.0f;
    }

    inline void mat4_mul(float* out, const float* a, const float* b)
    {
        float res[16];
        res[0] = a[0]*b[0] + a[4]*b[1] + a[8]*b[2] + a[12]*b[3];
        res[1] = a[1]*b[0] + a[5]*b[1] + a[9]*b[2] + a[13]*b[3];
        res[2] = a[2]*b[0] + a[6]*b[1] + a[10]*b[2] + a[14]*b[3];
        res[3] = a[3]*b[0] + a[7]*b[1] + a[11]*b[2] + a[15]*b[3];
        res[4] = a[0]*b[4] + a[4]*b[5] + a[8]*b[6] + a[12]*b[7];
        res[5] = a[1]*b[4] + a[5]*b[5] + a[9]*b[6] + a[13]*b[7];
        res[6] = a[2]*b[4] + a[6]*b[5] + a[10]*b[6] + a[14]*b[7];
        res[7] = a[3]*b[4] + a[7]*b[5] + a[11]*b[6] + a[15]*b[7];
        res[8] = a[0]*b[8] + a[4]*b[9] + a[8]*b[10] + a[12]*b[11];
        res[9] = a[1]*b[8] + a[5]*b[9] + a[9]*b[10] + a[13]*b[11];
        res[10] = a[2]*b[8] + a[6]*b[9] + a[10]*b[10] + a[14]*b[11];
        res[11] = a[3]*b[8] + a[7]*b[9] + a[11]*b[10] + a[15]*b[11];
        res[12] = a[0]*b[12] + a[4]*b[13] + a[8]*b[14] + a[12]*b[15];
        res[13] = a[1]*b[12] + a[5]*b[13] + a[9]*b[14] + a[13]*b[15];
        res[14] = a[2]*b[12] + a[6]*b[13] + a[10]*b[14] + a[14]*b[15];
        res[15] = a[3]*b[12] + a[7]*b[13] + a[11]*b[14] + a[15]*b[15];
        out[0]=res[0]; out[1]=res[1]; out[2]=res[2]; out[3]=res[3];
        out[4]=res[4]; out[5]=res[5]; out[6]=res[6]; out[7]=res[7];
        out[8]=res[8]; out[9]=res[9]; out[10]=res[10]; out[11]=res[11];
        out[12]=res[12]; out[13]=res[13]; out[14]=res[14]; out[15]=res[15];
    }

    inline void mat4_perspective(float* m, float fov, float aspect, float n, float f)
    {
        float safe_aspect = fmaxf(aspect, 0.0001f);
        float safe_fov = fmaxf(fov, 0.0001f);
        float t = tanf(safe_fov * 0.5f);
        m[0] = 1.0f / (safe_aspect * t); m[1]=0.0f; m[2]=0.0f; m[3]=0.0f;
        m[4]=0.0f; m[5] = 1.0f / t; m[6]=0.0f; m[7]=0.0f;
        m[8]=0.0f; m[9]=0.0f; m[10] = -(f + n) / (f - n); m[11] = -1.0f;
        m[12]=0.0f; m[13]=0.0f; m[14] = -(2.0f * f * n) / (f - n); m[15]=0.0f;
    }

    inline void mat4_lookAt(float* m, Vec3 eye, Vec3 center, Vec3 up)
    {
        Vec3 f = normalize({center.x - eye.x, center.y - eye.y, center.z - eye.z});
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        m[0]=s.x; m[1]=u.x; m[2]=-f.x; m[3]=0.0f;
        m[4]=s.y; m[5]=u.y; m[6]=-f.y; m[7]=0.0f;
        m[8]=s.z; m[9]=u.z; m[10]=-f.z; m[11]=0.0f;
        m[12]=-dot(s, eye); m[13]=-dot(u, eye); m[14]=dot(f, eye); m[15]=1.0f;
    }

    inline void mat4_set_translation(float* m, float x, float y, float z) { m[12] = x; m[13] = y; m[14] = z; }

    inline void mat4_translate(float* m, float x, float y, float z)
    {
        mat4_identity(m);
        mat4_set_translation(m, x, y, z);
    }

    inline void mat3_normal(float* out, const float* m)
    {
        out[0]=m[0]; out[1]=m[1]; out[2]=m[2];
        out[3]=m[4]; out[4]=m[5]; out[5]=m[6];
        out[6]=m[8]; out[7]=m[9]; out[8]=m[10];
    }

    inline void RunEdgeCaseDiagnostics()
    {
        printf("[DIAGNOSTIC] Running Edge Case Verification...\n");
        
        // Edge Case 1: Degenerate Perspective
        float P[16];
        mat4_perspective(P, 0.0f, 0.0f, 0.1f, 100.0f);
        for(int i=0; i<16; ++i) { assert(!std::isnan(P[i]) && !std::isinf(P[i])); }
        printf("[DIAGNOSTIC] Edge Case 1 (Degenerate Perspective): PASSED\n");

        // Edge Case 2: Zero Matrix Multiplication
        float T[16], Z[16], R[16];
        mat4_translate(T, 10, 20, 30);
        mat4_zero(Z);
        mat4_mul(R, T, Z);
        for(int i=0; i<16; ++i) { assert(std::abs(R[i]) < 1e-6f); }
        printf("[DIAGNOSTIC] Edge Case 2 (Zero Matrix): PASSED\n");
    }

    inline void RunMathDiagnostics()
    {
        printf("[DIAGNOSTIC] Starting Math Suite Verification...\n");
        float I[16]; mat4_identity(I);
        assert(std::abs(I[0]-1.0f)<1e-6 && std::abs(I[5]-1.0f)<1e-6 && std::abs(I[10]-1.0f)<1e-6 && std::abs(I[15]-1.0f)<1e-6);
        
        float T1[16]; mat4_translate(T1, 1, 2, 3);
        float T2[16]; mat4_translate(T2, 4, 5, 6);
        float TR[16]; mat4_mul(TR, T1, T2);
        assert(std::abs(TR[12]-5.0f)<1e-6 && std::abs(TR[13]-7.0f)<1e-6 && std::abs(TR[14]-9.0f)<1e-6);

        float P[16]; mat4_perspective(P, 1.570796f, 1.0f, 0.1f, 100.0f);
        assert(std::abs(P[0]-1.0f)<1e-6 && std::abs(P[10]-(-1.002002f))<1e-4);

        RunEdgeCaseDiagnostics();

        printf("[DIAGNOSTIC] Math Suite: PASSED\n");
    }
}

struct MeshPrimitive
{
    unsigned int vao, vbo, ebo, instanceVbo;
    int count;

    void initPlane(float size)
    {
        instanceVbo = 0;
        float h = size * 0.5f;
        float verts[] = {
            -h, -h, 0,  0, 0, 1,
             h, -h, 0,  0, 0, 1,
             h,  h, 0,  0, 0, 1,
            -h,  h, 0,  0, 0, 1
        };
        unsigned int idx[] = { 0, 1, 2, 2, 3, 0 };
        count = 6;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void initBox()
    {
        instanceVbo = 0;
        float v[] = {
            -0.5f,-0.5f, 0.5f,  0, 0, 1,   0.5f,-0.5f, 0.5f,  0, 0, 1,   0.5f, 0.5f, 0.5f,  0, 0, 1,  -0.5f, 0.5f, 0.5f,  0, 0, 1,
            -0.5f,-0.5f,-0.5f,  0, 0,-1,  -0.5f, 0.5f,-0.5f,  0, 0,-1,   0.5f, 0.5f,-0.5f,  0, 0,-1,   0.5f,-0.5f,-0.5f,  0, 0,-1,
            -0.5f, 0.5f,-0.5f,  0, 1, 0,  -0.5f, 0.5f, 0.5f,  0, 1, 0,   0.5f, 0.5f, 0.5f,  0, 1, 0,   0.5f, 0.5f,-0.5f,  0, 1, 0,
            -0.5f,-0.5f,-0.5f,  0,-1, 0,   0.5f,-0.5f,-0.5f,  0,-1, 0,   0.5f,-0.5f, 0.5f,  0,-1, 0,  -0.5f,-0.5f, 0.5f,  0,-1, 0,
             0.5f,-0.5f,-0.5f,  1, 0, 0,   0.5f, 0.5f,-0.5f,  1, 0, 0,   0.5f, 0.5f, 0.5f,  1, 0, 0,   0.5f,-0.5f, 0.5f,  1, 0, 0,
            -0.5f,-0.5f,-0.5f, -1, 0, 0,  -0.5f,-0.5f, 0.5f, -1, 0, 0,  -0.5f, 0.5f, 0.5f, -1, 0, 0,  -0.5f, 0.5f,-0.5f, -1, 0, 0
        };
        unsigned int idx[] = {
            0,1,2, 2,3,0, 4,5,6, 6,7,4, 8,9,10, 10,11,8, 12,13,14, 14,15,12, 16,17,18, 18,19,16, 20,21,22, 22,23,20
        };
        count = 36;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void initSphere(int sectors, int stacks)
    {
        ScopedTimer timer("Geometry Gen & Upload (Sphere)");
        instanceVbo = 0;
        int vcount = (sectors + 1) * (stacks + 1);
        
        float* verts = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));
        assert(verts);

        float sStep = 2.0f * 3.1415926f / (float)sectors;
        float tStep = 3.1415926f / (float)stacks;
        for(int i=0; i<=stacks; ++i)
        {
            float t = 3.1415926f / 2.0f - (float)i * tStep;
            float xy = cosf(t), z = sinf(t);
            for(int j=0; j<=sectors; ++j)
            {
                float s = (float)j * sStep;
                float x = xy * cosf(s), y = xy * sinf(s);
                int off = (i*(sectors+1) + j) * 6;
                verts[off+0]=x; verts[off+1]=y; verts[off+2]=z;
                verts[off+3]=x; verts[off+4]=y; verts[off+5]=z;
            }
        }
        int icount = sectors * stacks * 6;
        unsigned int* idx = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
        assert(idx);

        int cur = 0;
        for(int i=0; i<stacks; ++i)
        {
            int k1 = i*(sectors+1), k2 = k1 + sectors + 1;
            for(int j=0; j<sectors; ++j, ++k1, ++k2)
            {
                if(i != 0) { idx[cur++]=k1; idx[cur++]=k2; idx[cur++]=k1+1; }
                if(i != (stacks-1)) { idx[cur++]=k1+1; idx[cur++]=k2; idx[cur++]=k2+1; }
            }
        }
        count = cur;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vcount*6*sizeof(float), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount*sizeof(unsigned int), idx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void initInstanced(int maxInstanceCount, float* data)
    {
        ScopedTimer timer("Instanced VBO Init");
        glBindVertexArray(vao);
        glGenBuffers(1, &instanceVbo);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
        glBufferData(GL_ARRAY_BUFFER, maxInstanceCount * 4 * sizeof(float), data, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribDivisor(2, 1);
    }

    void updateInstanced(int count, float* data)
    {
        if(count <= 0) return;
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * 4 * sizeof(float), data);
    }

    void initFromRaw(int vcount, const float* vdata, int icount, const unsigned int* idata)
    {
        instanceVbo = 0;
        count = icount;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vcount * 6 * sizeof(float), vdata, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), idata, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void draw() const 
    { 
        glBindVertexArray(vao); 
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0); 
    }

    void drawInstanced(int instanceCount) const
    {
        if(instanceCount <= 0) return;
        glBindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, instanceCount);
    }
};

#include "AliceViewer.h"

#ifdef PHONGSHADER_RUN_TEST
PhongShader g_Shader;
MeshPrimitive g_Sphere, g_Plane;
float g_Time = 0.0f;
int g_FrameCount = 0;
const int INSTANCE_COUNT = 100000;

extern "C" void setup()
{
    // Initialize Arena if not done by main
    if(!Alice::g_Arena.memory) Alice::g_Arena.init(64 * 1024 * 1024); // 64MB

    Math::RunMathDiagnostics();
    if(!g_Shader.init()) exit(1);
    
    g_Plane.initPlane(2000.0f);
    g_Sphere.initSphere(16, 8); // Reduced detail for 100k instances

    float* offsets = (float*)Alice::g_Arena.allocate(INSTANCE_COUNT * 3 * sizeof(float));
    assert(offsets);

    int idx = 0;
    int side = 316; // 316 * 316 = 99856
    for(int i=0; i<side; ++i)
    {
        for(int j=0; j<side; ++j)
        {
            if(idx >= INSTANCE_COUNT * 3) break;
            offsets[idx++] = (float)(i - side/2) * 4.0f;
            offsets[idx++] = (float)(j - side/2) * 4.0f;
            offsets[idx++] = 1.0f;
        }
    }
    g_Sphere.initInstanced(INSTANCE_COUNT, offsets);
}

extern "C" void update(float dt) { g_Time += dt; }

extern "C" void draw()
{
    AliceViewer* av = AliceViewer::instance();
    if(!av) return;

    glEnable(GL_DEPTH_TEST);
    
    int w, h;
    glfwGetFramebufferSize(av->window, &w, &h);
    float aspect = (float)w / (float)h;

    float p[16], v[16], pv[16];
    Math::mat4_perspective(p, av->fov, aspect, 0.1f, 5000.0f);
    
    M4 viewMat = av->camera.getViewMatrix();
    for(int i=0; i<16; ++i) v[i] = viewMat.m[i];
    
    Math::mat4_mul(pv, p, v);

    float cp = cosf(av->camera.pitch), sp = sinf(av->camera.pitch);
    float cy = cosf(av->camera.yaw), sy = sinf(av->camera.yaw);
    float camX = av->camera.focusPoint.x + av->camera.distance * cp * sy;
    float camY = av->camera.focusPoint.y - av->camera.distance * cp * cy;
    float camZ = av->camera.focusPoint.z + av->camera.distance * sp;

    g_Shader.use();
    g_Shader.setLightPos(200.0f, 200.0f, 400.0f);
    g_Shader.setViewPos(camX, camY, camZ);
    g_Shader.setMaterialColor(0.6f, 0.6f, 0.6f);

    float m[16], mvp[16], n[9];
    
    // Draw Plane
    Math::mat4_identity(m);
    Math::mat4_mul(mvp, pv, m);
    Math::mat3_normal(n, m);
    g_Shader.setMVP(mvp);
    g_Shader.setModel(m);
    g_Shader.setNormalMatrix(n);
    g_Plane.draw();

    // Draw Instanced Spheres
    {
        // Only print metric every 60 frames
        bool print = (g_FrameCount % 60 == 0);
        double start = 0;
        if(print) start = glfwGetTime();

        Math::mat4_identity(m);
        Math::mat4_mul(mvp, pv, m);
        Math::mat3_normal(n, m);
        g_Shader.setMVP(mvp);
        g_Shader.setModel(m);
        g_Shader.setNormalMatrix(n);
        g_Sphere.drawInstanced(INSTANCE_COUNT);

        if(print)
        {
            double elapsed = (glfwGetTime() - start) * 1000.0;
            printf("[METRIC] Draw Call (100k instances): %.4f ms\n", elapsed);
        }
    }

    g_FrameCount++;
}
#endif
#endif
