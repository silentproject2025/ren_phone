# Nyx Apps — native (ELF) apps from SD or internal flash `EXP`

Run your own apps **at native speed** (Xtensa machine code) without reflashing the firmware.
An app is a folder on the SD card (or on internal flash, see [Storage](#storage-sd-card-or-internal-flash)); you open it from the **Nyx Apps** icon on Home.

```
/apps/<id>/
    manifest.txt     name, icon, permissions, memory quota
    app.elf          built with ESP-IDF (idf.py elf)
    data/            created automatically — the ONLY folder the app may touch
```

## Verification status (read this first)

| Part | Status |
|---|---|
| Memory arena, quota, leak/corruption detection, pressure handling, TLS Gate (`nyxmem.h`) | **Tested on a PC**: 189 checks, fuzzing with 5 seeds, ASan + UBSan, mutation tests |
| Host: manifest, path sandbox, permissions, file handles, async HTTP (`nyxhost.h`) | **Tested on a PC**: 158 checks, 5 example apps run through `dlopen`, mutation tests |
| Firmware glue (`nyxapp_renphone.ino`), `phone.ino` patch | **Syntax-checked only** (with stubs) plus a CI build; the patch fits and is idempotent on this repo's `phone.ino`. **Never run on a phone.** |
| ELF loader on Xtensa/PSRAM, task + watchdog, HTTP worker, tilt | **Not tested on hardware.** The biggest risk areas: see "Enabling the ELF loader" and "Things to check on the phone". |
| SD / internal flash (FFat) storage choice and the FFat worker task | **Not tested on hardware.** |

PC tests: `cd tests/nyxapp && ./run.sh` (needs `g++` and `gcc`).

## Trust model (honest version)

A native app shares the address space with the firmware and uses real pointers. The ESP32-S3 has no usable per-app memory protection. Therefore:

* **Protected against:** apps that are *wrong* — hanging or looping forever (the watchdog kills the task), asking for more memory than the quota (refused, not a crash), forgetting to `free` (released and reported on exit), writing past a block (canary detected), double-free or freeing a foreign pointer (counted and ignored), opening files outside `data/` through the API (refused), using a feature without permission (refused).
* **Not protected against:** apps that are *malicious* or have wild pointers. Such an app can overwrite firmware memory or call libc/IDF functions directly (for example `fopen` to any path); the file sandbox only applies to access through `NyxApi`. **Install only apps you trust.** If you need a real sandbox, see `experimental/nyxvm/` (unfinished).

## Manifest

```
name=Hello Native      # required, <24 characters
sym=H                  # one character for the icon
color=3060d0           # RRGGBB
perms=fs,net,tilt,notify
mem_kb=256             # PSRAM arena quota, 64..6144
stack_kb=8             # app task stack, 4..32
elf=app.elf
```

A value outside its range, an unknown permission, or an `elf` value containing `/` or `..` makes the manifest rejected.

## Writing an app

```c
#include "nyx_app.h"
NYX_APP_GLOBALS
static int  app_enter(void) { return 0; }                 // once, when opened
static void app_frame(void) { g_nyx->clear(g_nyx->rgb(0,0,0));
                              g_nyx->text(8, 8, "Hello", 0xFFFF, 2); }   // every frame, MUST be fast
static void app_leave(void) {}                            // once, when closed
NYX_APP_MAIN(app_enter, app_frame, app_leave)
```

* `main()` only registers `enter/frame/leave` and returns. All the work happens in `frame()`.
* A frame longer than **250 ms** gets the task killed (watchdog). Aim for under 30 ms.
* Memory: `mem_alloc/mem_calloc/mem_realloc/mem_free` (quota-limited arena). Do not use libc `malloc`.
* Drawing: coordinates are relative to the viewport (0,0 = top-left of the app area, below the status bar). RGB565.
* Files: `fopen("notes.txt", NYX_FOPEN_WRITE)` — the path is relative to `data/`, with no `..` and no leading `/`. At most 4 handles.
* Network: `http_get` plus polling `http_state` (one request at a time, buffer up to 128 KB). HTTPS waits for the **TLS Gate** (a contiguous internal-RAM block of at least 32 KB, which triggers other apps' `onLowMem` reclaim).
* Full examples: `examples/native/{hello,bounce,memhog,notes,leaky}`. API reference: `firmware/phone/nyx_app.h`.

## Building an app (ESP-IDF)

```bash
cd examples/native/idf_template
export NYX_SDK=$PWD/../../../firmware/phone
cp ../hello/app.c main/app.c
idf.py set-target esp32s3 && idf.py elf             # -> build/nyxapp.app.elf
python3 ../../../tools/pack_app.py --id hello --elf build/nyxapp.app.elf \
        --manifest ../hello/manifest.txt --sd /media/SDCARD
```

To try the app logic on a PC without hardware: `gcc -shared -fPIC -I firmware/phone app.c -o app.so` (used by `tests/nyxapp/test_host.cpp`).

## Storage: SD card or internal flash

Nyx Apps works with or without an SD card. When the app list opens, the location of `/apps` is chosen like this:

1. the SD card, if it is readable and contains `/apps`;
2. internal flash (FFat), if it contains `/apps`;
3. if neither has `/apps`: the SD card if readable, otherwise FFat.

**Without an SD card**, upload the app through the phone's Web File Manager: choose **Flash internal (FFat)**, fill in the **Target folder** field (for example `/apps/hello`), then upload `manifest.txt` and `app.elf` one by one. Parent folders are created automatically. The same Target folder field also works for the SD card.

Notes:

* The app task has its stack in PSRAM, and flash access with a PSRAM stack crashes the chip. So on FFat, an app's file operations run in a small worker task with an internal-RAM stack. The SD path is direct.
* FFat is small (about 9 MB) and shared with the wallpaper, DOOM WADs and the crash log. Use the SD card for big apps or lots of data.
* The ELF file is loaded completely into PSRAM before the app starts, so FFat read speed only affects the start-up time.

## Installing into the firmware

This repository already includes the integration: `nyx_app.h`, `nyxmem.h`, `nyxhost.h` and `nyxapp_renphone.ino` live in `firmware/phone/`, the CI workflow copies them into the sketch, and `phone.ino` has the `SCR_NYX` screen and the `apps[]` entry (46 apps in total). `apply_nyxapp_patch.py` in the repository root is the one-shot script that made the `phone.ino` changes (run with `--check` to see whether it would still change anything; it keeps a backup as `phone.ino.bak_nyxapp`).

Without an ELF loader the build still succeeds: Nyx Apps opens and explains that the loader is missing.

## Enabling the ELF loader (not yet tried)

`espressif/elf_loader` is an **ESP-IDF component** (enabled with `CONFIG_ELF_LOADER`, and `ELF_LOADER_LOAD_PSRAM` so the code is loaded into PSRAM). The Arduino-ESP32 build uses precompiled IDF libraries, so this component has to get into them:

* through your own **Build Arduino Libs** workflow (add the component and the sdkconfig options, then use the resulting libraries), or
* through the "Arduino as an ESP-IDF component" mode.

Note from the repository history: building custom libraries has hit linker problems before, so do this on a separate branch. The glue uses the API `esp_elf_init / esp_elf_relocate(elf, data, size) / esp_elf_request / esp_elf_deinit` (2.x series); adjust it if your version differs. `esp_elf.h` is detected automatically through `__has_include`.

### Enabling via CI (added)

Run **Actions → Build Arduino Libs → Run workflow** with `elf_loader` = true (default). The workflow adds `espressif/elf_loader` **2.0.0** to the lib-builder `main/idf_component.yml`, sets `CONFIG_ELF_LOADER=y` and `CONFIG_ELF_LOADER_LOAD_PSRAM=y`, and after the build warns if `libelf_loader.a` or `esp_elf.h` is missing from the result. Then run **Build Firmware**: its "Verifikasi library kustom" step prints whether the loader is present (`NYXAPP_HAVE_ELF=1`). Pinned to 2.0.0 because the glue uses the 3-argument `esp_elf_relocate(elf, data, size)` (1.x has 2 arguments). Still untested on hardware.

## Things to check on the phone (suggested order)

1. Build without the loader → open Nyx Apps → the app list shows and the "no loader" message is correct. Repeat with the SD card removed and an app on FFat.
2. Build with the loader → `hello`: does the ELF load? **Check the entry symbol:** the glue calls `esp_elf_request(&elf, 0, NYX_ENTRY_ARGC, argv)` with `argv[0]` = the `NyxApi` pointer. If the loader uses another entry convention, only `nyxTaskFn` (the `NYXC_START` branch) needs to change.
3. `memhog` → Serial should show a peak ≈ the quota and `0 leaked blocks`. `leaky` → the toast "App damaged its own heap".
4. Watchdog: make an app with `while(1){}` in `frame()` → it must be killed in under 0.5 s without freezing the phone.
5. Files on FFat: have an app write and read back a file under `data/` — this goes through the worker task and must not reboot the phone.
6. HTTP: try `http_get` on a plain URL first, then HTTPS. The worker task uses `HTTPClient` on core 0.
7. Watch `[NyxApp]` in Serial, and `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` before and after an app.

## Deliberate limits

* One app at a time; no hibernate/resume (that needs a VM; see `experimental/nyxvm/`).
* No audio or camera syscalls (the `AUDIO` / `CAMERA` permissions do not exist yet).
* App icons do not appear in the Home grid; everything goes through the single **Nyx Apps** icon (the Home grid is static, built from `apps[]`).
* No App Switcher integration.
* ELF at most 1 MB; arena quota 64 KB – 6 MB; `NYXAPP_FRAME_MS` = 250 ms (change it in `nyxapp_renphone.ino`).
