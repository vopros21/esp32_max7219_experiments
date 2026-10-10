#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>

#include "github.h"
#include "settings.h"
#include "modes.h"

#define REFRESH_MS (15 * 60 * 1000)
#define RETRY_MS (60 * 1000)
#define MAX_RESPONSE (48 * 1024)   // 32 weeks of calendar JSON is ~15 KB

static const char *TAG = "github";

static SemaphoreHandle_t lock;
static github_data_t data;

bool github_get(github_data_t *out)
{
    if (!lock)
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = data;
    xSemaphoreGive(lock);
    return out->valid;
}

// ---------------------------------------------------------------------------
// HTTP

typedef struct {
    char *buf;
    int len;
    int status;
} response_t;

// Performs one request and reads the whole body into a heap buffer (caller frees).
static esp_err_t request(const char *url, const char *body, response_t *resp)
{
    memset(resp, 0, sizeof(*resp));

    esp_http_client_config_t cfg = {
        .url = url,
        .method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return ESP_FAIL;

    esp_http_client_set_header(client, "User-Agent", "HubitHab");  // GitHub requires one
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    char auth[160];
    if (settings.gh_token[0]) {
        snprintf(auth, sizeof(auth), "Bearer %s", settings.gh_token);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    if (body)
        esp_http_client_set_header(client, "Content-Type", "application/json");

    int body_len = body ? strlen(body) : 0;
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err == ESP_OK && body_len && esp_http_client_write(client, body, body_len) != body_len)
        err = ESP_FAIL;
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0)
        err = ESP_FAIL;

    if (err == ESP_OK) {
        resp->status = esp_http_client_get_status_code(client);
        resp->buf = malloc(MAX_RESPONSE + 1);
        if (!resp->buf) {
            err = ESP_ERR_NO_MEM;
        } else {
            int r;
            while (resp->len < MAX_RESPONSE &&
                   (r = esp_http_client_read(client, resp->buf + resp->len, MAX_RESPONSE - resp->len)) > 0)
                resp->len += r;
            resp->buf[resp->len] = '\0';
            if (!esp_http_client_is_complete_data_received(client)) {
                ESP_LOGW(TAG, "Response incomplete or larger than %d bytes", MAX_RESPONSE);
                err = ESP_FAIL;
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(resp->buf);
        resp->buf = NULL;
    }
    return err;
}

static const char *status_error(int status)
{
    switch (status) {
    case 401: return "bad token";
    case 403:
    case 429: return "rate limited";
    case 404: return "user not found";
    default: return "server error";
    }
}

// ---------------------------------------------------------------------------
// Fetchers. Each fills `out` and returns NULL, or returns a short error text.

// Without a token: public profile only.
static const char *fetch_profile(github_data_t *out)
{
    for (const char *p = settings.gh_user; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-')
            return "bad username";
    }

    char url[96];
    snprintf(url, sizeof(url), "https://api.github.com/users/%s", settings.gh_user);
    response_t resp;
    if (request(url, NULL, &resp) != ESP_OK)
        return "network error";

    const char *error = NULL;
    cJSON *root = NULL;
    if (resp.status != 200)
        error = status_error(resp.status);
    else if (!(root = cJSON_Parse(resp.buf)))
        error = "bad response";
    else {
        out->followers = cJSON_GetObjectItem(root, "followers") ?
                         cJSON_GetObjectItem(root, "followers")->valueint : 0;
        out->repos = cJSON_GetObjectItem(root, "public_repos") ?
                     cJSON_GetObjectItem(root, "public_repos")->valueint : 0;
    }
    cJSON_Delete(root);
    free(resp.buf);
    return error;
}

static cJSON *path(cJSON *node, const char *const *keys)
{
    for (; node && *keys; keys++)
        node = cJSON_GetObjectItem(node, *keys);
    return node;
}

static void iso_utc(time_t t, char *buf, size_t size)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

// With a token: GraphQL, the only API that has the contribution calendar.
static const char *fetch_calendar(github_data_t *out)
{
    // Start at local midnight of the Sunday GH_WEEKS - 1 weeks ago, so the first
    // week is complete and the last one is the current week.
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_mday -= tm.tm_wday + (GH_WEEKS - 1) * 7;
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    char from[24], to[24];
    iso_utc(mktime(&tm), from, sizeof(from));
    iso_utc(now, to, sizeof(to));

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "query",
        "query($login:String!,$from:DateTime!,$to:DateTime!){user(login:$login){"
        "followers{totalCount} repositories(ownerAffiliations:OWNER,privacy:PUBLIC){totalCount} "
        "contributionsCollection(from:$from,to:$to){contributionCalendar{"
        "weeks{contributionDays{contributionCount weekday}}}}}}");
    cJSON *vars = cJSON_AddObjectToObject(req, "variables");
    cJSON_AddStringToObject(vars, "login", settings.gh_user);
    cJSON_AddStringToObject(vars, "from", from);
    cJSON_AddStringToObject(vars, "to", to);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body)
        return "out of memory";

    response_t resp;
    esp_err_t err = request("https://api.github.com/graphql", body, &resp);
    free(body);
    if (err != ESP_OK)
        return "network error";

    const char *error = NULL;
    cJSON *root = NULL;
    cJSON *user;
    cJSON *weeks;
    if (resp.status != 200) {
        error = status_error(resp.status);
    } else if (!(root = cJSON_Parse(resp.buf))) {
        error = "bad response";
    } else if (!cJSON_IsObject(user = path(root, (const char *[]){ "data", "user", NULL }))) {
        // GraphQL reports a missing user as data.user = null plus an errors array.
        error = cJSON_GetObjectItem(root, "errors") ? "user not found" : "bad response";
    } else if (!cJSON_IsArray(weeks = path(user, (const char *[]){
                   "contributionsCollection", "contributionCalendar", "weeks", NULL }))) {
        error = "bad response";
    } else {
        cJSON *n;
        if ((n = path(user, (const char *[]){ "followers", "totalCount", NULL })))
            out->followers = n->valueint;
        if ((n = path(user, (const char *[]){ "repositories", "totalCount", NULL })))
            out->repos = n->valueint;

        for (int w = 0; w < GH_WEEKS; w++)
            for (int d = 0; d < 7; d++)
                out->days[w][d] = GH_NO_DAY;

        // Right-align the weeks: the last one returned is the current week.
        int count = cJSON_GetArraySize(weeks);
        int last_w = -1, last_d = -1;
        for (int i = 0; i < count; i++) {
            int w = GH_WEEKS - count + i;
            if (w < 0)
                continue;
            cJSON *day;
            cJSON_ArrayForEach(day, cJSON_GetObjectItem(cJSON_GetArrayItem(weeks, i), "contributionDays")) {
                int d = cJSON_GetObjectItem(day, "weekday")->valueint;
                int c = cJSON_GetObjectItem(day, "contributionCount")->valueint;
                if (d < 0 || d > 6)
                    continue;
                out->days[w][d] = c > 0xFFFE ? 0xFFFE : c;
                last_w = w;
                last_d = d;
            }
        }

        // Today is the last day returned. A streak survives a quiet today until the
        // day is over, like on GitHub.
        out->today = last_w >= 0 ? out->days[last_w][last_d] : 0;
        out->streak = 0;
        bool skip_today = out->today == 0;
        for (int w = last_w; w >= 0; w--) {
            for (int d = (w == last_w ? last_d : 6); d >= 0; d--) {
                if (skip_today) {
                    skip_today = false;
                    continue;
                }
                if (out->days[w][d] == 0 || out->days[w][d] == GH_NO_DAY)
                    goto streak_done;
                out->streak++;
            }
        }
streak_done:
        out->has_calendar = true;
    }
    cJSON_Delete(root);
    free(resp.buf);
    return error;
}

static void log_calendar(const github_data_t *d)
{
    static const char *names[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    for (int day = 0; day < 7; day++) {
        char line[GH_WEEKS + 1];
        for (int w = 0; w < GH_WEEKS; w++) {
            uint16_t c = d->days[w][day];
            line[w] = c == GH_NO_DAY ? ' ' : c ? '#' : '.';
        }
        line[GH_WEEKS] = '\0';
        ESP_LOGI(TAG, "%s %s", names[day], line);
    }
}

static void github_task(void *arg)
{
    // TLS checks certificate dates; give SNTP a moment after the connection.
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        github_data_t fresh = { 0 };
        const char *error = settings.gh_token[0] ? fetch_calendar(&fresh) : fetch_profile(&fresh);

        if (error) {
            ESP_LOGW(TAG, "Fetch failed: %s", error);
            text_set_status("GitHub: %s", error);
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
            continue;
        }

        fresh.valid = true;
        fresh.updated = time(NULL);
        xSemaphoreTake(lock, portMAX_DELAY);
        data = fresh;
        xSemaphoreGive(lock);

        if (fresh.has_calendar) {
            log_calendar(&fresh);
            text_set_status("GitHub %s: today %d, streak %d, %d followers",
                            settings.gh_user, fresh.today, fresh.streak, fresh.followers);
        } else {
            text_set_status("GitHub %s: %d followers, %d repos (add a token for the heatmap)",
                            settings.gh_user, fresh.followers, fresh.repos);
        }
        vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
    }
}

void github_start(void)
{
    if (!settings.gh_user[0]) {
        ESP_LOGI(TAG, "No GitHub username, not fetching");
        return;
    }
    lock = xSemaphoreCreateMutex();
    xTaskCreate(github_task, "github", 10240, NULL, 4, NULL);
}
