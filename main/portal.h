#pragma once

// Setup mode: an open access point "HubitHab-XXXX" with a captive portal.
// Every DNS name resolves to the board, and any unknown URL redirects to the
// setup form, so phones open it automatically. Saving the form reboots.

// Access point name, derived from the MAC. Valid before portal_start().
const char *portal_ap_ssid(void);

#include <stdbool.h>

// after_failure: the saved Wi-Fi could not be joined. The driver is already
// running, and the board restarts after 5 min without a client to retry.
void portal_start(bool after_failure);
