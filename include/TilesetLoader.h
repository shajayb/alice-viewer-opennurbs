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
#include "stb_image.h"

using json = nlohmann::json;

namespace Alice
{
    struct TileNode
    {
        Math::DVec3 sphereCenter;
        double sphereRadius;
        Math::Vec3 glCenter;
        Math::Vec3 aabbMin;
        Math::Vec3 aabbMax;
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
        bool isLoading;
        bool hasContent;
        bool refineAdd; // true for ADD, false for REPLACE
    };

    struct ActiveNode
    {
        int index;
        float sse;
    };

    struct TilesetLoader
    {
        static void CalculateAABB(int nodeIdx, Buffer<TileNode>& nodes, Math::DVec3 targetEcef, double* enuMat)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count) return;
            TileNode& node = nodes[nodeIdx];
            
            // If it has content and is loaded, we could use mesh bounds, 
            // but for now let's combine from children if they exist.
            // If it's a leaf and loaded, use the mesh bounds.

            if (node.isLoaded && node.mesh.vao != 0 && node.childCount == 0)
            {
                // Mesh bounds are already relative to targetEcef/ENU (see ProcessAsyncLoad)
                // They should be stored in node.aabbMin/Max during upload.
            }
            else
            {
                // Compute initial AABB from the sphere in world-space, but converted to local GL-space
                double dx = node.sphereCenter.x - targetEcef.x;
                double dy = node.sphereCenter.y - targetEcef.y;
                double dz = node.sphereCenter.z - targetEcef.z;
                
                double lx = enuMat[0] * dx + enuMat[1] * dy + enuMat[2] * dz;
                double ly = enuMat[4] * dx + enuMat[5] * dy + enuMat[6] * dz;
                double lz = enuMat[8] * dx + enuMat[9] * dy + enuMat[10] * dz;

                float gx = (float)lx;    // East -> X
                float gy = (float)lz;    // Up -> Y
                float gz = (float)(-ly); // North -> -Z
                float r = (float)node.sphereRadius;

                node.glCenter = { gx, gy, gz };
                node.aabbMin = { gx - r, gy - r, gz - r };
                node.aabbMax = { gx + r, gy + r, gz + r };
            }

            for (int i = 0; i < node.childCount; ++i)
            {
                int cIdx = node.firstChild + i;
                CalculateAABB(cIdx, nodes, targetEcef, enuMat);
                TileNode& child = nodes[cIdx];
                if (child.aabbMin.x < node.aabbMin.x) node.aabbMin.x = child.aabbMin.x;
                if (child.aabbMin.y < node.aabbMin.y) node.aabbMin.y = child.aabbMin.y;
                if (child.aabbMin.z < node.aabbMin.z) node.aabbMin.z = child.aabbMin.z;
                if (child.aabbMax.x > node.aabbMax.x) node.aabbMax.x = child.aabbMax.x;
                if (child.aabbMax.y > node.aabbMax.y) node.aabbMax.y = child.aabbMax.y;
                if (child.aabbMax.z > node.aabbMax.z) node.aabbMax.z = child.aabbMax.z;
            }
        }

        static bool IsBelowHorizon(Math::DVec3 nodeCenter, Math::DVec3 cameraPos, double radius)
        {
            double d2 = cameraPos.x * cameraPos.x + cameraPos.y * cameraPos.y + cameraPos.z * cameraPos.z;
            double r = 6378137.0; 
            if (d2 < r * r) return false; 

            double distToHorizon = sqrt(d2 - r * r);
            double d = sqrt((nodeCenter.x - cameraPos.x) * (nodeCenter.x - cameraPos.x) +
                            (nodeCenter.y - cameraPos.y) * (nodeCenter.y - cameraPos.y) +
                            (nodeCenter.z - cameraPos.z) * (nodeCenter.z - cameraPos.z));
            
            if (d > distToHorizon + radius)
            {
                Math::DVec3 nnc = { nodeCenter.x, nodeCenter.y, nodeCenter.z };
                double nlen = sqrt(nnc.x*nnc.x + nnc.y*nnc.y + nnc.z*nnc.z);
                nnc.x /= nlen; nnc.y /= nlen; nnc.z /= nlen;

                Math::DVec3 ncp = { cameraPos.x, cameraPos.y, cameraPos.z };
                double clen = sqrt(ncp.x*ncp.x + ncp.y*ncp.y + ncp.z*ncp.z);
                ncp.x /= clen; ncp.y /= clen; ncp.z /= clen;

                double dot = nnc.x * ncp.x + nnc.y * ncp.y + nnc.z * ncp.z;
                if (dot < -0.1) return true; 
            }
            return false;
        }
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
            std::string headers;
            if (!Network::Fetch(url.c_str(), buffer, &status, &body, &headers)) 
            {
                fprintf(stderr, "[TilesetLoader] ERROR: Network::Fetch failed for %s\n", url.c_str());
                fprintf(stderr, "[TilesetLoader] Curl Status: %ld\n", status);
                if (!headers.empty()) fprintf(stderr, "[TilesetLoader] Response Headers:\n%s\n", headers.c_str());
                if (!body.empty()) fprintf(stderr, "[TilesetLoader] Response Body: %s\n", body.c_str());
                return false;
            }
            try
            {
                out_tileset = json::parse(buffer.begin(), buffer.end());
                if (out_tileset.contains("error"))
                {
                    fprintf(stderr, "[TilesetLoader] ERROR: Google API returned error: %s\n", out_tileset["error"].dump().c_str());
                    if (!headers.empty()) fprintf(stderr, "[TilesetLoader] Debug Headers:\n%s\n", headers.c_str());
                    return false;
                }
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
            node.firstChild = -1;

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
                if (len > 1e-6f) { planes[i][0] /= len; planes[i][1] /= len; planes[i][2] /= len; planes[i][3] /= len; }
            }
        }

        static bool IsSphereInFrustum(const float planes[6][4], double cx, double cy, double cz, double radius)
        {
            for (int i = 0; i < 6; ++i)
            {
                double dist = planes[i][0] * cx + planes[i][1] * cy + planes[i][2] * cz + planes[i][3];
                if (dist <= -radius) return false;
            }
            return true;
        }

        static bool IsAABBInFrustum(const float planes[6][4], Math::Vec3 min, Math::Vec3 max)
        {
            for (int i = 0; i < 6; ++i)
            {
                Math::Vec3 p;
                p.x = (planes[i][0] > 0) ? max.x : min.x;
                p.y = (planes[i][1] > 0) ? max.y : min.y;
                p.z = (planes[i][2] > 0) ? max.z : min.z;
                if (planes[i][0] * p.x + planes[i][1] * p.y + planes[i][2] * p.z + planes[i][3] < 0) return false;
            }
            return true;
        }

        static void PropagateBounds(int nodeIdx, Buffer<TileNode>& nodes)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count) return;
            TileNode& node = nodes[nodeIdx];
            if (node.childCount > 0)
            {
                node.aabbMin = { 1e30f, 1e30f, 1e30f };
                node.aabbMax = { -1e30f, -1e30f, -1e30f };
                for (int i = 0; i < node.childCount; ++i)
                {
                    int cIdx = node.firstChild + i;
                    PropagateBounds(cIdx, nodes);
                    TileNode& child = nodes[cIdx];
                    if (child.aabbMin.x < node.aabbMin.x) node.aabbMin.x = child.aabbMin.x;
                    if (child.aabbMin.y < node.aabbMin.y) node.aabbMin.y = child.aabbMin.y;
                    if (child.aabbMin.z < node.aabbMin.z) node.aabbMin.z = child.aabbMin.z;
                    if (child.aabbMax.x > node.aabbMax.x) node.aabbMax.x = child.aabbMax.x;
                    if (child.aabbMax.y > node.aabbMax.y) node.aabbMax.y = child.aabbMax.y;
                    if (child.aabbMax.z > node.aabbMax.z) node.aabbMax.z = child.aabbMax.z;
                }
            }
        }

        static bool TraverseAndGraft(int nodeIdx, Buffer<TileNode>& nodes, Math::DVec3 cameraPos, float viewportHeight, float fov, float sseThreshold, const float frustumPlanes[6][4], Buffer<ActiveNode>& activeNodes, LinearArena& arena, int depth, int& nodesVisited, int maxNodes)
        {
            if (nodeIdx < 0 || nodeIdx >= (int)nodes.count || nodesVisited >= maxNodes || depth > 32) return false;
            nodesVisited++;

            // 0. Frustum Culling (Hierarchical AABB first)
            // Bypass root culling to ensure we always enter the tree
            if (depth > 0)
            {
                if (nodes[nodeIdx].aabbMax.x > nodes[nodeIdx].aabbMin.x)
                {
                    if (!IsAABBInFrustum(frustumPlanes, nodes[nodeIdx].aabbMin, nodes[nodeIdx].aabbMax)) return false;
                }
                else // Fallback to sphere if AABB not yet ready
                {
                    if (!IsSphereInFrustum(frustumPlanes, nodes[nodeIdx].glCenter.x, nodes[nodeIdx].glCenter.y, nodes[nodeIdx].glCenter.z, nodes[nodeIdx].sphereRadius))
                    {
                        return false;
                    }
                }

                // 0b. Horizon Culling
                if (IsBelowHorizon(nodes[nodeIdx].sphereCenter, cameraPos, nodes[nodeIdx].sphereRadius))
                {
                    return false;
                }
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

            // Force descend if node has children AND (SSE > threshold OR it has no content to render)
            bool shouldDescend = (nodes[nodeIdx].childCount > 0) && (sse > sseThreshold || !nodes[nodeIdx].hasContent);
            
            if (!nodes[nodeIdx].isLoaded && (nodes[nodeIdx].isExternal || nodes[nodeIdx].hasContent))
            {
                // Request load if not loaded
                activeNodes.push_back({ nodeIdx, (float)sse });
            }

            if (shouldDescend)
            {
                bool allChildrenReady = true;
                int first = nodes[nodeIdx].firstChild;
                int count = nodes[nodeIdx].childCount;

                for (int i = 0; i < count; ++i)
                {
                    int cIdx = first + i;
                    if (nodes[cIdx].hasContent && !nodes[cIdx].isLoaded && !nodes[cIdx].isExternal)
                    {
                        allChildrenReady = false;
                        break;
                    }
                    if (nodes[cIdx].isExternal && (nodes[cIdx].firstChild == -1))
                    {
                        allChildrenReady = false;
                        break;
                    }
                }

                if (nodes[nodeIdx].refineAdd)
                {
                    if (nodes[nodeIdx].isLoaded && nodes[nodeIdx].hasContent)
                    {
                        activeNodes.push_back({ nodeIdx, (float)sse });
                    }
                    for (int i = 0; i < count; ++i)
                    {
                        TraverseAndGraft(first + i, nodes, cameraPos, viewportHeight, fov, sseThreshold, frustumPlanes, activeNodes, arena, depth + 1, nodesVisited, maxNodes);
                    }
                    return true;
                }
                else
                {
                    if (allChildrenReady && (nodes[nodeIdx].firstChild != -1))
                    {
                        for (int i = 0; i < count; ++i)
                        {
                            TraverseAndGraft(first + i, nodes, cameraPos, viewportHeight, fov, sseThreshold, frustumPlanes, activeNodes, arena, depth + 1, nodesVisited, maxNodes);
                        }
                        return true;
                    }
                    else
                    {
                        for (int i = 0; i < count; ++i)
                        {
                            TraverseAndGraft(first + i, nodes, cameraPos, viewportHeight, fov, sseThreshold, frustumPlanes, activeNodes, arena, depth + 1, nodesVisited, maxNodes);
                        }

                        if (nodes[nodeIdx].isLoaded && nodes[nodeIdx].hasContent)
                        {
                            activeNodes.push_back({ nodeIdx, (float)sse });
                            return true;
                        }
                        return false; 
                    }
                }
            }
            else
            {
                if (nodes[nodeIdx].isLoaded && nodes[nodeIdx].hasContent)
                {
                    activeNodes.push_back({ nodeIdx, (float)sse });
                    return true;
                }
            }

            return false;
        }

        struct AsyncLoadRequest
        {
            int nodeIdx;
            char url[1024];
            Math::DVec3 centerEcef;
            double enuMat[16];
            double transform[16];
            
            // Task 1: Arena-backed buffers
            float* verts;
            unsigned int* indices;
            size_t maxVCount;
            size_t maxICount;
            size_t vcount;
            size_t icount;
            
            Math::Vec3 min, max;
            bool isJson;
            uint8_t* jsonBuffer;
            size_t jsonBufferSize;

            // Texture output
            uint8_t* texData;
            int texWidth, texHeight, texChannels;
            char texKey[1024];
            bool hasTexture;
            float baseColorFactor[4];
            float emissiveFactor[3];

            bool success;
            bool completed;
        };

        static void ProcessAsyncLoad(AsyncLoadRequest* req)
        {
            req->texData = nullptr;
            req->hasTexture = false;
            
            // Task 2: Procedural Mock
            if (strncmp(req->url, "mock://", 7) == 0)
            {
                if (strcmp(req->url, "mock://cathedral") == 0)
                {
                    struct Box { Math::Vec3 center; Math::Vec3 size; };
                    std::vector<Box> boxes;
                    boxes.push_back({ {0, 0, 15}, {80, 20, 30} }); // Nave
                    boxes.push_back({ {0, 0, 15}, {20, 60, 30} }); // Transepts
                    boxes.push_back({ {0, 0, 32}, {28, 28, 4} });  // Dome Base
                    boxes.push_back({ {0, 0, 38}, {24, 24, 8} });  // Dome 1
                    boxes.push_back({ {0, 0, 46}, {18, 18, 8} });  // Dome 2
                    boxes.push_back({ {0, 0, 52}, {10, 10, 4} });  // Dome 3
                    boxes.push_back({ {-35, -8, 30}, {10, 10, 60} }); // Tower L
                    boxes.push_back({ {35, -8, 30}, {10, 10, 60} });  // Tower R
                    boxes.push_back({ {0, 0, 60}, {2, 2, 20} });   // Spire

                    req->vcount = (int)boxes.size() * 24;
                    req->icount = (int)boxes.size() * 36;
                    if (req->vcount > req->maxVCount || req->icount > req->maxICount) { req->success = false; req->completed = true; return; }

                    float cubeV[] = {
                        -0.5f,-0.5f, 0.5f,  0, 0, 1,  0, 0,   0.5f,-0.5f, 0.5f,  0, 0, 1,  1, 0,   0.5f, 0.5f, 0.5f,  0, 0, 1,  1, 1,  -0.5f, 0.5f, 0.5f,  0, 0, 1,  0, 1,
                        -0.5f,-0.5f,-0.5f,  0, 0,-1,  1, 0,  -0.5f, 0.5f,-0.5f,  0, 0,-1,  1, 1,   0.5f, 0.5f,-0.5f,  0, 0,-1,  0, 1,   0.5f,-0.5f,-0.5f,  0, 0,-1,  0, 0,
                        -0.5f, 0.5f,-0.5f,  0, 1, 0,  0, 1,  -0.5f, 0.5f, 0.5f,  0, 1, 0,  0, 0,   0.5f, 0.5f, 0.5f,  0, 1, 0,  1, 0,   0.5f, 0.5f,-0.5f,  0, 1, 0,  1, 1,
                        -0.5f,-0.5f,-0.5f,  0,-1, 0,  0, 0,   0.5f,-0.5f,-0.5f,  0,-1, 0,  1, 0,   0.5f,-0.5f, 0.5f,  0,-1, 0,  1, 1,  -0.5f,-0.5f, 0.5f,  0,-1, 0,  0, 1,
                         0.5f,-0.5f,-0.5f,  1, 0, 0,  1, 0,   0.5f, 0.5f,-0.5f,  1, 0, 0,  1, 1,   0.5f, 0.5f, 0.5f,  1, 0, 0,  0, 1,   0.5f,-0.5f, 0.5f,  1, 0, 0,  0, 0,
                        -0.5f,-0.5f,-0.5f, -1, 0, 0,  0, 0,  -0.5f,-0.5f, 0.5f, -1, 0, 0,  1, 0,  -0.5f, 0.5f, 0.5f, -1, 0, 0,  1, 1,  -0.5f, 0.5f,-0.5f, -1, 0, 0,  0, 1
                    };
                    unsigned int cubeI[] = { 0,1,2, 2,3,0, 4,5,6, 6,7,4, 8,9,10, 10,11,8, 12,13,14, 14,15,12, 16,17,18, 18,19,16, 20,21,22, 22,23,20 };

                    req->min = { 1e30f, 1e30f, 1e30f };
                    req->max = { -1e30f, -1e30f, -1e30f };

                    for (int b = 0; b < (int)boxes.size(); ++b)
                    {
                        Box& box = boxes[b];
                        for (int i = 0; i < 24; ++i)
                        {
                            int voff = (b * 24 + i) * 8;
                            int coff = i * 8;
                            float px = cubeV[coff + 0] * box.size.x + box.center.x;
                            float py = cubeV[coff + 1] * box.size.y + box.center.y;
                            float pz = cubeV[coff + 2] * box.size.z + box.center.z;
                            
                            float gx = px;
                            float gy = pz;
                            float gz = -py;

                            req->verts[voff + 0] = gx;
                            req->verts[voff + 1] = gy;
                            req->verts[voff + 2] = gz;

                            if (gx < req->min.x) req->min.x = gx;
                            if (gy < req->min.y) req->min.y = gy;
                            if (gz < req->min.z) req->min.z = gz;
                            if (gx > req->max.x) req->max.x = gx;
                            if (gy > req->max.y) req->max.y = gy;
                            if (gz > req->max.z) req->max.z = gz;

                            float nx = cubeV[coff + 3];
                            float ny = cubeV[coff + 4];
                            float nz = cubeV[coff + 5];

                            req->verts[voff + 3] = nx;
                            req->verts[voff + 4] = nz;
                            req->verts[voff + 5] = -ny;
                            req->verts[voff + 6] = cubeV[coff + 6];
                            req->verts[voff + 7] = cubeV[coff + 7];
                        }
                        for (int i = 0; i < 36; ++i) req->indices[b * 36 + i] = cubeI[i] + b * 24;
                    }
                }
                else
                {
                    // Generate randomized cuboid building
                    req->vcount = 24;
                    req->icount = 36;
                    if (req->vcount > req->maxVCount || req->icount > req->maxICount) { req->success = false; req->completed = true; return; }

                    float h = 10.0f + (float)(rand() % 40);
                    float w = 5.0f + (float)(rand() % 15);
                    float d = 5.0f + (float)(rand() % 15);
                    
                    float cubeV[] = {
                        -0.5f,-0.5f, 1.0f,  0, 0, 1,  0, 0,   0.5f,-0.5f, 1.0f,  0, 0, 1,  1, 0,   0.5f, 0.5f, 1.0f,  0, 0, 1,  1, 1,  -0.5f, 0.5f, 1.0f,  0, 0, 1,  0, 1,
                        -0.5f,-0.5f, 0.0f,  0, 0,-1,  1, 0,  -0.5f, 0.5f, 0.0f,  0, 0,-1,  1, 1,   0.5f, 0.5f, 0.0f,  0, 0,-1,  0, 1,   0.5f,-0.5f, 0.0f,  0, 0,-1,  0, 0,
                        -0.5f, 0.5f, 0.0f,  0, 1, 0,  0, 1,  -0.5f, 0.5f, 1.0f,  0, 1, 0,  0, 0,   0.5f, 0.5f, 1.0f,  0, 1, 0,  1, 0,   0.5f, 0.5f, 0.0f,  0, 1, 0,  1, 1,
                        -0.5f,-0.5f, 0.0f,  0,-1, 0,  0, 0,   0.5f,-0.5f, 0.0f,  0,-1, 0,  1, 0,   0.5f,-0.5f, 1.0f,  0,-1, 0,  1, 1,  -0.5f,-0.5f, 1.0f,  0,-1, 0,  0, 1,
                         0.5f,-0.5f, 0.0f,  1, 0, 0,  1, 0,   0.5f, 0.5f, 0.0f,  1, 0, 0,  1, 1,   0.5f, 0.5f, 1.0f,  1, 0, 0,  0, 1,   0.5f,-0.5f, 1.0f,  1, 0, 0,  0, 0,
                        -0.5f,-0.5f, 0.0f, -1, 0, 0,  0, 0,  -0.5f,-0.5f, 1.0f, -1, 0, 0,  1, 0,  -0.5f, 0.5f, 1.0f, -1, 0, 0,  1, 1,  -0.5f, 0.5f, 0.0f, -1, 0, 0,  0, 1
                    };
                    unsigned int cubeI[] = { 0,1,2, 2,3,0, 4,5,6, 6,7,4, 8,9,10, 10,11,8, 12,13,14, 14,15,12, 16,17,18, 18,19,16, 20,21,22, 22,23,20 };

                    req->min = { -w*0.5f, -d*0.5f, 0.0f };
                    req->max = { w*0.5f, d*0.5f, h };

                    for(int i=0; i<24; ++i)
                    {
                        req->verts[i*8+0] = cubeV[i*8+0] * w;
                        req->verts[i*8+1] = cubeV[i*8+2] * h; // Up is Y in GL
                        req->verts[i*8+2] = -cubeV[i*8+1] * d; // North is -Z
                        req->verts[i*8+3] = cubeV[i*8+3];
                        req->verts[i*8+4] = cubeV[i*8+5];
                        req->verts[i*8+5] = -cubeV[i*8+4];
                        req->verts[i*8+6] = cubeV[i*8+6];
                        req->verts[i*8+7] = cubeV[i*8+7];
                    }
                    memcpy(req->indices, cubeI, 36 * sizeof(unsigned int));
                }

                for (int i = 0; i < 4; ++i) req->baseColorFactor[i] = 1.0f;
                for (int i = 0; i < 3; ++i) req->emissiveFactor[i] = 0.0f;

                req->success = true; req->completed = true;
                return;
            }

            std::vector<uint8_t> buffer;
            if (!Network::Fetch(req->url, buffer)) { req->success = false; req->completed = true; return; }

            if (req->isJson)
            {
                req->jsonBufferSize = buffer.size();
                req->jsonBuffer = (uint8_t*)malloc(req->jsonBufferSize);
                memcpy(req->jsonBuffer, buffer.data(), req->jsonBufferSize);
                req->success = true;
                req->completed = true;
                return;
            }

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
            if (req->vcount > req->maxVCount) { cgltf_free(data); req->success = false; req->completed = true; return; }

            int stride = 8; 
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
            if (req->icount > req->maxICount) { cgltf_free(data); req->success = false; req->completed = true; return; }
            for (size_t i = 0; i < req->icount; ++i) req->indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);

            // Material Extraction
            if (prim->material)
            {
                if (prim->material->has_pbr_metallic_roughness)
                {
                    for (int i = 0; i < 4; ++i) req->baseColorFactor[i] = prim->material->pbr_metallic_roughness.base_color_factor[i];
                    
                    cgltf_texture* tex = prim->material->pbr_metallic_roughness.base_color_texture.texture;
                    if (tex && tex->image)
                    {
                        cgltf_image* img = tex->image;
                        if (img->buffer_view)
                        {
                            uint8_t* start = (uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
                            size_t size = img->buffer_view->size;
                            req->texData = stbi_load_from_memory(start, (int)size, &req->texWidth, &req->texHeight, &req->texChannels, 4);
                            if (req->texData)
                            {
                                req->hasTexture = true;
                                req->texChannels = 4;
                                snprintf(req->texKey, 1024, "%s_tex_%d", req->url, (int)img->buffer_view->offset);
                            }
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < 4; ++i) req->baseColorFactor[i] = 1.0f;
                }
                for (int i = 0; i < 3; ++i) req->emissiveFactor[i] = prim->material->emissive_factor[i];
            }
            else
            {
                for (int i = 0; i < 4; ++i) req->baseColorFactor[i] = 1.0f;
                for (int i = 0; i < 3; ++i) req->emissiveFactor[i] = 0.0f;
            }

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
