/*
 * Large temperature readout drawn with the embedded TrueType font.
 *
 * Sizes come from layout.h, positions are parameters, so the readout can be
 * arranged freely:
 *   - the two digits in front of the comma are LAYOUT_TEMP_DIGIT_HEIGHT tall and
 *     start at the given x, standing on the given baseline
 *   - comma and decimal digit are half as tall and end on that baseline
 *   - the degree sign is placed with the top right corner of its ink
 *
 * A proportional font is much wider than seven segment digits, so the renderer
 * condenses the text horizontally until it fits the given maximum width, but not
 * below LAYOUT_TEMP_MIN_SQUEEZE; if even that is not enough, the digit height is
 * reduced so the proportions stay readable.
 */

#include "bigdigit.h"

#include "esp_log.h"

#include "layout.h"
#include "ttf.h"

static const char *TAG = "bigdigit";

/* U+00B0 DEGREE SIGN, UTF-8 */
#define DEGREE_SIGN "\xC2\xB0"

/* Vertical extent of the ink of `utf8` below the baseline (positive = descender). */
static int ink_descent(const char *utf8, float scale_x, float scale_y)
{
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!ttf_text_ink_box(utf8, scale_x, scale_y, &x0, &y0, &x1, &y1) || y1 <= 0) {
        return 0;
    }
    return y1;
}

void bigdigit_draw_degree(gfx_canvas_t *canvas, int right_x, int top_y, int digit_height,
                          uint16_t color)
{
    if (!ttf_available() || digit_height <= 0) {
        return;
    }

    const float scale = ttf_scale_for_cap_height(digit_height) * LAYOUT_TEMP_DEGREE_SCALE;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!ttf_text_ink_box(DEGREE_SIGN, scale, scale, &x0, &y0, &x1, &y1)) {
        return;
    }

    /* ink top right corner to (right_x, top_y): the pen sits at
     * (right_x - x1, top_y - y0) */
    ttf_draw_text(canvas, right_x - x1, top_y - y0, DEGREE_SIGN, scale, scale, color);

    if (gfx_canvas_is_first_band(canvas)) {
        ESP_LOGI(TAG, "degree: %dx%d px ink, top right corner at (%d,%d)", x1 - x0, y1 - y0,
                 right_x, top_y);
    }
}

int bigdigit_draw_number(gfx_canvas_t *canvas, int x, int baseline_y, int max_width, int tenths,
                         uint16_t color)
{
    if (!ttf_available() || max_width < 40) {
        return 0;
    }

    if (tenths < 0) {
        tenths = -tenths;
    }
    if (tenths > 999) {
        tenths = 999;
    }

    char big_digits[3];
    big_digits[0] = (char)('0' + tenths / 100);
    big_digits[1] = (char)('0' + (tenths / 10) % 10);
    big_digits[2] = '\0';
    const char decimal_text[2] = {(char)('0' + tenths % 10), '\0'};

    const float small_factor = LAYOUT_TEMP_DECIMAL_SCALE;
    int cap_height = LAYOUT_TEMP_DIGIT_HEIGHT;

    float scale_y = ttf_scale_for_cap_height(cap_height);
    float scale_x = scale_y;
    const int natural = ttf_text_width(big_digits, scale_x, scale_y)
                        + ttf_text_width(",", scale_x * small_factor, scale_y * small_factor)
                        + ttf_text_width(decimal_text, scale_x * small_factor, scale_y * small_factor);

    if (natural > max_width) {
        const float squeeze = (float)max_width / (float)natural;
        if (squeeze < LAYOUT_TEMP_MIN_SQUEEZE) {
            /* condensing this much would look broken, so use a smaller height */
            cap_height = (int)((float)cap_height * squeeze / LAYOUT_TEMP_MIN_SQUEEZE);
            scale_y = ttf_scale_for_cap_height(cap_height);
            scale_x = scale_y * LAYOUT_TEMP_MIN_SQUEEZE;
        } else {
            scale_x = scale_y * squeeze;
        }
    }

    const float small_scale_y = scale_y * small_factor;
    const float small_scale_x = scale_x * small_factor;

    /* The comma tail hangs below the baseline, lift it so its ink ends on it. */
    const int comma_descent = ink_descent(",", small_scale_x, small_scale_y);

    int pen_x = x;
    ttf_draw_text(canvas, pen_x, baseline_y, big_digits, scale_x, scale_y, color);
    pen_x += ttf_text_width(big_digits, scale_x, scale_y);

    ttf_draw_text(canvas, pen_x, baseline_y - comma_descent, ",", small_scale_x, small_scale_y,
                  color);
    pen_x += ttf_text_width(",", small_scale_x, small_scale_y);

    ttf_draw_text(canvas, pen_x, baseline_y, decimal_text, small_scale_x, small_scale_y, color);
    pen_x += ttf_text_width(decimal_text, small_scale_x, small_scale_y);

    const int used = pen_x - x;
    if (gfx_canvas_is_first_band(canvas)) {
        ESP_LOGI(TAG, "\"%s,%s\" cap %d px, scale x/y %.3f/%.3f, from x=%d on baseline y=%d, %d px wide",
                 big_digits, decimal_text, cap_height, scale_x, scale_y, x, baseline_y, used);
    }

    return used;
}
