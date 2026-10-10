#include <RiscRuntimeV1.h>
#include <assert.h>
__attribute__((visibility("default"))) void app_main(void) {
 const risc_runtime_api_v1*r=risc_runtime_get_api(1);assert(r);
 const char *names[]={"x4.power","display.output","input.touch.raw","storage.volume","net.wifi","bluetooth.hci","alarm.service","storage.key-value"};
 const uint32_t versions[]={1,1,1,1,1,1,2,1};const uint64_t ids[]={17,3,4,9,15,16,0,1};
 risc_runtime_capability_v1 grants[8]={0},bad={.struct_size=sizeof(bad)};
 assert(!r->acquire("platform.gpio",1,0,&bad));assert(!r->acquire("runtime.retained-wake",1,0,&bad));
 assert(!r->acquire("x4.power",1,18,&bad));assert(!r->acquire("x4.power",2,17,&bad));
 for(unsigned i=0;i<8;i++){grants[i].struct_size=sizeof(grants[i]);assert(r->acquire(names[i],versions[i],ids[i],&grants[i]));}
 /* Existing Runtime request0 resolves a single authorized physical provider. */
 for(unsigned i=0;i<6;i++) {risc_runtime_capability_v1 unique={.struct_size=sizeof(unique)};assert(r->acquire(names[i],1,0,&unique));assert(unique.api==grants[i].api);assert(r->release(&unique));}
 for(unsigned i=8;i;i--)assert(r->release(&grants[i-1]));
 assert(r->diagnostic("IDLE exact grants acquired and released"));
}
