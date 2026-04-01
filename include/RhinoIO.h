#ifndef RHINO_IO_H
#define RHINO_IO_H

#include <cstdint>

namespace Alice
{
    // POD structures for DOD pipeline
    struct RhinoMeshBuffer
    {
        float* vertices;
        uint32_t* indices;
        uint32_t vertexCount;
        uint32_t indexCount;
    };

    struct RhinoParser
    {
        static bool LoadFile(const char* path, RhinoMeshBuffer& outBuffer);
    };
}

#ifdef RHINO_IO_RUN_TEST
#include <cstdio>

namespace Alice
{
    inline void RhinoIO_UnitTest()
    {
        printf("[RHINO_IO_TEST] Starting...\n");
        RhinoMeshBuffer buffer = {};
        const char* testPath = "test.3dm"; 
        
        bool success = RhinoParser::LoadFile(testPath, buffer);
        
        printf("[RHINO_IO_TEST] Load %s: %s\n", testPath, success ? "SUCCESS" : "FAILED (expected if file missing)");
        if (success)
        {
            printf("[RHINO_IO_TEST] Vertices: %u, Indices: %u\n", buffer.vertexCount, buffer.indexCount);
        }
    }
}
#endif // RHINO_IO_RUN_TEST

#endif // RHINO_IO_H
