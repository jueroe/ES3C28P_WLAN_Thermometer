/*
 * Site and network configuration.
 *
 * Everything here belongs to the installation, not to the program: change it
 * for another network or another place.  All values are compile time constants,
 * so a change needs a rebuild (`.\idf build`).
 *
 * The WLAN data and the coordinates are only the defaults for the first start:
 * as soon as the configuration page (touch in the upper right corner, or the
 * access point that starts when the network is out of reach) stored something
 * in NVS, the stored values win - no rebuild needed.
 *
 * This file contains the WiFi password.  It is listed in .gitignore and is not
 * part of the repository - main/connect.example.h is the template that is
 * committed.  Keep it that way before publishing the project.
 */

#pragma once

/* ------------------------------------------------------------------------- */
/* WLAN                                                                      */
/* ------------------------------------------------------------------------- */

/* Access point the device logs into (2.4 GHz only, the ESP32 has no 5 GHz).
 * Defaults for the first start - see netcfg.h */
#define CONNECT_WIFI_SSID       "FRITZ!Box 7590 j.oe"
#define CONNECT_WIFI_PASSWORD   "julianunddaniel"

/* Name of this unit: web page, JSON, log and the DHCP host name the router
 * shows.  Default for the first start, changeable on the configuration page -
 * with several units in the house give each one its own name. */
#define CONNECT_DEVICE_NAME     "Raumthermometer"

/* How long the background task waits for an IP address before it gives up */
#define CONNECT_WIFI_TIMEOUT_MS 20000

/* Weakest signal accepted for the stored network.  When the configured access
 * point is not in the scan result with at least this strength, the device skips
 * the connection attempt and opens the configuration access point instead. */
#define CONNECT_MIN_RSSI        (-85)

/* Access point of the device itself while the WLAN is being configured.  The
 * password needs at least 8 characters; an empty string makes it an open
 * network (anyone in range could then read the WLAN password). */
#define CONNECT_AP_SSID         "thermometer"
#define CONNECT_AP_PASSWORD     "thermometer"

/* ------------------------------------------------------------------------- */
/* Uhrzeit                                                                   */
/* ------------------------------------------------------------------------- */

/* NTP server.  "de.pool.ntp.org" answers from a German server pool */
#define CONNECT_SNTP_SERVER     "de.pool.ntp.org"

/* How long the background task waits for the first NTP answer.  The readout
 * runs all the time, so a long wait only delays the log line. */
#define CONNECT_SNTP_WAIT_MS    30000

/* POSIX time zone rule, used with setenv("TZ", ...): CET/CEST with the EU
 * summer time switch (last Sunday in March / October, 02:00 UTC).
 * CONNECT_TZ_NAME is only a label for the log output. */
#define CONNECT_TZ              "CET-1CEST,M3.5.0,M10.5.0/3"
#define CONNECT_TZ_NAME         "Europe/Berlin"

/* ------------------------------------------------------------------------- */
/* Standort (für Sonnenauf-/-untergang)                                      */
/* ------------------------------------------------------------------------- */

/* HIER wird der Standort eingestellt - die beiden Werte gehen in die Anfrage an
 * sunrise-sunset.org.  Dezimalgrad mit Punkt, Nord/Ost positiv, Süd/West
 * negativ (z. B. "48.41490736914154" / "11.099721982616911").  Als Text, damit
 * die URL ohne Float-Formatierung zusammengesetzt werden kann.
 * Auch diese beiden Werte lassen sich auf der Konfigurationsseite ändern; hier
 * stehen nur die Vorgaben. */
#define CONNECT_LATITUDE_TEXT   "48.41490736914154"
#define CONNECT_LONGITUDE_TEXT  "11.099721982616911"

/* Abfrage der Sonnenzeiten: Timeout und Abstand zwischen zwei Abfragen
 * (6 h - die Werte ändern sich nur täglich, ein Fehlversuch wird so aber bald
 * wiederholt).  Die Adresse wird aus den Koordinaten oben gebaut, siehe
 * netcfg_sun_url(). */
#define CONNECT_SUN_TIMEOUT_MS  10000
#define CONNECT_SUN_REFRESH_S   21600
