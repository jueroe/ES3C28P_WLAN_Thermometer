#include "ttf.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ttf";

/* stb_truetype takes its temporary buffers (glyph vertices, flattened points,
 * edges, bitmap) from the heap.  A failed allocation inside the library is
 * silent - the glyph simply stays empty, which looked like "the texts vanished"
 * on the display after WiFi and TLS had fragmented the heap.  The chunk sizes
 * in stb_truetype.h are reduced for that reason (see the note at the top of the
 * file), and this wrapper reports anything that still does not fit. */
static void *ttf_alloc(size_t size);
static void ttf_free(void *pointer);

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(size, user) ttf_alloc(size)
#define STBTT_free(ptr, user) ttf_free(ptr)
#define STBTT_assert(x) ((void)0)
#include "stb_truetype.h"

static void *ttf_alloc(size_t size)
{
    void *pointer = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (pointer == NULL) {
        ESP_LOGE(TAG, "no memory for %u bytes (free %d, largest %d)", (unsigned)size,
                 (int)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    return pointer;
}

static void ttf_free(void *pointer)
{
    heap_caps_free(pointer);
}

/* Font built into the firmware, see EMBED_FILES in CMakeLists.txt */
extern const uint8_t _binary_OpenSans_Regular_ttf_start[] asm("_binary_OpenSans_Regular_ttf_start");
extern const uint8_t _binary_OpenSans_Regular_ttf_end[] asm("_binary_OpenSans_Regular_ttf_end");

static stbtt_fontinfo s_font;
static bool s_ready;
esp_err_t ttf_init(const void *data, size_t size)
{
    if (data == NULL || size < 256) {
        ESP_LOGE(TAG, "no font data");
        return ESP_ERR_INVALID_ARG;
    }

    const int offset = stbtt_GetFontOffsetForIndex((const unsigned char *)data, 0);
    if (offset < 0) {
        ESP_LOGE(TAG, "no TrueType font in the embedded data");
        return ESP_ERR_INVALID_ARG;
    }
    if (!stbtt_InitFont(&s_font, (const unsigned char *)data, offset)) {
        ESP_LOGE(TAG, "stbtt_InitFont failed");
        return ESP_FAIL;
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &line_gap);
    s_ready = true;
    ESP_LOGI(TAG, "%u byte font loaded, ascent %d / descent %d / gap %d (font units)",
             (unsigned)size, ascent, descent, line_gap);
    return ESP_OK;
}

esp_err_t ttf_init_builtin(void)
{
    const size_t size = (size_t)(_binary_OpenSans_Regular_ttf_end - _binary_OpenSans_Regular_ttf_start);
    return ttf_init(_binary_OpenSans_Regular_ttf_start, size);
}

bool ttf_available(void)
{
    return s_ready;
}

float ttf_scale_for_cap_height(int cap_height)
{
    if (!s_ready || cap_height <= 0) {
        return 0.0f;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!stbtt_GetCodepointBox(&s_font, 'H', &x0, &y0, &x1, &y1) || y1 <= y0) {
        return stbtt_ScaleForPixelHeight(&s_font, (float)cap_height);
    }
    return (float)cap_height / (float)(y1 - y0);
}

/* Decode one UTF-8 code point, advance the pointer. */
static uint32_t utf8_next(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t codepoint;

    if (s[0] < 0x80) {
        codepoint = s[0];
        *text += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        codepoint = (uint32_t)(s[0] & 0x1F) << 6;
        codepoint |= (uint32_t)(s[1] & 0x3F);
        *text += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        codepoint = (uint32_t)(s[0] & 0x0F) << 12;
        codepoint |= (uint32_t)(s[1] & 0x3F) << 6;
        codepoint |= (uint32_t)(s[2] & 0x3F);
        *text += 3;
    } else {
        *text += 1; /* unsupported sequence */
        return 0;
    }
    return codepoint;
}

int ttf_text_width(const char *utf8, float scale_x, float scale_y)
{
    if (!s_ready || utf8 == NULL) {
        return 0;
    }

    int width = 0;
    const char *cursor = utf8;
    while (*cursor != '\0') {
        const uint32_t codepoint = utf8_next(&cursor);
        int advance = 0;
        int left_side_bearing = 0;
        stbtt_GetCodepointHMetrics(&s_font, (int)codepoint, &advance, &left_side_bearing);
        width += (int)((float)advance * scale_x);
    }
    return width;
}

bool ttf_text_ink_box(const char *utf8, float scale_x, float scale_y,
                      int *x0, int *y0, int *x1, int *y1)
{
    if (!s_ready || utf8 == NULL || *utf8 == '\0') {
        return false;
    }

    bool first = true;
    int pen_x = 0;
    const char *cursor = utf8;

    while (*cursor != '\0') {
        const uint32_t codepoint = utf8_next(&cursor);
        int advance = 0;
        int left_side_bearing = 0;
        stbtt_GetCodepointHMetrics(&s_font, (int)codepoint, &advance, &left_side_bearing);

        int gx0 = 0;
        int gy0 = 0;
        int gx1 = 0;
        int gy1 = 0;
        if (stbtt_GetCodepointBox(&s_font, (int)codepoint, &gx0, &gy0, &gx1, &gy1)) {
            const int bx0 = pen_x + (int)((float)gx0 * scale_x);
            const int bx1 = pen_x + (int)((float)gx1 * scale_x);
            const int by0 = -(int)((float)gy1 * scale_y); /* y grows down on screen */
            const int by1 = -(int)((float)gy0 * scale_y);
            if (first) {
                *x0 = bx0;
                *y0 = by0;
                *x1 = bx1;
                *y1 = by1;
                first = false;
            } else {
                if (bx0 < *x0) {
                    *x0 = bx0;
                }
                if (by0 < *y0) {
                    *y0 = by0;
                }
                if (bx1 > *x1) {
                    *x1 = bx1;
                }
                if (by1 > *y1) {
                    *y1 = by1;
                }
            }
        }
        pen_x += (int)((float)advance * scale_x);
    }

    return !first;
}

void ttf_draw_text(gfx_canvas_t *canvas, int x, int baseline_y, const char *utf8,
                   float scale_x, float scale_y, uint16_t color)
{
    if (!s_ready || utf8 == NULL) {
        return;
    }

    /* When the screen is drawn in bands, skip whole strings that lie outside
     * the current one - rasterising glyphs is the expensive part. */
    int ink_x0 = 0;
    int ink_y0 = 0;
    int ink_x1 = 0;
    int ink_y1 = 0;
    if (ttf_text_ink_box(utf8, scale_x, scale_y, &ink_x0, &ink_y0, &ink_x1, &ink_y1)) {
        const int top = baseline_y + ink_y0;
        const int bottom = baseline_y + ink_y1;
        if (bottom < canvas->origin_y || top >= canvas->origin_y + canvas->height) {
            return;
        }
    }

    int pen_x = x;
    const char *cursor = utf8;

    while (*cursor != '\0') {
        const uint32_t codepoint = utf8_next(&cursor);
        if (codepoint == 0) {
            continue;
        }

        int advance = 0;
        int left_side_bearing = 0;
        stbtt_GetCodepointHMetrics(&s_font, (int)codepoint, &advance, &left_side_bearing);

        int width = 0;
        int height = 0;
        int x_offset = 0;
        int y_offset = 0;
        unsigned char *bitmap = stbtt_GetCodepointBitmapSubpixel(&s_font, scale_x, scale_y, 0.0f, 0.0f,
                                                                (int)codepoint, &width, &height,
                                                                &x_offset, &y_offset);
        if (bitmap != NULL && width > 0 && height > 0) {
            gfx_blend_mask(canvas, pen_x + x_offset, baseline_y + y_offset, width, height,
                           bitmap, width, color);
            stbtt_FreeBitmap(bitmap, NULL);
        }
        pen_x += (int)((float)advance * scale_x);
    }
}

int ttf_draw_text_centered(gfx_canvas_t *canvas, int center_x, int baseline_y, const char *utf8,
                           int cap_height, int max_width, float min_squeeze, uint16_t color)
{
    if (!s_ready || utf8 == NULL || cap_height <= 0 || max_width <= 0) {
        return 0;
    }

    const float scale_y = ttf_scale_for_cap_height(cap_height);
    float scale_x = scale_y;
    int width = ttf_text_width(utf8, scale_x, scale_y);

    if (width > max_width) {
        const float squeeze = (float)max_width / (float)width;
        if (squeeze < min_squeeze) {
            /* shorter text instead of an unreadable squeeze */
            const int shorter = (int)((float)cap_height * squeeze / min_squeeze);
            const float shorter_scale = ttf_scale_for_cap_height(shorter);
            scale_x = shorter_scale * min_squeeze;
            width = ttf_text_width(utf8, scale_x, shorter_scale);
            ttf_draw_text(canvas, center_x - width / 2, baseline_y, utf8, scale_x, shorter_scale,
                          color);
            return width;
        }
        scale_x = scale_y * squeeze;
        width = ttf_text_width(utf8, scale_x, scale_y);
    }

    ttf_draw_text(canvas, center_x - width / 2, baseline_y, utf8, scale_x, scale_y, color);
    return width;
}
