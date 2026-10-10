/* Link the actual X4 GT911 provider to the production HID/adapter fixture.
 * Reuse the strict scoped GPIO/I2C/sync fixture; only physical reports and
 * monotonic time come from the HID scenario. No subscription stub is used. */
#define main x4_gt911_fixture_main
#include "gt911_test.c"
#undef main
void hid_renderer_watch_report(risc_touch_snapshot_v1 *sample);
uint64_t hid_renderer_watch_millis(void);
static bool hid_transact(void *c,uint64_t id,const uint8_t *w,size_t wn,
                         uint8_t *r,size_t rn,uint32_t timeout) {
    if(wn==2 && w[0]==0x81 && w[1]==0x4e) {
        risc_touch_snapshot_v1 value={0};hid_renderer_watch_report(&value);
        assert(value.width==480 && value.height==800);
        packet(value.contact_count,value.contacts[0].x,value.contacts[0].y,
               (value.buttons&RISC_TOUCH_BUTTON_PRIMARY)!=0);
        assert(value.contact_count<=5);
        for(unsigned i=0;i<value.contact_count;++i) {
            assert(value.contacts[i].id>=1 && value.contacts[i].id<=16);
            wire_point(i,(uint8_t)(value.contacts[i].id-1u),value.contacts[i].x,value.contacts[i].y);
        }
    }
    return transact(c,id,w,wn,r,rn,timeout);
}
static uint64_t hid_now(void *c){(void)c;return hid_renderer_watch_millis();}
const risc_touch_api_v1 *hid_watch_touch_start(void) {
    driver=t5_driver_get(2);assert(driver);api=driver->capability;
    power=risc_touch_power(api);assert(power);
    bus.base.transact=hid_transact;clock_api.monotonic_ms=hid_now;
    assert(start());return api;
}
void hid_watch_touch_stop(void){done(0);}
