/*
 * Display layout of the thermometer screen.
 *
 * Everything a human might want to tweak lives here.  Coordinates are in the
 * portrait orientation: (0,0) is the top left corner, x grows to the right up
 * to BOARD_SCREEN_WIDTH-1 (239), y grows down to BOARD_SCREEN_HEIGHT-1 (319).
 *
 * After a change: build, flash, and check the "bigdigit:" and "infotile:" lines
 * in the serial log - they report the resulting sizes, scales and positions.
 */

#pragma once

#include "board_es3c28p.h"

/* ------------------------------------------------------------------------- */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------- */

/* The ESP32-S3 has enough internal RAM for a full frame (240 x 320 x 2 bytes =
 * 150 KB), so the whole picture is drawn in one pass: the JPEG decoder runs
 * once per frame instead of once per band (~30 ms saved).  The band code is
 * kept as an option - set this to 160 to draw in two bands again.
 * Must divide BOARD_SCREEN_HEIGHT. */
#define LAYOUT_BAND_HEIGHT          BOARD_SCREEN_HEIGHT

/* ------------------------------------------------------------------------- */
/* Temperature text: "xx,x"                                                  */
/* ------------------------------------------------------------------------- */

/* Left edge of the first big digit */
#define LAYOUT_TEMP_X               25

/* Baseline of the big digits, i.e. their bottom edge.  The comma tail and the
 * half size decimal digit end on this line as well. */
#define LAYOUT_TEMP_BASELINE_Y      130 //160

/* The group is condensed horizontally until it fits this width.  The space to
 * the right of it is where the degree sign sits. */
#define LAYOUT_TEMP_MAX_WIDTH       190

/* Height of the two digits in front of the comma, in pixels */
#define LAYOUT_TEMP_DIGIT_HEIGHT    100 //120

/* Strongest horizontal squeeze allowed.  If the width would need a stronger
 * squeeze, the digit height is reduced instead so the proportions stay
 * readable.  1.0 = never condense, 0.5 = very narrow. */
#define LAYOUT_TEMP_MIN_SQUEEZE     0.55f

/* Comma and decimal digit, relative to the digit height (0.5 = half size) */
#define LAYOUT_TEMP_DECIMAL_SCALE   0.5f

/* ------------------------------------------------------------------------- */
/* Degree sign                                                                */
/* ------------------------------------------------------------------------- */

/* Position of the top right corner of the drawn degree ink */
#define LAYOUT_TEMP_DEGREE_X        215
#define LAYOUT_TEMP_DEGREE_Y        40

/* Size of the degree sign, relative to LAYOUT_TEMP_DIGIT_HEIGHT.  The glyph is
 * small by design; 0.5 draws the ring at half the size the font would give at
 * digit height. */
#define LAYOUT_TEMP_DEGREE_SCALE    0.5f

/* ------------------------------------------------------------------------- */
/* Outdoor temperature: one centred line under the room temperature          */
/* ------------------------------------------------------------------------- */

/* Baseline of the line, i.e. the bottom edge of its capital letters.  The room
 * digits end at LAYOUT_TEMP_BASELINE_Y (130), the info tile starts at
 * LAYOUT_INFO_Y (205) - 180 leaves a calm band in between.
 *
 * The ink starts at baseline - cap height, so with a cap of 24 it begins at
 * y=156 and the top 4 rows fall into the first band while the rest is in the
 * second.  That is fine, because every band paints the whole frame and clips
 * the rows outside itself - just do not put the baseline exactly on a band
 * border, then the line would be cut in the log output by band. */
#define LAYOUT_OUT_TEMP_BASELINE_Y  170

/* Cap height, in pixels - the same size as the humidity line in the info tile */
#define LAYOUT_OUT_TEMP_CAP         24

/* Text in front of the value.  Set to "" for the bare number. */
#define LAYOUT_OUT_TEMP_PREFIX      "außen "

/* Widest the line may become (the text is condensed if it would be wider) */
#define LAYOUT_OUT_TEMP_MAX_WIDTH   190

/* Strongest horizontal squeeze; 1.0 = never condense */
#define LAYOUT_OUT_TEMP_MIN_SQUEEZE 0.6f

/* ------------------------------------------------------------------------- */
/* Info tile: humidity, pressure and date/time, one line each                */
/* ------------------------------------------------------------------------- */

/* Text area of the lower glass tile */
#define LAYOUT_INFO_X               25
#define LAYOUT_INFO_Y               205
#define LAYOUT_INFO_W               190
#define LAYOUT_INFO_H               90

/* Cap heights of the three lines, in pixels */
#define LAYOUT_INFO_HUMIDITY_CAP    24
#define LAYOUT_INFO_PRESSURE_CAP    18
#define LAYOUT_INFO_TIME_CAP        18

/* Vertical gap between the lines (the block is centred in the area) */
#define LAYOUT_INFO_GAP             8

/* Strongest horizontal squeeze per line; 1.0 = never condense */
#define LAYOUT_INFO_MIN_SQUEEZE     0.6f

/* ------------------------------------------------------------------------- */
/* WLAN configuration screen                                                 */
/* ------------------------------------------------------------------------- */

/* Touch in this rectangle (upper right corner of the readout) opens the
 * configuration screen.  The finger has to stay there for LAYOUT_CONFIG_HOLD_MS
 * so that brushing the corner does not switch the mode by accident. */
#define LAYOUT_CONFIG_TRIGGER_X     180
#define LAYOUT_CONFIG_TRIGGER_Y     0
#define LAYOUT_CONFIG_TRIGGER_W     60
#define LAYOUT_CONFIG_TRIGGER_H     60
#define LAYOUT_CONFIG_HOLD_MS       500

/* Back button across the whole lower edge */
#define LAYOUT_CONFIG_BACK_Y        264
#define LAYOUT_CONFIG_BACK_H        56

/* Text column of the configuration screen */
#define LAYOUT_CONFIG_TEXT_X        16
#define LAYOUT_CONFIG_TEXT_W        208

/* Cap heights */
#define LAYOUT_CONFIG_TITLE_CAP     22
#define LAYOUT_CONFIG_STATUS_CAP    12
#define LAYOUT_CONFIG_LABEL_CAP     14
#define LAYOUT_CONFIG_VALUE_CAP     26
#define LAYOUT_CONFIG_HINT_CAP      13
#define LAYOUT_CONFIG_BUTTON_CAP    18

/* Baselines (the baseline is the bottom edge of the capital letters) */
#define LAYOUT_CONFIG_TITLE_Y       40
#define LAYOUT_CONFIG_STATUS_Y      60
#define LAYOUT_CONFIG_LINE1_Y       70
#define LAYOUT_CONFIG_ROW1_LABEL_Y  96
#define LAYOUT_CONFIG_ROW1_VALUE_Y  132
#define LAYOUT_CONFIG_ROW1_HINT_Y   152
#define LAYOUT_CONFIG_LINE2_Y       166
#define LAYOUT_CONFIG_ROW2_LABEL_Y  192
#define LAYOUT_CONFIG_ROW2_VALUE_Y  228
#define LAYOUT_CONFIG_BUTTON_TEXT_Y 300
