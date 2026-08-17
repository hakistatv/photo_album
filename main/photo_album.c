/*
 * Shows an uploaded 200x200 monochrome image on the Waveshare
 * ESP32-S3-ePaper-1.54 (V1) board's e-paper display. Serves a Wi-Fi AP +
 * web page so you can upload a new image (as a JPEG) at any time, and a
 * Settings page to change the Wi-Fi SSID/password/hostname. The BOOT
 * button cycles through whichever of the 5 slots (see photo_store.h)
 * currently hold a saved image.
 */

#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "epd.h"
#include "photo_store.h"
#include "device_settings.h"
#include "wifi_ap.h"
#include "mdns_service.h"
#include "web_server.h"
#include "boot_button.h"
#include "build_info.h"

static const char *TAG = "photo_album";

/* 0 = nothing shown via the button yet -- the next press starts from slot
 * 1 and searches forward, so this doesn't need to be a valid slot number. */
static int s_current_slot = 0;

/* Advances to the next occupied slot after s_current_slot, wrapping
 * around, and displays it. Slots with nothing saved are skipped; if none
 * are occupied (or only the current one is), this is a no-op or redisplays
 * it, respectively. */
static void cycle_to_next_slot(void) {
    for (int i = 1; i <= PHOTO_STORE_NUM_SLOTS; i++) {
        int slot = (s_current_slot + i - 1) % PHOTO_STORE_NUM_SLOTS + 1;
        if (!photo_store_is_occupied(slot)) {
            continue;
        }

        uint8_t *framebuffer = malloc(EPD_BUFFER_SIZE);
        if (!framebuffer) {
            ESP_LOGE(TAG, "Out of memory loading slot %d", slot);
            return;
        }

        /* Locked from load through epd_display() -- the web server's
         * upload handler also touches the framebuffer and panel from its
         * own task, and the two must not interleave. */
        epd_lock();
        bool ok = photo_store_load(slot, framebuffer, EPD_BUFFER_SIZE);
        if (ok) {
            epd_set_framebuffer(framebuffer, EPD_BUFFER_SIZE);
            epd_display();
        }
        epd_unlock();
        free(framebuffer);

        if (ok) {
            s_current_slot = slot;
            ESP_LOGI(TAG, "Showing slot %d", slot);
        } else {
            ESP_LOGW(TAG, "Failed to load slot %d", slot);
        }
        return;
    }
    ESP_LOGI(TAG, "BOOT button pressed, but no slots have a saved image yet");
}

void app_main(void) {
    ESP_LOGI(TAG, "photo_album starting up");
    /* Backup copy of the /origin route's provenance info (see
     * web_server.c, build_info.h) -- keeps FW_ORIGIN_MARK in the compiled
     * binary even if a fork strips the web server out entirely. */
    ESP_LOGI(TAG, "%s / %s (%s)", FW_ORIGIN_AUTHOR, FW_ORIGIN_REPO, FW_ORIGIN_MARK);

    init_nvs();
    load_runtime_config();

    if (!photo_store_init()) {
        ESP_LOGE(TAG, "photo_store_init failed -- image slots will not persist");
    }

    epd_power_init();
    epd_power_on();
    vTaskDelay(pdMS_TO_TICKS(10)); /* let the panel rail settle */
    epd_init();
    epd_clear();
    epd_display();
    ESP_LOGI(TAG, "Display cleared, waiting for an image upload");

    wifi_ap_init();
    start_mdns();
    start_webserver();
    boot_button_init(cycle_to_next_slot);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
