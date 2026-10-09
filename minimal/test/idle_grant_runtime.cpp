#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static unsigned completed;
static bool owner(){return true;}
static bool health(risc_runtime_health_v1*h){h->uptime_ms=1;return true;}
static void delay(uint32_t){}
static bool line(const char*s){if(!strcmp(s,"IDLE exact grants acquired and released"))completed++;return true;}
static int32_t get(void*,uint32_t n,const char*,void*,uint32_t,uint32_t*s){assert(n==1);*s=0;return RISC_KEY_VALUE_NOT_FOUND;}
static int32_t put(void*,uint32_t n,const char*,const void*,uint32_t){assert(n==1);return 0;}
int main(int argc,char**argv){assert(argc==2||argc==3);RiscBoot::KeyValueBackend kv{nullptr,get,put};
 RiscBoot::Runtime r({owner,health,delay,line,nullptr,&kv});bool prepared=r.prepare(argv[1]);
 if(argc==3){assert(!prepared&&!completed);printf("Rejected invalid idle mapping before activation: %s\n",r.error());return 0;}
 if(!prepared)fprintf(stderr,"%s\n",r.error());assert(prepared);assert(r.run()&&!r.retained()&&completed==1);
 puts("Actual Runtime/Graph: physical 17/3/4/9/15/16 + alarm2/0 + KV1; no raw or retained-wake grants PASS");}
