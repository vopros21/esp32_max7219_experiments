#pragma once

#include <stdbool.h>
#include <freertos/FreeRTOS.h>

typedef enum {
    EV_CLICK,       // short press: next mode
    EV_LONG,        // held ~1 s: mode action
    EV_VERY_LONG,   // held VERY_LONG_PRESS_MS, comes after EV_LONG
    EV_PRESS,       // raw press / release, used by the display loop to time long holds
    EV_RELEASE,
} button_event_t;

void buttons_init(void);
bool buttons_receive(button_event_t *ev, TickType_t timeout);
