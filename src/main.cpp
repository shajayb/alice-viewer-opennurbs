#include "AliceViewer.h"
#include <cstdio>

int main(int argc, char** argv)
{
    AliceViewer av;
    if (av.init(argc, argv) == 0)
    {
        av.run();
        av.cleanup();
        return 0;
    }
    
    fprintf(stderr, "[FATAL] AliceViewer initialization failed.\n");
    return 1;
}
