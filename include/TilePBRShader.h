#ifndef TILE_PBR_SHADER_H
#define TILE_PBR_SHADER_H

#include <glad/glad.h>
#include <string>

namespace Alice
{
    struct TilePBRShader
    {
        GLuint program;
        GLint uRes, uLightSpaceMatrix, uLightDir, uEyePos, uInvView;
        GLint sSSAO, sAlbedo, gPos, gNorm, sShadow;

        static const char* vsrc() {
            return R"(#version 400 core
layout(location=0) in vec3 aPos;
void main(){ gl_Position = vec4(aPos, 1.0); })";
        }

        static const char* fsrc() {
            return R"(#version 400 core
out vec4 FragColor;
uniform sampler2D sSSAO;
uniform sampler2D sAlbedo;
uniform sampler2D gPos;
uniform sampler2D gNorm;
uniform sampler2D sShadow;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uInvView;
uniform vec3 uLightDir;
uniform vec3 uEyePos;
uniform vec2 uRes;

float shadowCalc(vec3 worldPos, vec3 normal) {
    vec4 lightSpacePos = uLightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    if(projCoords.z > 1.0) return 0.0;
    
    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, uLightDir)), 0.0005);
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(sShadow, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sShadow, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;        
        }    
    }
    return shadow / 9.0;
}

void main(){
    vec2 uv = gl_FragCoord.xy / uRes;
    vec4 albedo = texture(sAlbedo, uv);
    vec3 pos = texture(gPos, uv).xyz;
    vec3 norm = texture(gNorm, uv).xyz;
    float ao = texture(sSSAO, uv).r;

    if (albedo.a < 0.01) {
        FragColor = vec4(0.9, 0.9, 0.9, 1.0); 
        return;
    }

    vec3 worldPos = (uInvView * vec4(pos, 1.0)).xyz;
    float shadow = shadowCalc(worldPos, norm);

    // Lambertian
    float diff = max(dot(norm, uLightDir), 0.0);    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(uEyePos - worldPos);
    vec3 halfwayDir = normalize(uLightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    
    vec3 ambient = vec3(0.3) * albedo.rgb;
    vec3 diffuse = diff * albedo.rgb * (1.0 - shadow);
    vec3 specular = vec3(0.2) * spec * (1.0 - shadow);

    // Final color with SSAO multiplication
    vec3 finalColor = (ambient + diffuse + specular) * ao;

    // Atmospheric Fog (Exponential)
    float dist = distance(uEyePos, worldPos);
    float fogDensity = 0.00025;
    float fogFactor = exp(-dist * fogDensity);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    finalColor = mix(vec3(0.9), finalColor, fogFactor);

    FragColor = vec4(finalColor, 1.0);
})";
        }

        void init()
        {
            GLuint vs = glCreateShader(GL_VERTEX_SHADER);
            const char* v = vsrc();
            glShaderSource(vs, 1, &v, NULL);
            glCompileShader(vs);

            GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
            const char* f = fsrc();
            glShaderSource(fs, 1, &f, NULL);
            glCompileShader(fs);

            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);

            uRes = glGetUniformLocation(program, "uRes");
            uLightSpaceMatrix = glGetUniformLocation(program, "uLightSpaceMatrix");
            uInvView = glGetUniformLocation(program, "uInvView");
            uLightDir = glGetUniformLocation(program, "uLightDir");
            uEyePos = glGetUniformLocation(program, "uEyePos");
            sSSAO = glGetUniformLocation(program, "sSSAO");
            sAlbedo = glGetUniformLocation(program, "sAlbedo");
            gPos = glGetUniformLocation(program, "gPos");
            gNorm = glGetUniformLocation(program, "gNorm");
            sShadow = glGetUniformLocation(program, "sShadow");
        }
    };
}

#endif
