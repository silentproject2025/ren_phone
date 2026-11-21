# Games `OK`

| Game | Description |
|---|---|
| **Inferno** | A gothic-themed FPS *raycaster*: animated enemies (melee, ranged, fast), bosses, 3 weapon tiers, limited ammo with *pickups*, joystick control plus touch *look* or tilt. |
| **NES** | An emulator (Nofrendo core) on a separate task; `.nes` ROMs go in the SD card root. If the SD card cannot be read, the ROM list comes from the **FFat** root instead (upload over the web file manager, choose *Flash internal (FFat)*) and the picker shows `[FFat]`. The ROM is read into PSRAM before the emulator starts. |
| **DOOM** | Based on doomgeneric, without sound. The `doom1.wad` file is provided by the user. |
| **Maze** | A chase through a maze with touch and tilt controls. |
| **Chess** | Online chess between phones over MQTT, or vs. AI (5 levels, including an AI that imitates the owner's style and the experimental Nyx level) `BETA`. See [Chess](./chess.md). |
| **Classics** | Snake, Flappy, 2048, TicTacToe, Breakout. |
| **PPU Demo** | A demonstration of the NyxPPU engine `EXP`. See [Experimental](./experimental.md). |

Games can also be controlled from another device with the [web gamepad](./remote-control.md#web-gamepad).

## DOOM WAD files

The WAD is provided by the user and is searched for automatically in the root with the name order `doom2.wad`, `doom1.wad`, `doom.wad`, `freedoom2.wad`, `freedoom1.wad`, `plutonia.wad`, `tnt.wad`. **The SD card is checked first**; only if the SD cannot be read or contains no WAD does the firmware use **FFat** (uploaded over the web). A WAD on the SD card is read directly from the card.

A WAD from FFat is loaded in full into PSRAM when DOOM opens. The reason: the DOOM task keeps its stack in PSRAM, and access to internal flash (FFat) briefly disables the cache, so ESP-IDF asserts `esp_task_stack_is_sane_cache_disabled()` and the device restarts. The consequences:

- A WAD on FFat must fit in PSRAM (`doom1.wad` at about 4 MB fits; a much larger WAD such as `doom2.wad` should be on the SD card). If loading fails, the screen shows the message *WAD on FFat could not be loaded into PSRAM* instead of restarting.
- While the WAD comes from FFat, saving games and DOOM configuration are disabled (so flash is not written from a task with a PSRAM stack).
