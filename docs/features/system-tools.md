# System and device tools

- [Battery (INA219)](#battery-ina219-ok)
- [OTA updates (per channel)](#ota-updates-per-channel)
- [Settings Backup and Restore](#settings-backup-and-restore-beta)
- [Crash log](#crash-log-beta)
- [HWmonitor](#hwmonitor)
- [Other utility apps](#other-utility-apps)

## Battery (INA219) `OK`

Power is read from the **INA219** sensor over I2C. The app has five tabs (Summary, Graph, Sensor, Battery, Per App) with these capabilities:

- real-time voltage, current and power, plus a 10-minute graph;
- battery percentage from a Li-ion voltage curve, internal-resistance compensation and *coulomb counting*;
- charge detection, mAh in and out, an estimate of remaining time or time to full, and a cycle count;
- full chip configuration (shunt, PGA, ADC resolution, *averaging*, operating mode), stored in NVS;
- a **Current direction** option to flip the sign of the current if the shunt wiring is reversed, and a "Charging" status that triggers the lock-screen animation;
- the **Per App** tab `BETA`: the mAh drawn from the battery is recorded per app (the app currently on screen; *Home* and *Locked* are separate categories), sorted from the most power-hungry. Statistics are stored in NVS and reset automatically when the battery is full (or with the Reset button). This is an estimate: background use (for example music) is attributed to the app that is open.

**Wiring:** VCC 3.3 V, GND, SDA 15, SCL 7 (on the ST7796 build the sensors use SDA 5 / SCL 4; see [Hardware](../hardware.md#35-st7796-display-variant)). The shunt resistor is placed in series with the battery's positive wire. Defaults: 100 mΩ shunt, gain /8, 2300 mAh capacity, 150 mΩ internal resistance.

> The battery percentage is an **estimate** (there is no dedicated *fuel-gauge* chip). Fill in Capacity and internal R correctly on the Battery tab so the numbers make sense.

## OTA updates (per channel)

Updates the firmware from GitHub without a cable, with several layers of protection.

1. Choose a **channel** (a branch name) as the firmware source.
2. *Check for updates* reads `info.txt` (branch, build, code base, size, MD5).
3. The device **refuses** firmware from a different "code base". (A phone also refuses firmware whose `display=` value does not match its display; see [Hardware](../hardware.md#35-st7796-display-variant).)
4. Before installing, the device checks the size, the ESP32-S3 image header and the **MD5 of the whole file**. If any check fails, the update is aborted and the old firmware is untouched.
5. **Boot safeguard:** the new firmware is marked *pending*. If it has not been declared healthy (the loop alive for 30 seconds) by the 4th boot, the device **automatically returns to the previous firmware**.

**Notes:**

- The rollback safeguard only applies to updates performed by firmware that already contains this code.
- A branch that has never been built shows the message *Channel not published yet*. Run the build workflow for that branch once so the `channel-<branch>` Release is created.
- Old firmware that does not contain the channel code has no branch picker; install it once through the flasher or the SD card.

## Settings Backup and Restore `BETA`

Saves all NyxOS settings to the SD card and restores them, for example before reflashing with *erase flash* or when moving to another unit. Opened from **Settings → About → Backup & Restore** (it does not appear on the Home grid).

| Action | Result |
|---|---|
| **Back Up Now** | All settings in NVS (theme, brightness, Home layout, calibration, learned dictionary words, Remote Control, and more) are written to `/backup/nyx_NNN.nbk`. The NVS contents are scanned rather than a fixed list, so settings from new features are included automatically. System namespaces (`nvs*`, `phy*`, `bt*`, `otaguard`) are excluded. |
| **History** | A list of backups with number, time, value count and size. At most **6** files; the oldest is removed automatically when a new backup is made. |
| **Restore** | Choose a backup and tap twice (*Sure? Tap again*). The values are written back to NVS, then the phone **restarts by itself** because settings are read into RAM at boot. Restore overwrites values present in the file; settings created after the backup are not deleted. |
| **Delete** | Removes one backup file (also with a double-tap confirmation). |
| **Include passwords and API keys** | A switch, **off** by default. WiFi passwords, SIP account, Chat account, Gemini API key and the Remote Control AP password are only included when this switch is on. |
| **Encrypt with a password** | A switch. Subsequent backups are encrypted with a 6 to 64 character password that is asked for every time (twice when creating) and is **not stored**. |
| **Automatic backup** | Without pressing anything: about 45 seconds after boot and then every 10 minutes, the whole NVS is copied to `/backup/auto.nbk`, **only when its content has changed**. If NVS is empty (flash erased or a new unit) and `auto.nbk` exists, all settings are restored automatically at boot and the phone restarts once. Passwords and API keys are included only when *Include passwords and API keys* is on **and** encryption is off (an automatic file cannot ask for a password). It does not count toward the limit of 6 files. Changes made in the last 10 minutes before power is lost are not yet copied. |

**File security.**

- An unencrypted `.nbk` file is **not encrypted**: anyone holding the SD card can read it. That is why passwords and API keys are excluded by default.
- An encrypted file uses PBKDF2-HMAC-SHA256 for key derivation, AES-256 in CTR mode and HMAC-SHA256 (*encrypt-then-MAC*). A wrong password or a modified file is rejected before any value is applied. Key derivation takes about 0.5 seconds.
- When restoring, the file from the SD card is treated as untrusted: CRC32 is checked in full, all lengths are checked against buffer limits and system namespaces are never written. A corrupted backup is rejected.
- One file is at most 96 KB; NVS values above 32 KB are skipped. Buffers are allocated in PSRAM only for the duration of the operation.

The file format and crypto are written as pure C++ (`backup_core.h`, `backup_crypto.h`) and tested on a PC, including comparison against Python's `hashlib` and `cryptography` (`tests/backup/`). **Not tested on a device yet.**

## Crash log `BETA`

At boot, NyxOS reads the last reset reason. For an abnormal reset (panic, watchdog, brownout), one line is written to `/crashlog.txt` on the SD card: the time, the reason, the screen that was open, the remaining internal RAM (free, minimum, largest block), the uptime and, if flash core dump is enabled in the build, the task + crash address + backtrace (decode with `addr2line` against the ELF file from CI). The latest details appear in **About Phone**. The file is rotated at 32 KB (`/crashlog.old`).

## HWmonitor

Live monitoring on the device: per-core load, internal RAM, PSRAM and the *largest free block*. The main tool for diagnosing memory problems.

## Other utility apps

Screenshot (BMP to `/screenshot`), Stopwatch and Timer (keep running in the background), Pomodoro, Neopixel, Mic Level, 3D Orient (MPU6050), Notepad, Canvas, scientific Calculator, Clock, Files and the Web File Manager.
