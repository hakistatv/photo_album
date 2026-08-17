#include <stdlib.h>
#include "esp_log.h"
#include "esp_jpeg_dec.h"
#include "epd.h"
#include "jpeg_display.h"

static const char *TAG = "jpeg_display";

/* Atkinson dithering error-diffusion offsets (dx, dy) from the pixel just
 * decided, each carrying 1/8 of its quantization error -- only 6/8 of the
 * error is redistributed (unlike Floyd-Steinberg's full 8/8), which keeps
 * blacks/whites more saturated and tends to look better on a 2-level
 * (pure black/white) e-paper display than full error diffusion does. */
static const struct { int dx; int dy; } ATKINSON_OFFSETS[6] = {
    {1, 0}, {2, 0},
    {-1, 1}, {0, 1}, {1, 1},
    {0, 2},
};

static void rgb888_to_grayscale(const uint8_t *rgb, int16_t *gray, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        uint8_t r = rgb[i * 3 + 0];
        uint8_t g = rgb[i * 3 + 1];
        uint8_t b = rgb[i * 3 + 2];
        /* ITU-R BT.601 luma, integer-approximated weights summing to 256. */
        gray[i] = (int16_t)((77 * r + 150 * g + 29 * b) >> 8);
    }
}

/* Dithers `gray` (width*height, values nominally 0-255 but may run outside
 * that range mid-diffusion) in place and draws the resulting 1-bit image
 * straight into the e-paper framebuffer. */
static void atkinson_dither_and_draw(int16_t *gray, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int16_t old_val = gray[idx];
            int16_t new_val = (old_val < 128) ? 0 : 255;
            int16_t err = (int16_t)(old_val - new_val);

            epd_draw_pixel((uint16_t)x, (uint16_t)y, new_val == 0 ? EPD_COLOR_BLACK : EPD_COLOR_WHITE);

            for (int i = 0; i < 6; i++) {
                int nx = x + ATKINSON_OFFSETS[i].dx;
                int ny = y + ATKINSON_OFFSETS[i].dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    gray[ny * width + nx] = (int16_t)(gray[ny * width + nx] + err / 8);
                }
            }
        }
    }
}

bool jpeg_display_render(const uint8_t *data, size_t len) {
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();

    jpeg_dec_handle_t dec = NULL;
    jpeg_error_t err = jpeg_dec_open(&config, &dec);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open failed: %d", err);
        return false;
    }

    jpeg_dec_io_t io = {0};
    io.inbuf = (uint8_t *)data;
    io.inbuf_len = (int)len;

    jpeg_dec_header_info_t info = {0};
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_parse_header failed: %d (not a valid JPEG?)", err);
        jpeg_dec_close(dec);
        return false;
    }

    if (info.width != EPD_WIDTH || info.height != EPD_HEIGHT) {
        ESP_LOGE(TAG, "Wrong image size: %ux%u (need %dx%d)",
                 (unsigned)info.width, (unsigned)info.height, EPD_WIDTH, EPD_HEIGHT);
        jpeg_dec_close(dec);
        return false;
    }

    int outbuf_len = 0;
    err = jpeg_dec_get_outbuf_len(dec, &outbuf_len);
    if (err != JPEG_ERR_OK || outbuf_len <= 0) {
        ESP_LOGE(TAG, "jpeg_dec_get_outbuf_len failed: %d", err);
        jpeg_dec_close(dec);
        return false;
    }

    /* ESP32-S3 requires a 16-byte aligned decode output buffer -- see
     * jpeg_dec_process()'s doc comment in esp_jpeg_dec.h. */
    uint8_t *rgb_buf = jpeg_calloc_align((size_t)outbuf_len, 16);
    if (!rgb_buf) {
        ESP_LOGE(TAG, "Out of memory allocating %d-byte RGB888 decode buffer", outbuf_len);
        jpeg_dec_close(dec);
        return false;
    }
    io.outbuf = rgb_buf;

    err = jpeg_dec_process(dec, &io);
    jpeg_dec_close(dec);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_process failed: %d", err);
        jpeg_free_align(rgb_buf);
        return false;
    }

    /* Convert grayscale in place, reusing rgb_buf's own memory instead of
     * allocating a second large buffer -- the decode pipeline is already
     * tight on heap (RGB888 alone is ~120KB) alongside Wi-Fi/httpd/mDNS.
     * This is safe because each pixel only needs 2 bytes (int16_t) as
     * grayscale vs. the 3 it started with as RGB: after processing pixel i,
     * the write frontier is at byte 2*(i+1)-1, and the next unread source
     * pixel starts at byte 3*(i+1) -- since 2*(i+1)-1 < 3*(i+1) always, the
     * write side can never catch up to and clobber data not yet read. */
    int16_t *gray = (int16_t *)rgb_buf;
    rgb888_to_grayscale(rgb_buf, gray, EPD_WIDTH, EPD_HEIGHT);

    epd_clear();
    atkinson_dither_and_draw(gray, EPD_WIDTH, EPD_HEIGHT);
    jpeg_free_align(rgb_buf); /* also frees `gray`, which points into the same block */

    ESP_LOGI(TAG, "JPEG decoded and dithered into framebuffer (%ux%u)",
             (unsigned)info.width, (unsigned)info.height);
    return true;
}
