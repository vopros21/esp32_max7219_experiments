#pragma once

#include <stdbool.h>
#include <stddef.h>

// Network stack, event loop and Wi-Fi driver. Safe to call more than once.
void net_init(void);

// Joins the saved network and blocks until the first IP address (true) or until
// WIFI_FIRST_ATTEMPTS attempts have failed (false, `why` says what went wrong).
// After a successful first connection, drops are retried forever in the background.
bool wifi_connect(char *why, size_t why_size);
