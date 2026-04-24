#include "AliceViewer.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <curl/curl.h>
#include <windows.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

void signal_handler(int signum) {
    printf("[FATAL] Process died with signal: %d\n", signum);
    void* stack[100];
    unsigned short frames = CaptureStackBackTrace(0, 100, stack, NULL);
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    for(int i = 0; i < frames; i++) {
        SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
        printf("%i: %s - 0x%p\n", frames - i - 1, symbol->Name, (void*)symbol->Address);
    }
    fflush(stdout);
    exit(1);
}

#ifdef _MSC_VER
#pragma comment(linker, "/STACK:8388608")
#endif

int main(int argc, char** argv)
{
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGFPE, signal_handler);

    setvbuf(stdout, NULL, _IONBF, 0);
    curl_global_init(CURL_GLOBAL_ALL);
    AliceViewer av;
    if (av.init(argc, argv) == 0)
    {
        try {
            av.run();
        } catch (const std::exception& e) {
            printf("[FATAL] Exception: %s\n", e.what()); fflush(stdout);
            return 1;
        } catch (...) {
            printf("[FATAL] Unknown Exception\n"); fflush(stdout);
            return 1;
        }
        av.cleanup();
        return 0;
    }
    
    fprintf(stderr, "[FATAL] AliceViewer initialization failed.\n");
    return 1;
}
