# Known limitations

## Memory and network

- Internal RAM is tight. `HTTP -1` often signals memory fragmentation rather than a network problem. Wait-and-retry mechanisms exist, but they cannot completely eliminate the problem at the sketch level.
- TLS certificates are not verified (see [Security](./security.md)).

## Audio

- Audio goes through Bluetooth by default. Without the ESP32-CAM and TWS earbuds, only the optional MAX98357A speaker can produce sound (Music app and phone calls; `BETA`, not yet tested on hardware). AVI video sound and the other apps stay silent.

## Phone calls

- Limited testing: it works normally with `sip2sip.info` over TCP; a `sip.linphone.org` account only works in one direction (as a receiver); LAN mode and TURN are unconfirmed.
- No ICE yet; TURN is UDP only.
- Calls to PSTN numbers are not supported through free services.

## Camera

- All features were tested on a single unit belonging to the owner; behaviour on other units (cables, power supply, sensor) may differ. See the risks in [Camera](./features/camera.md#risks-to-be-aware-of).

## Other features

- **Chat:** the screen shows ASCII and emoji (other letters appear as `?`); there is no notification while the app is closed.
- **Chess:** there is no chess clock. Not tested on a device; online mode needs an MQTT broker with your own account (separate from Chat).
- **Backup & Restore:** at most 6 files, a single file at most 96 KB, NVS values above 32 KB are skipped. Restore overwrites values present in the file and does not delete settings created afterwards. An unencrypted backup can be read by anyone holding the SD card. The Cloud tab is tested on a device; the SD Card tab is not.
- **Emoji:** single emoji only (one code point). Flags, ZWJ sequences, keycaps and skin-tone variants do not have their own sprites; at most 4,000 sprites, and a large set uses PSRAM (about 1.8 KB per emoji). Details in [System UI](./features/system-ui.md#emoji-beta).
- **Battery:** the percentage is an estimate. If the INA219 is not detected, the percentage and voltage stop updating (there is no ADC *fallback*).
- **Gutenberg download:** TXT at most 3 MB and EPUB at most 40 MB; the list holds at most 96 books; one download at a time. The language filter is only for the Gutendex source. The source choice (Gutendex/OPDS) is not remembered after a restart (default: Gutendex). A two-parallel-connection download exists in the code but is turned off (`GUT_PARALLEL 0`).
- **PNG:** at most 4 MB and 8 million pixels; above 2 MB there is no thumbnail.
- **OpenDML AVI (> 1 GB)** is not supported for seek and resume. VBR MP3 may drift slightly after a long jump.
- **NyxPPU** is not yet used in Inferno or the Home/Lock Screen.
- **Nyx Apps:** partly tested. The ELF loader is only present when the custom core libraries are built with the `elf_loader` option. The loader, the launcher and the `hello` example run on a real phone; the Nyx Store, the SD/FFat storage choice, the other examples, the watchdog and HTTP from apps are not yet confirmed. CI compiles the example apps and the web flasher offers them. Apps stored on internal flash share the roughly 9 MB FFat partition, and app file access there is slower than on the SD card. Apps are not sandboxed against a malicious app: install only apps you trust. See [Nyx Apps](./features/native-apps.md).
- **Hardware scroll and row-diff push:** ST7796 and Aether builds only. Vertical scrolling in landscape cannot use hardware scroll (the panel scrolls only along its 480-pixel axis). In portrait, hardware scroll falls back to full-frame drawing while someone views Remote Control, in game mode, when PSRAM is low, while an overlay is open and, on Home, when a wallpaper is set. Hardware scroll on screens other than Home is `BETA` (see [System UI](./features/system-ui.md#home-pages-and-hardware-scroll-st7796)).
- **DOOM from FFat:** the WAD is streamed from flash through a helper task (`wadio`, about 10 KB of internal RAM), so it no longer has to fit in PSRAM. Save games are not stored while the WAD is on FFat; use the SD card if you want to save games. DOOM has no sound. The zone memory is 6 MiB only when a contiguous PSRAM block of about 6.25 MB is free, otherwise 5 or 4 MiB, and big levels may then fail (`Z_Malloc: failed`, the device restarts). Restarting the phone before opening DOOM gives the largest block.

## Build and release

- CI does not pin tool versions, so upstream updates can break a build without any change in the repository.
- The automatic build after a push only **installs** the custom core libraries; it never builds them. Only the manual *Build Arduino Libs* workflow does. Manual inputs such as `nyx_nn` or `loop_core` are not available to push builds. See [Building](./building.md#what-the-automatic-build-does-and-does-not-do).
- The custom core libraries are tied to one core version. If the core version in CI changes, the package in the Release is skipped (the build stays green but uses the stock libraries) until the *Build Arduino Libs* workflow is run again.
