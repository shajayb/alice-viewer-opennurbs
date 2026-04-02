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
#define SSAO_RUN_TEST
#include "SSAO.h"
