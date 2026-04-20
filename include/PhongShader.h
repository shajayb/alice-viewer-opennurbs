#ifndef PHONG_SHADER_H
#define PHONG_SHADER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <cstdlib>

#include "AliceMemory.h"
#include "MeshPrimitive.h"

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
layout(location=2) in vec2 aTexCoord;
layout(location=3) in vec3 aInstanceOffset;
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

#ifdef PHONGSHADER_RUN_TEST
#include "AliceViewer.h"
PhongShader g_Shader;
MeshPrimitive g_Sphere, g_Plane;
float g_Time = 0.0f;
int g_FrameCount = 0;
const int INSTANCE_COUNT = 100000;

extern "C" void setup()
{
    if(!Alice::g_Arena.memory) Alice::g_Arena.init(64 * 1024 * 1024);
    if(!g_Shader.init()) return;
    
    g_Plane.initPlane(2000.0f);
    g_Sphere.initSphere(16, 8);

    float* offsets = (float*)Alice::g_Arena.allocate(INSTANCE_COUNT * 4 * sizeof(float));
    assert(offsets);

    int idx = 0;
    int side = 316;
    for(int i=0; i<side; ++i)
    {
        for(int j=0; j<side; ++j)
        {
            if(idx >= INSTANCE_COUNT * 4) break;
            offsets[idx++] = (float)(i - side/2) * 4.0f;
            offsets[idx++] = (float)(j - side/2) * 4.0f;
            offsets[idx++] = 1.0f;
            offsets[idx++] = 1.0f; // Scale
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
    Math::mat4_identity(m);
    Math::mat4_mul(mvp, pv, m);
    Math::mat3_normal(n, m);
    g_Shader.setMVP(mvp);
    g_Shader.setModel(m);
    g_Shader.setNormalMatrix(n);
    g_Plane.draw();

    g_Sphere.drawInstanced(INSTANCE_COUNT);
    g_FrameCount++;
}
#endif

#endif
