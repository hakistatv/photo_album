#pragma once

/*
 * Compile-time defaults, used the first time the device boots (or after an
 * NVS erase). SSID/password/hostname become runtime-editable via the
 * /settings web page after that -- see device_settings.h.
 */

/* --- Wi-Fi access point --- */
#define WIFI_AP_SSID     "photo-album"
#define WIFI_AP_PASS     "qrcode123"
#define WIFI_AP_CHANNEL  1
#define WIFI_AP_MAX_CONN 4

/* --- mDNS --- */
/* Resolves as http://<MDNS_HOSTNAME>.local -- plain "photo-album" (no
 * .local) isn't a valid browser hostname. */
#define MDNS_HOSTNAME    "photo-album"
