#define NOMINMAX
#define ALICE_FRAMEWORK
#define ALICE_VIEWER_RUN_TEST
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <opennurbs.h>
#include "AliceViewer.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "AliceMemory.h"
namespace Alice { extern LinearArena g_Arena; }

#define MAX_PRIMITIVE_BATCH 16384

// --- MVC Weak Symbols ---
#ifdef _MSC_VER
extern "C" 
{
    void def_setup() 
    {
    }
    void def_update(float dt) 
    {
    }
    void def_draw() 
    {
    }
    void def_keyPress(unsigned char k, int x, int y) 
    {
    }
    void def_mousePress(int b, int s, int x, int y) 
    {
    }
    void def_mouseMotion(int x, int y) 
    {
    }
}
#pragma comment(linker, "/alternatename:setup=def_setup")
#pragma comment(linker, "/alternatename:update=def_update")
#pragma comment(linker, "/alternatename:draw=def_draw")
#pragma comment(linker, "/alternatename:keyPress=def_keyPress")
#pragma comment(linker, "/alternatename:mousePress=def_mousePress")
#pragma comment(linker, "/alternatename:mouseMotion=def_mouseMotion")
#else
extern "C" 
{
    void __attribute__((weak)) setup() 
    {
    }
    void __attribute__((weak)) update(float dt) 
    {
    }
    void __attribute__((weak)) draw() 
    {
    }
    void __attribute__((weak)) keyPress(unsigned char k, int x, int y) 
    {
    }
    void __attribute__((weak)) mousePress(int b, int s, int x, int y) 
    {
    }
    void __attribute__((weak)) mouseMotion(int x, int y) 
    {
    }
}
#endif

// --- Internal State ---
static AliceViewer* g_instance = nullptr;
static M4 g_currentMVP;
static V3 g_currentVertexColor = { 0.5f, 0.5f, 0.5f };

NetworkStats g_NetStats;

// --- PerfTuner Implementation ---
void AliceViewer::PerfTuner::tune(float dt, double flushUs)
{
    frameDeltaMs = dt * 1000.0f;
    lastFlushTimeUs = flushUs;

    if (!enabled)
    {
        return;
    }

    const float budgetMs = 16.66f;
    if (frameDeltaMs > budgetMs || flushUs > 5000.0)
    {
        currentBatchThreshold = std::max(1024, currentBatchThreshold - 1024);
    }
    else if (frameDeltaMs < budgetMs * 0.5f && currentBatchThreshold < MAX_PRIMITIVE_BATCH)
    {
        currentBatchThreshold = std::min(MAX_PRIMITIVE_BATCH, currentBatchThreshold + 512);
    }
}

// --- Primitive Batching ---
struct PrimitiveBatch
{
    unsigned int vao, vbo;
    Vertex vertices[MAX_PRIMITIVE_BATCH];
    int count;
    int type;

    void init(int _type)
    {
        type = _type;
        count = 0;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), nullptr, GL_STREAM_DRAW);
        
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glEnableVertexAttribArray(1);
        
        glBindVertexArray(0);
    }

    void flush()
    {
        if (count == 0 || !g_instance) 
        {
            return;
        }

        double flushTime = 0.0;
        {
            double start = glfwGetTime();
            
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            
            glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vertex), vertices);

            M4 view = g_instance->camera.getViewMatrix();
            M4 proj = g_instance->makeInfiniteReversedZProjRH(g_instance->fov, 1280.0f/720.0f, g_instance->nearClip);

            glUseProgram(g_instance->shaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(g_instance->shaderProgram, "u_ModelView"), 1, 0, view.m);
            glUniformMatrix4fv(glGetUniformLocation(g_instance->shaderProgram, "u_Projection"), 1, 0, proj.m);
            GLint locAmb = glGetUniformLocation(g_instance->shaderProgram, "u_AmbientIntensity");
            GLint locDif = glGetUniformLocation(g_instance->shaderProgram, "u_DiffuseIntensity");
            glUniform1f(locAmb, g_instance->ambientIntensity);
            glUniform1f(locDif, g_instance->diffuseIntensity);

            if (type == GL_TRIANGLES)
            {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f); 
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glDrawArrays(GL_TRIANGLES, 0, count);

                glDisable(GL_POLYGON_OFFSET_FILL);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glUniform1f(locAmb, 0.0f);
                glUniform1f(locDif, 0.0f);
                glDrawArrays(GL_TRIANGLES, 0, count);

                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glUniform1f(locAmb, g_instance->ambientIntensity);
                glUniform1f(locDif, g_instance->diffuseIntensity);
            }
            else
            {
                glDrawArrays(type, 0, count);
            }
            count = 0;

            double end = glfwGetTime();
            flushTime = (end - start) * 1000000.0;
        }

        if (g_instance->tuner.enabled)
        {
            g_instance->tuner.lastFlushTimeUs = flushTime;
        }
    }

    void add(V3 p, V3 c)
    {
        if (g_instance && g_instance->m_computeAABB)
        {
            g_instance->m_sceneAABB.expand(p);
            return;
        }

        int threshold = g_instance ? g_instance->tuner.currentBatchThreshold : MAX_PRIMITIVE_BATCH;
        bool ready = (count >= threshold);
        if (type == GL_TRIANGLES)
        {
            ready &= (count % 3 == 0);
        }
        else if (type == GL_LINES)
        {
            ready &= (count % 2 == 0);
        }
        
        if (ready) 
        {
            flush();
        }
        vertices[count].pos = p;
        vertices[count].color = c;
        count++;
    }
};

static PrimitiveBatch g_pointBatch;
static PrimitiveBatch g_lineBatch;
static PrimitiveBatch g_triangleBatch;

float V3::length() const
{
    return sqrtf(x * x + y * y + z * z);
}

void V3::normalise()
{
    float l = length();
    if (l > 1e-9f) 
    { 
        x /= l; 
        y /= l; 
        z /= l; 
    }
}

void aliceColor3f(float r, float g, float b) 
{ 
    g_currentVertexColor = { r, g, b }; 
}

void backGround(float grey) 
{ 
    glClearColor(grey, grey, grey, 1.0f); 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
}

void backGround(float r, float g, float b) 
{ 
    glClearColor(r, g, b, 1.0f); 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
}

void drawLine(V3 a, V3 b) 
{ 
    g_lineBatch.add(a, g_currentVertexColor); 
    g_lineBatch.add(b, g_currentVertexColor); 
}

void drawPoint(V3 p) 
{ 
    g_pointBatch.add(p, g_currentVertexColor); 
}

extern "C" void drawTriangle(V3 a, V3 b, V3 c)
{
    g_triangleBatch.add(a, g_currentVertexColor);
    g_triangleBatch.add(b, g_currentVertexColor);
    g_triangleBatch.add(c, g_currentVertexColor);
}

void drawGrid(float size)
{
    V3 oldColor = g_currentVertexColor;
    
    for (int i = (int)-size; i <= (int)size; ++i) 
    {
        if (i == 0) 
        {
            g_currentVertexColor = { 0.2f, 0.2f, 0.2f };
        }
        else 
        {
            g_currentVertexColor = { 0.5f, 0.5f, 0.5f };
        }
        
        // Draw lines on the XY plane (z=0)
        drawLine({ (float)i, -size, 0 }, { (float)i, size, 0 });
        drawLine({ -size, (float)i, 0 }, { size, (float)i, 0 });
    }
    
    g_currentVertexColor = oldColor;
}

void alicePointSize(float size) 
{ 
    glPointSize(size); 
}

void aliceLineWidth(float width) 
{ 
    glLineWidth(width); 
}

static V3 nrm_v(V3 v) 
{ 
    v.normalise(); 
    return v; 
}

static float dot_v(V3 a, V3 b) 
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z; 
}

static V3 crs_v(V3 a, V3 b) 
{ 
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; 
}

static M4 lookAt(V3 eye, V3 target, V3 up)
{
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

static M4 perspective(float fov, float asp, float n, float f)
{
    float tn = tanf(fov * 0.5f);
    M4 r = { 0 };
    r.m[0] = 1.0f / (asp * tn);
    r.m[5] = 1.0f / tn;
    r.m[10] = -(f + n) / (f - n);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * f * n) / (f - n);
    r.m[15] = 0.0f;
    return r;
}

static M4 mult(M4 a, M4 b)
{
    M4 r = { 0 };
    for (int j = 0; j < 4; j++)
    {
        for (int i = 0; i < 4; i++)
        {
            r.m[j * 4 + i] = a.m[0 * 4 + i] * b.m[j * 4 + 0] +
                             a.m[1 * 4 + i] * b.m[j * 4 + 1] +
                             a.m[2 * 4 + i] * b.m[j * 4 + 2] +
                             a.m[3 * 4 + i] * b.m[j * 4 + 3];
        }
    }
    return r;
}

M4 AliceViewer::makeInfiniteReversedZProjRH(float fovRadians, float aspect, float zNear)
{
    float f = 1.0f / tanf(fovRadians * 0.5f);
    M4 res = { 0 };
    res.m[0] = f / aspect;
    res.m[5] = f;
    res.m[10] = 0.0f;
    res.m[11] = -1.0f;
    res.m[14] = zNear;
    res.m[15] = 0.0f;
    return res;
}

static M4 invert(const M4& m)
{
    float inv[16], det;
    const float* m_ = m.m;
    
    inv[0] = m_[5]*m_[10]*m_[15] - m_[5]*m_[11]*m_[14] - m_[9]*m_[6]*m_[15] + m_[9]*m_[7]*m_[14] + m_[13]*m_[6]*m_[11] - m_[13]*m_[7]*m_[10];
    inv[4] = -m_[4]*m_[10]*m_[15] + m_[4]*m_[11]*m_[14] + m_[8]*m_[6]*m_[15] - m_[8]*m_[7]*m_[14] - m_[12]*m_[6]*m_[11] + m_[12]*m_[7]*m_[10];
    inv[8] = m_[4]*m_[9]*m_[15] - m_[4]*m_[11]*m_[13] - m_[8]*m_[5]*m_[15] + m_[8]*m_[7]*m_[13] + m_[12]*m_[5]*m_[11] - m_[12]*m_[7]*m_[9];
    inv[12] = -m_[4]*m_[9]*m_[14] + m_[4]*m_[10]*m_[13] + m_[8]*m_[5]*m_[14] - m_[8]*m_[6]*m_[13] - m_[12]*m_[5]*m_[10] + m_[12]*m_[6]*m_[9];
    inv[1] = -m_[1]*m_[10]*m_[15] + m_[1]*m_[11]*m_[14] + m_[9]*m_[2]*m_[15] - m_[9]*m_[3]*m_[14] - m_[13]*m_[2]*m_[11] + m_[13]*m_[3]*m_[10];
    inv[5] = m_[0]*m_[10]*m_[15] - m_[0]*m_[11]*m_[14] - m_[8]*m_[2]*m_[15] + m_[8]*m_[3]*m_[14] + m_[12]*m_[2]*m_[11] - m_[12]*m_[3]*m_[10];
    inv[9] = -m_[0]*m_[9]*m_[15] + m_[0]*m_[11]*m_[13] + m_[8]*m_[1]*m_[15] - m_[8]*m_[3]*m_[13] - m_[12]*m_[1]*m_[11] + m_[12]*m_[3]*m_[9];
    inv[13] = m_[0]*m_[9]*m_[14] - m_[0]*m_[10]*m_[13] - m_[8]*m_[1]*m_[14] + m_[8]*m_[2]*m_[13] + m_[12]*m_[1]*m_[10] - m_[12]*m_[2]*m_[9];
    inv[2] = m_[1]*m_[6]*m_[15] - m_[1]*m_[7]*m_[14] - m_[5]*m_[2]*m_[15] + m_[5]*m_[3]*m_[14] + m_[13]*m_[2]*m_[7] - m_[13]*m_[3]*m_[6];
    inv[6] = -m_[0]*m_[6]*m_[15] + m_[0]*m_[7]*m_[14] + m_[4]*m_[2]*m_[15] - m_[4]*m_[3]*m_[14] - m_[12]*m_[2]*m_[7] + m_[12]*m_[3]*m_[6];
    inv[10] = m_[0]*m_[5]*m_[15] - m_[0]*m_[7]*m_[13] - m_[4]*m_[1]*m_[15] + m_[4]*m_[3]*m_[13] + m_[12]*m_[1]*m_[7] - m_[12]*m_[3]*m_[5];
    inv[14] = -m_[0]*m_[5]*m_[14] + m_[0]*m_[6]*m_[13] + m_[4]*m_[1]*m_[14] - m_[4]*m_[2]*m_[13] - m_[12]*m_[1]*m_[6] + m_[12]*m_[2]*m_[5];
    inv[3] = -m_[1]*m_[6]*m_[11] + m_[1]*m_[7]*m_[10] + m_[5]*m_[2]*m_[11] - m_[5]*m_[3]*m_[10] - m_[9]*m_[2]*m_[7] + m_[9]*m_[3]*m_[6];
    inv[7] = m_[0]*m_[6]*m_[11] - m_[0]*m_[7]*m_[10] - m_[4]*m_[2]*m_[11] + m_[4]*m_[3]*m_[10] + m_[8]*m_[2]*m_[7] - m_[8]*m_[3]*m_[6];
    inv[11] = -m_[0]*m_[5]*m_[11] + m_[0]*m_[7]*m_[9] + m_[4]*m_[1]*m_[11] - m_[4]*m_[3]*m_[9] - m_[8]*m_[1]*m_[7] + m_[8]*m_[3]*m_[5];
    inv[15] = m_[0]*m_[5]*m_[10] - m_[0]*m_[6]*m_[9] - m_[4]*m_[1]*m_[10] + m_[4]*m_[2]*m_[9] + m_[8]*m_[1]*m_[6] - m_[8]*m_[2]*m_[5];

    det = m_[0] * inv[0] + m_[1] * inv[4] + m_[2] * inv[8] + m_[3] * inv[12];
    if (det == 0) 
    {
        return m;
    }

    det = 1.0f / det;
    M4 res; 
    for (int i = 0; i < 16; i++) 
    {
        res.m[i] = inv[i] * det;
    }
    return res;
}

M4 ArcballCamera::getViewMatrix() const
{
    V3 eye; 
    float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
    eye.x = focusPoint.x + distance * cp * sy;
    eye.y = focusPoint.y - distance * cp * cy;
    eye.z = focusPoint.z + distance * sp;
    return lookAt(eye, focusPoint, { 0, 0, 1 });
}

void ArcballCamera::setBookmark(const char* name)
{
    if (strcmp(name, "Top") == 0) 
    { 
        pitch = 1.57f; 
        yaw = 0.0f; 
    }
    else if (strcmp(name, "Front") == 0) 
    { 
        pitch = 0.0f; 
        yaw = 0.0f; 
    }
    else if (strcmp(name, "Back") == 0) 
    { 
        pitch = 0.0f; 
        yaw = 3.14159f; 
    }
    else if (strcmp(name, "Left") == 0) 
    { 
        pitch = 0.0f; 
        yaw = -1.5708f; 
    }
    else if (strcmp(name, "Right") == 0) 
    { 
        pitch = 0.0f; 
        yaw = 1.5708f; 
    }
    else if (strcmp(name, "Perspective") == 0) 
    { 
        pitch = 0.6f; 
        yaw = 0.8f; 
    }
}

void ArcballCamera::update(GLFWwindow* window, float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();
    double mx, my; 
    glfwGetCursorPos(window, &mx, &my);
    float dx = (float)mx - lastMouseX; 
    float dy = (float)my - lastMouseY;
    lastMouseX = (float)mx; 
    lastMouseY = (float)my;

    if (!io.WantCaptureMouse)
    {
        bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        bool isLMB = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool isMMB = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        bool isRMB = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if (isAlt && isLMB)
        {
            yaw -= dx * 0.005f;
            pitch += dy * 0.005f;
        }

        if (isMMB)
        {
            float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
            V3 eye;
            eye.x = focusPoint.x + distance * cp * sy;
            eye.y = focusPoint.y - distance * cp * cy;
            eye.z = focusPoint.z + distance * sp;
            V3 f = nrm_v(focusPoint - eye);
            V3 r = nrm_v(crs_v(f, { 0, 0, 1 }));
            V3 u = crs_v(r, f);
            focusPoint -= (r * (dx * (distance * 0.001f))) + (u * (dy * (distance * 0.001f)));
        }

        if (isAlt && isRMB)
        {
            distance += dy * (distance * 0.005f);
        }

        distance -= io.MouseWheel * (distance * 0.1f);
        pitch = std::clamp(pitch, -1.5f, 1.5f);
        distance = std::max(1.0f, distance);
    }
}

static void key_cb(GLFWwindow* w, int k, int s, int a, int m) 
{
    ImGui_ImplGlfw_KeyCallback(w, k, s, a, m);
    if (!ImGui::GetIO().WantCaptureKeyboard && a == GLFW_PRESS) 
    {
        keyPress((unsigned char)k, 0, 0);
    }
}

static void mouse_cb(GLFWwindow* w, int b, int a, int m) 
{
    ImGui_ImplGlfw_MouseButtonCallback(w, b, a, m);
    if (!ImGui::GetIO().WantCaptureMouse) 
    { 
        double x, y; 
        glfwGetCursorPos(w, &x, &y); 
        mousePress(b, a, (int)x, (int)y); 
    }
}

static void cursor_cb(GLFWwindow* w, double x, double y) 
{
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    if (!ImGui::GetIO().WantCaptureMouse) 
    {
        mouseMotion((int)x, (int)y);
    }
}

static void scroll_cb(GLFWwindow* w, double x, double y) 
{
    ImGui_ImplGlfw_ScrollCallback(w, x, y);
}

void AliceViewer::drawTriangleArray(const V3* positions, size_t vertexCount, V3 color)
{
    size_t i = 0;
    while (i < vertexCount)
    {
        int threshold = tuner.currentBatchThreshold;
        threshold = (threshold / 3) * 3;
        
        int available = threshold - g_triangleBatch.count;
        if (available < 3)
        {
            g_triangleBatch.flush();
            available = threshold;
        }

        int toCopy = (int)std::min((size_t)available, vertexCount - i);
        toCopy = (toCopy / 3) * 3;
        
        if (toCopy == 0)
        {
            break;
        }

        for (int j = 0; j < toCopy; ++j)
        {
            g_triangleBatch.vertices[g_triangleBatch.count].pos = positions[i + j];
            g_triangleBatch.vertices[g_triangleBatch.count].color = color;
            g_triangleBatch.count++;
        }
        
        i += toCopy;
    }
}

AliceViewer* AliceViewer::instance() 
{ 
    return g_instance; 
}

V3 AliceViewer::screenToWorld(int x, int y, float planeZ)
{
    int w, h; 
    glfwGetFramebufferSize(window, &w, &h);
    float nx = (2.0f * (float)x) / (float)w - 1.0f;
    float ny = 1.0f - (2.0f * (float)y) / (float)h;
    M4 proj = makeInfiniteReversedZProjRH(fov, (float)w / (float)h, nearClip);
    M4 view = camera.getViewMatrix();
    M4 invVP = invert(mult(proj, view));

    auto unproj = [&](float ndcX, float ndcY, float ndcZ) 
    {
        float v[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4] = { 0 };
        for (int i = 0; i < 4; i++) 
        {
            for (int j = 0; j < 4; j++) 
            {
                r[i] += invVP.m[j * 4 + i] * v[j];
            }
        }
        return V3(r[0] / r[3], r[1] / r[3], r[2] / r[3]);
    };

    V3 p0 = unproj(nx, ny, 1.0f);
    V3 p1 = unproj(nx, ny, 0.0001f);
    V3 D = p1 - p0; 
    if (fabsf(D.z) < 1e-6f) 
    {
        return p0;
    }
    float t = (planeZ - p0.z) / D.z; 
    return p0 + D * t;
}

int AliceViewer::init(int argc, char** argv)
{
    if (!Alice::g_Arena.memory) Alice::g_Arena.init(512 * 1024 * 1024); // 512MB for 4K stencils
    
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--headless-capture") == 0 || strcmp(argv[i], "--headless") == 0)
        {
            m_headlessCapture = true;
        }
    }
    ON::Begin();
    g_instance = this;
    printf(">>> Alice Viewer 2 - Reversed-Z Pipeline <<<\n");
    if (!glfwInit()) 
    {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, msaaSamples);
    window = glfwCreateWindow(1280, 720, "ALICE VIEWER 2.0 - CAD RENDERING PIPELINE", 0, 0);
    if (!window) 
    {
        if (m_headlessCapture) {
            printf("[WARNING] GLFW Window Creation Failed, but continuing in headless mode.\n");
            // Create a dummy context if possible, or just skip rendering
        } else {
            printf("[FATAL] GLFW Window Creation Failed. Check OpenGL 4.5 support.\n");
            return 1;
        }
    }
    glfwMakeContextCurrent(window); 
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glClearDepth(0.0);
    glEnable(GL_MULTISAMPLE);

    glGenQueries(1, &timerQuery);
    glGenQueries(1, &resolveTimerQuery);

    IMGUI_CHECKVERSION(); 
    ImGui::CreateContext();
    if (window)
    {
        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 430 core");
        glfwSetKeyCallback(window, key_cb); 
        glfwSetMouseButtonCallback(window, mouse_cb);
        glfwSetCursorPosCallback(window, cursor_cb); 
        glfwSetScrollCallback(window, scroll_cb);
    }
    if (fov == 0.0f) fov = 0.785f; 
    camera.focusPoint = { 0, 0, 0 }; 
    camera.distance = 25.0f; 
    camera.yaw = 0.8f; 
    camera.pitch = 0.6f;
    double mx, my; 
    glfwGetCursorPos(window, &mx, &my);
    camera.lastMouseX = (float)mx; 
    camera.lastMouseY = (float)my;
    
    const char* vsSrc = 
        "#version 430 core\n"
        "layout(location=0) in vec3 a_Position;\n"
        "layout(location=1) in vec3 a_Color;\n"
        "uniform mat4 u_ModelView;\n"
        "uniform mat4 u_Projection;\n"
        "out vec3 v_ViewPos;\n"
        "out vec3 v_Color;\n"
        "void main()\n"
        "{\n"
        "    vec4 viewPos = u_ModelView * vec4(a_Position, 1.0);\n"
        "    v_ViewPos = viewPos.xyz;\n"
        "    v_Color = a_Color;\n"
        "    gl_Position = u_Projection * viewPos;\n"
        "}";
        
    const char* fsSrc = 
        "#version 430 core\n"
        "in vec3 v_ViewPos;\n"
        "in vec3 v_Color;\n"
        "out vec4 f_Color;\n"
        "layout(location = 1) out vec4 f_Seg;\n"
        "layout(location = 2) out float f_Depth;\n"
        "uniform float u_AmbientIntensity;\n"
        "uniform float u_DiffuseIntensity;\n"
        "void main()\n"
        "{\n"
        "    vec3 dx = dFdx(v_ViewPos);\n"
        "    vec3 dy = dFdy(v_ViewPos);\n"
        "    vec3 normal = normalize(cross(dx, dy));\n"
        "    vec3 viewDir = normalize(-v_ViewPos);\n"
        "    float diff = max(dot(normal, viewDir), 0.0);\n"
        "    vec3 finalColor = v_Color * (u_AmbientIntensity + (diff * u_DiffuseIntensity));\n"
        "    f_Color = vec4(finalColor, 1.0);\n"
        "    f_Seg = vec4(v_Color, 1.0);\n"
        "    // Linearized Reversed-Z Depth (Assuming 0.1 near, 1000.0 far range mapping)\n"
        "    // We use -v_ViewPos.z because view space is RH (Z is negative forward)\n"
        "    float near = 0.1;\n"
        "    float far = 1000.0;\n"
        "    float dist = -v_ViewPos.z;\n"
        "    f_Depth = clamp(1.0 - (dist - near) / (far - near), 0.0, 1.0);\n"
        "}";

    int success;
    char infoLog[512];

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER); 
    glShaderSource(vs, 1, &vsSrc, 0); 
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vs, 512, NULL, infoLog);
        printf("[FATAL] VS Compilation Failed:\n%s\n", infoLog);
        return 1;
    }
    
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER); 
    glShaderSource(fs, 1, &fsSrc, 0); 
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fs, 512, NULL, infoLog);
        printf("[FATAL] FS Compilation Failed:\n%s\n", infoLog);
        return 1;
    }
    
    shaderProgram = glCreateProgram(); 
    glAttachShader(shaderProgram, vs); 
    glAttachShader(shaderProgram, fs); 
    glLinkProgram(shaderProgram);
    
    g_pointBatch.init(GL_POINTS); 
    g_lineBatch.init(GL_LINES);
    g_triangleBatch.init(GL_TRIANGLES);
    setup(); 
    return 0;
}

void AliceViewer::run()
{
    float lastTime = (float)glfwGetTime();
    while (window && !glfwWindowShouldClose(window))
    {
#ifdef ALICE_TEST_MODE
        static int agentFrameCount = 0;
        if (!m_headlessCapture && agentFrameCount++ > 5) {
            printf("AGENT_WATCHDOG: 5 frames rendered. Manual inspection mode enabled.\n");
            // exit(0);
        }
#endif
        glfwPollEvents();
        float now = (float)glfwGetTime(), dt = now - lastTime; 
        lastTime = now;
        ImGui_ImplOpenGL3_NewFrame(); 
        ImGui_ImplGlfw_NewFrame(); 
        ImGui::NewFrame();
        
        {
            ImGui::Begin("Pipeline Parameter Sweeps");
            ImGui::Text("Domain 1: Reversed-Z");
            ImGui::SliderFloat("NearClip", &nearClip, 0.001f, 1.0f);
            
            ImGui::Separator();
            ImGui::Text("Domain 2: Shading");
            ImGui::SliderFloat("Ambient", &ambientIntensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Diffuse", &diffuseIntensity, 0.0f, 1.0f);
            ImGui::ColorEdit3("Background", &backColor.x);

            ImGui::Separator();
            ImGui::Text("Domain 3: AA Benchmarks");
            ImGui::SliderInt("MSAA Samples", &msaaSamples, 1, 8);
            if (ImGui::Button("Apply MSAA (Requires Restart)"))
            {
            }
            
            ImGui::Separator();
            ImGui::Text("Stats:");
            ImGui::Text("Frame: %.2f ms (%.1f FPS)", dt * 1000.0f, 1.0f/dt);
            
            GLuint64 gpuTime = 0;
            glGetQueryObjectui64v(timerQuery, GL_QUERY_RESULT_AVAILABLE, &gpuTime);
            glGetQueryObjectui64v(timerQuery, GL_QUERY_RESULT, &gpuTime);
            ImGui::Text("GPU Frame: %.3f ms", (double)gpuTime / 1000000.0);

            ImGui::End();
        }

        {
            ImGui::Begin("Camera Controls");
            if (ImGui::Button("Perspective")) camera.setBookmark("Perspective");
            ImGui::SameLine();
            if (ImGui::Button("Top")) camera.setBookmark("Top");
            ImGui::SameLine();
            if (ImGui::Button("Front")) camera.setBookmark("Front");
            ImGui::SameLine();
            if (ImGui::Button("Back")) camera.setBookmark("Back");
            ImGui::SameLine();
            if (ImGui::Button("Left")) camera.setBookmark("Left");
            ImGui::SameLine();
            if (ImGui::Button("Right")) camera.setBookmark("Right");
            ImGui::End();
        }

        {
            ImGui::Begin("Network Monitor");
            ImGui::Text("Active Requests: %d", g_NetStats.activeRequests);
            float usage = g_NetStats.meshMemoryTotal > 0 ? (float)g_NetStats.meshMemoryUsed / g_NetStats.meshMemoryTotal : 0.0f;
            ImGui::ProgressBar(usage, ImVec2(-1, 0), "Mesh Memory");
            ImGui::Text("Memory: %llu / %llu MB", (unsigned long long)(g_NetStats.meshMemoryUsed / (1024 * 1024)), (unsigned long long)(g_NetStats.meshMemoryTotal / (1024 * 1024)));
            ImGui::Text("API Status: %s", g_NetStats.apiConnected ? "CONNECTED" : "OFFLINE/403");
            ImGui::End();
        }

        update(dt); 
        backGround(backColor.x, backColor.y, backColor.z);
        
        if (m_headlessCapture)
        {
            static bool framed = false;
            if (!framed && m_sceneAABB.initialized)
            {
                printf("[HEADLESS] Triggering Zoom Extents...\n");
                frameScene(); 
                framed = true;
            }
        }

        camera.update(window, dt);
        int w, h; 
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) 
        {
            glViewport(0, 0, w, h);
            M4 view = camera.getViewMatrix();
            M4 proj = makeInfiniteReversedZProjRH(fov, (float)w / h, nearClip);
            
            glBeginQuery(GL_TIME_ELAPSED, timerQuery);

            glUseProgram(shaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_ModelView"), 1, 0, view.m);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_Projection"), 1, 0, proj.m);
            glUniform1f(glGetUniformLocation(shaderProgram, "u_AmbientIntensity"), ambientIntensity);
            glUniform1f(glGetUniformLocation(shaderProgram, "u_DiffuseIntensity"), diffuseIntensity);
            
            glEnable(GL_DEPTH_TEST); 
#ifdef ALICE_VIEWER_RUN_TEST
            void aliceTestDraw();
            aliceTestDraw();
#else
            draw();
#endif
            g_triangleBatch.flush();
            g_lineBatch.flush(); 
            g_pointBatch.flush();

            glEndQuery(GL_TIME_ELAPSED);
        }

        tuner.tune(dt, tuner.lastFlushTimeUs);

        ImGui::Render(); 
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        if (m_headlessCapture)
        {
            static int captureFrame = 0;
            captureFrame++;

            if (captureFrame == 100)
            {
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                size_t bufferSize = (size_t)width * height * 3;
                unsigned char* pixelBuffer = (unsigned char*)Alice::g_Arena.allocate(bufferSize);
                if (pixelBuffer)
                {
                    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixelBuffer);
                    stbi_flip_vertically_on_write(true);
                    
                    if (stbi_write_png("framebuffer.png", width, height, 3, pixelBuffer, width * 3))
                    {
                        printf("SUCCESS: Frame 100 captured\n");
                    }
                }
                
                captureHighResStencils("prod_4k");
            }

            if (captureFrame >= 105)
            {
                printf("[HEADLESS] Capture sequence complete. Manual inspection enabled.\n");
                // if (window) glfwSetWindowShouldClose(window, true);
                // else break;
            }
        }

        if (window) glfwSwapBuffers(window);
    }
}

void AliceViewer::cleanup() 
{ 
    if (window)
    {
        ImGui_ImplOpenGL3_Shutdown(); 
        ImGui_ImplGlfw_Shutdown(); 
        ImGui::DestroyContext(); 
    }
    glfwTerminate(); 
}

#ifdef ALICE_VIEWER_RUN_TEST
void aliceTestDraw();
#endif

void AliceViewer::frameScene()
{
    m_sceneAABB = AABB();
    m_computeAABB = true;
    
#ifdef ALICE_VIEWER_RUN_TEST
    aliceTestDraw();
#else
    draw();
#endif

    m_computeAABB = false;

    if (m_sceneAABB.initialized)
    {
        camera.focusPoint = m_sceneAABB.center();
        float radius = m_sceneAABB.radius();
        if (radius < 1e-3f) radius = 10.0f;
        
        // Add padding
        radius *= 1.2f;
        
        camera.distance = radius / tanf(fov * 0.5f);
        
        printf("[AliceViewer] frameScene: AABB(min:{%.2f,%.2f,%.2f}, max:{%.2f,%.2f,%.2f}) Center:{%.2f,%.2f,%.2f} Radius:%.2f -> Camera Distance:%.2f\n",
            m_sceneAABB.m_min.x, m_sceneAABB.m_min.y, m_sceneAABB.m_min.z,
            m_sceneAABB.m_max.x, m_sceneAABB.m_max.y, m_sceneAABB.m_max.z,
            camera.focusPoint.x, camera.focusPoint.y, camera.focusPoint.z,
            radius, camera.distance);
    }
    else
    {
        // printf("[AliceViewer] frameScene: No geometry detected. AABB not initialized.\n");
    }
}

void AliceViewer::captureHighResStencils(const char* prefix)
{
    if (!m_offscreen.allocated)
    {
        glGenFramebuffers(1, &m_offscreen.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_offscreen.fbo);

        glGenTextures(1, &m_offscreen.colorTex);
        glBindTexture(GL_TEXTURE_2D, m_offscreen.colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_offscreen.width, m_offscreen.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_offscreen.colorTex, 0);

        glGenTextures(1, &m_offscreen.segTex);
        glBindTexture(GL_TEXTURE_2D, m_offscreen.segTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_offscreen.width, m_offscreen.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_offscreen.segTex, 0);

        glGenTextures(1, &m_offscreen.depthR8Tex);
        glBindTexture(GL_TEXTURE_2D, m_offscreen.depthR8Tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_offscreen.width, m_offscreen.height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_offscreen.depthR8Tex, 0);

        glGenTextures(1, &m_offscreen.depthTex);
        glBindTexture(GL_TEXTURE_2D, m_offscreen.depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, m_offscreen.width, m_offscreen.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_offscreen.depthTex, 0);

        unsigned int bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        glDrawBuffers(3, bufs);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            printf("[FATAL] Offscreen FBO Incomplete\n");
            return;
        }
        m_offscreen.allocated = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_offscreen.fbo);
    glViewport(0, 0, m_offscreen.width, m_offscreen.height);
    
    // Clear targets
    glClearColor(backColor.x, backColor.y, backColor.z, 1.0f);
    glClearDepth(0.0); // Reversed-Z
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Mock segmentation clear (dark charcoal)
    GLfloat segClear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    glClearBufferfv(GL_COLOR, 1, segClear);
    // Depth clear (black/infinity)
    GLfloat depthClear[1] = { 0.0f };
    glClearBufferfv(GL_COLOR, 2, depthClear);

    M4 view = camera.getViewMatrix();
    M4 proj = makeInfiniteReversedZProjRH(fov, (float)m_offscreen.width / m_offscreen.height, nearClip);

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_ModelView"), 1, 0, view.m);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_Projection"), 1, 0, proj.m);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_AmbientIntensity"), ambientIntensity);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_DiffuseIntensity"), diffuseIntensity);
    
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);

#ifdef ALICE_VIEWER_RUN_TEST
    aliceTestDraw();
#else
    draw();
#endif
    g_triangleBatch.flush();
    g_lineBatch.flush();
    g_pointBatch.flush();

    // EXTRACTION
    size_t pixelCount = (size_t)m_offscreen.width * m_offscreen.height;
    unsigned char* colorData = (unsigned char*)Alice::g_Arena.allocate(pixelCount * 3);
    unsigned char* segData = (unsigned char*)Alice::g_Arena.allocate(pixelCount * 3);
    unsigned char* depth8 = (unsigned char*)Alice::g_Arena.allocate(pixelCount);

    if (colorData && segData && depth8)
    {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, m_offscreen.width, m_offscreen.height, GL_RGB, GL_UNSIGNED_BYTE, colorData);
        
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(0, 0, m_offscreen.width, m_offscreen.height, GL_RGB, GL_UNSIGNED_BYTE, segData);

        glReadBuffer(GL_COLOR_ATTACHMENT2);
        glReadPixels(0, 0, m_offscreen.width, m_offscreen.height, GL_RED, GL_UNSIGNED_BYTE, depth8);

        // Pixel Coverage Calculation
        size_t nonBackgroundCount = 0;
        unsigned char bgR = (unsigned char)(std::clamp(backColor.x, 0.0f, 1.0f) * 255.0f);
        unsigned char bgG = (unsigned char)(std::clamp(backColor.y, 0.0f, 1.0f) * 255.0f);
        unsigned char bgB = (unsigned char)(std::clamp(backColor.z, 0.0f, 1.0f) * 255.0f);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            if (abs((int)colorData[i * 3 + 0] - (int)bgR) > 2 || 
                abs((int)colorData[i * 3 + 1] - (int)bgG) > 2 || 
                abs((int)colorData[i * 3 + 2] - (int)bgB) > 2)
            {
                nonBackgroundCount++;
            }
        }
        float coverage = (float)nonBackgroundCount / (float)pixelCount * 100.0f;
        printf("[AliceViewer] High-res Pixel Coverage: %.4f%%\n", coverage);
        
        // Asynchronous File I/O
        int w = m_offscreen.width;
        int h = m_offscreen.height;
        std::string prefixStr = prefix;
        
        std::thread([colorData, segData, depth8, w, h, prefixStr]() {
            stbi_flip_vertically_on_write(true);
            char path[256];
            
            snprintf(path, 256, "%s_beauty.png", prefixStr.c_str());
            stbi_write_png(path, w, h, 3, colorData, w * 3);

            snprintf(path, 256, "%s_seg.png", prefixStr.c_str());
            stbi_write_png(path, w, h, 3, segData, w * 3);

            snprintf(path, 256, "%s_depth.png", prefixStr.c_str());
            stbi_write_png(path, w, h, 1, depth8, w);
            
            printf("[AliceViewer] Asynchronous capture complete: %s\n", prefixStr.c_str());
        }).detach();
    }
    else
    {
        printf("[ERROR] Failed to allocate extraction buffers. color:%p seg:%p depth:%p\n", colorData, segData, depth8);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


#ifdef ALICE_VIEWER_RUN_TEST
void runAliceTests()
{
    printf("\n[TEST] Starting AliceViewer Micro-Framework Tests...\n");

    {
        V3 a(1, 2, 3), b(4, 5, 6);
        V3 c = a + b;
        ALICE_ASSERT(c.x == 5 && c.y == 7 && c.z == 9);
        
        V3 d = b - a;
        ALICE_ASSERT(d.x == 3 && d.y == 3 && d.z == 3);

        V3 e = a * 2.0f;
        ALICE_ASSERT(e.x == 2 && e.y == 4 && e.z == 6);

        float len = V3(3, 0, 4).length();
        ALICE_EXPECT_NEAR(len, 5.0f, 1e-5f);

        V3 n(10, 0, 0);
        n.normalise();
        ALICE_EXPECT_NEAR(n.x, 1.0f, 1e-5f);
        ALICE_EXPECT_NEAR(n.y, 0.0f, 1e-5f);
    }

    {
        M4 id = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        M4 a = { 1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16 };
        M4 res = mult(a, id);
        for(int i=0; i<16; ++i)
        {
            ALICE_EXPECT_NEAR(res.m[i], a.m[i], 1e-5f);
        }
    }

    if (g_instance)
    {
        // Save state
        AABB oldAABB = g_instance->m_sceneAABB;
        ArcballCamera oldCam = g_instance->camera;

        V3 world = g_instance->screenToWorld(640, 360, 0.0f);
        ALICE_EXPECT_NEAR(world.z, 0.0f, 1e-3f);

        // Test Zoom Extents
        printf("[TEST] Testing frameScene()...\n");
        g_instance->frameScene();
        ALICE_ASSERT(g_instance->m_sceneAABB.initialized);

        // EDGE CASE: Zero Geometry
        printf("[TEST] Edge Case: Zero Geometry...\n");
        g_instance->m_sceneAABB = AABB(); 
        float radius = g_instance->m_sceneAABB.radius();
        ALICE_ASSERT(radius >= 0.1f);
        ALICE_ASSERT(!std::isnan(radius));

        // EDGE CASE: Flat / Coplanar Geometry (Z=0)
        printf("[TEST] Edge Case: Flat Geometry (Z=0)...\n");
        AABB flatAABB;
        flatAABB.expand({-10, -10, 0});
        flatAABB.expand({10, 10, 0});
        float flatRadius = flatAABB.radius();
        ALICE_ASSERT(flatRadius > 0.0f);
        ALICE_ASSERT(!std::isinf(flatRadius));

        // EDGE CASE: Extreme Coordinates
        printf("[TEST] Edge Case: Extreme Coordinates...\n");
        AABB extremeAABB;
        extremeAABB.expand({1e6f, 1e6f, 1e6f});
        ALICE_ASSERT(extremeAABB.m_min.x == 1e6f);
        ALICE_ASSERT(extremeAABB.m_max.x == 1e6f);

        // Restore state
        g_instance->m_sceneAABB = oldAABB;
        g_instance->camera = oldCam;

        // Test High-Res Capture
        printf("[TEST] Testing captureHighResStencils()...\n");
        g_instance->captureHighResStencils("test_capture");
    }

    printf("[TEST] All tests completed.\n\n");
}

static void drawBox(V3 min, V3 max, V3 color)
{
    aliceColor3f(color.x, color.y, color.z);
    V3 c000 = { min.x, min.y, min.z };
    V3 c100 = { max.x, min.y, min.z };
    V3 c010 = { min.x, max.y, min.z };
    V3 c110 = { max.x, max.y, min.z };
    V3 c001 = { min.x, min.y, max.z };
    V3 c101 = { max.x, min.y, max.z };
    V3 c011 = { min.x, max.y, max.z };
    V3 c111 = { max.x, max.y, max.z };

    // Front
    drawTriangle(c000, c100, c110);
    drawTriangle(c000, c110, c010);
    // Back
    drawTriangle(c001, c111, c101);
    drawTriangle(c001, c011, c111);
    // Left
    drawTriangle(c000, c010, c011);
    drawTriangle(c000, c011, c001);
    // Right
    drawTriangle(c100, c101, c111);
    drawTriangle(c100, c111, c110);
    // Top
    drawTriangle(c010, c110, c111);
    drawTriangle(c010, c111, c011);
    // Bottom
    drawTriangle(c000, c001, c101);
    drawTriangle(c000, c101, c100);
}

void aliceTestDraw()
{
    static bool testsRun = false;
    static std::vector<float> vbo;
    static std::vector<uint32_t> ebo;
    static bool loaded = false;

    if (!testsRun)
    {
        testsRun = true;
        runAliceTests();
    }

    if (!loaded) {
        const char* cachePath = "./build/bin/cesium_mesh_cache.bin";
        FILE* f = fopen(cachePath, "rb");
        if (!f) {
            cachePath = "./cesium_mesh_cache.bin";
            f = fopen(cachePath, "rb");
        }
        if (f) {
            uint32_t vCount, iCount;
            if (fread(&vCount, sizeof(uint32_t), 1, f) == 1 &&
                fread(&iCount, sizeof(uint32_t), 1, f) == 1) {
                vbo.resize(vCount);
                ebo.resize(iCount);
                fread(vbo.data(), sizeof(float), vCount, f);
                fread(ebo.data(), sizeof(uint32_t), iCount, f);
                loaded = true;
                printf("[Alice] Loaded cached mesh from %s: %u indices\n", cachePath, iCount);
            }
            fclose(f);
        } else {
            printf("[Alice] [ERROR] Could not find cesium_mesh_cache.bin\n");
        }
    }

    if (loaded) {
        aliceColor3f(0.8f, 0.8f, 0.8f);
        for (size_t i = 0; i < ebo.size(); i += 3) {
            uint32_t i0 = ebo[i];
            uint32_t i1 = ebo[i+1];
            uint32_t i2 = ebo[i+2];
            // vbo has 6 floats per vertex: pos(3), normal(3)
            V3 v0(vbo[i0*6], vbo[i0*6+1], vbo[i0*6+2]);
            V3 v1(vbo[i1*6], vbo[i1*6+1], vbo[i1*6+2]);
            V3 v2(vbo[i2*6], vbo[i2*6+1], vbo[i2*6+2]);
            drawTriangle(v0, v1, v2);
        }
    }
}
#endif
