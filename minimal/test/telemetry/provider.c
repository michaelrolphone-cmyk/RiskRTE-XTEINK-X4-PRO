#include "RiscProviderV2.h"
extern const void *telemetry_fixture_api(int);
extern void telemetry_fixture_start(int);
extern bool telemetry_fixture_quiesce(int);
static bool start(const risc_provider_dependency_v1*d,size_t n){(void)d;if(n)return false;telemetry_fixture_start(KIND);return true;}
static bool quiesce(void){return telemetry_fixture_quiesce(KIND);}
static void stop(void){}
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){
 static risc_driver_v2 driver={2,sizeof(driver),ID,CAPABILITY,1,NULL,start,stop,quiesce};
 driver.capability=telemetry_fixture_api(KIND);return abi==2?&driver:NULL;
}
