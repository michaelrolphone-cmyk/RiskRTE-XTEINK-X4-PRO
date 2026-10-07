/* Deployment-local X4 light sleep. Retain e-paper image and foreground RAM.
 * Caller closes touch and drains display; native CPU port vetoes active work.
 * This grants neither deep-sleep nor Watch PMU authority. */
#include "PortableAppSleep.h"
#include "../drivers/x4pro_power/X4PowerV1.h"
#include <RiscTimedSleepV1.h>
#ifdef PORTABLE_QUICK_ACTIONS
extern unsigned portable_quick_brightness(void);
#endif
#ifdef PORTABLE_DESK_CLOCK
#define portable_app_alarm_sleep portable_x4_light_sleep
static
#endif
int portable_app_alarm_sleep(const risc_runtime_api_v1 *rt,const risc_display_output_api_v1 *display,
                            const risc_battery_gauge_api_v1 *gauge,const alarm_service_v1 *alarms) {
    (void)gauge;
    risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
#if defined(PORTABLE_DESK_CLOCK) && defined(PORTABLE_DESK_CLOCK_SPARSE_START)
    if(!rt->acquire(X4_POWER_CAPABILITY,1,17,&grant))return -2;
#else
    if(!rt->acquire(X4_POWER_CAPABILITY,1,17,&grant))return 0;
#endif
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
#if defined(PORTABLE_DESK_CLOCK) && defined(PORTABLE_DESK_CLOCK_SPARSE_START)
    if(!rt->release(&grant))return -2;
#else
    if(!rt->release(&grant))return -1;
#endif
    return result;
}

#ifdef PORTABLE_DESK_CLOCK
#undef portable_app_alarm_sleep
#include "PortableDeskClockApp.h"
#include "PortableSleepPolicy.h"
#include "PortableBluetoothControl.h"
#include "WifiApi.h"
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <RiscDisplayOutputPowerV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscStorageVolumeV1.h>
#include <RiscRetainedWakeV1.h>

/* This is app policy, not a new Runtime or navigation interface. Every grant
 * remains owned throughout paint, reversible preparation and terminal entry.
 * Checked radio shutdown is explicit. No generic board/rail writes or native
 * ownership bypass is implied: CPU resource admission may still refuse. */
typedef struct {
    const risc_runtime_api_v1 *rt;
    const risc_display_output_api_v1 *display;
    const risc_display_output_api_v1_power *panel;
    const risc_touch_power_api_v1 *touch;
    const risc_storage_volume_api_v1_sleep *storage;
    const x4_power_deep_v1 *power;
    const risc_retained_wake_api_v1 *wake;
    const alarm_service_v1 *alarms;
    const wifi_api_v1 *wifi;
    const portable_bluetooth_control_v1 *bluetooth;
    bool dark, panel_touched, touch_touched, storage_touched, retained;
    uint32_t alarm_duration_ms, alarm_sampled_at;
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
    bool timer_only;
#endif
    bool record_staged;
    risc_retained_wake_record_v1 staged_value;
} x4_desk_sleep;

static bool desk_runtime_valid(const risc_runtime_api_v1 *rt) {
    return rt && rt->api_version==RISC_RUNTIME_API_V1 &&
        rt->struct_size>=RISC_RUNTIME_CAPABILITIES_V1_SIZE && rt->acquire && rt->release;
}
static bool desk_wake_valid(const risc_retained_wake_api_v1 *wake) {
    return wake && wake->api_version==RISC_RETAINED_WAKE_API_V1 &&
        wake->struct_size>=sizeof(*wake) && wake->read && wake->stage && wake->clear;
}
int portable_desk_clock_boot_read(const risc_runtime_api_v1 *rt,portable_desk_record *out) {
    if(!out || !desk_runtime_valid(rt))return 0;
    risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
    /* Demand-start failure can retain provider cleanup without a token. */
    if(!rt->acquire(RISC_RETAINED_WAKE_CAPABILITY,1,0,&grant))return -2;
#else
    if(!rt->acquire(RISC_RETAINED_WAKE_CAPABILITY,1,0,&grant))return 0;
#endif
    const risc_retained_wake_api_v1 *wake=grant.api;
    portable_desk_record record={0};int valid=0;
    if(desk_wake_valid(wake)) {
        risc_retained_wake_record_v1 value={.struct_size=sizeof(value)};
        uint32_t cause=RISC_BOOT_POWER_ON;
        int32_t rc=wake->read(wake->context,PORTABLE_DESK_CLOCK_RECORD_TYPE,
                             PORTABLE_DESK_CLOCK_RECORD_SCHEMA,&value,&cause);
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
        /* CONTEXT may indicate retained native custody. Unknown outcomes also
         * give no authority to release, clear, yield or try another provider. */
        if(rc!=RISC_RETAINED_WAKE_OK && rc!=RISC_RETAINED_WAKE_ABSENT &&
           rc!=RISC_RETAINED_WAKE_MISMATCH && rc!=RISC_RETAINED_WAKE_INVALID)return -2;
#endif
        valid=rc==RISC_RETAINED_WAKE_OK && cause==RISC_BOOT_DEEP_TIMER &&
            value.struct_size>=sizeof(value) && value.type==PORTABLE_DESK_CLOCK_RECORD_TYPE &&
            value.schema_version==PORTABLE_DESK_CLOCK_RECORD_SCHEMA &&
            portable_desk_decode(value.payload,value.size,&record);
    }
    if(!rt->release(&grant))return -2;
    if(valid)*out=record;
    return valid;
}
static int desk_retain(x4_desk_sleep *s) { s->retained=true;return -2; }
static int desk_cancelled(void *context) {
    x4_desk_sleep *s=context;
    if(s->retained)return -2;
    bool down=true;
    /* The bool key API cannot distinguish a failed sample from an uncertain
     * provider unlock. Conservatively retain instead of polling/releasing it. */
    if(!s->power->power.read_key(s->power->power.context,&down)) {
        return desk_retain(s);
    }
    return down?1:0;
}
static int desk_resume(void *context) {
    x4_desk_sleep *s=context;
    if(s->retained)return -2;
    if(s->storage_touched) {
        int32_t rc=s->storage->resume_sleep(s->storage->terminal.power.volume.base.context);
        if(rc!=RISC_STORAGE_SLEEP_READY && rc!=RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE)
            return desk_retain(s);
        s->storage_touched=false;
    }
    if(s->panel_touched) {
        if(s->panel->resume(s->display->context,RISC_DISPLAY_POWER_MAX_BUDGET_MS)!=RISC_DISPLAY_POWER_OK)
            return desk_retain(s);
        s->panel_touched=false;
    }
    if(s->touch_touched) {
        if(s->touch->resume(s->touch->base.context,RISC_TOUCH_POWER_MAX_BUDGET_MS)!=RISC_TOUCH_POWER_OK)
            return desk_retain(s);
        s->touch_touched=false;
    }
    if(s->dark) {
        if(!s->display->set_brightness(s->display->context,
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
            s->timer_only?0:
#endif
#ifdef PORTABLE_QUICK_ACTIONS
            (uint16_t)portable_quick_brightness(),
#else
            40,
#endif
            100))return desk_retain(s);
        s->dark=false;
    }
    /* Runtime retained-wake reads/stage/clear require providerStorageSafe.
     * A live deep pad hold makes that false; clear only AFTER full restoration. */
    if(s->record_staged) {
        if(s->wake->clear(s->wake->context)!=RISC_RETAINED_WAKE_OK)return desk_retain(s);
        s->record_staged=false;
    }
    return 1;
}
static int desk_refuse(x4_desk_sleep *s) { return desk_resume(s)==1?0:-2; }
/* Status is copied only while the alarm service outcome is still confirmed.
 * Output uncertainty is terminal, including uncertainty discovered by step. */
static int desk_alarm_state(x4_desk_sleep *s,int32_t rc) {
    if(rc==ALARM_OUTPUT)return desk_retain(s);
    alarm_status_v1 status={.struct_size=sizeof(status)};
    int32_t got=s->alarms->status(s->alarms->context,&status);
    if(got==ALARM_OUTPUT || status.output_uncertain)return desk_retain(s);
    if(got!=ALARM_OK || (rc!=ALARM_OK && rc!=ALARM_PENDING))return 0;
    return 1;
}
static int desk_alarm_prepare(x4_desk_sleep *s) {
    alarm_sleep_v1 decision={.struct_size=sizeof(decision)};
    int32_t ready=ALARM_PENDING;
    risc_runtime_health_v1 sampled={.struct_size=sizeof(sampled)};
    for(unsigned n=0;n<64 && ready==ALARM_PENDING;++n) {
        if(desk_cancelled(s))return s->retained?-2:0;
        if(!s->rt->health(&sampled))return 0;
        decision=(alarm_sleep_v1){.struct_size=sizeof(decision)};
        ready=s->alarms->prepare_sleep(s->alarms->context,&decision);
        /* No further alarm-service/writer calls after OK. Only reversible
         * peripheral work and clock/key reads follow on the serialized owner. */
        if(ready==ALARM_OK)break;
        int checked=desk_alarm_state(s,ready);
        if(checked!=1)return checked;
        if(ready==ALARM_PENDING) {
            checked=desk_alarm_state(s,s->alarms->step(s->alarms->context));
            if(checked!=1)return checked;
        }
    }
    if(ready!=ALARM_OK)return 0;
    if(decision.deadline && decision.deadline<=decision.rtc_seconds)return 0;
    /* Timestamp precedes the final reconciliation: any service-call latency
     * is charged to the alarm bound, never added back as a fresh deadline. */
    s->alarm_sampled_at=sampled.uptime_ms;
    s->alarm_duration_ms=RISC_TIMED_SLEEP_MAX_MS;
    if(decision.deadline) {
        /* Whole seconds do not expose the fractional clock sample. Subtract
         * one second conservatively, then subtract preparation/staging age. */
        uint32_t seconds=decision.deadline-decision.rtc_seconds;
        s->alarm_duration_ms=seconds>RISC_TIMED_SLEEP_MAX_MS/1000u?RISC_TIMED_SLEEP_MAX_MS:
            seconds>1u?(seconds-1u)*1000u:1u;
    }
    return 1;
}
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
/* Timer reconstruction never starts optional providers in order to stop them.
 * The already-borrowed alarm service, existing panel and owned key suffice. */
static int desk_prepare_timer(void *context) {
    x4_desk_sleep *s=context;
    int alarm=desk_alarm_prepare(s);
    if(alarm!=1)return alarm==-2?-2:desk_refuse(s);
    if(desk_cancelled(s))return desk_refuse(s);
    s->dark=true;
    if(!s->display->set_brightness(s->display->context,0,100))return desk_refuse(s);
    if(desk_cancelled(s))return desk_refuse(s);
    s->panel_touched=true;
    int32_t rc=s->panel->prepare(s->display->context,RISC_DISPLAY_POWER_MAX_BUDGET_MS);
    if(rc==RISC_DISPLAY_POWER_RETAINED || rc>0 || rc<RISC_DISPLAY_POWER_PLATFORM)return desk_retain(s);
    if(rc!=RISC_DISPLAY_POWER_OK || desk_cancelled(s))return desk_refuse(s);
    return 1;
}
#endif
static int desk_prepare(void *context) {
    x4_desk_sleep *s=context;
    if(desk_cancelled(s))return desk_refuse(s);
    /* No saved preference is written or automatically restored here. A false
     * checked Wi-Fi drain retains native custody even when link status is DOWN. */
    if(!s->wifi->disconnect_checked(s->wifi->context))return desk_retain(s);
    if(s->wifi->status(s->wifi->context)!=WIFI_LINK_DOWN)return desk_retain(s);
    if(desk_cancelled(s))return desk_refuse(s);
    if(!s->bluetooth->set_enabled(s->bluetooth->context,false))return desk_retain(s);
    uint8_t bluetooth=PORTABLE_BLUETOOTH_RETAINED;
    if(!s->bluetooth->status(s->bluetooth->context,&bluetooth) ||
       bluetooth!=PORTABLE_BLUETOOTH_OFF)return desk_retain(s);
    portable_desk_clock_radios_off();
    int alarm=desk_alarm_prepare(s);
    if(alarm!=1)return alarm==-2?-2:desk_refuse(s);
    if(desk_cancelled(s))return desk_refuse(s);
    s->dark=true;
    if(!s->display->set_brightness(s->display->context,0,100))return desk_refuse(s);
    if(desk_cancelled(s))return desk_refuse(s);
    s->touch_touched=true;
    int32_t rc=s->touch->prepare(s->touch->base.context,RISC_TOUCH_POWER_MAX_BUDGET_MS);
    if(rc==RISC_TOUCH_POWER_RETAINED || rc>0 || rc<RISC_TOUCH_POWER_INVALID)return desk_retain(s);
    if(rc!=RISC_TOUCH_POWER_OK || desk_cancelled(s))return desk_refuse(s);
    s->panel_touched=true;
    rc=s->panel->prepare(s->display->context,RISC_DISPLAY_POWER_MAX_BUDGET_MS);
    if(rc==RISC_DISPLAY_POWER_RETAINED || rc>0 || rc<RISC_DISPLAY_POWER_PLATFORM)return desk_retain(s);
    if(rc!=RISC_DISPLAY_POWER_OK || desk_cancelled(s))return desk_refuse(s);
    s->storage_touched=true;
    if(!s->storage->prepare_sleep(s->storage->terminal.power.volume.base.context) ||
       desk_cancelled(s))return desk_refuse(s);
    if(!s->storage->commit_sleep(s->storage->terminal.power.volume.base.context) ||
       desk_cancelled(s))return desk_refuse(s);
    return 1;
}
static int desk_stage(void *context,const portable_desk_record *record) {
    x4_desk_sleep *s=context;
    if(s->retained)return -2;
    if(s->panel_touched || s->touch_touched || s->storage_touched)return desk_retain(s);
    risc_retained_wake_record_v1 value={.struct_size=sizeof(value),
        .type=PORTABLE_DESK_CLOCK_RECORD_TYPE,.schema_version=PORTABLE_DESK_CLOCK_RECORD_SCHEMA,
        .size=PORTABLE_DESK_CLOCK_RECORD_BYTES};
    if(!portable_desk_encode(record,value.payload,value.size))return desk_refuse(s);
    /* A confirmed completed image is staged while no provider pad holds exist.
     * Nothing is committed to RTC until the native terminal-entry boundary. */
    s->record_staged=true;
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
    int32_t rc=s->wake->stage(s->wake->context,&value);
    if(rc!=RISC_RETAINED_WAKE_OK) {
        /* INVALID is a clean argument refusal. No other non-OK stage outcome
         * proves storage-safe custody, so do not even attempt clear/release. */
        if(rc!=RISC_RETAINED_WAKE_INVALID)return desk_retain(s);
        return desk_refuse(s);
    }
#else
    if(s->wake->stage(s->wake->context,&value)!=RISC_RETAINED_WAKE_OK)return desk_refuse(s);
#endif
    s->staged_value=value;return 1;
}
static int desk_stage_enter(void *context,const portable_desk_record *record,
                            uint32_t duration,uint32_t sampled_at) {
    x4_desk_sleep *s=context;
    if(s->retained)return -2;
    if(!duration || duration>RISC_TIMED_SLEEP_MAX_MS || desk_cancelled(s))return s->retained?-2:0;
    uint8_t encoded[PORTABLE_DESK_CLOCK_RECORD_BYTES];
    if(!s->record_staged || !portable_desk_encode(record,encoded,sizeof(encoded)) ||
       memcmp(encoded,s->staged_value.payload,sizeof(encoded)))return 0;
    risc_runtime_health_v1 now={.struct_size=sizeof(now)};
    if(!s->rt->health(&now))return 0;
    uint32_t elapsed=now.uptime_ms-sampled_at;
    uint32_t alarm_elapsed=now.uptime_ms-s->alarm_sampled_at;
    uint32_t alarm_ms=s->alarm_duration_ms;
    if(elapsed>=duration || alarm_elapsed>=alarm_ms)return 0;
    duration-=elapsed;alarm_ms-=alarm_elapsed;
    if(alarm_ms<duration)duration=alarm_ms;
    /* Entry's owned GPIO3 check consumes established neutrality. Do not call
     * Runtime stage/clear with prepared resources: the storage gate is closed. */
    int32_t rc=s->power->deep_sleep_for(s->power->power.context,duration);
    if(rc==RISC_DEEP_SLEEP_RETAINED || rc>=0 || rc<RISC_DEEP_SLEEP_UNSUPPORTED)
        return desk_retain(s);
    return 0; /* Shared caller restores; desk_resume then clears pending bytes. */
}
static int desk_neutral(x4_desk_sleep *s) {
    risc_runtime_health_v1 health={.struct_size=sizeof(health)};
    if(!s->rt->health(&health))return 0;
    uint32_t start=health.uptime_ms,previous=start;
    for(unsigned n=0;n<80;++n) {
        bool down=true;
        if(!s->power->power.read_key(s->power->power.context,&down))return desk_retain(s);
        if(!s->rt->health(&health))return 0;
        if((uint32_t)(health.uptime_ms-previous)>400u)return 0;
        previous=health.uptime_ms;
        if(down)start=health.uptime_ms;
        else if((uint32_t)(health.uptime_ms-start)>=30u)return 1;
        s->rt->yield_ms(5);
    }
    return 0;
}
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
static int desk_timer_sleep(const risc_runtime_api_v1 *rt,const risc_display_output_api_v1 *display,
                            const alarm_service_v1 *alarms) {
    if(!display || display->api_version!=RISC_DISPLAY_OUTPUT_API_V1 ||
       display->struct_size<sizeof(*display) || !display->set_brightness ||
       !alarms || alarms->api_version!=ALARM_SERVICE_API_V1 ||
       alarms->struct_size<sizeof(*alarms) || !alarms->prepare_sleep || !alarms->step || !alarms->status) {
        portable_desk_clock_refused();return 0;
    }
    const char *names[]={X4_POWER_CAPABILITY,RISC_DISPLAY_OUTPUT_CAPABILITY,RISC_RETAINED_WAKE_CAPABILITY};
    const uint64_t instances[]={17,3,0};
    risc_runtime_capability_v1 grants[3]={0};unsigned acquired=0;
    x4_desk_sleep state={.rt=rt,.display=display,.alarms=alarms,.timer_only=true};
    bool ran=false;int result=0;
    for(;acquired<3;++acquired) {
        grants[acquired].struct_size=sizeof(grants[acquired]);
        /* False can conceal retained failed-start cleanup even if the output
         * is empty. Preserve earlier grants and do no further provider I/O. */
        if(!rt->acquire(names[acquired],1,instances[acquired],&grants[acquired]))return -2;
    }
    state.power=x4_power_deep(grants[0].api);
    state.panel=risc_display_output_power(grants[1].api);
    state.wake=grants[2].api;
    if(!state.power || !state.power->power.read_key || !state.panel ||
       grants[1].api!=display || !desk_wake_valid(state.wake))goto release;
    result=desk_neutral(&state);
    if(result==-2)return -2;
    if(result!=1)goto release;
    const portable_desk_sleep_ops ops={.context=&state,.cancelled=desk_cancelled,
        .stage=desk_stage,.prepare=desk_prepare_timer,.resume=desk_resume,.stage_enter=desk_stage_enter};
    ran=true;result=portable_desk_clock_run(rt,&ops);
    if(result==-2 || state.retained)return -2;
    if(desk_resume(&state)!=1)return -2;
    result=0;
release:
    while(acquired)if(!rt->release(&grants[--acquired]))return -2;
    if(!ran)portable_desk_clock_refused();
    return result;
}
#endif
int portable_app_alarm_sleep(const risc_runtime_api_v1 *rt,const risc_display_output_api_v1 *display,
                            const risc_battery_gauge_api_v1 *gauge,const alarm_service_v1 *alarms) {
    if(!desk_runtime_valid(rt) || !rt->health || !rt->yield_ms)return 0;
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
    /* Pure adapter state, set only after validated timer-record admission. */
    if(portable_desk_adapter_timer_only())return desk_timer_sleep(rt,display,alarms);
#endif
    risc_runtime_capability_v1 prefs={.struct_size=sizeof(prefs)};
    unsigned mode=PORTABLE_SLEEP_LIGHT;
    if(rt->acquire("storage.key-value",1,PORTABLE_SLEEP_STORE_INSTANCE,&prefs)) {
        (void)portable_sleep_load_profile(prefs.api,PORTABLE_SLEEP_MASK_LIGHT|PORTABLE_SLEEP_MASK_DEEP,
                                         PORTABLE_SLEEP_LIGHT,&mode);
        if(!rt->release(&prefs))return -2;
    }
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
    else return -2; /* Unobservable failed-start cleanup is not a clean miss. */
#endif
    if(mode!=PORTABLE_SLEEP_DEEP)return portable_x4_light_sleep(rt,display,gauge,alarms);
    if(!display || display->api_version!=RISC_DISPLAY_OUTPUT_API_V1 ||
       display->struct_size<sizeof(*display) || !display->set_brightness ||
       !alarms || alarms->api_version!=ALARM_SERVICE_API_V1 ||
       alarms->struct_size<sizeof(*alarms) || !alarms->prepare_sleep || !alarms->step || !alarms->status) {
        portable_desk_clock_refused();return 0;
    }
    const char *names[]={X4_POWER_CAPABILITY,RISC_DISPLAY_OUTPUT_CAPABILITY,
        "input.touch.raw","storage.volume",RISC_RETAINED_WAKE_CAPABILITY,"net.wifi","bluetooth.hci"};
    const uint64_t instances[]={17,0,0,0,0,15,16};
    risc_runtime_capability_v1 grants[7]={0};unsigned acquired=0;
    x4_desk_sleep state={.rt=rt,.display=display,.alarms=alarms};int result=0;bool ran=false;
    for(;acquired<7;++acquired) {
        grants[acquired].struct_size=sizeof(grants[acquired]);
#ifdef PORTABLE_DESK_CLOCK_SPARSE_START
        if(!rt->acquire(names[acquired],1,instances[acquired],&grants[acquired]))return -2;
#else
        if(!rt->acquire(names[acquired],1,instances[acquired],&grants[acquired]))goto release;
#endif
    }
    state.power=x4_power_deep(grants[0].api);
    state.panel=risc_display_output_power(grants[1].api);
    state.touch=risc_touch_power(grants[2].api);
    state.storage=risc_storage_volume_sleep(grants[3].api);
    state.wake=grants[4].api;state.wifi=grants[5].api;state.bluetooth=grants[6].api;
    if(!state.power || !state.power->power.read_key || !state.panel || grants[1].api!=display ||
       !state.touch || !state.storage || !desk_wake_valid(state.wake) ||
       !state.wifi || state.wifi->api_version!=WIFI_API_V1 ||
       state.wifi->struct_size<WIFI_MANAGEMENT_V1_SIZE || !state.wifi->disconnect_checked || !state.wifi->status ||
       !state.bluetooth || state.bluetooth->api_version!=1 ||
       state.bluetooth->struct_size<sizeof(*state.bluetooth) ||
       !state.bluetooth->set_enabled || !state.bluetooth->status)goto release;
    result=desk_neutral(&state);
    if(result==-2)return -2;
    if(result!=1)goto release;
    const portable_desk_sleep_ops ops={.context=&state,.cancelled=desk_cancelled,
        .stage=desk_stage,.prepare=desk_prepare,.resume=desk_resume,.stage_enter=desk_stage_enter};
    ran=true;result=portable_desk_clock_run(rt,&ops);
    if(result==-2 || state.retained)return -2;
    /* Defense against a returning caller which failed to finish rollback. */
    if(desk_resume(&state)!=1)return -2;
    result=0;
release:
    while(acquired)if(!rt->release(&grants[--acquired]))return -2;
    if(!ran)portable_desk_clock_refused();
    return result;
}
#endif
