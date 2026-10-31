/*
 * Permanent HTTP endpoint with the current readings.
 *
 * Runs while the device is logged into the home network - the configuration
 * mode (configmode.c) is a separate server on the device's own access point and
 * never runs at the same time.
 *
 *   GET /api/values   JSON, for other devices and scripts
 *   GET /api          same as /api/values
 *   GET /              small page for a browser, refreshes every 10 s
 *
 * The page is also served on a second port (WEBSERVER_PUBLIC_PORT, default
 * 8080) that knows nothing but "/" - a port forward from the internet to that
 * port therefore cannot reach the JSON endpoint, and the page there does not
 * even mention it.  The internal port 80 keeps the complete set.
 *
 * The JSON is built by hand (no cJSON in ESP-IDF 6.1) and uses a decimal point,
 * the HTML page a decimal comma:
 *
 * {
 *   "name": "Raumthermometer",
 *   "valid": true,                 false = no sensor reading yet
 *   "temperature_c": 23.6,         includes the calibration offset
 *   "humidity_rh": 47.0,
 *   "pressure_hpa": 966.8,
 *   "offset_c": -0.5,              configured calibration offset
 *   "age_s": 3,                    seconds since the reading was taken
 *   "time": "18.10. 22:15",        local time, fallback "01.01. 00:00"
 *   "ip": "192.168.123.122",
 *   "rssi_dbm": -42,
 *   "uptime_s": 1234,
 *   "ssid": "FRITZ!Box 7590",      access point in use
 *   "bssid": "2c:91:ab:5e:e5:6a",  its address - a mesh has several
 *   "channel": 1,
 *   "remote": "",                  outdoor sensor as configured
 *   "outdoor_c": 5.8               its temperature, null while unknown
 * }
 *
 * The values come from the cache in sensor.c, so a request never blocks on the
 * I2C bus and never delays the display.
 */

#pragma once

#include "esp_err.h"

/**
 * Port of the second, read-only page server.  Forward this port in the router
 * if the page should be reachable from outside; the JSON endpoint exists only
 * on port 80 and stays in the local network.  Set to 0 to switch the second
 * server off.
 */
#define WEBSERVER_PUBLIC_PORT 8080

/** Start the server (port 80).  Needs an IP address, so call it after wifi_connect(). */
esp_err_t webserver_start(void);

/**
 * Stop the server again.  The configuration mode runs its own server on port 80
 * (through the device's access point), and two listeners on the same port would
 * answer each other's requests.
 */
void webserver_stop(void);
