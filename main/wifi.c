#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>

#include "wifi.h"
#include "settings.h"
#include "modes.h"

#define WIFI_FIRST_ATTEMPTS 3

#define BIT_CONNECTED BIT0
#define BIT_FAILED    BIT1

static const char *TAG = "wifi";

static EventGroupHandle_t events;
static esp_netif_t *sta_netif;
static bool ever_connected;
static int attempts;
static uint8_t last_reason;

void net_init(void)
{
    static bool done;
    if (done)
        return;
    done = true;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(sta_netif, "hubithab");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));  // settings live in our own NVS keys
}

static const char *reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "network not found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "wrong password?";
    default:
        return NULL;
    }
}

// Runs in the default event loop task: keep it short, no blocking calls.
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        last_reason = ev->reason;
        ESP_LOGW(TAG, "Disconnected, reason %d", ev->reason);

        if (ever_connected) {
            text_set_status("Wi-Fi lost, reconnecting to %s...", settings.ssid);
            esp_wifi_connect();
        } else if (++attempts < WIFI_FIRST_ATTEMPTS) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(events, BIT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ever_connected = true;
        text_set_status("Wi-Fi OK: %s", ip);
        xEventGroupSetBits(events, BIT_CONNECTED);
    }
}

bool wifi_connect(char *why, size_t why_size)
{
    net_init();
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, settings.ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, settings.pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = settings.pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;

    text_set_status("Connecting to %s...", settings.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(events, BIT_CONNECTED | BIT_FAILED,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & BIT_CONNECTED)
        return true;

    const char *text = reason_text(last_reason);
    if (text)
        snprintf(why, why_size, "%s", text);
    else
        snprintf(why, why_size, "error %d", last_reason);
    ESP_LOGW(TAG, "Giving up on '%s': %s", settings.ssid, why);
    return false;
}
