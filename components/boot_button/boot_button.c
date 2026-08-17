#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "boot_button.h"

static const char *TAG = "boot_button";

/* BOOT button, per docs/waveshare-esp32-s3-epaper-1.54-reference.md --
 * active-low (pulled up on-board; reads 0 when pressed). It's also the
 * ESP32-S3's download-mode strapping pin, but that only matters during
 * reset/power-on -- safe to read as a plain GPIO input once the app is
 * running. */
#define BOOT_BUTTON_PIN GPIO_NUM_0

#define POLL_INTERVAL_MS 20
#define DEBOUNCE_POLLS   3 /* ~60ms of a stable level before a transition counts */

/* Simple poll-and-debounce loop (no interrupt/ISR) -- the button is pressed
 * at most a few times a minute, so there's no responsiveness cost to
 * polling every 20ms, and it keeps this in line with the rest of the
 * project's hand-rolled, non-ISR-driven drivers (see epd_1in54). Fires
 * `cb` once per press (transition low), not on release, and not again
 * until the button has gone back high and been re-pressed. */
static void boot_button_task(void *arg) {
    boot_button_cb_t cb = (boot_button_cb_t)arg;
    bool pressed = false;
    int stable_count = 0;

    while (1) {
        bool is_low = gpio_get_level(BOOT_BUTTON_PIN) == 0;

        if (is_low == pressed) {
            stable_count = 0; /* matches what we already believe -- nothing changing */
        } else if (++stable_count >= DEBOUNCE_POLLS) {
            pressed = is_low;
            stable_count = 0;
            if (pressed) {
                ESP_LOGI(TAG, "BOOT button pressed");
                cb();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void boot_button_init(boot_button_cb_t cb) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io));

    xTaskCreate(boot_button_task, "boot_button", 4096, (void *)cb, 5, NULL);
    ESP_LOGI(TAG, "Watching BOOT button (GPIO%d) for slot-cycle presses", BOOT_BUTTON_PIN);
}
