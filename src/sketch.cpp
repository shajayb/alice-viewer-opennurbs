#include <cstdio>
#include <cstdlib>

#include "AliceMemory.h"
namespace Alice { LinearArena g_Arena; }

// ... commented out previous tests ...
//#define SELECTION_CONTEXT_RUN_TEST
//#include "SelectionContext.h"

//#define FASTAABB_RUN_TEST
//#include "FastAABB.h"

// Active Test
#define ALICE_RHINO_IMPLEMENTATION
#define RHINO_IO_RUN_TEST
#include "RhinoIO.h"

extern "C" void setup()
{
    Alice::g_Arena.init(1024 * 1024 * 100); // 100MB for testing

    // Initialize openNURBS global state
    ON::Begin();
    
    Alice::RhinoIO_UnitTest();

    // Terminate openNURBS global state
    ON::End();
}

extern "C" void update(float dt) {}
extern "C" void draw() {}
