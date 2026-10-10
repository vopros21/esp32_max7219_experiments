#pragma once

#include <stdbool.h>
#include <esp_err.h>

// User settings, entered on the setup page and kept in NVS.
typedef struct {
    char ssid[33];        // Wi-Fi network (802.11 max 32 bytes)
    char pass[65];        // Wi-Fi password (WPA2 max 64)
    char gh_user[40];     // GitHub username (max 39)
    char gh_token[128];   // GitHub token, optional; fine-grained tokens are ~93 chars
    char tz[64];          // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
} settings_t;

extern settings_t settings;

// Initialises NVS and loads the settings (missing keys stay empty).
void settings_init(void);
bool settings_configured(void);
esp_err_t settings_save(const settings_t *s);
esp_err_t settings_erase(void);
