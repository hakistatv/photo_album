# photo_album

ESP-IDF firmware for the Waveshare **ESP32-S3-ePaper-1.54** (V1: ESP32-S3FH4R2,
4MB Flash / 2MB PSRAM). Lets you upload up to 5 images into persistent slots
over its own Wi-Fi access point, and shows them on the onboard e-paper panel.

## Hardware

- Board: Waveshare ESP32-S3-ePaper-1.54, V1
- Docs: https://docs.waveshare.com/ESP32-S3-ePaper-1.54

| Function                            | Pin(s) |
|--------------------------------------|--------|
| E-paper (SPI2)                       | DC=GPIO10, CS=GPIO11, SCK=GPIO12, MOSI=GPIO13, RST=GPIO9, BUSY=GPIO8, PWR=GPIO6 (**active-LOW**) |
| BOOT button (cycles displayed slot)  | GPIO0 |

See [`components/epd_1in54/epd.h`](components/epd_1in54/epd.h) for the pin
map as implemented, and
[`docs/waveshare-esp32-s3-epaper-1.54-reference.md`](docs/waveshare-esp32-s3-epaper-1.54-reference.md)
for board variant identification and register-level notes.

## Features

- Boots, clears the e-paper display, and starts a Wi-Fi access point
  (SoftAP) -- connect directly to the board, no router needed
- Starts an HTTP server: a Home page at `/` linking to an Upload Image page
  at `/upload` and a Settings page at `/settings`
- Upload Image page: pick which of the 5 slots to save into, then upload a
  JPEG that's exactly 200x200 pixels (up to 64KB). The device decodes it,
  converts it to grayscale, Atkinson-dithers it down to 1-bit black/white,
  draws it on the panel, and persists it into that slot (`photo_store.c` --
  a dedicated flash partition, survives reboot/power loss). The page also
  shows a live thumbnail of whatever's currently saved in each slot, with a
  Clear button per slot to remove it (doesn't change whatever's currently on
  the panel, even if you clear the slot it came from)
- Pressing the **BOOT** button cycles the display through whichever slots
  currently have an image saved, skipping empty ones (`cycle_to_next_slot()`
  in `photo_album.c`)
- Settings page: change the Wi-Fi SSID, password, and device (mDNS) name
  from the browser -- no reflash needed. Saving restarts the board so the
  new settings take effect
- Advertises itself over mDNS so the AP can be reached by hostname instead
  of IP

## Configuring

There are two ways to set the Wi-Fi SSID/password and device (mDNS) name:

1. **Before you build** -- edit the defaults in
   [`components/device_settings/device_config.h`](components/device_settings/device_config.h):

   ```c
   #define WIFI_AP_SSID     "Hakista"
   #define WIFI_AP_PASS     "hak1sta!"
   #define WIFI_AP_CHANNEL  1
   #define WIFI_AP_MAX_CONN 4

   #define MDNS_HOSTNAME    "photo-album"
   ```

   Edit that file, then build/flash as usual -- no menuconfig step needed.

2. **After flashing, from the browser** -- open `/settings` on the device
   (see below) and change the SSID, password, and device name there. These
   are saved to NVS flash and take priority over `device_config.h` from then
   on (survives reflashing the app, but not `idf.py erase-flash`). Saving
   restarts the board immediately. Leave the password field blank to keep
   the current password unchanged.

   Note: settings saved this way are stored **unencrypted** in NVS -- fine
   for a local device you control, but don't expose this AP's `/settings`
   endpoint beyond that.

## Quick Flash (no build tools needed)

Flash the latest prebuilt firmware straight from your browser using
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) -- built and
published automatically by GitHub Actions on every push to `main`.

1. Open **https://hakistatv.github.io/photo_album/** in **Chrome or Edge on
   desktop** (Web Serial isn't supported in Firefox/Safari, or on mobile
   browsers).
2. Connect the board via USB **while holding the BOOT button down** -- hold
   BOOT, plug in the USB cable, then release BOOT after a second or two.
   This puts the ESP32-S3 into its serial bootloader (download) mode;
   without it, the board boots straight into whatever firmware is already
   on it instead of exposing itself for flashing, and the browser either
   won't see a usable port or the flash will fail partway through.
3. Click **Connect & Install**, select the board's serial port, then tick
   **Erase device** if this is the first time flashing this board with this
   firmware (or it previously ran different firmware -- clears out any
   leftover settings from before).
4. Wait for the flash to finish (roughly 30s-1min); the board reboots into
   the firmware automatically once it's done.

Then connect to the `Hakista` Wi-Fi network (default password
`hak1sta!`) and browse to `http://192.168.4.1/` -- see
[Connecting to the device](#connecting-to-the-device) below. Building from
source (next section) is only needed if you want to change the code.

## Building & flashing

This project uses ESP-IDF v6.0.2. Activate the toolchain, then use `idf.py`
as normal:

```sh
source ~/.espressif/tools/activate_idf_v6.0.2.sh
idf.py set-target esp32s3   # first time only
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

(To exit the serial monitor, press `Ctrl-]`.)

### Notes for this board

- Flash over UART, not JTAG (`idf.flashType` in `.vscode/settings.json` if
  using the VS Code ESP-IDF extension) -- this board's native USB-JTAG
  interface can leave the chip stuck in bootloader/download mode with the
  JTAG flash path.
- If a flash/monitor gets stuck at "waiting for download", check for (and
  kill) a leftover `openocd` process still holding the JTAG interface.
- `sdkconfig.defaults` sets `CONFIG_ESPTOOLPY_NO_STUB=y`, which this board
  needs for reliable flashing.

## Connecting to the device

1. Connect your phone/laptop's Wi-Fi to the SSID set in
   `components/device_settings/device_config.h`
   (default `Hakista`), using the configured password (default
   `hak1sta!`).
2. Browse to either:
   - `http://192.168.4.1/` (always works -- the SoftAP's fixed gateway IP), or
   - `http://<MDNS_HOSTNAME>.local/` (default `http://photo-album.local/` --
     works out of the box on macOS/iOS/Linux; Windows needs Bonjour
     installed)

You should see the Home page, with links to Upload Image and Settings. On
Upload Image, pick a slot, choose a 200x200 JPEG, and submit -- the panel
should redraw within a couple seconds and the slot's thumbnail should
appear in the grid, as a quick end-to-end check that Wi-Fi + web server +
JPEG decode + display + flash storage all agree with each other.

## Project layout

Each piece besides the app entry point lives in its own ESP-IDF component
under `components/`, with its own `CMakeLists.txt` declaring exactly what it
requires:

```
main/
  photo_album.c              -- app_main, BOOT-button slot cycling
  CMakeLists.txt
components/
  device_settings/            -- runtime SSID/password/hostname storage (NVS-backed)
    device_settings.c/.h
    device_config.h          -- edit Wi-Fi/mDNS defaults here before building
    CMakeLists.txt
  mdns_service/                -- mDNS advertisement (<hostname>.local)
    mdns_service.c/.h
    CMakeLists.txt
  wifi_ap/                      -- Wi-Fi access-point bring-up
    wifi_ap.c/.h
    CMakeLists.txt
  epd_1in54/                    -- SSD1681 e-paper panel driver (SPI + GPIO control)
    epd.c/.h
    CMakeLists.txt
  jpeg_display/                 -- JPEG decode -> grayscale -> Atkinson dither -> framebuffer
    jpeg_display.c/.h
    idf_component.yml          -- managed dependency (esp_new_jpeg)
    CMakeLists.txt
  photo_store/                  -- 5-slot persistent image storage on the "photos" partition
    photo_store.c/.h
    CMakeLists.txt
  boot_button/                  -- BOOT button (GPIO0) polling + debounce
    boot_button.c/.h
    CMakeLists.txt
  web_server/                   -- HTTP server: Home, Upload Image (GET+POST),
    web_server.c/.h                Settings (GET+POST), per-slot preview + Clear
    pages/                     -- every page's HTML lives here (embedded into
                                     the binary at build time, see CMakeLists.txt):
                                       home.html, upload.html, settings.html,
                                       restart.html, style.css
    CMakeLists.txt
docs/
  index.html                   -- ESP Web Tools browser flasher (served via GitHub Pages)
  firmware/                    -- prebuilt binaries + manifest.json for the installer above,
                                     refreshed automatically by the build-firmware workflow
  waveshare-esp32-s3-epaper-1.54-reference.md   -- hardware reference notes
.github/workflows/
  build-firmware.yml           -- rebuilds firmware and updates docs/firmware/ on every push
partitions.csv                 -- custom table: default single-app layout plus a
                                     "photos" data partition for the 5 image slots
sdkconfig.defaults             -- CONFIG_ESPTOOLPY_NO_STUB, custom partition table
```

**Partition table:** this project uses a custom `partitions.csv` to add the
`photos` data partition the slot storage lives on, on top of ESP-IDF's
default single-app layout. If `idf.py build` ever reports a stale/wrong
partition size after pulling changes, delete `sdkconfig` and rebuild so it
regenerates from `sdkconfig.defaults` (`sdkconfig` is a local cache, not
checked in).

## Attribution

This project is shared publicly for anyone to fork, learn from, and build
on. If you use this code -- in full or in a substantial part, source or
compiled firmware -- in your own project, please credit **Hakista TV**:

- [github.com/hakistatv](https://github.com/hakistatv)
- [youtube.com/HakistaTV](https://youtube.com/HakistaTV)

A link back to this repo in your README or project description is enough.
