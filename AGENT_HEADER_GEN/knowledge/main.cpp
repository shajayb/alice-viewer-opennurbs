#include "AliceViewer.h"
#include <cstdlib>

int main()
{
    AliceViewer viewer;
    if (viewer.init() != EXIT_SUCCESS)
    {
        return EXIT_FAILURE;
    }
    viewer.run();
    viewer.cleanup();
    return 0;
}
