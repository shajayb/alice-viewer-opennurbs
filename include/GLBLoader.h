#ifndef GLB_LOADER_H
#define GLB_LOADER_H

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <curl/curl.h>
#include <vector>
#include <string>
#include <cstdio>
#include "MeshPrimitive.h"
#include "AliceMath.h"

struct GLBLoader
{
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        size_t realsize = size * nmemb;
        std::vector<uint8_t>* mem = (std::vector<uint8_t>*)userp;
        size_t oldsize = mem->size();
        mem->resize(oldsize + realsize);
        memcpy(mem->data() + oldsize, contents, realsize);
        return realsize;
    }

    static bool fetch(const char* url, std::vector<uint8_t>& buffer)
    {
        std::string sUrl = url;
        if(sUrl.find("http://") == 0 || sUrl.find("https://") == 0)
        {
            CURL* curl = curl_easy_init();
            if(!curl) return false;

            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buffer);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            return (res == CURLE_OK);
        }
        else
        {
            FILE* f = fopen(url, "rb");
            if(!f) return false;
            fseek(f, 0, SEEK_END);
            size_t size = ftell(f);
            fseek(f, 0, SEEK_SET);
            buffer.resize(size);
            fread(buffer.data(), 1, size, f);
            fclose(f);
            return true;
        }
    }

    static bool load(const char* url, MeshPrimitive& out_mesh, Math::Vec3& out_min, Math::Vec3& out_max)
    {
        std::vector<uint8_t> buffer;
        if(!fetch(url, buffer))
        {
            fprintf(stderr, "[GLBLoader] Failed to fetch GLB from %s\n", url);
            return false;
        }

        cgltf_options options = {};
        cgltf_data* data = NULL;
        cgltf_result result = cgltf_parse(&options, buffer.data(), buffer.size(), &data);
        if(result != cgltf_result_success)
        {
            fprintf(stderr, "[GLBLoader] Failed to parse GLB\n");
            return false;
        }

        result = cgltf_load_buffers(&options, data, NULL);
        if(result != cgltf_result_success)
        {
            fprintf(stderr, "[GLBLoader] Failed to load GLB buffers\n");
            cgltf_free(data);
            return false;
        }

        // Find first mesh/primitive
        if(data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
        {
            fprintf(stderr, "[GLBLoader] No meshes/primitives found\n");
            cgltf_free(data);
            return false;
        }

        cgltf_primitive* prim = &data->meshes[0].primitives[0];
        
        // Extract Pos and Normal
        cgltf_accessor* pos_acc = NULL;
        cgltf_accessor* norm_acc = NULL;

        for(int i=0; i<(int)prim->attributes_count; ++i)
        {
            if(prim->attributes[i].type == cgltf_attribute_type_position) pos_acc = prim->attributes[i].data;
            if(prim->attributes[i].type == cgltf_attribute_type_normal) norm_acc = prim->attributes[i].data;
        }

        if(!pos_acc)
        {
            fprintf(stderr, "[GLBLoader] No position attribute found\n");
            cgltf_free(data);
            return false;
        }

        if(pos_acc->has_min)
        {
            out_min = { pos_acc->min[0], pos_acc->min[1], pos_acc->min[2] };
        }
        if(pos_acc->has_max)
        {
            out_max = { pos_acc->max[0], pos_acc->max[1], pos_acc->max[2] };
        }

        size_t vcount = pos_acc->count;
        // Interleave Pos(3) and Normal(3) -> 6 floats per vertex
        float* verts = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));
        
        for(size_t i=0; i<vcount; ++i)
        {
            cgltf_accessor_read_float(pos_acc, i, &verts[i*6], 3);
            if(norm_acc)
                cgltf_accessor_read_float(norm_acc, i, &verts[i*6+3], 3);
            else
            {
                verts[i*6+3] = 0; verts[i*6+4] = 0; verts[i*6+5] = 1;
            }
        }

        // Indices
        size_t icount = prim->indices ? prim->indices->count : 0;
        unsigned int* indices = NULL;
        if(icount > 0)
        {
            indices = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
            for(size_t i=0; i<icount; ++i)
            {
                indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
            }
        }
        else
        {
            fprintf(stderr, "[GLBLoader] No indices found in GLB\n");
            cgltf_free(data);
            return false;
        }

        // Upload to GPU
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
        
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);

        cgltf_free(data);
        return true;
    }

    static bool loadECEF(const char* url, MeshPrimitive& out_mesh, Math::Vec3& out_min, Math::Vec3& out_max)
    {
        // PHASE 1: Construct a basic ECEF-to-Local test mesh as authorized by prompt.
        // We mock a tile at LLA (51.5074, -0.1278) - London.
        double lat0 = 0.898981; 
        double lon0 = -0.002230;
        
        const double a = 6378137.0;
        const double b = 6356752.314245;
        const double e2 = 1.0 - (b * b) / (a * a);
        double sinLat0 = sin(lat0);
        double N0 = a / sqrt(1.0 - e2 * sinLat0 * sinLat0);
        
        Math::DVec3 center_ecef;
        center_ecef.x = (N0 + 0.0) * cos(lat0) * cos(lon0);
        center_ecef.y = (N0 + 0.0) * cos(lat0) * sin(lon0);
        center_ecef.z = (N0 * (1.0 - e2) + 0.0) * sin(lat0);
        
        // THE FLOAT64 TRAP: Construct 4 vertices of a 100m plane in ECEF.
        // We use double precision to ensure relative precision is maintained.
        Math::DVec3 ecef_verts[4];
        double enu_mat[16];
        Math::denu_matrix(enu_mat, lat0, lon0);
        
        // Local ENU offsets for a 100m x 100m plane
        double offsets[4][3] = {
            {-50.0, -50.0, 0.0},
            { 50.0, -50.0, 0.0},
            { 50.0,  50.0, 0.0},
            {-50.0,  50.0, 0.0}
        };

        // Convert Local ENU offsets to ECEF to simulate raw ECEF data fetch
        // ECEF = Center + R^T * Local (where R is ECEF-to-ENU)
        // R is [East^T, North^T, Up^T]^T, so R^T is [East, North, Up]
        for(int i=0; i<4; ++i)
        {
            double lx = offsets[i][0], ly = offsets[i][1], lz = offsets[i][2];
            ecef_verts[i].x = center_ecef.x + enu_mat[0] * lx + enu_mat[4] * ly + enu_mat[8] * lz;
            ecef_verts[i].y = center_ecef.y + enu_mat[1] * lx + enu_mat[5] * ly + enu_mat[9] * lz;
            ecef_verts[i].z = center_ecef.z + enu_mat[2] * lx + enu_mat[6] * ly + enu_mat[10] * lz;
        }

        // Process back to local float32
        float* processed_verts = (float*)Alice::g_Arena.allocate(4 * 6 * sizeof(float));
        for(int i=0; i<4; ++i)
        {
            // Subtract center in double
            double dx = ecef_verts[i].x - center_ecef.x;
            double dy = ecef_verts[i].y - center_ecef.y;
            double dz = ecef_verts[i].z - center_ecef.z;
            
            // Apply ENU rotation matrix: local = R * delta
            // R rows are East, North, Up
            float lx = (float)(enu_mat[0] * dx + enu_mat[1] * dy + enu_mat[2] * dz);
            float ly = (float)(enu_mat[4] * dx + enu_mat[5] * dy + enu_mat[6] * dz);
            float lz = (float)(enu_mat[8] * dx + enu_mat[9] * dy + enu_mat[10] * dz);
            
            processed_verts[i*6+0] = lx;
            processed_verts[i*6+1] = ly;
            processed_verts[i*6+2] = lz;
            
            // Up normal for plane
            processed_verts[i*6+3] = 0.0f;
            processed_verts[i*6+4] = 0.0f;
            processed_verts[i*6+5] = 1.0f;
        }

        unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };
        out_mesh.count = 6;
        glGenVertexArrays(1, &out_mesh.vao);
        glGenBuffers(1, &out_mesh.vbo);
        glGenBuffers(1, &out_mesh.ebo);
        
        glBindVertexArray(out_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, out_mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, 4 * 6 * sizeof(float), processed_verts, GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out_mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

        out_min = {-50.0f, -50.0f, 0.0f};
        out_max = { 50.0f,  50.0f, 0.0f};

        return true;
    }
};

#endif
