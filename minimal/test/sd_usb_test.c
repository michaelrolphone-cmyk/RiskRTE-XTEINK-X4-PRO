/* Actual native trace producer, production SD/FatFs/export and USB MSC owner.
 * Only board GPIO/time/sync/NVS and USB packet transport are simulated. */
#define main original_sd_test_main
#include "sd_test.c"
#undef main
#include "tusb.h"
#include "device/dcd.h"
#include "RiscUsbDeviceMscV1.h"
#include "RiscUsbPhyResourceV1.h"
#include "Transport.h"
extern const risc_driver_v2 *usb_t5_driver_get(uint32_t);
extern void usb_native_fixture_boot(void);
extern void usb_native_fixture_continue(void);
extern void usb_native_fixture_progress(void);
extern unsigned long long usb_native_fixture_size(void);
extern int32_t risc_native_diagnostic_read_after(uint64_t,char*,uint32_t,uint32_t*,uint64_t*);
static int32_t native_trace(void*c,uint64_t after,char*out,uint32_t capacity,uint32_t*count,uint64_t*next){
 (void)c;return risc_native_diagnostic_read_after(after,out,capacity,count,next);
}
static const risc_diagnostic_source_api_v1_trace native_source={{1,sizeof(native_source),NULL,fixture_diagnostic_read},native_trace};
static uint32_t read_ms,write_ms;
static uint64_t sd_time(void*c){
 (void)c;now_ms+=(card_reads-timed_reads)*read_ms+(card_writes-timed_writes)*write_ms;
 timed_reads=card_reads;timed_writes=card_writes;return now_ms;
}
static void usb_sleep(void*c,uint32_t ms){(void)c;now_ms+=ms;}
static const risc_platform_clock_api_v1 sd_clock={1,sizeof(sd_clock),NULL,sd_time,sleep_ms};
static const risc_platform_clock_api_v1 usb_clock={1,sizeof(usb_clock),NULL,sd_time,usb_sleep};
static bool phy_owned,usb_live,healthy=true;static unsigned phy_claims;
static bool phy_owner(void*c){(void)c;return owner;}
static bool phy_claim(void*c,uint64_t*t){(void)c;assert(export_state==EXPORT_HOST && !has_handles() && !mounted && bootlog_paused);++phy_claims;phy_owned=true;*t=1;return true;}
static bool phy_release(void*c,uint64_t t){(void)c;assert(t==1 && !usb_live && export_state==EXPORT_LOCAL);phy_owned=false;return true;}
static const risc_usb_phy_resource_api_v1 usb_phy={1,sizeof(usb_phy),NULL,RISC_USB_PHY_ESP32S3_OTG,0,phy_owner,phy_claim,phy_release};
static struct {uint8_t *buffer;uint16_t length;bool pending,stalled;} ep[2][2];
static unsigned slot(uint8_t addr){assert((addr&0x7f)<2);return addr&0x7f;}
void dcd_init(uint8_t p){(void)p;memset(ep,0,sizeof(ep));}
void dcd_int_enable(uint8_t p){(void)p;}
void dcd_int_disable(uint8_t p){(void)p;}
void dcd_connect(uint8_t p){(void)p;}
void dcd_disconnect(uint8_t p){(void)p;}
void dcd_remote_wakeup(uint8_t p){(void)p;assert(false);}
void dcd_sof_enable(uint8_t p,bool x){(void)p;(void)x;}
void dcd_set_address(uint8_t p,uint8_t a){(void)a;assert(dcd_edpt_xfer(p,0x80,NULL,0));}
bool dcd_edpt_open(uint8_t p,const tusb_desc_endpoint_t*d){(void)p;assert(d->bEndpointAddress==1||d->bEndpointAddress==0x81);return true;}
void dcd_edpt_close_all(uint8_t p){(void)p;memset(&ep[1],0,sizeof(ep[1]));}
void dcd_edpt_close(uint8_t p,uint8_t a){(void)p;memset(&ep[slot(a)][a>>7],0,sizeof(ep[0][0]));}
bool dcd_edpt_xfer(uint8_t p,uint8_t a,uint8_t*b,uint16_t n){
 (void)p;unsigned e=slot(a),d=a>>7;assert(!ep[e][d].pending);ep[e][d].buffer=b;ep[e][d].length=n;ep[e][d].pending=true;return true;
}
void dcd_edpt_stall(uint8_t p,uint8_t a){(void)p;ep[slot(a)][a>>7].stalled=true;ep[slot(a)][a>>7].pending=false;}
void dcd_edpt_clear_stall(uint8_t p,uint8_t a){(void)p;ep[slot(a)][a>>7].stalled=false;}
bool risc_msc_transport_start(void){assert(export_state==EXPORT_HOST && phy_owned);usb_live=true;healthy=true;return tud_init(0);}
bool risc_msc_transport_poll(void){assert(usb_live);tud_task_ext(0,false);return healthy;}
bool risc_msc_transport_stop(void){assert(usb_live);usb_live=false;risc_msc_stack_reset();return true;}
void risc_msc_transport_fault(void){healthy=false;}
bool risc_msc_transport_ok(void){return healthy;}

static const risc_usb_device_msc_api_v1 *usb_client;
static uint64_t usb_session;
static risc_usb_device_msc_status_v1 usb_status;
static int32_t usb_poll(void){usb_status.struct_size=sizeof(usb_status);return usb_client->poll(NULL,usb_session,&usb_status);}
static void complete(uint8_t a,const void*input,uint32_t count){
 unsigned e=slot(a),d=a>>7;assert(ep[e][d].pending&&count<=ep[e][d].length);
 if(!d&&count)memcpy(ep[e][d].buffer,input,count);
 ep[e][d].pending=false;dcd_event_xfer_complete(0,a,count,XFER_RESULT_SUCCESS,false);
}
static void setup(uint8_t request,uint16_t value){
 uint8_t bytes[8]={0,request,(uint8_t)value,(uint8_t)(value>>8),0,0,0,0};
 dcd_event_setup_received(0,bytes,false);assert(usb_poll()==0);assert(ep[0][1].pending&&ep[0][1].length==0);
 complete(0x80,NULL,0);assert(usb_poll()==0);
}
static void configure(void){dcd_event_bus_reset(0,TUSB_SPEED_FULL,false);assert(usb_poll()==0);setup(TUSB_REQ_SET_ADDRESS,1);setup(TUSB_REQ_SET_CONFIGURATION,1);assert(usb_status.state==RISC_USB_MSC_CONNECTED);assert(ep[1][0].pending&&ep[1][0].length==31);}
static uint32_t tag;
static void usb_command(uint8_t op,uint32_t bytes,bool input,uint32_t lba,uint16_t count,uint8_t control){
 msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=++tag,.total_bytes=bytes,.dir=input?0x80:0,.lun=0,.cmd_len=(op==0x28||op==0x2a||op==0x25||op==0x35)?10:6};
 cbw.command[0]=op;
 if(op==0x28||op==0x2a){cbw.command[2]=(uint8_t)(lba>>24);cbw.command[3]=(uint8_t)(lba>>16);cbw.command[4]=(uint8_t)(lba>>8);cbw.command[5]=(uint8_t)lba;cbw.command[7]=(uint8_t)(count>>8);cbw.command[8]=(uint8_t)count;}
 else cbw.command[4]=control;
 complete(1,&cbw,sizeof(cbw));(void)usb_poll();
}
static unsigned csw(bool finish){
 assert(ep[1][1].pending&&ep[1][1].length==13);msc_csw_t c;memcpy(&c,ep[1][1].buffer,sizeof(c));assert(c.signature==MSC_CSW_SIGNATURE&&c.tag==tag);
 if(finish){complete(0x81,NULL,13);(void)usb_poll();}return c.status;
}
int main(int argc,char**argv) {
 assert(argc==3 || argc==4);const char *scenario=argc==4?argv[3]:"full";read_ms=(uint32_t)strtoul(argv[1],NULL,10);write_ms=(uint32_t)strtoul(argv[2],NULL,10);
 usb_native_fixture_boot();format(false);deps[2].api=&sd_clock;deps[5].api=&native_source.base;
 assert(START());
 printf("mount: source=%llu cursor=%llu retained=%d handles=%d io_failed=%d log=%s elapsed=%llu\n",usb_native_fixture_size(),(unsigned long long)bootlog_cursor,bootlog_retained,has_handles(),io_failed,bootlog_error?bootlog_error:"none",(unsigned long long)(now_ms-operation_start));
 usb_native_fixture_continue();
 // Runtime services the SD consumer before/after capability acquisition.
 bootlog_service(1000);
 printf("service: source=%llu cursor=%llu retained=%d handles=%d io_failed=%d log=%s elapsed=%llu sectors=%u\n",usb_native_fixture_size(),(unsigned long long)bootlog_cursor,bootlog_retained,has_handles(),io_failed,bootlog_error?bootlog_error:"none",(unsigned long long)(now_ms-operation_start),operation_sectors);
 const risc_driver_v2*d=usb_t5_driver_get(2);const risc_driver_v2*sd=t5_driver_get(2);
 risc_provider_dependency_v1 usb_deps[]={{"platform.clock",1,&usb_clock},{RISC_USB_PHY_RESOURCE_CAPABILITY,1,&usb_phy},{"storage.volume",1,sd->capability}};
 assert(d->start(usb_deps,3));const risc_usb_device_msc_api_v1*u=d->capability;
 const uint64_t cutoff=usb_native_fixture_size();
 uint64_t token=0;int32_t result=u->begin(NULL,&token);usb_client=u;usb_session=token;char diagnostic[256];u->last_error(NULL,diagnostic,sizeof(diagnostic));
 char sd_error[256];bootlog_descriptor_error(sd_error,sizeof(sd_error));
 printf("usb_begin: result=%d token=%llu PHY_claims=%u usb=%s sd=%s cursor=%llu elapsed=%llu sectors=%u\n",result,(unsigned long long)token,phy_claims,diagnostic,sd_error,(unsigned long long)bootlog_cursor,(unsigned long long)(now_ms-operation_start),operation_sectors);
 if(result==RISC_USB_MSC_OK){
  const risc_usb_device_msc_api_v1_prepare *prep=risc_usb_device_msc_prepare(u);assert(prep);
  risc_usb_device_msc_status_v1 status={.struct_size=sizeof(status)};assert(u->poll(NULL,token,&status)==0 && status.state==RISC_USB_MSC_PREPARING && !phy_claims);
  unsigned steps=0;while(status.state==RISC_USB_MSC_PREPARING){
   usb_native_fixture_progress();
   if(!strcmp(scenario,"timeout"))read_ms=write_ms=2000;
   if(!strcmp(scenario,"write-fail"))card_reject_write=true;
   if(!strcmp(scenario,"close-fail"))log_fail_close=true;
   const unsigned io=card_reads+card_writes;for(unsigned i=0;i<30;++i)assert(u->poll(NULL,token,&status)==0);assert(card_reads+card_writes==io && !phy_claims);
   const int32_t advanced=prep->prepare_step(NULL,token);
   if(strcmp(scenario,"full") && strcmp(scenario,"cancel")) {
    assert(advanced==RISC_USB_MSC_RETAINED && !phy_claims && !usb_live);
    assert(u->poll(NULL,token,&status)==RISC_USB_MSC_RETAINED && status.state==RISC_USB_MSC_FAULT_RETAINED);
    assert(u->last_error(NULL,diagnostic,sizeof(diagnostic)) && strstr(diagnostic,"SD export result=-2"));
    if(!strcmp(scenario,"timeout"))assert(strstr(diagnostic,"budget exceeded"));
    assert(u->end(NULL,token,0)==RISC_USB_MSC_RETAINED && !d->quiesce());
    printf("checked genuine failure %s: %s PASS\n",scenario,diagnostic);free(card_image);return 0;
   }
   assert(advanced==0);assert(u->poll(NULL,token,&status)==0);assert(++steps<128);
   if(!strcmp(scenario,"cancel")) {
    assert(u->end(NULL,token,0)==0 && !phy_claims && !usb_live && mounted && !bootlog_paused && !has_handles());
    assert(u->poll(NULL,token,&status)==RISC_USB_MSC_REFUSED && d->quiesce());verify_cleanup();
    puts("cancel after durable preparation chunk preserves local ownership PASS");free(card_image);return 0;
   }
  }
  assert(status.state==RISC_USB_MSC_WAITING && phy_claims==1 && bootlog_cursor==cutoff && cutoff<usb_native_fixture_size());
  const unsigned io=card_reads+card_writes;bootlog_service(1000);assert(card_reads+card_writes==io);
  configure();
  usb_command(0x28,512,true,0,1,0);assert(ep[1][1].length==512 && !memcmp(ep[1][1].buffer,card_image,512));complete(0x81,NULL,512);assert(usb_poll()==0 && csw(true)==0);
  uint8_t host_data[512];memset(host_data,0xa6,sizeof(host_data));usb_command(0x2a,512,false,card_sectors-1,1,0);assert(ep[1][0].length==512);complete(1,host_data,512);assert(usb_poll()==0 && csw(true)==0 && !memcmp(card_image+(size_t)(card_sectors-1)*512,host_data,512));
  const unsigned log_before=log_writes;bootlog_service(1000);assert(log_writes==log_before && bootlog_cursor==cutoff);
  usb_command(0x1b,0,false,0,0,2);assert(csw(false)==0 && usb_live && !mounted);complete(0x81,NULL,13);assert(usb_poll()==0 && usb_status.state==RISC_USB_MSC_EJECTED);
  assert(u->end(NULL,token,0)==0 && !phy_owned && !usb_live && mounted && !bootlog_paused);assert(d->quiesce());
  now_ms+=2001;assert(ready(NULL));now_ms+=2001;assert(ready(NULL));
  char saved[65536],expected[65536];size_t n=read_log(saved,sizeof(saved)),used=0;uint64_t next=0;
  while(used<sizeof(expected)){uint32_t count=0;int32_t r=risc_native_diagnostic_read_after(used,expected+used,RISC_DIAGNOSTIC_SOURCE_TEXT_MAX,&count,&next);if(!r)break;assert(r==1 && next==used+count);used+=count;}
  assert(n==used && !memcmp(saved,expected,n));verify_cleanup();
  printf("prepared=%u elapsed_ms=%llu exact_tail=%llu PASS\n",steps,(unsigned long long)now_ms,(unsigned long long)bootlog_cursor);
 }
 else {assert(result==RISC_USB_MSC_RETAINED && !phy_claims && token && !usb_live);assert(!d->quiesce());}
 free(card_image);return result==RISC_USB_MSC_OK?0:2;
}
