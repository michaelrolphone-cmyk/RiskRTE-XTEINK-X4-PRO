#!/usr/bin/env python3
"""Exercise experiment wrappers with the unmodified IDF 4.4.7 esp_clk_init body."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root / 'idf447-clk-source.c').read_text()
start = source.index(' __attribute__((weak)) void esp_clk_init(void)')
end = source.index('\nstatic void select_rtc_slow_clk(', start)
body = source[start:end].replace(' __attribute__((weak)) void esp_clk_init(void)',
                               'void __real_esp_clk_init(void)')
stubs = '''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#define IRAM_ATTR
#define DRAM_ATTR
#define CONFIG_BOOTLOADER_WDT_ENABLE 1
#define CONFIG_BOOTLOADER_WDT_TIME_MS 9000
#define CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ 240
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#define RTC_XTAL_FREQ_40M 40
#define RTC_FAST_FREQ_8M 1
#define RTC_SLOW_FREQ_RTC 0
#define RESET_REASON_CHIP_POWER_ON 1
#define WDT_RWDT 0
#define WDT_STAGE0 0
#define WDT_STAGE_ACTION_RESET_RTC 0
typedef int soc_reset_reason_t;
typedef struct {unsigned ck8m_wait,xtal_wait,pll_wait,clkctl_init,pwrctl_init,rtc_dboost_fpd,xtal_fpu,bbpll_fpu,cpu_waiti_clk_gate,cali_ocode;} rtc_config_t;
#define RTC_CONFIG_DEFAULT() {1,2,3,1,1,1,0,0,1,0}
typedef struct {unsigned source,source_freq_mhz,div,freq_mhz;} rtc_cpu_freq_config_t;
typedef struct {unsigned inst;int *rwdt_dev;} wdt_hal_context_t;
extern int RTCCNTL;
int esp_rom_get_reset_reason(int);
void rtc_init(rtc_config_t);
unsigned rtc_clk_xtal_freq_get(void);
void rtc_clk_fast_freq_set(unsigned);
unsigned rtc_clk_slow_freq_get_hz(void);
void wdt_hal_write_protect_disable(wdt_hal_context_t*);
void wdt_hal_feed(wdt_hal_context_t*);
void wdt_hal_config_stage(wdt_hal_context_t*,unsigned,uint32_t,unsigned);
void wdt_hal_write_protect_enable(wdt_hal_context_t*);
void select_rtc_slow_clk(int);
void rtc_clk_cpu_freq_get_config(rtc_cpu_freq_config_t*);
bool rtc_clk_cpu_freq_mhz_to_config(unsigned,rtc_cpu_freq_config_t*);
void esp_rom_uart_tx_wait_idle(int);
void rtc_clk_cpu_freq_set_config(rtc_cpu_freq_config_t*);
unsigned cpu_hal_get_cycle_count(void);
void cpu_hal_set_cycle_count(uint64_t);
'''
test = r'''
#include "stub.h"
#include <stdio.h>
#include <string.h>
#include "X4RtcBeforeMspi.c"
int RTCCNTL;
static int reason;
static unsigned pll_calls,rtc_calls,flash_calls,event_count;
static const char *events[128];
static rtc_config_t received;
static void event(const char *s){assert(event_count<128);events[event_count++]=s;}
int esp_rom_get_reset_reason(int cpu){assert(cpu==0);return reason;}
void __real_rtc_clk_recalib_bbpll(void){++pll_calls;event("pll");}
void __real_rtc_init(rtc_config_t cfg){++rtc_calls;received=cfg;event("rtc");}
void __real_spi_flash_init_chip_state(void){++flash_calls;event("flash");}
void rtc_init(rtc_config_t cfg){__wrap_rtc_init(cfg);}
unsigned rtc_clk_xtal_freq_get(void){return 40;}
void rtc_clk_fast_freq_set(unsigned value){assert(value==1);event("fast-clock");}
unsigned rtc_clk_slow_freq_get_hz(void){return 150000;}
void wdt_hal_write_protect_disable(wdt_hal_context_t *c){assert(c->rwdt_dev==&RTCCNTL);event("wdt-unlock");}
void wdt_hal_feed(wdt_hal_context_t *c){(void)c;event("wdt-feed");}
void wdt_hal_config_stage(wdt_hal_context_t *c,unsigned stage,uint32_t ticks,unsigned action){
 (void)c;assert(stage==0&&action==0);assert(ticks==240000||ticks==1350000);event("wdt-stage");}
void wdt_hal_write_protect_enable(wdt_hal_context_t *c){(void)c;event("wdt-lock");}
void select_rtc_slow_clk(int c){assert(c==0);event("slow-clock");}
void rtc_clk_cpu_freq_get_config(rtc_cpu_freq_config_t *c){c->freq_mhz=80;event("cpu-get");}
bool rtc_clk_cpu_freq_mhz_to_config(unsigned n,rtc_cpu_freq_config_t *c){assert(n==240);c->freq_mhz=n;event("cpu-config");return true;}
void esp_rom_uart_tx_wait_idle(int n){assert(n==0);event("uart-drain");}
void rtc_clk_cpu_freq_set_config(rtc_cpu_freq_config_t *c){assert(c->freq_mhz==240);event("cpu-set");}
unsigned cpu_hal_get_cycle_count(void){return 100;}
void cpu_hal_set_cycle_count(uint64_t n){assert(n==300);event("cycle-adjust");}
#include "actual-clock-body.c"
static void reset(int r){reason=r;pll_calls=rtc_calls=flash_calls=event_count=0;risc_x4_rtc_order_state=0;risc_x4_rtc_order_skipped=0;}
int main(void){
 for(unsigned reset_class=0;reset_class<5;++reset_class){
  const int reasons[]={1,3,5,15,21};reset(reasons[reset_class]);
  __wrap_spi_flash_init_chip_state();
  assert(pll_calls==1&&rtc_calls==1&&flash_calls==1);
  assert(!strcmp(events[0],"pll")&&!strcmp(events[1],"rtc")&&!strcmp(events[2],"flash"));
  rtc_config_t expected=RTC_CONFIG_DEFAULT();expected.cali_ocode=reason==1;
  assert(!memcmp(&received,&expected,sizeof(expected)));
  assert(risc_x4_rtc_order_state==X4_RTC_ORDER_FLASH_READY);
  __wrap_rtc_clk_recalib_bbpll();event("flash-tuning");event("psram");
  assert(pll_calls==1&&risc_x4_rtc_order_state==X4_RTC_ORDER_RECALIB_SKIPPED);
  __wrap_esp_clk_init();
  assert(risc_x4_rtc_order_state==X4_RTC_ORDER_COMPLETE&&risc_x4_rtc_order_skipped==1&&rtc_calls==1);
  const char *rest[]={"fast-clock","wdt-unlock","wdt-feed","wdt-stage","wdt-lock","slow-clock","wdt-unlock","wdt-feed","wdt-stage","wdt-lock","cpu-get","cpu-config","uart-drain","cpu-set","cycle-adjust"};
  assert(event_count==5+sizeof(rest)/sizeof(rest[0]));
  for(unsigned i=0;i<sizeof(rest)/sizeof(rest[0]);++i)assert(!strcmp(events[i+5],rest[i]));
  __wrap_esp_clk_init();assert(rtc_calls==2&&risc_x4_rtc_order_skipped==1);
  __wrap_rtc_init(expected);assert(rtc_calls==3);
  __wrap_rtc_clk_recalib_bbpll();assert(pll_calls==2);
  __wrap_spi_flash_init_chip_state();assert(flash_calls==2&&rtc_calls==3&&pll_calls==2);
 }
 // Without an early hook, all original behavior must still execute.
 reset(1);__wrap_esp_clk_init();assert(rtc_calls==1&&risc_x4_rtc_order_state==0);
 __wrap_rtc_clk_recalib_bbpll();assert(pll_calls==1);
 // An unrelated rtc_init between phases must not be swallowed.
 reset(1);__wrap_spi_flash_init_chip_state();rtc_config_t c=RTC_CONFIG_DEFAULT();
 __wrap_rtc_init(c);assert(rtc_calls==2&&risc_x4_rtc_order_skipped==0);
 puts("RTC-before-MSPI experiment: exact IDF clock body, five reset classes, defaults, order, once-only suppression, later-call pass-through PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    p = Path(temp)
    (p/'soc').mkdir()
    (p/'stub.h').write_text(stubs)
    for n in ('esp_attr.h','esp_rom_sys.h','soc/rtc.h'):
        (p/n).write_text('#include "stub.h"\n')
    (p/'actual-clock-body.c').write_text(body)
    (p/'test.c').write_text(test)
    exe=p/'test'
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-I'+str(p),'-I'+str(root.parents[1]/'native'),str(p/'test.c'),'-o',str(exe)],check=True)
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    subprocess.run([str(exe)],check=True,env=environment)
print('Pinned clock source sha256:',hashlib.sha256(source.encode()).hexdigest())
