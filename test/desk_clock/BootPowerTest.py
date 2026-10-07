"""Run the actual isolated X4 board-alive bootstrap against a held-pad model."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    (folder / "driver").mkdir()
    (folder / "driver/gpio.h").write_text(r'''
#pragma once
#include <cstdint>
using gpio_num_t = int;
using esp_err_t = int;
constexpr int ESP_OK=0, GPIO_MODE_OUTPUT=2, GPIO_PULLUP_DISABLE=0;
constexpr int GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
esp_err_t gpio_set_level(gpio_num_t, unsigned);
esp_err_t gpio_hold_dis(gpio_num_t);
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_hold_en(gpio_num_t);
''')
    (folder / "test.cpp").write_text(r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "driver/gpio.h"
#include "platform/X4BootPower.h"
static bool held, padHigh, outputHigh, outputEnabled, digitalMux;
// Model a pull-down whenever an unheld pad is not driven. Every successful
// fake edge checks the physical level, not merely the final helper result.
static void settle() {
    const bool wasHigh=padHigh;
    if(!held)padHigh=outputEnabled && digitalMux && outputHigh;
    assert(!wasHigh || padHigh); // No HIGH-to-LOW interruption in keep-alive.
}
static unsigned calls, fail;
static int checkpoint(int pin) { assert(pin==1); return ++calls==fail ? -1 : ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin,unsigned level) {
    assert(level==1);
    int result=checkpoint(pin); if(result!=ESP_OK)return result;
    outputHigh=true; settle(); return ESP_OK;
}
esp_err_t gpio_hold_dis(gpio_num_t pin) {
    int result=checkpoint(pin); if(result!=ESP_OK)return result;
    held=false; settle(); return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t* c) {
    assert(c && c->pin_bit_mask==(1ULL<<1) && c->mode==GPIO_MODE_OUTPUT);
    assert(!c->pull_up_en && !c->pull_down_en && !c->intr_type);
    int result=checkpoint(1); if(result!=ESP_OK)return result;
    outputEnabled=digitalMux=true; settle(); return ESP_OK;
}
esp_err_t gpio_hold_en(gpio_num_t pin) {
    int result=checkpoint(pin); if(result!=ESP_OK)return result;
    assert(outputEnabled && outputHigh && padHigh); held=true; return ESP_OK;
}
void reset(bool hold,bool level) { held=hold;padHigh=outputHigh=level;outputEnabled=digitalMux=true;calls=fail=0; }
int main() {
    for(bool hold:{false,true})for(bool level:{false,true}) {
        reset(hold,level);assert(x4PrepareBootPower() && calls==5 && held && padHigh);
        // Deep sleep removes the ordinary digital output configuration. An
        // explicitly held RTC-capable pad retains its physical HIGH level.
        outputEnabled=digitalMux=outputHigh=false;settle();
        assert(padHigh);calls=0;
        assert(x4PrepareBootPower() && calls==5 && held && padHigh);
        // Repeated calls remain bounded and preserve the established level.
        calls=0;assert(x4PrepareBootPower() && calls==5 && held && padHigh);
    }
    for(bool retained:{false,true})for(unsigned failure=1;failure<=5;++failure) {
        reset(retained,retained);
        outputEnabled=digitalMux=outputHigh=false;settle();fail=failure;
        assert(!x4PrepareBootPower() && calls==failure);
        fail=calls=0;assert(x4PrepareBootPower() && held && padHigh);
    }
    puts("Actual X4 bootstrap: cold/stale hold/deep wake/repeat/all failures PASS (pad model)");
}
''')
    binary = folder / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-DBOARD_XTEINK_X4_PRO=1",
                    "-I" + str(folder), "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "Drivers/x4pro_board"), "-I" + str(ROOT / "src/platform"),
                    str(folder / "test.cpp"), str(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "src/platform/X4BootPower.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10, env=os.environ)
