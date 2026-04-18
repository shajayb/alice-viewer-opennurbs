#ifndef TILESET_H
#define TILESET_H

#include <glad/glad.h>
#include <nlohmann/json.hpp>
#include "AliceMath.h"
#include "MeshPrimitive.h"
#include "GLBLoader.h"
#include "cgltf.h"
#include <vector>
#include <string>

using json = nlohmann::json;

namespace Alice::Alg {

struct Tile
{
    MeshPrimitive mesh;
    Math::Vec3 min, max;
    float geometricError;
    std::string contentUri;
    std::vector<Tile*> children;
    bool isLoaded = false;

    void draw()
    {
        if(isLoaded) mesh.draw();
        for(auto child : children) child->draw();
    }
};

class Tileset
{
public:
    Tile* root = nullptr;
    std::string baseUrl;
    Math::DVec3 centerEcef = {0,0,0};
    bool hasCenter = false;
    double enuMat[16];

    bool load(const char* url)
    {
        std::vector<uint8_t> buffer;
        if(!GLBLoader::fetch(url, buffer)) return false;

        json tileset;
        try {
            tileset = json::parse(buffer.begin(), buffer.end());
        } catch (...) {
            return false;
        }

        baseUrl = url;
        size_t pos = baseUrl.find_last_of('/');
        if(pos != std::string::npos) baseUrl = baseUrl.substr(0, pos + 1);
        else if(baseUrl.find('\\') != std::string::npos) baseUrl = baseUrl.substr(0, baseUrl.find_last_of('\\') + 1);
        else baseUrl = "./";

        if(!tileset.contains("root")) return false;

        // Extract center from root bounding volume if available
        if(tileset["root"].contains("boundingVolume") && tileset["root"]["boundingVolume"].contains("sphere"))
        {
            auto sphere = tileset["root"]["boundingVolume"]["sphere"];
            if(sphere.is_array() && sphere.size() >= 3)
            {
                centerEcef.x = sphere[0].get<double>();
                centerEcef.y = sphere[1].get<double>();
                centerEcef.z = sphere[2].get<double>();
                hasCenter = true;
            }
        }

        if(hasCenter)
        {
            Math::LLA lla = Math::ecef_to_lla(centerEcef);
            Math::denu_matrix(enuMat, lla.lat, lla.lon);
        }

        root = parseTile(tileset["root"]);
        return root != nullptr;
    }

    Tile* parseTile(const json& t)
    {
        Tile* tile = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
        new (tile) Tile(); 

        if(t.contains("geometricError")) tile->geometricError = t["geometricError"].get<float>();
        
        if(t.contains("content") && t["content"].contains("uri"))
        {
            tile->contentUri = t["content"]["uri"];
            loadTileContent(tile);
        }

        if(t.contains("children") && t["children"].is_array())
        {
            for(auto& c : t["children"])
            {
                Tile* child = parseTile(c);
                if(child) tile->children.push_back(child);
            }
        }

        return tile;
    }

    void loadTileContent(Tile* tile)
    {
        std::string glbUrl = baseUrl + tile->contentUri;
        std::vector<uint8_t> glbBuffer;
        if(!GLBLoader::fetch(glbUrl.c_str(), glbBuffer)) return;

        cgltf_options options = {};
        cgltf_data* data = NULL;
        cgltf_result result = cgltf_parse(&options, glbBuffer.data(), glbBuffer.size(), &data);
        if(result != cgltf_result_success) return;

        result = cgltf_load_buffers(&options, data, NULL);
        if(result != cgltf_result_success) { cgltf_free(data); return; }

        if(data->meshes_count == 0 || data->meshes[0].primitives_count == 0) { cgltf_free(data); return; }

        cgltf_primitive* prim = &data->meshes[0].primitives[0];
        cgltf_accessor *pos_acc = NULL, *norm_acc = NULL;

        for(int i=0; i<(int)prim->attributes_count; ++i)
        {
            if(prim->attributes[i].type == cgltf_attribute_type_position) pos_acc = prim->attributes[i].data;
            if(prim->attributes[i].type == cgltf_attribute_type_normal) norm_acc = prim->attributes[i].data;
        }

        if(!pos_acc) { cgltf_free(data); return; }

        size_t vcount = pos_acc->count;
        float* verts = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));

        if(!hasCenter)
        {
            float f[3];
            cgltf_accessor_read_float(pos_acc, 0, f, 3);
            centerEcef = { (double)f[0], (double)f[1], (double)f[2] };
            Math::LLA lla = Math::ecef_to_lla(centerEcef);
            Math::denu_matrix(enuMat, lla.lat, lla.lon);
            hasCenter = true;
        }

        tile->min = { 1e30f, 1e30f, 1e30f };
        tile->max = { -1e30f, -1e30f, -1e30f };

        for(size_t i=0; i<vcount; ++i)
        {
            float p[3], n[3] = {0,0,1};
            cgltf_accessor_read_float(pos_acc, i, p, 3);
            if(norm_acc) cgltf_accessor_read_float(norm_acc, i, n, 3);

            float lx, ly, lz;
            if(p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f) { lx=0; ly=0; lz=0; }
            else
            {
                double dx = (double)p[0] - centerEcef.x;
                double dy = (double)p[1] - centerEcef.y;
                double dz = (double)p[2] - centerEcef.z;
                lx = (float)(enuMat[0] * dx + enuMat[1] * dy + enuMat[2] * dz);
                ly = (float)(enuMat[4] * dx + enuMat[5] * dy + enuMat[6] * dz);
                lz = (float)(enuMat[8] * dx + enuMat[9] * dy + enuMat[10] * dz);
            }

            verts[i*6+0] = lx; verts[i*6+1] = ly; verts[i*6+2] = lz;
            
            if(lx < tile->min.x) tile->min.x = lx;
            if(ly < tile->min.y) tile->min.y = ly;
            if(lz < tile->min.z) tile->min.z = lz;
            if(lx > tile->max.x) tile->max.x = lx;
            if(ly > tile->max.y) tile->max.y = ly;
            if(lz > tile->max.z) tile->max.z = lz;

            float nx = (float)(enuMat[0] * n[0] + enuMat[1] * n[1] + enuMat[2] * n[2]);
            float ny = (float)(enuMat[4] * n[0] + enuMat[5] * n[1] + enuMat[6] * n[2]);
            float nz = (float)(enuMat[8] * n[0] + enuMat[9] * n[1] + enuMat[10] * n[2]);
            float ilen = 1.0f / sqrtf(nx*nx + ny*ny + nz*nz + 1e-12f);
            verts[i*6+3] = nx*ilen; verts[i*6+4] = ny*ilen; verts[i*6+5] = nz*ilen;
        }

        size_t icount = prim->indices ? prim->indices->count : 0;
        unsigned int* indices = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
        for(size_t i=0; i<icount; ++i) indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);

        tile->mesh.instanceVbo = 0;
        tile->mesh.count = (int)icount;
        glGenVertexArrays(1, &tile->mesh.vao);
        glGenBuffers(1, &tile->mesh.vbo);
        glGenBuffers(1, &tile->mesh.ebo);
        glBindVertexArray(tile->mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, tile->mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, vcount * 6 * sizeof(float), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile->mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), indices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

        cgltf_free(data);
        tile->isLoaded = true;
    }

    void draw()
    {
        if(root) root->draw();
    }
};

} // namespace Alice::Alg

#endif
