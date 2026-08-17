/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Decodes a JPEG file, converts it to grayscale, dithers it to 1-bit
 * (Atkinson dithering), and renders it into the epd_1in54 framebuffer.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes `data`/`len` as a JPEG file and draws it into the e-paper
 * framebuffer via epd_clear()/epd_draw_pixel() -- does not call
 * epd_display(); the caller controls when to actually refresh the panel.
 *
 * The JPEG's pixel dimensions must be exactly 200x200 (EPD_WIDTH x
 * EPD_HEIGHT) -- no cropping/scaling is done. Returns false (logging the
 * reason) if the file isn't a decodable JPEG or isn't exactly 200x200. */
bool jpeg_display_render(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
