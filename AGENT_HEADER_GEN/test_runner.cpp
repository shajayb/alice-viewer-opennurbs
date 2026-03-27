#include <cstdio>
#include <cstdlib>

#include "AliceMemory.h"
namespace Alice { LinearArena g_Arena; }

#define SPATIAL_GRID_RUN_TEST
#include "SpatialGrid.h"

int main()
{
    printf("[RUNNER] Starting SpatialGrid Tests...\n");
    setup();
    return 0;
}
