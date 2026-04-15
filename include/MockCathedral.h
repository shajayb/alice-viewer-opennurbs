#ifndef MOCK_CATHEDRAL_H
#define MOCK_CATHEDRAL_H

#include <vector>
#include "MeshPrimitive.h"
#include "AliceMath.h"

namespace Alice
{
    struct MockCathedral
    {
        static void Generate(MeshPrimitive& out_mesh, LinearArena& arena)
        {
            // Simple assembly of boxes
            // Nave, Transepts, Dome
            
            struct Box { Math::Vec3 center; Math::Vec3 size; };
            std::vector<Box> boxes;
            
            // Nave
            boxes.push_back({ {0, 0, 15}, {80, 20, 30} });
            // Transepts
            boxes.push_back({ {0, 0, 15}, {20, 60, 30} });
            // Dome Base
            boxes.push_back({ {0, 0, 35}, {30, 30, 10} });
            // Dome
            boxes.push_back({ {0, 0, 50}, {25, 25, 20} });
            // Towers
            boxes.push_back({ {-35, 0, 25}, {10, 10, 50} });
            boxes.push_back({ {35, 0, 25}, {10, 10, 50} });

            // Each box is 24 vertices, 36 indices
            int totalV = (int)boxes.size() * 24;
            int totalI = (int)boxes.size() * 36;
            
            float* vdata = (float*)arena.allocate(totalV * 8 * sizeof(float));
            unsigned int* idata = (unsigned int*)arena.allocate(totalI * sizeof(unsigned int));

            float cubeV[] = {
                -0.5f,-0.5f, 0.5f,  0, 0, 1,  0, 0,   0.5f,-0.5f, 0.5f,  0, 0, 1,  1, 0,   0.5f, 0.5f, 0.5f,  0, 0, 1,  1, 1,  -0.5f, 0.5f, 0.5f,  0, 0, 1,  0, 1,
                -0.5f,-0.5f,-0.5f,  0, 0,-1,  1, 0,  -0.5f, 0.5f,-0.5f,  0, 0,-1,  1, 1,   0.5f, 0.5f,-0.5f,  0, 0,-1,  0, 1,   0.5f,-0.5f,-0.5f,  0, 0,-1,  0, 0,
                -0.5f, 0.5f,-0.5f,  0, 1, 0,  0, 1,  -0.5f, 0.5f, 0.5f,  0, 1, 0,  0, 0,   0.5f, 0.5f, 0.5f,  0, 1, 0,  1, 0,   0.5f, 0.5f,-0.5f,  0, 1, 0,  1, 1,
                -0.5f,-0.5f,-0.5f,  0,-1, 0,  0, 0,   0.5f,-0.5f,-0.5f,  0,-1, 0,  1, 0,   0.5f,-0.5f, 0.5f,  0,-1, 0,  1, 1,  -0.5f,-0.5f, 0.5f,  0,-1, 0,  0, 1,
                 0.5f,-0.5f,-0.5f,  1, 0, 0,  1, 0,   0.5f, 0.5f,-0.5f,  1, 0, 0,  1, 1,   0.5f, 0.5f, 0.5f,  1, 0, 0,  0, 1,   0.5f,-0.5f, 0.5f,  1, 0, 0,  0, 0,
                -0.5f,-0.5f,-0.5f, -1, 0, 0,  0, 0,  -0.5f,-0.5f, 0.5f, -1, 0, 0,  1, 0,  -0.5f, 0.5f, 0.5f, -1, 0, 0,  1, 1,  -0.5f, 0.5f,-0.5f, -1, 0, 0,  0, 1
            };
            unsigned int cubeI[] = { 0,1,2, 2,3,0, 4,5,6, 6,7,4, 8,9,10, 10,11,8, 12,13,14, 14,15,12, 16,17,18, 18,19,16, 20,21,22, 22,23,20 };

            for (int b = 0; b < (int)boxes.size(); ++b)
            {
                Box& box = boxes[b];
                for (int i = 0; i < 24; ++i)
                {
                    int voff = (b * 24 + i) * 8;
                    int coff = i * 8;
                    vdata[voff + 0] = cubeV[coff + 0] * box.size.x + box.center.x;
                    vdata[voff + 1] = cubeV[coff + 1] * box.size.y + box.center.y;
                    vdata[voff + 2] = cubeV[coff + 2] * box.size.z + box.center.z;
                    vdata[voff + 3] = cubeV[coff + 3];
                    vdata[voff + 4] = cubeV[coff + 4];
                    vdata[voff + 5] = cubeV[coff + 5];
                    vdata[voff + 6] = cubeV[coff + 6];
                    vdata[voff + 7] = cubeV[coff + 7];
                }
                for (int i = 0; i < 36; ++i)
                {
                    idata[b * 36 + i] = cubeI[i] + b * 24;
                }
            }

            out_mesh.initFromRaw(totalV, vdata, totalI, idata, true);
        }
    };
}

#endif
