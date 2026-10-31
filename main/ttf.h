/*
 * TrueType text rendering on the canvas.
 *
 * stb_truetype rasterises glyphs with anti-aliasing at any size and with
 * independent horizontal and vertical scale, so text can be condensed to fit a
 * narrow box.  The font file is embedded in the application image and read
 * directly from flash, no copy in RAM is needed.
 *
 * The font is Open Sans Regular (Apache License 2.0, see fonts/LICENSE).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "gfx.h"

/**
 * Initialise the renderer with a font file in memory (embedded, read from flash).
 *
 * @param data   pointer to the TrueType data
 * @param size   size in bytes
 */
esp_err_t ttf_init(const void *data, size_t size);

/** Initialise with the font that is built into the firmware. */
esp_err_t ttf_init_builtin(void);

/** True once a font is loaded. */
bool ttf_available(void);

/** Scale factor that makes an upper case letter `cap_height` pixels tall. */
float ttf_scale_for_cap_height(int cap_height);

/** Width in pixels of `utf8` at the given scale (no kerning). */
int ttf_text_width(const char *utf8, float scale_x, float scale_y);

/**
 * Draw `utf8` with its baseline at `baseline_y`.
 *
 * @param scale_x horizontal scale, values below the vertical scale condense the text
 * @param scale_y vertical scale (use ttf_scale_for_cap_height())
 */
void ttf_draw_text(gfx_canvas_t *canvas, int x, int baseline_y, const char *utf8,
                   float scale_x, float scale_y, uint16_t color);

/**
 * Ink box of `utf8` relative to the pen position and the baseline, in pixels.
 * Returns false when nothing would be drawn.
 */
bool ttf_text_ink_box(const char *utf8, float scale_x, float scale_y,
                      int *x0, int *y0, int *x1, int *y1);

/**
 * Draw `utf8` horizontally centred around `center_x`, standing on `baseline_y`.
 *
 * The text is scaled to `cap_height` and condensed if it would be wider than
 * `max_width`, but never below `min_squeeze` (1.0 = never condense).
 *
 * @return width actually used in pixels
 */
int ttf_draw_text_centered(gfx_canvas_t *canvas, int center_x, int baseline_y, const char *utf8,
                           int cap_height, int max_width, float min_squeeze, uint16_t color);
