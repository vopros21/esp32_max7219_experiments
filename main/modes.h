#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "buttons.h"

// A mode draws into fb. The display loop calls render() every tick and flushes
// fb only when render() reports a change.
typedef struct {
    const char *name;
    void (*enter)(uint32_t now_ms);
    bool (*render)(uint32_t now_ms);           // returns true if fb changed
    void (*on_button)(button_event_t ev);      // EV_LONG / EV_VERY_LONG, may be NULL
} display_mode_t;

extern const display_mode_t modes[];
extern const size_t mode_count;
extern const size_t mode_github;     // indexes in modes[]
extern const size_t mode_pomodoro;
extern const size_t mode_status;

// Status line shown by the Status mode. Safe to call from any task.
void text_set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// No network time yet: start the system clock from the build time.
// Call after the time zone has been set.
void clock_set_initial_time(void);

// SNTP has set the time; hides the "not synced" pixel. Safe from any task.
void clock_set_synced(void);

// Runs the Pomodoro timer in any mode. Returns true when a phase has just ended.
bool pomodoro_tick(uint32_t now_ms);
