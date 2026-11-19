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
2. *Check for updates* reads `info.txt` (branch, build, code base, size, MD5). When an update is found it also downloads the optional `changelog.txt` and opens a **What's new** screen with the English release notes (tap *Close* or Back to dismiss; tap the status text on the top card to reopen it). A missing changelog never blocks the update.
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

**Cloud backup (Cloud tab).** The same screen has a second tab, **Cloud**, that stores your settings in the project database (Supabase) without an account or password. Needs WiFi and `supabase/backup.sql` run once on the server.

| Action | Result |
|---|---|
| **Back Up to Cloud** | Settings are encrypted on the phone and uploaded. One slot per device; a new backup replaces the old one (at most once every 30 seconds). |
| **Restore** / **Delete** | Tap twice to confirm. Restore downloads, verifies and decrypts the backup, writes it back to NVS and restarts. Delete removes it from the server. |
| **Include passwords and API keys** | Same switch as the SD tab, **off** by default. Still encrypted when on. |

How it is protected:

- **Your device code is your user** (shown on the tab, derived from the chip's MAC, cannot be changed). It is *not* a secret, since a MAC is broadcast over WiFi and Bluetooth, so it never opens anything by itself.
- Every request carries a **token** derived from a secret that never leaves the chip (the eFuse unique ID). The server stores only the SHA-256 of the token and releases or changes a backup only when it matches. The first device to use a code owns it.
- The file is encrypted **on the phone** (AES-256 + HMAC) with a second key derived from the same secret and never sent. The server and the database admin see only random data, and the server rejects anything that is not an encrypted file. A modified backup is rejected before anything is applied.
- Nothing personal is uploaded: no name, email, IP or SSID. Only the device code, the encrypted blob, its size and time. The file header (time, number of values, whether passwords are included) is not encrypted.
- Because the key comes from the chip, a cloud backup can be restored **only on the same device**. To move to another unit, use the SD card.
- If a chip has no usable unique ID, a random key is kept in NVS instead (the tab warns you). That backup cannot be restored after *erase flash*.
- TLS: set `RP_REG_CA` in `devreg_conf.h` to verify the server. Left empty, the connection is not verified (like the other network modules); the data stays encrypted but the token could be intercepted by an active attacker on the same network.

The file format and crypto are written as pure C++ (`backup_core.h`, `backup_crypto.h`, `backup_cloud.h`) and tested on a PC, including comparison against Python's `hashlib` and `cryptography` (`tests/backup/`). **Not tested on a device yet.**

## Crash log `BETA`

At boot, NyxOS reads the last reset reason. For an abnormal reset (panic, watchdog, brownout), one line is written to `/crashlog.txt` on the SD card: the time, the reason, the screen that was open, the remaining internal RAM (free, minimum, largest block), the uptime and, if flash core dump is enabled in the build, the task + crash address + backtrace (decode with `addr2line` against the ELF file from CI). The latest details appear in **About Phone**. The file is rotated at 32 KB (`/crashlog.old`).

## HWmonitor

Live monitoring on the device: per-core load, internal RAM, PSRAM and the *largest free block*. The main tool for diagnosing memory problems.

## Other utility apps

Screenshot (BMP to `/screenshot`), Stopwatch and Timer (keep running in the background), Pomodoro, Neopixel, Mic Level, 3D Orient (MPU6050), Notepad, Canvas, scientific Calculator, Clock, Files and the Web File Manager.
