#pragma once
/* Xteink X4 Pro pad map. Confirmed in FreeInk/CrossPoint hardware notes
 * (app1 / ESP32S3_X4_TL_SSD1677), not the earlier app0 pin scramble. */
#define X4PRO_PIN_BTN_LEFT 0u
#define X4PRO_PIN_PERIPH_EN 1u
#define X4PRO_PIN_TOUCH_PWR 2u
#define X4PRO_PIN_BTN_POWER 3u
#define X4PRO_PIN_TOUCH_RST 4u
#define X4PRO_PIN_SD_PWR 5u
#define X4PRO_PIN_EPD_BUSY 6u
#define X4PRO_PIN_BTN_RIGHT 7u
#define X4PRO_PIN_LIGHT_COOL 8u
#define X4PRO_PIN_LIGHT_WARM 9u
#define X4PRO_PIN_TOUCH_INT 10u
#define X4PRO_PIN_EPD_MOSI 11u
#define X4PRO_PIN_EPD_SCLK 12u
#define X4PRO_PIN_EPD_CS 13u
#define X4PRO_PIN_EPD_RST 14u
#define X4PRO_PIN_EPD_DC 18u
#define X4PRO_PIN_CHG_STAT 21u
#define X4PRO_PIN_I2C_SCL 38u
#define X4PRO_PIN_I2C_SDA 39u
#define X4PRO_PIN_SD_DAT0 40u
#define X4PRO_PIN_SD_CLK 41u
#define X4PRO_PIN_SD_CMD 42u
#define X4PRO_PANEL_WIDTH 800u
#define X4PRO_PANEL_HEIGHT 480u
#define X4PRO_I2C_GT911 0x5du
#define X4PRO_I2C_RTC 0x51u
#define X4PRO_I2C_CW2017 0x63u
