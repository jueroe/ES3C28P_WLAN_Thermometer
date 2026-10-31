/* UNIT ENV III on the shared I2C bus: SHT30 for temperature/humidity and
 * QMP6988 for barometric pressure. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float temperature_c; /* °C, from the SHT30 */
    float humidity_rh;   /* % relative humidity, from the SHT30 */
    float pressure_hpa;  /* hPa, from the QMP6988 */
} sensor_reading_t;

/** Bring up the shared I2C bus and both sensors. */
esp_err_t sensor_init(void);

/** True once sensor_init() succeeded. */
bool sensor_is_available(void);

/**
 * Read all three values.  Returns ESP_ERR_INVALID_STATE if not initialised.
 *
 * The reading is also kept as "the last one", so other tasks (the HTTP endpoint)
 * can pick it up without touching the I2C bus: see sensor_last().
 */
esp_err_t sensor_read(sensor_reading_t *reading);

/**
 * Copy the last successful reading and its age.  Safe to call from any task;
 * no bus access.
 *
 * @param age_ms receives the milliseconds since that reading
 * @return false when no reading was taken yet
 */
bool sensor_last(sensor_reading_t *reading, int64_t *age_ms);
