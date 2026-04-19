#ifndef ALICE_MATH_H
#define ALICE_MATH_H

#include <cmath>
#include <algorithm>

namespace Math
{
    struct Vec3 { float x, y, z; };
    struct DVec3 { double x, y, z; };
    struct LLA { double lat, lon, alt; };

    inline Vec3 normalize(Vec3 v) { float l = 1.0f / sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); return {v.x*l, v.y*l, v.z*l}; }
    inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
    inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

    inline LLA ecef_to_lla(DVec3 ecef)
    {
        const double a = 6378137.0;
        const double b = 6356752.314245;
        const double e2 = 1.0 - (b * b) / (a * a);
        const double ep2 = (a * a) / (b * b) - 1.0;
        
        double p = sqrt(ecef.x * ecef.x + ecef.y * ecef.y);
        double th = atan2(a * ecef.z, b * p);
        double lon = atan2(ecef.y, ecef.x);
        double lat = atan2(ecef.z + ep2 * b * pow(sin(th), 3), p - e2 * a * pow(cos(th), 3));
        double sinLat = sin(lat);
        double N = a / sqrt(1.0 - e2 * sinLat * sinLat);
        double alt = p / cos(lat) - N;
        
        return { lat, lon, alt };
    }

    inline DVec3 lla_to_ecef(double lat, double lon, double alt)
    {
        const double a = 6378137.0;
        const double e2 = 0.0066943799901413165; 
        double sLat = sin(lat), cLat = cos(lat);
        double sLon = sin(lon), cLon = cos(lon);
        double N = a / sqrt(1.0 - e2 * sLat * sLat);
        return {
            (N + alt) * cLat * cLon,
            (N + alt) * cLat * sLon,
            (N * (1.0 - e2) + alt) * sLat
        };
    }

    constexpr double DEG2RAD = 0.017453292519943295;
    constexpr double RAD2DEG = 57.29577951308232;

    inline void denu_matrix(double* m, double lat, double lon)
    {
        double sLat = sin(lat), cLat = cos(lat);
        double sLon = sin(lon), cLon = cos(lon);
        
        // m is treated as Row-Major for the projection step in TilesetLoader
        // Row 0: East
        m[0] = -sLon;
        m[1] = cLon;
        m[2] = 0.0;
        m[3] = 0.0;
        
        // Row 1: North
        m[4] = -sLat * cLon;
        m[5] = -sLat * sLon;
        m[6] = cLat;
        m[7] = 0.0;
        
        // Row 2: Up
        m[8] = cLat * cLon;
        m[9] = cLat * sLon;
        m[10] = sLat;
        m[11] = 0.0;
        
        m[12] = 0.0; m[13] = 0.0; m[14] = 0.0; m[15] = 1.0;
    }

    inline void enu_matrix(float* m, double lat, double lon)
    {
        double dm[16];
        denu_matrix(dm, lat, lon);
        for(int i=0; i<16; ++i) m[i] = (float)dm[i];
    }

    inline void mat4_identity(float* m)
    {
        for(int i=0; i<16; ++i) m[i] = 0.0f;
        m[0]=1.0f; m[5]=1.0f; m[10]=1.0f; m[15]=1.0f;
    }

    inline void mat4_mul(float* out, const float* a, const float* b)
    {
        float res[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                res[i + j * 4] = a[i + 0 * 4] * b[0 + j * 4] +
                                 a[i + 1 * 4] * b[1 + j * 4] +
                                 a[i + 2 * 4] * b[2 + j * 4] +
                                 a[i + 3 * 4] * b[3 + j * 4];
            }
        }
        for(int i=0; i<16; ++i) out[i] = res[i];
    }

    inline void mat4d_identity(double* m)
    {
        for(int i=0; i<16; ++i) m[i] = 0.0;
        m[0]=1.0; m[5]=1.0; m[10]=1.0; m[15]=1.0;
    }

    inline void mat4d_mul(double* out, const double* a, const double* b)
    {
        double res[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                res[i + j * 4] = a[i + 0 * 4] * b[0 + j * 4] +
                                 a[i + 1 * 4] * b[1 + j * 4] +
                                 a[i + 2 * 4] * b[2 + j * 4] +
                                 a[i + 3 * 4] * b[3 + j * 4];
            }
        }
        for(int i=0; i<16; ++i) out[i] = res[i];
    }

    inline void mat4_ortho(float* m, float l, float r, float b, float t, float n, float f)
    {
        for(int i=0; i<16; ++i) m[i] = 0.0f;
        m[0] = 2.0f / (r - l);
        m[5] = 2.0f / (t - b);
        m[10] = -2.0f / (f - n);
        m[12] = -(r + l) / (r - l);
        m[13] = -(t + b) / (t - b);
        m[14] = -(f + n) / (f - n);
        m[15] = 1.0f;
    }

    inline void mat4d_transform_point(DVec3& p, const double* m)
    {
        double px = p.x, py = p.y, pz = p.z;
        double x = m[0] * px + m[4] * py + m[8] * pz + m[12];
        double y = m[1] * px + m[5] * py + m[9] * pz + m[13];
        double z = m[2] * px + m[6] * py + m[10] * pz + m[14];
        double w = m[3] * px + m[7] * py + m[11] * pz + m[15];
        p.x = x / w; p.y = y / w; p.z = z / w;
    }

    inline void mat4_inverse(float* res, const float* m)
    {
        float inv[16], det;
        const float* m_ = m;
        
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
        if (det == 0) return;

        det = 1.0f / det;
        for (int i = 0; i < 16; i++) res[i] = inv[i] * det;
    }

    inline void mat4_perspective(float* m, float fov, float aspect, float n, float f)
    {
        float t = tanf(fov * 0.5f);
        for(int i=0; i<16; ++i) m[i] = 0.0f;
        m[0] = 1.0f / (aspect * t);
        m[5] = 1.0f / t;
        m[10] = -(f + n) / (f - n);
        m[11] = -1.0f;
        m[14] = -(2.0f * f * n) / (f - n);
    }

    inline void mat3_normal(float* out, const float* m)
    {
        out[0]=m[0]; out[1]=m[1]; out[2]=m[2];
        out[3]=m[4]; out[4]=m[5]; out[5]=m[6];
        out[6]=m[8]; out[7]=m[9]; out[8]=m[10];
    }
}

#endif
