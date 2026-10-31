/*
 * Large temperature readout.
 *
 * Drawn with the embedded TrueType font (Open Sans) via ttf.h, so the digits
 * have real font shapes and anti-aliased edges instead of the seven segment
 * look.  The sizes come from layout.h, the positions are passed in, so the
 * readout can be placed anywhere on the screen.
 */

#pragma once

#include <stdint.h>

#include "gfx.h"

/**
 * Draw "xx,x" with the comma and one decimal digit.
 *
 * The first big digit starts at `x`, the big digits stand on `baseline_y`
 * (= their bottom edge).  The comma and the decimal digit are half as tall and
 * end on the same line.  The group is condensed horizontally until it fits
 * `max_width`, but never below LAYOUT_TEMP_MIN_SQUEEZE; if that is not enough
 * the digit height is reduced instead.
 *
 * @param tenths temperature in 1/10 °C (217 = 21,7 °C), sign is ignored
 * @return width actually used in pixels, 0 on invalid arguments
 */
int bigdigit_draw_number(gfx_canvas_t *canvas, int x, int baseline_y, int max_width, int tenths,
                         uint16_t color);

/**
 * Draw the degree sign with the top right corner of its ink at (right_x, top_y).
 *
 * @param digit_height the digit height the sign should relate to (scaled by
 *                     LAYOUT_TEMP_DEGREE_SCALE)
 */
void bigdigit_draw_degree(gfx_canvas_t *canvas, int right_x, int top_y, int digit_height,
                          uint16_t color);
