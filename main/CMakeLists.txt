idf_component_register(SRCS "photo_album.c"
                       PRIV_REQUIRES epd_1in54 photo_store device_settings wifi_ap mdns_service web_server boot_button
                       INCLUDE_DIRS "")
