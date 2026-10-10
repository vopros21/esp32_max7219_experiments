#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define GH_WEEKS 32         // one display column per week
#define GH_NO_DAY 0xFFFF    // days after today in the current week

typedef struct {
    bool valid;             // at least one successful fetch
    bool has_calendar;      // token set: calendar, today and streak are filled in
    uint16_t days[GH_WEEKS][7];  // contributions per day; [0] = oldest week, [w][0] = Sunday
    time_t start;           // local midnight of the Sunday of week 0
    int today_week, today_day;   // position of today in days[][]
    int today;
    int streak;             // consecutive days with contributions, ending today (or yesterday)
    int followers;
    int repos;              // public repositories
    time_t updated;
} github_data_t;

// Starts the background fetcher (call once Wi-Fi is up). Refreshes every 15 min.
void github_start(void);

// Copies the latest data. Returns false if nothing has been fetched yet.
bool github_get(github_data_t *out);

// True once the first fetch has succeeded. Cheap, no copy.
bool github_ready(void);
