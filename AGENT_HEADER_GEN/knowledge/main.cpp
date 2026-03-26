#include "AliceViewer.h"
#include <cstdlib>

int main()
{
    AliceViewer viewer;
    if (viewer.init() != 0)
    {
        return EXIT_FAILURE;
    }
    viewer.run();
    viewer.cleanup();
    return 0;
}
