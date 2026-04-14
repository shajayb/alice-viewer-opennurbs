#pragma once

#include <cstddef>

#pragma pack(push, 1)

struct V3
{
    float x, y, z;

    V3() : x(0), y(0), z(0) 
    {
    }

    V3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) 
    {
    }

    V3 operator+(const V3& v) const
    {
        return { x + v.x, y + v.y, z + v.z };
    }

    V3 operator-(const V3& v) const
    {
        return { x - v.x, y - v.y, z - v.z };
    }

    V3 operator*(float s) const
    {
        return { x * s, y * s, z * s };
    }

    V3 operator/(float s) const
    {
        return { x / s, y / s, z / s };
    }

    V3& operator+=(const V3& v)
    {
        x += v.x; 
        y += v.y; 
        z += v.z; 
        return *this;
    }

    V3& operator-=(const V3& v)
    {
        x -= v.x; 
        y -= v.y; 
        z -= v.z; 
        return *this;
    }

    float length() const;
    void normalise();
};

struct Vertex
{
    V3 pos;
    V3 color;
};

struct alignas(16) V4
{
    float x, y, z, w;

    V4() : x(0), y(0), z(0), w(0) 
    {
    }

    V4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) 
    {
    }
};

namespace Alice
{
    using vec = V3;
}

struct alignas(16) M4
{
    float m[16];
};

struct ArcballCamera
{
    V3 focusPoint;
    float distance;
    float yaw;
    float pitch;
    float lastMouseX;
    float lastMouseY;

    M4 getViewMatrix() const;
    void update(struct GLFWwindow* window, float deltaTime);
    void setBookmark(const char* name);
};

#pragma pack(pop)

extern "C" 
{
    void setup();
    void update(float deltaTime);
    void draw();
    void keyPress(unsigned char key, int x, int y);
    void mousePress(int button, int state, int x, int y);
    void mouseMotion(int x, int y);

    void drawGrid(float size);
    void drawPoint(V3 pt);
    void drawLine(V3 a, V3 b);
    void drawTriangle(V3 a, V3 b, V3 c);
    void aliceColor3f(float r, float g, float b);
    void alicePointSize(float size);
    void aliceLineWidth(float width);
}

void backGround(float grey);
void backGround(float r, float g, float b);

#ifndef ALICE_FRAMEWORK
    #ifdef glColor3f
        #undef glColor3f
    #endif
    #define glColor3f aliceColor3f

    #ifdef glPointSize
        #undef glPointSize
    #endif
    #define glPointSize alicePointSize

    #ifdef glLineWidth
        #undef glLineWidth
    #endif
    #define glLineWidth aliceLineWidth
#endif

inline V3 nrm(V3 v) 
{ 
    v.normalise(); 
    return v; 
}

inline float dot(V3 a, V3 b) 
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z; 
}

inline V3 crs(V3 a, V3 b) 
{ 
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; 
}

struct GLFWwindow;

class AliceViewer
{
public:
    struct PerfTuner
    {
        double lastFlushTimeUs = 0.0;
        double frameDeltaMs = 0.0;
        int currentBatchThreshold = 8192;
        bool enabled = true;

        void tune(float dt, double flushUs);
    };

    GLFWwindow* window;
    ArcballCamera camera;
    unsigned int shaderProgram;
    float fov = 0.8f;
    float nearClip = 0.1f;
    int msaaSamples = 4;
    float ambientIntensity = 0.35f;
    float diffuseIntensity = 0.65f;
    V3 backColor = {0.588f, 0.588f, 0.588f};
    PerfTuner tuner;
    unsigned int timerQuery;
    unsigned int resolveTimerQuery;
    bool m_headlessCapture = false;

    int init(int argc, char** argv);
    void run();
    void cleanup();

    V3 screenToWorld(int screenX, int screenY, float planeZ = 0.0f);

    static AliceViewer* instance();
    static M4 makeInfiniteReversedZProjRH(float fovRadians, float aspect, float zNear);
    void drawTriangleArray(const V3* positions, size_t vertexCount, V3 color);
};

#ifdef ALICE_VIEWER_RUN_TEST
#include <cstdio>
#include <cmath>

#define ALICE_ASSERT(cond) \
    if (!(cond)) { printf("[FAIL] ASSERT: %s at %s:%d\n", #cond, __FILE__, __LINE__); }

#define ALICE_EXPECT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { printf("[FAIL] EXPECT_NEAR: %f != %f (eps %f) at %s:%d\n", (float)(a), (float)(b), (float)(eps), __FILE__, __LINE__); }

struct ScopedProfiler
{
    const char* name;
    double start;
    double* outUs;

    ScopedProfiler(const char* n, double* out = nullptr) : name(n), outUs(out)
    {
        start = glfwGetTime();
    }

    ~ScopedProfiler()
    {
        double end = glfwGetTime();
        double dur = (end - start) * 1000000.0;
        if (outUs)
        {
            *outUs = dur;
        }
        else
        {
            printf("[PROFILE] %s: %.2f us\n", name, dur);
        }
    }
};

void runAliceTests();
#endif
