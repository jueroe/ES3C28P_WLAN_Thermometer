/*
 * Colour definitions for the RGB565 canvas.
 *
 * This header deliberately includes nothing but <stdint.h>, so pure data tables
 * (for example the wallpaper table in background.h) can use the colours without
 * pulling in the ESP-IDF headers of gfx.h.  That also keeps such a header
 * resolvable for IntelliSense in files that are not part of a build target.
 */

#pragma once

#include <stdint.h>

/* The panel expects the high byte first, so every value is byte swapped once
 * here and stored in that form in the framebuffer. */
#define GFX_SWAP16(value)   ((uint16_t)((((uint16_t)(value) << 8) | ((uint16_t)(value) >> 8))))
#define GFX_RGB565(r, g, b) GFX_SWAP16((uint16_t)((((uint16_t)(r) & 0xF8) << 8) | \
                                                 (((uint16_t)(g) & 0xFC) << 3) | \
                                                 ((uint16_t)(b) >> 3)))

#define GFX_BLACK       GFX_RGB565(0x00, 0x00, 0x00)
#define GFX_WHITE       GFX_RGB565(0xFF, 0xFF, 0xFF)
#define GFX_RED         GFX_RGB565(0xFF, 0x00, 0x00)
#define GFX_GREEN       GFX_RGB565(0x00, 0xFF, 0x00)
#define GFX_BLUE        GFX_RGB565(0x00, 0x00, 0xFF)
#define GFX_CYAN        GFX_RGB565(0x00, 0xFF, 0xFF)
#define GFX_MAGENTA     GFX_RGB565(0xFF, 0x00, 0xFF)
#define GFX_YELLOW      GFX_RGB565(0xFF, 0xFF, 0x00)
#define GFX_ORANGE      GFX_RGB565(0xFF, 0x80, 0x00)
#define GFX_GRAY        GFX_RGB565(0x80, 0x80, 0x80)
#define GFX_DARK_GRAY   GFX_RGB565(0x30, 0x30, 0x30)
#define GFX_NAVY        GFX_RGB565(0x00, 0x20, 0x60)
