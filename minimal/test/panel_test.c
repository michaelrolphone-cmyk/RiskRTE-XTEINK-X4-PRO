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
static bool stuck_refresh, absent_busy, stuck_poweroff, scoped_bus = true, ambiguous, unstable_probe;
static bool rollback_clock, bad_clock, reenter;
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
static bool retire_ok = true;
static unsigned retired_outputs;
#endif
static unsigned chip = PROBE_SSD, phase, claims, releases, writes, pin_reads, holds;
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
    (void)c; if (bad_clock) return UINT64_MAX;
    if (rollback_clock) return --fake_now;
    return fake_now;
}
static void fake_sleep(void *c, uint32_t ms) {
    (void)c; fake_now += ms;
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
    if (pin == 13 && !level && pads[pin].level) { shift = bits = 0; }
    if (pin == 12 && level && !pads[12].level && !pads[13].level && pads[11].output) {
        shift = (uint8_t)((shift << 1) | pads[11].level);
        if (++bits == 8) {
            if (pads[18].level) model_data(shift); else model_command(shift);
            bits = shift = 0;
        }
    }
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
    assert(pin == 14 && owner && lock_held && pads[pin].output && pads[pin].level && enable);
    if (fail_hold) return RISC_DEEP_SLEEP_PLATFORM;
    pads[pin].held = true; return 0;
}
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
static bool fake_retire(void *c, uint64_t token) {
    (void)c; const unsigned pin = find_pin(token);
    assert(owner && lock_held && pin == 14 && pads[pin].output && pads[pin].level && pads[pin].held);
    if (!retire_ok) return false;
    pads[pin].token = 0; ++retired_outputs; return true;
}
#endif
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
    assert(display->wait_present(NULL, token, 20000, &status));
    assert(status.state == RISC_DISPLAY_PRESENT_COMPLETE);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
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
        risc_display_surface_v1 surface={0}; uint64_t token=0; risc_display_present_status_v1 status={0};
        assert(!display->present_status(NULL,0,&status));
        queue(&surface,&token); assert(!driver->quiesce());
        unsigned before=writes; assert(!display->wait_present(NULL,token,20000,NULL) && writes==before);
        assert(display->wait_present(NULL,token,0,&status) && status.state==RISC_DISPLAY_PRESENT_QUEUED && writes==before);
        display->release(NULL,surface.frame); assert(!display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&surface));
        if (!strcmp(scenario,"foreign-owner")) {
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
    printf("x4 ordinary panel %s: PASS\n",scenario); return 0;
}
