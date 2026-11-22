# Games `OK`

| Game | Description |
|---|---|
| **Inferno** | A gothic-themed FPS *raycaster*: animated enemies (melee, ranged, fast), bosses, 3 weapon tiers, limited ammo with *pickups*, joystick control plus touch *look* or tilt. |
| **NES** | An emulator (Nofrendo core) on a separate task; `.nes` ROMs go in the SD card root. If the SD card cannot be read, the ROM list comes from the **FFat** root instead (upload over the web file manager, choose *Flash internal (FFat)*) and the picker shows `[FFat]`. The ROM is read into PSRAM before the emulator starts. |
| **DOOM** | Based on doomgeneric, without sound (the build has `DOOM_NO_AUDIO` set; the game does not use the optional speaker module either, see [Known limitations](../limitations.md)). The `doom1.wad` file is provided by the user. |
| **Maze** | A chase through a maze with touch and tilt controls. |
| **Chess** | Online chess between phones over MQTT, or vs. AI (5 levels, including an AI that imitates the owner's style and the experimental Nyx level) `BETA`. See [Chess](./chess.md). |
| **Classics** | Snake, Flappy, 2048, TicTacToe, Breakout. |
| **PPU Demo** | A demonstration of the NyxPPU engine `EXP`. See [Experimental](./experimental.md). |

Games can also be controlled from another device with the [web gamepad](./remote-control.md#web-gamepad).

## DOOM WAD files

The WAD is provided by the user and is searched for automatically in the root with the name order `doom2.wad`, `doom1.wad`, `doom.wad`, `freedoom2.wad`, `freedoom1.wad`, `plutonia.wad`, `tnt.wad`. **The SD card is checked first**; only if the SD cannot be read or contains no WAD does the firmware use **FFat** (uploaded over the web). A WAD on the SD card is read directly from the card.

A WAD from FFat is **streamed**, not loaded into PSRAM. The DOOM task keeps its stack in PSRAM, and access to internal flash (FFat) briefly disables the cache, so ESP-IDF would assert `esp_task_stack_is_sane_cache_disabled()` and restart the device. To avoid that, a small helper task named `wadio` (internal-RAM stack of 6 KB, plus a 4 KB read buffer) does every FFat read; the DOOM task only sends it a request and waits. Consequences:

- The WAD no longer has to fit in PSRAM, so PSRAM stays free for DOOM's zone memory. With the whole WAD preloaded, `doom1.wad` (about 4 MB) left only about 2 MB, which gave a 1 MiB zone and `Z_Malloc: failed on allocation` from E1M3 on. When streaming, the zone is 5 MiB or 6 MiB. The zone size is printed at start (`zone memory: ... 500000 allocated for zone` means 5 MiB). A 6 MiB zone needs a contiguous PSRAM block of about 6.25 MB; if the largest block is smaller, the engine picks 5 MiB or 4 MiB.
- If the helper task cannot be created (not enough internal RAM), the firmware falls back to loading the WAD into PSRAM as before. In that case the WAD must fit in PSRAM, and if loading fails the screen shows *WAD on FFat could not be loaded into PSRAM* instead of restarting.
- Reading from FFat is slower than from PSRAM, so levels may take slightly longer to load. Frame rate during play is the same.
- While the WAD comes from FFat, saving games and DOOM configuration are disabled (so flash is not written from a task with a PSRAM stack).
