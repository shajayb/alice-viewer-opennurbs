#include "AliceViewer.h"

int main()
{
    AliceViewer viewer;
    viewer.init();
    viewer.run();
    viewer.cleanup();
    return 0;
}
