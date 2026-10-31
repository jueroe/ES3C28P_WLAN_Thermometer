#include "infotile.h"

#include <math.h>
#include <stdio.h>

#include "esp_log.h"

#include "layout.h"
#include "ttf.h"

static const char *TAG = "infotile";

/* One line of the block */
typedef struct {
    char text[24];
    int cap_height;
    int baseline_y;
    int used_width;
} line_t;

void infotile_draw(gfx_canvas_t *canvas, const infotile_values_t *values, uint16_t color)
{
    if (!ttf_available() || values == NULL) {
        return;
    }

    /* German decimal comma for the humidity, decimal point for the pressure */
    const int humidity_tenths = (int)lroundf(values->humidity_rh * 10.0f);
    const int pressure_thousandths = (int)lroundf(values->pressure_hpa * 1000.0f);

    line_t lines[3];
    snprintf(lines[0].text, sizeof(lines[0].text), "%d,%d %%rF", humidity_tenths / 10,
             humidity_tenths % 10);
    lines[0].cap_height = LAYOUT_INFO_HUMIDITY_CAP;

    snprintf(lines[1].text, sizeof(lines[1].text), "%d hPa", (pressure_thousandths+500) / 1000);
    lines[1].cap_height = LAYOUT_INFO_PRESSURE_CAP;

    snprintf(lines[2].text, sizeof(lines[2].text), "%02d.%02d. %02d:%02d", values->day,
             values->month, values->hour, values->minute);
    lines[2].cap_height = LAYOUT_INFO_TIME_CAP;

    /* centre the block vertically: glyphs without descenders, so the cap height
     * is a good measure for the ink height of a line */
    int block_height = 0;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        block_height += lines[i].cap_height;
        if (i > 0) {
            block_height += LAYOUT_INFO_GAP;
        }
    }

    const int top = LAYOUT_INFO_Y + (LAYOUT_INFO_H - block_height) / 2;
    const int center_x = LAYOUT_INFO_X + LAYOUT_INFO_W / 2;

    int baseline = top;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        baseline += lines[i].cap_height;
        lines[i].baseline_y = baseline;
        lines[i].used_width = ttf_draw_text_centered(canvas, center_x, baseline, lines[i].text,
                                                     lines[i].cap_height, LAYOUT_INFO_W,
                                                     LAYOUT_INFO_MIN_SQUEEZE, color);
        baseline += LAYOUT_INFO_GAP;
    }

    if (!gfx_canvas_is_first_band(canvas)) {
        return;
    }

    ESP_LOGI(TAG, "area x %d..%d, y %d..%d, centre line at x=%d, block %d px high from y=%d",
             LAYOUT_INFO_X, LAYOUT_INFO_X + LAYOUT_INFO_W - 1, LAYOUT_INFO_Y,
             LAYOUT_INFO_Y + LAYOUT_INFO_H - 1, center_x, block_height, top);
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        ESP_LOGI(TAG, "  line %u \"%s\": cap %d px, baseline y=%d, %d px wide", (unsigned)i + 1,
                 lines[i].text, lines[i].cap_height, lines[i].baseline_y, lines[i].used_width);
    }
}
