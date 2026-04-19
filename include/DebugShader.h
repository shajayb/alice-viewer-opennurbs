#ifndef DEBUG_SHADER_H
#define DEBUG_SHADER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>

struct DebugShader
{
    unsigned int program;
    int uMVP;

    static constexpr const char* vsrc = R"(#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

    static constexpr const char* fsrc = R"(#version 400 core
out vec4 FragColor;
void main(){
    FragColor = vec4(0.8, 0.8, 0.8, 1.0); // Flat light grey
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
        return true;
    }

    void use() const { glUseProgram(program); }
    void setMVP(const float* m) const { glUniformMatrix4fv(uMVP, 1, GL_FALSE, m); }
};

#endif
