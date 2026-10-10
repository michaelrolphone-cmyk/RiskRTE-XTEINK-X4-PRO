#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <string.h>
static const uint32_t table[2]={TEST_API,sizeof(table)};
static bool start(const risc_provider_dependency_v1*d,size_t n) {
#if TEST_INSTANCE
 for(size_t i=0;i<n;i++)if(!strcmp(d[i].capability_id,"hardware.device"))
  return d[i].api_version==1 && ((const risc_hardware_device_v1*)d[i].api)->instance_id==TEST_INSTANCE;
 return false;
#else
 (void)d;return n==0;
#endif
}
static void stop(void){}
static bool quiesce(void){return true;}
static const risc_driver_v2 driver={2,sizeof(driver),TEST_ID,TEST_CAP,TEST_API,table,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t v){return v==2?&driver:0;}
