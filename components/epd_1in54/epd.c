/*
 * See epd.h for provenance. Command sequence, timings, and LUT tables are
 * copied byte-for-byte from Waveshare's port_display.cpp for this board.
 */
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "epd.h"

static const char *TAG = "epd";

static spi_device_handle_t s_spi;
static uint8_t s_buffer[EPD_BUFFER_SIZE];
static SemaphoreHandle_t s_mutex;

/* Full-update LUT waveform table for the 1.54" panel (159 bytes: 153 LUT
 * bytes + 6 timing/voltage parameter bytes consumed by epd_set_lut()). */
static const uint8_t WF_FULL_1IN54[159] = {
    0x80,0x48,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x48,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x80,0x48,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x48,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xA,
    0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x1,0x0,0x8,0x1,0x0,0x2,
    0xA,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x22,0x22,0x22,0x22,0x22,0x22,0x0,
    0x0,0x0,0x22,0x17,0x41,0x0,0x32,0x20
};

static void set_cs(int level)  { gpio_set_level(EPD_CS_PIN, level); }
static void set_dc(int level)  { gpio_set_level(EPD_DC_PIN, level); }
static void set_rst(int level) { gpio_set_level(EPD_RST_PIN, level); }

static void read_busy(void) {
    /* BUSY is LOW: idle, HIGH: busy */
    while (gpio_get_level(EPD_BUSY_PIN) == 1) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void spi_send_byte(uint8_t data) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void epd_send_command(uint8_t command) {
    set_dc(0);
    set_cs(0);
    spi_send_byte(command);
    set_cs(1);
}

static void epd_send_data(uint8_t data) {
    set_dc(1);
    set_cs(0);
    spi_send_byte(data);
    set_cs(1);
}

static void write_bytes(const uint8_t *buf, int len) {
    set_dc(1);
    set_cs(0);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8 * len;
    t.tx_buffer = buf;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    set_cs(1);
}

static void epd_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end) {
    epd_send_command(0x44); /* SET_RAM_X_ADDRESS_START_END_POSITION */
    epd_send_data((x_start >> 3) & 0xFF);
    epd_send_data((x_end >> 3) & 0xFF);

    epd_send_command(0x45); /* SET_RAM_Y_ADDRESS_START_END_POSITION */
    epd_send_data(y_start & 0xFF);
    epd_send_data((y_start >> 8) & 0xFF);
    epd_send_data(y_end & 0xFF);
    epd_send_data((y_end >> 8) & 0xFF);
}

static void epd_set_cursor(uint16_t x_start, uint16_t y_start) {
    epd_send_command(0x4E); /* SET_RAM_X_ADDRESS_COUNTER */
    epd_send_data(x_start & 0xFF);

    epd_send_command(0x4F); /* SET_RAM_Y_ADDRESS_COUNTER */
    epd_send_data(y_start & 0xFF);
    epd_send_data((y_start >> 8) & 0xFF);
}

static void epd_set_lut(const uint8_t *lut) {
    epd_send_command(0x32);
    write_bytes(lut, 153);
    read_busy();

    epd_send_command(0x3f);
    epd_send_data(lut[153]);

    epd_send_command(0x03);
    epd_send_data(lut[154]);

    epd_send_command(0x04);
    epd_send_data(lut[155]);
    epd_send_data(lut[156]);
    epd_send_data(lut[157]);

    epd_send_command(0x2c);
    epd_send_data(lut[158]);
}

static void epd_turn_on_display(void) {
    epd_send_command(0x22);
    epd_send_data(0xc7);
    epd_send_command(0x20);
    read_busy();
}

void epd_power_init(void) {
    gpio_config_t io = {};
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = 0x1ULL << EPD_PWR_PIN;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io));
}

void epd_power_on(void) {
    gpio_set_level(EPD_PWR_PIN, 0); /* active-low */
}

void epd_power_off(void) {
    gpio_set_level(EPD_PWR_PIN, 1);
}

static void epd_hw_init(void) {
    ESP_LOGI(TAG, "Initializing EPD SPI bus + GPIOs");

    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = EPD_MOSI_PIN;
    buscfg.sclk_io_num = EPD_SCK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = EPD_WIDTH * EPD_HEIGHT;
    ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = 40 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.queue_size = 7;
    ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &devcfg, &s_spi));

    gpio_config_t io = {};
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (0x1ULL << EPD_RST_PIN) | (0x1ULL << EPD_DC_PIN) | (0x1ULL << EPD_CS_PIN);
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io));

    io.mode = GPIO_MODE_INPUT;
    io.pin_bit_mask = 0x1ULL << EPD_BUSY_PIN;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io));

    set_rst(1);
}

void epd_init(void) {
    /* Created here (before any other task -- Wi-Fi/web server, the boot
     * button poller -- is started) so epd_lock()/epd_unlock() are always
     * safe to call once app_main() has gotten this far. */
    s_mutex = xSemaphoreCreateMutex();

    epd_hw_init();

    set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    set_rst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    read_busy();
    epd_send_command(0x12); /* SWRESET */
    read_busy();

    epd_send_command(0x01); /* Driver output control */
    epd_send_data(0xC7);
    epd_send_data(0x00);
    epd_send_data(0x01);

    epd_send_command(0x11); /* Data entry mode */
    epd_send_data(0x01);

    epd_set_windows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);

    epd_send_command(0x3C); /* Border waveform */
    epd_send_data(0x01);

    epd_send_command(0x18);
    epd_send_data(0x80);

    epd_send_command(0x22); /* Load temperature and waveform setting */
    epd_send_data(0xB1);
    epd_send_command(0x20);

    epd_set_cursor(0, EPD_HEIGHT - 1);
    read_busy();

    epd_set_lut(WF_FULL_1IN54);

    ESP_LOGI(TAG, "EPD init complete");
}

void epd_clear(void) {
    memset(s_buffer, 0xff, EPD_BUFFER_SIZE);
}

void epd_draw_pixel(uint16_t x, uint16_t y, epd_color_t color) {
    if (x >= EPD_WIDTH || y >= EPD_HEIGHT) {
        return;
    }
    uint16_t index = y * (EPD_WIDTH / 8) + (x >> 3);
    uint8_t bit = 7 - (x & 0x07);
    if (color == EPD_COLOR_WHITE) {
        s_buffer[index] |= (uint8_t)(0x01 << bit);
    } else {
        s_buffer[index] &= (uint8_t)~(0x01 << bit);
    }
}

void epd_display(void) {
    epd_send_command(0x24);
    write_bytes(s_buffer, EPD_BUFFER_SIZE);
    epd_turn_on_display();
}

const uint8_t *epd_get_framebuffer(void) {
    return s_buffer;
}

void epd_set_framebuffer(const uint8_t *buf, size_t len) {
    if (len != EPD_BUFFER_SIZE) {
        return;
    }
    memcpy(s_buffer, buf, len);
}

void epd_lock(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void epd_unlock(void) {
    xSemaphoreGive(s_mutex);
}
