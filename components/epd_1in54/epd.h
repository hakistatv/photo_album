/*
 * Minimal driver for the 1.54" 200x200 e-paper panel (SSD1681-family
 * controller) on the Waveshare ESP32-S3-ePaper-1.54 (V1) board.
 *
 * Pin map, power-enable polarity, controller init sequence, and LUT
 * waveform tables are ported verbatim from Waveshare's official example:
 * https://github.com/waveshareteam/ESP32-S3-ePaper-1.54
 * (02_Example/ESP-IDF/V1/11_FactoryProgram/components/port_bsp/)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define EPD_BUFFER_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)

#define EPD_SPI_HOST    SPI2_HOST

#define EPD_DC_PIN      GPIO_NUM_10
#define EPD_CS_PIN      GPIO_NUM_11
#define EPD_SCK_PIN     GPIO_NUM_12
#define EPD_MOSI_PIN    GPIO_NUM_13
#define EPD_RST_PIN     GPIO_NUM_9
#define EPD_BUSY_PIN    GPIO_NUM_8

/* Panel power rail enable, active-LOW (per Waveshare port_power.cpp). */
#define EPD_PWR_PIN     GPIO_NUM_6

typedef enum {
    EPD_COLOR_BLACK = 0,
    EPD_COLOR_WHITE = 1,
} epd_color_t;

/* Enables the EPD_PWR_PIN GPIO as output (call once before epd_power_on). */
void epd_power_init(void);
/* Drives the panel's power rail on/off (active-low enable). */
void epd_power_on(void);
void epd_power_off(void);

/* Sets up SPI + control GPIOs and runs the full controller init/reset
 * sequence with the full-refresh LUT. Call after epd_power_on(). */
void epd_init(void);

/* Fills the framebuffer white. Does not touch the panel until epd_display(). */
void epd_clear(void);

/* Sets one pixel in the framebuffer (does not touch the panel). */
void epd_draw_pixel(uint16_t x, uint16_t y, epd_color_t color);

/* Pushes the framebuffer to the panel and performs a full refresh. */
void epd_display(void);

/* Read-only access to the internal framebuffer (EPD_BUFFER_SIZE bytes) --
 * e.g. for persisting the currently-drawn image to storage. Reflects
 * whatever was last drawn via epd_clear()/epd_draw_pixel(); not a
 * snapshot, so don't hold the pointer across further draw calls. */
const uint8_t *epd_get_framebuffer(void);

/* Overwrites the internal framebuffer directly (len must equal
 * EPD_BUFFER_SIZE) -- e.g. for restoring a previously-saved image. Does
 * not touch the panel until epd_display(). */
void epd_set_framebuffer(const uint8_t *buf, size_t len);

/* Serializes access to the framebuffer + panel across tasks (the web
 * server's upload handler and the boot button's slot-cycle handler both
 * touch them). Wrap any render/save-then-display or load-then-display
 * sequence in epd_lock()/epd_unlock() so the two can't interleave. Created
 * by epd_init() -- don't call before that. */
void epd_lock(void);
void epd_unlock(void);

#ifdef __cplusplus
}
#endif
