idf_component_register(SRCS "wifi_ap.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES device_settings esp_wifi esp_event esp_netif)
