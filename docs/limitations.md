# Known limitations

## Memory and network

- Internal RAM is tight. `HTTP -1` often signals memory fragmentation rather than a network problem. Wait-and-retry mechanisms exist, but they cannot completely eliminate the problem at the sketch level.
- TLS certificates are not verified (see [Security](./security.md)).

## Audio

- Audio only goes through Bluetooth. Without the ESP32-CAM and TWS earbuds, no app produces sound.

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
- **DOOM from FFat:** the WAD must fit in PSRAM and save games are not stored; use the SD card for large WADs or if you want to save games.

## Build and release

- CI does not pin tool versions, so upstream updates can break a build without any change in the repository.
- The custom core libraries are tied to one core version. If the core version in CI changes, the package in the Release is skipped (the build stays green but uses the stock libraries) until the *Build Arduino Libs* workflow is run again.
