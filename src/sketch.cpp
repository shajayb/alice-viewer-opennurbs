#include <cstdio>
#include <cstdlib>

#include "AliceMemory.h"
namespace Alice { LinearArena g_Arena; }

// ... previous tests ...
//#define SELECTION_CONTEXT_RUN_TEST
//#include "SelectionContext.h"

// Active Test
#define AABB_RUN_TEST
#include "AABBTest.h"
