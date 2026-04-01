#include "RhinoIO.h"
#include "AliceMemory.h"

// STRICT MANDATE: Include opennurbs only in the .cpp
#include <opennurbs.h>

namespace Alice
{
    extern LinearArena g_Arena; // Defined in sketch.cpp switchboard

    bool RhinoParser::LoadFile(const char* path, RhinoMeshBuffer& outBuffer)
    {
        ONX_Model model;
        
        // OpenNURBS 8.x prefers ONX_Model for high-level I/O
        if (!model.Read(path))
        {
            return false;
        }

        // Use iterator to find the first mesh in the model geometry table
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

                // Pre-size buffers using the arena - zero heap allocation in loop
                outBuffer.vertices = (float*)g_Arena.allocate(sizeof(float) * 3 * outBuffer.vertexCount);
                outBuffer.indices = (uint32_t*)g_Arena.allocate(sizeof(uint32_t) * outBuffer.indexCount);

                if (!outBuffer.vertices || !outBuffer.indices)
                {
                    break;
                }

                // Translation: ON_Mesh to DOD structure
                // Vertex translation
                for (int v = 0; v < mesh->m_V.Count(); ++v)
                {
                    outBuffer.vertices[v * 3 + 0] = (float)mesh->m_V[v].x;
                    outBuffer.vertices[v * 3 + 1] = (float)mesh->m_V[v].y;
                    outBuffer.vertices[v * 3 + 2] = (float)mesh->m_V[v].z;
                }

                // Index translation (triangulated faces)
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
