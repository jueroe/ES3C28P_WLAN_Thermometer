/*
 * Wallpaper handling: decodes a JPEG from the littlefs partition and paints it
 * into the canvas bands.
 *
 * The JPEG is decoded with TJpgDec (main/tjpgd).  The decoder walks the whole
 * image for one band and drops the rows that are not needed, so a band pass is a
 * full decode; with LAYOUT_BAND_HEIGHT 160 a frame needs two of them.  The
 * picture stays at full resolution, a complete 240x320 RGB565 frame (150 KB)
 * would not fit next to the WiFi stack.
 *
 * The table in background.h maps season and time of day to the file names, the
 * files themselves live at /littlefs/backgrounds/<season>/<time of day>/.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#include "gfx.h"

/* Seasons and times of day, used as indices into the table in background.h */
typedef enum {
    SEASON_SPRING = 0,
    SEASON_SUMMER,
    SEASON_AUTUMN,
    SEASON_WINTER,
    SEASON_COUNT
} season_t;

typedef enum {
    DAYTIME_MORNING = 0,
    DAYTIME_DAY,
    DAYTIME_EVENING,
    DAYTIME_NIGHT,
    DAYTIME_COUNT
} daytime_t;

/* Number of wallpapers per season and time of day (see background.h) */
#define WALLPAPER_PER_SLOT 3

/* ------------------------------------------------------------------------- */
/* When the background changes - everything adjustable lives here             */
/* ------------------------------------------------------------------------- */

/* Seasons are the meteorological ones, three months each:
 *   December, January, February  -> winter
 *   March, April, May            -> spring
 *   June, July, August           -> summer
 *   September, October, November -> autumn
 *
 * Inside a day the phase follows the sun at both ends and two fixed times:
 *
 *   sunrise                   -> morning   (sunrise from clock.c)
 *   WALLPAPER_MORNING_END     -> day       (10:00 instead of 9:00 in winter)
 *   sunset                    -> evening   (sunset from clock.c)
 *   WALLPAPER_NIGHT_START     -> night
 *
 * The image inside the slot is picked at random when the phase changes. */
#define WALLPAPER_MORNING_END_HOUR         9
#define WALLPAPER_MORNING_END_HOUR_WINTER 10
#define WALLPAPER_NIGHT_START_HOUR        23

/* Used as long as the sun times are unknown (no network, first seconds after
 * boot) or when they could not be fetched */
#define WALLPAPER_SUNRISE_FALLBACK_HOUR     7
#define WALLPAPER_SUNRISE_FALLBACK_MINUTE   0
#define WALLPAPER_SUNSET_FALLBACK_HOUR     19
#define WALLPAPER_SUNSET_FALLBACK_MINUTE    0

/**
 * Decode a JPEG file far enough to know its size.  Does not paint anything.
 *
 * The compressed image stays in RAM until the next call, so the band passes of
 * one frame do not read littlefs again.
 */
esp_err_t wallpaper_decode(const char *path);

/**
 * Paint screen rows [y0, y0+rows) of the decoded wallpaper into the canvas.
 *
 * The canvas must be positioned at y0 (gfx_canvas_set_origin()) and hold at
 * least `rows` lines.  Every call decodes the whole JPEG and keeps only the
 * requested rows, so the number of calls is what a frame costs.
 */
esp_err_t wallpaper_paint(gfx_canvas_t *canvas, int y0, int rows);

/** Full path of one wallpaper of a slot, e.g. /littlefs/backgrounds/spring/day/2.jpg */
void wallpaper_slot_path(char *buffer, size_t size, season_t season, daytime_t daytime, int index);

/**
 * Decode and paint one wallpaper of a slot, band by band, and send it to the
 * panel.
 */
esp_err_t wallpaper_show_slot(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel,
                              season_t season, daytime_t daytime, int index);

/** Random index 0..WALLPAPER_PER_SLOT-1. */
int wallpaper_random_index(void);

/** Season for a month (1..12), the meteorological seasons (see above). */
season_t wallpaper_season_from_month(int month);

/**
 * Time of day for a date and time, following the rules at the top of this file:
 * sunrise -> morning, WALLPAPER_MORNING_END_HOUR -> day, sunset -> evening,
 * WALLPAPER_NIGHT_START_HOUR -> night.
 */
daytime_t wallpaper_daytime_for(int month, int hour, int minute);
