#ifndef DEBUG_RENDER_H
#define DEBUG_RENDER_H

#include <glad/glad.h>
#include <cstdio>
#include "AliceViewer.h"
#include "PhongShader.h"

struct DebugRender
{
    unsigned int program;
    int uMVP;

    static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aInstanceData;
out vec3 vNormal;
uniform mat4 uMVP;
void main(){
    vec3 worldPos = (aPos * aInstanceData.w) + aInstanceData.xyz;
    vNormal = aNormal;
    gl_Position = uMVP * vec4(worldPos, 1.0);
})";

    static constexpr const char* fsrc = R"(#version 400 core
out vec4 FragColor;
in vec3 vNormal;
void main(){
    vec3 L = normalize(vec3(0.5, 0.5, 1.0));
    vec3 N = normalize(gl_FrontFacing ? vNormal : -vNormal);
    float d = max(dot(N, L), 0.0) * 0.5 + 0.5;
    FragColor = vec4(vec3(0.176) * d, 1.0); // Deep Charcoal #2D2D2D base
})";

    void init()
    {
        unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsrc, NULL);
        glCompileShader(vs);
        checkShader(vs, "DEBUG-VS");

        unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsrc, NULL);
        glCompileShader(fs);
        checkShader(fs, "DEBUG-FS");

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        checkProgram(program);

        uMVP = glGetUniformLocation(program, "uMVP");
    }

    void checkShader(unsigned int id, const char* type)
    {
        int status;
        glGetShaderiv(id, GL_COMPILE_STATUS, &status);
        if (!status)
        {
            char log[1024];
            glGetShaderInfoLog(id, 1024, NULL, log);
            printf("[ERROR] Debug Shader %s: %s\n", type, log);
        }
    }

    void checkProgram(unsigned int id)
    {
        int status;
        glGetProgramiv(id, GL_LINK_STATUS, &status);
        if (!status)
        {
            char log[1024];
            glGetProgramInfoLog(id, 1024, NULL, log);
            printf("[ERROR] Debug Program Link: %s\n", log);
        }
    }

    void draw(MeshPrimitive* mesh, const float* mvp)
    {
        if (!mesh || mesh->vao == 0) return;
        glUseProgram(program);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp);
        
        // Ensure location 2 is set for non-instanced if mesh doesn't have it enabled
        glVertexAttrib4f(2, 0.0f, 0.0f, 0.0f, 1.0f);
        
        mesh->draw();
    }
};

#endif
