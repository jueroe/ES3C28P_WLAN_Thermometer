#include "sun.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "clock.h"
#include "connect.h"
#include "netcfg.h"

static const char *TAG = "sun";

/* The answer of sunrise-sunset.org is ~700 bytes */
#define SUN_RESPONSE_SIZE 1024

/* Days since 1970-01-01; newlib has no timegm(), so the UTC timestamp from the
 * JSON is converted by hand and localtime_r() applies the time zone. */
static bool utc_to_epoch(int year, int month, int day, int hour, int minute, int second,
                         time_t *out)
{
    static const int days_per_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 60) {
        return false;
    }

    long days = 0;
    for (int y = 1970; y < year; y++) {
        const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    for (int m = 1; m < month; m++) {
        days += days_per_month[m - 1] + ((m == 2 && leap) ? 1 : 0);
    }
    days += day - 1;

    *out = (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

/* "2026-10-18T05:38:23+00:00" -> local hour/minute */
static bool parse_iso_to_local(const char *iso, int *hour, int *minute)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hh, &mm, &ss) != 6) {
        ESP_LOGE(TAG, "cannot read the timestamp \"%s\"", iso);
        return false;
    }

    time_t stamp = 0;
    if (!utc_to_epoch(year, month, day, hh, mm, ss, &stamp)) {
        ESP_LOGE(TAG, "timestamp \"%s\" out of range", iso);
        return false;
    }

    /* the API reports UTC ("+00:00"), but accept any offset and correct for it */
    const char *clock_part = strchr(iso, 'T');
    const char *offset = clock_part != NULL ? strpbrk(clock_part, "+-") : NULL;
    if (offset != NULL && strlen(offset) >= 6) {
        int offset_hour = 0;
        int offset_minute = 0;
        if (sscanf(offset + 1, "%2d:%2d", &offset_hour, &offset_minute) == 2) {
            const long seconds = offset_hour * 3600L + offset_minute * 60L;
            stamp -= offset[0] == '-' ? -seconds : seconds;
        }
    }

    struct tm local;
    localtime_r(&stamp, &local);
    *hour = local.tm_hour;
    *minute = local.tm_min;
    return true;
}

/* Copy the value of "key":"value" out of the JSON text.  The reply of this one
 * API has a fixed, flat shape, so a full JSON parser would be overkill. */
static bool json_string(const char *json, const char *key, char *out, size_t size)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *start = strstr(json, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }

    const size_t length = (size_t)(end - start);
    if (length == 0 || length >= size) {
        return false;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

static esp_err_t http_get(const char *url, char *body, size_t size)
{
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = CONNECT_SUN_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "e32r28t-thermometer",
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "cannot create the HTTP client");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    const int content_length = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "server answered %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (content_length >= (int)size) {
        ESP_LOGE(TAG, "answer is %d bytes, the buffer holds %u", content_length, (unsigned)size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    int total = 0;
    while (total < (int)size - 1) {
        const int read = esp_http_client_read(client, body + total, (int)size - 1 - total);
        if (read <= 0) {
            break;
        }
        total += read;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total <= 0) {
        ESP_LOGE(TAG, "empty answer");
        return ESP_ERR_INVALID_RESPONSE;
    }
    body[total] = '\0';
    ESP_LOGD(TAG, "answer: %s", body);
    return ESP_OK;
}

esp_err_t sun_fetch(void)
{
    char *body = malloc(SUN_RESPONSE_SIZE);
    if (body == NULL) {
        ESP_LOGE(TAG, "no memory for the %d byte answer", SUN_RESPONSE_SIZE);
        return ESP_ERR_NO_MEM;
    }

    char url[160];
    netcfg_sun_url(url, sizeof(url));

    esp_err_t err = http_get(url, body, SUN_RESPONSE_SIZE);
    if (err == ESP_OK) {
        char status[16];
        if (!json_string(body, "status", status, sizeof(status)) || strcmp(status, "OK") != 0) {
            ESP_LOGE(TAG, "API did not answer with status OK");
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    int rise_hour = 0;
    int rise_minute = 0;
    int set_hour = 0;
    int set_minute = 0;

    if (err == ESP_OK) {
        char sunrise[32];
        char sunset[32];
        if (!json_string(body, "sunrise", sunrise, sizeof(sunrise)) ||
            !json_string(body, "sunset", sunset, sizeof(sunset))) {
            ESP_LOGE(TAG, "answer has no sunrise/sunset");
            err = ESP_ERR_INVALID_RESPONSE;
        } else if (!parse_iso_to_local(sunrise, &rise_hour, &rise_minute) ||
                   !parse_iso_to_local(sunset, &set_hour, &set_minute)) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    free(body);

    if (err != ESP_OK) {
        return err;
    }

    clock_set_sun_times(rise_hour, rise_minute, set_hour, set_minute);
    ESP_LOGI(TAG, "sunrise %02d:%02d, sunset %02d:%02d (%s)", rise_hour, rise_minute, set_hour,
             set_minute, CONNECT_TZ_NAME);
    return ESP_OK;
}
