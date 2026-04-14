#pragma once

#include <cstdio>
#include <cstdlib>
#include "AliceMemory.h"
#include "AliceViewer.h"
#include "RhinoLoader.h"

#ifdef CAD_PIPELINE_RUN_TEST

static Alice::Buffer<V3> g_TerrainMesh;
static Alice::Buffer<V3> g_QuadMesh;
static Alice::Buffer<V3> g_OSMMesh;

struct AABB
{
    V3 min, max, center;
    float radius;

    void compute(const Alice::Buffer<V3>& buffer)
    {
        if (buffer.size() == 0)
        {
            return;
        }
        min = { 1e30f, 1e30f, 1e30f };
        max = { -1e30f, -1e30f, -1e30f };
        for (size_t i = 0; i < buffer.size(); ++i)
        {
            const V3& v = buffer[i];
            if (v.x < min.x) min.x = v.x;
            if (v.y < min.y) min.y = v.y;
            if (v.z < min.z) min.z = v.z;
            if (v.x > max.x) max.x = v.x;
            if (v.y > max.y) max.y = v.y;
            if (v.z > max.z) max.z = v.z;
        }
        center = (min + max) * 0.5f;
        radius = (max - min).length() * 0.5f;
    }
};

static AABB g_AABBs[3];
static int g_CurrentAABB = 0;

static void logSweep(const char* message)
{
    printf("[SWEEP] %s\n", message);
    FILE* f = nullptr;
    if (fopen_s(&f, "./pipeline_sweep_results.log", "a") == 0)
    {
        fprintf(f, "%s\n", message);
        fflush(f);
        fclose(f);
    }
}

struct ZTestPlane
{
    V3 vertices[6];
    void init(V3 center, float size, float offset)
    {
        float s = size * 0.5f;
        vertices[0] = { center.x - s, center.y - s, center.z + offset };
        vertices[1] = { center.x + s, center.y - s, center.z + offset };
        vertices[2] = { center.x + s, center.y + s, center.z + offset };
        vertices[3] = { center.x - s, center.y - s, center.z + offset };
        vertices[4] = { center.x + s, center.y + s, center.z + offset };
        vertices[5] = { center.x - s, center.y + s, center.z + offset };
    }
};

static ZTestPlane g_ZPlanes[2];

extern "C" void setup()
{
    logSweep("--- Starting Alice Viewer 2 Pipeline Sweep ---");
    Alice::g_Arena.init(512 * 1024 * 1024); // 512MB
    
    g_TerrainMesh.init(Alice::g_Arena, 2000000); // 2M vertices
    g_QuadMesh.init(Alice::g_Arena, 2000000);
    g_OSMMesh.init(Alice::g_Arena, 5000000);

    bool s1 = Alice::RhinoLoader::LoadMesh("CF_beta_village.3dm", "", g_TerrainMesh, Alice::g_Arena);
    logSweep(s1 ? "[LOAD] Terrain Success" : "[LOAD] Terrain FAIL");

    bool s2 = Alice::RhinoLoader::LoadMesh("contour_stack_meshed.3dm", "QUAD REMESHED", g_QuadMesh, Alice::g_Arena);
    logSweep(s2 ? "[LOAD] Quad Success" : "[LOAD] Quad FAIL");

    bool s3 = Alice::RhinoLoader::LoadMesh("Context_buildings_7500_radius + mockup test.3dm", "OSM MESH", g_OSMMesh, Alice::g_Arena);
    logSweep(s3 ? "[LOAD] OSM Success" : "[LOAD] OSM FAIL");

    g_AABBs[0].compute(g_TerrainMesh);
    g_AABBs[1].compute(g_QuadMesh);
    g_AABBs[2].compute(g_OSMMesh);
    
    g_ZPlanes[0].init({0, 0, 1000}, 10.0f, 0.0f);
    g_ZPlanes[1].init({0, 0, 1000}, 10.0f, 0.0001f); 
}

extern "C" void update(float dt)
{
    static float sweepTimer = 0.0f;
    static int sweepState = 0; 
    
    sweepTimer += dt;
    if (sweepTimer > 2.0f) 
    {
        sweepTimer = 0.0f;
        AliceViewer* v = AliceViewer::instance();
        if (!v)
        {
            return;
        }
        
        char buf[256];
        if (sweepState == 0) 
        {
            static int zSub = 0;
            float zValues[] = { 0.001f, 0.01f, 0.1f, 1.0f };
            if (zSub < 4)
            {
                v->nearClip = zValues[zSub];
                sprintf_s(buf, "[Z-SWP] NearClip: %.3f", v->nearClip);
                logSweep(buf);
                zSub++;
            }
            else
            {
                sweepState++;
            }
        }
        else if (sweepState == 1) 
        {
            static int shdSub = 0;
            float amb[] = { 0.10f, 0.35f, 0.45f, 0.50f };
            float dif[] = { 0.90f, 0.65f, 0.55f, 0.50f };
            if (shdSub < 4)
            {
                v->ambientIntensity = amb[shdSub];
                v->diffuseIntensity = dif[shdSub];
                sprintf_s(buf, "[SHD-SWP] Amb: %.2f, Dif: %.2f", v->ambientIntensity, v->diffuseIntensity);
                logSweep(buf);
                shdSub++;
            }
            else
            {
                sweepState++;
            }
        }
    }
}

static void drawMesh(const Alice::Buffer<V3>& buffer, V3 color)
{
    if (buffer.size() == 0)
    {
        return;
    }
    AliceViewer::instance()->drawTriangleArray(buffer.data, buffer.size(), color);
}

extern "C" void draw()
{
    drawGrid(100.0f);
    
    aliceColor3f(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 2; ++i)
    {
        drawTriangle(g_ZPlanes[i].vertices[0], g_ZPlanes[i].vertices[1], g_ZPlanes[i].vertices[2]);
        drawTriangle(g_ZPlanes[i].vertices[3], g_ZPlanes[i].vertices[4], g_ZPlanes[i].vertices[5]);
    }

    drawMesh(g_TerrainMesh, {0.2f, 0.5f, 0.2f});
    drawMesh(g_QuadMesh, {0.5f, 0.2f, 0.2f});
    drawMesh(g_OSMMesh, {0.2f, 0.2f, 0.5f});
}

extern "C" void keyPress(unsigned char key, int x, int y)
{
    if (key == 'f' || key == 'F')
    {
        AliceViewer* v = AliceViewer::instance();
        if (v)
        {
            const AABB& aabb = g_AABBs[g_CurrentAABB];
            v->camera.focusPoint = aabb.center;
            v->camera.distance = aabb.radius * 1.5f;
            g_CurrentAABB = (g_CurrentAABB + 1) % 3;
            printf("[UI] Zoom Extents: Mesh %d | Radius: %.2f\n", g_CurrentAABB, aabb.radius);
        }
    }
}

#endif
