/* Complete ordinary provider exercised through a scoped-token GPIO model. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../drivers/x4pro_panel/driver.c"

enum { IDLE, REFRESH, PON, POF, RAM };
static struct { uint64_t token; bool output, pullup, level, held; } pads[49];
static uint64_t fake_now = 100, next_pin_token = 10, admission_cost;
static bool owner = true, lock_exists, lock_held, unlock_ok = true, destroy_ok = true;
static bool light_ok=true;static uint16_t light_level,light_max;
static bool fake_light(void*c,uint16_t n,uint16_t max){(void)c;light_level=n;light_max=max;return light_ok;}
static bool probe_pullup=true;
static bool power_ok = true, fail_claim, fail_read, fail_write, fail_release, fail_hold;
static bool fail_unhold, hold_retained, frozen_clock, stuck_reset;
static bool stuck_refresh, absent_busy, stuck_poweroff, scoped_bus = true, ambiguous, unstable_probe;
static bool rollback_clock, bad_clock, reenter;
static bool async_model;
static uint64_t phase_until;
static unsigned async_calls;
static uint64_t wire_hash=UINT64_C(1469598103934665603);
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
static bool retire_ok = true;
static unsigned retired_outputs;
#endif
static unsigned chip = PROBE_SSD, phase, claims, releases, writes, pin_reads, holds;
static unsigned clock_reads;
static unsigned refreshes, poweroffs, deep_sleeps, command_count[256], data_count, probe_reads, charge_every;
static uint8_t shift, bits, current_command, registers[256][9], old_pixel, new_pixel;
static unsigned read_index, plane_bytes[256];
static const risc_display_output_api_v1 *display;

static bool fake_owner(void *c) { (void)c; return owner; }
static bool fake_create(void *c, uint64_t *out) {
    (void)c; *out = 0; if (!owner || lock_exists) return false;
    lock_exists = true; *out = 7; return true;
}
static bool fake_lock(void *c, uint64_t token) {
    (void)c; assert(token == 7);
    if (!owner || !lock_exists || lock_held) return false;
    lock_held = true; fake_now += admission_cost; return true;
}
static bool fake_unlock(void *c, uint64_t token) {
    (void)c; assert(token == 7); if (!owner || !lock_held || !unlock_ok) return false;
    lock_held = false; return true;
}
static bool fake_destroy(void *c, uint64_t token) {
    (void)c; assert(token == 7); if (!owner || lock_held || !lock_exists || !destroy_ok) return false;
    lock_exists = false; return true;
}
static uint64_t fake_time(void *c) {
    (void)c; ++clock_reads; if (bad_clock) return UINT64_MAX;
    if (async_model && fake_now >= phase_until &&
        (phase == PON || (phase == REFRESH && !stuck_refresh))) phase = IDLE;
    if (rollback_clock) return --fake_now;
    return fake_now;
}
static void fake_sleep(void *c, uint32_t ms) {
    (void)c; if (!frozen_clock) fake_now += ms;
    if (reenter && display) {
        risc_display_surface_v1 s = {0};
        assert(!display->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &s));
        assert(!t5_driver_get(2)->quiesce());
    }
    if (phase == PON || (phase == REFRESH && !stuck_refresh) || (phase == POF && !stuck_poweroff)) phase = IDLE;
}
static bool fake_ready(void *c) { (void)c; return power_ok; }
static unsigned find_pin(uint64_t token) {
    for (unsigned p = 0; p < 49; ++p) if (token && pads[p].token == token) return p;
    assert(!"foreign or stale pin token"); return 0;
}
static bool fake_claim(void *c, uint8_t pin, bool output, bool level, bool pullup, uint64_t *out) {
    (void)c; *out = 0; ++claims; assert(owner && lock_held);
    assert(pin == 6 || pin == 11 || pin == 12 || pin == 13 || pin == 14 || pin == 18);
    if ((!probe_pullup && pullup) || fail_claim || (!scoped_bus && (pin == 11 || pin == 12 || pin == 13))) return false;
    assert(!pads[pin].token && !(output && pullup));
    pads[pin].token = next_pin_token++; pads[pin].output = output;
    pads[pin].pullup = pullup; pads[pin].level = level; pads[pin].held = false; *out = pads[pin].token;
    if (pin == 11 && !output) assert(pullup);
    return true;
}
static void model_command(uint8_t cmd) {
    current_command = cmd; ++command_count[cmd]; data_count = 0; read_index = 0;
    if (cmd == 0x71) ++probe_reads;
    if ((chip == PROBE_SSD && cmd == 0x20 && registers[0x22][0] == 0x03) || (chip == PROBE_UC8279 && cmd == 0x02)) {
        ++poweroffs; phase = POF;
    } else if ((chip == PROBE_SSD && cmd == 0x20) || (chip == PROBE_UC8279 && cmd == 0x12)) {
        ++refreshes; phase = absent_busy ? IDLE : REFRESH;
    }
    if (chip == PROBE_UC8279 && cmd == 0x04) phase = PON;
    if (phase == PON || phase == REFRESH) phase_until = fake_now + 3u;
    if (chip == PROBE_UC8279 && cmd == 0x00) assert(phase != PON);
    if ((chip == PROBE_UC8279 && cmd == 0x07) || (chip == PROBE_SSD && cmd == 0x10)) ++deep_sleeps;
}
static void model_data(uint8_t value) {
    if (data_count < 9) registers[current_command][data_count] = value;
    if (chip == PROBE_SSD && data_count == 0) {
        if (current_command == 0x24) new_pixel = value;
        if (current_command == 0x26) old_pixel = value;
    }
    if (chip == PROBE_UC8279 && data_count == 12000u) {
        if (current_command == 0x13) new_pixel = value;
        if (current_command == 0x10) old_pixel = value;
    }
    if (chip == PROBE_UC8279 && (current_command == 0x13 || current_command == 0x10) && data_count < 12000) assert(value == 0xFF);
    ++data_count; ++plane_bytes[current_command];
}
static bool fake_write(void *c, uint64_t token, bool level) {
    (void)c; unsigned pin = find_pin(token); ++writes;
    assert(owner && lock_held && pads[pin].output && !pads[pin].held);
    if (charge_every && writes % charge_every == 0) ++fake_now;
    if (fail_write) return false;
    wire_hash=(wire_hash ^ (uint64_t)(pin*2u+(level?1u:0u)))*UINT64_C(1099511628211);
    if (pin == 13 && !level && pads[pin].level) { shift = bits = 0; }
    if (pin == 12 && level && !pads[12].level && !pads[13].level && pads[11].output) {
        shift = (uint8_t)((shift << 1) | pads[11].level);
        if (++bits == 8) {
            if (pads[18].level) model_data(shift); else model_command(shift);
            bits = shift = 0;
        }
    }
    if (pin == 14 && level && !pads[pin].level && !stuck_reset) phase = IDLE;
    pads[pin].level = level; return true;
}
static bool fake_read(void *c, uint64_t token, bool *out) {
    (void)c; unsigned pin = find_pin(token); ++pin_reads; assert(owner && lock_held);
    if (fail_read) return false;
    if (pin == 6) { *out = chip == PROBE_SSD ? phase != IDLE : phase == IDLE; return true; }
    if (pin == 11 && !pads[pin].output) {
        uint8_t value = 0xFF;
        if (chip == PROBE_UC8279 || ambiguous) {
            const uint8_t ver[] = {1,2,0x68,4,5};
            value = current_command == 0x71 ? 0x13 : ver[(read_index / 8u) % 5u];
            if (ambiguous && current_command == 0x70 && read_index / 8 == 2) value = 0x67;
            if (unstable_probe && probe_reads > 1 && current_command == 0x70) value ^= 0x10;
        }
        *out = (value >> (7u - (read_index++ % 8u))) & 1u; return true;
    }
    *out = pads[pin].level; return true;
}
static bool fake_release(void *c, uint64_t token) {
    (void)c; unsigned pin = find_pin(token); ++releases;
    if (fail_release || pads[pin].held) return false;
    memset(&pads[pin], 0, sizeof(pads[pin])); return true;
}
static int32_t fake_hold(void *c, uint64_t token, bool enable) {
    (void)c; unsigned pin = find_pin(token); ++holds;
    assert(pin == 14 && owner && lock_held && pads[pin].output && pads[pin].level);
    if (hold_retained || (!enable && fail_unhold)) return RISC_DEEP_SLEEP_RETAINED;
    if (fail_hold) return RISC_DEEP_SLEEP_PLATFORM;
    pads[pin].held = enable; return 0;
}
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
static bool fake_retire(void *c, uint64_t token) {
    (void)c; const unsigned pin = find_pin(token);
    assert(owner && lock_held && pin == 14 && pads[pin].output && pads[pin].level && pads[pin].held);
    if (!retire_ok) return false;
    pads[pin].token = 0; ++retired_outputs; return true;
}
#endif
static risc_display_present_metrics_v1 snapshot_metrics(void) {
    const risc_display_output_api_v1_metrics *ext=risc_display_output_metrics(display);
    assert(ext && ext->power.prepare && ext->power.resume && ext->power.history.seed_previous);
    const unsigned w=writes,r=pin_reads,c=clock_reads,cl=claims,re=releases,h=holds;
    const uint64_t now=fake_now;const bool locked=lock_held;
    risc_display_present_metrics_v1 out={.api_version=1,.struct_size=sizeof(out)};
    assert(ext->snapshot(NULL,&out));
    assert(w==writes && r==pin_reads && c==clock_reads && cl==claims && re==releases && h==holds);
    assert(now==fake_now && locked==lock_held);
    return out;
}
static void queue(risc_display_surface_v1 *surface, uint64_t *token) {
    assert(display->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, surface));
    assert(surface->width == 800 && surface->height == 480 && surface->stride_bytes == 100 && surface->size_bytes == 48000);
    risc_display_surface_v1 other = {0};
    display->release(NULL, surface->frame + 1);
    assert(!display->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &other));
    assert(!display->submit(NULL, surface->frame + 1, NULL, 0, NULL, token));
    const risc_display_present_options_v1 invalid = {RISC_DISPLAY_PRESENT_CLEAN, RISC_DISPLAY_QUEUE_MAILBOX, 0};
    assert(!display->submit(NULL, surface->frame, NULL, 0, &invalid, token));
    assert(display->submit(NULL, surface->frame, NULL, 0, NULL, token));
}
static void complete(uint64_t token) {
    risc_display_present_status_v1 status = {0};
    if (async_model) {
        for (unsigned n = 0; n < 11000u; ++n) {
            const unsigned before = writes;
            const uint32_t old_bytes = present_state == PRESENT_QUEUED ? 0u : bytes_sent;
            const risc_driver_poll_v2 *d = (const risc_driver_poll_v2*)t5_driver_get(2);
            assert(d->streams.driver.struct_size == sizeof(*d) && !d->streams.bind_streams);
            d->poll(8); ++async_calls;
            assert(!lock_held && writes-before <= 512u*24u+1000u);
            assert(bytes_sent-old_bytes <= 512u);
            assert(display->present_status(NULL,token,&status));
            if (status.state == RISC_DISPLAY_PRESENT_COMPLETE || status.state == RISC_DISPLAY_PRESENT_FAILED) break;
            assert(!t5_driver_get(2)->quiesce());
            const risc_display_output_api_v1_power *power = risc_display_output_power(display);
            assert(power && power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_BUSY);
            ++fake_now; /* Host scheduler/input opportunity between each slice. */
        }
    } else assert(display->wait_present(NULL, token, 20000, &status));
    assert(status.state == RISC_DISPLAY_PRESENT_COMPLETE);
}
static void test_metrics(const risc_driver_v2 *driver,uint64_t token) {
    const risc_display_output_api_v1_metrics *ext=risc_display_output_metrics(display);
    risc_display_output_api_v1_metrics legacy=api;
    legacy.power.history.base.struct_size=sizeof(risc_display_output_api_v1_power);
    assert(risc_display_output_power(&legacy.power.history.base) && !risc_display_output_metrics(&legacy.power.history.base));
    legacy=api;legacy.metrics_tag^=1;assert(!risc_display_output_metrics(&legacy.power.history.base));
    legacy=api;legacy.metrics_version=2;assert(!risc_display_output_metrics(&legacy.power.history.base));
    legacy=api;legacy.snapshot=NULL;assert(!risc_display_output_metrics(&legacy.power.history.base));
    risc_display_present_metrics_v1 out=snapshot_metrics();
    assert(out.token==token && out.state==PRESENT_QUEUED && !out.bytes_sent && !out.gpio_write_calls);
    assert(out.valid_times==RISC_DISPLAY_METRICS_QUEUED && out.mode==RISC_DISPLAY_METRICS_FULL && !out.damage_count);
    assert(out.effective_update.width==800 && out.effective_update.height==480);
    risc_display_present_metrics_v1 saved=out;
    owner=false;assert(!ext->snapshot(NULL,&out));owner=true;assert(!memcmp(&out,&saved,sizeof(out)));
    out.api_version=2;saved=out;assert(!ext->snapshot(NULL,&out)&&!memcmp(&out,&saved,sizeof(out)));
    out.api_version=1;out.struct_size=sizeof(out)-1;saved=out;assert(!ext->snapshot(NULL,&out)&&!memcmp(&out,&saved,sizeof(out)));
    assert(!ext->snapshot(NULL,NULL));
    const unsigned before=writes;
    ((const risc_driver_poll_v2 *)driver)->poll(8);
    out=snapshot_metrics();assert(out.state==PRESENT_ACTIVE && out.bytes_sent==512 && out.gpio_write_calls==writes-before);
    assert(out.valid_times==(RISC_DISPLAY_METRICS_QUEUED|RISC_DISPLAY_METRICS_TRANSFER_START));
    complete(token);out=snapshot_metrics();
    assert(out.state==PRESENT_COMPLETE && out.bytes_sent==120000 && out.gpio_write_calls==writes-before);
    assert(out.valid_times==63 && out.queued_ms<=out.transfer_start_ms && out.transfer_start_ms<=out.transfer_end_ms);
    assert(out.transfer_end_ms<out.refresh_ms && out.refresh_ms<=out.busy_assert_ms && out.busy_assert_ms<out.busy_done_ms);
    assert(out.refresh_ms>refresh_ms); /* UC legacy diagnostic is PON; metric is DRF. */
    risc_display_surface_v1 surface={0};assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
    risc_display_rect_v1 damage[2]={{17,20,1,4},{25,22,1,2}};
    const risc_display_rect_v1 original[2]={{17,20,1,4},{25,22,1,2}};
    assert(display->submit(NULL,surface.frame,damage,2,NULL,&token));memset(damage,0,sizeof(damage));
    out=snapshot_metrics();assert(out.mode==RISC_DISPLAY_METRICS_PARTIAL && out.damage_count==2);
    assert(!memcmp(out.submitted_damage,original,sizeof(original)) && !out.bytes_sent && !out.gpio_write_calls);
    assert(out.effective_update.x==16 && out.effective_update.y==20 && out.effective_update.width==16 && out.effective_update.height==4);
    complete(token);
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
    const risc_display_present_options_v1 clean={RISC_DISPLAY_PRESENT_CLEAN,RISC_DISPLAY_QUEUE_FIFO,0};
    assert(display->submit(NULL,surface.frame,original,2,&clean,&token));out=snapshot_metrics();
    assert(out.mode==RISC_DISPLAY_METRICS_FULL && out.damage_count==2 && out.effective_update.width==800 && out.effective_update.height==480);
    complete(token);out=snapshot_metrics();saved=out;
    retained=true;assert(!ext->snapshot(NULL,&out));retained=false;assert(!memcmp(&out,&saved,sizeof(out)));
    assert(ext->power.prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);assert(!ext->snapshot(NULL,&out));
    assert(ext->power.resume(NULL,1500)==RISC_DISPLAY_POWER_OK);out=snapshot_metrics();
    assert(!out.token && !out.state && !out.bytes_sent && !out.gpio_write_calls && !out.valid_times);
    assert(driver->quiesce());assert(!ext->snapshot(NULL,&out));
}
/* Ordinary app damage must use the provider's own completed physical image.
 * No explicit seed call is made until its independent precedence case. */
static void ordinary_history(const risc_driver_v2 *driver,uint64_t token) {
    const bool expected=getenv("X4_EXPECT_HISTORY")?atoi(getenv("X4_EXPECT_HISTORY"))!=0:true;
    const risc_display_rect_v1 damage={0,0,8,1};
    const risc_display_present_options_v1 clean={RISC_DISPLAY_PRESENT_CLEAN,RISC_DISPLAY_QUEUE_FIFO,0};
    risc_display_surface_v1 surface={0};
    complete(token);assert(registers[0xE5][0]==0x1E);
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));((uint8_t*)surface.pixels)[0]=0xF0;
    assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token));assert(partial_update==expected);complete(token);
    assert(registers[0xE5][0]==(expected?0x5A:0x1E));
    unsigned ordinary_mode=registers[0xE5][0];
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));((uint8_t*)surface.pixels)[0]=0x55;display->release(NULL,surface.frame);
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));((uint8_t*)surface.pixels)[0]=0xA5;
    assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token));assert(partial_update==expected);complete(token);
    assert(old_pixel==(expected?0x0F:0xFF));
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));((uint8_t*)surface.pixels)[0]=0xC3;
    assert(display->submit(NULL,surface.frame,&damage,1,&clean,&token));assert(!partial_update);complete(token);
    assert(registers[0xE5][0]==0x1E);
    const risc_display_output_api_v1_history *history=risc_display_output_history(display);assert(history);
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));memset(surface.pixels,0x3C,surface.size_bytes);
    assert(history->seed_previous(NULL,surface.frame));((uint8_t*)surface.pixels)[0]=0x69;
    assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token));assert(partial_update);complete(token);assert(old_pixel==0xC3);
    /* A partial completion must retain the old physical pixels outside damage,
     * even if the caller left unsubmitted data elsewhere in its surface. */
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));((uint8_t*)surface.pixels)[100]=0xF0;
    const risc_display_rect_v1 next_damage={0,1,8,1};
    assert(display->submit(NULL,surface.frame,&next_damage,1,NULL,&token));assert(partial_update==expected);complete(token);
    if(expected)assert(previous_frame[0]==0x69&&previous_frame[100]==0xF0&&previous_frame[101]==0x3C);
    /* A timeout invalidates inferred history before another admission. */
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
    assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token));
    const risc_driver_poll_v2 *d=(const risc_driver_poll_v2*)driver;d->poll(8);fake_now=async_deadline;d->poll(8);assert(present_state==PRESENT_FAILED);
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
    assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token));assert(!partial_update);complete(token);
    const risc_display_output_api_v1_power *power=risc_display_output_power(display);assert(power);
    assert(power->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);assert(power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);
    assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
    assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token));assert(!partial_update);complete(token);
    assert(driver->quiesce());
    printf("ordinary damage refresh=0x%02x; release/seed/clean/failure/resume history passed; total_async_slices=%u\n",ordinary_mode,async_calls);
}
static void assert_sleep_blocks(const risc_display_output_api_v1_power *power, uint64_t old_token) {
    risc_display_surface_v1 other = {0}; risc_display_present_status_v1 status = {0};
    uint64_t token = 99;
    const unsigned before = writes;
    assert(!display->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &other));
    assert(!display->submit(NULL, frame_serial, NULL, 0, NULL, &token) && !token);
    assert(!display->wait_present(NULL, old_token, 100, &status));
    assert(!display->present_status(NULL, old_token, &status));
    assert(!power->history.seed_previous(NULL, frame_serial));
    assert(writes == before);
}
static void test_power(const char *scenario, const risc_driver_v2 *driver) {
    const risc_display_output_api_v1_power *power = risc_display_output_power(display);
    assert(power && risc_display_output_history(display) == &power->history);
    assert(power->resume(NULL, 0) == RISC_DISPLAY_POWER_OK);
    assert(power->prepare(NULL, 0) == RISC_DISPLAY_POWER_BUSY && !poweroffs);
    risc_display_surface_v1 surface = {0}; uint64_t token = 0;
    assert(display->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &surface));
    assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_BUSY && !poweroffs);
    const uint64_t stale_frame = surface.frame;
    memset(surface.pixels, 0x69, surface.size_bytes);
    assert(display->submit(NULL, surface.frame, NULL, 0, NULL, &token));
    assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_BUSY && !poweroffs);
    complete(token);
    const unsigned visible_refreshes = refreshes, original_claims = claims, original_releases = releases;
    if (strstr(scenario, "owner")) {
        const unsigned before = writes; owner = false;
        assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_UNAVAILABLE);
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_UNAVAILABLE);
        owner = true; assert(writes == before);
        lock_held = true; assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_BUSY); lock_held = false;
        admission_cost = 21; assert(power->prepare(NULL, 20) == RISC_DISPLAY_POWER_TIMEOUT && !poweroffs); admission_cost = 0;
    }
    if (strstr(scenario, "clock")) {
        bad_clock = true; assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_PLATFORM); bad_clock = false;
        rollback_clock = true; assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_PLATFORM); rollback_clock = false;
        ++fake_now; assert(!poweroffs);
    }
    if (strstr(scenario, "-sleep-gpio")) {
        fail_write = true; assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED);
        fail_write = false; assert(!driver->quiesce());
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED); return;
    }
    if (strstr(scenario, "read")) {
        fail_read = true; assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED);
        fail_read = false; assert(!driver->quiesce()); return;
    }
    if (strstr(scenario, "unlock")) {
        unlock_ok = false; assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED);
        assert(!driver->quiesce()); return;
    }
    if (strstr(scenario, "pof")) {
        stuck_poweroff = true;
        const uint64_t began = fake_now;
        assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_TIMEOUT && fake_now - began <= 1500u);
        assert(poweroffs == 1 && !deep_sleeps); assert_sleep_blocks(power, token);
        assert(power->prepare(NULL, 20) == RISC_DISPLAY_POWER_TIMEOUT && poweroffs == 1);
        stuck_poweroff = false;
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_OK);
        assert(!reset_held && !shutdown_stage && !previous_seeded && !deep_sleeps);
    }
    if (strstr(scenario, "frozen")) {
        frozen_clock = stuck_poweroff = true;
        assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_TIMEOUT && poweroffs == 1);
        frozen_clock = stuck_poweroff = false;
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_OK);
    }
    if (strstr(scenario, "-sleep-hold")) {
        fail_hold = true;
        if (strstr(scenario, "retained")) hold_retained = true;
        assert(power->prepare(NULL, 1500) == (hold_retained ? RISC_DISPLAY_POWER_RETAINED : RISC_DISPLAY_POWER_PLATFORM));
        assert(poweroffs == 1 && deep_sleeps == 1);
        fail_hold = false;
        if (hold_retained) { assert(!driver->quiesce()); return; }
        if (strstr(scenario, "retry")) {
            assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_OK && poweroffs == 1 && deep_sleeps == 1);
        } else assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_OK);
    }
    assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_OK);
    assert(reset_held && pads[14].held && pads[14].token && lock_exists);
    assert(claims == original_claims && releases == original_releases && refreshes == visible_refreshes);
    const unsigned sent_poweroffs = poweroffs, sent_sleeps = deep_sleeps, before = writes;
    assert(power->prepare(NULL, 0) == RISC_DISPLAY_POWER_OK);
    assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_OK && writes == before);
    assert_sleep_blocks(power, token);
    assert(power->resume(NULL, 0) == RISC_DISPLAY_POWER_BUSY && writes == before);
    if (strstr(scenario, "unhold")) {
        fail_unhold = !strstr(scenario, "platform");
        fail_hold = !fail_unhold;
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED && reset_held);
        fail_unhold = fail_hold = false; assert(!driver->quiesce());
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED); return;
    }
    if (strstr(scenario, "resume-timeout")) {
        assert(power->resume(NULL, 1) == RISC_DISPLAY_POWER_TIMEOUT && shutdown_stage == 5u);
        assert_sleep_blocks(power, token);
        assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_BUSY && !driver->quiesce());
    }
    if (strstr(scenario, "resume-gpio")) {
        fail_write = true; assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_RETAINED);
        fail_write = false; assert(!driver->quiesce()); return;
    }
    if (strstr(scenario, "resume-busy")) {
        stuck_reset = stuck_poweroff = true; phase = POF;
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_TIMEOUT);
        stuck_reset = stuck_poweroff = false; assert_sleep_blocks(power, token);
    }
    if (strstr(scenario, "wire-budget")) {
        charge_every = 1; const uint64_t began = fake_now;
        assert(power->resume(NULL, 200) == RISC_DISPLAY_POWER_TIMEOUT);
        assert(fake_now - began <= 360u); /* Deadline plus one <=6-byte command. */
        charge_every = 0; assert_sleep_blocks(power, token);
    }
    assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_OK);
    assert(!reset_held && !pads[14].held && started && !shutdown_stage && !previous_seeded);
    assert(poweroffs == sent_poweroffs && deep_sleeps == sent_sleeps && refreshes == visible_refreshes);
    const unsigned resumed_writes = writes;
    assert(power->resume(NULL, 0) == RISC_DISPLAY_POWER_OK);
    assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_OK && writes == resumed_writes);
    for (size_t i = 0; i < FRAME_BYTES; ++i) assert(frame[i] == 0x69);
    assert(display->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &surface));
    assert(surface.frame != stale_frame && !power->history.seed_previous(NULL, stale_frame));
    assert(!display->submit(NULL, stale_frame, NULL, 0, NULL, &token));
    assert(power->history.seed_previous(NULL, surface.frame));
    memset(surface.pixels, 0x96, surface.size_bytes);
    const risc_display_rect_v1 damage = {0,0,8,1};
    assert(display->submit(NULL, surface.frame, &damage, 1, NULL, &token)); complete(token);
    assert(old_pixel == (uint8_t)~0x69 && new_pixel == (uint8_t)~0x96);
    assert(power->prepare(NULL, 1500) == RISC_DISPLAY_POWER_OK);
    const unsigned final_pof = poweroffs, final_sleep = deep_sleeps;
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
    if (strstr(scenario, "retire")) {
        retire_ok = false; assert(!driver->quiesce() && reset_held && shutdown_stage == 3u); retire_ok = true;
        assert(poweroffs == final_pof && deep_sleeps == final_sleep);
    }
    if (strstr(scenario, "release")) {
        fail_release = true; assert(!driver->quiesce() && shutdown_stage == 4u); fail_release = false;
        assert(power->resume(NULL, 1500) == RISC_DISPLAY_POWER_UNAVAILABLE);
    }
    assert(driver->quiesce() && !lock_exists && retired_outputs == 1);
    assert(driver->quiesce() && poweroffs == final_pof && deep_sleeps == final_sleep);
#else
    assert(!driver->quiesce() && poweroffs == final_pof && deep_sleeps == final_sleep);
#endif
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    async_model = strstr(scenario,"async") != NULL;
    if (!strncmp(scenario, "uc", 2) || !strcmp(scenario, "mismatch") || !strcmp(scenario, "unstable-probe")) chip = PROBE_UC8279;
    garden_gpio_v1 native = {.api_version=1,.struct_size=sizeof(native),.claim=fake_claim,.write=fake_write,.read=fake_read,.release=fake_release,.deep_sleep_hold=fake_hold};
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
    native.retire_held_output = fake_retire;
    if (!strcmp(scenario, "legacy-hold")) native.struct_size = GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE;
#endif
    const risc_platform_clock_api_v1 clock = {1,sizeof(clock),NULL,fake_time,fake_sleep};
    const risc_provider_sync_api_v1 sync = {1,sizeof(sync),NULL,fake_owner,fake_create,fake_lock,fake_unlock,fake_destroy};
    const x4_power_ready_api_v1 power = {1,sizeof(power),NULL,fake_ready};
    const risc_frontlight_api_v1 light={1,sizeof(light),NULL,fake_light,NULL};
    risc_hw_spi_display_v1 config = {
        .struct_size=sizeof(config),.bus={.struct_size=sizeof(config.bus),.kind=1,.instance_id=101,.controller=0,.frequency_hz=1000000,.sclk=12,.mosi=11,.miso=-1,.sda=-1,.scl=-1},
        .width=800,.height=480,.offset_y=chip==PROBE_UC8279?120:0,.cs=13,.dc=18,.reset=14,.backlight=-1,.busy=6,
        .busy_active_high=chip==PROBE_SSD,.power_pins={0,0,0,0},.reset_assert_ms=chip==PROBE_UC8279?50:10,.reset_recovery_ms=chip==PROBE_UC8279?50:10};
    const char *materialized=getenv(chip==PROBE_UC8279?"X4_PANEL_TYPED_CONFIG_UC":"X4_PANEL_TYPED_CONFIG_SSD");
    if(materialized){FILE*f=fopen(materialized,"rb");assert(f);assert(fread(&config,1,sizeof(config),f)==sizeof(config));assert(fgetc(f)==EOF);assert(!fclose(f));}
    risc_hardware_device_v1 device = {1,sizeof(device),2,chip==PROBE_UC8279?"ultrachip,uc8279":"solomon-systech,ssd1677","unspecified","display.spi",1,sizeof(config),&config};
    risc_provider_dependency_v1 deps[] = {{"hardware.device",1,&device},{"platform.gpio",1,&native},{"platform.clock",1,&clock},{"platform.sync",1,&sync},{"board.power.ready",1,&power},{"display.frontlight",1,&light}};
    const risc_driver_v2 *driver = t5_driver_get(2); assert(driver && !t5_driver_get(1)); display = driver->capability;
    if (!strcmp(scenario,"validation")) {
        assert(!driver->start(NULL,6) && !driver->start(deps,4));
        config.width=480; assert(!driver->start(deps,6)); config.width=800;
        config.bus.mosi=1; assert(!driver->start(deps,6)); config.bus.mosi=11;
        config.offset_y=120; assert(!driver->start(deps,6)); config.offset_y=0;
        config.busy_active_high=0; assert(!driver->start(deps,6)); config.busy_active_high=1;
        config.power_count=1; assert(!driver->start(deps,6)); config.power_count=0;
        native.struct_size=offsetof(garden_gpio_v1,deep_sleep_hold); assert(!driver->start(deps,6)); native.struct_size=sizeof(native);
        owner=false; assert(!driver->start(deps,6)); owner=true;
        power_ok=false; assert(!driver->start(deps,6)); power_ok=true;
        deps[4]=deps[0]; assert(!driver->start(deps,6));
        assert(!claims && !lock_exists && driver->quiesce());
    } else if (!strcmp(scenario,"pullup-retained")) {
        probe_pullup=false;assert(!driver->start(deps,6));assert(!driver->quiesce());
        char error[112];const risc_driver_diagnostics_v2 *d=(const risc_driver_diagnostics_v2*)driver;
        assert(d->last_error(error,sizeof(error)) && strstr(error,"cause=gpio operation retained"));
    } else if (!strcmp(scenario,"claim-retained") || !strcmp(scenario,"scope-retained")) {
        fail_claim=!strcmp(scenario,"claim-retained"); scoped_bus=strcmp(scenario,"scope-retained")!=0;
        assert(!driver->start(deps,6)); assert(!driver->quiesce()); driver->stop(); assert(!driver->start(deps,6));
    } else if (!strcmp(scenario,"ambiguous") || !strcmp(scenario,"mismatch") || !strcmp(scenario,"unstable-probe")) {
        ambiguous=!strcmp(scenario,"ambiguous"); unstable_probe=!strcmp(scenario,"unstable-probe");
        if (!strcmp(scenario,"mismatch")) {device.compatible="solomon-systech,ssd1677";config.offset_y=0;config.busy_active_high=1;config.reset_assert_ms=config.reset_recovery_ms=10;}
        assert(!driver->start(deps,6)); assert(!refreshes && !driver->quiesce());
    } else {
        assert(driver->start(deps,6)); assert(!driver->start(deps,6));
        assert(claims==(chip==PROBE_SSD?10u:14u));
        assert(!display->set_brightness(NULL,1,0));assert(!display->set_brightness(NULL,101,100));
        assert(display->set_brightness(NULL,40,100) && light_level==40 && light_max==100);
        light_ok=false;assert(!display->set_brightness(NULL,50,100));light_ok=true;
        assert(probe_reads==(chip==PROBE_SSD?1u:2u));
        risc_display_info_v1 info={0}; assert(display->get_info(NULL,&info));
        assert(info.width==800 && info.height==480 && info.preferred_format==RISC_DISPLAY_FORMAT_MONO1);
        assert(info.flags&RISC_DISPLAY_INFO_BRIGHTNESS);
        assert(!!(info.flags&RISC_DISPLAY_INFO_ASYNC_PRESENT)==(chip==PROBE_UC8279));
        risc_display_surface_v1 surface={0}; uint64_t token=0; risc_display_present_status_v1 status={0};
        assert(!display->present_status(NULL,0,&status));
        if (strstr(scenario, "-sleep-")) { test_power(scenario, driver); goto done; }
        queue(&surface,&token); assert(!driver->quiesce());
        unsigned before=writes; assert(!display->wait_present(NULL,token,20000,NULL) && writes==before);
        assert(display->wait_present(NULL,token,0,&status) && status.state==RISC_DISPLAY_PRESENT_QUEUED && writes==before);
        display->release(NULL,surface.frame); assert(!display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
        if (!strcmp(scenario,"uc-async-metrics")) {
            test_metrics(driver,token);goto done;
        } else if (!strcmp(scenario,"uc-async-history")) {
            ordinary_history(driver,token);goto done;
        } else if (!strcmp(scenario,"uc-async-retained")) {
            const risc_driver_poll_v2 *d=(const risc_driver_poll_v2*)driver;
            d->poll(8);assert(present_state==PRESENT_ACTIVE && bytes_sent && bytes_sent<=512u);
            const unsigned writes_before=writes;owner=false;d->poll(8);assert(writes==writes_before);owner=true;
            fail_write=true;d->poll(8);assert(present_state==PRESENT_FAILED && !driver->quiesce());
            risc_display_present_metrics_v1 out={.api_version=1,.struct_size=sizeof(out)};
            assert(!risc_display_output_metrics(display)->snapshot(NULL,&out));
        } else if (!strcmp(scenario,"uc-async-deadline")) {
            const risc_driver_poll_v2 *d=(const risc_driver_poll_v2*)driver;
            d->poll(8);assert(present_state==PRESENT_ACTIVE);
            fake_now=async_deadline;d->poll(8);assert(present_state==PRESENT_FAILED && !refreshes);
            const risc_display_present_metrics_v1 out=snapshot_metrics();
            assert(out.state==PRESENT_FAILED && out.bytes_sent>0 && !(out.valid_times&RISC_DISPLAY_METRICS_BUSY_DONE));
            const unsigned writes_before=writes;d->poll(8);assert(writes==writes_before);
        } else if (!strcmp(scenario,"foreign-owner")) {
            owner=false;
            assert(!display->present_status(NULL,token,&status) && !display->wait_present(NULL,token,500,&status) && !driver->quiesce());
            assert(writes==before); owner=true; complete(token);
        } else if (!strcmp(scenario,"unlock-retained")) {
            unlock_ok=false; assert(!display->wait_present(NULL,token,20000,&status));
            assert(status.state==RISC_DISPLAY_PRESENT_COMPLETE && !driver->quiesce());
        } else if (!strcmp(scenario,"deadline")) {
            charge_every=20; assert(display->wait_present(NULL,token,5,&status));
            assert(status.state==RISC_DISPLAY_PRESENT_FAILED && !refreshes);
            unsigned old_writes=writes; assert(display->wait_present(NULL,token,20000,&status) && writes==old_writes);
        } else if (!strcmp(scenario,"admission-deadline")) {
            admission_cost=5; assert(display->wait_present(NULL,token,5,&status)); assert(status.state==RISC_DISPLAY_PRESENT_FAILED && !refreshes && writes==before);
        } else if (!strcmp(scenario,"clock-failure") || !strcmp(scenario,"clock-rollback")) {
            bad_clock=!strcmp(scenario,"clock-failure"); rollback_clock=!strcmp(scenario,"clock-rollback");
            assert(display->wait_present(NULL,token,20,&status) && status.state==RISC_DISPLAY_PRESENT_FAILED && !refreshes);
        } else if (!strcmp(scenario,"write-retained") || !strcmp(scenario,"read-retained")) {
            fail_write=!strcmp(scenario,"write-retained"); fail_read=!strcmp(scenario,"read-retained");
            assert(display->wait_present(NULL,token,20000,&status) && status.state==RISC_DISPLAY_PRESENT_FAILED);
            assert(!driver->quiesce() && !display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
        } else if (!strcmp(scenario,"busy-stuck") || !strcmp(scenario,"busy-absent")) {
            stuck_refresh=!strcmp(scenario,"busy-stuck"); absent_busy=!strcmp(scenario,"busy-absent");
            assert(display->wait_present(NULL,token,500,&status) && status.state==RISC_DISPLAY_PRESENT_FAILED && refreshes==1);
            if(stuck_refresh) assert(!driver->quiesce() && !poweroffs);
        } else {
            reenter=true; complete(token); reenter=false;
            unsigned previous_refreshes=refreshes; complete(token); assert(refreshes==previous_refreshes);
            assert(plane_bytes[chip==PROBE_SSD?0x24:0x13]==(chip==PROBE_SSD?48000u:60000u));
            if(chip==PROBE_SSD) assert(registers[0x21][0]==0x40 && registers[0x3C][0]==0xC0 && registers[0x22][0]==0xF7);
            else assert(registers[0x50][0]==0x97 && registers[0xE5][0]==0x1E && registers[0x00][0]==0x17 && registers[0x00][1]==0x4D);
            const risc_display_output_api_v1_history *history=risc_display_output_history(display); assert(history);
            assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
            memset(surface.pixels,0x0F,surface.size_bytes); assert(!history->seed_previous(NULL,surface.frame+1));
            assert(history->seed_previous(NULL,surface.frame)); memset(surface.pixels,0xF0,surface.size_bytes);
            const risc_display_rect_v1 bad={799,479,8,4},damage={17,20,1,4};
            assert(!display->submit(NULL,surface.frame,&bad,1,NULL,&token));
            assert(display->submit(NULL,surface.frame,&damage,1,NULL,&token)); complete(token);
            assert(old_pixel==0xF0 && new_pixel==0x0F);
            if(chip==PROBE_SSD) {
                assert(registers[0x21][0]==0 && registers[0x3C][0]==0x80 && registers[0x22][0]==0xFC);
                assert(registers[0x44][0]==16 && registers[0x44][2]==23 && registers[0x45][0]==(459u&255) && registers[0x45][2]==(456u&255));
            } else {
                const uint8_t window[]={0,16,0,23,0,140,0,143,1};
                assert(!memcmp(registers[0x90],window,sizeof(window)) && registers[0x50][0]==0xD7 && registers[0xE5][0]==0x5A);
            }
            if (!strcmp(scenario,"uc-poweroff") || !strcmp(scenario,"ssd-poweroff")) {
                stuck_poweroff=true; assert(!driver->quiesce()); assert(poweroffs==1 && !deep_sleeps);
                assert(!driver->quiesce() && poweroffs==1); assert(!display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
                stuck_poweroff=false; phase=IDLE;
            }
            if (!strcmp(scenario,"hold-retry")) {
                fail_hold=true; assert(!driver->quiesce() && deep_sleeps==1 && !reset_held); fail_hold=false;
            }
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
            if (!strcmp(scenario,"retire-retry")) {
                retire_ok=false; assert(!driver->quiesce() && reset_held && pads[14].token && !retired_outputs); retire_ok=true;
            }
            if (!strcmp(scenario,"release-retry")) {
                fail_release=true; assert(!driver->quiesce() && retired_outputs==1 && !pads[14].token && pads[6].token); fail_release=false;
            }
            if (!strcmp(scenario,"destroy-retry")) {
                destroy_ok=false; assert(!driver->quiesce() && retired_outputs==1 && lock_exists && !pads[6].token); destroy_ok=true;
            }
            if (!strcmp(scenario,"legacy-hold")) {
                assert(!driver->quiesce() && reset_held && pads[14].token && !retired_outputs);
                assert(!driver->quiesce() && poweroffs==1 && deep_sleeps==1);
            } else {
                assert(driver->quiesce() && !lock_exists && retired_outputs==1 && pads[14].held && !pads[14].token);
                for (unsigned pin=0;pin<49;++pin) assert(!pads[pin].token);
                assert(driver->quiesce() && poweroffs==1 && deep_sleeps==1);
                const unsigned old_writes=writes; driver->stop(); assert(writes==old_writes);
                assert(driver->start(deps,6));
                assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
                display->release(NULL,surface.frame); assert(driver->quiesce());
            }
#else
            assert(!driver->quiesce()); assert(poweroffs==1 && deep_sleeps==1 && reset_held && pads[14].held);
            assert(!driver->quiesce() && poweroffs==1 && deep_sleeps==1);
            driver->stop(); assert(!driver->start(deps,6));
#endif
        }
    }
done:
    printf("x4 ordinary panel %s: PASS (async slices=%u) wire=%016llx\n",scenario,async_calls,(unsigned long long)wire_hash); return 0;
}
