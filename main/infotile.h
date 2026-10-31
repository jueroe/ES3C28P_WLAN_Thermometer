/*
 * Lower info tile: humidity, air pressure and date/time, one centred line each.
 *
 * Geometry and font sizes come from layout.h, so a change there is enough to
 * rearrange the block.
 */

#pragma once

#include <stdbool.h>

#include "gfx.h"

typedef struct {
    float humidity_rh;   /* relative humidity in percent */
    float pressure_hpa;  /* air pressure in hPa */
    int day;             /* 1..31 */
    int month;           /* 1..12 */
    int hour;            /* 0..23 */
    int minute;          /* 0..59 */
} infotile_values_t;

/**
 * Draw the three lines inside LAYOUT_INFO_*, centred horizontally and stacked
 * vertically, then report the result in the log.
 *
 * Formats: "47,1 %rF" (German decimal comma), "967 hPa" and "18.10. 23:59".
 */
void infotile_draw(gfx_canvas_t *canvas, const infotile_values_t *values, uint16_t color);
