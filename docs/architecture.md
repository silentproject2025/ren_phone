# Architecture

This document explains how NyxOS is put together: the two-chip design, how memory is managed, how work is split across cores and tasks, how frames are rendered, and how the main chip talks to the Bluetooth/camera coprocessor.

- [System overview](#system-overview)
- [Memory is the main constraint](#memory-is-the-main-constraint)
- [Task layout](#task-layout)
- [Rendering](#rendering)
- [Bluetooth coprocessor (RPLink)](#bluetooth-coprocessor-rplink)

## System overview

The device consists of two microcontrollers connected over UART. The ESP32-S3 handles the UI, apps and networking. The ESP32-CAM acts as a **coprocessor** that handles Bluetooth audio (A2DP) and the camera.

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

Important design consequences:

- **There is no built-in speaker.** All audio output (music, video, calls, voice memos) goes through the ESP32-CAM to Bluetooth TWS earbuds. An optional MAX98357A module on the S3 can play Music and phone calls instead (`BETA`); AVI video sound still needs Bluetooth.
- **Two firmwares must be kept consistent.** `rplink.h` must be **identical** on the S3 side (`firmware/phone/rplink.h`) and the CAM side (`firmware/cam_bt_coprocessor/rplink.h`).
- **Memory is the scarcest resource.** See below.

## Memory is the main constraint

Internal RAM is only **~162 KB**, while a single HTTPS connection needs a **contiguous** block of roughly 20–30 KB. Without strict memory discipline, `HTTP -1` failures and random reboots are unavoidable.

| Rule | How it is applied |
|---|---|
| Large buffers never live in internal RAM | Display sprites, caches and decode buffers are allocated in PSRAM. |
| Heavy task stacks are moved to PSRAM | `createTaskPsramStack()` is used for Gemini, NES, Music, Wake Word, Chat and Phone. |
| Wait before giving up | Pauses and retries give lwIP time to release the previous TLS buffers. |
| Network apps can be "sacrificed" | The **App Memory Guard** frees background app caches when RAM runs low. |
| Smaller mbedTLS | Build flags described in [Building → manual build](./building.md#option-c--manual-build-for-the-esp32-s3). |
| mbedTLS buffers in PSRAM | Custom core libraries (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), see [Building → custom core libraries](./building.md#custom-core-libraries-mbedtls-in-psram). |
| Emoji set from SD in PSRAM | `emjInit()` reads `/emoji.bin` once at boot; code points, lookup index and pixels are all allocated in PSRAM (~1.8 KB per emoji). |

## Task layout

```
Core 0                              Core 1
──────────────────────              ──────────────────────
musTask (prio 3, UART to CAM)       doomTask   (DOOM, prio 1)
Gemini / Chat / Wake Word           NES task   (48 KB stack in PSRAM)
WiFi / lwIP (owned by ESP-IDF)      callTask   (Phone, prio 3)
```

`loop()` (UI, input, redraw) runs on the core set by `CONFIG_ARDUINO_RUNNING_CORE`; the Arduino-ESP32 default is **Core 1**. The firmware prints `[CORE] setup/loop berjalan di core N` at boot, so check the Serial Monitor. If `loop()` is on Core 1, DOOM/NES/Phone share a CPU with the UI.

The `nyxHelper` task (the second core for Chess Nyx search, see [Chess](./features/chess.md)) runs on the core opposite `loop()`, only while Nyx is searching, with a 10 KB stack in PSRAM and priority 1.

**Task priorities** (higher number = higher priority; `loopTask` = 1):

| Task | Priority | Reason |
|---|---|---|
| `musTask`, `callTask` | 3 | UART/RTP audio streams are jitter-sensitive. |
| `wakeTask`, `memoRec`, `micTask` | 2 | I2S microphone reads via DMA must not starve. |
| Other HTTP / decode / emulator tasks | 1 | They block for a long time or always `vTaskDelay`. |

Use `createTaskPsramStackPrio()` when a priority other than 1 is needed.

The UI stays in `loop()` so drawing order is deterministic. Blocking work (HTTP, decoding, emulators) is moved to separate tasks, and the UI only reads their status.

> Do not write NVS/`Preferences` or run OTA from a task whose stack lives in PSRAM: when the flash cache is briefly disabled, this can crash the device.

## Rendering

Frames are drawn into a sprite in PSRAM (LovyanGFX) and then pushed to the panel. Transitions are **time-based** (`millis()`), not frame-count-based, so their duration stays consistent even when the CPU is busy. When Home is only scrolling, the header, status bar and dock are not redrawn.

## Bluetooth coprocessor (RPLink)

The ESP32-S3 does not use Bluetooth for audio. Data is sent over UART (921,600 baud) to the **ESP32-CAM**, which decodes MP3 (libhelix) and sends it over A2DP to the TWS earbuds.

```
SD ─▶ S3 (master) ──UART──▶ ESP32-CAM ──A2DP──▶ TWS
      UI, playlist, RYNE      MP3 decode + PCM buffer
```

**Frame format** (`rplink.h`, **must be identical on both sides**):

```
[0xA5][type][lenLo][lenHi][payload ≤ 1024][xor]
```

`xor` is the XOR of `type`, the length bytes and the entire payload.

| Direction | Commands |
|---|---|
| S3 → CAM | `P` begin · `D` data · `F` finish · `U` pause · `V` volume · `X` stop · `A` scan · `C` connect · `T` tx-gain · `B` BT mode · `G` ping |
| CAM → S3 | `S` status · `E` track ended · `L` scan result · `W` scan done · `R` hello · `K` earbud button |

The same protocol also carries camera frames (preview, stills and sequence-number flow control) and is raised to 5 Mbaud while the Camera app is open, with automatic step-down when the link is noisy. See [Camera](./features/camera.md).

AVRCP absolute volume is deliberately **not advertised**, because on some earbuds it causes the CAM to be reset by its watchdog. The `RP_AVRCP_ENABLE` switch exists to isolate memory or link problems.
