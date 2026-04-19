#ifndef ALICE_NETWORK_H
#define ALICE_NETWORK_H
#include <curl/curl.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
namespace Alice {
struct Network {
    static size_t wc(void* p, size_t s, size_t n, void* u) { size_t r=s*n; auto* m=(std::vector<uint8_t>*)u; size_t o=m->size(); m->resize(o+r); memcpy(m->data()+o,p,r); return r; }
    static bool Fetch(const char* u, std::vector<uint8_t>& b, long* sc=0, std::string* bd=0, std::string* h=0, long to=10, const char* bt=0) {
        std::string s(u); if(s.find("http")!=0) { FILE* f=fopen(u,"rb"); if(!f)return 0; fseek(f,0,2); size_t sz=ftell(f); fseek(f,0,0); b.resize(sz); fread(b.data(),1,sz,f); fclose(f); return 1; }
        CURL* c=curl_easy_init(); if(!c)return 0; curl_easy_setopt(c,CURLOPT_URL,u); curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wc); curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
        curl_easy_setopt(c,CURLOPT_TIMEOUT,to); curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,0L); curl_easy_setopt(c,CURLOPT_USERAGENT,"Alice/1.0"); curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        struct curl_slist* headers = NULL;
        if(bt) { 
            char auth[2048]; snprintf(auth, 2048, "Authorization: Bearer %s", bt); 
            headers = curl_slist_append(headers, auth); 
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers); 
        }
        CURLcode r=curl_easy_perform(c); if(sc)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,sc); if(r!=CURLE_OK)printf("[NET] FAIL: %s (%s)\n",u,curl_easy_strerror(r)); fflush(stdout);
        if(headers) curl_slist_free_all(headers); curl_easy_cleanup(c); return r==CURLE_OK;
    }
    static bool FetchWithRetry(const char* u, std::vector<uint8_t>& b, long to=10, const char* bt=0, int maxRetries=5) {
        int retry = 0; int waitMs = 1000;
        while(retry < maxRetries) {
            long sc = 0; b.clear();
            bool ok = Fetch(u, b, &sc, 0, 0, to, bt);
            if(ok && sc == 200) return true;
            if(sc == 429 || sc == 503 || !ok) {
                printf("[NET] Error (HTTP %ld / %s). Retrying %d/%d in %dms...\n", sc, ok?"OK":"FAIL", retry+1, maxRetries, waitMs); fflush(stdout);
                std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
                waitMs *= 2; retry++; continue;
            }
            return false;
        }
        return false;
    }
};
}
#endif
