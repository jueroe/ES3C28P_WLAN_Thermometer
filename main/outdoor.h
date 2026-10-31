/*
 * Outdoor temperature from a second thermometer on the network.
 *
 * The partner unit (an ESP32-C3 with a SHT31, see PORTING.md) answers on
 * GET /api/values with the same JSON this unit produces:
 *
 *   {"name":"Außenthermometer","valid":true,"temperature_c":5.7,
 *    "humidity_rh":84.0,"age_s":5,...}
 *
 * Only "valid" and "temperature_c" are needed here.  The address comes from the
 * configuration page (NVS key "remote", see netcfg.h) and may be an IP or a
 * host name - "192.168.123.191" as well as "aussenthermometer.local".
 *
 * The value is cached with a timestamp, so the display task never waits for the
 * network: it reads the cache while the network task refreshes it.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "gfx.h"

/* Seconds between two queries.  Outdoor temperatures change slowly, and every
 * query costs a TCP connection. */
#define OUTDOOR_REFRESH_S  60

/* Give up on a partner unit that does not answer within this time */
#define OUTDOOR_TIMEOUT_MS 5000

/**
 * Create the lock for the cached value.  Call once from app_main.
 */
esp_err_t outdoor_init(void);

/**
 * Fetch the temperature from the configured partner unit and store it.
 * Does nothing (and returns ESP_ERR_NOT_FOUND) when no address is configured.
 */
esp_err_t outdoor_fetch(void);

/**
 * Cached value from the last successful fetch.
 *
 * @param temperature_c receives the temperature, may be NULL
 * @param age_ms        receives the age of that reading, may be NULL
 * @return false while no value was received yet (no address, or the partner
 *         unit has not answered)
 */
bool outdoor_last(float *temperature_c, int64_t *age_ms);

/**
 * Draw "außen xx,x°" centred under the room temperature.
 *
 * Does nothing when no value is cached, so the line stays empty until the
 * partner unit answered once.  Positions and sizes come from layout.h.
 */
void outdoor_draw(gfx_canvas_t *canvas, uint16_t color);
