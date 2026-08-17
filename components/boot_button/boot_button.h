#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Invoked (from a dedicated FreeRTOS task, not an ISR -- safe to do real
 * work like SPI transactions in it) once per debounced press of the BOOT
 * button. */
typedef void (*boot_button_cb_t)(void);

/* Starts watching the BOOT button (GPIO0, per
 * docs/waveshare-esp32-s3-epaper-1.54-reference.md) and calls `cb` on each
 * debounced press. Call once at startup. */
void boot_button_init(boot_button_cb_t cb);

#ifdef __cplusplus
}
#endif
