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
| Nyx Store: catalogue parser, validation, install-state logic (`nyxstore_core.h`) | **Tested on a PC**: 85 checks, fuzzing, ASan + UBSan, mutation checks |
| Nyx Store: UI, download task, SHA-256, write/rename/remove (`nyxstore_renphone.ino`) | **Syntax-checked only** (with stubs). **Never run on a phone, never run against the real GitHub release.** |

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

### Prebuilt example apps (CI + web flasher)

The **Build Nyx Apps (ELF)** workflow (`.github/workflows/build-nyxapps.yml`) compiles every `examples/native/<id>/` (needs `app.c` + `manifest.txt`) with the `espressif/idf` Docker image using `examples/native/idf_template`, checks the ELF (magic, Xtensa, ≤ 1 MB) and, on `main`/`master`, publishes one `.zip` per app to the rolling pre-release `nyxapps`. The **Web Flasher** page then shows a **Nyx Apps** tab with download buttons (`.zip`, `app.elf`, `manifest.txt`). Each zip contains `apps/<id>/{app.elf,manifest.txt,data/}`: extract it to the SD root, or upload the two files through the Web File Manager (see Storage below).

* New example app = new folder under `examples/native/`; CI picks it up automatically.
* The ESP-IDF image is an input of the manual run (default `espressif/idf:v5.5.5`); keep it equal to the IDF version of the core libraries.
* Packaging logic: `tools/nyxapps_release.py` (uses `tools/pack_app.py` for validation).
* **Not tested on hardware**, and the `idf.py elf` build itself has only been written, not run, in CI yet.

## Nyx Store (install apps from the phone)

Open **Nyx Apps** and tap **Store** (top right). The Store downloads the catalogue `nyxapps.json` from the rolling pre-release `nyxapps` of this repository (the same release the web flasher reads) and lists every app in it. Tap an app for its size, arena memory and permissions, then **Install**, **Update** or **Remove**. It needs WiFi; nothing else changes about how apps run.

How an install works:

1. Download `<id>.manifest.txt`, parse it with the same parser the launcher uses, and check that its `perms=` equal the permissions the catalogue advertised (a mismatch aborts the install).
2. Download `<id>.elf` (exact size from the catalogue, at most 1 MB) into a PSRAM buffer on a network task, then compare its **SHA-256** with the catalogue.
3. Write it to `/apps/<id>/app.elf.tmp` in small per-frame slices (so the progress bar keeps moving on slow flash), then rename it to `app.elf`. `manifest.txt` is written **last**, so a half-installed app never shows up in the Nyx Apps list. A small `store.txt` (`sha256=...`) records what was installed; a differing hash in a later catalogue shows **Update**. Apps copied by hand (no `store.txt`) show *Installed manually* and can be reinstalled from the Store.

Where it installs follows the rule in [Storage](#storage-sd-card-or-internal-flash) (SD card if it holds `/apps`, else internal flash, ...). Removing an app deletes `/apps/<id>/` including its `data/` folder, after a confirmation. Back cancels a running download.

Publishing an app to the Store = adding `examples/native/<id>/{app.c,manifest.txt}`: CI builds it and `tools/nyxapps_release.py` adds it to the catalogue. Catalogue entries with an unsafe `id` or file name (`/`, `..`), a malformed SHA-256 or a size outside 64 B - 1 MB are ignored.

**Security, honestly.** The connection uses `setInsecure()` (like the OTA updater), so the SHA-256 protects against corrupted or truncated downloads, **not** against someone who controls your network and can rewrite both the catalogue and the file. Native apps are not sandboxed (see the trust model above). Install only apps from a source you trust.

Tests: `tests/nyxapp/test_store.cpp` (catalogue parser, validation, fuzzing, install-state logic; run by `tests/nyxapp/run.sh`). The UI and the download/install glue (`nyxstore_renphone.ino`) are **syntax-checked only and have never run on a phone**.

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

This repository already includes the integration: `nyx_app.h`, `nyxmem.h`, `nyxhost.h` and `nyxapp_renphone.ino` live in `firmware/phone/`, the CI workflow copies them into the sketch, and `phone.ino` has the `SCR_NYX` screen and the `apps[]` entry (46 apps in total). `tools/patches/apply_nyxapp_patch.py` is the one-shot script that made the `phone.ino` changes (run with `--check` to see whether it would still change anything; it keeps a backup as `phone.ino.bak_nyxapp`).

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
