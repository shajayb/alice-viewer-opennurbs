#ifndef AABB_DRAW_H
#define AABB_DRAW_H

#include "AliceViewer.h"
#include "AliceMemory.h"

struct AABBCompute
{
    V3 min;
    V3 max;

    void compute(const Alice::Buffer<V3>& cloud)
    {
        if (cloud.size() == 0)
        {
            min = { 0, 0, 0 };
            max = { 0, 0, 0 };
            return;
        }

        min = { 1e30f, 1e30f, 1e30f };
        max = { -1e30f, -1e30f, -1e30f };

        const V3* data = cloud.data;
        size_t n = cloud.size();
        for (size_t i = 0; i < n; ++i)
        {
            const V3& v = data[i];
            if (v.x < min.x) min.x = v.x;
            if (v.y < min.y) min.y = v.y;
            if (v.z < min.z) min.z = v.z;
            if (v.x > max.x) max.x = v.x;
            if (v.y > max.y) max.y = v.y;
            if (v.z > max.z) max.z = v.z;
        }
    }

    void draw()
    {
        // CAD Orange: RGB(1.0, 0.5, 0.0)
        aliceColor3f(1.0f, 0.5f, 0.0f);
        aliceLineWidth(2.0f);

        V3 p1 = { min.x, min.y, min.z };
        V3 p2 = { max.x, min.y, min.z };
        V3 p3 = { max.x, max.y, min.z };
        V3 p4 = { min.x, max.y, min.z };

        V3 p5 = { min.x, min.y, max.z };
        V3 p6 = { max.x, min.y, max.z };
        V3 p7 = { max.x, max.y, max.z };
        V3 p8 = { min.x, max.y, max.z };

        // Bottom
        drawLine(p1, p2); drawLine(p2, p3); drawLine(p3, p4); drawLine(p4, p1);
        // Top
        drawLine(p5, p6); drawLine(p6, p7); drawLine(p7, p8); drawLine(p8, p5);
        // Columns
        drawLine(p1, p5); drawLine(p2, p6); drawLine(p3, p7); drawLine(p4, p8);
    }
};

#endif
