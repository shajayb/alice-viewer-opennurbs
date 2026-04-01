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

    struct RhinoParser
    {
        static bool LoadFile(const char* path, RhinoMeshBuffer& outBuffer);
    };
}

#ifdef RHINO_IO_RUN_TEST
#include <cstdio>

namespace Alice
{
    inline void RhinoIO_UnitTest()
    {
        printf("[RHINO_IO_TEST] Starting...\n");
        RhinoMeshBuffer buffer = {};
        const char* testPath = "test.3dm"; 
        
        bool success = RhinoParser::LoadFile(testPath, buffer);
        
        printf("[RHINO_IO_TEST] Load %s: %s\n", testPath, success ? "SUCCESS" : "FAILED (expected if file missing)");
        if (success)
        {
            printf("[RHINO_IO_TEST] Vertices: %u, Indices: %u\n", buffer.vertexCount, buffer.indexCount);
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

    bool RhinoParser::LoadFile(const char* path, RhinoMeshBuffer& outBuffer)
    {
        ONX_Model model;
        
        if (!model.Read(path))
        {
            return false;
        }

        ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
        const ON_ModelComponent* component = nullptr;
        
        while ((component = it.NextComponent()) != nullptr)
        {
            const ON_ModelGeometryComponent* geometry = ON_ModelGeometryComponent::Cast(component);
            if (!geometry)
            {
                continue;
            }

            const ON_Mesh* mesh = ON_Mesh::Cast(geometry->Geometry(nullptr));
            if (mesh)
            {
                outBuffer.vertexCount = mesh->m_V.Count();
                outBuffer.indexCount = mesh->m_F.Count() * 3;

                outBuffer.vertices = (float*)g_Arena.allocate(sizeof(float) * 3 * outBuffer.vertexCount);
                outBuffer.indices = (uint32_t*)g_Arena.allocate(sizeof(uint32_t) * outBuffer.indexCount);

                if (!outBuffer.vertices || !outBuffer.indices)
                {
                    break;
                }

                for (int v = 0; v < mesh->m_V.Count(); ++v)
                {
                    outBuffer.vertices[v * 3 + 0] = (float)mesh->m_V[v].x;
                    outBuffer.vertices[v * 3 + 1] = (float)mesh->m_V[v].y;
                    outBuffer.vertices[v * 3 + 2] = (float)mesh->m_V[v].z;
                }

                for (int f = 0; f < mesh->m_F.Count(); ++f)
                {
                    outBuffer.indices[f * 3 + 0] = (uint32_t)mesh->m_F[f].vi[0];
                    outBuffer.indices[f * 3 + 1] = (uint32_t)mesh->m_F[f].vi[1];
                    outBuffer.indices[f * 3 + 2] = (uint32_t)mesh->m_F[f].vi[2];
                }

                return true;
            }
        }

        return false;
    }
}

#endif // ALICE_RHINO_IMPLEMENTATION
