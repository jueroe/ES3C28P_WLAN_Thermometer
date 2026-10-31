/*
 * Network and site settings that survive a reboot.
 *
 * The compile time values in connect.h are only the defaults: as soon as the
 * WLAN configuration page stored something in NVS, that wins.  So a device can
 * be moved to another network without a rebuild, and connect.h is only needed
 * for the very first start.
 *
 * NVS namespace "net", keys "ssid", "pass", "lat", "lon" and "skip".
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define NETCFG_SSID_MAX      32
#define NETCFG_PASSWORD_MAX  64
#define NETCFG_COORD_MAX     24
#define NETCFG_NAME_MAX      32
#define NETCFG_REMOTE_MAX    63

/* Calibration offset added to every temperature reading, in tenths of a degree
 * (NVS stores an integer).  ±5.0 °C is plenty for a sensor placement error. */
#define NETCFG_OFFSET_LIMIT_TENTHS 50

typedef struct {
    char ssid[NETCFG_SSID_MAX + 1];
    char password[NETCFG_PASSWORD_MAX + 1];
    char latitude[NETCFG_COORD_MAX];
    char longitude[NETCFG_COORD_MAX];
    char name[NETCFG_NAME_MAX + 1];     /* shown in the browser, router and log */
    char remote[NETCFG_REMOTE_MAX + 1]; /* outdoor sensor, empty = none */
    int16_t offset_tenths;              /* temperature calibration, 0.1 °C steps */
} netcfg_t;

/**
 * Bring up the NVS partition.  Call once before anything else touches NVS;
 * repeated calls are free.
 */
esp_err_t netcfg_init(void);

/**
 * Fill `out` with the settings in use: the connect.h defaults first, then
 * whatever NVS holds.
 *
 * @return true when the values came from NVS (WLAN configured on the device)
 */
bool netcfg_load(netcfg_t *out);

/** Store the settings and clear the "configuration skipped" mark. */
esp_err_t netcfg_save(const netcfg_t *cfg);

/**
 * The user left the configuration screen without saving.  While this is set the
 * device starts without WLAN instead of opening the access point again, so the
 * readout still works without a network.
 */
bool netcfg_skipped(void);
void netcfg_set_skipped(bool skipped);

/** Effective coordinates in decimal degrees, as text (see netcfg_t). */
const char *netcfg_latitude(void);
const char *netcfg_longitude(void);

/** Name of this unit for the browser, the router and the log ("Raumthermometer"). */
const char *netcfg_device_name(void);

/**
 * Host name for DHCP/mDNS, derived from the device name: ASCII, lower case,
 * spaces and punctuation replaced by "-" ("Raumthermometer Küche" ->
 * "raumthermometer-kueche").  Falls back to "raumthermometer".
 */
void netcfg_hostname(char *out, size_t size);

/** IP or host name of the outdoor sensor, "" when none was configured. */
const char *netcfg_remote(void);

/**
 * Calibration offset for the temperature, in tenths of a degree (-15 = -1.5 °C).
 * sensor.c adds it to every reading, so display and JSON agree.
 */
int netcfg_temp_offset_tenths(void);

/** Build the sunrise-sunset.org request URL from the effective coordinates. */
void netcfg_sun_url(char *out, size_t size);
