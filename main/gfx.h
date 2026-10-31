/*
 * Minimal software drawing library for the SPI LCD test project.
 *
 * A canvas holds RGB565 pixels for a slice of the screen.  All drawing calls use
 * screen coordinates; rows that fall outside the slice are clipped away.  So the
 * same code paints a full frame into a big canvas or a horizontal band into a
 * small one (see origin_y / gfx_canvas_set_origin()).
 *
 * The ILI9341 expects the high byte of every 16 bit pixel first, so all pixel
 * values are stored byte swapped (see GFX_RGB565).  Always create colours with
 * GFX_RGB565()/GFX_COLOR_*.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#include "gfx_color.h"

/* Number of pixel lines sent to the panel per transfer */
#define GFX_FLUSH_BAND_LINES 24

typedef struct {
    uint16_t *pixels; /* RGB565, byte swapped for the panel, natively DMA capable */
    uint16_t *scratch; /* bounce buffer for partial flushes, allocated on first use */
    int width;        /* pixels per row */
    int height;       /* rows stored in this canvas */
    int origin_y;     /* screen row that pixels[0] belongs to */
} gfx_canvas_t;

/** Allocate the pixel memory.  The bounce buffer follows with the first partial flush. */
esp_err_t gfx_canvas_init(gfx_canvas_t *canvas, int width, int height);

/** Release the buffers of a canvas created with gfx_canvas_init(). */
void gfx_canvas_free(gfx_canvas_t *canvas);

/** Move the canvas to another screen row (row 0 is the top of the screen). */
void gfx_canvas_set_origin(gfx_canvas_t *canvas, int origin_y);

/** True while the canvas still starts at the top of the screen. */
bool gfx_canvas_is_first_band(const gfx_canvas_t *canvas);

/* Split a byte swapped RGB565 value into 0..31 / 0..63 / 0..31 components and back */
static inline void gfx_unpack565(uint16_t value, int *r, int *g, int *b)
{
    const uint16_t native = GFX_SWAP16(value);
    *r = (native >> 11) & 0x1F;
    *g = (native >> 5) & 0x3F;
    *b = native & 0x1F;
}

static inline uint16_t gfx_pack565(int r, int g, int b)
{
    return GFX_SWAP16((uint16_t)((r << 11) | (g << 5) | b));
}

void gfx_clear(gfx_canvas_t *canvas, uint16_t color);
void gfx_pixel(gfx_canvas_t *canvas, int x, int y, uint16_t color);
void gfx_fill_rect(gfx_canvas_t *canvas, int x, int y, int width, int height, uint16_t color);
void gfx_rect_outline(gfx_canvas_t *canvas, int x, int y, int width, int height, uint16_t color);
void gfx_hline(gfx_canvas_t *canvas, int x, int y, int width, uint16_t color);
void gfx_vline(gfx_canvas_t *canvas, int x, int y, int height, uint16_t color);
void gfx_line(gfx_canvas_t *canvas, int x0, int y0, int x1, int y1, uint16_t color);
void gfx_circle_filled(gfx_canvas_t *canvas, int cx, int cy, int radius, uint16_t color);
void gfx_gradient_h(gfx_canvas_t *canvas, int x, int y, int width, int height,
                    uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1);

/** Width in pixels that gfx_text() needs for `text`. */
int gfx_text_width(const char *text, int scale);

/**
 * Blend an 8 bit alpha mask onto the canvas.
 *
 * @param mask    one byte per pixel, 0 = transparent, 255 = fully `color`
 * @param stride  bytes per mask row (usually the width)
 */
void gfx_blend_mask(gfx_canvas_t *canvas, int x, int y, int width, int height,
                    const uint8_t *mask, int stride, uint16_t color);

/**
 * Draw `text` (upper case only font, lower case input is folded) at x/y.
 * `opaque` fills the character cells with `background` first.
 */
void gfx_text(gfx_canvas_t *canvas, int x, int y, const char *text, uint16_t color,
              uint16_t background, int scale, bool opaque);

/**
 * Send a rectangle (screen coordinates) to the panel.
 *
 * Rows outside the canvas are dropped, so a band canvas needs no special caller.
 */
esp_err_t gfx_flush(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel,
                    int x, int y, int width, int height);

/** Send the whole canvas to the panel, at the screen position of its origin. */
esp_err_t gfx_flush_all(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel);
