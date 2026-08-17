#include "esp_log.h"
#include "mdns.h"
#include "device_settings.h"
#include "mdns_service.h"

static const char *TAG = "mdns_service";

void start_mdns(void) {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(current_hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set("photo_album"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "mDNS started: http://%s.local/", current_hostname);
}
