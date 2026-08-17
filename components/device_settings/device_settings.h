#pragma once

#include "esp_err.h"

/*
 * Runtime-configurable settings (Wi-Fi SSID/password, mDNS hostname),
 * editable via the /settings web page (see web_server.h) and persisted in
 * NVS so they survive reboots. device_config.h supplies the defaults used
 * the first time the device boots (or after an NVS erase).
 */

#define SSID_MAX_LEN 32
#define PASS_MAX_LEN 64
#define HOST_MAX_LEN 32

extern char current_ssid[SSID_MAX_LEN + 1];
extern char current_pass[PASS_MAX_LEN + 1];
extern char current_hostname[HOST_MAX_LEN + 1];

/* Initializes the NVS flash partition. Call once, before
 * load_runtime_config() or save_runtime_config(). */
void init_nvs(void);

/* Loads current_ssid/current_pass/current_hostname from NVS, falling back
 * to the device_config.h defaults for any value not yet saved. */
void load_runtime_config(void);

/* Persists new settings to NVS. Does not apply them or restart the device
 * -- callers (e.g. the /settings POST handler) are responsible for that. */
esp_err_t save_runtime_config(const char *ssid, const char *pass, const char *hostname);
