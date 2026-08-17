#pragma once

/* Advertises the board as http://<current_hostname>.local/ (see
 * device_settings.h) so clients that support Bonjour/mDNS (macOS, iOS,
 * Linux w/ avahi; Windows needs Bonjour installed) can browse to it by name
 * instead of the AP's IP. Call after wifi_ap_init(). */
void start_mdns(void);
