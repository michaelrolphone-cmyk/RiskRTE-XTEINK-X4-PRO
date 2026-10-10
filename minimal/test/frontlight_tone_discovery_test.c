#include <RiscFrontlightToneV1.h>
#include <RiscDisplayOutputFrontlightV1.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(risc_frontlight_api_v1_tone, base)==0, "frontlight prefix");
_Static_assert(offsetof(risc_frontlight_api_v1_tone, tone_tag)==sizeof(risc_frontlight_api_v1), "frontlight suffix");
_Static_assert(offsetof(risc_display_output_api_v1_frontlight, snapshot)==0, "snapshot prefix");
_Static_assert(offsetof(risc_display_output_api_v1_frontlight, frontlight_tag)==sizeof(risc_display_output_api_v1_snapshot), "display suffix");
_Static_assert(offsetof(risc_display_output_api_v1_snapshot, metrics)==0, "metrics prefix");
_Static_assert(offsetof(risc_display_output_api_v1_metrics, power)==0, "power prefix");
_Static_assert(offsetof(risc_display_output_api_v1_power, history)==0, "history prefix");
_Static_assert(offsetof(risc_display_output_api_v1_history, base)==0, "base prefix");

static bool set(void *c,uint16_t a,uint16_t b) {(void)c;(void)a;(void)b;assert(!"discovery calls callbacks");return false;}
static bool get(void *c,uint16_t *a,uint16_t *b) {(void)c;(void)a;(void)b;assert(!"discovery calls callbacks");return false;}
static int32_t display_get(void *c,uint16_t *a,uint16_t *b) {(void)c;(void)a;(void)b;assert(!"discovery calls callbacks");return -1;}
static bool seed(void *c,risc_display_frame_v1 f) {(void)c;(void)f;assert(!"discovery calls callbacks");return false;}
static int32_t power(void *c,uint32_t t) {(void)c;(void)t;assert(!"discovery calls callbacks");return -1;}
static bool metrics(void *c,risc_display_present_metrics_v1 *v) {(void)c;(void)v;assert(!"discovery calls callbacks");return false;}
static bool copy(void *c,uint32_t f,void *p,size_t n,uint32_t s) {(void)c;(void)f;(void)p;(void)n;(void)s;assert(!"discovery calls callbacks");return false;}

int main(void) {
    assert(!risc_frontlight_tone(NULL) && !risc_display_output_frontlight(NULL));
    /* Exact short allocations plus sanitizers detect any suffix read before
     * the complete size check, including every partially appended suffix. */
    for(size_t size=sizeof(uint32_t)*2;size<sizeof(risc_frontlight_api_v1_tone);++size) {
        risc_frontlight_api_v1 *old=calloc(1,size); assert(old);
        old->api_version=1;old->struct_size=(uint32_t)size;
        assert(!risc_frontlight_tone(old));free(old);
    }
    for(size_t size=sizeof(uint32_t)*2;size<sizeof(risc_display_output_api_v1_frontlight);++size) {
        risc_display_output_api_v1 *old=calloc(1,size); assert(old);
        old->api_version=1;old->struct_size=(uint32_t)size;
        assert(!risc_display_output_frontlight(old));free(old);
    }
    risc_frontlight_api_v1_tone light={{1,sizeof(light),NULL,set,get},RISC_FRONTLIGHT_TONE_TAG,RISC_FRONTLIGHT_TONE_VERSION,set,get};
    assert(risc_frontlight_tone(&light.base)==&light);
    risc_frontlight_api_v1_tone bad_light;
#define BAD_LIGHT(field,value) do {bad_light=light;bad_light.field=value;assert(!risc_frontlight_tone(&bad_light.base));} while(0)
    BAD_LIGHT(base.api_version,2);BAD_LIGHT(tone_tag,0);BAD_LIGHT(tone_version,2);
    BAD_LIGHT(set_tone,NULL);BAD_LIGHT(get_tone,NULL);
    light.base.struct_size++;assert(risc_frontlight_tone(&light.base)==&light);
    risc_display_output_api_v1_frontlight display={0};
    risc_display_output_api_v1 *base=&display.snapshot.metrics.power.history.base;
    base->api_version=1;base->struct_size=sizeof(display);
    display.snapshot.metrics.power.history.extension_tag=RISC_DISPLAY_HISTORY_TAG;
    display.snapshot.metrics.power.history.extension_version=1;
    display.snapshot.metrics.power.history.seed_previous=seed;
    display.snapshot.metrics.power.power_tag=RISC_DISPLAY_POWER_TAG;
    display.snapshot.metrics.power.power_version=1;
    display.snapshot.metrics.power.prepare=power;display.snapshot.metrics.power.resume=power;
    display.snapshot.metrics.metrics_tag=RISC_DISPLAY_METRICS_TAG;
    display.snapshot.metrics.metrics_version=1;display.snapshot.metrics.snapshot=metrics;
    display.snapshot.snapshot_tag=RISC_DISPLAY_SNAPSHOT_TAG;
    display.snapshot.snapshot_version=1;display.snapshot.copy_completed=copy;
    display.frontlight_tag=RISC_DISPLAY_FRONTLIGHT_TAG;display.frontlight_version=1;
    display.set_tone=set;display.get_tone=display_get;
    assert(risc_display_output_frontlight(base)==&display);
    assert(risc_display_output_snapshot(base)==&display.snapshot);
    assert(risc_display_output_metrics(base)==&display.snapshot.metrics);
    assert(risc_display_output_power(base)==&display.snapshot.metrics.power);
    assert(risc_display_output_history(base)==&display.snapshot.metrics.power.history);
    risc_display_output_api_v1_frontlight bad;
#define BAD_DISPLAY(field,value) do {bad=display;bad.field=value;assert(!risc_display_output_frontlight(&bad.snapshot.metrics.power.history.base));} while(0)
    BAD_DISPLAY(snapshot.metrics.power.history.base.api_version,2);
    BAD_DISPLAY(snapshot.metrics.power.history.extension_tag,0);
    BAD_DISPLAY(snapshot.metrics.power.history.extension_version,2);
    BAD_DISPLAY(snapshot.metrics.power.history.seed_previous,NULL);
    BAD_DISPLAY(snapshot.metrics.power.power_tag,0);BAD_DISPLAY(snapshot.metrics.power.power_version,2);
    BAD_DISPLAY(snapshot.metrics.power.prepare,NULL);BAD_DISPLAY(snapshot.metrics.power.resume,NULL);
    BAD_DISPLAY(snapshot.metrics.metrics_tag,0);BAD_DISPLAY(snapshot.metrics.metrics_version,2);
    BAD_DISPLAY(snapshot.metrics.snapshot,NULL);
    BAD_DISPLAY(snapshot.snapshot_tag,0);BAD_DISPLAY(snapshot.snapshot_version,2);BAD_DISPLAY(snapshot.copy_completed,NULL);
    BAD_DISPLAY(frontlight_tag,0);BAD_DISPLAY(frontlight_version,2);BAD_DISPLAY(set_tone,NULL);BAD_DISPLAY(get_tone,NULL);
    base->struct_size++;assert(risc_display_output_frontlight(base)==&display);
    puts("Frontlight tone discovery: exact legacy/partial allocations, full-size-before-suffix, malformed nested tags/callbacks and prefix layout PASS");
}
