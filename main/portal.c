#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <dns_server.h>

#include "portal.h"
#include "settings.h"
#include "wifi.h"

#define MAX_SCAN 16
#define RETRY_AFTER_US (5 * 60 * 1000000LL)  // fallback setup: retry saved Wi-Fi after 5 min

static const char *TAG = "portal";

static char ap_ssid[20];
static char scan_ssids[MAX_SCAN][33];
static int scan_count;

// POSIX TZ strings for a few common zones. A custom string can be typed instead.
static const struct {
    const char *label;
    const char *tz;
} ZONES[] = {
    { "UTC", "UTC0" },
    { "London, Lisbon", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Berlin, Paris, Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Kyiv, Helsinki, Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Istanbul", "<+03>-3" },
    { "Moscow", "MSK-3" },
    { "Dubai", "<+04>-4" },
    { "India", "IST-5:30" },
    { "China, Singapore", "CST-8" },
    { "Tokyo", "JST-9" },
    { "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Sao Paulo", "<-03>3" },
    { "New York", "EST5EDT,M3.2.0,M11.1.0" },
    { "Chicago", "CST6CDT,M3.2.0,M11.1.0" },
    { "Denver", "MST7MDT,M3.2.0,M11.1.0" },
    { "Los Angeles", "PST8PDT,M3.2.0,M11.1.0" },
};
#define ZONE_COUNT (sizeof(ZONES) / sizeof(ZONES[0]))

const char *portal_ap_ssid(void)
{
    if (!ap_ssid[0]) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf(ap_ssid, sizeof(ap_ssid), "HubitHab-%02X%02X", mac[4], mac[5]);
    }
    return ap_ssid;
}

// Blocking scan; the access point stays up (APSTA mode). Keeps unique names only.
static void scan_networks(void)
{
    static wifi_ap_record_t recs[MAX_SCAN * 2];
    uint16_t n = sizeof(recs) / sizeof(recs[0]);

    wifi_scan_config_t cfg = { 0 };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK ||
        esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed");
        return;
    }

    scan_count = 0;
    for (int i = 0; i < n && scan_count < MAX_SCAN; i++) {
        const char *name = (const char *)recs[i].ssid;
        bool dup = !name[0];
        for (int j = 0; j < scan_count && !dup; j++)
            dup = strcmp(scan_ssids[j], name) == 0;
        if (!dup)
            strlcpy(scan_ssids[scan_count++], name, sizeof(scan_ssids[0]));
    }
    ESP_LOGI(TAG, "Found %d networks", scan_count);
}

// ---------------------------------------------------------------------------
// HTML helpers

// An empty chunk terminates a chunked response, so never send one by accident
// (e.g. an empty saved value). The real terminator is httpd_resp_sendstr_chunk(req, NULL).
static void send(httpd_req_t *req, const char *s)
{
    if (*s)
        httpd_resp_sendstr_chunk(req, s);
}

// Values come from NVS and scan results, so escape them before putting them
// into attributes.
static void send_escaped(httpd_req_t *req, const char *s)
{
    char buf[64];
    size_t n = 0;
    for (; *s; s++) {
        const char *rep = NULL;
        switch (*s) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        }
        size_t len = rep ? strlen(rep) : 1;
        if (n + len >= sizeof(buf)) {
            buf[n] = '\0';
            send(req, buf);
            n = 0;
        }
        if (rep) {
            memcpy(buf + n, rep, len);
            n += len;
        } else {
            buf[n++] = *s;
        }
    }
    buf[n] = '\0';
    send(req, buf);
}

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HubitHab setup</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:28em;margin:1em auto;padding:0 1em;"
    "background:#111;color:#eee}"
    "h1{font-size:1.4em}label{display:block;margin:1em 0 .2em;font-weight:600}"
    "input,select,button{width:100%;box-sizing:border-box;padding:.6em;font-size:1em;"
    "border-radius:6px;border:1px solid #555;background:#222;color:#eee}"
    "button{margin-top:1.5em;background:#2a7;border:0;color:#fff;font-weight:600}"
    "small{color:#999}a{color:#6cf}"
    "</style></head><body>";

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    send(req, PAGE_HEAD);
    send(req, "<h1>HubitHab setup</h1><form method='post' action='/save'>");

    send(req, "<label for='ssid'>Wi-Fi network</label>"
              "<input id='ssid' name='ssid' list='nets' required maxlength='32' value='");
    send_escaped(req, settings.ssid);
    send(req, "'><datalist id='nets'>");
    for (int i = 0; i < scan_count; i++) {
        send(req, "<option value='");
        send_escaped(req, scan_ssids[i]);
        send(req, "'>");
    }
    send(req, "</datalist><small>Pick from the list or type it. "
              "<a href='/rescan'>Scan again</a></small>");

    send(req, "<label for='pass'>Wi-Fi password</label>"
              "<input id='pass' name='pass' type='password' maxlength='64'");
    if (settings.pass[0])
        send(req, " placeholder='unchanged'");
    send(req, ">");

    send(req, "<label for='gh_user'>GitHub username</label>"
              "<input id='gh_user' name='gh_user' maxlength='39' autocapitalize='off' value='");
    send_escaped(req, settings.gh_user);
    send(req, "'>");

    send(req, "<label for='gh_token'>GitHub token (optional)</label>"
              "<input id='gh_token' name='gh_token' type='password' maxlength='127'");
    if (settings.gh_token[0])
        send(req, " placeholder='unchanged'");
    send(req, "><small>Needed for the contribution heatmap. Use a read-only token.</small>");

    send(req, "<label for='tz'>Time zone</label><select id='tz' name='tz'>");
    bool known = false;
    for (int i = 0; i < ZONE_COUNT; i++) {
        bool sel = strcmp(settings.tz, ZONES[i].tz) == 0;
        known |= sel;
        send(req, "<option value='");
        send_escaped(req, ZONES[i].tz);
        send(req, sel ? "' selected>" : "'>");
        send_escaped(req, ZONES[i].label);
        send(req, "</option>");
    }
    send(req, "</select><small>Not listed? Enter a POSIX TZ string:</small>"
              "<input name='tz_custom' maxlength='63' autocapitalize='off' value='");
    if (!known)
        send_escaped(req, settings.tz);
    send(req, "'>");

    send(req, "<button type='submit'>Save and restart</button></form></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t rescan_handler(httpd_req_t *req)
{
    scan_networks();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Form parsing

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Decode application/x-www-form-urlencoded in place: '+' = space, %XX = byte.
static void url_decode(char *s)
{
    char *out = s;
    for (; *s; s++) {
        if (*s == '+') {
            *out++ = ' ';
        } else if (*s == '%' && hex_value(s[1]) >= 0 && hex_value(s[2]) >= 0) {
            *out++ = hex_value(s[1]) * 16 + hex_value(s[2]);
            s += 2;
        } else {
            *out++ = *s;
        }
    }
    *out = '\0';
}

// Copies the decoded field into out. Returns false if missing or too long.
static bool form_field(const char *body, const char *key, char *out, size_t size)
{
    char raw[400];  // encoded values can be 3x longer than decoded ones
    if (httpd_query_key_value(body, key, raw, sizeof(raw)) != ESP_OK)
        return false;
    url_decode(raw);
    if (strlen(raw) >= size)
        return false;
    strcpy(out, raw);
    return true;
}

static void send_message(httpd_req_t *req, const char *status, const char *html)
{
    if (status)
        httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html");
    send(req, PAGE_HEAD);
    send(req, html);
    send(req, "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t save_handler(httpd_req_t *req)
{
    static char body[2048];  // httpd handles one request at a time
    if (req->content_len >= sizeof(body)) {
        send_message(req, "400 Bad Request", "<p>Form too large.</p>");
        return ESP_OK;
    }
    int got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    // Start from the current settings: empty password / token fields mean "keep".
    settings_t s = settings;
    char tmp[128];

    if (!form_field(body, "ssid", s.ssid, sizeof(s.ssid)) || !s.ssid[0]) {
        send_message(req, "400 Bad Request", "<p>Wi-Fi network is missing or too long. "
                     "<a href='/'>Back</a></p>");
        return ESP_OK;
    }
    if (form_field(body, "pass", tmp, sizeof(s.pass)) && tmp[0])
        strcpy(s.pass, tmp);
    if (!form_field(body, "gh_user", s.gh_user, sizeof(s.gh_user)))
        s.gh_user[0] = '\0';
    if (form_field(body, "gh_token", tmp, sizeof(s.gh_token)) && tmp[0])
        strcpy(s.gh_token, tmp);
    if (!(form_field(body, "tz_custom", s.tz, sizeof(s.tz)) && s.tz[0]) &&
        !form_field(body, "tz", s.tz, sizeof(s.tz)))
        strcpy(s.tz, "UTC0");

    esp_err_t err = settings_save(&s);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
        send_message(req, "500 Internal Server Error", "<p>Saving failed.</p>");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Saved, restarting");
    send_message(req, NULL, "<h1>Saved</h1><p>HubitHab is restarting. "
                 "You can leave this network now.</p>");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// Unknown URLs (including the OS connectivity checks) redirect to the form,
// which makes phones and laptops show the captive portal.
static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    // iOS needs a body to detect the portal.
    httpd_resp_sendstr(req, "Redirect to the setup page");
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    static const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    static const httpd_uri_t rescan = { .uri = "/rescan", .method = HTTP_GET, .handler = rescan_handler };
    static const httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_handler };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &rescan);
    httpd_register_uri_handler(server, &save);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_handler);
}

// Fallback setup only: if nobody is on the setup network, restart and try the saved
// Wi-Fi again (the router may simply have been off). Otherwise check again later.
static void retry_timer_cb(void *arg)
{
    wifi_sta_list_t list;
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0) {
        ESP_LOGI(TAG, "Setup page in use, not restarting yet");
        esp_timer_start_once(*(esp_timer_handle_t *)arg, RETRY_AFTER_US);
        return;
    }
    ESP_LOGI(TAG, "No one is setting up, restarting to retry Wi-Fi");
    esp_restart();
}

void portal_start(bool after_failure)
{
    // Redirected captive-portal traffic produces a lot of harmless warnings.
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    net_init();

    wifi_config_t ap = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap.ap.ssid, portal_ap_ssid(), sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(portal_ap_ssid());

    // APSTA: the station side is used for scanning. After a failed connect the
    // driver is already running in STA mode; adding the AP is enough.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (!after_failure)
        ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Access point '%s' started, setup page at http://192.168.4.1", ap_ssid);

    scan_networks();
    start_webserver();

    dns_server_config_t dns = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    start_dns_server(&dns);

    if (after_failure) {
        static esp_timer_handle_t timer;
        const esp_timer_create_args_t args = {
            .callback = retry_timer_cb,
            .arg = &timer,
            .name = "setup_retry",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
        ESP_ERROR_CHECK(esp_timer_start_once(timer, RETRY_AFTER_US));
    }
}
