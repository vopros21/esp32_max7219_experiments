#pragma once

// Setup mode: an open access point "HubitHab-XXXX" with a captive portal.
// Every DNS name resolves to the board, and any unknown URL redirects to the
// setup form, so phones open it automatically. Saving the form reboots.

// Access point name, derived from the MAC. Valid before portal_start().
const char *portal_ap_ssid(void);

void portal_start(void);
