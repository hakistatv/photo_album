#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "device_config.h"
#include "device_settings.h"

static const char *TAG = "device_settings";

#define CFG_NVS_NAMESPACE "dev_cfg"
#define CFG_KEY_SSID      "wifi_ssid"
#define CFG_KEY_PASS      "wifi_pass"
#define CFG_KEY_HOST      "mdns_host"

char current_ssid[SSID_MAX_LEN + 1];
char current_pass[PASS_MAX_LEN + 1];
char current_hostname[HOST_MAX_LEN + 1];

void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void load_runtime_config(void) {
    /* current_* are zero-initialized static storage, so even if the
     * default is exactly SSID_MAX_LEN/PASS_MAX_LEN/HOST_MAX_LEN chars long
     * (leaving no room for strncpy to add its own '\0'), the buffer's last
     * byte is still 0 from that zero-initialization. */
    strncpy(current_ssid, WIFI_AP_SSID, sizeof(current_ssid) - 1);
    strncpy(current_pass, WIFI_AP_PASS, sizeof(current_pass) - 1);
    strncpy(current_hostname, MDNS_HOSTNAME, sizeof(current_hostname) - 1);

    nvs_handle_t nvs;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No saved settings in NVS yet -- using device_config.h defaults");
        return;
    }

    size_t len = sizeof(current_ssid);
    nvs_get_str(nvs, CFG_KEY_SSID, current_ssid, &len);
    len = sizeof(current_pass);
    nvs_get_str(nvs, CFG_KEY_PASS, current_pass, &len);
    len = sizeof(current_hostname);
    nvs_get_str(nvs, CFG_KEY_HOST, current_hostname, &len);

    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded settings from NVS: SSID=\"%s\" hostname=\"%s\"", current_ssid, current_hostname);
}

esp_err_t save_runtime_config(const char *ssid, const char *pass, const char *hostname) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, CFG_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, CFG_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, CFG_KEY_HOST, hostname);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}
