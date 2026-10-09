#!/usr/bin/env python3
"""Run the production X4 hook around the actual pinned Arduino startup code."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FRAMEWORK = Path(os.environ.get('X4_ARDUINO_FRAMEWORK', str(Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio')) / 'packages/framework-arduinoespressif32')))
main = FRAMEWORK / 'cores/esp32/main.cpp'
misc = FRAMEWORK / 'cores/esp32/esp32-hal-misc.c'
# Hashes are pinned to platformio/framework-arduinoespressif32 3.20017.241212.
EXPECTED = {
    'main.cpp': '130ee85dbf77f5343ce3b7a4f9b9355316fb293105603af8cd70ebbcaff5d9ff',
    'esp32-hal-misc.c': 'd6374832d4e7d987e8c9eb71aa36f12e6f03a5d16c298a139a6b222a108788d1',
}
for source in (main, misc):
    if hashlib.sha256(source.read_bytes()).hexdigest() != EXPECTED[source.name]:
        raise ValueError('Startup source differs from pinned Arduino 2.0.17: ' + str(source))
body = misc.read_text().split('void initArduino()\n{', 1)[1].split('\n}\n', 1)[0]
with tempfile.TemporaryDirectory() as name:
    work = Path(name)
    declarations = '''#pragma once
#include <cstddef>
#include <cstdint>
#include <driver/gpio.h>
using TaskHandle_t=void*;
#define CONFIG_AUTOSTART_ARDUINO 1
#define CONFIG_FREERTOS_UNICORE 0
#define ARDUINO_RUNNING_CORE 0
#define ARDUINO_USB_CDC_ON_BOOT 1
#define ARDUINO_USB_MSC_ON_BOOT 0
#define ARDUINO_USB_DFU_ON_BOOT 0
#define ARDUINO_USB_ON_BOOT 1
#define CONFIG_SPIRAM_SUPPORT 1
#define F_CPU 240000000
#define CONFIG_LOG_DEFAULT_LEVEL 0
#define ESP_ERR_NVS_NO_FREE_PAGES 10
#define ESP_ERR_NVS_NEW_VERSION_FOUND 11
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define log_e(...) ((void)0)
struct esp_partition_t {size_t size;};
struct SerialFake {void begin();};
struct UsbFake {void begin();};
extern SerialFake Serial;
extern UsbFake USB;
extern void (*serialEventRun)();
void setup(); void loop(); void esp_task_wdt_reset();
void xTaskCreateUniversal(void(*)(void*),const char*,size_t,void*,int,void**,int);
extern "C" {
void initArduino(); void init(); void initVariant();
void setCpuFrequencyMhz(int); void psramInit();
void esp_log_level_set(const char*,int);
int nvs_flash_init();
const esp_partition_t* esp_partition_find_first(int,int,const char*);
int esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
}
'''
    files = {
        'Arduino.h': declarations,
        'USB.h': '#include "Arduino.h"\n',
        'freertos/FreeRTOS.h': '#include "Arduino.h"\n',
        'freertos/task.h': '#include "Arduino.h"\n',
        'esp_task_wdt.h': '#include "Arduino.h"\n',
        'esp_attr.h': '#define RTC_NOINIT_ATTR\n',
        'esp_system.h': '#pragma once\nint esp_reset_reason();\n',
        'esp_sleep.h': '#pragma once\nint esp_sleep_get_wakeup_cause();\n',
        'esp_timer.h': '#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
        'rom/rtc.h': '#pragma once\nint rtc_get_reset_reason(int);\n',
        'soc/gpio_reg.h': '#define GPIO_IN_REG 1\n',
        'soc/rtc_cntl_reg.h': '#define RTC_CNTL_PAD_HOLD_REG 2\n#define RTC_CNTL_BROWN_OUT_REG 3\n',
        'soc/soc.h': '#pragma once\n#include <cstdint>\nuint32_t x4_test_reg_read(int);\n#define REG_READ(reg) x4_test_reg_read(reg)\n',
        'X4NativeBuildIdentity.h': '#define X4_NATIVE_COMPOSITION_IDENTITY "X4_NATIVE_COMPOSITION:host-fixture"\n',
        'pinned_main.cpp': main.read_text(),
        'pinned_init.cpp': '#include "Arduino.h"\nextern "C" void initArduino() {\n' + body + '\n}\n',
    }
    for path, content in files.items():
        target = work / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content)
    for mode in (1, 0):
        binary = work / ('test-' + str(mode))
        args = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-DARDUINO_USB_MODE=' + str(mode),
                '-I' + str(work), '-I' + str(ROOT / 'minimal/test/early_native_fake'),
                str(work / 'pinned_main.cpp'), str(work / 'pinned_init.cpp'), str(ROOT / 'minimal/test/early_native_boot_test.cpp'),
                '-Wl,--wrap=app_main', '-o', str(binary)]
        if os.environ.get('SANITIZE') == '1':
            args[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(args, check=True)
        for held in (0, 1, 2):
            for failure in range(8):
                subprocess.run([str(binary), str(failure), str(held)], check=True)
        subprocess.run([str(binary), 'reset'], check=True)
        subprocess.run([str(binary), 'milestone'], check=True)
print('Pinned Arduino app_main/initArduino ordering, hold/failures, and reset breadcrumbs PASS')
