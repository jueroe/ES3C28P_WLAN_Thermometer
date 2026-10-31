/*
 * Sunrise and sunset from sunrise-sunset.org.
 *
 * The request is built from the effective coordinates in netcfg.h (NVS, or the
 * latitude/longitude in connect.h as defaults), the answer is JSON.  Only the
 * two fields "sunrise" and "sunset" are used; they arrive as UTC timestamps
 * ("2026-10-18T05:38:23+00:00") and are converted to local time, so the result
 * follows the time zone from connect.h including summer time.
 *
 * The values end up in clock.c (clock_set_sun_times()), the rest of the program
 * reads them with clock_sunrise() / clock_sunset().
 *
 * HTTPS needs a valid system clock (certificate check), so call this after the
 * NTP sync.
 */

#pragma once

#include "esp_err.h"

/**
 * Fetch sunrise and sunset, convert them to local time and store them.
 *
 * @return ESP_OK on success, an error from the HTTP client or the parser otherwise
 */
esp_err_t sun_fetch(void);
