#include "statusled.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "board_es3c28p.h"

static const char *TAG = "statusled";

/* One addressable WS2812 in GRB order, driven through RMT. */
#define LED_COUNT   1
#define LED_MAX     255

/* Update interval of the task: 50 ms gives 1200 steps per colour turn */
#define LED_TICK_MS 50

static led_strip_handle_t s_strip;
static bool s_ready;
static bool s_manual; /* set by statusled_set(): leave the LED alone */
static bool s_night;  /* ambilight on */

/* Hundredths of a percent -> 0..255 channel value */
static int level(int value, int percent)
{
    int scaled = value * LED_MAX * percent / (255 * 100);
    if (scaled < 0) {
        scaled = 0;
    }
    if (scaled > LED_MAX) {
        scaled = LED_MAX;
    }
    return scaled;
}

static void led_write(int red, int green, int blue)
{
    if (!s_ready) {
        return;
    }
    led_strip_set_pixel(s_strip, 0, (uint32_t)red, (uint32_t)green, (uint32_t)blue);
    led_strip_refresh(s_strip);
}

/* Hue wheel, h in 0..1535 (six sectors of 256), full saturation and value */
static void hue_to_rgb(int h, int *red, int *green, int *blue)
{
    const int sector = (h >> 8) % 6;
    const int fraction = h & 0xFF;

    switch (sector) {
    case 0: /* red -> yellow */
        *red = 255;
        *green = fraction;
        *blue = 0;
        break;
    case 1: /* yellow -> green */
        *red = 255 - fraction;
        *green = 255;
        *blue = 0;
        break;
    case 2: /* green -> cyan */
        *red = 0;
        *green = 255;
        *blue = fraction;
        break;
    case 3: /* cyan -> blue */
        *red = 0;
        *green = 255 - fraction;
        *blue = 255;
        break;
    case 4: /* blue -> magenta */
        *red = fraction;
        *green = 0;
        *blue = 255;
        break;
    default: /* magenta -> red */
        *red = 255;
        *green = 0;
        *blue = 255 - fraction;
        break;
    }
}

static void statusled_task(void *argument)
{
    (void)argument;

    for (;;) {
        if (!s_manual) {
            if (s_night) {
                /* hue from the run time, so the fade keeps running smoothly
                 * across redraws of the display */
                const int64_t milliseconds = esp_timer_get_time() / 1000;
                const int phase = (int)(milliseconds % STATUSLED_AMBIENT_PERIOD_MS);
                const int hue = phase * 6 * 256 / STATUSLED_AMBIENT_PERIOD_MS;

                int red = 0;
                int green = 0;
                int blue = 0;
                hue_to_rgb(hue, &red, &green, &blue);
                led_write(level(red, STATUSLED_AMBIENT_BRIGHTNESS),
                          level(green, STATUSLED_AMBIENT_BRIGHTNESS),
                          level(blue, STATUSLED_AMBIENT_BRIGHTNESS));
            } else {
                const int white = LED_MAX * STATUSLED_IDLE_BRIGHTNESS / 100;
                led_write(white, white, white);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

void statusled_init(void)
{
    const led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_LED_WS2812_PIN,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WS2812 on GPIO%d not available: %s", BOARD_LED_WS2812_PIN,
                 esp_err_to_name(err));
        s_strip = NULL;
        return;
    }

    led_strip_clear(s_strip);
    s_ready = true;
}

void statusled_set(bool red, bool green, bool blue)
{
    s_manual = true;
    led_write(red ? LED_MAX : 0, green ? LED_MAX : 0, blue ? LED_MAX : 0);
}

void statusled_ambilight(bool night)
{
    s_manual = false;
    s_night = night;
}

void statusled_start(void)
{
    if (xTaskCreate(statusled_task, "statusled", 2560, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no memory for the LED task, the LED stays off");
    }
}

void statusled_self_test(void)
{
    static const struct {
        const char *name;
        bool red;
        bool green;
        bool blue;
    } steps[] = {
        {"red", true, false, false},
        {"green", false, true, false},
        {"blue", false, false, true},
        {"white (all three)", true, true, true},
        {"off", false, false, false},
    };

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        statusled_set(steps[i].red, steps[i].green, steps[i].blue);
        ESP_LOGI(TAG, "RGB LED: %s", steps[i].name);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
