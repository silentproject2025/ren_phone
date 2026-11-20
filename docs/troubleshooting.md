# Troubleshooting

Find your symptom in the table. For memory-related problems, the **HWmonitor** app (internal RAM, PSRAM, largest free block) is the main diagnostic tool.

## Memory, network and audio

| Symptom | Likely cause | Action |
|---|---|---|
| HTTPS features (Gemini, NASA, Trivia) return `HTTP -1` | Internal RAM fragmentation, or the Remote AP is active | Turn off the Remote AP; check the *largest free block* in HWmonitor; retry. |
| No sound from any app | TWS not connected, the ESP32-CAM not fitted, or the SD card in the CAM module not removed | Connect the TWS in the Music app; check the CAM wiring and firmware. |
| Video plays without sound | TWS not connected, or the audio is not 44.1 kHz MP3 | Re-convert with the `ffmpeg` command in [Media](./features/media.md#avi--mjpeg-player). |
| The device restarts by itself right after the boot screen after changing the custom SDK libraries | The new cache settings (especially `psram_cache_line_64b`) do not suit; the boot safeguard restores the old firmware after several failures | Rebuild the libraries with the 64 B line off, `dcache_kb=32`, `icache_kb=16`; check Settings → About (last reset) and `/crashlog.txt` on the SD card. See the safety note in [Chess](./features/chess.md#nyx-max-chess-strength-up-to-the-hardware-limit). |
| The screen is striped, shaky or randomly colored | SPI 80 MHz exceeds what the cable can do | Lower to 60 MHz in Settings, then restart. |
| Battery percentage and voltage do not change | INA219 not detected | Check the SDA 15 and SCL 7 wiring and the I2C address. |
| The charging animation does not appear | The phone is not locked, the charger was connected since boot, or the INA219 does not detect charge current | Lock the phone, wait 10 seconds, unplug the charger for 3 seconds, then plug it in again; check the "Charging" status on the Battery tab and the Current direction option. |
| New features do not appear after flashing | The latest build failed, so an old `.bin` was flashed | Check Actions and the build number in About Phone. |
| The touch screen does not respond (ST7796 build) | The FT6236 SDA wire does not match the pin: the default is now **SDA 6, SCL 7** | Move the SDA wire to GPIO 6 (or change `RP_TOUCH_SDA` in `display_conf.h`). Serial should print `[touch] FT6236 @0x38 TERDETEKSI (SDA=6 SCL=7 ...)`. |

## Phone calls

| Symptom | Likely cause | Action |
|---|---|---|
| The call connects but is silent or one-way | NAT or CGNAT without a relay | Configure TURN, or use LAN mode. |
| SIP registration fails | Transport does not match the provider, or wrong credentials | Try `transport=udp` in `sip.cfg`; check Server, username and password. |
| You cannot hear the other party during a call | TWS not connected | Connect the TWS first. |
| Phone with a `sip.linphone.org` account can only receive | That account only works in one direction on this firmware | Use a `sip2sip.info` account. |

## Emoji and keyboard

| Symptom | Likely cause | Action |
|---|---|---|
| Emoji appear as a `?` box | The emoji is not in the active set (flags, ZWJ sequences, or emoji outside `emoji.bin`) | Use single emoji, or regenerate `emoji.bin` from a fuller source. |
| Only the 54 built-in emoji sprites appear | `/emoji.bin` is not used: missing from the SD root, SD unreadable, header or size mismatch, or insufficient PSRAM | Copy `emoji.bin` to the SD root again, regenerate it with `tools/make_emoji_bin.py`, reduce the number of emoji, then restart. |
| Emoji look scrambled (not a `?` box) | Not yet determined. An intact, correct `emoji.bin` has been confirmed not to cause it; the Remote web preview (JPEG, about 2 fps) can also blur small details | Run `python3 tools/preview_emoji_bin.py emoji.bin check.png` (check that the size matches and the sprites look right), then compare with a photo of the real phone screen, not the web preview. |

## Camera

| Symptom | Likely cause | Action |
|---|---|---|
| The Camera preview freezes for a few seconds after running, but the phone does not crash | The **last** chunk of a frame was lost on the UART link (more often at high baud) and the preview buffer slot being filled leaked (older `cam_rx.h`); after three leaks all frames are dropped | Already fixed: a slot that never completes is freed when the next frame starts. If it still happens, look at the *dropped* and *bad* counts on the System tab of the Camera app and lower the link baud. |
| The Camera app shows "CAM does not support the camera yet" | The CAM firmware has not been updated | Reflash the `firmware/cam_bt_coprocessor` firmware. |
| "CAM RAM low" message | Camera, A2DP and MP3 running together | Turn off Bluetooth or music before opening Camera. |
| The ESP32-CAM restarts often | *Brownout* from a weak power supply | Use the 5V pin with a strong supply. |

## Chess

| Symptom | Likely cause | Action |
|---|---|---|
| Chess shows "Set the Chess broker first" or "Chess broker not set" | Broker host, username or password not filled in | Fill them in the Chess app and **Save**, put `/chess.cfg` on the SD card, or copy from the Chat settings. |
| Chess shows "Wrong username/password" or "Cannot reach the broker" | Wrong credentials, WiFi not connected, or the broker is unavailable | Check the account and host; make sure WiFi is on; if an ACL exists, allow `renphone/chess/#`. |
| The Chess Network screen shows "RAM not enough for diff" | The initial weights for the **Diff** chip do not fit in PSRAM | Close other apps and reopen the Network screen; the Network, Weights and Squares tabs still work without Diff. |
| Chess shows "RAM tight, close other apps" | Not enough internal RAM for the TLS connection | Close other apps (App Switcher → Close all), then try again. |
| The NPS tab shows "2nd core not running" | The helper task could not be created (not enough PSRAM for the 10 KB stack) or the 2nd core switch is off | Turn the **2nd core** switch on; check free PSRAM in HWmonitor. |

## Backup, OTA and updates

| Symptom | Likely cause | Action |
|---|---|---|
| Backup shows "SD card not detected" | The SD card cannot be read | Check the SD card; backups are stored in `/backup/` on the SD card. |
| Cloud restore shows "No backup for this device" right after a successful cloud backup | Firmware 1.0.0 could not read the server's reply when it contained spaces after the colons (`"status": "ok"`) | Update to 1.0.1 or newer. If it still happens: no backup was ever saved under this device code, or the key was kept in NVS (the Cloud tab warns about it) and was lost after *erase flash*. |
| Restore shows "Backup corrupted, rejected" or "Wrong password" | The file is corrupted or modified, or the encryption password is wrong | Choose another backup; for an encrypted file enter the same password used when the backup was made. |
| The message *Channel not published yet* appears | The branch has never been built | Run the build workflow for that branch once. |
| An OTA update is rejected | The firmware's code base differs (or the `display=` value does not match the phone's display) | Intended behaviour; use firmware from the same code base and display variant. |

## Other

| Symptom | Likely cause | Action |
|---|---|---|
| DOOM restarts the device with `assert failed: spi_flash_disable_interrupts_caches_and_other_cpu ... esp_task_stack_is_sane_cache_disabled()` | Old firmware: the DOOM task (stack in PSRAM) read the WAD from FFat | Update the firmware (an FFat WAD is now loaded into PSRAM first), or put the WAD on the SD card. |
| The `/remote` page over home WiFi answers `403` | The *Allow via home WiFi* option is off | Turn the AP on in the *Remote* app (or reboot if *Auto start* is on), connect to the Accretion-Phone WiFi, open `http://192.168.4.1/remote`, then turn the option on. |
| Wake Word is hard to recognise | A noisy room or unrepresentative samples | Re-record the 4 samples in a quiet environment; adjust the *Sensitivity* slider. |

## Build and CI

| Symptom | Likely cause | Action |
|---|---|---|
| CI build fails with `undefined reference to ...` | A new `.ino` file (or a `#include`d header) is not registered in the `cp` steps of `build-firmware.yml` | Add a line `cp firmware/phone/<file>.ino build_sketch/`, then push again. |
| CI build is green but does not use the custom libraries | One of the package install conditions is not met | Check the *Pasang* (install) and *Verifikasi* (verify) step logs (see [Building](./building.md#custom-core-libraries-mbedtls-in-psram)). |
