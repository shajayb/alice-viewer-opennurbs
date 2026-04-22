#include "AliceViewer.h"
#include <cstdio>
#include <curl/curl.h>

#ifdef _MSC_VER
#pragma comment(linker, "/STACK:8388608")
#endif

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    curl_global_init(CURL_GLOBAL_ALL);
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
