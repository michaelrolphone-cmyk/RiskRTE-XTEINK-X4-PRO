/* Deployment-local X4 light sleep. Retain e-paper image and foreground RAM.
 * Caller closes touch and drains display; native CPU port vetoes active work.
 * This grants neither deep-sleep nor Watch PMU authority. */
#include "PortableAppSleep.h"
#include "../drivers/x4pro_power/X4PowerV1.h"
#include <RiscTimedSleepV1.h>
#ifdef PORTABLE_QUICK_ACTIONS
extern unsigned portable_quick_brightness(void);
#endif
int portable_app_alarm_sleep(const risc_runtime_api_v1 *rt,const risc_display_output_api_v1 *display,
                            const risc_battery_gauge_api_v1 *gauge,const alarm_service_v1 *alarms) {
    (void)gauge;
    risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
    if(!rt->acquire(X4_POWER_CAPABILITY,1,17,&grant))return 0;
    const x4_power_v1 *power=grant.api;
    int result=0;
    if(!power || power->api_version!=1 || power->struct_size<sizeof(*power) ||
       !power->read_key || !power->light_sleep || !display || !display->set_brightness ||
       !alarms || alarms->api_version!=1 || alarms->struct_size<sizeof(*alarms) ||
       !alarms->prepare_sleep || !alarms->step || !alarms->status)goto done;
    /* Bounded release barrier; no initial held/waking key becomes an action. */
    bool neutral=false;
    risc_runtime_health_v1 health={.struct_size=sizeof(health)};
    if(!rt->health(&health))goto done;
    uint32_t release_start=health.uptime_ms;
    for(unsigned n=0;n<80;n++) {
        bool down=true;if(!power->read_key(power->context,&down))goto done;
        if(!rt->health(&health))goto done;
        if(!down && (uint32_t)(health.uptime_ms-release_start)>=30){neutral=true;break;}
        if(down)release_start=health.uptime_ms;
        rt->yield_ms(5);
    }
    if(!neutral)goto done;
    if(!display->set_brightness(display->context,0,100)){result=-1;goto done;}
    alarm_sleep_v1 decision={.struct_size=sizeof(decision)};
    int32_t ready=ALARM_PENDING;
    for(unsigned n=0;n<64 && ready==ALARM_PENDING;n++) {
        decision=(alarm_sleep_v1){.struct_size=sizeof(decision)};
        ready=alarms->prepare_sleep(alarms->context,&decision);
        if(ready==ALARM_PENDING)(void)alarms->step(alarms->context);
    }
    if(ready==ALARM_OK) {
        uint32_t duration=0;
        if(decision.deadline) {
            uint32_t seconds=decision.deadline>decision.rtc_seconds?decision.deadline-decision.rtc_seconds:0;
            duration=seconds>RISC_TIMED_SLEEP_MAX_MS/1000u?RISC_TIMED_SLEEP_MAX_MS:seconds?seconds*1000u:1u;
        }
        risc_light_sleep_result_v1 wake={.struct_size=sizeof(wake)};
        int32_t status=power->light_sleep(power->context,duration,&wake);
        /* Native retained ownership forbids even grant release or restoration. */
        if(status==RISC_LIGHT_SLEEP_RETAINED)return -2;
        result=status==RISC_LIGHT_SLEEP_OK?1:0;
    }
    if(!display->set_brightness(display->context,
#ifdef PORTABLE_QUICK_ACTIONS
       (uint16_t)portable_quick_brightness(),
#else
       40,
#endif
       100))result=-1;
done:
    if(!rt->release(&grant))return -1;
    return result;
}
