# Hardware

- [Components](#components)
- [ESP32-S3 pin mapping](#esp32-s3-pin-mapping)
- [ESP32-CAM assembly notes](#esp32-cam-assembly-notes)
- [3.5″ ST7796 display variant](#35-st7796-display-variant)
- [Aether Board variant](#aether-board-variant)

## Components

**Board names.** The ILI9341 build (320×240, XPT2046 touch) is **Accretion v1** and the 3.5″ ST7796 build (480×320, FT6236 touch) is **Accretion v2**. The Aether Board keeps its own name. The name is chosen by the build profile in `firmware/phone/display_conf.h` (`RP_BOARD_NAME`) and shown in *About Phone → Papan*. The technical ids (`ili9341`, `st7796`, the `-st7796` OTA channel suffix, file names) are unchanged.

| Part | Specification |
|---|---|
| Brain | ESP32-S3, 16 MB flash, 8 MB OPI PSRAM |
| Display | ILI9341 320×240 (SPI2), portrait and landscape. 3.5″ variant: ST7796 480×320 + FT6236 touch (see [below](#35-st7796-display-variant)). Aether Board: ST7796 480×320 + FT6336U touch (see [below](#aether-board-variant)) |
| Touch | XPT2046 on a separate SPI3 bus (ILI9341). ST7796: FT6236 over I2C. Aether Board: FT6336U over I2C |
| Storage | microSD (SDIO 1-bit; over SPI on the Aether Board), with internal FFat as a fallback |
| Sensors | MPU6050 (tilt, shake, auto-rotation) and INA219 (voltage, current, power) |
| Microphone | INMP441 (I2S) |
| Audio output | **Bluetooth (A2DP)** via the ESP32-CAM coprocessor. Optional: a **MAX98357A** I2S amp + speaker for the HWmonitor "Test Speaker" tone and the Music app's *Output: SPK* mode |
| Camera | OV2640 on an ESP32-CAM module (OV3660/OV5640 are detected automatically) |
| Extras | Vibration motor and Neopixel |
| Partition scheme | `app3M_fat9M_16MB` |

## ESP32-S3 pin mapping

| Block | Signal → GPIO |
|---|---|
| Display | SCLK 12 · MOSI 11 · MISO 13 · DC 2 · CS 10 · RST 14 · BL 21 |
| Touch | ILI9341 (XPT2046, SPI): SCLK 6 · MOSI 5 · MISO 4 · CS 9 · ST7796 (FT6236, I2C): SDA 6 · SCL 7 (`Wire`) |
| SD (SDIO 1-bit) | CLK 39 · CMD 38 · D0 40 |
| Microphone | SCK 47 · WS 46 · SD 45 |
| Link to CAM | S3 TX 41 → CAM RX 13 · S3 RX 42 ← CAM TX 14 · common GND |
| MPU6050 and INA219 | ILI9341: SDA 15 · SCL 7 (`Wire`) · ST7796: SDA 5 · SCL 4 (`Wire1`, its own bus) |
| Speaker (optional, MAX98357A) | BCLK 16 · LRC 17 · DIN 8 (ILI9341) or 15 (ST7796 / Aether). Override with `-DAUDIO_I2S_BCLK / AUDIO_I2S_LRC / AUDIO_I2S_DOUT`. Power VIN from 5V |
| Other | Vibration motor 18 · Neopixel 48 |

The display and sensor pin definitions live in [`firmware/phone/display_conf.h`](../firmware/phone/display_conf.h) and [`firmware/phone/hw_conf.h`](../firmware/phone/hw_conf.h).

## ESP32-CAM assembly notes

- Power the module through the **5V** pin with a sufficiently strong supply. The module easily suffers a *brownout* when the camera and the Bluetooth radio are active at the same time.
- **Remove the SD card from the ESP32-CAM module.** GPIO 13 and 14 are shared with that slot, and GPIO 4 is used as the flash LED.
- The camera pin map follows the **AI-Thinker** board. Clone boards with a different pin map require changing the `CAM_PIN_*` constants in `firmware/cam_bt_coprocessor/cam_module.h`.
- Long or low-quality UART cables can make the 4–5 Mbaud link unstable. The firmware automatically steps down one speed rung (5M → 4M → 3M → 2M → 921,600 baud) until the link is clean. Use the shortest cable possible.

## 3.5″ ST7796 display variant

This build is **separate** from the regular ILI9341 build. The code is the same; the variant is selected with the `-DRP_DISPLAY_ST7796` flag (all settings live in `display_conf.h`). The **Build Firmware Binaries** workflow builds every display variant in its own job (the Aether Board has [its own section](#aether-board-variant) below). ILI9341 and ST7796 compared:

| | ILI9341 (original) | ST7796 (new) |
|---|---|---|
| Release per build | `build-N` | `st7796-N` (always a prerelease) |
| Files | `firmware.bin`, `ren_phone_s3_merged.bin` | `firmware-st7796.bin`, `ren_phone_s3_st7796_merged.bin` |
| OTA channel | `channel-<branch>` | `channel-<branch>-st7796` |
| `info.txt` | `display=ili9341` | `display=st7796` |

The phone refuses an OTA update when the `display=` value in `info.txt` does not match its display.

**Installing the other display's firmware over OTA (for example an ILI9341 phone → ST7796 firmware).** In the Update app, the WiFi row has a small `9341` / `7796` pill to the left of the channel pill. Tap it twice to activate it (it turns red): *Check for updates* then reads the channel of the opposite display (`channel-<branch>-st7796` or the unsuffixed one) and matches `display=` against that target display instead of the running build's display. The fetched build may be installed even if its number is not newer. The option lives only in RAM, so it is lost on reboot. Its purpose is a panel swap: install the new display's firmware while the old panel is still attached, then swap the panel. If you install it without swapping the panel, the screen will be dark or garbled and the XPT2046 vs. FT6236 touch controllers will not match; recover through the SD `/update` folder or USB.

**Default wiring.** The display SPI pins are the same as the ILI9341 (SCLK 12, MOSI 11, MISO 13, DC 2, CS 10, RST 14, BL 21). The FT6236 uses its **own** I2C bus (`Wire`: SDA 6, SCL 7, address 0x38; SDA was previously GPIO 15); RST/INT are not used. Change them with `RP_TOUCH_SDA / RP_TOUCH_SCL / RP_TOUCH_RST` in `display_conf.h`.

**Separate sensor bus.** In the ST7796 build, the MPU6050 and INA219 move to a second I2C bus (`Wire1`): **SDA 5, SCL 4** (the former XPT2046 touch SPI pins, which are unused in this build). 4.7k–10k pull-ups to 3V3 are needed (most modules already have them). Change them with `RP_SENS_SDA / RP_SENS_SCL / RP_SENS_I2C_HZ`; avoid GPIO 33–37 (octal PSRAM), 26–32 (flash), 19/20 (USB) and 0/3/45/46. The ILI9341 build is unchanged (MPU6050 and INA219 stay on SDA 15 / SCL 7).

**If something is off after flashing:**

| Symptom | Fix |
|---|---|
| Negative (inverted) colors | `RP_ST7796_INVERT 1` |
| Red and blue swapped | `RP_ST7796_RGB_ORDER 1` |
| Touch is mirrored or rotated | `RP_TOUCH_SWAP_XY` / `RP_TOUCH_FLIP_X` / `RP_TOUCH_FLIP_Y` |

Capacitive touch does not use the calibration wizard.

**Multi-touch (2 fingers).** The FT6236 reports two fingers. The second finger is used by games (DOOM, NES, Inferno) so the joystick and an action button or look direction can be used at the same time. Enabled by default; disable with `-DRP_MULTITOUCH=0`.

## Aether Board variant

The Aether Board is an ESP32-S3 board with an SPI display, an FT6336U capacitive touch controller (I2C address 0x38, same registers as the FT6236) and a microSD slot that **shares the display's SPI bus** (same SCLK, MOSI and MISO; separate chip select). The code treats it as a 480×320 ST7796 panel, so everything in the [ST7796 section](#35-st7796-display-variant) applies as well: capacitive touch without the calibration wizard, multi-touch, hardware scroll and row-diff push.

The variant is selected with `-DRP_BOARD_AETHER`. Its profile is at the top of `display_conf.h`, and every value can be overridden with `-D...` without editing the file. It is marked `BETA`.

| | Aether Board |
|---|---|
| Release per build | `aether-N` (always a prerelease) |
| Files | `firmware-aether.bin`, `ren_phone_s3_aether_merged.bin` |
| OTA channel | `channel-<branch>-aether` |
| `info.txt` | `display=aether` |

| Block | Signal → GPIO |
|---|---|
| Display (SPI) | SCLK 12 · MOSI 11 · MISO 13 · CS 10 · DC 7 · RST 4 · BL 3 |
| microSD (SPI, shared with the display bus) | CS 14, 20 MHz; `SD_MMC` calls are redirected to the Arduino `SD` library. Because the bus is shared, DOOM releases its display transaction around every SD read, and NES ROMs are read through an internal-RAM buffer (a direct SD-to-PSRAM read returns zeros) |
| Touch FT6336U (I2C) | SDA 8 · SCL 9 · RST 5 · INT 6 (not used; touch is polled) |
| MPU6050 and INA219 (`Wire1`) | SDA 1 · SCL 2 (the ST7796 defaults 5/4 clash with the touch and display reset pins) |

The display colors are inverted by default on this board (`RP_ST7796_INVERT 1`). The other options in the ST7796 troubleshooting table (`RP_ST7796_RGB_ORDER`, `RP_TOUCH_SWAP_XY`, `RP_TOUCH_FLIP_X`, `RP_TOUCH_FLIP_Y`) work here too.
