#include "bootstrap/Runtime.h"
#include "RiscBluetoothHostV1.h"
#include "RiscBatteryGaugeV1.h"
#include "RiscPlatformClockV1.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static std::string mode;
static unsigned now,entries,finis,claims,releases,sends,starts[2],ns_reads;
static bool claimed,rf,fail_close,stored,radio_stored;
static uint8_t broadcast_record[4],radio_record[4],queued[260],packet[31];
static size_t queued_size,packet_size;
extern "C" unsigned telemetry_fixture_entry(){return ++entries;}
extern "C" const char *telemetry_fixture_mode(){return mode.c_str();}
extern "C" void telemetry_fixture_fini(){finis++;}
extern "C" void telemetry_fixture_start(int kind){assert(kind<2);starts[kind]++;}
extern "C" bool telemetry_fixture_quiesce(int kind){return kind==0 || !claimed;}
extern "C" void telemetry_fixture_radio(unsigned flags){radio_stored=true;radio_record[0]=0x51;radio_record[1]=1;radio_record[2]=flags;radio_record[3]=flags^0xa5u;}
extern "C" void telemetry_fixture_fail_close(){fail_close=true;}
extern "C" void telemetry_fixture_check(unsigned live){assert(rf==bool(live));if(live){const uint8_t expected[]={2,1,6,11,0x16,0xd2,0xfc,0x40,1,73,0x0c,0x82,0x0f,0x16,1};assert(packet_size==sizeof(expected)&&!memcmp(packet,expected,sizeof(expected)));}}
static bool battery_read(void*,risc_battery_sample_v1*out){*out={3970,73,RISC_BATTERY_CHARGING};return true;}
static const risc_battery_gauge_api_v1 battery={1,sizeof(battery),nullptr,battery_read};
static bool claim(void*,uint64_t*out){assert(!claimed);claimed=true;claims++;*out=1;return true;}
static int32_t release(void*,uint64_t token){assert(token==1&&claimed);releases++;if(fail_close)return -1;claimed=rf=false;queued_size=0;return 1;}
static bool send(void*,uint64_t token,uint8_t kind,const uint8_t*p,size_t n){assert(claimed&&token==1&&kind==1&&n==3u+p[2]);sends++;unsigned op=p[0]|unsigned(p[1])<<8;uint8_t ack[]={14,4,1,p[0],p[1],0};memcpy(queued,ack,6);queued_size=6;if(op==0x2018){queued[1]=12;for(unsigned i=0;i<8;i++)queued[6+i]=0x12+i;queued_size=14;}if(op==0x2008){packet_size=p[3];memcpy(packet,p+4,31);}if(op==0x200a)rf=p[3]!=0;return true;}
static int32_t next(void*,uint64_t token,uint8_t*kind,uint8_t*p,size_t cap,size_t*n){assert(claimed&&token==1&&cap>=queued_size);*kind=4;*n=queued_size;if(!queued_size)return 0;memcpy(p,queued,queued_size);queued_size=0;return 1;}
static const portable_bluetooth_host_v1 hci={{1,sizeof(hci),nullptr,nullptr,nullptr,nullptr,nullptr},claim,send,next,release};
extern "C" const void *telemetry_fixture_api(int kind){return kind==0?static_cast<const void*>(&battery):static_cast<const void*>(&hci);}
static uint64_t clock_now(void*){return now;}
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),nullptr,clock_now,nullptr};
static int32_t get(void*,uint32_t ns,const char*key,void*out,uint32_t capacity,uint32_t*size){assert(ns==1&&capacity>=4);ns_reads++;*size=0;const uint8_t*p=nullptr;if(!strcmp(key,"ble_broadcast")){if(!stored)return 1;p=broadcast_record;}else {assert(!strcmp(key,"quick_radio"));if(!radio_stored)return 1;p=radio_record;}memcpy(out,p,4);*size=4;return 0;}
static int32_t put(void*,uint32_t ns,const char*key,const void*bytes,uint32_t size){assert(ns==1&&!strcmp(key,"ble_broadcast")&&size==4);memcpy(broadcast_record,bytes,4);stored=true;return 0;}
int main(int argc,char**argv){assert(argc==3);mode=argv[2];RiscBoot::KeyValueBackend kv{nullptr,get,put,64};
 RiscBoot::Port port{[](){return true;},[](risc_runtime_health_v1*h){h->uptime_ms=now;return true;},[](uint32_t n){now+=n;},[](const char*){return true;}};port.keyValue=&kv;
 port.bindPlatforms=[](RiscBoot::Runtime&r){return r.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,&clock_api);};
 auto*runtime=new RiscBoot::Runtime(port);if(!runtime->prepare(argv[1])){fprintf(stderr,"%s\n",runtime->error());assert(0);}
 bool result=runtime->run();if(mode=="retained"){assert(!result&&runtime->retained()&&rf&&claimed&&finis==0);puts("Actual Runtime, advertising cleanup failure retains mapped invocation/providers PASS");std::fflush(stdout);std::_Exit(0);}
 if(!result)fprintf(stderr,"%s\n",runtime->error());assert(result&&!runtime->retained());
 if(mode=="timer")assert(!starts[0]&&!starts[1]&&!claims&&!sends&&!ns_reads&&entries==1&&finis==1);
 else assert(starts[0]==1&&starts[1]==1&&entries==3&&finis==3&&claims==1&&releases==1&&!claimed&&!rf);
 delete runtime;printf("Actual Runtime %s: exact 3-field BTHome packet,16-live-slot client, navigation and radio-policy cleanup PASS\n",mode.c_str());
}
