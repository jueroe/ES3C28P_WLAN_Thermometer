/*
 * Room thermometer / hygrometer / barometer on the QDtech / LCDWiki
 * "2.8inch ESP32-S3 Display" (SKU ES3C28P):
 *
 *   ESP32-S3R8 (16 MB flash, 8 MB PSRAM), ILI9341V 240x320 IPS portrait,
 *   FT6336G capacitive touch, UNIT ENV III (SHT30 + QMP6988).
 *
 * The application shows a wallpaper chosen by season and time of day and the
 * current temperature as "xx,x°" in the large glass tile.  The wallpaper files
 * are packed into the littlefs partition "storage" at build time (data/).
 *
 * The hardware test screen (test pattern, touch dots, uptime) lives in
 * testscreen.c and can be switched on with APP_SHOW_TESTSCREEN below, or
 * removed from the project altogether.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "background.h"
#include "bigdigit.h"
#include "board_es3c28p.h"
#include "clock.h"
#include "configmode.h"
#include "connect.h"
#include "discovery.h"
#include "gfx.h"
#include "infotile.h"
#include "layout.h"
#include "netcfg.h"
#include "outdoor.h"
#include "sensor.h"
#include "statusled.h"
#include "storage.h"
#include "sun.h"
#include "testscreen.h"
#include "touch.h"
#include "ttf.h"
#include "wallpaper.h"
#include "watchdog.h"
#include "webserver.h"
#include "wifi.h"

static const char *TAG = "cyd";

/* 1 = hardware test screen instead of the thermometer readout */
#define APP_SHOW_TESTSCREEN 0

/* 1 = go straight to the WLAN configuration mode after boot, without waiting
 * for a network problem and without touching the corner (for testing the
 * access point and the web page).  While this is 1 the device NEVER connects -
 * do not leave it on. */
#define APP_FORCE_CONFIG 0

#if APP_FORCE_CONFIG
#warning "APP_FORCE_CONFIG is 1: this build always starts the WLAN configuration"
#endif

/* TEST ONLY: seconds after the network came up to jump into the configuration
 * mode, or 0 for normal operation.  Used to check that the configuration server
 * really takes over port 80 from the readings endpoint. */
#define APP_TEST_CONFIG_AFTER_CONNECT_S 0

/* The wallpaper slot now follows season and time of day, see wallpaper.h.
 * Seconds between sensor reads (a thermometer does not need more) */
#define SENSOR_INTERVAL_S 10

/* The display task wakes up this often to look for a touch in the corner of the
 * readout (configuration mode).  Sensor and background keep their 10 s cycle. */
#define APP_TICK_MS 100

/* Shown in the info tile as long as the NTP clock has no time yet: 01.01. 00:00
 * (the year is not part of the line, the tile shows dd.mm. hh:mm) */
#define APP_FALLBACK_DAY     1
#define APP_FALLBACK_MONTH   1
#define APP_FALLBACK_HOUR    0
#define APP_FALLBACK_MINUTE  0

static esp_lcd_panel_handle_t s_panel;
static gfx_canvas_t s_canvas;

/* ------------------------------------------------------------------------- */
/* Backlight                                                                 */
/* ------------------------------------------------------------------------- */

static void backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = BOARD_BL_LEDC_MODE,
        .duty_resolution = BOARD_BL_LEDC_RESOLUTION,
        .timer_num = BOARD_BL_LEDC_TIMER,
        .freq_hz = BOARD_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    const ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_LCD_PIN_BACKLIGHT,
        .speed_mode = BOARD_BL_LEDC_MODE,
        .channel = BOARD_BL_LEDC_CHANNEL,
        .timer_sel = BOARD_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

static void backlight_set(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    const uint32_t duty = (uint32_t)((percent * BOARD_BL_MAX_DUTY) / 100);
    ESP_ERROR_CHECK(ledc_set_duty(BOARD_BL_LEDC_MODE, BOARD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BOARD_BL_LEDC_MODE, BOARD_BL_LEDC_CHANNEL));
}

/* ------------------------------------------------------------------------- */
/* LCD                                                                       */
/* ------------------------------------------------------------------------- */

static esp_err_t lcd_init(void)
{
    const spi_bus_config_t bus_config = {
        .sclk_io_num = BOARD_LCD_PIN_SCLK,
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = BOARD_LCD_PIN_MISO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BOARD_SCREEN_WIDTH * GFX_FLUSH_BAND_LINES * sizeof(uint16_t),
    };

    ESP_LOGI(TAG, "SPI%d for ILI9341: SCLK=%d MOSI=%d MISO=%d CS=%d DC=%d RST=EN",
             BOARD_LCD_HOST, BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_MISO,
             BOARD_LCD_PIN_CS, BOARD_LCD_PIN_DC);

    esp_err_t err = spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_config, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi: %s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_PIN_RST, /* GPIO_NUM_NC: shared with the ESP32 EN line */
        .rgb_ele_order = BOARD_LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_ili9341(io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_ili9341: %s", esp_err_to_name(err));
        return err;
    }

    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_init(s_panel)) != ESP_OK) {
        ESP_LOGE(TAG, "panel reset/init: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_swap_xy(s_panel, BOARD_LCD_LANDSCAPE);
    if (err == ESP_OK) {
        err = esp_lcd_panel_mirror(s_panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_invert_color(s_panel, BOARD_LCD_INVERT_COLORS);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel orientation: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "ILI9341 ready, %dx%d %s, %d MHz SPI clock, %s element order",
             BOARD_SCREEN_WIDTH, BOARD_SCREEN_HEIGHT,
             BOARD_LCD_LANDSCAPE ? "landscape" : "portrait",
             BOARD_LCD_PIXEL_CLOCK_HZ / 1000000,
             BOARD_LCD_BGR ? "BGR" : "RGB");
    return ESP_OK;
}

static void boot_button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
}

/* ------------------------------------------------------------------------- */
/* Thermometer screen                                                        */
/* ------------------------------------------------------------------------- */

static int s_shown_tenths;
static int s_shown_humidity_tenths;
static int s_shown_pressure_whole;
static int s_shown_clock;
static int s_shown_outdoor_tenths; /* INT_MIN until the partner unit answered */

/* How many bands still report their text pixel count at boot (diagnostics) */
static int s_text_log_budget = 6;

/* Current date and time for the display, with the fallback while NTP has not
 * answered yet */
static void shown_time(int *day, int *month, int *hour, int *minute)
{
    struct tm now;
    if (clock_local_now(&now)) {
        *day = now.tm_mday;
        *month = now.tm_mon + 1;
        *hour = now.tm_hour;
        *minute = now.tm_min;
    } else {
        *day = APP_FALLBACK_DAY;
        *month = APP_FALLBACK_MONTH;
        *hour = APP_FALLBACK_HOUR;
        *minute = APP_FALLBACK_MINUTE;
    }
}

/* Fill the info tile values from a sensor reading plus the current time. */
static void info_values_from(const sensor_reading_t *reading, infotile_values_t *info)
{
    info->humidity_rh = reading->humidity_rh;
    info->pressure_hpa = reading->pressure_hpa;
    shown_time(&info->day, &info->month, &info->hour, &info->minute);
}

/* Comparable number of the shown date/time, so a redraw happens exactly
 * when the minute changes */
static int clock_key(const infotile_values_t *info)
{
    return ((info->month * 32 + info->day) * 24 + info->hour) * 60 + info->minute;
}

/* Comparable number of the outdoor temperature shown, or OUTDOOR_KEY_NONE while
 * the partner unit has not answered yet */
#define OUTDOOR_KEY_NONE INT_MIN

static int outdoor_key(void)
{
    float temperature = 0.0f;
    if (!outdoor_last(&temperature, NULL)) {
        return OUTDOOR_KEY_NONE;
    }
    return (int)lroundf(temperature * 10.0f);
}

/* ------------------------------------------------------------------------- */
/* Wallpaper slot: season and time of day, one of three images picked at       */
/* random whenever the slot changes (rules in wallpaper.h)                     */
/* ------------------------------------------------------------------------- */

static season_t s_season;
static daytime_t s_daytime;
static int s_wallpaper_index;
static char s_wallpaper_path[128];
static uint16_t s_text_color;
static bool s_wallpaper_valid;

/* Pick a new wallpaper if season or time of day changed.
 * Returns true when the slot changed, so the caller has to redraw. */
static bool wallpaper_update(void)
{
    int day = 0;
    int month = 0;
    int hour = 0;
    int minute = 0;
    shown_time(&day, &month, &hour, &minute);

    const season_t season = wallpaper_season_from_month(month);
    const daytime_t daytime = wallpaper_daytime_for(month, hour, minute);
    if (s_wallpaper_valid && season == s_season && daytime == s_daytime) {
        return false;
    }

    s_season = season;
    s_daytime = daytime;
    s_wallpaper_index = wallpaper_random_index();
    s_text_color = backgrounds[season][daytime][s_wallpaper_index].textcolor;
    wallpaper_slot_path(s_wallpaper_path, sizeof(s_wallpaper_path), season, daytime,
                        s_wallpaper_index);
    s_wallpaper_valid = true;

    ESP_LOGI(TAG, "background %d/%d/%d at %02d.%02d. %02d:%02d: %s, text colour 0x%04X",
             season, daytime, s_wallpaper_index, day, month, hour, minute, s_wallpaper_path,
             s_text_color);

    /* the status LED doubles as ambilight: rainbow fade at night, dimmed white
     * during the day (see statusled.h) */
    const bool night = (daytime == DAYTIME_NIGHT);
    statusled_ambilight(night);
    ESP_LOGI(TAG, "status LED: %s", night ? "rainbow fade (night)" : "white, dimmed");
    return true;
}

/* stb_truetype can fail silently when a temporary buffer does not fit (see the
 * note in stb_truetype.h), so every band is checked for text pixels afterwards:
 * a frame without any would show an empty readout. */
static uint32_t count_text_pixels(const gfx_canvas_t *canvas, uint16_t text_color)
{
    uint32_t count = 0;
    const size_t pixels = (size_t)canvas->width * canvas->height;
    for (size_t i = 0; i < pixels; i++) {
        if (canvas->pixels[i] == text_color) {
            count++;
        }
    }
    return count;
}

/* Draw wallpaper and the tiles band by band, sending each band to the panel. */
static esp_err_t draw_thermometer(int tenths, const infotile_values_t *info)
{
    const int64_t start_us = esp_timer_get_time();
    const esp_err_t err = wallpaper_decode(s_wallpaper_path);
    if (err != ESP_OK) {
        return err;
    }
    const int64_t decoded_us = esp_timer_get_time();

    int used = 0;
    for (int band_y = 0; band_y < BOARD_SCREEN_HEIGHT; band_y += LAYOUT_BAND_HEIGHT) {
        gfx_canvas_set_origin(&s_canvas, band_y);
        const esp_err_t paint_err = wallpaper_paint(&s_canvas, band_y, LAYOUT_BAND_HEIGHT);
        if (paint_err != ESP_OK) {
            gfx_canvas_set_origin(&s_canvas, 0);
            return paint_err;
        }

        used = bigdigit_draw_number(&s_canvas, LAYOUT_TEMP_X, LAYOUT_TEMP_BASELINE_Y,
                                    LAYOUT_TEMP_MAX_WIDTH, tenths, s_text_color);
        bigdigit_draw_degree(&s_canvas, LAYOUT_TEMP_DEGREE_X, LAYOUT_TEMP_DEGREE_Y,
                             LAYOUT_TEMP_DIGIT_HEIGHT, s_text_color);
        outdoor_draw(&s_canvas, s_text_color);
        infotile_draw(&s_canvas, info, s_text_color);

        const uint32_t text_pixels = count_text_pixels(&s_canvas, s_text_color);
        if (text_pixels == 0) {
            ESP_LOGE(TAG, "band %d..%d has no text pixels - the readout stays empty", band_y,
                     band_y + LAYOUT_BAND_HEIGHT - 1);
        } else if (s_text_log_budget > 0) {
            s_text_log_budget--;
            ESP_LOGI(TAG, "band %d..%d: %u text pixels", band_y, band_y + LAYOUT_BAND_HEIGHT - 1,
                     (unsigned)text_pixels);
        }

        const esp_err_t flush_err = gfx_flush_all(&s_canvas, s_panel);
        if (flush_err != ESP_OK) {
            return flush_err;
        }
    }
    gfx_canvas_set_origin(&s_canvas, 0);

    ESP_LOGI(TAG, "frame: %d,%d degC, text from (%d,%d), %d px wide, degree at (%d,%d), decode %lld ms, total %lld ms",
             tenths / 10, tenths % 10, LAYOUT_TEMP_X, LAYOUT_TEMP_BASELINE_Y, used,
             LAYOUT_TEMP_DEGREE_X, LAYOUT_TEMP_DEGREE_Y,
             (long long)((decoded_us - start_us) / 1000),
             (long long)((esp_timer_get_time() - start_us) / 1000));
    return ESP_OK;
}

static void thermometer_task(void)
{
    sensor_reading_t reading = {0};
    infotile_values_t info;

    watchdog_subscribe();

    /* season and time of day decide the slot, the image is picked at random */
    wallpaper_update();

    if (!sensor_is_available() || sensor_read(&reading) != ESP_OK) {
        ESP_LOGW(TAG, "no sensor reading yet, showing 00,0");
    }
    s_shown_tenths = (int)lroundf(reading.temperature_c * 10.0f);
    s_shown_humidity_tenths = (int)lroundf(reading.humidity_rh * 10.0f);
    s_shown_pressure_whole = (int)lroundf(reading.pressure_hpa);
    info_values_from(&reading, &info);
    s_shown_clock = clock_key(&info);
    s_shown_outdoor_tenths = outdoor_key();

    if (draw_thermometer(s_shown_tenths, &info) != ESP_OK) {
        ESP_LOGE(TAG, "could not draw the thermometer screen");
    }

    for (;;) {
        /* Short steps so that a touch in the corner starts the configuration
         * mode immediately; the values themselves keep the 10 s cycle. */
        bool enter_config = false;
        for (int tick = 0; tick < SENSOR_INTERVAL_S * (1000 / APP_TICK_MS) && !enter_config;
             tick++) {
            vTaskDelay(pdMS_TO_TICKS(APP_TICK_MS));
            watchdog_feed();
            enter_config = configmode_requested() || configmode_corner_touched();
        }
        if (enter_config) {
            configmode_run(&s_canvas, s_panel); /* never returns, restarts the device */
        }

        /* the clock runs on its own, so refresh it even when the sensor fails;
         * a new background has to be drawn in any case */
        bool redraw = wallpaper_update();
        info_values_from(&reading, &info);
        const int clock_now = clock_key(&info);
        const int outdoor_now = outdoor_key();

        if (!sensor_is_available() || sensor_read(&reading) != ESP_OK) {
            ESP_LOGW(TAG, "sensor read failed");
            if (redraw || clock_now != s_shown_clock || outdoor_now != s_shown_outdoor_tenths) {
                s_shown_clock = clock_now;
                s_shown_outdoor_tenths = outdoor_now;
                if (draw_thermometer(s_shown_tenths, &info) != ESP_OK) {
                    ESP_LOGW(TAG, "redraw failed");
                }
            }
            continue;
        }

        char stamp[32];
        const int tenths = (int)lroundf(reading.temperature_c * 10.0f);
        ESP_LOGI(TAG, "%s ENV III: %.2f C, %.1f %%rH, %.1f hPa, IP %s",
                 clock_format(stamp, sizeof(stamp)), reading.temperature_c, reading.humidity_rh,
                 reading.pressure_hpa, wifi_ip_address());

        const int humidity_tenths = (int)lroundf(reading.humidity_rh * 10.0f);
        const int pressure_whole = (int)lroundf(reading.pressure_hpa);
        if (!redraw && tenths == s_shown_tenths && humidity_tenths == s_shown_humidity_tenths &&
            pressure_whole == s_shown_pressure_whole && clock_now == s_shown_clock &&
            outdoor_now == s_shown_outdoor_tenths) {
            continue;
        }
        s_shown_tenths = tenths;
        s_shown_humidity_tenths = humidity_tenths;
        s_shown_pressure_whole = pressure_whole;
        s_shown_clock = clock_now;
        s_shown_outdoor_tenths = outdoor_now;
        info_values_from(&reading, &info);
        if (draw_thermometer(tenths, &info) != ESP_OK) {
            ESP_LOGW(TAG, "redraw failed");
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Application                                                               */
/* ------------------------------------------------------------------------- */

/* Network comes up in the background: the readout starts immediately with the
 * fallback date and picks up the real time as soon as NTP answered. */
static void network_task(void *argument)
{
    (void)argument;

    netcfg_t cfg;
    const bool stored = netcfg_load(&cfg);
    ESP_LOGI(TAG, "WLAN \"%s\" from %s", cfg.ssid, stored ? "NVS" : "connect.h");
    ESP_LOGI(TAG, "device name \"%s\", offset %d.%d C, outdoor sensor %s", cfg.name,
             cfg.offset_tenths / 10, abs(cfg.offset_tenths % 10),
             cfg.remote[0] != '\0' ? cfg.remote : "not set");

    bool connected = false;
    if (wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi driver did not come up, continuing without network");
        vTaskDelete(NULL);
        return;
    }

    /* Every access point of the network is logged with its own strength: a mesh
     * runs several with the same name, and the driver does not always pick the
     * strongest.  The best one is then used explicitly.  A weak signal is no
     * reason to give up any more - it is only reported. */
    wifi_bss_t access_points[16];
    const int found = wifi_scan_all(access_points, 16);
    const uint8_t *best_bssid = NULL;
    int best_rssi = WIFI_RSSI_NOT_FOUND;

    for (int i = 0; i < found; i++) {
        const wifi_bss_t *ap = &access_points[i];
        ESP_LOGI(TAG, "scan: \"%s\" %02x:%02x:%02x:%02x:%02x:%02x channel %d, %d dBm", ap->ssid,
                 ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4],
                 ap->bssid[5], ap->channel, ap->rssi);
        if (strcmp(ap->ssid, cfg.ssid) == 0 && ap->rssi > best_rssi) {
            best_rssi = ap->rssi;
            best_bssid = ap->bssid;
        }
    }

    if (best_bssid == NULL) {
        ESP_LOGW(TAG, "\"%s\" is not in the scan result - trying anyway", cfg.ssid);
    } else if (best_rssi < CONNECT_MIN_RSSI) {
        ESP_LOGW(TAG, "\"%s\" is weak (%d dBm, below the %d dBm guideline) - trying anyway",
                 cfg.ssid, best_rssi, CONNECT_MIN_RSSI);
    } else {
        ESP_LOGI(TAG, "\"%s\" strongest at %d dBm", cfg.ssid, best_rssi);
    }

    if (best_bssid != NULL) {
        ESP_LOGI(TAG, "using the strongest access point of \"%s\"", cfg.ssid);
        connected =
            wifi_connect_bssid(cfg.ssid, cfg.password, best_bssid, CONNECT_WIFI_TIMEOUT_MS);
    }
    if (!connected) {
        connected = wifi_connect(cfg.ssid, cfg.password, CONNECT_WIFI_TIMEOUT_MS);
    }

    if (!connected) {
        if (netcfg_skipped()) {
            ESP_LOGW(TAG, "no network - the readout runs without time and sun times");
        } else {
            ESP_LOGW(TAG, "no network - opening the WLAN configuration");
            configmode_request();
        }
        vTaskDelete(NULL);
        return;
    }

    wifi_log_link();

    /* the readings endpoint comes up before the time is known: it reports the
     * fallback date until NTP answered */
    if (webserver_start() != ESP_OK) {
        ESP_LOGW(TAG, "the readings endpoint is not available");
    }

    /* announce <name>.local, so the unit is reachable without knowing the IP or
     * the router's domain */
    if (discovery_start() != ESP_OK) {
        ESP_LOGW(TAG, "the unit is not announced via mDNS");
    }

#if APP_TEST_CONFIG_AFTER_CONNECT_S > 0
    /* TEST ONLY: enter the configuration mode while the readings endpoint is
     * running, to check that it takes over port 80.  Set the macro to 0 when
     * done (see the top of this file). */
    vTaskDelay(pdMS_TO_TICKS(APP_TEST_CONFIG_AFTER_CONNECT_S * 1000));
    ESP_LOGW(TAG, "TEST: opening the WLAN configuration while the endpoint runs");
    configmode_request();
#endif
    clock_start_sntp();
    if (!clock_wait_for_sync(CONNECT_SNTP_WAIT_MS)) {
        /* the first query can get lost on a busy network; without a retry the
         * clock would stay at 1970 for the next poll interval (1 h) */
        for (int attempt = 0; attempt < 3 && !configmode_requested(); attempt++) {
            ESP_LOGW(TAG, "retrying the NTP query (%d/3)", attempt + 1);
            esp_sntp_restart();
            if (clock_wait_for_sync(CONNECT_SNTP_WAIT_MS)) {
                break;
            }
        }
    }
    clock_log_now();

    /* HTTPS needs the correct date for the certificate check, so the sun times
     * are fetched after the NTP sync.  The outdoor temperature is much more
     * short-lived, so the loop ticks every NETWORK_TICK_S and keeps two
     * separate schedules. */
#define NETWORK_TICK_S 30
    int64_t last_sun = -CONNECT_SUN_REFRESH_S;
    int64_t last_outdoor = -OUTDOOR_REFRESH_S;

    for (;;) {
        if (configmode_requested()) {
            /* the configuration mode owns the radio now - no point in talking
             * to servers that are out of reach */
            ESP_LOGI(TAG, "configuration mode active, the network task stops");
            vTaskDelete(NULL);
            return;
        }

        const int64_t now_s = esp_timer_get_time() / 1000000;

        if (netcfg_remote()[0] != '\0' && now_s - last_outdoor >= OUTDOOR_REFRESH_S) {
            last_outdoor = now_s;
            if (outdoor_fetch() != ESP_OK) {
                ESP_LOGW(TAG, "no outdoor temperature from %s", netcfg_remote());
            }
        }

        if (now_s - last_sun >= CONNECT_SUN_REFRESH_S) {
            last_sun = now_s;
            if (sun_fetch() != ESP_OK) {
                ESP_LOGW(TAG, "sun times not updated, retrying in %d s",
                         CONNECT_SUN_REFRESH_S);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(NETWORK_TICK_S * 1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "--- ES3C28P room thermometer, ESP-IDF %s ---", IDF_VER);
    ESP_LOGI(TAG, "heap: %d bytes free, largest free block %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    /* NVS holds the WLAN data, the location and the temperature offset, so it
     * has to be up before anything reads them */
    if (netcfg_init() != ESP_OK) {
        ESP_LOGW(TAG, "NVS not available - the WLAN cannot be configured on the device");
    }

    watchdog_init();

    backlight_init();
    backlight_set(0);
    statusled_init();
    statusled_self_test();
    statusled_start();
    boot_button_init();

    if (lcd_init() != ESP_OK) {
        ESP_LOGE(TAG, "LCD bring-up failed, stopping here");
        return;
    }

    if (gfx_canvas_init(&s_canvas, BOARD_SCREEN_WIDTH, LAYOUT_BAND_HEIGHT) != ESP_OK) {
        ESP_LOGE(TAG, "not enough memory for the %dx%d band canvas", BOARD_SCREEN_WIDTH,
                 LAYOUT_BAND_HEIGHT);
        return;
    }

    if (storage_init() == ESP_OK) {
        storage_log_summary();
    } else {
        ESP_LOGW(TAG, "wallpaper partition not available");
    }

    if (sensor_init() != ESP_OK) {
        ESP_LOGW(TAG, "ENV III not available, continuing without sensor values");
    }

    if (touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "touch not available");
    }

    if (outdoor_init() != ESP_OK) {
        ESP_LOGW(TAG, "the outdoor temperature stays empty");
    }

    if (ttf_init_builtin() != ESP_OK) {
        ESP_LOGE(TAG, "font could not be loaded, the readout stays empty");
    }

    /* Time zone first: the clock shows the local time as soon as NTP answers */
    clock_init();

#if APP_FORCE_CONFIG
    ESP_LOGE(TAG, "!! APP_FORCE_CONFIG is 1: always starting the WLAN configuration, "
                  "no connection attempt !!");
    configmode_request();
#else
    if (xTaskCreate(network_task, "network", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no memory for the network task - no WiFi and no NTP");
    }
#endif

    ESP_LOGI(TAG, "%d free heap bytes after initialisation", (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

#if APP_SHOW_TESTSCREEN
    backlight_set(100);
    testscreen_run(&s_canvas, s_panel); /* never returns */
#endif

    backlight_set(100);
    thermometer_task(); /* never returns */
}
