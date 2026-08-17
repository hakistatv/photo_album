#pragma once

#include "esp_http_server.h"

/* Starts the HTTP server:
 *   "/"         (GET)       Home -- links to Upload Image and Settings.
 *   "/upload"   (GET, POST) Shows the slot previews and an upload form;
 *                           POST accepts a multipart/form-data 200x200
 *                           JPEG upload plus a "slot" field, redraws the
 *                           e-paper display, and persists the image into
 *                           that slot -- see photo_store.h.
 *   "/slot/<n>" (GET)       Serves a stored slot's image as a BMP, for the
 *                           preview grid.
 *   "/slot/<n>/clear" (POST) Removes the stored image from slot n; does not
 *                           touch whatever's currently on the panel.
 *   "/settings" (GET, POST) Shows/updates the Wi-Fi SSID/password and mDNS
 *                           hostname -- see device_settings.h. Saving
 *                           restarts the device to apply them.
 * Returns NULL on failure. */
httpd_handle_t start_webserver(void);
