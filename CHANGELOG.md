# Changelog

## Unreleased
- New: Web File Manager login is now stored as a salted PBKDF2 hash (not plain text); old plain-text passwords are migrated automatically on first boot
- New: the password on the File Explorer screen is masked (`********`); tap the password part to show it for 10 seconds. After a reboot only the hash exists, so the line shows "(tersimpan)" — tap it to set a new password or generate a random one
- New: change the Web File Manager password and user name from the web page itself (`/passwd`, the old password is required)
- Changed: wrong-password lockout now escalates (60 s, 5 min, 15 min, 1 h) and survives a reboot; a successful login resets it
- New: Gemini and NASA API keys are stored encrypted (`enc1:...`, AES-256 + HMAC, device-bound key). Plain-text keys pasted into the files are encrypted automatically the next time they are loaded. An SD card moved to another device can no longer read the keys
- New: OTA updates can be verified with an ECDSA P-256 signature over `info.txt` plus a SHA-256 of the firmware (off until a public key is put in `ota_pubkey.h`; see docs/security.md)
- Not changed (deferred): TLS certificate verification (`setInsecure()`) in Chat, chess, Gutenberg and the OTA download
- Changed: the Web File Manager only responds while File Explorer is open (and for 10 minutes afterwards); open File Explorer on the phone to enable it again
- New: Web File Manager login is locked for 60 seconds after 5 wrong passwords; passwords are no longer printed to the serial log
- New: you can now change the Web File Manager password yourself (tap the "Login web" line in File Explorer), or reset it to a new random one
- New: Nyx Store — browse, install, update and remove Nyx Apps from your phone (open it with the Store button in Nyx Apps; needs WiFi)

## 1.0.2
- New: NES can now load ROMs from internal flash (FFat) when no SD card is detected
- Fixed: NES dropped straight back to Home after picking a ROM
- New: Nyx Apps can now run from internal flash (FFat) when no SD card is inserted
- New: the web file manager has a Folder field to upload straight into a folder such as /apps/hello
- Improved: swiping Home pages left and right now follows your finger sooner, without a jump

## 1.0.1
- Fixed: Cloud restore always said "No backup for this device" even right after a successful backup
- New: countdown before the automatic restart after a restore (SD Card and Cloud)

## 1.0.0
- New: OTA updates now show an English changelog before you install
- New: one-time device registration to Supabase on first online boot
- Improved: Home landscape layout (widgets on page 1, apps on the other pages)
- Improved: smoother scrolling on the ST7796 screen
