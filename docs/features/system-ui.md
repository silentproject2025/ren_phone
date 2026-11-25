# System UI `OK`

NyxOS delivers a modern-phone experience on a small screen (320×240, or 480×320 on the ST7796 variant).

- [Components](#components)
- [Home pages and hardware scroll (ST7796)](#home-pages-and-hardware-scroll-st7796)
- [The "Nyx Modern" look](#the-nyx-modern-look)
- [Charging animation](#charging-animation-ok)
- [New system gestures](#new-system-gestures-beta)
- [Notifications and App Switcher](#notifications-and-app-switcher-v133-beta)
- [Ghost-touch protection](#ghost-touch-protection)
- [Personalisation](#personalisation)
- [Emoji](#emoji-beta)
- [Keyboard dictionary](#keyboard-dictionary-word-suggestions-and-autocorrect-beta)

## Components

| Component | Function |
|---|---|
| **Lock Screen** | Clock, custom wallpaper, swipe to unlock. While unlocking, the lock screen **rises with your finger** and reveals Home underneath. |
| **Home** | Icon grid with search, a weather widget (Open-Meteo, no API key) and notification badges on icons. |
| **Control Center** | Buttons for WiFi, Airplane, DND, Theme, Orientation, Shake, Lock, Neopixel, Silent, Bluetooth and Screenshot, plus a brightness slider. **Long-press** a button to open that feature's settings (WiFi, Theme, Rotation, Neopixel, Bluetooth, Gallery); long-pressing the **Lock** button opens the Power menu. |
| **Notification Shade** | Notification history grouped per app (a count chip on the right opens/closes a group). Swipe a row left/right to dismiss; **long-press** a row for quick actions (Open / Delete / Cancel). `BETA` |
| **Dynamic Island** | A pill-shaped element at the top of the screen that changes with events. |
| **App Switcher** | Switch between apps. Each card shows a **preview** of the app's last screen (kept in PSRAM, not on the SD card); swipe a card up to close it; a **Close all** chip sits at the top right. `BETA` |
| **Always-On Display** | Low-power display after a few seconds idle (8 s on the lock screen, 15 s on Home). The screen fades to black while the backlight steps down, then shows a dim clock, date, notification count and Wi-Fi IP; touching it fades the screen back in. If PSRAM is low the switch is instant. |
| **On-screen keyboard** | Available in every app. Shift once = one capital letter, shift twice quickly = caps lock (the keys `; , . ?` stay the same); in Notepad and Chat the first letter of a sentence is capitalised automatically and a double space types `. `; holding Backspace repeats. If your thumb consistently lands on the row above or below the key you aim at, use *Settings → Sensor & Screen → Tap offset* (`-` / `+`, up to 8 px, saved across restarts) to shift the keyboard's touch point; a positive value moves it down. |
| **8 themes** | Aurora, Dark, AMOLED, Light, Pastel, iOS, Full Dark and **Nyx** (violet-tinted black with violet and teal accents; the default theme, changeable in Settings). |
| **Power menu** | Shut down completely (deep sleep; wake with the BOOT or RST button) and Restart. Opened by long-pressing the Lock button, or via Settings → About → Power. |

**Design decisions.** Vibration is deliberately minimal: only for important events, not on every tap. The frosted-glass effect is a lightweight imitation (it samples colors from already-drawn pixels and blends them with the theme tint), not a real blur, to keep CPU load low.

## Home pages and hardware scroll (ST7796)

`OK` for Home on the 3.5″ ST7796 build (and the Aether Board, which is also an ST7796 panel); `BETA` for the other screens below. The ILI9341 build has none of this and keeps the normal scrolling.

The ST7796 panel can scroll its own memory, so a scroll only sends the few rows that newly appear instead of redrawing the whole frame over SPI. NyxOS uses this in three places:

- **Portrait Home:** the app grid scrolls with the panel's hardware vertical scroll.
- **Landscape Home:** Home is split into **pages** you swipe left and right. With widgets enabled, the first page holds only the widgets; the next pages hold the apps (the first app page also shows the title and category chips). Rows are never cut in half. The page follows your finger, with a rubber-band effect at the ends, and snaps to the nearest page when you let go. A vertical swipe also flips the page, as the older Home did. Page dots show where you are. The pages exist because the panel can only scroll along its long (480-pixel) axis, which is the horizontal axis in landscape; vertical scrolling in landscape cannot use hardware scroll.
- **Other portrait screens** (`BETA`): Settings, Notepad, AI Chat, Files, Trivia, the NASA screens (APOD, NeoWs, EPIC, image library), Home editor, Remote Control, Chat, Books, Backup & Restore, About, and the Gallery album and grid (not the photo viewer). These have no cache; the firmware compares the rows of each new frame with what the panel shows, detects a vertical shift, scrolls the panel and rewrites only the rows that differ. To make the rows match after a shift, these screens use a **flat background** while this mode is possible, so the aurora glow is not drawn there (set Scroll Hardware to Off to get the glow back).

**Setting.** *Settings → Performance → Scroll Hardware* has three modes: Off (the old cache-based scrolling), Auto (the default) and Auto + Flip (reverses the scroll direction for panels that mount the other way round). Portrait and landscape keep their own mode.

**When it falls back.** Hardware scroll steps aside, and the screen is drawn and pushed in full, while someone is viewing Remote Control (a browser that polled the screen in the last 4 seconds; the Remote Control access point being on is not enough), in game mode, when PSRAM is low, while an overlay is open (Control Center, Notification Shade, App Switcher, Dynamic Island menu, power menu, call screen, lock screen, AOD, search, keyboard, jiggle/edit mode and similar) and, on portrait Home, while a wallpaper is set. The pages and the Home cache are prepared once Home has been still for about 250 ms, so swiping right after opening Home or coming back from an app can feel less smooth for the first second. Screens in the generic mode enter hardware scroll when a vertical shift is detected between two frames, and leave it again if most rows keep changing from frame to frame (an animation rather than a scroll). The reason for a fallback is printed to the serial log: `[hws] fallback jalur cache: ...` in portrait and `[hp] fallback: ...` for landscape pages; entering and leaving are logged as `[hws] masuk ...` and `[hws] keluar ...`.

**Weather icon.** The animated weather icon on Home (about 6 frames per second) runs only while Scroll Hardware is Off, and it pauses while a finger is on the screen or a scroll is still gliding. With hardware scroll on, the icon stays still: a redraw every 160 ms would rebuild the Home cache and keep the screen from entering hardware scroll.

**Row-diff push.** On ST7796 and Aether builds `push()` sends only the band of rows that changed since the last frame (`RP_PUSH_DIFF`, on by default; `BETA`). If the screen shows leftovers or corruption, build with `-DRP_PUSH_DIFF=0` or run the workflow with `push_diff=0` (see [Building](../building.md)). The ILI9341 build always pushes full frames.

## The "Nyx Modern" look

- **Squircle icons** (rounded squares) with soft shadows and edge highlights; neon icon colors are softened automatically.
- **Aurora background**: two soft radial glows in the theme's accent color on pages such as Settings (replaced by a flat background on screens that use hardware scroll in portrait, see above).
- **Settings** with colored icon chips on each row (the pressed row is highlighted, the list glides after a flick, and the WI-FI section shows the connected network and signal), **bento-style Home widgets** (clock, battery, music, weather) and a **flat keyboard** with a press effect.
- **App transitions**: a Home icon grows into the app screen when opened and shrinks back to its icon when closed. Toasts and the Dynamic Island appear with smoother animations.
- **Home widget animations**: clock digits change smoothly, the battery ring fills when Home opens, and the weather icon moves gently (only while Scroll Hardware is Off, see above).

## Charging animation `OK`

When a charger is plugged in, the lock screen shows a small black hole with an accretion ring that pulses for about 4.6 seconds. The ring's arc length follows the battery percentage, and the text changes to "Fully charged" at 100%. Touching the screen stops it. The animation appears only if **all** of the following are true:

- The phone is on the lock screen (the AOD is woken automatically).
- About 8 seconds have passed since boot.
- The previous state was "not charging" for at least 1.5 seconds, and then the charger was plugged in (a charger already connected at boot does not trigger the animation until it is unplugged first).
- The INA219 is readable and the charge current exceeds 20 mA (the current direction can be reversed in the Battery tab if the status never changes to "Charging").

## New system gestures `BETA`

| Gesture | How it works |
|---|---|
| **Pull-to-refresh** | Pull down from the top strip of the screen (left of the search button) in apps that have a reload action: Update, NEO Asteroid, EPIC Earth and NASA Gallery. An indicator circle follows your finger and fills; release after it is full to reload. |
| **Left-edge swipe-back** | Touch the left edge of the screen and drag right; a "<" bubble follows your finger, and releasing after a long enough drag goes back. Active only in non-game, non-editor apps so it does not interfere with D-pads, the drawing canvas or typing. |
| **Finger-following panels** | The Control Center and Notification Shade grow with your finger as you pull; release to open (with a small bounce) or close. |

## Notifications and App Switcher (v133) `BETA`

| Feature | How it works |
|---|---|
| **Per-app grouping** | Notifications from the same app are merged into one row (the newest) with a count chip. Tap the chip to open/close the group; rows below slide smoothly. |
| **Swipe to dismiss** | Swipe a row left/right past one third of the screen width (or flick quickly). On a parent row the whole group is removed; on a child row only that entry. A light vibration fires once when crossing the threshold. |
| **Quick actions** | Long-press a row (~0.45 s, finger still): **Open** (if the notification has a linked app), **Delete**, **Cancel**. Tap outside the row to cancel. |
| **Scrolling and feedback** | The list glides after a flick; touching it stops the glide without a tap. The pressed row is highlighted. Tap the title to jump to the top. Notifications under one minute old have an accent dot, and age labels refresh every 15 s. A notification without a linked app does not close the shade; for such a group, tap the row to open or close it. |
| **Switcher card previews** | A 64×48 thumbnail of the app's last screen, captured from the canvas just before leaving the app. At most 8 thumbnails in PSRAM; they disappear on reboot or when the card is closed. Disable with `-DSW_THUMB_ENABLE=0` if the capture seems to hold up transitions. |
| **Close all** | A chip at the top right of the Switcher; equivalent to swiping up each card (the app's RAM cache is released too). |

Note: the Notification Shade panel now adapts its height when its content changes, and the scroll limit is computed from the real panel height. Previously, in landscape, the bottom two rows could not be reached. The notification age label ("N hours ago") was also fixed; the old format was invalid.

## Ghost-touch protection

If the touch panel sends false touches, the system rejects points in dead boxes, outside the screen, or with low pressure, and then holds all input until the screen is truly quiet. At boot, if ghost touches are detected, the calibration wizard is skipped and default calibration is used without being saved. The thresholds and dead boxes are set by the `TOUCH_*` constants in `phone.ino`; real finger touches may also be rejected while ghost touches are continuously active.

## Personalisation

- **Wallpaper.** Put `wallpaper.jpg` on the SD card, or upload it through the Web File Manager. It applies to Home and the Lock Screen and takes effect immediately without a restart.
- **Edit Home.** Reorder icons and hide apps from the grid. A *Reset* button with confirmation is provided.
- **Profile.** An avatar and owner-name card at the top of Settings; the photo is picked from the SD card.
- **About Phone.** Device and system information. A scrolling page with an identity card (logo or owner photo, name, NyxOS version, board), three quick tiles (battery, free RAM, chip temperature) and sections for Devices, Software, Processor, Memory, Power, Network and System. Firmware, RAM, PSRAM, SD card and battery show a usage bar (orange from 75 %, red from 90 %; the battery bar turns orange at 30 % and red at 15 %). A value that does not fit next to its label drops to a second line. Drag to scroll (it glides after you lift your finger); tap the title to jump to the top.
- **Backup & Restore.** Save and restore all settings to the SD card, opened from Settings → About. See [System tools](./system-tools.md#settings-backup-and-restore-beta).

## Emoji `BETA`

Emoji appear throughout NyxOS text (keyboard, Notepad, Chat, AI Chat/Gemini and more) as colored 10, 16 or 24 px sprites, following the line height. LovyanGFX's built-in fonts only accept 16-bit character codes, so emoji are stored in the firmware as private-use code points (PUA, `U+E000` + sprite number) and every `setFont()` is wrapped by `emjF()`. The boundary between the firmware and the outside world is guarded by `emjNormalize()` (when reading) and `emjToWire()` (when writing or sending), so `.txt` files on the SD card and Chat messages remain standard UTF-8 that can be opened on an ordinary phone or PC.

| Set | Description |
|---|---|
| **Built-in** | 54 hand-drawn sprites (`emoji_data.h`, generated by `tools/gen_emoji.py`). Always present as a fallback. |
| **From SD** | The `/emoji.bin` file in the SD card root, generated by `tools/make_emoji_bin.py` from Noto Emoji or Twemoji PNGs. At boot only the code-point index is loaded into PSRAM; sprites are read from the SD on first use and kept in an LRU cache. It replaces the built-in set. Replacing the file only needs a restart. |

**Creating `emoji.bin`** (example in Termux; needs Pillow: `pkg install python-pillow`, or `pip install pillow`)

```bash
git clone --depth 1 https://github.com/googlefonts/noto-emoji ~/noto-emoji
python3 tools/make_emoji_bin.py ~/noto-emoji ~/storage/downloads/emoji.bin
```

Recognised sources (a `git clone` folder searched recursively, or its zip file): Noto (`png/128/emoji_u1f600.png`) and Twemoji (`assets/72x72/1f600.png`, repo `jdecked/twemoji`). Copy the result to the SD card root as `/emoji.bin`, then restart the phone. To inspect a generated file on a PC or in Termux, run `python3 tools/preview_emoji_bin.py emoji.bin check.png` (it checks the size and renders the sprites to a PNG).

**Emoji keyboard.** The emoji page opens from the smiley button next to `123`: 30 emoji per page (3 rows × 10), the `<` and `>` buttons change page, and the `space n/N` label shows the current page. The order is popular faces, hearts and common symbols first, then the rest by code point. Backspace removes one whole emoji.

**Emoji set limitations**

- **Single emoji only (one code point) from `U+231A` upward.** Multi-code-point sequences have no sprite of their own. Skin-tone modifiers, ZWJ, `VS-16` and keycaps are dropped when displayed: 👍🏽 appears as 👍, 👨‍💻 appears as two separate sprites (👨 and 💻), and `1️⃣` appears as `1`. Flags appear as a `?` box.
- An emoji that is not in the set is drawn as a `?` box (it is not lost and does not corrupt the text).
- **At most 4000 sprites** (PUA slots `E000` to `EF9F`; `EFFF` is reserved for the `?` box). This limit is practically never reached since Noto only has about 1,400 single emoji.
- **Memory and SD.** An `emoji.bin` file is `16 + N × 1868` bytes (N emoji; three sprite sizes): 1,390 emoji ≈ 2.6 MB. The file is **not** loaded in full: PSRAM only holds the code-point index (~12 bytes per emoji) plus an LRU cache of 128 sprites per size, allocated when that size is first used (10 px ≈ 25 KB, 16 px ≈ 64 KB, 24 px ≈ 144 KB). The SD file stays open while the set is active and sprites are read through a small buffer in internal RAM. Consequently, the SD card must stay inserted: if it cannot be read, emoji not yet in the cache appear as `?` boxes (three consecutive failures close the file; already-cached emoji still display). Opening the emoji page for the first time causes a brief pause because ~30 sprites are read from the SD.
- If the file is missing, the SD card cannot be read, the header or size does not match, the file size is too small, or PSRAM is insufficient: the built-in set is used without an error message.
- **Chat:** usernames are ASCII only (emoji in names are not sent). Emoji in messages are sent as standard UTF-8.

## Keyboard dictionary (word suggestions and autocorrect) `BETA`

A thin row of **3 word suggestions** for the word being typed appears above the keys, with an `ab` mode button on the left. It is active in **Notepad** and **Chat**. Password, URL, SSID, API key, name and search fields deliberately do not use it. AI apps are not included because their screen hides the input box while the keyboard is open, so corrections there would not be visible.

| Action | Result |
|---|---|
| Tap a suggestion | The word being typed is replaced by the suggestion, followed by a space. |
| Press space, `,` `.` `?` `!` | A mistyped word is auto-corrected. The word about to be corrected is shown in the accent color in the suggestion row **before** space is pressed. |
| Press **Backspace** right after a correction | Reverts to what you originally typed and the word is **learned** (not corrected again). Stored in NVS (max. 120 words). Foreign words typed twice are also learned automatically. |
| Tap the `ab` button | Cycle the mode: **suggestions + correction** (accent-colored button) → **suggestions only** (underlined) → **off** (struck through). The choice is saved. |

*Autocorrect rules (conservative, so that valid words are not damaged).* Only when the word is **not** in any dictionary, is at least 4 letters long, has the same first letter, and there is a dictionary word one **plausible typo** away: a doubled letter (`nasii`), adjacent keys on QWERTY (`makab` → `makan`), a missing letter, or two swapped letters (`thier` → `their`). Left untouched: capitalised words in mid-sentence (names), `NASA`/`iPhone`, emails/URLs/hashtags/codes with digits, words with apostrophes (`don't`), repeated laughter (`hehe`, `wkwk`) and deliberately stretched letters (`yaaa`).

*Dictionaries.* Built in: ~1,290 Indonesian words (including slang: `gak`, `udah`, `banget`, `mager`) and ~940 English words, from `tools/kamus_id.txt` and `tools/kamus_en.txt` (order = approximate frequency; `python3 tools/gen_kamus.py` turns them into `firmware/phone/kamus_data.h`). The language you type more often is prioritised in suggestions. **Optional extra dictionary:** put `/kamus.txt` in the SD card root (one word per line, most frequent first; the `word 1234` format is also accepted; 2 MB maximum). It is loaded once into PSRAM when the keyboard is first used. A word that is not in the dictionary may be corrected wrongly; one press of Backspace immediately undoes it and prevents it from recurring. A fuller dictionary from the SD card reduces this.

The logic is tested on a PC (`tests/dictionary/`) but not yet on a device.

## Factory reset `BETA`

*Settings > About > Factory reset* (tap twice within 4 seconds) erases all settings stored in NVS and the settings copies, API key, broker logins and AI memory on the SD card, then restarts. Your own files on the SD card (photos, music, books, memos) and the firmware stay. Afterwards the phone starts like a new one and asks for screen calibration.
