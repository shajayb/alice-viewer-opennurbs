#ifndef TILESET_LOADER_H
#define TILESET_LOADER_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "AliceNetwork.h"
#include "ApiKeyReader.h"
#include "AliceMath.h"
#include "AliceMemory.h"
#include "MeshPrimitive.h"
#include "cgltf.h"

using json = nlohmann::json;

namespace Alice
{
    struct TileNode
    {
        Math::DVec3 sphereCenter;
        double sphereRadius;
        float geometricError;
        double transform[16];
        double worldTransform[16];
        const char* contentUri;
        const char* baseUrl;
        int firstChild;
        int childCount;
        MeshPrimitive mesh;
        bool isLoaded;
        bool isExternal;
        bool hasContent;
        bool refineAdd; // true for ADD, false for REPLACE
    };

    struct TilesetLoader
    {
        static std::string ConstructGoogleTilesetURL()
        {
            char key[256];
            if (!ApiKeyReader::GetGoogleKey(key, 256)) return "";
            std::string url = "https://tile.googleapis.com/v1/3dtiles/root.json?key=";
            url += key;
            return url;
        }

        static bool FetchRootTileset(const std::string& url, json& out_tileset)
        {
            std::vector<uint8_t> buffer;
            long status = 0;
            std::string body;
            if (!Network::Fetch(url.c_str(), buffer, &status, &body)) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: Network::Fetch failed for %s (Status: %ld)\n", url.c_str(), status);
                if (!body.empty()) fprintf(stderr, "[TilesetLoader] Response Body: %s\n", body.c_str());
                return false;
            }
            try
            {
                out_tileset = json::parse(buffer.begin(), buffer.end());
                return true;
            }
            catch (...)
            {
                fprintf(stderr, "[TilesetLoader] ERROR: JSON parse failed for %s\n", url.c_str());
                return false;
            }
        }

        static int ParseRecursive(const json& t, Buffer<TileNode>& nodes, LinearArena& arena, const std::string& baseUrl, const double* parentTransform = nullptr)
        {
            if (nodes.count >= nodes.capacity) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: Nodes buffer capacity exceeded (%d)\n", (int)nodes.capacity);
                return -1;
            }
            int idx = (int)nodes.count++;
            
            TileNode& node = nodes[idx];
            memset(&node, 0, sizeof(TileNode));

            // Default transform to identity
            double localTransform[16];
            Math::mat4d_identity(localTransform);
            if (t.contains("transform") && t["transform"].is_array())
            {
                for (int i = 0; i < 16; ++i) localTransform[i] = t["transform"][i].get<double>();
            }
            for (int i = 0; i < 16; ++i) node.transform[i] = localTransform[i];

            if (parentTransform)
            {
                Math::mat4d_mul(node.worldTransform, parentTransform, localTransform);
            }
            else
            {
                for (int i = 0; i < 16; ++i) node.worldTransform[i] = localTransform[i];
            }

            // Store baseUrl
            char* bBuf = (char*)arena.allocate(baseUrl.size() + 1);
            if (bBuf)
            {
                memcpy(bBuf, baseUrl.c_str(), baseUrl.size() + 1);
                node.baseUrl = bBuf;
            }

            if (t.contains("boundingVolume"))
            {
                if (t["boundingVolume"].contains("sphere"))
                {
                    auto s = t["boundingVolume"]["sphere"];
                    node.sphereCenter = { s[0].get<double>(), s[1].get<double>(), s[2].get<double>() };
                    node.sphereRadius = s[3].get<double>();
                }
                else if (t["boundingVolume"].contains("box"))
                {
                    auto b = t["boundingVolume"]["box"];
                    node.sphereCenter = { b[0].get<double>(), b[1].get<double>(), b[2].get<double>() };
                    double hx = b[3].get<double>(), hy = b[7].get<double>(), hz = b[11].get<double>();
                    node.sphereRadius = sqrt(hx*hx + hy*hy + hz*hz);
                }

                // Transform sphereCenter to world space (ECEF)
                Math::mat4d_transform_point(node.sphereCenter, node.worldTransform);
            }

            node.geometricError = t.value("geometricError", 0.0f);
            node.refineAdd = (t.value("refine", "") == "ADD");

            if (t.contains("content"))
            {
                std::string uri = t["content"].value("uri", "");
                if (uri.empty()) uri = t["content"].value("url", "");
                if (!uri.empty())
                {
                    char* buf = (char*)arena.allocate(uri.size() + 1);
                    if (buf)
                    {
                        memcpy(buf, uri.c_str(), uri.size() + 1);
                        node.contentUri = buf;
                        node.hasContent = true;
                        node.isExternal = (uri.find(".json") != std::string::npos);
                    }
                }
            }

            if (t.contains("children") && t["children"].is_array())
            {
                node.childCount = (int)t["children"].size();
                node.firstChild = (int)nodes.count;
                for (const auto& c : t["children"])
                {
                    if (ParseRecursive(c, nodes, arena, baseUrl, node.worldTransform) == -1) return -1;
                }
            }

            return idx;
        }

        static void FetchAndGraft(int nodeIdx, Buffer<TileNode>& nodes, LinearArena& arena)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count) return;
            if (!nodes[nodeIdx].isExternal || nodes[nodeIdx].isLoaded) return;

            std::string url = nodes[nodeIdx].baseUrl;
            std::string uri = nodes[nodeIdx].contentUri;
            if (uri.find("http") == 0) url = uri;
            else url += uri;

            std::vector<uint8_t> buffer;
            if (!Network::Fetch(url.c_str(), buffer)) return;

            json external;
            try { external = json::parse(buffer.begin(), buffer.end()); } catch(...) { return; }

            if (external.contains("root"))
            {
                std::string newBaseUrl = url;
                size_t pos = newBaseUrl.find_last_of('/');
                if (pos != std::string::npos) newBaseUrl = newBaseUrl.substr(0, pos + 1);

                int externalRootIdx = ParseRecursive(external["root"], nodes, arena, newBaseUrl, nodes[nodeIdx].worldTransform);
                
                nodes[nodeIdx].firstChild = externalRootIdx;
                nodes[nodeIdx].childCount = 1;
                nodes[nodeIdx].isLoaded = true;
                nodes[nodeIdx].isExternal = false; 
            }
        }


        static void ExtractFrustumPlanes(const float* m, float planes[6][4])
        {
            // Left
            planes[0][0] = m[3] + m[0]; planes[0][1] = m[7] + m[4]; planes[0][2] = m[11] + m[8]; planes[0][3] = m[15] + m[12];
            // Right
            planes[1][0] = m[3] - m[0]; planes[1][1] = m[7] - m[4]; planes[1][2] = m[11] - m[8]; planes[1][3] = m[15] - m[12];
            // Bottom
            planes[2][0] = m[3] + m[1]; planes[2][1] = m[7] + m[5]; planes[2][2] = m[11] + m[9]; planes[2][3] = m[15] + m[13];
            // Top
            planes[3][0] = m[3] - m[1]; planes[3][1] = m[7] - m[5]; planes[3][2] = m[11] - m[9]; planes[3][3] = m[15] - m[13];
            // Near (Directive: M[3]-M[2] for Reversed-Z with glClipControl(GL_ZERO_TO_ONE))
            planes[4][0] = m[3] - m[2]; planes[4][1] = m[7] - m[6]; planes[4][2] = m[11] - m[10]; planes[4][3] = m[15] - m[14];
            // Far (Directive: M[2])
            planes[5][0] = m[2]; planes[5][1] = m[6]; planes[5][2] = m[10]; planes[5][3] = m[14];

            for (int i = 0; i < 6; ++i)
            {
                float len = sqrtf(planes[i][0] * planes[i][0] + planes[i][1] * planes[i][1] + planes[i][2] * planes[i][2]);
                planes[i][0] /= len; planes[i][1] /= len; planes[i][2] /= len; planes[i][3] /= len;
            }
        }

        static bool IsSphereInFrustum(const float planes[6][4], double cx, double cy, double cz, double radius)
        {
            for (int i = 0; i < 6; ++i)
            {
                if (planes[i][0] * cx + planes[i][1] * cy + planes[i][2] * cz + planes[i][3] <= -radius) return false;
            }
            return true;
        }

        static bool TraverseAndGraft(int nodeIdx, Buffer<TileNode>& nodes, Math::DVec3 cameraPos, float viewportHeight, float fov, float sseThreshold, const float frustumPlanes[6][4], Buffer<int>& activeNodes, LinearArena& arena, int depth, int& nodesVisited, int maxNodes)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count || nodesVisited >= maxNodes || depth > 32) return false;
            nodesVisited++;

            // 0. Frustum Culling
            if (!IsSphereInFrustum(frustumPlanes, nodes[nodeIdx].sphereCenter.x, nodes[nodeIdx].sphereCenter.y, nodes[nodeIdx].sphereCenter.z, nodes[nodeIdx].sphereRadius))
            {
                return false;
            }

            // 1. Calculate Screen Space Error (SSE)
            double dx = nodes[nodeIdx].sphereCenter.x - cameraPos.x;
            double dy = nodes[nodeIdx].sphereCenter.y - cameraPos.y;
            double dz = nodes[nodeIdx].sphereCenter.z - cameraPos.z;
            double distSq = dx * dx + dy * dy + dz * dz;
            double distance = sqrt(distSq);
            distance = (std::max)(distance, 1e-6);

            float geometricError = nodes[nodeIdx].geometricError;
            double sse = (geometricError * viewportHeight) / (distance * 2.0 * tan(fov * 0.5));

            bool shouldDescend = sse > sseThreshold;

            if (shouldDescend && nodes[nodeIdx].isExternal)
            {
                FetchAndGraft(nodeIdx, nodes, arena);
            }

            bool hasChildren = (nodes[nodeIdx].childCount > 0);
            bool refined = false;

            if (shouldDescend && hasChildren)
            {
                int first = nodes[nodeIdx].firstChild;
                int count = nodes[nodeIdx].childCount;
                
                bool allChildrenReady = true;
                for (int i = 0; i < count; ++i)
                {
                    // Recursively process children
                    if (!TraverseAndGraft(first + i, nodes, cameraPos, viewportHeight, fov, sseThreshold, frustumPlanes, activeNodes, arena, depth + 1, nodesVisited, maxNodes))
                    {
                        // If a child didn't refine (e.g. culled or didn't meet SSE), it's "ready" in the sense it doesn't need to be rendered
                    }
                    
                    // Crucial: In REPLACE mode, if any child that SHOULD be rendered is NOT loaded, we cannot cull the parent.
                    // This prevents holes/popping and Z-fighting if parent and children are both rendered.
                    if (!nodes[first + i].isLoaded && nodes[first + i].hasContent && !nodes[first + i].isExternal)
                    {
                        allChildrenReady = false;
                    }
                }

                if (nodes[nodeIdx].refineAdd)
                {
                    // ADD: Render both parent and children
                    if (nodes[nodeIdx].hasContent && !nodes[nodeIdx].isExternal && nodes[nodeIdx].isLoaded)
                    {
                        activeNodes.push_back(nodeIdx);
                    }
                    refined = true;
                }
                else
                {
                    // REPLACE: If children were processed AND are loaded, parent is omitted
                    if (allChildrenReady)
                    {
                        refined = true;
                    }
                    else if (nodes[nodeIdx].hasContent && !nodes[nodeIdx].isExternal && nodes[nodeIdx].isLoaded)
                    {
                        // Fallback to parent if children aren't ready
                        activeNodes.push_back(nodeIdx);
                        refined = true;
                    }
                }
            }
            else
            {
                // Terminal node or decided not to descend: add if it has content
                if (nodes[nodeIdx].hasContent && !nodes[nodeIdx].isExternal && nodes[nodeIdx].isLoaded)
                {
                    activeNodes.push_back(nodeIdx);
                    refined = true;
                }
            }

            return refined;
        }

        struct AsyncLoadRequest
        {
            int nodeIdx;
            char url[1024];
            Math::DVec3 centerEcef;
            double enuMat[16];
            double transform[16];
            
            // Output data
            float* verts;
            unsigned int* indices;
            size_t vcount;
            size_t icount;
            Math::Vec3 min, max;
            bool success;
            bool completed;
        };

        static void ProcessAsyncLoad(AsyncLoadRequest* req)
        {
            std::vector<uint8_t> buffer;
            if (!Network::Fetch(req->url, buffer)) { req->success = false; req->completed = true; return; }

            cgltf_options options = {};
            cgltf_data* data = NULL;
            cgltf_result result = cgltf_parse(&options, buffer.data(), buffer.size(), &data);
            if (result != cgltf_result_success) { req->success = false; req->completed = true; return; }

            result = cgltf_load_buffers(&options, data, NULL);
            if (result != cgltf_result_success) { cgltf_free(data); req->success = false; req->completed = true; return; }

            if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) { cgltf_free(data); req->success = false; req->completed = true; return; }

            cgltf_primitive* prim = &data->meshes[0].primitives[0];
            cgltf_accessor *pos_acc = NULL, *norm_acc = NULL, *uv_acc = NULL;

            for (int i = 0; i < (int)prim->attributes_count; ++i)
            {
                if (prim->attributes[i].type == cgltf_attribute_type_position) pos_acc = prim->attributes[i].data;
                if (prim->attributes[i].type == cgltf_attribute_type_normal) norm_acc = prim->attributes[i].data;
                if (prim->attributes[i].type == cgltf_attribute_type_texcoord) uv_acc = prim->attributes[i].data;
            }

            if (!pos_acc) { cgltf_free(data); req->success = false; req->completed = true; return; }

            req->vcount = pos_acc->count;
            int stride = 8; 
            req->verts = (float*)malloc(req->vcount * stride * sizeof(float));

            req->min = { 1e30f, 1e30f, 1e30f };
            req->max = { -1e30f, -1e30f, -1e30f };

            for (size_t i = 0; i < req->vcount; ++i)
            {
                float p[3], n[3] = { 0,0,1 }, uv[2] = { 0,0 };
                cgltf_accessor_read_float(pos_acc, i, p, 3);
                if (norm_acc) cgltf_accessor_read_float(norm_acc, i, n, 3);
                if (uv_acc) cgltf_accessor_read_float(uv_acc, i, uv, 2);

                double relX = req->transform[12] - req->centerEcef.x;
                double relY = req->transform[13] - req->centerEcef.y;
                double relZ = req->transform[14] - req->centerEcef.z;

                double dx = req->transform[0] * p[0] + req->transform[4] * p[1] + req->transform[8] * p[2] + relX;
                double dy = req->transform[1] * p[0] + req->transform[5] * p[1] + req->transform[9] * p[2] + relY;
                double dz = req->transform[2] * p[0] + req->transform[6] * p[1] + req->transform[10] * p[2] + relZ;

                double lx = req->enuMat[0] * dx + req->enuMat[1] * dy + req->enuMat[2] * dz; 
                double ly = req->enuMat[4] * dx + req->enuMat[5] * dy + req->enuMat[6] * dz; 
                double lz = req->enuMat[8] * dx + req->enuMat[9] * dy + req->enuMat[10] * dz; 

                float gx = (float)lx;    
                float gy = (float)lz;    
                float gz = (float)(-ly); 

                req->verts[i * stride + 0] = gx; req->verts[i * stride + 1] = gy; req->verts[i * stride + 2] = gz;

                if (gx < req->min.x) req->min.x = gx;
                if (gy < req->min.y) req->min.y = gy;
                if (gz < req->min.z) req->min.z = gz;
                if (gx > req->max.x) req->max.x = gx;
                if (gy > req->max.y) req->max.y = gy;
                if (gz > req->max.z) req->max.z = gz;

                double nx_rot = req->transform[0] * n[0] + req->transform[4] * n[1] + req->transform[8] * n[2];
                double ny_rot = req->transform[1] * n[0] + req->transform[5] * n[1] + req->transform[9] * n[2];
                double nz_rot = req->transform[2] * n[0] + req->transform[6] * n[1] + req->transform[10] * n[2];

                double enx = req->enuMat[0] * nx_rot + req->enuMat[1] * ny_rot + req->enuMat[2] * nz_rot;
                double eny = req->enuMat[4] * nx_rot + req->enuMat[5] * ny_rot + req->enuMat[6] * nz_rot;
                double enz = req->enuMat[8] * nx_rot + req->enuMat[9] * ny_rot + req->enuMat[10] * nz_rot;

                float ngx = (float)enx;
                float ngy = (float)enz;
                float ngz = (float)(-eny);

                float ilen = 1.0f / sqrtf(ngx * ngx + ngy * ngy + ngz * ngz + 1e-12f);
                req->verts[i * stride + 3] = ngx * ilen; req->verts[i * stride + 4] = ngy * ilen; req->verts[i * stride + 5] = ngz * ilen;
                req->verts[i * stride + 6] = uv[0]; req->verts[i * stride + 7] = uv[1];
            }

            req->icount = prim->indices ? prim->indices->count : 0;
            req->indices = (unsigned int*)malloc(req->icount * sizeof(unsigned int));
            for (size_t i = 0; i < req->icount; ++i) req->indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);

            cgltf_free(data);
            req->success = true;
            req->completed = true;
        }

        static bool LoadGLBWithENU(const char* url, MeshPrimitive& out_mesh, Math::DVec3 centerEcef, double* enuMat, const double* transform, Math::Vec3& out_min, Math::Vec3& out_max, LinearArena& arena)
        {
            std::vector<uint8_t> buffer;
            if (!Network::Fetch(url, buffer)) return false;

            cgltf_options options = {};
            cgltf_data* data = NULL;
            cgltf_result result = cgltf_parse(&options, buffer.data(), buffer.size(), &data);
            if (result != cgltf_result_success) return false;

            result = cgltf_load_buffers(&options, data, NULL);
            if (result != cgltf_result_success) { cgltf_free(data); return false; }

            if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) { cgltf_free(data); return false; }

            cgltf_primitive* prim = &data->meshes[0].primitives[0];
            cgltf_accessor *pos_acc = NULL, *norm_acc = NULL, *uv_acc = NULL;

            for (int i = 0; i < (int)prim->attributes_count; ++i)
            {
                if (prim->attributes[i].type == cgltf_attribute_type_position) pos_acc = prim->attributes[i].data;
                if (prim->attributes[i].type == cgltf_attribute_type_normal) norm_acc = prim->attributes[i].data;
                if (prim->attributes[i].type == cgltf_attribute_type_texcoord) uv_acc = prim->attributes[i].data;
            }

            if (!pos_acc) { cgltf_free(data); return false; }

            size_t vcount = pos_acc->count;
            int stride = 8; // pos(3) + norm(3) + uv(2)
            float* verts = (float*)arena.allocate(vcount * stride * sizeof(float));

            out_min = { 1e30f, 1e30f, 1e30f };
            out_max = { -1e30f, -1e30f, -1e30f };

            for (size_t i = 0; i < vcount; ++i)
            {
                float p[3], n[3] = { 0,0,1 }, uv[2] = { 0,0 };
                cgltf_accessor_read_float(pos_acc, i, p, 3);
                if (norm_acc) cgltf_accessor_read_float(norm_acc, i, n, 3);
                if (uv_acc) cgltf_accessor_read_float(uv_acc, i, uv, 2);

                // 1. Positions: Calculate relative translation in double precision
                double relX = transform[12] - centerEcef.x;
                double relY = transform[13] - centerEcef.y;
                double relZ = transform[14] - centerEcef.z;

                double dx = transform[0] * p[0] + transform[4] * p[1] + transform[8] * p[2] + relX;
                double dy = transform[1] * p[0] + transform[5] * p[1] + transform[9] * p[2] + relY;
                double dz = transform[2] * p[0] + transform[6] * p[1] + transform[10] * p[2] + relZ;

                // 2. Project to ENU
                double lx = enuMat[0] * dx + enuMat[1] * dy + enuMat[2] * dz; // East
                double ly = enuMat[4] * dx + enuMat[5] * dy + enuMat[6] * dz; // North
                double lz = enuMat[8] * dx + enuMat[9] * dy + enuMat[10] * dz; // Up

                // 3. Map to OpenGL (Directive: Up -> Y, East -> X, North -> -Z)
                float gx = (float)lx;    // East -> X
                float gy = (float)lz;    // Up -> Y
                float gz = (float)(-ly); // North -> -Z

                verts[i * stride + 0] = gx; verts[i * stride + 1] = gy; verts[i * stride + 2] = gz;

                if (gx < out_min.x) out_min.x = gx;
                if (gy < out_min.y) out_min.y = gy;
                if (gz < out_min.z) out_min.z = gz;
                if (gx > out_max.x) out_max.x = gx;
                if (gy > out_max.y) out_max.y = gy;
                if (gz > out_max.z) out_max.z = gz;

                // 4. Normals: Rotate part of transform
                double nx_rot = transform[0] * n[0] + transform[4] * n[1] + transform[8] * n[2];
                double ny_rot = transform[1] * n[0] + transform[5] * n[1] + transform[9] * n[2];
                double nz_rot = transform[2] * n[0] + transform[6] * n[1] + transform[10] * n[2];

                // 5. Apply ENU rotation
                double enx = enuMat[0] * nx_rot + enuMat[1] * ny_rot + enuMat[2] * nz_rot;
                double eny = enuMat[4] * nx_rot + enuMat[5] * ny_rot + enuMat[6] * nz_rot;
                double enz = enuMat[8] * nx_rot + enuMat[9] * ny_rot + enuMat[10] * nz_rot;

                // 6. Map normal consistent with vertices (X=East, Y=Up, Z=-North)
                float ngx = (float)enx;
                float ngy = (float)enz;
                float ngz = (float)(-eny);

                float ilen = 1.0f / sqrtf(ngx * ngx + ngy * ngy + ngz * ngz + 1e-12f);
                verts[i * stride + 3] = ngx * ilen; verts[i * stride + 4] = ngy * ilen; verts[i * stride + 5] = ngz * ilen;

                // 7. UVs
                verts[i * stride + 6] = uv[0]; verts[i * stride + 7] = uv[1];
            }

            size_t icount = prim->indices ? prim->indices->count : 0;
            unsigned int* indices = (unsigned int*)arena.allocate(icount * sizeof(unsigned int));
            for (size_t i = 0; i < icount; ++i) indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);

            out_mesh.instanceVbo = 0;
            out_mesh.count = (int)icount;
            glGenVertexArrays(1, &out_mesh.vao);
            glGenBuffers(1, &out_mesh.vbo);
            glGenBuffers(1, &out_mesh.ebo);
            glBindVertexArray(out_mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, out_mesh.vbo);
            glBufferData(GL_ARRAY_BUFFER, vcount * stride * sizeof(float), verts, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out_mesh.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), indices, GL_STATIC_DRAW);
            
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)(6 * sizeof(float)));

            cgltf_free(data);
            return true;
        }
    };
}

#endif
