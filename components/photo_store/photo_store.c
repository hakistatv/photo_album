/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Persists e-paper framebuffers into fixed-size slots on the "photos" data
 * partition (see partitions.csv). Each slot is a 4-byte magic number
 * followed by an EPD_BUFFER_SIZE-byte framebuffer -- the magic is what lets
 * us tell an occupied slot apart from an empty (erased) one, since erased
 * flash reads back as all-0xFF, which is indistinguishable from a genuine
 * all-white framebuffer without it.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "epd.h"
#include "photo_store.h"

static const char *TAG = "photo_store";

/* 2 erase blocks (4KB each) per slot -- comfortably covers the 4-byte magic
 * plus EPD_BUFFER_SIZE (5000 bytes), and esp_partition_erase_range()
 * requires erase-block-aligned offsets/lengths. */
#define SLOT_SIZE ((size_t)8192)
#define SLOT_MAGIC ((uint32_t)0x504F4831) /* "PHO1", arbitrary sentinel */

static const esp_partition_t *s_part = NULL;

static bool slot_valid(int slot) {
    return slot >= 1 && slot <= PHOTO_STORE_NUM_SLOTS;
}

bool photo_store_init(void) {
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "photos");
    if (!s_part) {
        ESP_LOGE(TAG, "\"photos\" partition not found -- check partitions.csv");
        return false;
    }
    if (s_part->size < SLOT_SIZE * PHOTO_STORE_NUM_SLOTS) {
        ESP_LOGE(TAG, "\"photos\" partition too small: %u bytes (need %u)",
                 (unsigned)s_part->size, (unsigned)(SLOT_SIZE * PHOTO_STORE_NUM_SLOTS));
        s_part = NULL;
        return false;
    }
    ESP_LOGI(TAG, "photo_store ready (%d slots, %u bytes each)", PHOTO_STORE_NUM_SLOTS, (unsigned)SLOT_SIZE);
    return true;
}

bool photo_store_save(int slot, const uint8_t *framebuffer, size_t len) {
    if (!s_part || !slot_valid(slot) || len != EPD_BUFFER_SIZE) {
        return false;
    }
    size_t offset = (size_t)(slot - 1) * SLOT_SIZE;

    esp_err_t err = esp_partition_erase_range(s_part, offset, SLOT_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase slot %d failed: %s", slot, esp_err_to_name(err));
        return false;
    }

    uint32_t magic = SLOT_MAGIC;
    err = esp_partition_write(s_part, offset, &magic, sizeof(magic));
    if (err == ESP_OK) {
        err = esp_partition_write(s_part, offset + sizeof(magic), framebuffer, len);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write slot %d failed: %s", slot, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Saved slot %d (%u bytes)", slot, (unsigned)len);
    return true;
}

bool photo_store_is_occupied(int slot) {
    if (!s_part || !slot_valid(slot)) {
        return false;
    }
    size_t offset = (size_t)(slot - 1) * SLOT_SIZE;
    uint32_t magic = 0;
    if (esp_partition_read(s_part, offset, &magic, sizeof(magic)) != ESP_OK) {
        return false;
    }
    return magic == SLOT_MAGIC;
}

bool photo_store_load(int slot, uint8_t *out, size_t len) {
    if (len != EPD_BUFFER_SIZE || !photo_store_is_occupied(slot)) {
        return false;
    }
    size_t offset = (size_t)(slot - 1) * SLOT_SIZE + sizeof(uint32_t);
    return esp_partition_read(s_part, offset, out, len) == ESP_OK;
}

bool photo_store_clear(int slot) {
    if (!s_part || !slot_valid(slot)) {
        return false;
    }
    size_t offset = (size_t)(slot - 1) * SLOT_SIZE;

    esp_err_t err = esp_partition_erase_range(s_part, offset, SLOT_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear slot %d failed: %s", slot, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Cleared slot %d", slot);
    return true;
}
