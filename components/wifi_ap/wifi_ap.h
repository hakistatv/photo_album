#pragma once

/* Brings up Wi-Fi in access-point mode using current_ssid/current_pass
 * (see device_settings.h) so a phone/laptop can connect directly to the
 * board (no home router needed). Call init_nvs() + load_runtime_config()
 * before this, and this before start_webserver()/start_mdns(). */
void wifi_ap_init(void);
