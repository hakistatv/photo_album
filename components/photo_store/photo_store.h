#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_STORE_NUM_SLOTS 5

/* Finds the "photos" data partition (see partitions.csv). Call once at
 * startup before any other photo_store_*() call. */
bool photo_store_init(void);

/* Saves a dithered e-paper framebuffer (EPD_BUFFER_SIZE bytes -- see
 * epd_get_framebuffer() in epd.h) into `slot` (1..PHOTO_STORE_NUM_SLOTS),
 * persisting it across reboots. Overwrites whatever was previously there. */
bool photo_store_save(int slot, const uint8_t *framebuffer, size_t len);

/* Loads `slot`'s framebuffer into `out` (must be EPD_BUFFER_SIZE bytes).
 * Returns false if the slot is empty (nothing saved yet) or on read failure. */
bool photo_store_load(int slot, uint8_t *out, size_t len);

/* True if `slot` currently holds a saved image. */
bool photo_store_is_occupied(int slot);

/* Removes whatever's saved in `slot` (a no-op, not an error, if it was
 * already empty). Does not touch whatever's currently drawn on the panel,
 * even if this was the slot last shown. */
bool photo_store_clear(int slot);

#ifdef __cplusplus
}
#endif
