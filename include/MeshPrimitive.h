#ifndef MESH_PRIMITIVE_H
#define MESH_PRIMITIVE_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <cmath>

#include "AliceMemory.h"

namespace Alice { extern LinearArena g_Arena; }

struct MeshPrimitive
{
    unsigned int vao, vbo, ebo, instanceVbo;
    int count;

    void initPlane(float size)
    {
        instanceVbo = 0;
        float h = size * 0.5f;
        float verts[] = {
            -h, -h, 0,  0, 0, 1,
             h, -h, 0,  0, 0, 1,
             h,  h, 0,  0, 0, 1,
            -h,  h, 0,  0, 0, 1
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
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void initBox()
    {
        instanceVbo = 0;
        float v[] = {
            -0.5f,-0.5f, 0.5f,  0, 0, 1,   0.5f,-0.5f, 0.5f,  0, 0, 1,   0.5f, 0.5f, 0.5f,  0, 0, 1,  -0.5f, 0.5f, 0.5f,  0, 0, 1,
            -0.5f,-0.5f,-0.5f,  0, 0,-1,  -0.5f, 0.5f,-0.5f,  0, 0,-1,   0.5f, 0.5f,-0.5f,  0, 0,-1,   0.5f,-0.5f,-0.5f,  0, 0,-1,
            -0.5f, 0.5f,-0.5f,  0, 1, 0,  -0.5f, 0.5f, 0.5f,  0, 1, 0,   0.5f, 0.5f, 0.5f,  0, 1, 0,   0.5f, 0.5f,-0.5f,  0, 1, 0,
            -0.5f,-0.5f,-0.5f,  0,-1, 0,   0.5f,-0.5f,-0.5f,  0,-1, 0,   0.5f,-0.5f, 0.5f,  0,-1, 0,  -0.5f,-0.5f, 0.5f,  0,-1, 0,
             0.5f,-0.5f,-0.5f,  1, 0, 0,   0.5f, 0.5f,-0.5f,  1, 0, 0,   0.5f, 0.5f, 0.5f,  1, 0, 0,   0.5f,-0.5f, 0.5f,  1, 0, 0,
            -0.5f,-0.5f,-0.5f, -1, 0, 0,  -0.5f,-0.5f, 0.5f, -1, 0, 0,  -0.5f, 0.5f, 0.5f, -1, 0, 0,  -0.5f, 0.5f,-0.5f, -1, 0, 0
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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void initSphere(int sectors, int stacks)
    {
        instanceVbo = 0;
        int vcount = (sectors + 1) * (stacks + 1);
        
        float* verts = (float*)Alice::g_Arena.allocate(vcount * 6 * sizeof(float));
        assert(verts);

        float sStep = 2.0f * 3.1415926f / (float)sectors;
        float tStep = 3.1415926f / (float)stacks;
        for(int i=0; i<=stacks; ++i)
        {
            float t = 3.1415926f / 2.0f - (float)i * tStep;
            float xy = cosf(t), z = sinf(t);
            for(int j=0; j<=sectors; ++j)
            {
                float s = (float)j * sStep;
                float x = xy * cosf(s), y = xy * sinf(s);
                int off = (i*(sectors+1) + j) * 6;
                verts[off+0]=x; verts[off+1]=y; verts[off+2]=z;
                verts[off+3]=x; verts[off+4]=y; verts[off+5]=z;
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
        glBufferData(GL_ARRAY_BUFFER, vcount*6*sizeof(float), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount*sizeof(unsigned int), idx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0, 0, 0);
    }

    void draw() const 
    { 
        glBindVertexArray(vao); 
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0); 
    }

    void cleanup()
    {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (ebo) glDeleteBuffers(1, &ebo);
        if (instanceVbo) glDeleteBuffers(1, &instanceVbo);
        vao = vbo = ebo = instanceVbo = 0;
        count = 0;
    }
};

#endif
