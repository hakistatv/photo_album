# Photo Album

ESP-IDF firmware for a [Waveshare ESP32-S3-ePaper-1.54 (V1)](https://docs.waveshare.com/ESP32-S3-ePaper-1.54)
board. It stands up its own Wi-Fi access point and a small web UI where you
can upload up to 5 images into persistent slots; the device decodes each
JPEG, dithers it to black/white, and displays it on the 1.54" e-paper panel.
The physical BOOT button cycles through whatever slots are occupied.

## Install the firmware

No ESP-IDF install needed -- flash a prebuilt build straight from your
browser (Chrome or Edge, desktop only) via
**[the web installer](https://hakistatv.github.io/photo_album/)**.
It's built via [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
and rebuilt automatically from `main` on every push (see
[`.github/workflows/build-firmware.yml`](.github/workflows/build-firmware.yml)),
so it always installs the latest firmware. Plug the board in, click
**Connect & Install**, and pick its serial port.

## Hardware

| | |
|---|---|
| Board | Waveshare ESP32-S3-ePaper-1.54 (V1) |
| MCU | ESP32-S3 |
| Display | 1.54", 200x200, monochrome, SSD1681-family controller, SPI |

Pin map (see [`components/epd_1in54/epd.h`](components/epd_1in54/epd.h)):

| Signal | GPIO |
|---|---|
| DC | 10 |
| CS | 11 |
| SCK | 12 |
| MOSI | 13 |
| RST | 9 |
| BUSY | 8 |
| Panel power enable (active-low) | 6 |
| BOOT button (cycles displayed slot) | 0 |

More hardware notes (board variant identification, register-level details,
links) are in [`docs/waveshare-esp32-s3-epaper-1.54-reference.md`](docs/waveshare-esp32-s3-epaper-1.54-reference.md).

## How it works

1. On boot, the device clears the display and starts a Wi-Fi access point.
2. Connect a phone or laptop to that AP and browse to the device.
3. From the **Home** page, go to **Upload Image**, pick which of the 5
   slots to save into, and upload a JPEG that's exactly 200x200 pixels.
4. The device decodes the JPEG, converts it to grayscale, Atkinson-dithers
   it down to 1-bit black/white, draws it on the panel, and persists it into
   that slot (survives reboot/power loss).
5. Press the **BOOT** button on the board to cycle the display through
   whichever slots currently have an image saved.

The Upload page shows a live thumbnail of what's saved in each slot, and a
**Clear** button per slot to remove its saved image (this doesn't change
whatever's currently on the panel, even if you clear the slot it came from).

### Pages

| Page | Path | Purpose |
|---|---|---|
| Home | `/` | Links to Upload Image and Settings |
| Upload Image | `/upload` | Slot previews (with Clear buttons) + upload form |
| Settings | `/settings` | Wi-Fi SSID/password and mDNS hostname; saving restarts the device |

### Connecting

- Wi-Fi network: `photo-album` (see `WIFI_AP_SSID`/`WIFI_AP_PASS` in
  [`components/device_settings/device_config.h`](components/device_settings/device_config.h) --
  these are just the first-boot defaults; change them any time from the
  **Settings** page)
- Password: `qrcode123`
- Browse to `http://192.168.4.1/` or `http://photo-album.local/` (mDNS --
  needs Bonjour/avahi support on the client; built into macOS/iOS/Linux,
  Windows needs Bonjour installed)

### Image requirements

- JPEG format
- Exactly 200x200 pixels (the upload is rejected otherwise -- the device
  does not crop or resize)
- Up to 64 KB

## Project layout

```
main/
  photo_album.c        Orchestrates startup: NVS, display init, Wi-Fi AP, mDNS, web server, BOOT button
components/
  epd_1in54/             E-paper panel driver (SPI + GPIO control, SSD1681 command set)
  jpeg_display/          JPEG decode -> grayscale -> Atkinson dither -> framebuffer
  photo_store/            5-slot persistent image storage on a dedicated flash partition
  boot_button/            BOOT button (GPIO0) polling + debounce, cycles the displayed slot
  web_server/             HTTP server: Home / Upload Image / Settings pages
  wifi_ap/                Wi-Fi access-point bring-up
  mdns_service/           mDNS advertisement (<hostname>.local)
  device_settings/        Compile-time defaults + NVS-backed runtime Wi-Fi/hostname settings
docs/
  index.html                                     ESP Web Tools browser flasher (GitHub Pages)
  firmware/                                       Prebuilt binaries + manifest.json for the installer above
  waveshare-esp32-s3-epaper-1.54-reference.md    Hardware reference notes
.github/workflows/
  build-firmware.yml    Rebuilds firmware and updates docs/firmware/ on every push to main
```

## Building and flashing locally

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html)
v6.0.2, targeting `esp32s3`.

```
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

(To exit the serial monitor, press `Ctrl-]`.)

### Configuration

`WIFI_AP_SSID`/`WIFI_AP_PASS`/`MDNS_HOSTNAME` in
[`components/device_settings/device_config.h`](components/device_settings/device_config.h)
are just the first-boot defaults -- once running, change them from the
**Settings** page instead (persisted in NVS, survives reflashing the app
unless you erase the whole flash).

### Notes for this board

- Flash over UART, not JTAG (`idf.flashType` in `.vscode/settings.json` if
  using the VS Code ESP-IDF extension) -- this board's native USB-JTAG
  interface can leave the chip stuck in bootloader/download mode with the
  JTAG flash path.
- If a flash/monitor gets stuck at "waiting for download", check for (and
  kill) a leftover `openocd` process still holding the JTAG interface.
- `sdkconfig.defaults` sets `CONFIG_ESPTOOLPY_NO_STUB=y`, which this board
  needs for reliable flashing.
- Uses a custom partition table ([`partitions.csv`](partitions.csv)) to add
  the `photos` data partition the slot storage lives on -- `idf.py
  set-target` doesn't need re-running just to pick this up, it's read on
  every build.
