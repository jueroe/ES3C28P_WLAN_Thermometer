#include "outdoor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_es3c28p.h"
#include "layout.h"
#include "netcfg.h"
#include "ttf.h"

static const char *TAG = "outdoor";

/* The answer of the partner unit is ~350 bytes */
#define OUTDOOR_RESPONSE_SIZE 512

/* U+00B0 DEGREE SIGN, UTF-8 (the same constant as in bigdigit.c) */
#define DEGREE_SIGN "\xC2\xB0"

static float s_temperature_c;
static int64_t s_taken_us;
static bool s_valid;
static SemaphoreHandle_t s_lock;

esp_err_t outdoor_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "no memory for the reading lock");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool lock(void)
{
    if (s_lock == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    return true;
}

static void unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

/* Copy the number that follows "key": out of the JSON text.  The reply of the
 * partner unit has a fixed, flat shape, so a parser would be overkill (the same
 * approach as in sun.c). */
static bool json_number(const char *json, const char *key, double *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *start = strstr(json, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);

    char *end = NULL;
    const double value = strtod(start, &end);
    if (end == start) {
        return false;
    }
    *out = value;
    return true;
}

static esp_err_t http_get(const char *url, char *body, size_t size)
{
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = OUTDOOR_TIMEOUT_MS,
        .user_agent = "e32r28t-thermometer",
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "cannot create the HTTP client");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request to %s failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "%s answered %d", url, status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
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
        ESP_LOGW(TAG, "%s sent an empty answer", url);
        return ESP_ERR_INVALID_RESPONSE;
    }
    body[total] = '\0';
    ESP_LOGD(TAG, "answer: %s", body);
    return ESP_OK;
}

esp_err_t outdoor_fetch(void)
{
    const char *remote = netcfg_remote();
    if (remote[0] == '\0') {
        return ESP_ERR_NOT_FOUND; /* no partner unit configured */
    }

    char url[96];
    snprintf(url, sizeof(url), "http://%s/api/values", remote);

    char *body = malloc(OUTDOOR_RESPONSE_SIZE);
    if (body == NULL) {
        ESP_LOGE(TAG, "no memory for the %d byte answer", OUTDOOR_RESPONSE_SIZE);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = http_get(url, body, OUTDOOR_RESPONSE_SIZE);

    double temperature = 0.0;
    if (err == ESP_OK) {
        /* "valid":false means the partner unit has no reading itself - then its
         * temperature is meaningless and is not taken over */
        if (strstr(body, "\"valid\":true") == NULL) {
            ESP_LOGW(TAG, "%s has no reading of its own", remote);
            err = ESP_ERR_INVALID_STATE;
        } else if (!json_number(body, "temperature_c", &temperature)) {
            ESP_LOGW(TAG, "%s sent no temperature_c", remote);
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    free(body);

    if (err != ESP_OK) {
        return err;
    }

    if (lock()) {
        s_temperature_c = (float)temperature;
        s_taken_us = esp_timer_get_time();
        s_valid = true;
        unlock();
    }

    const int tenths = (int)lroundf((float)temperature * 10.0f);
    ESP_LOGI(TAG, "%s: %s%d,%d C", remote, tenths < 0 ? "-" : "", abs(tenths) / 10,
             abs(tenths) % 10);
    return ESP_OK;
}

bool outdoor_last(float *temperature_c, int64_t *age_ms)
{
    if (!lock()) {
        return false;
    }
    const bool valid = s_valid;
    if (valid) {
        if (temperature_c != NULL) {
            *temperature_c = s_temperature_c;
        }
        if (age_ms != NULL) {
            *age_ms = (esp_timer_get_time() - s_taken_us) / 1000;
        }
    }
    unlock();
    return valid;
}

void outdoor_draw(gfx_canvas_t *canvas, uint16_t color)
{
    float temperature = 0.0f;
    if (!ttf_available() || !outdoor_last(&temperature, NULL)) {
        return;
    }

    const int tenths = (int)lroundf(temperature * 10.0f);
    char text[32];
    snprintf(text, sizeof(text), "%s%s%d,%d" DEGREE_SIGN, LAYOUT_OUT_TEMP_PREFIX,
             tenths < 0 ? "-" : "", abs(tenths) / 10, abs(tenths) % 10);

    const int used = ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2,
                                            LAYOUT_OUT_TEMP_BASELINE_Y, text, LAYOUT_OUT_TEMP_CAP,
                                            LAYOUT_OUT_TEMP_MAX_WIDTH, LAYOUT_OUT_TEMP_MIN_SQUEEZE,
                                            color);
    if (gfx_canvas_is_first_band(canvas)) {
        ESP_LOGI(TAG, "line \"%s\": cap %d px, baseline y=%d, %d px wide", text,
                 LAYOUT_OUT_TEMP_CAP, LAYOUT_OUT_TEMP_BASELINE_Y, used);
    }
}
