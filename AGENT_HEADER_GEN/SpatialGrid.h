#ifndef SPATIAL_GRID_H
#define SPATIAL_GRID_H

#include <cstdio>
#include <chrono>
#include <cmath>
#include "AliceMemory.h"
#include "AliceViewer.h"

namespace Alice
{

struct SpatialGrid
{
    Buffer<int> pointIndices;
    Buffer<int> cellStarts;
    Buffer<int> cellCounts;
    Buffer<int> tempOffsets; // Used during build

    V3 minBound;
    V3 maxBound;
    V3 cellSize;
    int res;
    int totalCells;

    SpatialGrid() : res(0), totalCells(0) {}

    void init(LinearArena& arena, int resolution, int maxPoints)
    {
        res = resolution;
        totalCells = res * res * res;
        
        pointIndices.init(arena, maxPoints);
        cellStarts.init(arena, totalCells);
        cellCounts.init(arena, totalCells);
        tempOffsets.init(arena, totalCells);

        for (int i = 0; i < totalCells; ++i)
        {
            cellStarts.push_back(0);
            cellCounts.push_back(0);
            tempOffsets.push_back(0);
        }
    }

    inline int getCellIndex(const V3& p) const
    {
        int ix = (int)((p.x - minBound.x) / cellSize.x);
        int iy = (int)((p.y - minBound.y) / cellSize.y);
        int iz = (int)((p.z - minBound.z) / cellSize.z);

        if (ix < 0) ix = 0; if (ix >= res) ix = res - 1;
        if (iy < 0) iy = 0; if (iy >= res) iy = res - 1;
        if (iz < 0) iz = 0; if (iz >= res) iz = res - 1;

        return ix + iy * res + iz * res * res;
    }

    void build(const Buffer<V3>& cloud)
    {
        if (cloud.size() == 0) return;

        // 1. Calculate Bounds
        minBound = cloud[0];
        maxBound = cloud[0];
        for (size_t i = 1; i < cloud.size(); ++i)
        {
            const V3& p = cloud[i];
            if (p.x < minBound.x) minBound.x = p.x;
            if (p.y < minBound.y) minBound.y = p.y;
            if (p.z < minBound.z) minBound.z = p.z;
            if (p.x > maxBound.x) maxBound.x = p.x;
            if (p.y > maxBound.y) maxBound.y = p.y;
            if (p.z > maxBound.z) maxBound.z = p.z;
        }

        // Add small epsilon to avoid out-of-bounds at max
        maxBound.x += 0.001f;
        maxBound.y += 0.001f;
        maxBound.z += 0.001f;

        cellSize.x = (maxBound.x - minBound.x) / (float)res;
        cellSize.y = (maxBound.y - minBound.y) / (float)res;
        cellSize.z = (maxBound.z - minBound.z) / (float)res;

        // 2. Pass 1: Count
        for (int i = 0; i < totalCells; ++i) cellCounts[i] = 0;
        for (size_t i = 0; i < cloud.size(); ++i)
        {
            cellCounts[getCellIndex(cloud[i])]++;
        }

        // 3. Prefix Sum
        int offset = 0;
        for (int i = 0; i < totalCells; ++i)
        {
            cellStarts[i] = offset;
            tempOffsets[i] = offset;
            offset += cellCounts[i];
        }

        // 4. Pass 2: Fill
        pointIndices.clear();
        pointIndices.count = cloud.size(); // Manually set size for indexed access
        for (size_t i = 0; i < cloud.size(); ++i)
        {
            int cellIdx = getCellIndex(cloud[i]);
            pointIndices[tempOffsets[cellIdx]++] = (int)i;
        }
    }

    void queryAABB(V3 qMin, V3 qMax, Buffer<int>& outIndices) const
    {
        int ixMin = (int)((qMin.x - minBound.x) / cellSize.x);
        int iyMin = (int)((qMin.y - minBound.y) / cellSize.y);
        int izMin = (int)((qMin.z - minBound.z) / cellSize.z);

        int ixMax = (int)((qMax.x - minBound.x) / cellSize.x);
        int iyMax = (int)((qMax.y - minBound.y) / cellSize.y);
        int izMax = (int)((qMax.z - minBound.z) / cellSize.z);

        if (ixMin < 0) ixMin = 0; if (ixMin >= res) ixMin = res - 1;
        if (iyMin < 0) iyMin = 0; if (iyMin >= res) iyMin = res - 1;
        if (izMin < 0) izMin = 0; if (izMin >= res) izMin = res - 1;

        if (ixMax < 0) ixMax = 0; if (ixMax >= res) ixMax = res - 1;
        if (iyMax < 0) iyMax = 0; if (iyMax >= res) iyMax = res - 1;
        if (izMax < 0) izMax = 0; if (izMax >= res) izMax = res - 1;

        for (int z = izMin; z <= izMax; ++z)
        {
            int zOff = z * res * res;
            for (int y = iyMin; y <= iyMax; ++y)
            {
                int yOff = y * res;
                for (int x = ixMin; x <= ixMax; ++x)
                {
                    int cellIdx = x + yOff + zOff;
                    int start = cellStarts[cellIdx];
                    int count = cellCounts[cellIdx];
                    for (int i = 0; i < count; ++i)
                    {
                        outIndices.push_back(pointIndices[start + i]);
                    }
                }
            }
        }
    }
};

} // namespace Alice

#ifdef SPATIAL_GRID_RUN_TEST

namespace Alice
{
    extern LinearArena g_Arena;
}

struct GridProfiler
{
    const char* label;
    std::chrono::high_resolution_clock::time_point start;
    GridProfiler(const char* l) : label(l) { start = std::chrono::high_resolution_clock::now(); }
    ~GridProfiler()
    {
        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - start).count();
        printf("[METRIC] %s: %.4f\n", label, ms);
    }
};

extern "C"
{
    void setup()
    {
        if (Alice::g_Arena.memory == nullptr) Alice::g_Arena.init(256 * 1024 * 1024);

        printf("[TEST] Initializing SpatialGrid Performance Test (1M points)...\n");

        Alice::Buffer<V3> cloud;
        cloud.init(Alice::g_Arena, 1000000);
        for (int i = 0; i < 1000000; ++i)
        {
            cloud.push_back(V3((float)(rand() % 1000) / 10.0f, (float)(rand() % 1000) / 10.0f, (float)(rand() % 1000) / 10.0f));
        }

        Alice::SpatialGrid grid;
        grid.init(Alice::g_Arena, 64, 1000000);

        {
            GridProfiler p("grid_build_1M_ms");
            grid.build(cloud);
        }

        Alice::Buffer<int> out;
        out.init(Alice::g_Arena, 1000000);

        V3 qMin(45.0f, 45.0f, 45.0f);
        V3 qMax(55.0f, 55.0f, 55.0f); // ~1% of 100x100x100 volume

        {
            GridProfiler p("grid_query_1M_ms");
            grid.queryAABB(qMin, qMax, out);
        }

        printf("[TEST] Query found %zu points.\n", out.size());
        
        // Final sanity check for performance
        auto start = std::chrono::high_resolution_clock::now();
        grid.queryAABB(qMin, qMax, out);
        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - start).count();
        
        if (ms < 0.5f) printf("[OK] Sub-millisecond target met: %.4f ms\n", ms);
        else printf("[WARNING] Performance target MISSED: %.4f ms\n", ms);

        printf("[TEST] Execution finished. Exiting.\n");
        std::exit(0);
    }

    void update(float dt) { (void)dt; }
    void draw() {}
    void keyPress(unsigned char k, int x, int y) { (void)k; (void)x; (void)y; }
    void mousePress(int b, int s, int x, int y) { (void)b; (void)s; (void)x; (void)y; }
    void mouseMotion(int x, int y) { (void)x; (void)y; }
}
#endif

#endif
