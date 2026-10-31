#include "netcfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "connect.h"

static const char *TAG = "netcfg";

#define NETCFG_NAMESPACE  "net"
#define NETCFG_KEY_SSID   "ssid"
#define NETCFG_KEY_PASS   "pass"
#define NETCFG_KEY_LAT    "lat"
#define NETCFG_KEY_LON    "lon"
#define NETCFG_KEY_NAME   "name"
#define NETCFG_KEY_REMOTE "remote"
#define NETCFG_KEY_OFFSET "off"
#define NETCFG_KEY_SKIP   "skip"

/* Long enough for the longest value (password: 63 characters plus NUL) */
#define NETCFG_VALUE_MAX  80

static bool s_ready;
static bool s_loaded;
static bool s_from_nvs;
static netcfg_t s_cfg;

esp_err_t netcfg_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS had to be erased (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

static void set_defaults(netcfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->ssid, sizeof(cfg->ssid), "%s", CONNECT_WIFI_SSID);
    snprintf(cfg->password, sizeof(cfg->password), "%s", CONNECT_WIFI_PASSWORD);
    snprintf(cfg->latitude, sizeof(cfg->latitude), "%s", CONNECT_LATITUDE_TEXT);
    snprintf(cfg->longitude, sizeof(cfg->longitude), "%s", CONNECT_LONGITUDE_TEXT);
    snprintf(cfg->name, sizeof(cfg->name), "%s", CONNECT_DEVICE_NAME);
}

/* Read one string key into `out`.  A missing or empty key keeps the default. */
static void read_string(nvs_handle_t handle, const char *key, char *out, size_t size)
{
    char value[NETCFG_VALUE_MAX];
    size_t length = sizeof(value);
    if (nvs_get_str(handle, key, value, &length) == ESP_OK && value[0] != '\0') {
        strlcpy(out, value, size);
    }
}

static void load_once(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    set_defaults(&s_cfg);

    nvs_handle_t handle;
    if (netcfg_init() != ESP_OK || nvs_open(NETCFG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    char ssid[NETCFG_VALUE_MAX];
    size_t length = sizeof(ssid);
    if (nvs_get_str(handle, NETCFG_KEY_SSID, ssid, &length) == ESP_OK && ssid[0] != '\0') {
        strlcpy(s_cfg.ssid, ssid, sizeof(s_cfg.ssid));
        s_from_nvs = true;
        read_string(handle, NETCFG_KEY_PASS, s_cfg.password, sizeof(s_cfg.password));
        read_string(handle, NETCFG_KEY_LAT, s_cfg.latitude, sizeof(s_cfg.latitude));
        read_string(handle, NETCFG_KEY_LON, s_cfg.longitude, sizeof(s_cfg.longitude));
    }

    /* name and outdoor sensor can be set on their own, without touching the
     * network data */
    read_string(handle, NETCFG_KEY_NAME, s_cfg.name, sizeof(s_cfg.name));
    read_string(handle, NETCFG_KEY_REMOTE, s_cfg.remote, sizeof(s_cfg.remote));

    int16_t offset = 0;
    if (nvs_get_i16(handle, NETCFG_KEY_OFFSET, &offset) == ESP_OK) {
        if (offset < -NETCFG_OFFSET_LIMIT_TENTHS) {
            offset = -NETCFG_OFFSET_LIMIT_TENTHS;
        } else if (offset > NETCFG_OFFSET_LIMIT_TENTHS) {
            offset = NETCFG_OFFSET_LIMIT_TENTHS;
        }
        s_cfg.offset_tenths = offset;
    }
    nvs_close(handle);
}

bool netcfg_load(netcfg_t *out)
{
    load_once();
    if (out != NULL) {
        *out = s_cfg;
    }
    return s_from_nvs;
}

esp_err_t netcfg_save(const netcfg_t *cfg)
{
    if (cfg == NULL || cfg->ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (netcfg_init() != ESP_OK) {
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NETCFG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NETCFG_KEY_SSID, cfg->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NETCFG_KEY_PASS, cfg->password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NETCFG_KEY_LAT, cfg->latitude);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NETCFG_KEY_LON, cfg->longitude);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NETCFG_KEY_NAME, cfg->name);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NETCFG_KEY_REMOTE, cfg->remote);
    }
    if (err == ESP_OK) {
        err = nvs_set_i16(handle, NETCFG_KEY_OFFSET, cfg->offset_tenths);
    }
    /* a fresh configuration clears "start without WLAN" */
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NETCFG_KEY_SKIP, 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storing the network settings failed: %s", esp_err_to_name(err));
        return err;
    }

    s_cfg = *cfg;
    s_from_nvs = true;
    s_loaded = true;
    ESP_LOGI(TAG, "stored \"%s\": WLAN \"%s\", location %s / %s, offset %d.%d C, outdoor sensor %s",
             s_cfg.name, s_cfg.ssid, s_cfg.latitude, s_cfg.longitude,
             s_cfg.offset_tenths / 10, abs(s_cfg.offset_tenths % 10),
             s_cfg.remote[0] != '\0' ? s_cfg.remote : "-");
    return ESP_OK;
}

bool netcfg_skipped(void)
{
    if (netcfg_init() != ESP_OK) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NETCFG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t skipped = 0;
    const esp_err_t err = nvs_get_u8(handle, NETCFG_KEY_SKIP, &skipped);
    nvs_close(handle);
    return err == ESP_OK && skipped != 0;
}

void netcfg_set_skipped(bool skipped)
{
    if (netcfg_init() != ESP_OK) {
        return;
    }

    nvs_handle_t handle;
    if (nvs_open(NETCFG_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    const esp_err_t err = nvs_set_u8(handle, NETCFG_KEY_SKIP, skipped ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "configuration skipped: %s", skipped ? "yes" : "no");
}

const char *netcfg_latitude(void)
{
    load_once();
    return s_cfg.latitude;
}

const char *netcfg_longitude(void)
{
    load_once();
    return s_cfg.longitude;
}

void netcfg_sun_url(char *out, size_t size)
{
    load_once();
    snprintf(out, size, "https://api.sunrise-sunset.org/json?lat=%s&lng=%s&formatted=0",
             s_cfg.latitude, s_cfg.longitude);
}

const char *netcfg_device_name(void)
{
    load_once();
    return s_cfg.name;
}

const char *netcfg_remote(void)
{
    load_once();
    return s_cfg.remote;
}

int netcfg_temp_offset_tenths(void)
{
    load_once();
    return s_cfg.offset_tenths;
}

/* One byte of a host name: lower case letters and digits stay, everything else
 * becomes a separating "-" (never two in a row, never at the ends). */
static void add_host_byte(char *out, size_t size, size_t *used, char byte)
{
    char mapped;
    if (byte >= 'A' && byte <= 'Z') {
        mapped = (char)(byte - 'A' + 'a');
    } else if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')) {
        mapped = byte;
    } else {
        mapped = '-';
    }

    if (mapped == '-' && (*used == 0 || out[*used - 1] == '-')) {
        return;
    }
    if (*used + 1 < size) {
        out[(*used)++] = mapped;
        out[*used] = '\0';
    }
}

void netcfg_hostname(char *out, size_t size)
{
    load_once();
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';

    size_t used = 0;
    const unsigned char *cursor = (const unsigned char *)s_cfg.name;
    for (size_t i = 0; cursor[i] != '\0'; i++) {
        const unsigned char byte = cursor[i];

        /* German umlauts are two bytes in UTF-8; spell them out */
        if (byte == 0xC3 && cursor[i + 1] != '\0') {
            const char *replacement = NULL;
            switch (cursor[i + 1]) {
            case 0xA4:
            case 0x84: replacement = "ae"; break; /* ä Ä */
            case 0xB6:
            case 0x96: replacement = "oe"; break; /* ö Ö */
            case 0xBC:
            case 0x9C: replacement = "ue"; break; /* ü Ü */
            case 0x9F: replacement = "ss"; break; /* ß */
            default: break;
            }
            i++;
            if (replacement != NULL) {
                for (const char *r = replacement; *r != '\0'; r++) {
                    add_host_byte(out, size, &used, *r);
                }
            } else {
                add_host_byte(out, size, &used, '-');
            }
            continue;
        }
        if (byte >= 0x80) {
            add_host_byte(out, size, &used, '-');
            continue;
        }
        add_host_byte(out, size, &used, (char)byte);
    }

    while (used > 0 && out[used - 1] == '-') {
        out[--used] = '\0';
    }
    if (used == 0) {
        snprintf(out, size, "%s", "raumthermometer");
    }
}
