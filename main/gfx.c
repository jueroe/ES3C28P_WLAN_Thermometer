#include "gfx.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "font5x7.h"

static const char *TAG = "gfx";

esp_err_t gfx_canvas_init(gfx_canvas_t *canvas, int width, int height)
{
    memset(canvas, 0, sizeof(*canvas));

    const size_t pixels_size = (size_t)width * height * sizeof(uint16_t);

    /* The bounce buffer is only needed for partial flushes, so it is allocated
     * on first use - a full frame reads straight from the pixel memory and the
     * 11 KB of RAM stay with the network stack. */
    canvas->pixels = heap_caps_malloc(pixels_size, MALLOC_CAP_DMA);
    if (canvas->pixels == NULL) {
        ESP_LOGE(TAG, "out of memory: %d x %d needs %u bytes (largest free DMA block: %u bytes)",
                 width, height, (unsigned)pixels_size,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        gfx_canvas_free(canvas);
        return ESP_ERR_NO_MEM;
    }

    canvas->width = width;
    canvas->height = height;
    canvas->origin_y = 0;
    ESP_LOGI(TAG, "canvas %dx%d: %u bytes", width, height, (unsigned)pixels_size);
    return ESP_OK;
}

void gfx_canvas_free(gfx_canvas_t *canvas)
{
    if (canvas->pixels != NULL) {
        heap_caps_free(canvas->pixels);
    }
    if (canvas->scratch != NULL) {
        heap_caps_free(canvas->scratch);
    }
    memset(canvas, 0, sizeof(*canvas));
}

void gfx_canvas_set_origin(gfx_canvas_t *canvas, int origin_y)
{
    canvas->origin_y = origin_y;
}

bool gfx_canvas_is_first_band(const gfx_canvas_t *canvas)
{
    return canvas->origin_y == 0;
}

/* Clip screen rows to the part of the screen this canvas holds.  On success
 * `y` and `height` are the local rows to write. */
static bool clip_rows(const gfx_canvas_t *canvas, int *y, int *height)
{
    int first = *y - canvas->origin_y;
    int last = first + *height;
    if (first < 0) {
        first = 0;
    }
    if (last > canvas->height) {
        last = canvas->height;
    }
    if (first >= last) {
        return false;
    }
    *y = first;
    *height = last - first;
    return true;
}

void gfx_pixel(gfx_canvas_t *canvas, int x, int y, uint16_t color)
{
    const int local_y = y - canvas->origin_y;
    if ((unsigned)x < (unsigned)canvas->width && (unsigned)local_y < (unsigned)canvas->height) {
        canvas->pixels[local_y * canvas->width + x] = color;
    }
}

void gfx_clear(gfx_canvas_t *canvas, uint16_t color)
{
    const size_t count = (size_t)canvas->width * canvas->height;
    for (size_t i = 0; i < count; i++) {
        canvas->pixels[i] = color;
    }
}

void gfx_fill_rect(gfx_canvas_t *canvas, int x, int y, int width, int height, uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    int local_y = y;
    int local_height = height;
    if (!clip_rows(canvas, &local_y, &local_height)) {
        return;
    }

    const int x0 = x > 0 ? x : 0;
    const int x1 = (x + width) < canvas->width ? (x + width) : canvas->width;
    if (x0 >= x1) {
        return;
    }

    for (int row = local_y; row < local_y + local_height; row++) {
        uint16_t *dest = &canvas->pixels[row * canvas->width + x0];
        for (int i = x1 - x0; i > 0; i--) {
            *dest++ = color;
        }
    }
}

void gfx_hline(gfx_canvas_t *canvas, int x, int y, int width, uint16_t color)
{
    gfx_fill_rect(canvas, x, y, width, 1, color);
}

void gfx_vline(gfx_canvas_t *canvas, int x, int y, int height, uint16_t color)
{
    gfx_fill_rect(canvas, x, y, 1, height, color);
}

void gfx_rect_outline(gfx_canvas_t *canvas, int x, int y, int width, int height, uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    gfx_fill_rect(canvas, x, y, width, 1, color);
    gfx_fill_rect(canvas, x, y + height - 1, width, 1, color);
    gfx_fill_rect(canvas, x, y, 1, height, color);
    gfx_fill_rect(canvas, x + width - 1, y, 1, height, color);
}

void gfx_line(gfx_canvas_t *canvas, int x0, int y0, int x1, int y1, uint16_t color)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx - dy;

    for (;;) {
        gfx_pixel(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int error2 = error * 2;
        if (error2 > -dy) {
            error -= dy;
            x0 += sx;
        }
        if (error2 < dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void gfx_circle_filled(gfx_canvas_t *canvas, int cx, int cy, int radius, uint16_t color)
{
    if (radius < 0) {
        return;
    }

    int x = radius;
    int y = 0;
    int error = 1 - radius;

    while (x >= y) {
        gfx_fill_rect(canvas, cx - x, cy + y, 2 * x + 1, 1, color);
        gfx_fill_rect(canvas, cx - y, cy + x, 2 * y + 1, 1, color);
        gfx_fill_rect(canvas, cx - x, cy - y, 2 * x + 1, 1, color);
        gfx_fill_rect(canvas, cx - y, cy - x, 2 * y + 1, 1, color);

        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

void gfx_gradient_h(gfx_canvas_t *canvas, int x, int y, int width, int height,
                    uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int column = 0; column < width; column++) {
        const uint8_t r = r0 + (int)(r1 - r0) * column / (width - 1 ? width - 1 : 1);
        const uint8_t g = g0 + (int)(g1 - g0) * column / (width - 1 ? width - 1 : 1);
        const uint8_t b = b0 + (int)(b1 - b0) * column / (width - 1 ? width - 1 : 1);
        gfx_fill_rect(canvas, x + column, y, 1, height, GFX_RGB565(r, g, b));
    }
}

int gfx_text_width(const char *text, int scale)
{
    const int length = (int)strlen(text);
    if (length == 0) {
        return 0;
    }
    return (length * FONT5X7_ADVANCE - 1) * (scale < 1 ? 1 : scale);
}

/* Split a byte swapped RGB565 value into 5/6/5 components. */
static inline void rgb565_unpack(uint16_t value, int *r, int *g, int *b)
{
    gfx_unpack565(value, r, g, b);
}

static inline uint16_t rgb565_pack(int r, int g, int b)
{
    return gfx_pack565(r, g, b);
}

void gfx_blend_mask(gfx_canvas_t *canvas, int x, int y, int width, int height,
                    const uint8_t *mask, int stride, uint16_t color)
{
    if (mask == NULL || width <= 0 || height <= 0) {
        return;
    }

    int cr = 0;
    int cg = 0;
    int cb = 0;
    rgb565_unpack(color, &cr, &cg, &cb);

    const int first_row = y - canvas->origin_y;

    for (int row = 0; row < height; row++) {
        const int dest_y = first_row + row;
        if (dest_y < 0 || dest_y >= canvas->height) {
            continue;
        }
        const uint8_t *mask_row = mask + row * stride;
        uint16_t *dest_row = &canvas->pixels[dest_y * canvas->width];

        for (int column = 0; column < width; column++) {
            const int alpha = mask_row[column];
            if (alpha == 0) {
                continue;
            }
            const int dest_x = x + column;
            if (dest_x < 0 || dest_x >= canvas->width) {
                continue;
            }
            if (alpha == 255) {
                dest_row[dest_x] = color;

                continue;
            }

            int r = 0;
            int g = 0;
            int b = 0;
            rgb565_unpack(dest_row[dest_x], &r, &g, &b);
            /* 5/6/5 weights: scale the foreground by alpha and add the rest */
            r += (cr - r) * alpha / 255;
            g += (cg - g) * alpha / 255;
            b += (cb - b) * alpha / 255;
            dest_row[dest_x] = rgb565_pack(r, g, b);

        }
    }
}

void gfx_text(gfx_canvas_t *canvas, int x, int y, const char *text, uint16_t color,
              uint16_t background, int scale, bool opaque)
{
    if (scale < 1) {
        scale = 1;
    }

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        char character = *cursor;
        if (character >= 'a' && character <= 'z') {
            character -= 'a' - 'A'; /* the font only contains upper case glyphs */
        }
        if (character < FONT5X7_FIRST_CHAR || character > FONT5X7_LAST_CHAR) {
            character = '?';
        }

        const uint8_t *glyph = font5x7[character - FONT5X7_FIRST_CHAR];
        if (opaque) {
            gfx_fill_rect(canvas, x, y, FONT5X7_ADVANCE * scale, FONT5X7_HEIGHT * scale, background);
        }
        for (int column = 0; column < FONT5X7_WIDTH; column++) {
            for (int row = 0; row < FONT5X7_HEIGHT; row++) {
                if (glyph[column] & (1 << row)) {
                    gfx_fill_rect(canvas, x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += FONT5X7_ADVANCE * scale;
    }
}

esp_err_t gfx_flush(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel,
                    int x, int y, int width, int height)
{
    const int x0 = x > 0 ? x : 0;
    const int x1 = (x + width) < canvas->width ? (x + width) : canvas->width;

    int local_y = y;
    int local_height = height;
    if (!clip_rows(canvas, &local_y, &local_height) || x0 >= x1) {
        return ESP_OK;
    }
    const int local_y_end = local_y + local_height;

    const int line_pixels = x1 - x0;
    const bool full_width = (line_pixels == canvas->width);

    if (!full_width && canvas->scratch == NULL) {
        const size_t scratch_size = (size_t)canvas->width * GFX_FLUSH_BAND_LINES * sizeof(uint16_t);
        canvas->scratch = heap_caps_malloc(scratch_size, MALLOC_CAP_DMA);
        if (canvas->scratch == NULL) {
            ESP_LOGE(TAG, "no memory for the %u byte bounce buffer", (unsigned)scratch_size);
            return ESP_ERR_NO_MEM;
        }
    }

    for (int band_y = local_y; band_y < local_y_end; band_y += GFX_FLUSH_BAND_LINES) {
        const int band_lines = (local_y_end - band_y) < GFX_FLUSH_BAND_LINES
                                   ? (local_y_end - band_y)
                                   : GFX_FLUSH_BAND_LINES;
        const uint16_t *source;

        if (full_width) {
            /* rows are contiguous in this case, the panel driver is fine with a stride of full_width */
            source = &canvas->pixels[band_y * canvas->width + x0];
        } else {
            /* the panel driver assumes stride == drawn width, so use the bounce buffer */
            for (int row = 0; row < band_lines; row++) {
                memcpy(&canvas->scratch[row * line_pixels],
                       &canvas->pixels[(band_y + row) * canvas->width + x0],
                       (size_t)line_pixels * sizeof(uint16_t));
            }
            source = canvas->scratch;
        }

        const int screen_y = canvas->origin_y + band_y;
        const esp_err_t err =
            esp_lcd_panel_draw_bitmap(panel, x0, screen_y, x1, screen_y + band_lines, source);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "draw_bitmap(%d,%d)-(%d,%d) failed: %s", x0, screen_y, x1,
                     screen_y + band_lines, esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t gfx_flush_all(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel)
{
    return gfx_flush(canvas, panel, 0, canvas->origin_y, canvas->width, canvas->height);
}
