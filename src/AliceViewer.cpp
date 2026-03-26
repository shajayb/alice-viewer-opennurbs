#define ALICE_FRAMEWORK
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "AliceViewer.h"
#include <cstdio>
#include <iostream>
#include <string>

// --- MVC Weak Symbols ---
#ifdef _MSC_VER
    extern "C" {
        void def_setup() {}
        void def_update(float dt) {}
        void def_draw() {}
        void def_keyPress(unsigned char k, int x, int y) {}
        void def_mousePress(int b, int s, int x, int y) {}
        void def_mouseMotion(int x, int y) {}
    }
    #pragma comment(linker, "/alternatename:setup=def_setup")
    #pragma comment(linker, "/alternatename:update=def_update")
    #pragma comment(linker, "/alternatename:draw=def_draw")
    #pragma comment(linker, "/alternatename:keyPress=def_keyPress")
    #pragma comment(linker, "/alternatename:mousePress=def_mousePress")
    #pragma comment(linker, "/alternatename:mouseMotion=def_mouseMotion")
#else
    extern "C" {
        void __attribute__((weak)) setup() {}
        void __attribute__((weak)) update(float dt) {}
        void __attribute__((weak)) draw() {}
        void __attribute__((weak)) keyPress(unsigned char k, int x, int y) {}
        void __attribute__((weak)) mousePress(int b, int s, int x, int y) {}
        void __attribute__((weak)) mouseMotion(int x, int y) {}
    }
#endif

// --- Internal State ---
static AliceViewer* g_instance = nullptr;
static M4 g_currentMVP;
static float g_currentColor[3] = { 0.17f, 0.17f, 0.17f };

// --- Primitive Batching ---
#define MAX_PRIMITIVE_BATCH 16384
struct PrimitiveBatch
{
    unsigned int vao, vbo;
    V3 vertices[MAX_PRIMITIVE_BATCH];
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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    void flush()
    {
        if (count == 0 || !g_instance) return;
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(V3), vertices);
        glUseProgram(g_instance->shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(g_instance->shaderProgram, "mvp"), 1, 0, g_currentMVP.m);
        glUniform3fv(glGetUniformLocation(g_instance->shaderProgram, "uColor"), 1, g_currentColor);
        glDrawArrays(type, 0, count);
        count = 0;
    }

    void add(V3 p)
    {
        if (count >= MAX_PRIMITIVE_BATCH - 1) flush();
        vertices[count++] = p;
    }
};

static PrimitiveBatch g_pointBatch;
static PrimitiveBatch g_lineBatch;

// --- Alice API Implementations ---

void aliceColor3f(float r, float g, float b) 
{ 
    g_lineBatch.flush(); g_pointBatch.flush();
    g_currentColor[0] = r; g_currentColor[1] = g; g_currentColor[2] = b; 
}
void backGround(float grey) { glClearColor(grey, grey, grey, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
void backGround(float r, float g, float b) { glClearColor(r, g, b, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
void drawLine(V3 a, V3 b) { g_lineBatch.add(a); g_lineBatch.add(b); }
void drawPoint(V3 p) { g_pointBatch.add(p); }
void drawGrid(float size)
{
    float old[3] = { g_currentColor[0], g_currentColor[1], g_currentColor[2] };
    for (int i = (int)-size; i <= (int)size; ++i) {
        if (i == 0) aliceColor3f(0.2f, 0.2f, 0.2f);
        else aliceColor3f(0.5f, 0.5f, 0.5f);
        drawLine({ (float)i, -size, 0 }, { (float)i, size, 0 });
        drawLine({ -size, (float)i, 0 }, { size, (float)i, 0 });
    }
    aliceColor3f(old[0], old[1], old[2]);
}
void alicePointSize(float size) { glPointSize(size); }
void aliceLineWidth(float width) { glLineWidth(width); }

// --- Internal Math ---
static V3 nrm_v(V3 v) { v.normalise(); return v; }
static float dot_v(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 crs_v(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

static M4 lookAt(V3 eye, V3 target, V3 up)
{
    V3 f = nrm_v(target - eye), s = nrm_v(crs_v(f, up)), v = crs_v(s, f);
    M4 r = { 0 };
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z; r.m[1] = v.x; r.m[5] = v.y; r.m[9] = v.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[12] = -dot_v(s, eye); r.m[13] = -dot_v(v, eye);
    r.m[14] = dot_v(f, eye); r.m[15] = 1.0f; return r;
}
static M4 perspective(float fov, float asp, float n, float f)
{
    float tn = tanf(fov * 0.5f); M4 r = { 0 };
    r.m[0] = 1.0f / (asp * tn); r.m[5] = 1.0f / tn; r.m[10] = -(f + n) / (f - n);
    r.m[11] = -1.0f; r.m[14] = -(2.0f * f * n) / (f - n); return r;
}
static M4 mult(M4 a, M4 b)
{
    M4 r = { 0 };
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k)
        r.m[j * 4 + i] += a.m[k * 4 + i] * b.m[j * 4 + k];
    return r;
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
    if (det == 0) return m;
    det = 1.0f / det;
    M4 res; for (int i = 0; i < 16; i++) res.m[i] = inv[i] * det;
    return res;
}

M4 ArcballCamera::getViewMatrix() const
{
    V3 eye; float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
    eye.x = focusPoint.x + distance * cp * sy; eye.y = focusPoint.y - distance * cp * cy; eye.z = focusPoint.z + distance * sp;
    return lookAt(eye, focusPoint, { 0, 0, 1 });
}

void ArcballCamera::setBookmark(const char* name)
{
    std::string s = name;
    if (s == "Top") { pitch = 1.57f; yaw = 0.0f; }
    else if (s == "Front") { pitch = 0.0f; yaw = 0.0f; }
    else if (s == "Back") { pitch = 0.0f; yaw = 3.14159f; }
    else if (s == "Left") { pitch = 0.0f; yaw = -1.5708f; }
    else if (s == "Right") { pitch = 0.0f; yaw = 1.5708f; }
    else if (s == "Perspective") { pitch = 0.6f; yaw = 0.8f; }
}

void ArcballCamera::update(GLFWwindow* window, float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();
    double mx, my; glfwGetCursorPos(window, &mx, &my);
    float dx = (float)mx - lastMouseX; float dy = (float)my - lastMouseY;
    lastMouseX = (float)mx; lastMouseY = (float)my;

    if (!io.WantCaptureMouse)
    {
        bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        bool isLMB = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool isMMB = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        bool isRMB = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        // Orbit: ALT + LMB
        if (isAlt && isLMB)
        {
            yaw -= dx * 0.005f;
            pitch += dy * 0.005f;
        }

        // Pan: MMB
        if (isMMB)
        {
            float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
            V3 eye = { focusPoint.x + distance * cp * sy, focusPoint.y - distance * cp * cy, focusPoint.z + distance * sp };
            V3 f = nrm_v(focusPoint - eye), r = nrm_v(crs_v(f, { 0, 0, 1 })), u = crs_v(r, f);
            focusPoint -= (r * (dx * 0.02f)) + (u * (dy * 0.02f));
        }

        // Zoom: ALT + RMB Drag
        if (isAlt && isRMB)
        {
            distance += dy * 0.1f;
        }

        // Zoom: Scroll
        distance -= io.MouseWheel * 1.5f;

        pitch = std::clamp(pitch, -1.5f, 1.5f);
        distance = std::max(1.0f, distance);
    }
}

// --- Callback Routing ---
static void key_cb(GLFWwindow* w, int k, int s, int a, int m) {
    ImGui_ImplGlfw_KeyCallback(w, k, s, a, m);
    if (!ImGui::GetIO().WantCaptureKeyboard && a == GLFW_PRESS) keyPress((unsigned char)k, 0, 0);
}
static void mouse_cb(GLFWwindow* w, int b, int a, int m) {
    ImGui_ImplGlfw_MouseButtonCallback(w, b, a, m);
    if (!ImGui::GetIO().WantCaptureMouse) { double x, y; glfwGetCursorPos(w, &x, &y); mousePress(b, a, (int)x, (int)y); }
}
static void cursor_cb(GLFWwindow* w, double x, double y) {
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    if (!ImGui::GetIO().WantCaptureMouse) mouseMotion((int)x, (int)y);
}
static void scroll_cb(GLFWwindow* w, double x, double y) {
    ImGui_ImplGlfw_ScrollCallback(w, x, y);
}

AliceViewer* AliceViewer::instance() { return g_instance; }

V3 AliceViewer::screenToWorld(int x, int y, float planeZ)
{
    int w, h; glfwGetFramebufferSize(window, &w, &h);
    float nx = (2.0f * (float)x) / (float)w - 1.0f;
    float ny = 1.0f - (2.0f * (float)y) / (float)h;
    M4 proj = perspective(fov, (float)w / (float)h, 0.1f, 1000.0f);
    M4 view = camera.getViewMatrix();
    M4 invVP = invert(mult(proj, view));
    auto unproj = [&](float ndcX, float ndcY, float ndcZ) {
        float v[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4] = { 0 };
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[i] += invVP.m[j * 4 + i] * v[j];
        return V3(r[0] / r[3], r[1] / r[3], r[2] / r[3]);
    };
    V3 p0 = unproj(nx, ny, -1.0f); V3 p1 = unproj(nx, ny, 1.0f);
    V3 D = p1 - p0; if (fabsf(D.z) < 1e-6f) return p0;
    float t = (planeZ - p0.z) / D.z; return p0 + D * t;
}

int AliceViewer::init()
{
    g_instance = this;
    printf(">>> AliceViewer Framework v2.0.1 Loaded <<<\n");
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(1280, 720, "Alice CAD Framework", 0, 0);
    if (!window) return 1;
    glfwMakeContextCurrent(window); gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 400 core");
    glfwSetKeyCallback(window, key_cb); glfwSetMouseButtonCallback(window, mouse_cb);
    glfwSetCursorPosCallback(window, cursor_cb); glfwSetScrollCallback(window, scroll_cb);
    camera.focusPoint = { 0, 0, 0 }; camera.distance = 25.0f; camera.yaw = 0.8f; camera.pitch = 0.6f;
    double mx, my; glfwGetCursorPos(window, &mx, &my);
    camera.lastMouseX = (float)mx; camera.lastMouseY = (float)my;
    const char* vsSrc = "#version 400 core\nlayout(location=0) in vec3 pos; uniform mat4 mvp; void main(){gl_Position=mvp*vec4(pos,1.0);}";
    const char* fsSrc = "#version 400 core\nuniform vec3 uColor; out vec4 color; void main(){color=vec4(uColor,1.0);}";
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs, 1, &vsSrc, 0); glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &fsSrc, 0); glCompileShader(fs);
    shaderProgram = glCreateProgram(); glAttachShader(shaderProgram, vs); glAttachShader(shaderProgram, fs); glLinkProgram(shaderProgram);
    g_pointBatch.init(GL_POINTS); g_lineBatch.init(GL_LINES);
    setup(); return 0;
}

void AliceViewer::run()
{
    float lastTime = (float)glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        float now = (float)glfwGetTime(), dt = now - lastTime; lastTime = now;
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        
        {
            ImGui::Begin("Camera Controls");
            if (ImGui::Button("Perspective")) camera.setBookmark("Perspective"); ImGui::SameLine();
            if (ImGui::Button("Top")) camera.setBookmark("Top"); ImGui::SameLine();
            if (ImGui::Button("Front")) camera.setBookmark("Front");
            if (ImGui::Button("Back")) camera.setBookmark("Back"); ImGui::SameLine();
            if (ImGui::Button("Left")) camera.setBookmark("Left"); ImGui::SameLine();
            if (ImGui::Button("Right")) camera.setBookmark("Right");
            
            ImGui::Separator();
            ImGui::Text("Stats:");
            ImGui::Text("LookAt: %.2f, %.2f, %.2f", camera.focusPoint.x, camera.focusPoint.y, camera.focusPoint.z);
            ImGui::Text("Yaw: %.2f, Pitch: %.2f", camera.yaw, camera.pitch);
            ImGui::Text("Distance: %.2f", camera.distance);
            ImGui::SliderFloat("FoV", &fov, 0.1f, 2.0f);
            ImGui::End();
        }

        update(dt); backGround(0.9f); camera.update(window, dt);
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            glViewport(0, 0, w, h);
            g_currentMVP = mult(perspective(fov, (float)w / h, 0.1f, 1000.0f), camera.getViewMatrix());
            glEnable(GL_DEPTH_TEST); draw();
            g_lineBatch.flush(); g_pointBatch.flush();
        }
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
}

void AliceViewer::cleanup() { ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwTerminate(); }
