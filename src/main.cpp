#include "AliceViewer.h"
#include <cstdio>
#include <curl/curl.h>

int main(int argc, char** argv)
{
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
