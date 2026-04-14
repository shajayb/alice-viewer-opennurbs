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

    inline void denu_matrix(double* m, double lat, double lon)
    {
        double sLat = sin(lat), cLat = cos(lat);
        double sLon = sin(lon), cLon = cos(lon);
        
        // East (X):  [-sin(lon), cos(lon), 0]
        // North (Y): [-sin(lat)cos(lon), -sin(lat)sin(lon), cos(lat)]
        // Up (Z):    [cos(lat)cos(lon), cos(lat)sin(lon), sin(lat)]
        
        // m[0..2] = East
        m[0] = -sLon;
        m[1] = cLon;
        m[2] = 0.0;
        
        // m[4..6] = North
        m[4] = -sLat * cLon;
        m[5] = -sLat * sLon;
        m[6] = cLat;
        
        // m[8..10] = Up
        m[8] = cLat * cLon;
        m[9] = cLat * sLon;
        m[10] = sLat;
        
        m[3] = 0.0; m[7] = 0.0; m[11] = 0.0;
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
