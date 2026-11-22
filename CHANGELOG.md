# Changelog

## Unreleased
- New: optional MAX98357A speaker support is back. HWmonitor has a "Test Speaker" button that plays a 1 kHz tone (default pins BCLK 16, LRC 17, DIN 8 or 15; see docs/hardware.md)

## 1.0.3
- New: Nyx Store - browse, install, update and remove Nyx Apps from your phone (Store button in Nyx Apps; needs WiFi)
- Improved: new look for Nyx Store, the Nyx Apps list and File Explorer (squircle icons, glass rows, file tiles colored by type)
- New: the Web File Manager now needs a login (user admin, random password). The password is stored only as a salted hash, shown masked in File Explorer (tap to reveal for 10 s), and can be changed on the phone or on the web page (/passwd)
- Changed: the Web File Manager only responds while File Explorer is open and for 10 minutes afterwards. Wrong passwords lock the login (60 s, 5 min, 15 min, 1 h), even after a reboot
- New: Gemini and NASA API keys are stored encrypted. Plain-text keys are encrypted on the next load, and an SD card moved to another device cannot read them
- New: the Remote Control WiFi password is now random (the old default is replaced automatically); see it in the Remote app
- New: optional signed OTA updates (ECDSA P-256 + SHA-256), off until a public key is added (see docs/security.md)
- Changed: the ILI9341 and ST7796 builds are now named Accretion v1 and Accretion v2; About Phone shows the board and screen of the device it runs on
- Not changed: TLS certificates are still not verified (setInsecure) in Chat, chess, Gutenberg and the OTA download

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
