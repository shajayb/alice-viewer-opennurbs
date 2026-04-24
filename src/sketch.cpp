#define ALICE_TEST_CESIUM_GEPR

#ifdef ALICE_TEST_CESIUM_GEPR
#include "AliceMemory.h"
namespace Alice { 
    LinearArena g_Arena; 
    LinearArena g_JsonArena;
}
#include "cesium_gepr_and_osm.h"
#else

#include "AliceViewer.h"
#include "AliceMemory.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace Alice { 
    LinearArena g_Arena; 
    LinearArena g_JsonArena;
}

static int frameCount = 0;
static int captureCount = 0;

static unsigned int shadowFBO = 0;
static unsigned int shadowDepthTex = 0;
static unsigned int shadowProgram = 0;
static unsigned int defaultProgram = 0;
static const int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

extern "C" void aliceTriangleBatchFlush();

M4 makeOrtho(float l, float r, float b, float t, float n, float f) {
    M4 m = { 0 };
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = 1.0f / (f - n); // RH: -n -> 1, -f -> 0
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = f / (f - n);
    m.m[15] = 1.0f;
    return m;
}

static V3 nrm_v(V3 v) {
    float l = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (l > 1e-9f) { v.x /= l; v.y /= l; v.z /= l; }
    return v;
}

static V3 crs_v(V3 a, V3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

static float dot_v(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static M4 lookAt_v(V3 eye, V3 target, V3 up) {
    V3 f = nrm_v(target - eye);
    V3 s = nrm_v(crs_v(f, up));
    V3 v = crs_v(s, f);
    M4 r = { 0 };
    r.m[0] = s.x; r.m[1] = v.x; r.m[2] = -f.x; r.m[3] = 0.0f;
    r.m[4] = s.y; r.m[5] = v.y; r.m[6] = -f.y; r.m[7] = 0.0f;
    r.m[8] = s.z; r.m[9] = v.z; r.m[10] = -f.z; r.m[11] = 0.0f;
    r.m[12] = -dot_v(s, eye); r.m[13] = -dot_v(v, eye); r.m[14] = dot_v(f, eye); r.m[15] = 1.0f;
    return r;
}

static M4 mult_v(M4 a, M4 b) {
    M4 r = { 0 };
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            r.m[j * 4 + i] = a.m[0 * 4 + i] * b.m[j * 4 + 0] +
                             a.m[1 * 4 + i] * b.m[j * 4 + 1] +
                             a.m[2 * 4 + i] * b.m[j * 4 + 2] +
                             a.m[3 * 4 + i] * b.m[j * 4 + 3];
        }
    }
    return r;
}

extern "C" {
    void setup() {
        AliceViewer* viewer = AliceViewer::instance();
        viewer->m_manualMatrices = false;
        viewer->camera.distance = 45.0f;
        viewer->camera.pitch = 0.6f;
        viewer->camera.yaw = 0.785f;
        viewer->camera.focusPoint = {0, 0, 0};
        viewer->camera.setBookmark("perspective");
        viewer->backColor = {1, 1, 1}; // White
        viewer->camera.focusPoint = {0, 0, 0};
        
        defaultProgram = viewer->shaderProgram;

        // Shadow FBO
        glGenFramebuffers(1, &shadowFBO);
        glGenTextures(1, &shadowDepthTex);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f }; // In Reversed-Z, 0 is far
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Custom Shadow Shader
        const char* vsSrc = 
            "#version 430 core\n"
            "layout(location=0) in vec3 a_Position;\n"
            "layout(location=1) in vec3 a_Color;\n"
            "uniform mat4 u_ModelView;\n"
            "uniform mat4 u_Projection;\n"
            "uniform mat4 u_LightSpaceMatrix;\n"
            "out vec3 v_ViewPos;\n"
            "out vec3 v_Color;\n"
            "out vec4 v_LightSpacePos;\n"
            "void main() {\n"
            "    vec4 viewPos = u_ModelView * vec4(a_Position, 1.0);\n"
            "    v_ViewPos = viewPos.xyz;\n"
            "    v_Color = a_Color;\n"
            "    v_LightSpacePos = u_LightSpaceMatrix * vec4(a_Position, 1.0);\n"
            "    gl_Position = u_Projection * viewPos;\n"
            "}";

        const char* fsSrc = 
            "#version 430 core\n"
            "in vec3 v_ViewPos;\n"
            "in vec3 v_Color;\n"
            "in vec4 v_LightSpacePos;\n"
            "out vec4 f_Color;\n"
            "layout(location = 1) out vec4 f_Seg;\n"
            "layout(location = 2) out float f_Depth;\n"
            "uniform float u_AmbientIntensity;\n"
            "uniform float u_DiffuseIntensity;\n"
            "uniform sampler2D u_ShadowMap;\n"
            "uniform vec3 u_LightDirView;\n"
            "void main() {\n"
            "    vec3 dx = dFdx(v_ViewPos);\n"
            "    vec3 dy = dFdy(v_ViewPos);\n"
            "    vec3 normal = normalize(cross(dx, dy));\n"
            "    float diff = max(dot(normal, normalize(u_LightDirView)), 0.0);\n"
            "    vec3 projCoords = v_LightSpacePos.xyz / v_LightSpacePos.w;\n"
            "    projCoords.xy = projCoords.xy * 0.5 + 0.5;\n"
            "    float shadow = 0.0;\n"
            "    if (projCoords.z >= 0.0 && projCoords.z <= 1.0) {\n"
            "        float closestDepth = texture(u_ShadowMap, projCoords.xy).r;\n"
            "        shadow = projCoords.z < closestDepth - 0.002 ? 1.0 : 0.0;\n"
            "    }\n"
            "    vec3 finalColor = v_Color * (u_AmbientIntensity + (1.0 - shadow) * (diff * u_DiffuseIntensity));\n"
            "    f_Color = vec4(finalColor, 1.0);\n"
            "    f_Seg = vec4(v_Color, 1.0);\n"
            "    float near = 0.1;\n"
            "    float far = 1000.0;\n"
            "    float dist = -v_ViewPos.z;\n"
            "    f_Depth = clamp(1.0 - (dist - near) / (far - near), 0.0, 1.0);\n"
            "}";

        unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsSrc, NULL);
        glCompileShader(vs);
        unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsSrc, NULL);
        glCompileShader(fs);
        shadowProgram = glCreateProgram();
        glAttachShader(shadowProgram, vs);
        glAttachShader(shadowProgram, fs);
        glLinkProgram(shadowProgram);
        
        viewer->shaderProgram = shadowProgram;
    }

    void update(float dt) {
        frameCount++;
        AliceViewer* viewer = AliceViewer::instance();
    }

    void draw() {
        AliceViewer* viewer = AliceViewer::instance();
        
        M4 camView = viewer->m_currentView;
        M4 camProj = viewer->m_currentProj;
        
        // Define Light
        V3 lightPos = { -15, 30, 15 };
        M4 lightView = lookAt_v(lightPos, {0,0,0}, {0,0,1});
        M4 lightProj = makeOrtho(-25, 25, -25, 25, 0.1f, 100.0f);
        M4 lightSpaceMatrix = mult_v(lightProj, lightView);

        auto renderScene = []() {
            // Cube (Red) 5x5x5
            aliceColor3f(1.0f, 0.0f, 0.0f);
            V3 v0(-2.5, -2.5, 0); V3 v1( 2.5, -2.5, 0); V3 v2( 2.5,  2.5, 0); V3 v3(-2.5,  2.5, 0);
            V3 v4(-2.5, -2.5, 5); V3 v5( 2.5, -2.5, 5); V3 v6( 2.5,  2.5, 5); V3 v7(-2.5,  2.5, 5);
            drawTriangle(v4, v5, v6); drawTriangle(v4, v6, v7); // Front
            drawTriangle(v0, v2, v1); drawTriangle(v0, v3, v2); // Back
            drawTriangle(v3, v6, v2); drawTriangle(v3, v7, v6); // Top
            drawTriangle(v0, v1, v5); drawTriangle(v0, v5, v4); // Bottom
            drawTriangle(v1, v2, v6); drawTriangle(v1, v6, v5); // Right
            drawTriangle(v0, v4, v7); drawTriangle(v0, v7, v3); // Left
            
            // Floor (White) 25x25
            aliceColor3f(1.0f, 1.0f, 1.0f);
            V3 f0(-12.5, -12.5, 0); V3 f1( 12.5, -12.5, 0); V3 f2( 12.5,  12.5, 0); V3 f3(-12.5,  12.5, 0);
            drawTriangle(f0, f1, f2); drawTriangle(f0, f2, f3);

            // Sphere (Red)
            aliceColor3f(1.0f, 0.0f, 0.0f);
            int res = 16;
            float rad = 2.5f;
            V3 center(0, 0, 9.0f);
            for (int i=0; i<res; i++) {
                float lat0 = 3.14159f * (-0.5f + (float)(i) / res);
                float z0 = sinf(lat0); float zr0 = cosf(lat0);
                float lat1 = 3.14159f * (-0.5f + (float)(i+1) / res);
                float z1 = sinf(lat1); float zr1 = cosf(lat1);
                for (int j=0; j<res; j++) {
                    float lng = 2 * 3.14159f * (float)(j) / res;
                    float x = cosf(lng); float y = sinf(lng);
                    float lng1 = 2 * 3.14159f * (float)(j+1) / res;
                    float x1 = cosf(lng1); float y1 = sinf(lng1);
                    V3 p0(x*zr0*rad+center.x, y*zr0*rad+center.y, z0*rad+center.z);
                    V3 p1(x*zr1*rad+center.x, y*zr1*rad+center.y, z1*rad+center.z);
                    V3 p2(x1*zr0*rad+center.x, y1*zr0*rad+center.y, z0*rad+center.z);
                    V3 p3(x1*zr1*rad+center.x, y1*zr1*rad+center.y, z1*rad+center.z);
                    drawTriangle(p0, p2, p1);
                    drawTriangle(p2, p3, p1);
                }
            }
        };

        // Pass 1: Shadow Map
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glClearDepth(0.0);
        glClear(GL_DEPTH_BUFFER_BIT);
        
        viewer->m_currentView = lightView;
        viewer->m_currentProj = lightProj;
        viewer->shaderProgram = defaultProgram; 
        
        renderScene();
        aliceTriangleBatchFlush();
        
        // Pass 2: Screen
        if (viewer->m_isRenderingOffscreen) {
            glBindFramebuffer(GL_FRAMEBUFFER, viewer->m_offscreen.fbo);
            glViewport(0, 0, viewer->m_offscreen.width, viewer->m_offscreen.height);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            int w, h;
            glfwGetFramebufferSize(viewer->window, &w, &h);
            glViewport(0, 0, w, h);
        }

        viewer->m_currentView = camView;
        viewer->m_currentProj = camProj;
        viewer->shaderProgram = shadowProgram;

        glUseProgram(shadowProgram);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTex);
        glUniform1i(glGetUniformLocation(shadowProgram, "u_ShadowMap"), 1);
        glUniformMatrix4fv(glGetUniformLocation(shadowProgram, "u_LightSpaceMatrix"), 1, 0, lightSpaceMatrix.m);
        glUniform1f(glGetUniformLocation(shadowProgram, "u_AmbientIntensity"), 0.3f);
        glUniform1f(glGetUniformLocation(shadowProgram, "u_DiffuseIntensity"), 0.7f);
        
        V3 ldw = nrm_v(lightPos);
        V3 ldv;
        ldv.x = camView.m[0]*ldw.x + camView.m[4]*ldw.y + camView.m[8]*ldw.z;
        ldv.y = camView.m[1]*ldw.x + camView.m[5]*ldw.y + camView.m[9]*ldw.z;
        ldv.z = camView.m[2]*ldw.x + camView.m[6]*ldw.y + camView.m[10]*ldw.z;
        ldv = nrm_v(ldv);
        glUniform3f(glGetUniformLocation(shadowProgram, "u_LightDirView"), ldv.x, ldv.y, ldv.z);

        renderScene();
        aliceTriangleBatchFlush();

        // Restore viewer state so it doesn't crash in run()'s final flush
        viewer->shaderProgram = shadowProgram;
    }

    void keyPress(unsigned char key, int x, int y) {
        if (key == 's' || key == 'S') {
            char filename[256];
            snprintf(filename, sizeof(filename), "framebuffer_%03d", captureCount++);
            AliceViewer::instance()->captureHighResStencils(filename);
        }
    }
    void mousePress(int button, int state, int x, int y) {}
    void mouseMotion(int x, int y) {}
}
#endif // ALICE_TEST_CESIUM_GEPR
