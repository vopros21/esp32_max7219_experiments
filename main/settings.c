#include <string.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "settings.h"

#define NVS_NAMESPACE "hubithab"

static const char *TAG = "settings";

settings_t settings;

static void load_str(nvs_handle_t h, const char *key, char *out, size_t size)
{
    size_t len = size;
    if (nvs_get_str(h, key, out, &len) != ESP_OK)
        out[0] = '\0';
}

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    memset(&settings, 0, sizeof(settings));
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No settings saved");
        return;
    }
    load_str(h, "ssid", settings.ssid, sizeof(settings.ssid));
    load_str(h, "pass", settings.pass, sizeof(settings.pass));
    load_str(h, "gh_user", settings.gh_user, sizeof(settings.gh_user));
    load_str(h, "gh_token", settings.gh_token, sizeof(settings.gh_token));
    load_str(h, "tz", settings.tz, sizeof(settings.tz));
    nvs_close(h);
    ESP_LOGI(TAG, "Loaded: Wi-Fi '%s', GitHub '%s', token %s, TZ '%s'",
             settings.ssid, settings.gh_user, settings.gh_token[0] ? "set" : "not set", settings.tz);
}

bool settings_configured(void)
{
    return settings.ssid[0] != '\0';
}

esp_err_t settings_save(const settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    if ((err = nvs_set_str(h, "ssid", s->ssid)) == ESP_OK &&
        (err = nvs_set_str(h, "pass", s->pass)) == ESP_OK &&
        (err = nvs_set_str(h, "gh_user", s->gh_user)) == ESP_OK &&
        (err = nvs_set_str(h, "gh_token", s->gh_token)) == ESP_OK &&
        (err = nvs_set_str(h, "tz", s->tz)) == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK)
        settings = *s;
    return err;
}

esp_err_t settings_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err;
}
