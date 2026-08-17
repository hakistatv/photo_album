idf_component_register(SRCS "web_server.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_server
                       PRIV_REQUIRES epd_1in54 jpeg_display photo_store device_settings
                       EMBED_TXTFILES "pages/home.html" "pages/upload.html" "pages/settings.html"
                                      "pages/restart.html" "pages/style.css")
