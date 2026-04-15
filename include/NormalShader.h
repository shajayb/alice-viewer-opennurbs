#ifndef NORMAL_SHADER_H
#define NORMAL_SHADER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>

#include "AliceMath.h"
#include "MeshPrimitive.h"
#include "AliceViewer.h"

struct NormalShader
{
    unsigned int program;
    int uMVP, uNormalMatrix, uLightDir, uLightColor;

    static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aInstanceOffset;
out vec3 Normal;
uniform mat4 uMVP;
uniform mat3 uNormalMatrix;
void main(){
    vec3 worldPos = aPos + aInstanceOffset;
    Normal = normalize(uNormalMatrix * aNormal);
    gl_Position = uMVP * vec4(worldPos, 1.0);
})";

    static constexpr const char* fsrc = R"(#version 400 core
out vec4 FragColor;
in vec3 Normal;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
void main(){
    vec3 n = normalize(Normal);
    vec3 l = normalize(uLightDir);
    float diffuse = max(dot(n, l), 0.0);
    vec3 color = (diffuse * 0.8 + 0.2) * uLightColor;
    FragColor = vec4(color, 1.0);
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
        uNormalMatrix = glGetUniformLocation(program, "uNormalMatrix");
        uLightDir = glGetUniformLocation(program, "uLightDir");
        uLightColor = glGetUniformLocation(program, "uLightColor");
        return true;
    }

    void use() const { glUseProgram(program); }
    void setMVP(const float* m) const { glUniformMatrix4fv(uMVP, 1, GL_FALSE, m); }
    void setNormalMatrix(const float* m) const { glUniformMatrix3fv(uNormalMatrix, 1, GL_FALSE, m); }
    void setLightDir(float x, float y, float z) const { glUniform3f(uLightDir, x, y, z); }
    void setLightColor(float r, float g, float b) const { glUniform3f(uLightColor, r, g, b); }
};

#ifdef NORMAL_SHADER_RUN_TEST
static NormalShader g_NormalShader;
static MeshPrimitive g_Sphere, g_Box, g_Plane;

extern "C" void setup()
{
    if(!Alice::g_Arena.memory) Alice::g_Arena.init(64 * 1024 * 1024);
    if(!g_NormalShader.init()) exit(1);
    
    g_Plane.initPlane(20.0f);
    g_Box.initBox();
    g_Sphere.initSphere(32, 16);

    AliceViewer* av = AliceViewer::instance();
    if(av)
    {
        av->camera.focusPoint = {0,0,0};
        av->camera.distance = 10.0f;
    }
}

extern "C" void update(float dt) { (void)dt; }

extern "C" void draw()
{
    AliceViewer* av = AliceViewer::instance();
    if(!av) return;

    glEnable(GL_DEPTH_TEST);
    backGround(0.9f);
    drawGrid(10.0f);

    int w, h;
    glfwGetFramebufferSize(av->window, &w, &h);
    float aspect = (float)w / (float)h;

    float p[16], v[16], pv[16];
    Math::mat4_perspective(p, av->fov, aspect, 0.1f, 100.0f);
    M4 viewMat = av->camera.getViewMatrix();
    for(int i=0; i<16; ++i) v[i] = viewMat.m[i];
    Math::mat4_mul(pv, p, v);

    g_NormalShader.use();
    g_NormalShader.setLightDir(0.5f, 1.0f, 0.2f);
    g_NormalShader.setLightColor(1.0f, 1.0f, 1.0f);

    float m[16], mvp[16], n[9];
    
    // Draw Plane
    Math::mat4_identity(m);
    Math::mat4_mul(mvp, pv, m);
    Math::mat3_normal(n, m);
    g_NormalShader.setMVP(mvp);
    g_NormalShader.setNormalMatrix(n);
    g_Plane.draw();

    // Draw Box
    Math::mat4_identity(m);
    // Translate box
    m[12] = -2.0f; m[13] = 0.0f; m[14] = 1.0f;
    Math::mat4_mul(mvp, pv, m);
    Math::mat3_normal(n, m);
    g_NormalShader.setMVP(mvp);
    g_NormalShader.setNormalMatrix(n);
    g_Box.draw();

    // Draw Sphere
    Math::mat4_identity(m);
    // Translate sphere
    m[12] = 2.0f; m[13] = 0.0f; m[14] = 1.0f;
    Math::mat4_mul(mvp, pv, m);
    Math::mat3_normal(n, m);
    g_NormalShader.setMVP(mvp);
    g_NormalShader.setNormalMatrix(n);
    g_Sphere.draw();
}
#endif

#endif
