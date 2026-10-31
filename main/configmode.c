#include "configmode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "board_es3c28p.h"
#include "connect.h"
#include "discovery.h"
#include "layout.h"
#include "netcfg.h"
#include "textutil.h"
#include "touch.h"
#include "ttf.h"
#include "watchdog.h"
#include "webserver.h"
#include "wifi.h"

static const char *TAG = "configmode";

/* Networks offered on the page */
#define CONFIGMODE_MAX_APS   16

/* Room for the whole HTML page (head, ~16 networks, form) */
#define CONFIGMODE_PAGE_SIZE 6144

/* Longest form value accepted from the browser */
#define CONFIGMODE_VALUE_MAX 96

/* Touch poll interval of the display task */
#define CONFIGMODE_POLL_MS   100

/* Delay between the answer to /save and the restart, so the browser still gets it */
#define CONFIGMODE_RESTART_DELAY_US (1500 * 1000)

/* The scan runs in the background: it takes a few seconds while the access
 * point is up, and the page must answer immediately (phone browsers give up
 * quickly and fall back to mobile data when the Wi-Fi has no internet). */
#define CONFIGMODE_SCAN_INTERVAL_MS 20000

/* Safety net for a unit that is already mounted: when nobody configured it for
 * this long, it restarts and tries the stored network again - otherwise a boot
 * without coverage would leave it in the access point for good. */
#define CONFIGMODE_IDLE_RESTART_MS (15 * 60 * 1000)

#define CFG_COLOR_BG           GFX_RGB565(0x10, 0x14, 0x18)
#define CFG_COLOR_CARD         GFX_RGB565(0x1B, 0x23, 0x2B)
#define CFG_COLOR_LINE         GFX_RGB565(0x2C, 0x38, 0x44)
#define CFG_COLOR_TEXT         GFX_RGB565(0xF2, 0xF6, 0xFA)
#define CFG_COLOR_MUTED        GFX_RGB565(0x9F, 0xB0, 0xC0)
#define CFG_COLOR_ACCENT       GFX_RGB565(0x2F, 0x7D, 0x4F)
#define CFG_COLOR_ACCENT_DOWN  GFX_RGB565(0x1C, 0x4E, 0x32)

typedef enum {
    CONFIGMODE_REASON_NETWORK, /* started because the network is out of reach */
    CONFIGMODE_REASON_USER,    /* started from the corner of the readout */
} configmode_reason_t;

static volatile bool s_requested;
static volatile configmode_reason_t s_reason = CONFIGMODE_REASON_NETWORK;

/* Scan result, filled by the background scan task; the web handlers read it
 * under s_scan_lock */
static wifi_ap_info_t s_aps[CONFIGMODE_MAX_APS];
static int s_ap_count;
static SemaphoreHandle_t s_scan_lock;

/* WLAN data of the access point of this device */
static char s_ap_ip[16];

/* Restart request from the web page */
static volatile int64_t s_restart_at_us;
static volatile bool s_saved;

/* Last client activity, used by the idle restart */
static volatile int64_t s_last_activity_us;

/* Set by /rescan: the scan task starts over instead of waiting for its interval */
static volatile bool s_rescan;

static void note_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

void configmode_request(void)
{
    s_reason = CONFIGMODE_REASON_NETWORK;
    s_requested = true;
}

bool configmode_requested(void)
{
    return s_requested;
}

static bool inside(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

bool configmode_corner_touched(void)
{
    static TickType_t press_start;
    static bool resting;

    if (!touch_is_available() || s_requested) {
        return false;
    }

    touch_state_t touch;
    if (touch_read(&touch) != ESP_OK || !touch.pressed ||
        !inside(touch.x, touch.y, LAYOUT_CONFIG_TRIGGER_X, LAYOUT_CONFIG_TRIGGER_Y,
                LAYOUT_CONFIG_TRIGGER_W, LAYOUT_CONFIG_TRIGGER_H)) {
        resting = false;
        return false;
    }

    if (!resting) {
        resting = true;
        press_start = xTaskGetTickCount();
        return false;
    }
    if (xTaskGetTickCount() - press_start < pdMS_TO_TICKS(LAYOUT_CONFIG_HOLD_MS)) {
        return false;
    }

    resting = false;
    s_reason = CONFIGMODE_REASON_USER;
    s_requested = true;
    ESP_LOGI(TAG, "corner pressed for %d ms, opening the WLAN configuration",
             LAYOUT_CONFIG_HOLD_MS);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Background scan                                                           */
/* ------------------------------------------------------------------------- */

/* A scan while the access point is up switches the channel and costs a few
 * seconds, so it runs on its own task and the page always answers at once. */
static void scan_task(void *argument)
{
    (void)argument;

    watchdog_subscribe();

    for (;;) {
        wifi_ap_info_t found[CONFIGMODE_MAX_APS];
        const int count = wifi_scan_networks(found, CONFIGMODE_MAX_APS);

        if (s_scan_lock != NULL) {
            xSemaphoreTake(s_scan_lock, portMAX_DELAY);
            memcpy(s_aps, found, sizeof(found));
            s_ap_count = count;
            xSemaphoreGive(s_scan_lock);
        }

        for (int i = 0; i < count; i++) {
            ESP_LOGI(TAG, "  %2d: \"%s\", %d dBm, %s", i + 1, found[i].ssid, found[i].rssi,
                     found[i].secured ? "secured" : "open");
        }

        /* sleep in slices, so that /rescan takes effect quickly and the
         * watchdog sees a live task */
        for (int waited = 0; waited < CONFIGMODE_SCAN_INTERVAL_MS && !s_rescan; waited += 500) {
            watchdog_feed();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        s_rescan = false;
        watchdog_feed();
    }
}

/* ------------------------------------------------------------------------- */
/* DNS and captive portal                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Minimal DNS server: every name resolves to the access point.
 *
 * Phones look up the address of their connectivity check host (for Android
 * "connectivitycheck.gstatic.com") before they send the HTTP request.  Without
 * DNS that lookup fails, no request arrives, and the phone silently marks the
 * network as "no internet".  Answering all names with the access point address
 * makes the check land on our server, whose redirect then opens the form.
 */

/* Decode the question name (labels joined by dots) for the log. */
static void dns_log_question(const uint8_t *query, int length)
{
    char name[128];
    size_t used = 0;
    size_t pos = 12;

    while (pos < (size_t)length) {
        const uint8_t label = query[pos];
        if (label == 0 || (label & 0xC0) == 0xC0) {
            break;
        }
        if (used + label + 2 >= sizeof(name)) {
            break;
        }
        if (used > 0) {
            name[used++] = '.';
        }
        memcpy(&name[used], &query[pos + 1], label);
        used += label;
        pos += (size_t)label + 1;
    }
    name[used] = '\0';

    const uint16_t type =
        (pos + 2 < (size_t)length) ? (uint16_t)((query[pos + 1] << 8) | query[pos + 2]) : 0;
    ESP_LOGI(TAG, "DNS query: %s (type %u)", name[0] != '\0' ? name : "?", type);
}

static size_t dns_build_answer(const uint8_t *query, int length, uint8_t *answer, size_t size)
{
    if (length < 12 + 5 || size < 12 + 16) {
        return 0;
    }

    /* walk over the question name: labels, terminated by a zero byte or a
     * compression pointer */
    size_t pos = 12;
    while (pos < (size_t)length) {
        const uint8_t label = query[pos];
        if (label == 0) {
            pos++;
            break;
        }
        if ((label & 0xC0) == 0xC0) {
            pos += 2;
            break;
        }
        pos += (size_t)label + 1;
    }
    if (pos + 4 > (size_t)length) {
        return 0;
    }

    const uint16_t type = (uint16_t)((query[pos] << 8) | query[pos + 1]);
    const size_t question_end = pos + 4;
    if (question_end + 16 > size) {
        return 0;
    }

    memcpy(answer, query, question_end);
    answer[2] = (uint8_t)(0x80 | (query[2] & 0x01)); /* response, RD copied */
    answer[3] = 0x80;                                /* recursion available, no error */
    const bool with_address = type == 1;              /* A, not AAAA */
    answer[6] = 0;
    answer[7] = with_address ? 1 : 0;
    answer[8] = 0;
    answer[9] = 0;
    answer[10] = 0;
    answer[11] = 0;

    if (!with_address) {
        return question_end;
    }

    size_t out = question_end;
    answer[out++] = 0xC0; /* name: pointer to the question */
    answer[out++] = 0x0C;
    answer[out++] = 0x00; /* type A */
    answer[out++] = 0x01;
    answer[out++] = 0x00; /* class IN */
    answer[out++] = 0x01;
    answer[out++] = 0x00; /* TTL 60 s */
    answer[out++] = 0x00;
    answer[out++] = 0x00;
    answer[out++] = 60;
    answer[out++] = 0x00; /* 4 bytes of address */
    answer[out++] = 0x04;
    answer[out++] = 192;
    answer[out++] = 168;
    answer[out++] = 4;
    answer[out++] = 1;
    return out;
}

static void dns_task(void *argument)
{
    (void)argument;

    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "no socket for the DNS answerer");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_address = {0};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(53);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&bind_address, sizeof(bind_address)) < 0) {
        ESP_LOGE(TAG, "cannot bind the DNS port");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "DNS: every name answers with %s",
             s_ap_ip[0] != '\0' ? s_ap_ip : "192.168.4.1");

    uint8_t query[512];
    uint8_t answer[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        const int length =
            recvfrom(sock, query, sizeof(query), 0, (struct sockaddr *)&from, &from_length);
        if (length < 12) {
            continue;
        }
        const size_t answer_length = dns_build_answer(query, length, answer, sizeof(answer));
        if (answer_length == 0) {
            continue;
        }
        dns_log_question(query, length);
        sendto(sock, answer, answer_length, 0, (struct sockaddr *)&from, from_length);
    }
}

/* A client connected or left: that counts as activity for the idle restart. */
static void ap_event_handler(void *argument, esp_event_base_t base, int32_t id, void *data)
{
    (void)argument;
    (void)base;

    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "client %02x:%02x:%02x:%02x:%02x:%02x connected", event->mac[0],
                 event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event =
            (const wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "client %02x:%02x:%02x:%02x:%02x:%02x disconnected", event->mac[0],
                 event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
    } else {
        return;
    }
    s_last_activity_us = esp_timer_get_time();
}

/* ------------------------------------------------------------------------- */
/* Display                                                                   */
/* ------------------------------------------------------------------------- */

static void button_ink(gfx_canvas_t *canvas, bool pressed)
{
    gfx_fill_rect(canvas, 0, LAYOUT_CONFIG_BACK_Y, BOARD_SCREEN_WIDTH, LAYOUT_CONFIG_BACK_H,
                  pressed ? CFG_COLOR_ACCENT_DOWN : CFG_COLOR_ACCENT);
    ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_BUTTON_TEXT_Y,
                           "<- zurück zur APP", LAYOUT_CONFIG_BUTTON_CAP, LAYOUT_CONFIG_TEXT_W,
                           0.7f, CFG_COLOR_TEXT);
}

/* Redraw only the button, for the press feedback.  The canvas is moved to the
 * band that holds the button (the full-frame canvas stays at origin 0). */
static void button_refresh(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel, bool pressed)
{
    gfx_canvas_set_origin(canvas, (LAYOUT_CONFIG_BACK_Y / canvas->height) * canvas->height);
    button_ink(canvas, pressed);
    if (panel != NULL) {
        gfx_flush(canvas, panel, 0, LAYOUT_CONFIG_BACK_Y, BOARD_SCREEN_WIDTH,
                  LAYOUT_CONFIG_BACK_H);
    }
    gfx_canvas_set_origin(canvas, 0);
}

/* Draw the whole screen band by band, so it works with the band canvas. */
static void screen_draw(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel, const char *status)
{
    char ap_line[64];
    snprintf(ap_line, sizeof(ap_line), "http://%s", s_ap_ip[0] != '\0' ? s_ap_ip : "192.168.4.1");
    const bool secured = CONNECT_AP_PASSWORD[0] != '\0';

    for (int band = 0; band < BOARD_SCREEN_HEIGHT; band += LAYOUT_BAND_HEIGHT) {
        gfx_canvas_set_origin(canvas, band);
        gfx_clear(canvas, CFG_COLOR_BG);

        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_TITLE_Y,
                               "WLAN einrichten", LAYOUT_CONFIG_TITLE_CAP, LAYOUT_CONFIG_TEXT_W,
                               0.7f, CFG_COLOR_TEXT);
        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_STATUS_Y, status,
                               LAYOUT_CONFIG_STATUS_CAP, LAYOUT_CONFIG_TEXT_W, 0.7f,
                               CFG_COLOR_MUTED);
        gfx_hline(canvas, LAYOUT_CONFIG_TEXT_X, LAYOUT_CONFIG_LINE1_Y, LAYOUT_CONFIG_TEXT_W,
                  CFG_COLOR_LINE);

        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_ROW1_LABEL_Y,
                               "1. Mit dem Hotspot verbinden", LAYOUT_CONFIG_LABEL_CAP,
                               LAYOUT_CONFIG_TEXT_W, 0.7f, CFG_COLOR_MUTED);
        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_ROW1_VALUE_Y,
                               CONNECT_AP_SSID, LAYOUT_CONFIG_VALUE_CAP, LAYOUT_CONFIG_TEXT_W,
                               0.7f, CFG_COLOR_TEXT);
        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_ROW1_HINT_Y,
                               secured ? "Passwort: " CONNECT_AP_PASSWORD
                                       : "offener Zugang, kein Passwort",
                               LAYOUT_CONFIG_HINT_CAP, LAYOUT_CONFIG_TEXT_W, 0.7f, CFG_COLOR_MUTED);

        gfx_hline(canvas, LAYOUT_CONFIG_TEXT_X, LAYOUT_CONFIG_LINE2_Y, LAYOUT_CONFIG_TEXT_W,
                  CFG_COLOR_LINE);

        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_ROW2_LABEL_Y,
                               "2. Im Browser öffnen", LAYOUT_CONFIG_LABEL_CAP,
                               LAYOUT_CONFIG_TEXT_W, 0.7f, CFG_COLOR_MUTED);
        ttf_draw_text_centered(canvas, BOARD_SCREEN_WIDTH / 2, LAYOUT_CONFIG_ROW2_VALUE_Y, ap_line,
                               LAYOUT_CONFIG_VALUE_CAP, LAYOUT_CONFIG_TEXT_W, 0.7f,
                               CFG_COLOR_TEXT);

        button_ink(canvas, false);
        if (panel != NULL) {
            gfx_flush_all(canvas, panel);
        }
    }
    gfx_canvas_set_origin(canvas, 0);
}

/* ------------------------------------------------------------------------- */
/* Web page                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *cursor;
    size_t left;
    bool ok;
} page_t;

static void page_add(page_t *page, const char *format, ...)
{
    if (!page->ok) {
        return;
    }
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(page->cursor, page->left, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= page->left) {
        page->ok = false;
        return;
    }
    page->cursor += written;
    page->left -= (size_t)written;
}

static void url_decode(const char *source, size_t length, char *target, size_t size)
{
    size_t out = 0;
    for (size_t i = 0; i < length && out + 1 < size; i++) {
        const char c = source[i];
        if (c == '+') {
            target[out++] = ' ';
        } else if (c == '%' && i + 2 < length) {
            char hex[3] = {source[i + 1], source[i + 2], '\0'};
            char *end = NULL;
            const long value = strtol(hex, &end, 16);
            if (end == hex + 2 && value > 0) {
                target[out++] = (char)value;
                i += 2;
            } else {
                target[out++] = c;
            }
        } else {
            target[out++] = c;
        }
    }
    target[out] = '\0';
}

/* An IPv4 address or a host name, optionally with a port: letters, digits,
 * dots, hyphens and exactly one ":" followed by 1..65535.  No scheme and no
 * path - outdoor_fetch() adds "http://" in front and "/api/values" behind. */
static bool valid_host(const char *text)
{
    if (text[0] == '\0') {
        return true;
    }

    const char *colon = strchr(text, ':');
    const char *host_end = colon != NULL ? colon : text + strlen(text);
    if (host_end == text) {
        return false;
    }

    for (const char *c = text; c < host_end; c++) {
        const bool allowed = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') ||
                             (*c >= 'A' && *c <= 'Z') || *c == '.' || *c == '-';
        if (!allowed) {
            return false;
        }
    }

    if (colon == NULL) {
        return true;
    }
    if (strchr(colon + 1, ':') != NULL) {
        return false;
    }

    const char *digit = colon + 1;
    if (*digit == '\0') {
        return false;
    }
    long port = 0;
    for (; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        port = port * 10 + (*digit - '0');
        if (port > 65535) {
            return false;
        }
    }
    return port >= 1;
}

/* Value of `key` in an application/x-www-form-urlencoded body, already decoded */
static bool form_value(const char *body, const char *key, char *out, size_t size)
{
    const size_t key_length = strlen(key);
    const char *cursor = body;

    while (cursor != NULL && *cursor != '\0') {
        const char *end = strchr(cursor, '&');
        const size_t field_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (field_length > key_length && strncmp(cursor, key, key_length) == 0 &&
            cursor[key_length] == '=') {
            url_decode(cursor + key_length + 1, field_length - key_length - 1, out, size);
            return true;
        }
        cursor = end != NULL ? end + 1 : NULL;
    }
    out[0] = '\0';
    return false;
}

static int receive_body(httpd_req_t *req, char *buffer, size_t size)
{
    int total = 0;
    int attempts = 0;
    while (total < (int)size) {
        const int read = httpd_req_recv(req, buffer + total, size - (size_t)total);
        if (read == HTTPD_SOCK_ERR_TIMEOUT && attempts++ < 3) {
            continue;
        }
        if (read <= 0) {
            break;
        }
        total += read;
    }
    buffer[total] = '\0';
    return total;
}

static const char *PAGE_HEAD =
    "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Raumthermometer – Einrichtung</title><style>"
    "*{box-sizing:border-box}"
    "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;margin:0;padding:18px;"
    "background:#101418;color:#e8eef5}"
    "h1{font-size:20px;margin:0 0 4px;font-weight:600}"
    "p{color:#9fb0c0;font-size:14px;margin:0 0 18px;line-height:1.4}"
    "label{display:block;font-size:13px;color:#9fb0c0;margin:16px 0 5px}"
    "select,input{width:100%;padding:11px;font-size:16px;border-radius:8px;"
    "border:1px solid #2c3844;background:#1b232b;color:#e8eef5}"
    ".two{display:flex;gap:8px}"
    ".row{display:flex;gap:10px;margin-top:26px}"
    "button{flex:1;padding:13px;font-size:16px;border-radius:8px;border:0;font-weight:600}"
    ".save{background:#2f7d4f;color:#fff}.cancel{background:#2c3844;color:#dae2ea}"
    "a{color:#7fc8a0}"
    "</style></head><body>";

static const char *PAGE_TAIL =
    "</select>"
    "<label for=\"ssid_manual\">Anderes Netzwerk (optional, für versteckte Namen)</label>"
    "<input id=\"ssid_manual\" name=\"ssid_manual\" maxlength=\"32\" autocapitalize=\"off\" "
    "autocorrect=\"off\" autocomplete=\"off\">"
    "<label for=\"password\">Passwort (leer = gespeichertes behalten)</label>"
    "<input id=\"password\" name=\"password\" type=\"password\" maxlength=\"63\" "
    "autocomplete=\"off\">"
    "<label for=\"lat\">Standort für die Sonnenzeiten: Breite / Länge</label>"
    "<div class=\"two\">"
    "<input id=\"lat\" name=\"lat\" inputmode=\"decimal\" value=\"%s\">"
    "<input id=\"lon\" name=\"lon\" inputmode=\"decimal\" value=\"%s\">"
    "</div>"
    "<label for=\"remote\">Außentemperatursensor: IP-Adresse oder Hostname (optional)</label>"
    "<input id=\"remote\" name=\"remote\" maxlength=\"63\" placeholder=\"z. B. 192.168.123.50 oder name.ddns.net:8083\" "
    "autocapitalize=\"off\" autocorrect=\"off\" autocomplete=\"off\" value=\"%s\">"
    "<label for=\"offset\">Temperatur-Offset in °C (Kalibrierung)</label>"
    "<input id=\"offset\" name=\"offset\" inputmode=\"decimal\" maxlength=\"6\" "
    "placeholder=\"z. B. -0,5\" value=\"%s\">"
    "<p style=\"font-size:13px;margin:6px 0 0\">Wird zu jedem Messwert addiert, in 0,1-Schritten, "
    "erlaubt sind −5,0 bis +5,0. Leer oder 0 lässt die Anzeige unverändert.</p>"
    "<div class=\"row\">"
    "<button class=\"cancel\" type=\"submit\" formaction=\"/cancel\" formnovalidate>Abbrechen"
    "</button><button class=\"save\" type=\"submit\">Speichern</button>"
    "</div></form>"
    "<p style=\"margin-top:22px\"><a href=\"/rescan\">Netzwerke neu suchen</a></p>"
    "</body></html>";

static esp_err_t handler_root(httpd_req_t *req)
{
    note_activity();

    char *buffer = malloc(CONFIGMODE_PAGE_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "no memory for the %d byte page", CONFIGMODE_PAGE_SIZE);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "kein Speicher");
    }

    netcfg_t cfg;
    netcfg_load(&cfg);

    page_t page = {.cursor = buffer, .left = CONFIGMODE_PAGE_SIZE, .ok = true};
    page_add(&page, "%s", PAGE_HEAD);

    char escaped_name[200];
    html_escape(cfg.name, escaped_name, sizeof(escaped_name));
    page_add(&page,
             "<h1>%s</h1>"
             "<p>WLAN auswählen, Passwort eingeben und speichern. Das Gerät startet danach neu "
             "und meldet sich im gewählten Netzwerk an.</p>"
             "<form method=\"post\" action=\"/save\">"
             "<label for=\"name\">Name dieses Geräts (erscheint im Browser und im Router)</label>"
             "<input id=\"name\" name=\"name\" maxlength=\"32\" value=\"%s\" "
             "autocomplete=\"off\">"
             "<label for=\"ssid\">Netzwerk</label><select id=\"ssid\" name=\"ssid\">",
             escaped_name, escaped_name);

    /* the list is written by the background scan task */
    int ap_count = 0;
    if (s_scan_lock != NULL) {
        xSemaphoreTake(s_scan_lock, portMAX_DELAY);
        ap_count = s_ap_count;
    }
    if (ap_count == 0) {
        page_add(&page, "<option value=\"\">kein Netzwerk gefunden</option>");
    }
    for (int i = 0; i < ap_count; i++) {
        char escaped[200];
        html_escape(s_aps[i].ssid, escaped, sizeof(escaped));
        page_add(&page, "<option value=\"%s\"%s>%s (%d dBm%s)</option>", escaped,
                 strcmp(s_aps[i].ssid, cfg.ssid) == 0 ? " selected" : "", escaped, s_aps[i].rssi,
                 s_aps[i].secured ? ", gesichert" : ", offen");
    }
    if (s_scan_lock != NULL) {
        xSemaphoreGive(s_scan_lock);
    }

    char latitude[NETCFG_COORD_MAX * 6];
    char longitude[NETCFG_COORD_MAX * 6];
    char remote[NETCFG_REMOTE_MAX * 6];
    html_escape(cfg.latitude, latitude, sizeof(latitude));
    html_escape(cfg.longitude, longitude, sizeof(longitude));
    html_escape(cfg.remote, remote, sizeof(remote));

    /* the offset with a decimal comma, as a German user types it */
    char offset[16];
    const int offset_tenths = cfg.offset_tenths;
    snprintf(offset, sizeof(offset), "%s%d,%d", offset_tenths < 0 ? "-" : "",
             abs(offset_tenths) / 10, abs(offset_tenths) % 10);

    page_add(&page, PAGE_TAIL, latitude, longitude, remote, offset);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const esp_err_t err = page.ok ? httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN)
                                  : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                                        "Seite zu groß");
    free(buffer);
    return err;
}

static esp_err_t send_message(httpd_req_t *req, int status, const char *title,
                              const char *detail)
{
    char page[768];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Raumthermometer</title><style>"
             "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;margin:0;padding:18px;"
             "background:#101418;color:#e8eef5}"
             "h1{font-size:20px;margin:0 0 10px;font-weight:600}"
             "p{color:#9fb0c0;font-size:15px;line-height:1.5}"
             "a{color:#7fc8a0}"
             "</style></head><body><h1>%s</h1><p>%s</p>"
             "<p><a href=\"/\">zurück zur Einrichtung</a></p></body></html>",
             title, detail);

    httpd_resp_set_status(req, status == 200 ? "200 OK" : "400 Bad Request");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_save(httpd_req_t *req)
{
    note_activity();

    char body[512];
    if (receive_body(req, body, sizeof(body) - 1) <= 0) {
        return send_message(req, 400, "Nichts empfangen",
                            "Das Formular war leer. Bitte erneut versuchen.");
    }

    netcfg_t cfg;
    netcfg_load(&cfg);
    netcfg_t next = cfg;

    char raw[CONFIGMODE_VALUE_MAX + 1];

    /* device name: an empty field keeps the current name */
    if (form_value(body, "name", raw, sizeof(raw)) && raw[0] != '\0') {
        if (strlen(raw) > NETCFG_NAME_MAX) {
            return send_message(req, 400, "Gerätename zu lang",
                                "Der Name hat höchstens 32 Zeichen.");
        }
        strlcpy(next.name, raw, sizeof(next.name));
    }

    /* the free text field wins over the list - it is the way to reach a hidden
     * network, and the list may be out of date */
    const bool manual = form_value(body, "ssid_manual", raw, sizeof(raw)) && raw[0] != '\0';
    if (!manual && !form_value(body, "ssid", raw, sizeof(raw))) {
        raw[0] = '\0';
    }
    if (raw[0] == '\0') {
        /* nothing picked, for example because the scan found no network: keep
         * the stored one so the other fields can still be saved */
        if (cfg.ssid[0] == '\0') {
            return send_message(req, 400, "Kein Netzwerk gewählt",
                                "Bitte ein Netzwerk aus der Liste wählen oder den Namen eintragen.");
        }
    } else {
        if (strlen(raw) > NETCFG_SSID_MAX) {
            return send_message(req, 400, "Netzwerkname zu lang",
                                "Ein WLAN-Name hat höchstens 32 Zeichen.");
        }
        strlcpy(next.ssid, raw, sizeof(next.ssid));
    }

    if (form_value(body, "password", raw, sizeof(raw))) {
        const size_t length = strlen(raw);
        if (length > 0 && length < 8) {
            return send_message(req, 400, "Passwort zu kurz",
                                "Ein WPA2-Passwort hat mindestens 8 Zeichen. Für ein offenes "
                                "Netzwerk das Feld leer lassen.");
        }
        if (length >= NETCFG_PASSWORD_MAX) {
            return send_message(req, 400, "Passwort zu lang",
                                "Ein WLAN-Passwort hat höchstens 63 Zeichen.");
        }
        /* The field always starts empty.  On an unchanged network that means
         * "keep the stored password", otherwise changing only the offset or the
         * name would cut the connection. */
        const bool ssid_changed = strcmp(next.ssid, cfg.ssid) != 0;
        if (length == 0 && !ssid_changed && cfg.password[0] != '\0') {
            ESP_LOGI(TAG, "web: password empty on an unchanged network, keeping the stored one");
        } else {
            strlcpy(next.password, raw, sizeof(next.password));
        }
    }

    /* coordinates: an empty field keeps the value in use */
    if (form_value(body, "lat", raw, sizeof(raw)) && raw[0] != '\0') {
        char *end = NULL;
        const double value = strtod(raw, &end);
        if (end == raw || *end != '\0' || value < -90.0 || value > 90.0) {
            return send_message(req, 400, "Breitengrad ungültig",
                                "Bitte Dezimalgrad zwischen -90 und 90 angeben, zum Beispiel "
                                "48.4149.");
        }
        strlcpy(next.latitude, raw, sizeof(next.latitude));
    }
    if (form_value(body, "lon", raw, sizeof(raw)) && raw[0] != '\0') {
        char *end = NULL;
        const double value = strtod(raw, &end);
        if (end == raw || *end != '\0' || value < -180.0 || value > 180.0) {
            return send_message(req, 400, "Längengrad ungültig",
                                "Bitte Dezimalgrad zwischen -180 und 180 angeben, zum Beispiel "
                                "11.0997.");
        }
        strlcpy(next.longitude, raw, sizeof(next.longitude));
    }

    /* outdoor sensor: an empty field removes it */
    if (form_value(body, "remote", raw, sizeof(raw))) {
        if (!valid_host(raw)) {
            return send_message(req, 400, "Adresse des Außensensors ungültig",
                                "Bitte einen Hostnamen oder eine IPv4-Adresse eintragen, "
                                "optional mit Port (z. B. name.ddns.net:8083). "
                                "Kein http:// davor und kein / dahinter.");
        }
        strlcpy(next.remote, raw, sizeof(next.remote));
    }

    /* temperature calibration: a decimal comma or point, an empty field is 0 */
    if (form_value(body, "offset", raw, sizeof(raw))) {
        if (raw[0] == '\0') {
            next.offset_tenths = 0;
        } else {
            /* strtod() only knows the decimal point, a German keyboard gives a
             * comma - accept both */
            char number[CONFIGMODE_VALUE_MAX + 1];
            strlcpy(number, raw, sizeof(number));
            for (char *c = number; *c != '\0'; c++) {
                if (*c == ',') {
                    *c = '.';
                }
            }

            char *end = NULL;
            const double value = strtod(number, &end);
            const double rounded = (double)(int)(value * 10.0 + (value < 0 ? -0.5 : 0.5));
            if (end == number || *end != '\0' || rounded < -NETCFG_OFFSET_LIMIT_TENTHS ||
                rounded > NETCFG_OFFSET_LIMIT_TENTHS) {
                return send_message(req, 400, "Temperatur-Offset ungültig",
                                    "Bitte einen Wert zwischen -5,0 und +5,0 eintragen, zum "
                                    "Beispiel -0,5. Leer oder 0 bedeutet keine Korrektur.");
            }
            next.offset_tenths = (int16_t)rounded;
        }
    }

    const esp_err_t err = netcfg_save(&next);
    if (err != ESP_OK) {
        return send_message(req, 400, "Speichern fehlgeschlagen",
                            "Die Einstellungen konnten nicht abgelegt werden.");
    }

    ESP_LOGI(TAG, "web: stored \"%s\", offset %d.%d C, restarting", next.ssid,
             next.offset_tenths / 10, abs(next.offset_tenths % 10));
    s_saved = true;
    s_restart_at_us = esp_timer_get_time() + CONFIGMODE_RESTART_DELAY_US;

    return send_message(req, 200, "Gespeichert",
                        "Das Gerät startet jetzt neu und meldet sich im gewählten Netzwerk an. "
                        "Diese Seite kann geschlossen werden.");
}

static esp_err_t handler_cancel(httpd_req_t *req)
{
    note_activity();
    ESP_LOGI(TAG, "web: cancelled");
    return send_message(req, 200, "Abgebrochen",
                        "Es wurde nichts geändert. Diese Seite kann geschlossen werden. "
                        "Am Gerät führt die Schaltfläche unten zurück zur Anzeige.");
}

static esp_err_t handler_rescan(httpd_req_t *req)
{
    note_activity();
    s_rescan = true;
    ESP_LOGI(TAG, "web: rescan requested");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

/* ------------------------------------------------------------------------- */
/* Connectivity checks                                                       */
/* ------------------------------------------------------------------------- */

/* Answer the checks of the operating systems as "everything is fine".
 * Otherwise the phone marks the network as "no internet", shows the "!" and
 * moves its traffic to mobile data, which makes the configuration page
 * unreachable. */
static esp_err_t handler_no_content(httpd_req_t *req)
{
    note_activity();
    ESP_LOGI(TAG, "connectivity check %s -> 204", req->uri);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handler_apple_success(httpd_req_t *req)
{
    note_activity();
    ESP_LOGI(TAG, "connectivity check %s -> Success", req->uri);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req,
                           "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_ncsi(httpd_req_t *req)
{
    note_activity();
    const bool ncsi = strcmp(req->uri, "/ncsi.txt") == 0;
    ESP_LOGI(TAG, "connectivity check %s -> text", req->uri);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, ncsi ? "Microsoft NCSI" : "Microsoft Connect Test",
                           HTTPD_RESP_USE_STRLEN);
}

/* Every other path leads to the form, so any address the user types ends up in
 * the configuration. */
static esp_err_t handler_captive(httpd_req_t *req)
{
    note_activity();
    ESP_LOGD(TAG, "%s %s -> redirect", req->method == HTTP_POST ? "POST" : "GET", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "Konfiguration: http://192.168.4.1/", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    /* a phone opens several connections at once while probing for a portal */
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    /* form, save, cancel, rescan, six connectivity checks and the wildcard */
    config.max_uri_handlers = 14;
    /* required for the wildcard handlers below: without it the server only
     * compares strings and answers 404 to every unknown path */
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handler_root, .user_ctx = NULL},
        {.uri = "/save", .method = HTTP_POST, .handler = handler_save, .user_ctx = NULL},
        {.uri = "/cancel", .method = HTTP_POST, .handler = handler_cancel, .user_ctx = NULL},
        {.uri = "/rescan", .method = HTTP_GET, .handler = handler_rescan, .user_ctx = NULL},
        /* connectivity checks: answer as if the internet worked */
        {.uri = "/generate_204", .method = HTTP_GET, .handler = handler_no_content,
         .user_ctx = NULL},
        {.uri = "/gen_204", .method = HTTP_GET, .handler = handler_no_content, .user_ctx = NULL},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_apple_success,
         .user_ctx = NULL},
        {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = handler_apple_success,
         .user_ctx = NULL},
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = handler_ncsi, .user_ctx = NULL},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = handler_ncsi, .user_ctx = NULL},
        /* everything else: redirect to the form */
        {.uri = "/*", .method = HTTP_GET, .handler = handler_captive, .user_ctx = NULL},
        {.uri = "/*", .method = HTTP_POST, .handler = handler_captive, .user_ctx = NULL},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "registering %s failed: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "web server at http://%s (stack %d bytes, %d sockets)", s_ap_ip,
             config.stack_size, config.max_open_sockets);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Mode                                                                      */
/* ------------------------------------------------------------------------- */

void configmode_run(gfx_canvas_t *canvas, esp_lcd_panel_handle_t panel)
{
    s_requested = true;
    ESP_LOGI(TAG, "WLAN configuration mode (%s)",
             s_reason == CONFIGMODE_REASON_NETWORK ? "network not reachable" : "requested");
    ESP_LOGI(TAG, "heap: %d bytes free, largest block %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    /* Port 80 is taken by the readings endpoint while the device is in the home
     * network.  It has to go before this mode binds the port, otherwise requests
     * through the access point would end up on the readings page.  The mDNS
     * announcement goes with it - the unit is only reachable through the access
     * point now. */
    discovery_stop();
    webserver_stop();

    if (wifi_start_ap(CONNECT_AP_SSID, CONNECT_AP_PASSWORD, s_ap_ip, sizeof(s_ap_ip)) != ESP_OK) {
        ESP_LOGE(TAG, "access point not available, restarting instead");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    /* Hand the access point itself out as DNS server, so phones resolve their
     * connectivity check host to us (see dns_task). */
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != NULL) {
        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(s_ap_ip);
        const esp_err_t dns_err = esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
        if (dns_err != ESP_OK) {
            ESP_LOGW(TAG, "cannot offer %s as DNS: %s", s_ap_ip, esp_err_to_name(dns_err));
        }
    }
    xTaskCreate(dns_task, "dns", 4096, NULL, 3, NULL);

    /* the scan list is filled in the background; the first page shows what is
     * already there */
    if (s_scan_lock == NULL) {
        s_scan_lock = xSemaphoreCreateMutex();
    }
    if (s_scan_lock != NULL) {
        xTaskCreate(scan_task, "scan", 4096, NULL, 3, NULL);
    } else {
        ESP_LOGE(TAG, "no memory for the scan lock - the network list stays empty");
    }

    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, ap_event_handler,
                                        NULL, NULL);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                        ap_event_handler, NULL, NULL);

    web_start();

    note_activity();

    const char *status = s_reason == CONFIGMODE_REASON_NETWORK
                             ? "kein Netzwerk - bitte einrichten"
                             : "Einstellungen ändern";
    screen_draw(canvas, panel, status);

    ESP_LOGI(TAG, "heap after access point and web server: %d bytes free, largest block %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    bool back_pressed = false;
    bool back_pressed_last = false;
    bool saved_shown = false;

    watchdog_subscribe();

    for (;;) {
        watchdog_feed();

        if (s_restart_at_us != 0 && esp_timer_get_time() >= s_restart_at_us) {
            ESP_LOGI(TAG, "restarting with the new settings");
            esp_restart();
        }

        /* nobody configured the unit for a long time: go back and try the
         * stored network, an already mounted device must not stay an access
         * point forever */
        if (esp_timer_get_time() - s_last_activity_us >=
            (int64_t)CONFIGMODE_IDLE_RESTART_MS * 1000) {
            ESP_LOGW(TAG, "%d minutes without a configuration - restarting to try the stored "
                          "network",
                     CONFIGMODE_IDLE_RESTART_MS / 60000);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }

        if (s_saved && !saved_shown) {
            saved_shown = true;
            screen_draw(canvas, panel, "gespeichert - Neustart ...");
        }

        touch_state_t touch;
        const bool valid = touch_read(&touch) == ESP_OK;
        const bool inside_button =
            valid && touch.pressed &&
            inside(touch.x, touch.y, 0, LAYOUT_CONFIG_BACK_Y, BOARD_SCREEN_WIDTH,
                   LAYOUT_CONFIG_BACK_H);

        if (inside_button != back_pressed) {
            back_pressed = inside_button;
            button_refresh(canvas, panel, back_pressed);
        }
        if (back_pressed) {
            back_pressed_last = true;
        } else if (back_pressed_last) {
            /* released inside the button: leave the mode and keep the readout
             * running without a network until the next corner touch */
            back_pressed_last = false;
            ESP_LOGI(TAG, "back to the app without a new WLAN");
            netcfg_set_skipped(true);
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIGMODE_POLL_MS));
    }
}
