from pathlib import Path
p=Path(__file__).parent/'stubs';p.mkdir(exist_ok=True)
headers=['driver/gpio.h','driver/rtc_io.h','esp_attr.h','esp_system.h','esp_sleep.h','esp_timer.h','rom/rtc.h','soc/gpio_reg.h','soc/rtc_cntl_reg.h','soc/soc.h','nvs.h','nvs_flash.h']
for name in headers:
 f=p/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('#pragma once\n#include "mock.h"\n')
(p/'X4NativeBuildIdentity.h').write_text('#pragma once\n#define X4_NATIVE_COMPOSITION_IDENTITY "persistent-bootlog-host-test"\n')
(p/'mock.h').write_text(r'''#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NVS_NOT_FOUND=0x1102;
constexpr int ESP_ERR_NVS_INVALID_LENGTH=0x110c,ESP_ERR_NVS_NO_FREE_PAGES=0x110d;
using gpio_num_t=int;
constexpr int GPIO_NUM_1=1,GPIO_MODE_INPUT_OUTPUT=3,GPIO_PULLUP_DISABLE=0;
constexpr int GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0,NVS_READWRITE=1;
struct gpio_config_t {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;};
#define RTC_NOINIT_ATTR
#define GPIO_IN_REG 0
#define GPIO_IN1_REG 1
#define GPIO_STRAP_REG 2
#define RTC_CNTL_PAD_HOLD_REG 3
#define RTC_CNTL_BROWN_OUT_REG 4
#define REG_READ(r) (uint32_t(0x1200u+(r)))
using nvs_handle_t=uint32_t;
int rtc_gpio_deinit(gpio_num_t);
int gpio_set_level(gpio_num_t,int);
int gpio_config(const gpio_config_t*);
int gpio_hold_dis(gpio_num_t);
int gpio_hold_en(gpio_num_t);
int gpio_get_level(gpio_num_t);
int esp_reset_reason();
int rtc_get_reset_reason(int);
int esp_sleep_get_wakeup_cause();
int64_t esp_timer_get_time();
int nvs_flash_init();
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
''')
