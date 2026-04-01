#ifndef RHINO_IO_H
#define RHINO_IO_H

#include <cstdint>

namespace Alice
{
    // POD structures for DOD pipeline
    struct RhinoMeshBuffer
    {
        float* vertices;
        uint32_t* indices;
        uint32_t vertexCount;
        uint32_t indexCount;
    };

    struct RhinoPointBuffer
    {
        float* points; // x,y,z
        uint32_t count;
    };

    struct RhinoCurveBuffer
    {
        float* points; // x,y,z (polyline segments)
        uint32_t* counts; // number of points per curve
        uint32_t curveCount;
        uint32_t totalPointCount;
    };

    struct RhinoModel
    {
        RhinoMeshBuffer mesh;
        RhinoPointBuffer points;
        RhinoCurveBuffer curves;
    };

    struct RhinoParser
    {
        static bool LoadFile(const char* path, RhinoModel& outModel);
    };
}

#ifdef RHINO_IO_RUN_TEST
#include <cstdio>

namespace Alice
{
    inline void RhinoIO_UnitTest()
    {
        printf("[RHINO_IO_TEST] Starting...\n");
        RhinoModel model = {};
        const char* testPath = "test.3dm"; 
        
        bool success = RhinoParser::LoadFile(testPath, model);
        
        printf("[RHINO_IO_TEST] Load %s: %s\n", testPath, success ? "SUCCESS" : "FAILED (expected if file missing)");
        if (success)
        {
            printf("[RHINO_IO_TEST] Mesh - Vertices: %u, Indices: %u\n", model.mesh.vertexCount, model.mesh.indexCount);
            printf("[RHINO_IO_TEST] Points: %u\n", model.points.count);
            printf("[RHINO_IO_TEST] Curves: %u (Total Points: %u)\n", model.curves.curveCount, model.curves.totalPointCount);
        }
    }
}
#endif // RHINO_IO_RUN_TEST

#endif // RHINO_IO_H

// -----------------------------------------------------------------------------
// IMPLEMENTATION
// -----------------------------------------------------------------------------
#ifdef ALICE_RHINO_IMPLEMENTATION

#include <opennurbs.h>
#include "AliceMemory.h"

namespace Alice
{
    extern LinearArena g_Arena;

    bool RhinoParser::LoadFile(const char* path, RhinoModel& outModel)
    {
        ONX_Model model;
        if (!model.Read(path)) return false;

        uint32_t meshCount = 0;
        uint32_t pointCount = 0;
        uint32_t curveCount = 0;
        uint32_t totalCurvePoints = 0;

        // Pass 1: Count
        ONX_ModelComponentIterator it1(model, ON_ModelComponent::Type::ModelGeometry);
        const ON_ModelComponent* comp1 = nullptr;
        while ((comp1 = it1.NextComponent()) != nullptr)
        {
            const ON_ModelGeometryComponent* geometry = ON_ModelGeometryComponent::Cast(comp1);
            if (!geometry) continue;
            const ON_Geometry* geom = geometry->Geometry(nullptr);
            if (!geom) continue;

            if (ON_Mesh::Cast(geom)) meshCount++;
            else if (ON_Point::Cast(geom)) pointCount++;
            else if (const ON_Curve* curve = ON_Curve::Cast(geom))
            {
                curveCount++;
                // Approximate curve with polyline for DOD buffer
                ON_Polyline poly;
                if (curve->IsPolyline(&poly)) totalCurvePoints += poly.Count();
                else totalCurvePoints += 100; // Default approximation
            }
        }

        // Allocate
        if (pointCount > 0)
        {
            outModel.points.points = (float*)g_Arena.allocate(sizeof(float) * 3 * pointCount);
            outModel.points.count = 0;
        }
        if (curveCount > 0)
        {
            outModel.curves.points = (float*)g_Arena.allocate(sizeof(float) * 3 * totalCurvePoints);
            outModel.curves.counts = (uint32_t*)g_Arena.allocate(sizeof(uint32_t) * curveCount);
            outModel.curves.curveCount = 0;
            outModel.curves.totalPointCount = 0;
        }

        // Pass 2: Extract
        ONX_ModelComponentIterator it2(model, ON_ModelComponent::Type::ModelGeometry);
        const ON_ModelComponent* comp2 = nullptr;
        while ((comp2 = it2.NextComponent()) != nullptr)
        {
            const ON_ModelGeometryComponent* geometry = ON_ModelGeometryComponent::Cast(comp2);
            if (!geometry) continue;
            const ON_Geometry* geom = geometry->Geometry(nullptr);
            if (!geom) continue;

            // Mesh (just one for now as per previous logic)
            const ON_Mesh* mesh = ON_Mesh::Cast(geom);
            if (mesh && outModel.mesh.vertexCount == 0)
            {
                outModel.mesh.vertexCount = mesh->m_V.Count();
                outModel.mesh.indexCount = mesh->m_F.Count() * 3;
                outModel.mesh.vertices = (float*)g_Arena.allocate(sizeof(float) * 3 * outModel.mesh.vertexCount);
                outModel.mesh.indices = (uint32_t*)g_Arena.allocate(sizeof(uint32_t) * outModel.mesh.indexCount);
                if (outModel.mesh.vertices && outModel.mesh.indices)
                {
                    for (int v = 0; v < mesh->m_V.Count(); ++v)
                    {
                        outModel.mesh.vertices[v * 3 + 0] = (float)mesh->m_V[v].x;
                        outModel.mesh.vertices[v * 3 + 1] = (float)mesh->m_V[v].y;
                        outModel.mesh.vertices[v * 3 + 2] = (float)mesh->m_V[v].z;
                    }
                    for (int f = 0; f < mesh->m_F.Count(); ++f)
                    {
                        outModel.mesh.indices[f * 3 + 0] = (uint32_t)mesh->m_F[f].vi[0];
                        outModel.mesh.indices[f * 3 + 1] = (uint32_t)mesh->m_F[f].vi[1];
                        outModel.mesh.indices[f * 3 + 2] = (uint32_t)mesh->m_F[f].vi[2];
                    }
                }
            }
            // Points
            else if (const ON_Point* point = ON_Point::Cast(geom))
            {
                if (outModel.points.points)
                {
                    outModel.points.points[outModel.points.count * 3 + 0] = (float)point->point.x;
                    outModel.points.points[outModel.points.count * 3 + 1] = (float)point->point.y;
                    outModel.points.points[outModel.points.count * 3 + 2] = (float)point->point.z;
                    outModel.points.count++;
                }
            }
            // Curves
            else if (const ON_Curve* curve = ON_Curve::Cast(geom))
            {
                if (outModel.curves.points)
                {
                    ON_Polyline poly;
                    if (curve->IsPolyline(&poly))
                    {
                        for (int p = 0; p < poly.Count(); ++p)
                        {
                            outModel.curves.points[outModel.curves.totalPointCount * 3 + 0] = (float)poly[p].x;
                            outModel.curves.points[outModel.curves.totalPointCount * 3 + 1] = (float)poly[p].y;
                            outModel.curves.points[outModel.curves.totalPointCount * 3 + 2] = (float)poly[p].z;
                            outModel.curves.totalPointCount++;
                        }
                        outModel.curves.counts[outModel.curves.curveCount++] = poly.Count();
                    }
                }
            }
        }

        return true;
    }
}

#endif // ALICE_RHINO_IMPLEMENTATION
