<div align="center">

# Accretion Phone · NyxOS

**A do-it-yourself smartphone with an operating system written from scratch, running on a single ESP32-S3.**

[![Build Firmware](https://github.com/silentproject2025/ren_phone/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/silentproject2025/ren_phone/actions/workflows/build-firmware.yml)
[![Build CAM](https://github.com/silentproject2025/ren_phone/actions/workflows/build-cam-coprocessor.yml/badge.svg)](https://github.com/silentproject2025/ren_phone/actions/workflows/build-cam-coprocessor.yml)
[![Build Nyx Apps](https://github.com/silentproject2025/ren_phone/actions/workflows/build-nyxapps.yml/badge.svg)](https://github.com/silentproject2025/ren_phone/actions/workflows/build-nyxapps.yml)
[![Nyx Apps tests](https://github.com/silentproject2025/ren_phone/actions/workflows/nyxapp-tests.yml/badge.svg)](https://github.com/silentproject2025/ren_phone/actions/workflows/nyxapp-tests.yml)
![Version](https://img.shields.io/badge/version-1.0.3-green)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3-red)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue)](./LICENSE)

</div>

NyxOS is neither Android nor Linux. The launcher, notification center, network stack, emulators, media players and even the graphics engine are custom code running on top of FreeRTOS.

| | |
|---|---|
| **Microcontroller** | ESP32-S3, 16 MB flash, 8 MB OPI PSRAM |
| **Built-in apps** | 47 |
| **Code size** | ~67,000 lines of main firmware + ~1,500 lines of coprocessor firmware (third-party code and generated data tables excluded) |
| **UI language** | English (default) and Indonesian, switchable in *Settings → Language* |
| **Version** | 1.0.3 (see [`VERSION`](./VERSION)) |
| **License** | AGPL-3.0 |

> **A note on testing status.** Most features were tested by the maintainer on physical hardware. Please be aware of these exceptions up front:
>
> - **Phone calls** are only partially verified: they work with a `sip2sip.info` account (SIP over TCP); other SIP providers, LAN mode and TURN are unconfirmed.
> - **NyxPPU** is an experiment (phase 1) and **SPI overclock** depends on cable quality.
> - **New system gestures** (pull-to-refresh, edge swipe-back, panels that follow the finger) are `BETA`.
> - **Nyx Apps** (native ELF apps) is `PARTIAL`. The ELF loader and the example app `hello` are **confirmed running on a real phone** (it shows text, a frame counter and reacts to touch). Not yet confirmed on a device: the **Nyx Store**, the SD-card vs. internal-flash storage choice, the other example apps (`bounce`, `memhog`, `notes`, `leaky`), the watchdog, and file access and HTTP from inside an app. The memory manager and the host API are also tested on a PC, and CI compiles the example apps to real ELF files and offers them on the web flasher. The loader is only present when the custom core libraries are built with the `elf_loader` option (on by default in that workflow). The normal automatic build does **not** create those libraries; see [what the automatic build does and does not do](#what-the-automatic-build-does-and-does-not-do).
> - **Security hardening** (Web File Manager login, encrypted API keys, random Remote Control password, optional signed OTA updates) is `BETA`: its core logic is covered by PC tests (`tests/security/`) and the firmware glue is not yet confirmed on a device. Signed OTA stays off until you add a public key; see the [security notice](#security-notice).
> - **SysMem** (the central memory manager for the main firmware) is `BETA`: its core logic is tested on a PC, its firmware glue is only syntax-checked.
> - **Device registry** (anonymous, one-time registration with a public dashboard) is `BETA`; see the [security notice](#security-notice).
> - **Backup & Restore**: the **Cloud** tab (back up and restore) is tested on a device; the **SD Card** tab is `BETA` (tested on a PC, not yet on a device). **Chess** (online and vs. AI) is `BETA`: the logic is tested on a PC but not yet on a device.
> - **ST7796 display tricks beyond Home** are `BETA`: hardware scroll on other portrait screens (Settings, Files, Chat, Books and more) and row-diff screen push (`RP_PUSH_DIFF`, on by default in ST7796 and Aether builds). If the screen shows leftovers or corruption, build with `push_diff=0`. The **Aether Board** profile is `BETA` too.
>
> - **Speaker output** (optional MAX98357A module: Music *Output: SPK*, call audio through the speaker, MP3 from internal flash) is `BETA`: not yet tested on hardware. Bluetooth stays the default output.
>
> - **Online music** (Jamendo search and streaming in the Music app, through the Bluetooth TWS earbuds or the optional speaker) is `BETA`: not yet compiled or tested on a device. It needs Wi-Fi and a Jamendo `client_id` in the build (`JAM_CLIENT_ID`, normally the GitHub Secret of the same name; forks without it get a placeholder and no results). The Audius source needs no key and is the default when the build has no Jamendo id.
>
> - **WiFi Sensing** (motion, presence and a rough breathing estimate from WiFi CSI) is `BETA`: the signal processing is tested with synthetic signals on a PC (`tests/wifisense/`) but not yet on a device. It needs CSI in the custom core libraries (input `wifi_csi`, on by default). The labels on its screen are still Indonesian only.
> - **Automatic broker login** (Chat and Chess fetch their MQTT host, username and password from Supabase, plus the **Refetch** button) is `BETA`: not yet tested on a device. The login is **not secret**; see the [security notice](#security-notice).
>
> Each feature is described in full in the [feature documentation](./docs/features/).

## Table of contents

- [Feature overview](#feature-overview)
- [Architecture at a glance](#architecture-at-a-glance)
- [Hardware](#hardware)
- [Quick start](#quick-start)
- [Repository layout](#repository-layout)
- [Continuous integration](#continuous-integration)
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
| | Redesigned File Explorer (glass rows, file tiles colored by type), Nyx Apps list and Nyx Store screens (squircle icons, detail cards) | `BETA` |
| | Landscape Home with swipeable pages (widgets first, then apps) and ST7796 hardware scroll (portrait Home grid, landscape page swipes) — [details](./docs/features/system-ui.md#home-pages-and-hardware-scroll-st7796) | `OK` (ST7796 builds) |
| | Hardware scroll on other portrait screens (Settings, Files, Notepad, AI Chat, Chat, Books, Backup, About, Gallery grid and more) — [details](./docs/features/system-ui.md#home-pages-and-hardware-scroll-st7796) | `BETA` (ST7796 builds) |
| | Row-diff screen push (`RP_PUSH_DIFF`): only the rows that changed are sent to the panel | `BETA` (ST7796 and Aether, on by default) |
| | New gestures (pull-to-refresh, edge swipe-back, finger-following panels) | `BETA` |
| | Grouped notifications, swipe-to-dismiss, quick actions, App Switcher previews | `BETA` |
| | [Developer options](./docs/features/system-tools.md#developer-options-beta): change pins without rebuilding (tried first, then confirmed), live ADC calculation or INA219 readings, found in *Settings > System*; unlock by tapping the *Firmware version* row in *About Phone* 7 times | `BETA` |
| | Color emoji (built-in set and full set from SD) | `BETA` |
| | Keyboard dictionary (word suggestions + autocorrect) | `BETA` |
| **Communication** | [Chat over an MQTT broker](./docs/features/chat.md) | `OK` |
| | [Phone calls (SIP and LAN)](./docs/features/phone-calls.md) | `PARTIAL` |
| | [Chess (online and vs. AI)](./docs/features/chess.md) | `BETA` |
| | [Automatic broker login](./docs/features/chat.md#automatic-broker-login-beta) for Chat and Chess: fetched once from Supabase, with a **Refetch** button instead of the old *Set* button | `BETA` |
| **Remote** | [Remote Control (web) and web gamepad](./docs/features/remote-control.md) | `OK` |
| **Media** | [Bluetooth music, AVI/MJPEG player, Gallery](./docs/features/media.md) | `OK` |
| | [Camera (OV2640 on the ESP32-CAM)](./docs/features/camera.md) | `OK` |
| | [Books (`.txt`, `.md`, `.epub`) and Project Gutenberg downloads](./docs/features/books.md) | `OK` |
| | Optional speaker output (MAX98357A): Music *Output: SPK*, call audio without earbuds, MP3 from internal flash when there is no SD card — [details](./docs/features/media.md) | `BETA` |
| | [Online music (Jamendo, Audius)](./docs/features/media.md#online-music-jamendo): search or browse by tag, then play through the TWS earbuds (*Output: BT*) or the speaker (*Output: SPK*) | `BETA` |
| **Voice and AI** | [Wake word "Hei Nyx", Gemini AI Chat, voice memo](./docs/features/voice-and-ai.md) | `OK` (voice commands `BETA`) |
| **System** | [Battery (INA219), per-channel OTA with release notes, HWmonitor, crash log](./docs/features/system-tools.md) | `OK` (per-app energy and crash log `BETA`) |
| | [Settings Backup & Restore](./docs/features/system-tools.md#settings-backup-and-restore-beta) | `OK` (Cloud) / `BETA` (SD Card) |
| | [Device registry](./docs/security.md) (anonymous id, optional public username, public dashboard) | `BETA` |
| | [Nyx Apps](./docs/features/native-apps.md): native ELF apps loaded from the SD card, or from internal flash when no SD card is inserted. Example apps are compiled by CI and downloadable from the web flasher, or installable on the phone itself through the **Nyx Store**. The loader and the `hello` example are confirmed on a device; the Store and the rest are not | `PARTIAL` (Store `BETA`) |
| | [Security hardening](./docs/security.md): Web File Manager login (salted PBKDF2 hash, escalating lockout, only active while File Explorer is open), encrypted Gemini/NASA API keys, random Remote Control WiFi password, optional signed OTA updates (ECDSA P-256) | `BETA` |
| | [SysMem](./docs/sysmem.md): per-feature memory accounting, budgets, reserve floor and one reclaim path for the main firmware | `BETA` |
| | [WiFi Sensing](./docs/features/system-tools.md#wifi-sensing-beta): detects motion and presence and estimates breathing from WiFi CSI (router as transmitter, no camera); multi-subcarrier mode, per-router calibration | `BETA` |
| **Games** | [Inferno, NES, DOOM, Maze, classics, PPU demo](./docs/features/games.md) | `OK` |
| **Experimental** | [SPI overclock (`HW`), NyxPPU pixel engine (`EXP`)](./docs/features/experimental.md) | — |
| **Platform** | Custom core libraries (mbedTLS and WiFi/lwIP buffers in PSRAM, larger CPU cache, optional ELF loader, optional WiFi CSI for WiFi Sensing). Built **manually** once with *Build Arduino Libs*; every CI firmware build then installs them automatically when they exist — [details](#what-the-automatic-build-does-and-does-not-do) | `OK` |
| | Web flasher on GitHub Pages: stable and test firmware, SHA-256 checksums, and a **Nyx Apps** tab with the example apps | `OK` (firmware) / `PARTIAL` (Nyx Apps tab) |

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

- **Audio goes to Bluetooth earbuds.** Music, video, calls and voice memos go through the ESP32-CAM. An optional MAX98357A speaker module adds a second path: the HWmonitor "Test Speaker" tone, the Music app (*Output: SPK*) and phone calls when no earbuds are connected. Online music (Jamendo) follows the same output switch: with *Output: BT* a download task fills a PSRAM ring that the music task sends to the CAM, with *Output: SPK* the S3 decodes the stream itself. AVI video sound still needs Bluetooth, and the speaker path is `BETA` (see [Hardware](./docs/hardware.md)).
- **The two firmwares must stay consistent.** `rplink.h` must be **identical** in `firmware/phone/` and `firmware/cam_bt_coprocessor/`.
- **Memory is the scarcest resource.** Only ~162 KB of internal RAM is available; see [Architecture](./docs/architecture.md).
- **Fragmentation is handled by design, not by compaction.** Memory is judged by the *largest contiguous block*, not by total free bytes. Each Nyx App gets one PSRAM arena (allocated once, freed once, so apps cannot fragment system PSRAM); inside it a best-fit allocator merges neighbouring free blocks on `free()`. Before an HTTPS handshake a **TLS gate** waits for, and reclaims memory until, a contiguous internal-RAM block is available. Blocks are never moved (native apps use real pointers), so a long-running app can still fragment its own arena. See [SysMem](./docs/sysmem.md) and [Nyx Apps](./docs/features/native-apps.md).

## Hardware

| Part | Specification |
|---|---|
| Brain | ESP32-S3, 16 MB flash, 8 MB OPI PSRAM |
| Display | ILI9341 320×240 (SPI), the 3.5″ ST7796 480×320 variant with FT6236 capacitive touch, or the Aether Board (ST7796 480×320 with FT6336U touch) |
| Touch | XPT2046 (ILI9341), FT6236 (ST7796) or FT6336U (Aether Board) |
| Storage | microSD (SDIO 1-bit; on the Aether Board over SPI, sharing the display bus); internal FFat (about 9 MB) as fallback for wallpaper, DOOM WADs, crash log and Nyx Apps |
| Sensors | MPU6050 (tilt, shake, auto-rotation), INA219 (voltage, current, power) |
| Microphone | INMP441 (I2S) |
| Audio output | **Bluetooth (A2DP)** via the ESP32-CAM coprocessor (default). Optional: a MAX98357A I2S amplifier and speaker for Music, calls and the speaker test |
| Camera | OV2640 on an ESP32-CAM module (OV3660, OV5640 and GC2145 auto-detected; GC2145 modules such as the RHYX M21-45 are limited to 800×600) |
| Extras | Vibration motor, Neopixel |
| Partition scheme | `app3M_fat9M_16MB` |

The ILI9341 build is named **Accretion v1** and the 3.5″ ST7796 build **Accretion v2**; *About Phone* shows the board it is running on. Pin maps, assembly notes and the ST7796 and Aether Board variants are in [docs/hardware.md](./docs/hardware.md).

## Quick start

### Option A — flash a prebuilt image

Every push triggers the **Build Firmware Binaries** workflow, which publishes a GitHub Release:

| Release asset | Purpose |
|---|---|
| `ren_phone_s3_merged.bin` | Complete ESP32-S3 image (bootloader, partitions, app) for a first flash. |
| `firmware.bin` | App-only image used by OTA updates. |
| `cam_merged.bin` | Complete ESP32-CAM image (from the **Build CAM BT Co-Processor** workflow). |
| `<id>.zip`, `<id>.elf`, `<id>.manifest.txt`, `nyxapps.json` | Example Nyx Apps, in the rolling pre-release `nyxapps` (from the **Build Nyx Apps (ELF)** workflow, `main`/`master` only). Each zip holds `apps/<id>/{app.elf,manifest.txt}`; the raw `.elf`/`.manifest.txt` files and the `nyxapps.json` index (with SHA-256) are what the on-phone **Nyx Store** downloads. |

The file names above are the ILI9341 build. The 3.5″ ST7796 build publishes `firmware-st7796.bin` and `ren_phone_s3_st7796_merged.bin` in `st7796-N` prereleases, and the Aether Board build publishes `firmware-aether.bin` and `ren_phone_s3_aether_merged.bin` in `aether-N` prereleases. Pick the one that matches your display: the phone refuses OTA firmware built for a different display (see [docs/hardware.md](./docs/hardware.md)).

Prebuilt images already contain the custom core libraries **if** they existed when the build ran (see [the CI section](#what-the-automatic-build-does-and-does-not-do)). The build log of that run (steps *Pasang* and *Verifikasi*) tells you which case you have.

Flash through the browser-based web flasher published on GitHub Pages (its **Nyx Apps** tab also lets you download the example apps), or with `esptool`:

```bash
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 ren_phone_s3_merged.bin
```

On first boot, follow the on-screen **touch calibration** (resistive ILI9341 builds only; the capacitive ST7796 and Aether builds have no calibration wizard).

### Option B — build from source

```bash
# Requirements: arduino-cli, esp32 board package 3.x (Espressif), and the libraries
# LovyanGFX, JPEGDEC, PubSubClient, WebSockets
arduino-cli compile --fqbn esp32:esp32:esp32s3 \
  --board-options "FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
  <assembled-sketch-dir>
```

The 3.5″ ST7796 display and the Aether Board are selected with the `-DRP_DISPLAY_ST7796` and `-DRP_BOARD_AETHER` compiler flags (all settings live in `firmware/phone/display_conf.h`); CI builds each variant in its own job.

A local build uses the **stock** core libraries unless you install the custom package yourself (see [docs/building.md](./docs/building.md#custom-core-libraries-mbedtls-in-psram)); without it, HTTPS has less free internal RAM and Nyx Apps report that the loader is missing.

The firmware is assembled from `firmware/phone/` and `third_party/` by the CI workflow. Step-by-step instructions (CI, manual build, the ESP32-CAM firmware, custom core libraries) are in [docs/building.md](./docs/building.md).

> To build in the cloud without a local toolchain, fork the repository, enable GitHub Actions and run **Build Firmware Binaries**. For OTA updates to download firmware without a login, the repository must be **public**.

### Option C — run the host tests (PC or Termux)

Most logic that does not need hardware is tested on a PC. For example, the Nyx Apps suite (memory arena, host API, example apps through `dlopen`, and a syntax check of the firmware glue):

```bash
cd tests/nyxapp && ./run.sh      # needs g++ and gcc
```

The scripts use `$TMPDIR` (falling back to `/tmp`), so they also run in **Termux**, which has no `/tmp`. All suites are listed in [tests/README.md](./tests/README.md).

### Option D — build a Nyx App

CI builds every folder in `examples/native/` for you. To build one yourself you need ESP-IDF (5.x) and the `idf_template` project:

```bash
cd examples/native/idf_template
export NYX_SDK=$PWD/../../../firmware/phone
cp ../hello/app.c main/app.c
idf.py set-target esp32s3 && idf.py elf          # -> build/nyxapp.app.elf
python3 ../../../tools/pack_app.py --id hello --elf build/nyxapp.app.elf \
        --manifest ../hello/manifest.txt --sd /media/SDCARD
```

Without an SD card, upload `manifest.txt` and `app.elf` through the phone's Web File Manager (*Flash internal (FFat)*, target folder `/apps/<id>`). The app only runs on firmware built with the ELF loader (custom core libraries with `elf_loader`); otherwise the Nyx Apps screen says the loader is missing. Details, limits and the trust model are in [docs/features/native-apps.md](./docs/features/native-apps.md).

## Repository layout

```text
.
├── .github/workflows/        CI: firmware, CAM, custom core libs, Nyx Apps (ELF), web flasher, PC tests
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
├── supabase/                 SQL for the optional device registry (setup.sql, register page, dashboard), cloud backup (backup.sql) and the Chat/Chess broker login (broker_config.sql)
├── third_party/              Vendored third-party code (Nofrendo NES core, ESP-DOOM core)
├── tests/                    Host-side (PC) tests: backup, chess, ebook, gutenberg, camera, dictionary, nyxapp, sysmem, security, wifisense
├── tools/                    Generators and helpers (emoji, fonts, dictionaries, i18n, Nyx App packaging)
│   ├── pack_app.py           Lay out one app as <sd>/apps/<id>/{app.elf,manifest.txt,data/}
│   ├── nyxapps_release.py    Zip built ELFs + build the nyxapps.json index for the web flasher
│   ├── ota_keygen.sh         Create the ECDSA key pair for signed OTA updates (see docs/security.md)
│   ├── ota_sign.sh           Sign an OTA info.txt (used by CI when the OTA_SIGNING_KEY secret is set)
│   ├── patches/              Idempotent patch scripts for phone.ino; the Nyx Apps and SysMem ones are still checked by CI, the rest are kept as history
│   ├── ci/                   Helper scripts used by the CI workflows
│   └── legacy-patches/       Archived one-shot patch scripts, including elf_loader.patch (see its README)
├── CHANGELOG.md              Release notes; the matching section is shown on the phone during an OTA update
├── CONTRIBUTING.md
├── LICENSE                   AGPL-3.0
└── VERSION                   Project version (read by CI)
```

The folder name `firmware/phone/` matches `phone.ino` so the sketch can also be opened directly in the Arduino IDE. See [firmware/README.md](./firmware/README.md) for how the sketch files are organised.

> The repository and some old comments and variables are named `ren_phone`, which was the project's original name.

## Continuous integration

| Workflow | What it does |
|---|---|
| **Build Firmware Binaries** (`build-firmware.yml`) | Every push: compiles the S3 firmware (ILI9341, ST7796 and Aether Board variants), publishes a Release and the OTA channel, then calls the web flasher. |
| **Build CAM BT Co-Processor** (`build-cam-coprocessor.yml`) | Compiles the ESP32-CAM coprocessor firmware. |
| **Build Arduino Libs** (`build-arduino-libs.yml`) | Manual: builds the custom core libraries (mbedTLS in PSRAM, cache sizes, WiFi/lwIP in PSRAM, optional `elf_loader` 2.0.0, optional WiFi CSI with the `wifi_csi` input). Re-run only when the core version or the options change. |
| **Build Nyx Apps (ELF)** (`build-nyxapps.yml`) | Compiles each `examples/native/<id>/` with ESP-IDF to an ELF, checks it (magic, Xtensa, ≤ 1 MB), and on `main`/`master` publishes zips to the `nyxapps` pre-release and refreshes the web flasher. Pushes and pull requests only prove the apps still compile. |
| **Web Flasher (GitHub Pages)** (`flasher-pages.yml`) | Builds the flasher page: stable vs. test firmware, `.bin` downloads, SHA-256, and the Nyx Apps tab. |
| **nyxapp-tests**, **sysmem-tests**, **security-tests** | Run the PC test suites when the related files change. `security-tests` also checks the OTA signing flow (key generation, signing, device-style verification). |

### What the automatic build does and does not do

The build that starts after every push (or pull request to `main`/`master`) is a **firmware** build. It *uses* the custom core libraries but does not *build* them, and it ignores the manual options.

| Question | Answer |
|---|---|
| Does a push build use the custom core libraries (PSRAM mbedTLS/WiFi, bigger cache, `elf_loader`)? | **Yes, if the package already exists.** It downloads `esp32s3-libs.tar.gz` from the `arduino-libs-custom` pre-release and installs it over the stock libraries. |
| Does a push build *create* those libraries? | **No.** Only the manual **Build Arduino Libs** workflow builds them. It is slow (hours); run it once per core version, or when you change one of its options. |
| What if the package is missing, built for another core version or without the expected options? | The build stays **green** and silently uses the stock libraries. Nyx Apps then reports that the loader is missing. Read the *Pasang* and *Verifikasi* steps in the log to be sure. |
| I re-ran Build Arduino Libs. Do existing firmware files change? | No. Start **Build Firmware Binaries** again (push or *Run workflow*) so a firmware is compiled against the new package. |
| Do the manual options (`use_custom_libs`, `loop_core`, `simd_pie`, `nyx_nn`, `push_diff`, `chess_opt`) apply to a push? | **No.** Only *Run workflow* offers them. A push always uses the defaults, and the release notes do not record which options a manual run used. |
| Are the example Nyx Apps compiled on every push? | No, only when something related changes (`examples/native/**`, `nyx_app.h`, the packaging tools or the workflow). |

Typical order for a firmware that can run Nyx Apps:

1. Run **Build Arduino Libs** once with `elf_loader` on (the default).
2. Push, or run **Build Firmware Binaries**.
3. In the log, check that *Verifikasi library kustom terpasang* shows `elf_loader ADA di library kustom`.
4. Flash the new firmware, then copy or install an app.

More detail, including the install conditions and the log lines to look for, is in [docs/building.md](./docs/building.md#what-the-automatic-build-does-and-does-not-do).

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
| SysMem: central memory manager for the main firmware (Indonesian) | [docs/sysmem.md](./docs/sysmem.md) |
| Nyx Apps: manifest, API, building, storage, CI builds | [docs/features/native-apps.md](./docs/features/native-apps.md) |
| Known limitations | [docs/limitations.md](./docs/limitations.md) |
| Troubleshooting | [docs/troubleshooting.md](./docs/troubleshooting.md) |
| Host-side tests | [tests/README.md](./tests/README.md) |

## Known limitations

A short list; the full list is in [docs/limitations.md](./docs/limitations.md).

- Internal RAM is tight. `HTTP -1` errors usually indicate memory fragmentation rather than a network problem. Gemini, Trivia and the NASA apps now try even when the largest free block is below 12 KB and retry a failed connection up to 3 times, but they cannot work when almost no internal RAM is left.
- Audio goes through Bluetooth by default. Without the ESP32-CAM and earbuds, sound needs the optional MAX98357A speaker (Music, online music and calls only, `BETA`); AVI video sound and everything else stay silent. Online music and AVI video share one PSRAM ring, so opening a video stops an online track. DOOM has no sound at all.
- DOOM needs a contiguous block of about 6.25 MB of PSRAM for its full 6 MiB zone; with less it runs with a 5 or 4 MiB zone and big levels may fail. A WAD on FFat is streamed by a helper task and no longer has to fit in PSRAM (see [Games](./docs/features/games.md#doom-wad-files)).
- Hardware scroll and row-diff push exist only on ST7796 and Aether builds. Vertical scrolling in landscape cannot use hardware scroll, because the panel can only scroll along its long (480-pixel) axis; landscape Home uses swipeable pages instead. Hardware scroll also steps aside while someone views Remote Control, in game mode, while an overlay is open and, on portrait Home, when a wallpaper is set (see [System UI](./docs/features/system-ui.md#home-pages-and-hardware-scroll-st7796)).
- Phone calls: partially tested; no ICE; TURN over UDP only; no PSTN numbers through free services.
- WiFi Sensing reports only "motion" and "someone still is breathing" (no pose, no seeing through walls). Breathing needs the phone to be still, the person within about 1-3 m and a router that is not too far away. Not yet tested on a device.
- TLS certificates are not verified (see the security notice). The Web File Manager uses HTTP Basic authentication over plain HTTP, so use it only on networks you trust.
- Nyx Apps need the ELF loader, which is only in custom core libraries built with `elf_loader` enabled. The loader and the `hello` example run on a device; the Nyx Store, the storage choice and the other examples are not yet confirmed. App files on internal flash share the roughly 9 MB FFat partition. See [Nyx Apps](./docs/features/native-apps.md).
- There is no memory compaction: native apps use real pointers, so blocks are never moved. Fragmentation is limited (per-app arenas, block merging, the TLS gate), not removed.
- Most CI steps do not pin tool versions, so upstream updates can break a build without any change in this repository. The Nyx Apps build pins its ESP-IDF image and `elf_loader` (2.0.0); the custom libraries pin the core version (3.3.x).
- A green push build does not prove the custom libraries were used: if the package is missing or does not match the core version, the build falls back to the stock libraries without failing.

## Security notice

This project is a personal, experimental device. Before using it with sensitive data or networks, read [docs/security.md](./docs/security.md). In short:

- TLS certificates are **not verified** (`setInsecure()`); do not send sensitive information.
- SIP signalling and RTP audio are **not encrypted**.
- The Web File Manager requires a login (user `admin`, random password on first start). The password is stored only as a salted hash and shown masked in File Explorer. Wrong passwords lock the login (60 s, 5 min, 15 min, 1 h). The page answers only while File Explorer is open and for 10 minutes afterwards. Basic authentication over plain HTTP is **not encrypted**, so enable it only on trusted networks.
- Gemini and NASA API keys are stored **encrypted** (`enc1:...`) with a key bound to the device, so an SD card moved to another device cannot read them. This does not protect against someone who dumps the flash of the device itself.
- The Remote Control WiFi uses WPA2 with a random password (the old public default is replaced automatically).
- OTA updates can be verified with an ECDSA P-256 signature. This is **off by default**: it starts working once a public key is placed in `firmware/phone/ota_pubkey.h` (see [docs/security.md](./docs/security.md)).
- On the first WiFi connection the device registers itself once with an anonymous id (a hash of the chip MAC) and sends its firmware version, build, branch and display type. A public username is optional and shown on the dashboard only if you set one. Leave the username empty to stay off the public list; details are in [docs/security.md](./docs/security.md).
- The Chat and Chess broker logins are served by a Supabase function that the public key in the firmware can call, so **anyone who reads the repository can obtain them**. Use broker users made only for this project, limited by ACL, never an admin account (see [docs/security.md](./docs/security.md)).

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
