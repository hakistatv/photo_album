# Waveshare ESP32-S3-ePaper-1.54 — Hardware Reference

Knowledge base for this project's target board, gathered from the official Waveshare
docs/GitHub. This is a **reference only** — no project code was changed to produce it.

Sources:
- https://docs.waveshare.com/ESP32-S3-ePaper-1.54
- https://docs.waveshare.com/ESP32-S3-ePaper-1.54/Development-Environment-Setup-ESP-IDF
- https://github.com/waveshareteam/ESP32-S3-ePaper-1.54 (official example/driver code)

## Board identity

Our attached board was identified via `esptool chip_id` as **ESP32-S3 (QFN56), 4MB
embedded flash, 2MB embedded PSRAM** — this matches the **V1** variant
(`ESP32-S3FH4R2`). The V2 variant (`ESP32-S3-PICO-1-N8R8`, 8MB flash/8MB PSRAM) uses
different, **incompatible** example code — always check flash/PSRAM size before
reusing example code from the repo's `V2/` folder.

- MCU: ESP32-S3FH4R2, dual-core Xtensa LX7 @ 240MHz, 512KB SRAM, 384KB ROM
- Flash: 4MB embedded
- PSRAM: 2MB embedded
- Wi-Fi 802.11 b/g/n (2.4GHz) + Bluetooth 5 LE, internal antenna
- Battery: MX1.25 2-pin header, onboard Li-ion recharge management circuit

## Onboard peripherals

- 1.54" e-paper display, 200×200 resolution (controller IC not documented publicly;
  driven via custom SPI command sequence in `port_display.cpp`, not a named
  off-the-shelf driver component)
- ES8311 audio codec
- PCF85063 RTC (I2C)
- SHTC3 temperature/humidity sensor (I2C)
- FT6336 capacitive touch controller (only on the Touch variant / some SKUs)
- microSD card slot
- BOOT button + PWR (power) button
- Status LED
- 2× 6-pin 2.54mm expansion headers

## Pin map (V1), from `port_bsp/epaper_config.h`

| Function | GPIO |
|---|---|
| EPD SCK | 12 |
| EPD MOSI | 13 |
| EPD CS | 11 |
| EPD DC | 10 |
| EPD RST | 9 |
| EPD BUSY | 8 |
| EPD PWR (panel power enable) | 6 |
| Touch RST | 7 |
| Touch INT | 21 |
| I2C SDA | 47 |
| I2C SCL | 48 |
| RTC (PCF85063) addr | 0x51 |
| SHTC3 addr | 0x70 |
| FT6336 touch addr | 0x38 |
| BOOT button | 0 |
| PWR button | 18 |
| LED | 3 |
| VBAT sense | 17 |
| Audio power enable | 42 |
| SD MISO/D0 | 40 |
| SD MOSI/CMD | 41 |
| SD CLK | 39 |
| SD mount point | `/sdcard` |

EPD SPI bus: `SPI2_HOST`. I2C bus: `I2C_NUM_0`.

## Official example structure (relevant to ESP-IDF)

Repo layout: `01_Arduino_Libraries/`, `02_Example/{Arduino,ESP-IDF,XiaoZhi}/`,
`03_Firmware/`.

`02_Example/ESP-IDF/V1/` contains numbered examples:
`01_ADC_Test`, `02_I2C_PCF85063`, `03_I2C_SHTC3`, `04_SD_Card`, `05_WIFI_AP`,
`06_WIFI_STA`, `07_BATT_PWR_Test`, `08_Audio_Test`, `09_LVGL_V8_Test`,
`10_LVGL_V9_Test`, `11_FactoryProgram` (full out-of-box demo), `12_RTC_Sleep_Test`.

`11_FactoryProgram/components/port_bsp/` is the board-support layer — this is where
`epaper_config.h` (pin map above) and `port_display.{h,cpp}` (EPD init/clear/draw/
partial-refresh functions: `EPD_Init`, `EPD_Clear`, `EPD_Display`,
`EPD_Init_Partial`, `EPD_DisplayPart`, `EPD_DrawColorPixel`) live, alongside
`port_adc`, `port_codec`, `port_ft6336`, `port_i2c`, `port_lvgl`, `port_power`,
`port_sdcard`, `port_shtc3`.

`port_bsp/idf_component.yml` managed dependencies:
```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  lvgl/lvgl: ^9.3.0
  espressif/button: "*"
  pedrominatel/shtc3: "^1.4.1"
  waveshare/pcf85063a: "^1.1.1"
```

## Build requirements

- **ESP-IDF ≥ v5.5.0** required per Waveshare docs (we're on v6.0.2 — satisfies this).
- No e-paper-specific Kconfig options are documented beyond the pin map above; the
  EPD is driven directly via SPI2_HOST with manual GPIO control for DC/RST/BUSY/CS,
  not through a generic `esp_lcd` panel driver.

## Open questions (not resolved by the docs pulled so far)

- Exact e-paper controller IC (e.g. SSD16xx/UC81xx family) and its full init command
  sequence — would need to read `port_display.cpp` directly (not yet pulled).
- Whether the Touch (FT6336) pins/parts apply to this exact SKU or only the
  "Touch" variant of the board.
