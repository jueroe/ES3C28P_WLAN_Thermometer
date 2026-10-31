#include "wallpaper.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "tjpgd.h"

#include "background.h"
#include "board_es3c28p.h"
#include "clock.h"

static const char *TAG = "wallpaper";

static const char *const s_season_names[SEASON_COUNT] = {"spring", "summer", "autumn", "winter"};
static const char *const s_daytime_names[DAYTIME_COUNT] = {"morning", "day", "evening", "night"};

/* TJpgDec takes its huffman tables, quantisation tables and the MCU buffer from
 * this pool; 4 KB is the documented size for JD_FASTDECODE 1. */
#define WALLPAPER_WORK_SIZE 4096

/* Compressed JPEG of the current wallpaper.  It stays in RAM so that every band
 * of a frame can be decoded without reading littlefs again. */
static uint8_t *s_jpeg;
static size_t s_jpeg_size;
static uint8_t *s_work;
static int s_width;
static int s_height;

/* TJpgDec reports both, the input stream and the output rectangle, through one
 * context pointer */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
    gfx_canvas_t *canvas;
    int y0;   /* first screen row of the band */
    int rows; /* height of the band */
} decode_context_t;

static const char *result_text(JRESULT result)
{
    switch (result) {
    case JDR_OK:
        return "ok";
    case JDR_INTR:
        return "interrupted";
    case JDR_INP:
        return "input stream error";
    case JDR_MEM1:
        return "work area too small";
    case JDR_MEM2:
        return "input buffer too small";
    case JDR_PAR:
        return "parameter error";
    case JDR_FMT1:
        return "broken data";
    case JDR_FMT2:
        return "unsupported format";
    case JDR_FMT3:
        return "not a baseline JPEG";
    default:
        return "unknown";
    }
}

static size_t input_func(JDEC *jd, uint8_t *buffer, size_t size)
{
    decode_context_t *context = jd->device;
    const size_t remaining = context->size - context->offset;
    if (size > remaining) {
        size = remaining;
    }
    /* a NULL buffer means "skip these bytes" */
    if (buffer != NULL && size > 0) {
        memcpy(buffer, context->data + context->offset, size);
    }
    context->offset += size;
    return size;
}

/* Called for every decoded MCU; only the rows of the current band are copied,
 * the rest is dropped.  The panel wants the high byte of a pixel first, so the
 * halves are swapped on the way into the canvas. */
static int output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    decode_context_t *context = jd->device;
    gfx_canvas_t *canvas = context->canvas;
    const uint16_t *source = (const uint16_t *)bitmap;
    const int width = rect->right - rect->left + 1;
    const int height = rect->bottom - rect->top + 1;

    int x0 = rect->left;
    int x1 = rect->right + 1;
    if (x1 > canvas->width) {
        x1 = canvas->width;
    }
    if (x0 >= x1) {
        return 1;
    }

    for (int row = 0; row < height; row++) {
        const int screen_y = rect->top + row;
        if (screen_y < context->y0 || screen_y >= context->y0 + context->rows) {
            continue;
        }
        uint16_t *dest = &canvas->pixels[(screen_y - context->y0) * canvas->width + x0];
        const uint16_t *line = &source[row * width];
        for (int x = x0; x < x1; x++) {
            const uint16_t pixel = line[x - rect->left];
            dest[x - x0] = (uint16_t)((pixel << 8) | (pixel >> 8));
        }
    }
    return 1; /* continue decoding */
}

/* Read a whole file into a buffer that is reused for the next wallpaper. */
static esp_err_t read_file(const char *path, uint8_t **data, size_t *size)
{
    struct stat file_info;
    if (stat(path, &file_info) != 0) {
        ESP_LOGE(TAG, "%s not found", path);
        return ESP_ERR_NOT_FOUND;
    }
    if (file_info.st_size <= 0) {
        ESP_LOGE(TAG, "%s is empty", path);
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return ESP_FAIL;
    }

    const size_t file_size = (size_t)file_info.st_size;
    if (*data == NULL || *size < file_size) {
        heap_caps_free(*data);
        *data = heap_caps_malloc(file_size, MALLOC_CAP_8BIT);
        if (*data == NULL) {
            ESP_LOGE(TAG, "no memory for the %u byte file", (unsigned)file_size);
            fclose(file);
            *size = 0;
            return ESP_ERR_NO_MEM;
        }
    }
    *size = file_size;

    const size_t read = fread(*data, 1, file_size, file);
    fclose(file);
    if (read != file_size) {
        ESP_LOGE(TAG, "short read on %s (%u of %u bytes)", path, (unsigned)read, (unsigned)file_size);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wallpaper_decode(const char *path)
{
    esp_err_t err = read_file(path, &s_jpeg, &s_jpeg_size);
    if (err != ESP_OK) {
        return err;
    }

    if (s_work == NULL) {
        s_work = heap_caps_malloc(WALLPAPER_WORK_SIZE, MALLOC_CAP_8BIT);
        if (s_work == NULL) {
            ESP_LOGE(TAG, "no memory for the %d byte decoder work area", WALLPAPER_WORK_SIZE);
            return ESP_ERR_NO_MEM;
        }
    }

    /* one throw-away pass over the header to learn the image size */
    decode_context_t context = {.data = s_jpeg, .size = s_jpeg_size};
    JDEC decoder;
    const JRESULT result = jd_prepare(&decoder, input_func, s_work, WALLPAPER_WORK_SIZE, &context);
    if (result != JDR_OK) {
        ESP_LOGE(TAG, "%s: %s", path, result_text(result));
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_width = decoder.width;
    s_height = decoder.height;
    if (s_width != BOARD_SCREEN_WIDTH || s_height != BOARD_SCREEN_HEIGHT) {
        ESP_LOGW(TAG, "%s is %dx%d, the screen is %dx%d - the picture is cropped", path, s_width,
                 s_height, BOARD_SCREEN_WIDTH, BOARD_SCREEN_HEIGHT);
    }

    ESP_LOGI(TAG, "%s: %dx%d, %u bytes JPEG, %s", path, s_width, s_height, (unsigned)s_jpeg_size,
             decoder.ncomp == 3 ? "YCbCr" : "grayscale");
    return ESP_OK;
}

esp_err_t wallpaper_paint(gfx_canvas_t *canvas, int y0, int rows)
{
    if (s_jpeg == NULL || s_work == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    decode_context_t context = {
        .data = s_jpeg,
        .size = s_jpeg_size,
        .canvas = canvas,
        .y0 = y0,
        .rows = rows,
    };

    JDEC decoder;
    JRESULT result = jd_prepare(&decoder, input_func, s_work, WALLPAPER_WORK_SIZE, &context);
    if (result != JDR_OK) {
        ESP_LOGE(TAG, "band %d: %s", y0, result_text(result));
        return ESP_ERR_INVALID_RESPONSE;
    }

    result = jd_decomp(&decoder, output_func, 0);
    if (result != JDR_OK) {
        ESP_LOGE(TAG, "band %d: %s", y0, result_text(result));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
void wallpaper_slot_path(char *buffer, size_t size, season_t season, daytime_t daytime, int index)
{
    if (season >= SEASON_COUNT || daytime >= DAYTIME_COUNT || index < 0 || index >= WALLPAPER_PER_SLOT) {
        snprintf(buffer, size, "%s", "");
        return;
    }
    snprintf(buffer, size, "%s%s/%s/%s", PATH_TO_BACKGROUND_IMAGES, s_season_names[season],
             s_daytime_names[daytime], backgrounds[season][daytime][index].filename);
}

esp_err_t wallpaper_show_slot(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel,
                              season_t season, daytime_t daytime, int index)
{
    char path[128];
    wallpaper_slot_path(path, sizeof(path), season, daytime, index);
    if (path[0] == '\0') {
        ESP_LOGE(TAG, "invalid wallpaper slot %d/%d/%d", season, daytime, index);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wallpaper_decode(path);
    if (err != ESP_OK) {
        return err;
    }

    for (int y = 0; y < BOARD_SCREEN_HEIGHT; y += canvas->height) {
        const int rows = (BOARD_SCREEN_HEIGHT - y) < canvas->height ? (BOARD_SCREEN_HEIGHT - y)
                                                                   : canvas->height;
        gfx_canvas_set_origin(canvas, y);
        err = wallpaper_paint(canvas, y, rows);
        if (err != ESP_OK) {
            gfx_canvas_set_origin(canvas, 0);
            return err;
        }
        err = gfx_flush_all(canvas, panel);
        if (err != ESP_OK) {
            gfx_canvas_set_origin(canvas, 0);
            return err;
        }
    }
    gfx_canvas_set_origin(canvas, 0);
    return ESP_OK;
}

int wallpaper_random_index(void)
{
    return (int)(esp_random() % WALLPAPER_PER_SLOT);
}

season_t wallpaper_season_from_month(int month)
{
    if (month < 1 || month > 12) {
        return SEASON_SUMMER;
    }
    if (month >= 3 && month <= 5) {
        return SEASON_SPRING;
    }
    if (month >= 6 && month <= 8) {
        return SEASON_SUMMER;
    }
    if (month >= 9 && month <= 11) {
        return SEASON_AUTUMN;
    }
    return SEASON_WINTER;
}

daytime_t wallpaper_daytime_for(int month, int hour, int minute)
{
    const int now_minutes = hour * 60 + minute;

    const sun_time_t *sunrise = clock_sunrise();
    const sun_time_t *sunset = clock_sunset();
    const int sunrise_minutes =
        (sunrise != NULL && sunrise->valid) ? sunrise->hour * 60 + sunrise->minute
                                           : WALLPAPER_SUNRISE_FALLBACK_HOUR * 60 +
                                                 WALLPAPER_SUNRISE_FALLBACK_MINUTE;
    const int sunset_minutes =
        (sunset != NULL && sunset->valid) ? sunset->hour * 60 + sunset->minute
                                         : WALLPAPER_SUNSET_FALLBACK_HOUR * 60 +
                                               WALLPAPER_SUNSET_FALLBACK_MINUTE;

    int morning_end = (wallpaper_season_from_month(month) == SEASON_WINTER)
                          ? WALLPAPER_MORNING_END_HOUR_WINTER * 60
                          : WALLPAPER_MORNING_END_HOUR * 60;
    if (morning_end > sunset_minutes) {
        /* northern winter: the sun may set before the fixed time */
        morning_end = sunset_minutes;
    }

    if (now_minutes >= WALLPAPER_NIGHT_START_HOUR * 60) {
        return DAYTIME_NIGHT;
    }
    if (now_minutes < sunrise_minutes) {
        return DAYTIME_NIGHT;
    }
    if (now_minutes < morning_end) {
        return DAYTIME_MORNING;
    }
    if (now_minutes < sunset_minutes) {
        return DAYTIME_DAY;
    }
    return DAYTIME_EVENING;
}
