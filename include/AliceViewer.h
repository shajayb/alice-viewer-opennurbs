#pragma once

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdio>

#pragma pack(push, 1)

struct V3
{
    float x, y, z;
    
    V3() : x(0), y(0), z(0) {}
    V3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    
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
        x += v.x; y += v.y; z += v.z; return *this; 
    }
    V3& operator-=(const V3& v) 
    { 
        x -= v.x; y -= v.y; z -= v.z; return *this; 
    }
    
    float length() const 
    { 
        return sqrtf(x * x + y * y + z * z); 
    }
    void normalise() 
    { 
        float l = length(); 
        if (l > 1e-9f) { x /= l; y /= l; z /= l; } 
    }
};

struct V4
{
    float x, y, z, w;
    V4() : x(0), y(0), z(0), w(0) {}
    V4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

namespace Alice 
{ 
    using vec = V3; 
}

struct M4
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

// --- MVC "Sketch" Interface (Weak Symbols / Externs) ---
// ... (rest of extern "C" remains the same)
extern "C" {
    void setup();
    void update(float deltaTime);
    void draw();
    void keyPress(unsigned char key, int x, int y);
    void mousePress(int button, int state, int x, int y);
    void mouseMotion(int x, int y);

    // --- Alice CAD Framework Primitive API (C-Linkage compatible) ---
    void drawGrid(float size);
    void drawPoint(V3 pt);
    void drawLine(V3 a, V3 b);
    void aliceColor3f(float r, float g, float b);
    void alicePointSize(float size);
    void aliceLineWidth(float width);
}

// C++ Overloads (Not in extern "C")
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

// --- Math Utilities ---
inline V3 nrm(V3 v) { v.normalise(); return v; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 crs(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

struct GLFWwindow;

class AliceViewer
{
public:
    GLFWwindow* window;
    ArcballCamera camera;
    unsigned int shaderProgram;
    float fov = 0.8f;

    int init();
    void run();
    void cleanup();
    
    // Utility for coordinate space transformation
    V3 screenToWorld(int screenX, int screenY, float planeZ = 0.0f);
    
    static AliceViewer* instance();
};
