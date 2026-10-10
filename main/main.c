#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>

#include "display.h"
#include "buttons.h"
#include "modes.h"
#include "settings.h"
#include "portal.h"
#include "wifi.h"

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define TICK_MS 10                 // display loop period
#define VERY_LONG_PRESS_MS 3000    // hold time for EV_VERY_LONG
#define FACTORY_RESET_MS 10000     // hold time that erases settings and reboots

static const char *TAG = "main";

static void display_task(void *pvParameter)
{
    display_init();

    size_t mode = 0;
    modes[mode].enter(xTaskGetTickCount() * portTICK_PERIOD_MS);

    bool pressed = false;
    bool very_long_sent = false;
    uint32_t press_ms = 0;

    while (1) {
        button_event_t ev;
        uint32_t now_ms;
        if (buttons_receive(&ev, pdMS_TO_TICKS(TICK_MS))) {
            now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (ev == EV_PRESS) {
                pressed = true;
                very_long_sent = false;
                press_ms = now_ms;
            } else if (ev == EV_RELEASE) {
                pressed = false;
            } else if (ev == EV_CLICK) {
                mode = (mode + 1) % mode_count;
                ESP_LOGI(TAG, "Click: mode %s", modes[mode].name);
                modes[mode].enter(now_ms);
            } else {
                ESP_LOGI(TAG, "Long press in mode %s", modes[mode].name);
                if (modes[mode].on_button)
                    modes[mode].on_button(ev);
            }
        }
        now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (pressed && !very_long_sent && now_ms - press_ms >= VERY_LONG_PRESS_MS) {
            very_long_sent = true;
            ESP_LOGI(TAG, "Very long press in mode %s", modes[mode].name);
            if (modes[mode].on_button)
                modes[mode].on_button(EV_VERY_LONG);
        }

        if (pressed && now_ms - press_ms >= FACTORY_RESET_MS) {
            ESP_LOGW(TAG, "Button held for 10 s: erasing settings, restarting");
            settings_erase();
            esp_restart();
        }

        // A finished Pomodoro phase takes over the display from any mode.
        if (pomodoro_tick(now_ms) && mode != mode_pomodoro) {
            mode = mode_pomodoro;
            modes[mode].enter(now_ms);
        }

        if (modes[mode].render(now_ms))
            fb_flush();
    }
}

void app_main()
{
    clock_set_initial_time();
    settings_init();

    buttons_init();
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, APP_CPU_NUM);

    if (!settings_configured()) {
        text_set_status("Setup: join Wi-Fi %s, open 192.168.4.1", portal_ap_ssid());
        portal_start(false);
        return;
    }

    char why[32];
    if (!wifi_connect(why, sizeof(why))) {
        text_set_status("Can't join %s (%s). Setup: join Wi-Fi %s, open 192.168.4.1",
                        settings.ssid, why, portal_ap_ssid());
        portal_start(true);
    }
}
