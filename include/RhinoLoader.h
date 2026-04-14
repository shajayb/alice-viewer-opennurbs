#pragma once

#include <opennurbs.h>
#include "AliceMemory.h"
#include "AliceViewer.h"
#include <cstdio>
#include <string>
#include <filesystem>
#include <vector>

namespace Alice
{

class RhinoLoader
{
public:
    static bool LoadMesh(const char* filename, const char* objectName, Alice::Buffer<V3>& outBuffer, Alice::LinearArena& arena)
    {
        std::string foundPath = "";
        std::vector<std::string> searchPrefixes = { "", "./build/bin/", "../", "../../build/bin/" };
        
        for (const auto& prefix : searchPrefixes)
        {
            std::string probe = prefix + filename;
            if (std::filesystem::exists(probe))
            {
                foundPath = probe;
                break;
            }
        }

        if (foundPath.empty()) return false;

        ONX_Model model;
        if (!model.Read(foundPath.c_str())) return false;

        ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
        const ON_ModelComponent* component = nullptr;
        
        while ((component = it.NextComponent()) != nullptr)
        {
            const ON_ModelGeometryComponent* geometryComponent = ON_ModelGeometryComponent::Cast(component);
            if (!geometryComponent) continue;

            const ON_Geometry* geom = geometryComponent->Geometry(nullptr);
            if (!geom) continue;

            const ON_Mesh* mesh = ON_Mesh::Cast(geom);
            if (!mesh) continue;

            const ON_3dmObjectAttributes* attrs = geometryComponent->Attributes(nullptr);
            if (!attrs) continue;

            ON_String currentName(attrs->m_name);
            bool match = false;
            
            // If objectName is null/empty, take the first mesh. Otherwise match name.
            if (objectName == nullptr || strlen(objectName) == 0)
            {
                match = true; 
            }
            else if (currentName.Length() > 0 && _stricmp(currentName.Array(), objectName) == 0)
            {
                match = true;
            }

            if (match)
            {
                int faceCount = mesh->m_F.Count();
                for (int i = 0; i < faceCount; ++i)
                {
                    const ON_MeshFace& face = mesh->m_F[i];
                    
                    // Vertex 0
                    outBuffer.push_back(V3((float)mesh->m_V[face.vi[0]].x, (float)mesh->m_V[face.vi[0]].y, (float)mesh->m_V[face.vi[0]].z));
                    // Vertex 1
                    outBuffer.push_back(V3((float)mesh->m_V[face.vi[1]].x, (float)mesh->m_V[face.vi[1]].y, (float)mesh->m_V[face.vi[1]].z));
                    // Vertex 2
                    outBuffer.push_back(V3((float)mesh->m_V[face.vi[2]].x, (float)mesh->m_V[face.vi[2]].y, (float)mesh->m_V[face.vi[2]].z));

                    if (face.IsQuad())
                    {
                        // Vertex 2 (again for second triangle)
                        outBuffer.push_back(V3((float)mesh->m_V[face.vi[2]].x, (float)mesh->m_V[face.vi[2]].y, (float)mesh->m_V[face.vi[2]].z));
                        // Vertex 3
                        outBuffer.push_back(V3((float)mesh->m_V[face.vi[3]].x, (float)mesh->m_V[face.vi[3]].y, (float)mesh->m_V[face.vi[3]].z));
                        // Vertex 0
                        outBuffer.push_back(V3((float)mesh->m_V[face.vi[0]].x, (float)mesh->m_V[face.vi[0]].y, (float)mesh->m_V[face.vi[0]].z));
                    }
                }
                return true;
            }
        }

        return false;
    }
};

} // namespace Alice
