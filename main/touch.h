/* FT6336G capacitive touch panel of the ES3C28P board. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool pressed;
    uint16_t raw_x; /* controller value in panel pixels, useful for checking the board header */
    uint16_t raw_y;
    uint16_t strength; /* fixed non-zero value: a capacitive panel has no pressure reading */
    int x;             /* screen coordinate, only valid when pressed */
    int y;
} touch_state_t;

/** Bring up the shared I2C bus and the controller. */
esp_err_t touch_init(void);

/** True once touch_init() succeeded. */
bool touch_is_available(void);

/**
 * Read the panel.  Returns ESP_OK also when nothing is pressed
 * (state->pressed is false in that case).
 */
esp_err_t touch_read(touch_state_t *state);
