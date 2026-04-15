#ifndef MESH_PRIMITIVE_H
#define MESH_PRIMITIVE_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <cmath>

#include "AliceMemory.h"
#include "TextureCache.h"

namespace Alice { extern LinearArena g_Arena; }

struct MeshPrimitive
{
    unsigned int vao, vbo, ebo, instanceVbo;
    unsigned int albedoTex;
    float baseColorFactor[4];
    float emissiveFactor[3];
    int count;

    MeshPrimitive() : vao(0), vbo(0), ebo(0), instanceVbo(0), albedoTex(0), count(0) 
    {
        baseColorFactor[0] = baseColorFactor[1] = baseColorFactor[2] = baseColorFactor[3] = 1.0f;
        emissiveFactor[0] = emissiveFactor[1] = emissiveFactor[2] = 0.0f;
    }

    void initPlane(float size)
    {
        cleanup();
        float h = size * 0.5f;
        // Pos(3), Normal(3), UV(2)
        float verts[] = {
            -h, -h, 0,  0, 0, 1,  0, 0,
             h, -h, 0,  0, 0, 1,  1, 0,
             h,  h, 0,  0, 0, 1,  1, 1,
            -h,  h, 0,  0, 0, 1,  0, 1
        };
        unsigned int idx[] = { 0, 1, 2, 2, 3, 0 };
        count = 6;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        
        // Location 0: Pos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        // Location 1: Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        // Location 2: UV
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        
        glBindVertexArray(0);
    }

    void initBox()
    {
        cleanup();
        // Pos(3), Normal(3), UV(2)
        float v[] = {
            // Front
            -0.5f,-0.5f, 0.5f,  0, 0, 1,  0, 0,   0.5f,-0.5f, 0.5f,  0, 0, 1,  1, 0,   0.5f, 0.5f, 0.5f,  0, 0, 1,  1, 1,  -0.5f, 0.5f, 0.5f,  0, 0, 1,  0, 1,
            // Back
            -0.5f,-0.5f,-0.5f,  0, 0,-1,  1, 0,  -0.5f, 0.5f,-0.5f,  0, 0,-1,  1, 1,   0.5f, 0.5f,-0.5f,  0, 0,-1,  0, 1,   0.5f,-0.5f,-0.5f,  0, 0,-1,  0, 0,
            // Top
            -0.5f, 0.5f,-0.5f,  0, 1, 0,  0, 1,  -0.5f, 0.5f, 0.5f,  0, 1, 0,  0, 0,   0.5f, 0.5f, 0.5f,  0, 1, 0,  1, 0,   0.5f, 0.5f,-0.5f,  0, 1, 0,  1, 1,
            // Bottom
            -0.5f,-0.5f,-0.5f,  0,-1, 0,  0, 0,   0.5f,-0.5f,-0.5f,  0,-1, 0,  1, 0,   0.5f,-0.5f, 0.5f,  0,-1, 0,  1, 1,  -0.5f,-0.5f, 0.5f,  0,-1, 0,  0, 1,
            // Right
             0.5f,-0.5f,-0.5f,  1, 0, 0,  1, 0,   0.5f, 0.5f,-0.5f,  1, 0, 0,  1, 1,   0.5f, 0.5f, 0.5f,  1, 0, 0,  0, 1,   0.5f,-0.5f, 0.5f,  1, 0, 0,  0, 0,
            // Left
            -0.5f,-0.5f,-0.5f, -1, 0, 0,  0, 0,  -0.5f,-0.5f, 0.5f, -1, 0, 0,  1, 0,  -0.5f, 0.5f, 0.5f, -1, 0, 0,  1, 1,  -0.5f, 0.5f,-0.5f, -1, 0, 0,  0, 1
        };
        unsigned int idx[] = {
            0,1,2, 2,3,0, 4,5,6, 6,7,4, 8,9,10, 10,11,8, 12,13,14, 14,15,12, 16,17,18, 18,19,16, 20,21,22, 22,23,20
        };
        count = 36;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        
        glBindVertexArray(0);
    }

    void initSphere(int sectors, int stacks)
    {
        cleanup();
        int vcount = (sectors + 1) * (stacks + 1);
        
        float* verts = (float*)Alice::g_Arena.allocate(vcount * 8 * sizeof(float));
        assert(verts);

        float sStep = 2.0f * 3.1415926f / (float)sectors;
        float tStep = 3.1415926f / (float)stacks;
        for(int i=0; i<=stacks; ++i)
        {
            float t = 3.1415926f / 2.0f - (float)i * tStep;
            float xy = cosf(t), z = sinf(t);
            float v = (float)i / (float)stacks;
            for(int j=0; j<=sectors; ++j)
            {
                float s = (float)j * sStep;
                float x = xy * cosf(s), y = xy * sinf(s);
                float u = (float)j / (float)sectors;
                int off = (i*(sectors+1) + j) * 8;
                verts[off+0]=x; verts[off+1]=y; verts[off+2]=z;
                verts[off+3]=x; verts[off+4]=y; verts[off+5]=z;
                verts[off+6]=u; verts[off+7]=v;
            }
        }
        int icount = sectors * stacks * 6;
        unsigned int* idx = (unsigned int*)Alice::g_Arena.allocate(icount * sizeof(unsigned int));
        assert(idx);

        int cur = 0;
        for(int i=0; i<stacks; ++i)
        {
            int k1 = i*(sectors+1), k2 = k1 + sectors + 1;
            for(int j=0; j<sectors; ++j, ++k1, ++k2)
            {
                if(i != 0) { idx[cur++]=k1; idx[cur++]=k2; idx[cur++]=k1+1; }
                if(i != (stacks-1)) { idx[cur++]=k1+1; idx[cur++]=k2; idx[cur++]=k2+1; }
            }
        }
        count = cur;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vcount * 8 * sizeof(float), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), idx, GL_STATIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        
        glBindVertexArray(0);
    }

    void initInstanced(int maxInstanceCount, float* data)
    {
        glBindVertexArray(vao);
        glGenBuffers(1, &instanceVbo);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
        glBufferData(GL_ARRAY_BUFFER, maxInstanceCount * 4 * sizeof(float), data, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(3); // Moved to location 3 to accommodate UV at 2
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribDivisor(3, 1);
        glBindVertexArray(0);
    }

    void updateInstanced(int count, float* data)
    {
        if(count <= 0 || !instanceVbo) return;
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * 4 * sizeof(float), data);
    }

    void initFromRaw(int vcount, const float* vdata, int icount, const unsigned int* idata, bool hasUV = false)
    {
        cleanup();
        count = icount;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        int stride = hasUV ? 8 : 6;
        glBufferData(GL_ARRAY_BUFFER, vcount * stride * sizeof(float), vdata, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned int), idata, GL_STATIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)(3 * sizeof(float)));
        
        if (hasUV)
        {
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)(6 * sizeof(float)));
        }
        else
        {
            glDisableVertexAttribArray(2);
            glVertexAttrib2f(2, 0.0f, 0.0f);
        }
        
        glBindVertexArray(0);
    }

    void draw() const 
    { 
        if (!vao) return;
        glBindVertexArray(vao); 
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0); 
    }

    void drawInstanced(int instanceCount) const
    {
        if(instanceCount <= 0 || !vao) return;
        glBindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, instanceCount);
    }

    void cleanup()
    {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (ebo) glDeleteBuffers(1, &ebo);
        if (instanceVbo) glDeleteBuffers(1, &instanceVbo);
        if (albedoTex) Alice::TextureCache::Get().Release(albedoTex);
        vao = vbo = ebo = instanceVbo = albedoTex = 0;
        count = 0;
    }
};

#endif
