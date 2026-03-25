#pragma once

#include <cmath>

#pragma pack(push, 1)

struct V3
{
    float x, y, z;
};

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
};

#pragma pack(pop)

inline V3 sub(V3 a, V3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline V3 add(V3 a, V3 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline V3 mul(V3 v, float s)
{
    return { v.x * s, v.y * s, v.z * s };
}

inline float dot(V3 a, V3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline V3 crs(V3 a, V3 b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline V3 nrm(V3 v)
{
    float l = sqrtf(dot(v, v));
    if (l > 0.0f)
    {
        return mul(v, 1.0f / l);
    }
    return v;
}

inline M4 perspective(float fov, float asp, float n, float f)
{
    float tn = tanf(fov * 0.5f);
    M4 r = { 0 };
    r.m[0] = 1.0f / (asp * tn);
    r.m[5] = 1.0f / tn;
    r.m[10] = -(f + n) / (f - n);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * f * n) / (f - n);
    return r;
}

inline M4 lookAt(V3 eye, V3 target, V3 up)
{
    V3 f = nrm(sub(target, eye));
    V3 s = nrm(crs(f, up));
    V3 v = crs(s, f);
    
    M4 r = { 0 };
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    
    r.m[1] = v.x;
    r.m[5] = v.y;
    r.m[9] = v.z;
    
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(v, eye);
    r.m[14] = dot(f, eye);
    r.m[15] = 1.0f;
    
    return r;
}

inline M4 mult(M4 a, M4 b)
{
    M4 r = { 0 };
    for (int j = 0; j < 4; ++j)
    {
        for (int i = 0; i < 4; ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                r.m[j * 4 + i] += a.m[k * 4 + i] * b.m[j * 4 + k];
            }
        }
    }
    return r;
}

struct GLFWwindow;

class AliceViewer
{
public:
    GLFWwindow* window;
    ArcballCamera camera;
    unsigned int shaderProgram;
    unsigned int vao, vbo;
    int gridVertexCount;

    int init();
    void run();
    void cleanup();
};
