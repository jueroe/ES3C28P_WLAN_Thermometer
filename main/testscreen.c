#include "testscreen.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_es3c28p.h"
#include "statusled.h"
#include "touch.h"

static const char *TAG = "testscreen";

/* Row layout, portrait 240x320 (y positions, heights) */
#define ROW_TITLE_Y     0
#define ROW_TITLE_H     26
#define ROW_BARS_Y      32
#define ROW_BARS_H      120
#define ROW_GRAD_Y      158
#define ROW_GRAD_H      30
#define ROW_GEOM_Y      194
#define ROW_GEOM_H      64
#define ROW_STATUS_Y    264
#define ROW_STATUS_H    56

static void draw_test_pattern(gfx_canvas_t *canvas)
{
    gfx_clear(canvas, GFX_BLACK);

    /* title bar */
    gfx_fill_rect(canvas, 0, ROW_TITLE_Y, BOARD_SCREEN_WIDTH, ROW_TITLE_H, GFX_NAVY);
    gfx_text(canvas, 4, ROW_TITLE_Y + 3, "ES3C28P IPS  240X320 PORTRAIT", GFX_WHITE, GFX_NAVY, 1, true);
    gfx_text(canvas, 4, ROW_TITLE_Y + 13, "ESP32-D0WD-V3  " CONFIG_IDF_TARGET "  IDF " IDF_VER,
             GFX_GRAY, GFX_NAVY, 1, true);

    /* colour bars: white, yellow, cyan, green, magenta, red, blue, black */
    static const uint16_t bars[] = {GFX_WHITE, GFX_YELLOW, GFX_CYAN, GFX_GREEN,
                                    GFX_MAGENTA, GFX_RED, GFX_BLUE, GFX_BLACK};
    const int bar_width = BOARD_SCREEN_WIDTH / (int)(sizeof(bars) / sizeof(bars[0]));
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); i++) {
        gfx_fill_rect(canvas, (int)i * bar_width, ROW_BARS_Y, bar_width + 1, ROW_BARS_H, bars[i]);
    }

    /* ramps to judge grey/colour reproduction */
    const int ramp_height = ROW_GRAD_H / 3;
    gfx_gradient_h(canvas, 0, ROW_GRAD_Y, BOARD_SCREEN_WIDTH, ramp_height, 0, 0, 0, 255, 255, 255);
    gfx_gradient_h(canvas, 0, ROW_GRAD_Y + ramp_height, BOARD_SCREEN_WIDTH, ramp_height, 0, 0, 0, 255, 0, 0);
    gfx_gradient_h(canvas, 0, ROW_GRAD_Y + 2 * ramp_height, BOARD_SCREEN_WIDTH, ramp_height, 0, 0, 0, 0, 0, 255);

    /* geometry: outline, diagonals, circle */
    gfx_rect_outline(canvas, 4, ROW_GEOM_Y, BOARD_SCREEN_WIDTH - 8, ROW_GEOM_H, GFX_WHITE);
    gfx_line(canvas, 4, ROW_GEOM_Y, BOARD_SCREEN_WIDTH - 5, ROW_GEOM_Y + ROW_GEOM_H - 1, GFX_DARK_GRAY);
    gfx_line(canvas, BOARD_SCREEN_WIDTH - 5, ROW_GEOM_Y, 4, ROW_GEOM_Y + ROW_GEOM_H - 1, GFX_DARK_GRAY);
    gfx_circle_filled(canvas, BOARD_SCREEN_WIDTH / 2, ROW_GEOM_Y + ROW_GEOM_H / 2, 20, GFX_ORANGE);
    gfx_rect_outline(canvas, BOARD_SCREEN_WIDTH / 2 - 21, ROW_GEOM_Y + ROW_GEOM_H / 2 - 21, 43, 43, GFX_WHITE);
    gfx_text(canvas, 8, ROW_GEOM_Y + 4, "GEOMETRY 1PX", GFX_WHITE, GFX_BLACK, 1, false);

    /* status area background, its text is refreshed every frame */
    gfx_fill_rect(canvas, 0, ROW_STATUS_Y, BOARD_SCREEN_WIDTH, ROW_STATUS_H, GFX_DARK_GRAY);
}

/* Numbers of the status block, drawn at absolute screen coordinates and
 * clipped to whichever band is being painted */
static void draw_status(gfx_canvas_t *canvas, const char *line2, const char *line3, int sweep)
{
    const uint16_t background = GFX_DARK_GRAY;

    gfx_fill_rect(canvas, 0, ROW_STATUS_Y, BOARD_SCREEN_WIDTH, ROW_STATUS_H, background);
    gfx_text(canvas, 4, ROW_STATUS_Y + 3, line2, GFX_WHITE, background, 1, true);
    gfx_text(canvas, 4, ROW_STATUS_Y + 14, line3, GFX_WHITE, background, 1, false);

    /* sweeping bar */
    const int sweep_width = BOARD_SCREEN_WIDTH / 6;
    const int sweep_x = sweep * (BOARD_SCREEN_WIDTH - sweep_width) / 99;
    gfx_fill_rect(canvas, sweep_x, ROW_STATUS_Y + 26, sweep_width, 10, GFX_GREEN);
    gfx_rect_outline(canvas, 0, ROW_STATUS_Y + 26, BOARD_SCREEN_WIDTH, 10, GFX_WHITE);
}

/* Paint one band: test pattern, the last touch point and the status block */
static void draw_band(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel, int band_y,
                      const touch_state_t *dot, const char *line2, const char *line3, int sweep)
{
    gfx_canvas_set_origin(canvas, band_y);
    draw_test_pattern(canvas);

    if (dot != NULL && dot->pressed) {
        gfx_circle_filled(canvas, dot->x, dot->y, 5, GFX_MAGENTA);
        gfx_circle_filled(canvas, dot->x, dot->y, 2, GFX_WHITE);
    }

    draw_status(canvas, line2, line3, sweep);
    gfx_flush_all(canvas, panel);
}

/* Repaint the whole screen, band by band */
static void redraw_all(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel, const touch_state_t *dot,
                       const char *line2, const char *line3, int sweep)
{
    for (int band_y = 0; band_y < BOARD_SCREEN_HEIGHT; band_y += canvas->height) {
        draw_band(canvas, panel, band_y, dot, line2, line3, sweep);
    }
    gfx_canvas_set_origin(canvas, 0);
}

void testscreen_draw(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel)
{
    redraw_all(canvas, panel, NULL, "", "", 0);
}

void testscreen_run(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel)
{
    char touch_line[48];
    snprintf(touch_line, sizeof(touch_line), "TOUCH: %s", touch_is_available() ? "READY" : "NOT AVAILABLE");

    const int64_t start_us = esp_timer_get_time();
    int64_t last_report_us = start_us;
    int64_t last_touch_log_us = 0;
    int frames = 0;
    int fps = 0;
    touch_state_t last_touch = {0};

    for (;;) {
        const int64_t now_us = esp_timer_get_time();
        const int64_t uptime_us = now_us - start_us;
        const int sweep = (int)((uptime_us / 50000) % 100); /* 0..99, 5 s per cycle */

        /* touch: remember the point and follow it in the status line */
        touch_state_t touch = {0};
        if (touch_is_available() && touch_read(&touch) == ESP_OK && touch.pressed) {
            last_touch = touch;

            snprintf(touch_line, sizeof(touch_line), "TOUCH: RAW %4u/%4u -> %3d/%3d  Z%4u",
                     touch.raw_x, touch.raw_y, touch.x, touch.y, touch.strength);
            if (now_us - last_touch_log_us > 200000) {
                last_touch_log_us = now_us;
                ESP_LOGI(TAG, "%s", touch_line);
            }
        }

        char line2[48];
        snprintf(line2, sizeof(line2), "UPTIME %5d.%1ds  FPS %2d  HEAP %4d KB",
                 (int)(uptime_us / 1000000), (int)((uptime_us / 100000) % 10), fps,
                 (int)(heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024));
        redraw_all(canvas, panel, &last_touch, line2, touch_line, sweep);

        /* BOOT key redraws the clean test pattern */
        if (gpio_get_level(BOARD_PIN_BOOT) == 0) {
            ESP_LOGI(TAG, "BOOT pressed, redrawing the test pattern");
            statusled_set(false, false, true);
            testscreen_draw(canvas, panel);
            statusled_set(false, true, false);
            while (gpio_get_level(BOARD_PIN_BOOT) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        frames++;
        if (now_us - last_report_us >= 1000000) {
            fps = frames;
            frames = 0;
            last_report_us = now_us;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
