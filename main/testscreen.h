/*
 * Bring-up test screen for the E32R28T board.
 *
 * Everything in here is for checking the hardware: the test pattern with colour
 * bars, gradients and geometry, the status/uptime line, the touch test (paints a
 * dot where the panel is pressed) and the BOOT key handling.
 *
 * It is self-contained: to remove it from the firmware, delete testscreen.c and
 * testscreen.h and the single call in main.c.
 */

#pragma once

#include "esp_lcd_panel_ops.h"

#include "gfx.h"

/* Runs the test screen until the board is reset (never returns). */
void testscreen_run(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel);

/* Draws the clean test pattern into the canvas and sends it to the panel. */
void testscreen_draw(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel);
