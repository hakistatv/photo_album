#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "device_config.h"
#include "device_settings.h"
#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

void wifi_ap_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    strlcpy((char *)wifi_config.ap.ssid, current_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(current_ssid);
    strlcpy((char *)wifi_config.ap.password, current_pass, sizeof(wifi_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started: SSID=\"%s\" channel=%d", current_ssid, WIFI_AP_CHANNEL);
    ESP_LOGI(TAG, "Connect, then browse to http://192.168.4.1/ or http://%s.local/", current_hostname);
}
