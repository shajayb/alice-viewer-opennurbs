#include "AliceViewer.h"
#include <cstdlib>

int main(int argc, char** argv)
{
    AliceViewer viewer;
    if (viewer.init(argc, argv) != 0)
    {
        return EXIT_FAILURE;
    }
    viewer.run();
    viewer.cleanup();
    return 0;
}
