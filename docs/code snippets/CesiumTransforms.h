#ifndef CESIUM_TRANSFORMS_H
#define CESIUM_TRANSFORMS_H

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @file CesiumTransforms.h
 * @brief Minimal, dependency-free C++ math for WGS84 Geodetic, ECEF, and local ENU transformations.
 * 
 * SUMMARY & DECOUPLING STRATEGY:
 * - WGS84 Ellipsoid: Uses the standard semi-major (6378137.0) and semi-minor (6356752.314245) axes.
 * - Geodetic-to-ECEF: Implements the exact normal-based projection used in `Ellipsoid.cpp`.
 * - ECEF-to-Local (ENU): Constructs a 4x4 East-North-Up transformation matrix at a specific 
 *   origin, allowing ECEF coordinates to be mapped to a local meters-based system.
 * - St. Paul's Centering: Predetermines the ECEF origin for St. Paul's (Lat 51.5138, Lon -0.0984) 
 *   to allow for a 1000x1000m scene centered at (0,0,0).
 */

struct Vec3 {
    double x, y, z;
    Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }
    static double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static Vec3 normalize(const Vec3& v) {
        double mag = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (mag < 1e-15) return {0, 0, 0};
        return {v.x / mag, v.y / mag, v.z / mag};
    }
};

struct Mat4 {
    double m[16]; // Column-major
    
    /**
     * @brief Transforms a 3D point from ECEF to the local scene frame.
     */
    Vec3 transformPoint(const Vec3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
        };
    }
};

class CesiumTransforms {
public:
    static constexpr double WGS84_A = 6378137.0;
    static constexpr double WGS84_B = 6356752.314245179;
    static constexpr double WGS84_A2 = WGS84_A * WGS84_A;
    static constexpr double WGS84_B2 = WGS84_B * WGS84_B;

    /**
     * @brief Converts Lat/Lon/Height (Radians) to ECEF Cartesian coordinates.
     */
    static Vec3 geodeticToEcef(double latRad, double lonRad, double height) {
        double cosLat = std::cos(latRad);
        double sinLat = std::sin(latRad);
        double cosLon = std::cos(lonRad);
        double sinLon = std::sin(lonRad);

        // Geodetic surface normal
        Vec3 n = Vec3::normalize({ cosLat * cosLon, cosLat * sinLon, sinLat });
        
        // Offset normal by ellipsoid radii
        Vec3 k = { WGS84_A2 * n.x, WGS84_A2 * n.y, WGS84_B2 * n.z };
        double gamma = std::sqrt(Vec3::dot(n, k));
        k /= gamma;

        return { k.x + n.x * height, k.y + n.y * height, k.z + n.z * height };
    }

    /**
     * @brief Creates a 4x4 matrix that transforms ECEF coordinates to a local 
     * East-North-Up frame where the provided 'origin' point becomes (0,0,0).
     */
    static Mat4 createEcefToEnuMatrix(const Vec3& origin) {
        // 1. Calculate Local Up (Normal to ellipsoid at origin)
        Vec3 up = Vec3::normalize({ origin.x / WGS84_A2, origin.y / WGS84_A2, origin.z / WGS84_B2 });
        
        // 2. Calculate Local East (Tangent to latitude line)
        Vec3 east = Vec3::normalize({ -origin.y, origin.x, 0.0 });
        
        // 3. Calculate Local North
        Vec3 north = Vec3::cross(up, east);

        // Construct Fixed-to-ENU matrix
        // Rotation (Rows): East, North, Up
        // Translation: -Rotation * Origin
        Mat4 mat;
        mat.m[0] = east.x;  mat.m[4] = east.y;  mat.m[8]  = east.z;  mat.m[12] = -Vec3::dot(east, origin);
        mat.m[1] = north.x; mat.m[5] = north.y; mat.m[9]  = north.z;  mat.m[13] = -Vec3::dot(north, origin);
        mat.m[2] = up.x;    mat.m[6] = up.y;    mat.m[10] = up.z;     mat.m[14] = -Vec3::dot(up, origin);
        mat.m[3] = 0.0;     mat.m[7] = 0.0;     mat.m[11] = 0.0;     mat.m[15] = 1.0;

        return mat;
    }

    /**
     * @brief Returns a matrix centered on St. Paul's Cathedral (51.5138 N, 0.0984 W).
     */
    static Mat4 getStPaulsSceneMatrix() {
        const double lat = 51.5138 * (M_PI / 180.0);
        const double lon = -0.0984 * (M_PI / 180.0);
        Vec3 origin = geodeticToEcef(lat, lon, 0.0);
        return createEcefToEnuMatrix(origin);
    }
};

#endif // CESIUM_TRANSFORMS_H
