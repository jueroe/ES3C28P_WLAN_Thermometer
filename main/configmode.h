/*
 * WLAN configuration mode.
 *
 * Started when the device cannot reach its network, or by holding a finger in
 * the upper right corner of the readout (LAYOUT_CONFIG_TRIGGER_* in layout.h).
 * The mode
 *
 *   - opens the access point CONNECT_AP_SSID (password CONNECT_AP_PASSWORD),
 *   - serves a small page at http://192.168.4.1 to pick a network, type the
 *     password and optionally change the coordinates for the sun times,
 *   - shows on the display how to reach that page and where the back button is,
 *   - stores the result in NVS (see netcfg.h).
 *
 * Saving on the web page and the back button both end in a restart: the normal
 * boot then comes up with the new settings, and the network task never has to
 * be restarted in place.  The back button additionally sets the "skipped" mark,
 * so a device without a network still shows the readout instead of opening the
 * access point again on every boot.
 */

#pragma once

#include <stdbool.h>

#include "esp_lcd_panel_ops.h"

#include "gfx.h"

/**
 * Ask for the configuration mode from the network task.  The display task picks
 * this up with configmode_requested() and calls configmode_run().
 */
void configmode_request(void);

/** True once configmode_request() was called. */
bool configmode_requested(void);

/**
 * True when a finger rests in the upper right corner long enough to switch to
 * the configuration mode.  Poll every ~100 ms from the display task.
 */
bool configmode_corner_touched(void);

/**
 * Run the configuration mode: access point, web page, display and touch.
 * Never returns - the device restarts when the user leaves the mode.
 */
void configmode_run(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel);
