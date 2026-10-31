/*
 * Board definitions for the QDtech / LCDWiki "2.8inch ESP32-S3 Display"
 * (SKU ES3C28P: ESP32-S3R8 with 16 MB flash and 8 MB octal PSRAM, ILI9341V,
 * FT6336G capacitive touch, ES8311 audio codec, WS2812 status LED).
 *
 * Pin numbers are taken from the LCDWiki page "2.8inch ESP32-S3 Display"
 * (ESP32 pin assignment table), which matches the board schematic:
 * https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
 *
 * Change the values here if your board revision is wired differently - all
 * modules of this project only use the macros below.
 */

#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

/* ------------------------------------------------------------------------- */
/* LCD - ILI9341V, 4-wire SPI (own SPI bus)                                  */
/* ------------------------------------------------------------------------- */
/* In ESP-IDF 6 the two free SPI buses are numbered SPI1 and SPI2; the macros
 * SPI2_HOST / SPI3_HOST still work as deprecated aliases and are used here.
 * The LCD is the only SPI device on this board (the microSD slot is wired to
 * the SDIO peripheral), so it gets its own bus. */
#define BOARD_LCD_HOST            SPI2_HOST
#define BOARD_LCD_PIN_SCLK        GPIO_NUM_12
#define BOARD_LCD_PIN_MOSI        GPIO_NUM_11
#define BOARD_LCD_PIN_MISO        GPIO_NUM_13
#define BOARD_LCD_PIN_CS          GPIO_NUM_10
#define BOARD_LCD_PIN_DC          GPIO_NUM_46
#define BOARD_LCD_PIN_RST         GPIO_NUM_NC /* reset shares the ESP32 EN line */

#define BOARD_LCD_H_RES           240
#define BOARD_LCD_V_RES           320

/* 1 = 320x240 landscape, 0 = 240x320 portrait.
 *
 * The application runs in portrait: screen origin (0,0) is the top left corner,
 * x grows to the right up to BOARD_SCREEN_WIDTH-1 = 239, y grows down to 319.
 * This matches MADCTL MX (the usual portrait setting of the ILI9341), so the
 * content equals the factory landscape picture turned 90 degrees
 * counter-clockwise - the board sits 90 degrees clockwise in the housing.
 *
 * MADCTL mapping of the flags: MX = mirror_x, MY = mirror_y, MV = !portrait.
 * If the picture appears upside down, swap MIRROR_X and MIRROR_Y. */
#define BOARD_LCD_LANDSCAPE       0
#define BOARD_LCD_MIRROR_X        1
#define BOARD_LCD_MIRROR_Y        0

#if BOARD_LCD_LANDSCAPE
#define BOARD_SCREEN_WIDTH        BOARD_LCD_V_RES
#define BOARD_SCREEN_HEIGHT       BOARD_LCD_H_RES
#else
#define BOARD_SCREEN_WIDTH        BOARD_LCD_H_RES
#define BOARD_SCREEN_HEIGHT       BOARD_LCD_V_RES
#endif

/* Drop to 20 MHz if the picture shows noise, 40 MHz is fine on-board */
#define BOARD_LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)
/* 1 = BGR element order, 0 = RGB.  The reference configurations for this board
 * (LCDWiki demo, Tactility, Bruce) use BGR for the ILI9341V.
 * If red and blue are swapped in the test pattern, toggle this value. */
#define BOARD_LCD_BGR             1
/* 1 = invert all colours.  This is an IPS panel and needs inversion on;
 * 0 would show it like a photo negative. */
#define BOARD_LCD_INVERT_COLORS   1

/* ------------------------------------------------------------------------- */
/* LCD backlight, dimmed with LEDC (high level = backlight on)               */
/* ------------------------------------------------------------------------- */
#define BOARD_LCD_PIN_BACKLIGHT   GPIO_NUM_45
#define BOARD_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BOARD_BL_LEDC_TIMER       LEDC_TIMER_0
#define BOARD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BOARD_BL_LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define BOARD_BL_LEDC_FREQ_HZ     20000
#define BOARD_BL_MAX_DUTY         ((1 << 10) - 1)

/* ------------------------------------------------------------------------- */
/* Touch - FT6336G capacitive controller on the shared I2C bus               */
/* ------------------------------------------------------------------------- */
#define BOARD_TOUCH_ENABLE        1
#define BOARD_TOUCH_I2C_ADDRESS   0x38
#define BOARD_TOUCH_PIN_RST       GPIO_NUM_18
#define BOARD_TOUCH_PIN_IRQ       GPIO_NUM_17 /* input, polled instead of using the interrupt */
#define BOARD_TOUCH_I2C_FREQ_HZ   400000

/*
 * The FT6336G reports coordinates in screen pixels (not raw ADC values), so no
 * calibration window is needed.  The controller sits on the same glass as the
 * display; in the portrait orientation above its axes match the portrait screen
 * 1:1 - no swap, no mirroring.  Should the touch land mirrored, flip the flags
 * below (the serial log prints the raw and the mapped point).
 */
#define BOARD_TOUCH_SWAP_XY       0
#define BOARD_TOUCH_MIRROR_X      0
#define BOARD_TOUCH_MIRROR_Y      0

/* Coordinate range the controller reports; used to scale if a panel reports a
 * different resolution than 240x320. */
#define BOARD_TOUCH_X_MAX         240
#define BOARD_TOUCH_Y_MAX         320

/* ------------------------------------------------------------------------- */
/* I2C bus - shared by the touch controller and the UNIT ENV III             */
/* ------------------------------------------------------------------------- */
/* The IIC connector on the board is wired to the capacitive touch bus:
 *   SDA = IO16, SCL = IO15.
 * FT6336G (0x38), SHT30 (0x44) and QMP6988 (0x70) all live on this bus. */
#define BOARD_I2C_HOST            I2C_NUM_0
#define BOARD_I2C_PIN_SDA         GPIO_NUM_16
#define BOARD_I2C_PIN_SCL         GPIO_NUM_15
#define BOARD_I2C_FREQ_HZ         100000

/* UNIT ENV III (M5Stack): SHT30 for temperature/humidity, QMP6988 for pressure */
#define BOARD_SHT30_ADDRESS       0x44
#define BOARD_QMP6988_ADDRESS     0x70

/* ------------------------------------------------------------------------- */
/* RGB status LED: single addressable WS2812 on one data line                */
/* ------------------------------------------------------------------------- */
/* The board carries one WS2812 (GRB order) instead of three PWM channels.
 * main.c uses green = running, blue while the test pattern is redrawn. */
#define BOARD_LED_WS2812_PIN      GPIO_NUM_42

/* ------------------------------------------------------------------------- */
/* Keys                                                                      */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_BOOT            GPIO_NUM_0 /* "BOOT" key, low when pressed */

/* ------------------------------------------------------------------------- */
/* Not used by this project, listed for completeness                         */
/* ------------------------------------------------------------------------- */
/* microSD (SDIO):  CLK = IO38, CMD = IO40, D0..D3 = IO39/41/48/47             */
/* Audio (ES8311):  enable = IO1 (low active), MCLK = IO4, BCLK = IO5,        */
/*                  DOUT = IO8, LRCK = IO7, DIN = IO6                        */
/* Battery sense:   IO9 (200k/200k divider)                                   */
