<div align="center">

# Accretion Phone · NyxOS

**A do-it-yourself smartphone with an operating system written from scratch, running on a single ESP32-S3.**

[![Build Firmware](https://github.com/silentproject2025/ren_phone/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/silentproject2025/ren_phone/actions/workflows/build-firmware.yml)
[![Build CAM](https://github.com/silentproject2025/ren_phone/actions/workflows/build-cam-coprocessor.yml/badge.svg)](https://github.com/silentproject2025/ren_phone/actions/workflows/build-cam-coprocessor.yml)
![Version](https://img.shields.io/badge/version-1.0.2-green)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3-red)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue)](./LICENSE)

</div>

NyxOS is neither Android nor Linux. The launcher, notification center, network stack, emulators, media players and even the graphics engine are custom code running on top of FreeRTOS.

| | |
|---|---|
| **Microcontroller** | ESP32-S3, 16 MB flash, 8 MB OPI PSRAM |
| **Built-in apps** | 46 |
| **Code size** | ~62,000 lines of main firmware + ~1,500 lines of coprocessor firmware (third-party code and generated data tables excluded) |
| **UI language** | English (default) and Indonesian, switchable in *Settings → Language* |
| **Version** | 1.0.2 (see [`VERSION`](./VERSION)) |
| **License** | AGPL-3.0 |

> **A note on testing status.** Most features were tested by the maintainer on physical hardware. Please be aware of these exceptions up front:
>
> - **Phone calls** are only partially verified: they work with a `sip2sip.info` account (SIP over TCP); other SIP providers, LAN mode and TURN are unconfirmed.
> - **NyxPPU** is an experiment (phase 1) and **SPI overclock** depends on cable quality.
> - **New system gestures** (pull-to-refresh, edge swipe-back, panels that follow the finger) are `BETA`.
> - **Nyx Apps** (native ELF apps) is `EXP`: the launcher and its storage choice (SD card, or internal flash when no SD card is inserted) are not yet tested on a device, and the default build does not include the ELF loader, so apps cannot run until it is added.
> - **Device registry** (anonymous, one-time registration with a public dashboard) is `BETA`; see the [security notice](#security-notice).
> - **Backup & Restore**: the **Cloud** tab (back up and restore) is tested on a device; the **SD Card** tab is `BETA` (tested on a PC, not yet on a device). **Chess** (online and vs. AI) is `BETA`: the logic is tested on a PC but not yet on a device.
>
> Each feature is described in full in the [feature documentation](./docs/features/).

## Table of contents

- [Feature overview](#feature-overview)
- [Architecture at a glance](#architecture-at-a-glance)
- [Hardware](#hardware)
- [Quick start](#quick-start)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Known limitations](#known-limitations)
- [Security notice](#security-notice)
- [Contributing](#contributing)
- [Support the project](#support-the-project)
- [License and acknowledgements](#license-and-acknowledgements)

## Feature overview

Status markers used throughout the documentation:

| Marker | Meaning |
|---|---|
| `OK` | Tested by the maintainer on a physical device and working well. |
| `PARTIAL` | Tested in specific scenarios; other scenarios are unconfirmed. |
| `BETA` | Code complete but **not yet tested on a device**. Bugs may remain. |
| `HW` | The code is correct; the end result depends on hardware quality. |
| `EXP` | Experiment or early scaffolding. |

> "Tested" means tested by the maintainer on **a single unit**. Other units with different cables or components may behave differently.

| Area | Feature | Status |
|---|---|---|
| **System UI** | Home, Lock Screen, Control Center, themes, squircle icons, bento widgets, app open/close animations — [details](./docs/features/system-ui.md) | `OK` |
| | Charging animation on the lock screen | `OK` |
| | Landscape Home with swipeable pages (widgets first, then apps) and ST7796 hardware scroll (portrait grid scrolling, landscape page swipes) — [details](./docs/features/system-ui.md#home-pages-and-hardware-scroll-st7796) | `OK` (ST7796 builds) |
| | New gestures (pull-to-refresh, edge swipe-back, finger-following panels) | `BETA` |
| | Grouped notifications, swipe-to-dismiss, quick actions, App Switcher previews | `BETA` |
| | Color emoji (built-in set and full set from SD) | `BETA` |
| | Keyboard dictionary (word suggestions + autocorrect) | `BETA` |
| **Communication** | [Chat over an MQTT broker](./docs/features/chat.md) | `OK` |
| | [Phone calls (SIP and LAN)](./docs/features/phone-calls.md) | `PARTIAL` |
| | [Chess (online and vs. AI)](./docs/features/chess.md) | `BETA` |
| **Remote** | [Remote Control (web) and web gamepad](./docs/features/remote-control.md) | `OK` |
| **Media** | [Bluetooth music, AVI/MJPEG player, Gallery](./docs/features/media.md) | `OK` |
| | [Camera (OV2640 on the ESP32-CAM)](./docs/features/camera.md) | `OK` |
| | [Books (`.txt`, `.md`, `.epub`) and Project Gutenberg downloads](./docs/features/books.md) | `OK` |
| **Voice and AI** | [Wake word "Hei Nyx", Gemini AI Chat, voice memo](./docs/features/voice-and-ai.md) | `OK` (voice commands `BETA`) |
| **System** | [Battery (INA219), per-channel OTA with release notes, HWmonitor, crash log](./docs/features/system-tools.md) | `OK` (per-app energy and crash log `BETA`) |
| | [Settings Backup & Restore](./docs/features/system-tools.md#settings-backup-and-restore-beta) | `OK` (Cloud) / `BETA` (SD Card) |
| | [Device registry](./docs/security.md) (anonymous id, optional public username, public dashboard) | `BETA` |
| | [Nyx Apps](./docs/features/native-apps.md): native ELF apps loaded from the SD card, or from internal flash when no SD card is inserted | `EXP` |
| **Games** | [Inferno, NES, DOOM, Maze, classics, PPU demo](./docs/features/games.md) | `OK` |
| **Experimental** | [SPI overclock (`HW`), NyxPPU pixel engine (`EXP`)](./docs/features/experimental.md) | — |
| **Platform** | mbedTLS in PSRAM (custom core libraries), applied automatically by CI | `OK` |

## Architecture at a glance

The device is built from two microcontrollers connected over UART. The ESP32-S3 runs the UI, apps and networking. The ESP32-CAM acts as a **coprocessor** that handles Bluetooth audio (A2DP) and the camera.

```
┌────────────────────────────┐   UART 921,600 baud    ┌──────────────────────────┐
│ ESP32-S3  (main)           │  (raised to 2-5 Mbaud  │ ESP32-CAM  (coprocessor) │
│  · UI, apps, games         │   while Camera is open)│  · MP3 decode (libhelix) │
│  · WiFi, HTTP/TLS, SIP     │ ◀────── RPLink ──────▶ │  · A2DP to TWS earbuds   │
│  · SD card, display, touch │                        │  · OV2640 camera         │
│  · INMP441 mic, sensors    │                        │                          │
└────────────────────────────┘                        └────────────┬─────────────┘
                                                                   │ Bluetooth A2DP
                                                                   ▼
                                                           Earbuds / TWS
```

Key design consequences:

- **The device has no speaker.** All audio (music, video, calls, voice memos) goes through the ESP32-CAM to Bluetooth earbuds.
- **The two firmwares must stay consistent.** `rplink.h` must be **identical** in `firmware/phone/` and `firmware/cam_bt_coprocessor/`.
- **Memory is the scarcest resource.** Only ~162 KB of internal RAM is available; see [Architecture](./docs/architecture.md).

## Hardware

| Part | Specification |
|---|---|
| Brain | ESP32-S3, 16 MB flash, 8 MB OPI PSRAM |
| Display | ILI9341 320×240 (SPI), or the 3.5″ ST7796 480×320 variant with FT6236 capacitive touch |
| Touch | XPT2046 (ILI9341) or FT6236 (ST7796) |
| Storage | microSD (SDIO 1-bit); internal FFat (about 9 MB) as fallback for wallpaper, DOOM WADs, crash log and Nyx Apps |
| Sensors | MPU6050 (tilt, shake, auto-rotation), INA219 (voltage, current, power) |
| Microphone | INMP441 (I2S) |
| Audio output | **Bluetooth (A2DP) only**, via the ESP32-CAM coprocessor |
| Camera | OV2640 on an ESP32-CAM module (OV3660, OV5640 and GC2145 auto-detected; GC2145 modules such as the RHYX M21-45 are limited to 800×600) |
| Extras | Vibration motor, Neopixel |
| Partition scheme | `app3M_fat9M_16MB` |

Pin maps, assembly notes and the ST7796 variant are in [docs/hardware.md](./docs/hardware.md).

## Quick start

### Option A — flash a prebuilt image

Every push triggers the **Build Firmware Binaries** workflow, which publishes a GitHub Release:

| Release asset | Purpose |
|---|---|
| `ren_phone_s3_merged.bin` | Complete ESP32-S3 image (bootloader, partitions, app) for a first flash. |
| `firmware.bin` | App-only image used by OTA updates. |
| `cam_merged.bin` | Complete ESP32-CAM image (from the **Build CAM BT Co-Processor** workflow). |

Flash through the browser-based web flasher published on GitHub Pages, or with `esptool`:

```bash
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 ren_phone_s3_merged.bin
```

On first boot, follow the on-screen **touch calibration** (resistive ILI9341 builds only; the capacitive ST7796 build has no calibration wizard).

### Option B — build from source

```bash
# Requirements: arduino-cli, esp32 board package 3.x (Espressif), and the libraries
# LovyanGFX, JPEGDEC, PubSubClient, WebSockets
arduino-cli compile --fqbn esp32:esp32:esp32s3 \
  --board-options "FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
  <assembled-sketch-dir>
```

The firmware is assembled from `firmware/phone/` and `third_party/` by the CI workflow. Step-by-step instructions (CI, manual build, the ESP32-CAM firmware, custom core libraries) are in [docs/building.md](./docs/building.md).

> To build in the cloud without a local toolchain, fork the repository, enable GitHub Actions and run **Build Firmware Binaries**. For OTA updates to download firmware without a login, the repository must be **public**.

## Repository layout

```text
.
├── .github/workflows/        CI: firmware build, CAM build, custom libs, web flasher
├── docs/                     Documentation (architecture, hardware, features, ...)
├── examples/
│   └── native/               Example Nyx Apps (hello, bounce, memhog, notes, leaky) and an ESP-IDF template
├── experimental/
│   └── nyxvm/                Early design of a sandboxed app VM (not wired into the build)
├── firmware/
│   ├── phone/                NyxOS sketch for the ESP32-S3 (phone.ino + one .ino per feature)
│   │   └── extras/           Sources the CI copies into the sketch's src/ folder
│   ├── cam_bt_coprocessor/   ESP32-CAM coprocessor sketch (Bluetooth audio + camera)
│   └── prebuilt/             Alternative prebuilt firmware (Bruce fork), separate from NyxOS
├── supabase/                 SQL, registration page and dashboard for the optional device registry
├── third_party/              Vendored third-party code (Nofrendo NES core, ESP-DOOM core)
├── tests/                    Host-side (PC) tests: backup, chess, ebook, gutenberg, camera, dictionary, nyxapp
├── tools/                    Generators and helpers (emoji, fonts, dictionaries, i18n)
│   ├── ci/                   Helper scripts used by the CI workflows
│   └── legacy-patches/       Archived one-shot patch scripts (see its README)
├── CHANGELOG.md              Release notes; the matching section is shown on the phone during an OTA update
├── CONTRIBUTING.md
├── LICENSE                   AGPL-3.0
└── VERSION                   Project version (read by CI)
```

The folder name `firmware/phone/` matches `phone.ino` so the sketch can also be opened directly in the Arduino IDE. See [firmware/README.md](./firmware/README.md) for how the sketch files are organised.

> The repository and some old comments and variables are named `ren_phone`, which was the project's original name.

## Documentation

| Topic | Document |
|---|---|
| System design: memory, tasks, rendering, the RPLink coprocessor protocol | [docs/architecture.md](./docs/architecture.md) |
| Components, pin mapping, assembly notes, ST7796 variant | [docs/hardware.md](./docs/hardware.md) |
| Install, build, CI, OTA channels, custom libraries | [docs/building.md](./docs/building.md) |
| Release notes shown on the phone during an update | [CHANGELOG.md](./CHANGELOG.md) |
| SD card layout and configuration files | [docs/configuration.md](./docs/configuration.md) |
| Feature guides | [docs/features/](./docs/features/) |
| UI language system (English / Indonesian) | [docs/localization.md](./docs/localization.md) |
| Security and privacy considerations | [docs/security.md](./docs/security.md) |
| Known limitations | [docs/limitations.md](./docs/limitations.md) |
| Troubleshooting | [docs/troubleshooting.md](./docs/troubleshooting.md) |
| Host-side tests | [tests/README.md](./tests/README.md) |

## Known limitations

A short list; the full list is in [docs/limitations.md](./docs/limitations.md).

- Internal RAM is tight. `HTTP -1` errors usually indicate memory fragmentation rather than a network problem.
- Audio works only through Bluetooth. Without the ESP32-CAM and earbuds, no app produces sound.
- Phone calls: partially tested; no ICE; TURN over UDP only; no PSTN numbers through free services.
- TLS certificates are not verified (see the security notice).
- Nyx Apps need an ELF loader that the default build does not include, and app files on internal flash share the roughly 9 MB FFat partition; see [Nyx Apps](./docs/features/native-apps.md).
- CI does not pin tool versions, so upstream updates can break a build without any change in this repository.

## Security notice

This project is a personal, experimental device. Before using it with sensitive data or networks, read [docs/security.md](./docs/security.md). In short:

- TLS certificates are **not verified** (`setInsecure()`); do not send sensitive information.
- SIP signalling and RTP audio are **not encrypted**.
- The Web File Manager has **no authentication**; enable it only on trusted networks.
- API keys are stored as plain text on the SD card.
- On the first WiFi connection the device registers itself once with an anonymous id (a hash of the chip MAC) and sends its firmware version, build, branch and display type. A public username is optional and shown on the dashboard only if you set one. Leave the username empty to stay off the public list; details are in [docs/security.md](./docs/security.md).

## Contributing

Test reports from real hardware are the most valuable contribution. See [CONTRIBUTING.md](./CONTRIBUTING.md) for what needs testing most, how to report a bug, and how to work on the code.

## Support the project

Accretion Phone is built and tested by one person on real hardware. If it is useful to you, a donation helps cover parts, prototype boards and test devices.

[![Support on Saweria](https://img.shields.io/badge/Support-Saweria-orange)](https://saweria.co/accretionlabs)

Donating is entirely optional: the firmware is, and will stay, free under AGPL-3.0. Starring the repository, sharing it and sending hardware test reports help just as much.

## License and acknowledgements

Licensed under [AGPL-3.0](./LICENSE). Third-party code follows its own license:

| Component | Notes |
|---|---|
| Nofrendo (`third_party/nofrendo_core/`) | NES emulator |
| ESP-DOOM / doomgeneric (`third_party/esp_doom_core/`) | DOOM port |
| LovyanGFX, JPEGDEC, PubSubClient, WebSockets | Arduino libraries installed at build time |
| audio-tools, arduino-libhelix, ESP32-A2DP | Libraries for the ESP32-CAM coprocessor firmware |
