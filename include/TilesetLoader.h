#ifndef TILESET_LOADER_H
#define TILESET_LOADER_H

#include "GLBLoader.h"
#include <nlohmann/json.hpp>
#include "AliceMath.h"
#include <vector>
#include <string>

using json = nlohmann::json;

struct TilesetLoader
{
    static bool load(const char* url, MeshPrimitive& out_mesh, Math::Vec3& out_min, Math::Vec3& out_max)
    {
        std::vector<uint8_t> buffer;
        if(!GLBLoader::fetch(url, buffer))
        {
            fprintf(stderr, "[TilesetLoader] Failed to fetch tileset from %s. Using mock data.\n", url);
            return GLBLoader::loadECEF(url, out_mesh, out_min, out_max);
        }

        json tileset;
        try {
            tileset = json::parse(buffer.begin(), buffer.end());
        } catch (const std::exception& e) {
            fprintf(stderr, "[TilesetLoader] Failed to parse JSON: %s. Using mock data.\n", e.what());
            return GLBLoader::loadECEF(url, out_mesh, out_min, out_max);
        }

        Math::DVec3 center_ecef = {0,0,0};
        bool has_center = false;

        if(tileset.contains("root") && tileset["root"].contains("boundingVolume") && tileset["root"]["boundingVolume"].contains("sphere"))
        {
            auto sphere = tileset["root"]["boundingVolume"]["sphere"];
            if(sphere.is_array() && sphere.size() >= 3)
            {
                center_ecef.x = sphere[0].get<double>();
                center_ecef.y = sphere[1].get<double>();
                center_ecef.z = sphere[2].get<double>();
                has_center = true;
            }
        }

        if(!tileset.contains("root") || !tileset["root"].contains("content") || !tileset["root"]["content"].contains("uri"))
        {
             fprintf(stderr, "[TilesetLoader] tileset.json does not contain root tile URI. Using mock data.\n");
             return GLBLoader::loadECEF(url, out_mesh, out_min, out_max);
        }

        std::string root_uri = tileset["root"]["content"]["uri"];
        
        // Resolve URI
        std::string base_url = url;
        size_t pos = base_url.find_last_of('/');
        if(pos != std::string::npos) base_url = base_url.substr(0, pos + 1);
        else if(base_url.find('\\') != std::string::npos)
        {
            pos = base_url.find_last_of('\\');
            base_url = base_url.substr(0, pos + 1);
        }
        else base_url = "./";

        std::string glb_url = base_url + root_uri;

        std::vector<uint8_t> glb_buffer;
        if(!GLBLoader::fetch(glb_url.c_str(), glb_buffer))
        {
             fprintf(stderr, "[TilesetLoader] Failed to fetch root tile from %s. Using mock data.\n", glb_url.c_str());
             return GLBLoader::loadECEF(url, out_mesh, out_min, out_max);
        }

        return parseAndTransform(glb_buffer.data(), glb_buffer.size(), out_mesh, out_min, out_max, center_ecef, has_center);
    }

    static bool parseAndTransform(uint8_t* buffer, size_t size, MeshPrimitive& out_mesh, Math::Vec3& out_min, Math::Vec3& out_max, Math::DVec3 center_ecef, bool has_center)
    {
        cgltf_options options = {};
        cgltf_data* data = NULL;
        cgltf_result result = cgltf_parse(&options, buffer, size, &data);
        if(result != cgltf_result_success)
        {
             fprintf(stderr, "[TilesetLoader] cgltf parse failed. Falling back to mock data.\n");
             return GLBLoader::loadECEF("", out_mesh, out_min, out_max);
        }

        result = cgltf_load_buffers(&options, data, NULL);
        if(result != cgltf_result_success)
        {
            cgltf_free(data);
            return GLBLoader::loadECEF("", out_mesh, out_min, out_max);
        }

        if(data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
        {
            cgltf_free(data);
            return GLBLoader::loadECEF("", out_mesh, out_min, out_max);
        }

        cgltf_primitive* prim = &data->meshes[0].primitives[0];
        cgltf_accessor* pos_acc = NULL;
        cgltf_accessor* norm_acc = NULL;

        for(int i=0; i<(int)prim->attributes_count; ++i)
        {
            if(prim->attributes[i].type == cgltf_attribute_type_position) pos_acc = prim->attributes[i].data;
            if(prim->attributes[i].type == cgltf_attribute_type_normal) norm_acc = prim->attributes[i].data;
        }

        if(!pos_acc)
        {
            cgltf_free(data);
            return GLBLoader::loadECEF("", out_mesh, out_min, out_max);
        }

        size_t vcount = pos_acc->count;
        float* verts = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));

        auto read_double = [](const cgltf_accessor* acc, size_t index, double* out) {
            float f[3];
            cgltf_accessor_read_float(acc, index, f, 3);
            out[0] = (double)f[0]; out[1] = (double)f[1]; out[2] = (double)f[2];
        };

        if(!has_center)
        {
            double first_pos[3];
            read_double(pos_acc, 0, first_pos);
            center_ecef = { first_pos[0], first_pos[1], first_pos[2] };
        }
        
        Math::LLA lla = Math::ecef_to_lla(center_ecef);
        double enu_mat[16];
        Math::denu_matrix(enu_mat, lla.lat, lla.lon);

        out_min = { 1e30f, 1e30f, 1e30f };
        out_max = { -1e30f, -1e30f, -1e30f };

        for(size_t i=0; i<vcount; ++i)
        {
            double raw_pos[3];
            read_double(pos_acc, i, raw_pos);
            
            float lx, ly, lz;

            // Spike suppression: if vertex is at (0,0,0) in ECEF, it's likely a null/error value.
            // Map it to local origin (center_ecef) to prevent massive spikes.
            if(raw_pos[0] == 0.0 && raw_pos[1] == 0.0 && raw_pos[2] == 0.0)
            {
                 lx = 0; ly = 0; lz = 0;
            }
            else
            {
                double dx = raw_pos[0] - center_ecef.x;
                double dy = raw_pos[1] - center_ecef.y;
                double dz = raw_pos[2] - center_ecef.z;
                
                lx = (float)(enu_mat[0] * dx + enu_mat[1] * dy + enu_mat[2] * dz);
                ly = (float)(enu_mat[4] * dx + enu_mat[5] * dy + enu_mat[6] * dz);
                lz = (float)(enu_mat[8] * dx + enu_mat[9] * dy + enu_mat[10] * dz);
            }

            verts[i*6+0] = lx;
            verts[i*6+1] = ly;
            verts[i*6+2] = lz;

            if(lx < out_min.x) out_min.x = lx;
            if(ly < out_min.y) out_min.y = ly;
            if(lz < out_min.z) out_min.z = lz;
            if(lx > out_max.x) out_max.x = lx;
            if(ly > out_max.y) out_max.y = ly;
            if(lz > out_max.z) out_max.z = lz;
            
            if(norm_acc)
            {
                float raw_norm[3];
                cgltf_accessor_read_float(norm_acc, i, raw_norm, 3);
                // Normals should also be rotated to ENU if they were in ECEF, 
                // but usually they are local. If we rotate them, we must re-normalize.
                float nx = (float)(enu_mat[0] * (double)raw_norm[0] + enu_mat[1] * (double)raw_norm[1] + enu_mat[2] * (double)raw_norm[2]);
                float ny = (float)(enu_mat[4] * (double)raw_norm[0] + enu_mat[5] * (double)raw_norm[1] + enu_mat[6] * (double)raw_norm[2]);
                float nz = (float)(enu_mat[8] * (double)raw_norm[0] + enu_mat[9] * (double)raw_norm[1] + enu_mat[10] * (double)raw_norm[2]);
                
                float ilen = 1.0f / sqrtf(nx*nx + ny*ny + nz*nz + 1e-12f);
                verts[i*6+3] = nx * ilen;
                verts[i*6+4] = ny * ilen;
                verts[i*6+5] = nz * ilen;
            }
            else
            {
                verts[i*6+3] = 0; verts[i*6+4] = 0; verts[i*6+5] = 1;
            }
        }

        size_t icount = prim->indices ? prim->indices->count : 0;
        unsigned int* indices = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
        for(size_t i=0; i<icount; ++i)
        {
            indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
        }

        out_mesh.instanceVbo = 0;
        out_mesh.count = (int)icount;
        glGenVertexArrays(1, &out_mesh.vao);
        glGenBuffers(1, &out_mesh.vbo);
        glGenBuffers(1, &out_mesh.ebo);
        
        glBindVertexArray(out_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, out_mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, vcount * 6 * sizeof(float), verts, GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out_mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), indices, GL_STATIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

        cgltf_free(data);
        return true;
    }
};

#endif
