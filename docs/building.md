# Installing and building

There are three installation paths. Pick the one that matches your needs.

| Path | Best for | Software needed |
|---|---|---|
| **A. Flash a CI-built firmware** | Users who just want to try it | Web flasher or `esptool` |
| **B. Fork and build on GitHub Actions** | Developers who want to modify the code without a local toolchain | A GitHub account |
| **C. Manual build (Arduino CLI / IDE)** | Developers who need full control | Arduino CLI or Arduino IDE |

- [Option A — CI-built firmware](#option-a--ci-built-firmware)
- [What the automatic build does and does not do](#what-the-automatic-build-does-and-does-not-do)
- [Option B — build on GitHub Actions](#option-b--build-on-github-actions)
- [Option C — manual build for the ESP32-S3](#option-c--manual-build-for-the-esp32-s3)
- [Building the ESP32-CAM coprocessor](#building-the-esp32-cam-coprocessor)
- [Custom core libraries (mbedTLS in PSRAM)](#custom-core-libraries-mbedtls-in-psram)
- [Working on the code](#working-on-the-code)
- [Over-the-air updates](#over-the-air-updates)

## Option A — CI-built firmware

Every *push* triggers the **Build Firmware Binaries** workflow, which publishes a GitHub Release.

| Release asset | Purpose |
|---|---|
| `ren_phone_s3_merged.bin` | Complete ESP32-S3 image (bootloader, partitions, app) for a first flash. |
| `firmware.bin` | App-only image, used by OTA updates. |
| `info.txt` | OTA metadata (branch, build, code base, size, MD5). Published on the `channel-<branch>` Release together with `firmware.bin`. |
| `changelog.txt` | English release notes shown on the phone before an OTA update is installed. Built from [`CHANGELOG.md`](../CHANGELOG.md); optional. |
| `cam_merged.bin` | Complete ESP32-CAM image, from the **Build CAM BT Co-Processor** workflow. |

### Writing the changelog

`CHANGELOG.md` in the repository root holds one section per release, newest first. The heading must contain the number from [`VERSION`](../VERSION):

```markdown
## 1.0.1
- New: something added
- Fixed: something repaired
```

During the build, the workflow copies the section that matches `VERSION` (or the first section if none matches) into `changelog.txt`, keeps ASCII characters only and trims it to about 1,400 bytes. It is uploaded next to `info.txt`. If the file is missing or empty, updates still work, the phone just shows no release notes. Write plain English and avoid emoji or accented letters: the phone font cannot draw them.

Tag naming: `build-<number>` for the S3, `cam-<number>` for the CAM, and `channel-<branch>` for per-branch OTA channels. The `main`, `master` and `remote-control` branches are published as regular releases; every other branch is always a *prerelease*. The 3.5″ ST7796 build and the Aether Board build have their own tags and channels, see [Hardware → ST7796 variant](./hardware.md#35-st7796-display-variant) and [Aether Board variant](./hardware.md#aether-board-variant).

When run manually (*Run workflow*), the workflow offers these inputs:

| Input | Values | Purpose |
|---|---|---|
| `use_custom_libs` | `true` (default) / `false` | Use or skip the custom core libraries (A/B test against the stock libraries). |
| `loop_core` | `default`, `0`, `1` | Core on which `loop()`/UI runs (A/B test of game performance). |
| `simd_pie` | `on` (default) / `off` | `off` = scalar path (`-DCHS_NN_NO_PIE`) for A/B tests. SIMD PIE (ESP32-S3 vector instructions) only accelerates neural-network evaluation, so it only matters when `nyx_nn=on`. |
| `nyx_nn` | `off` (default) / `on` | `on` = `-DCHS_NN_NYX=1`; the Nyx chess level uses neural-network evaluation. Experimental; when the NN is active, the second core (Lazy SMP) is automatically unused. |
| `push_diff` | `default` / `0` / `1` | Row-diff screen push (`RP_PUSH_DIFF`) for the `st7796` and `aether` builds; `ili9341` ignores it. `default` uses the profile setting in `display_conf.h` (on); `0` forces it off (try this if the screen shows leftovers or corruption); `1` forces it on. |
| `chess_opt` | `O3` (default) / `O2` | Optimisation level of the chess engine; `O2` gives smaller code (for A/B tests or when the 3 MB app partition is full). |

Builds triggered by a push use the default values.

After a Release is created, the **Web Flasher (GitHub Pages)** workflow is called automatically and assembles a browser-based flasher page. The page distinguishes **stable** firmware (from `main`/`master`) from **test** firmware (from other branches) and provides `.bin` download buttons, SHA-256 checksums and a link to the repository.

### Nyx Apps (ELF) build

The **Build Nyx Apps (ELF)** workflow (`build-nyxapps.yml`) compiles every `examples/native/<id>/` that has an `app.c` and a `manifest.txt`, using the `espressif/idf` Docker image (default `espressif/idf:v5.5.5`, an input of the manual run; keep it equal to the IDF version of the core libraries) and `examples/native/idf_template`. It checks each ELF, and on `main`/`master` (or a manual run with `publish` on) it packs `<id>.zip` plus `nyxapps.json` with `tools/nyxapps_release.py` into the rolling pre-release `nyxapps`, then re-runs the web flasher, whose **Nyx Apps** tab links the downloads. A new example app is just a new folder. On a device, the loader and the `hello` example are confirmed; see [Nyx Apps](./features/native-apps.md) for what is still unconfirmed.

You can also flash with `esptool`:

```bash
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 ren_phone_s3_merged.bin   # ESP32-S3
esptool.py --chip esp32   --baud 921600 write_flash 0x0 cam_merged.bin            # ESP32-CAM
```

On first boot, follow the on-screen **touch calibration** procedure.

### What the automatic build does and does not do

The build that starts after every push is a **firmware** build. It uses the custom core libraries (see [below](#custom-core-libraries-mbedtls-in-psram)) but never builds them, and it ignores the manual inputs.

| Question | Answer |
|---|---|
| Does a push build use the custom core libraries? | Yes, **if the package already exists**: it downloads `esp32s3-libs.tar.gz` from the `arduino-libs-custom` pre-release and installs it over the stock libraries. |
| Does a push build create them? | No. Only the manual **Build Arduino Libs** workflow does (slow, hours). Run it once per core version, or when you change one of its options (including `elf_loader`). |
| What if the package is missing, for another core version, or lacks the expected options? | The build stays green and silently uses the stock libraries. Nyx Apps then reports that the loader is missing. |
| After re-running Build Arduino Libs, do old firmware files change? | No. Start **Build Firmware Binaries** again so a new firmware is compiled against the new package. |
| Do the manual inputs (`use_custom_libs`, `loop_core`, `simd_pie`, `nyx_nn`, `push_diff`, `chess_opt`) apply to a push? | No. Only *Run workflow* offers them; a push uses the defaults. The Release notes only name the commit and branch, not the inputs. |
| Is the Nyx Apps (ELF) workflow part of every push? | No. It runs when `examples/native/**`, `firmware/phone/nyx_app.h`, the packaging tools or its own workflow file change. |

Checklist for a firmware that must run Nyx Apps:

1. Run **Build Arduino Libs** once with `elf_loader` = true (the default).
2. Push, or run **Build Firmware Binaries** by hand.
3. In the log, step *Verifikasi library kustom terpasang* must print `elf_loader ADA di library kustom -> NYXAPP_HAVE_ELF=1`. If it prints the warning instead, the loader is not in this firmware.
4. Flash, open **Nyx Apps** and start an app (`hello` is the smallest).

## Option B — build on GitHub Actions

1. *Fork* this repository.
2. Enable GitHub Actions on the fork.
3. Run the **Build Firmware Binaries** workflow (automatic on every *push*, or manually through *Run workflow*).
4. Download the `.bin` files from the *artifacts* or from the Release, then flash them.

> For OTA updates to download firmware without a login, the repository must be **public**.

## Option C — manual build for the ESP32-S3

**Software prerequisites**

- Board package **esp32 by Espressif 3.x**
- Libraries: `LovyanGFX`, `JPEGDEC`, `PubSubClient`, `WebSockets`

> The CI workflow currently also installs `lvgl` and `ESP32-audioI2S`, but the source code in this repository no longer includes either (the old internal MP3 player was removed). They are not needed for a manual build.

**Steps**

1. Use `firmware/phone/` as the sketch: all `*.ino` files and headers in that folder belong to one sketch, with `phone.ino` as the main file. (The folder name matches `phone.ino`, so Arduino IDE opens it directly.)
2. Copy `third_party/nofrendo_core/` and `third_party/esp_doom_core/` into the sketch's `src/` folder, and `firmware/phone/extras/chess_smp.cpp` into `src/` as well, together with a generated `src/chess_engine.inc` (the ENGINE and AI blocks extracted from `chess_renphone.ino`, as `tests/chess/extract.sh` does for the tests). The small patches this needs (file exclusions, `-O2` pragmas, symbol renames) are defined in the **Compile Sketch** step of `.github/workflows/build-firmware.yml`; that step is the authoritative recipe for assembling the sketch.
3. Add the following mbedTLS *build flags*:

   ```
   -DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1
   -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
   ```

4. Select the board and options:

   ```
   FQBN   : esp32:esp32:esp32s3
   Options: FlashSize=16M, PSRAM=opi, PartitionScheme=app3M_fat9M_16MB
   ```

5. Compile and upload, then follow the touch calibration on first boot.

> **CI note.** `tools/ci/ci_hoist_types.py` moves the `#include` blocks, structs and enums at the top of `phone.ino` into a temporary header, on the copy inside `build_sketch/`. This step only applies in CI and does not modify `phone.ino` in the repository. Its purpose is to ensure the `arduino-cli` auto-generated prototypes are always inserted after all types are known.

## Building the ESP32-CAM coprocessor

| Parameter | Value |
|---|---|
| Source folder | `firmware/cam_bt_coprocessor/` |
| Board | AI Thinker ESP32-CAM (`esp32:esp32:esp32cam`) |
| Partition scheme | Huge APP |
| PSRAM | Enabled |
| Libraries | `audio-tools`, `arduino-libhelix`, `ESP32-A2DP` |

Make sure `rplink.h` is **identical** on the S3 side and the CAM side. The Camera feature requires **both firmwares** (S3 and CAM) to be reflashed; without the new CAM firmware, the Camera app only shows a message that the CAM does not support the camera, and other features are unaffected.

## Custom core libraries (mbedTLS in PSRAM)

This component is **optional**, but it is used by the CI builds.

**Background.** The stock core libraries place mbedTLS buffers in internal RAM. The custom libraries move them to PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`), which leaves more internal RAM while an HTTPS connection is active.

**Two workflows work in sequence:**

1. **Build Arduino Libs (mbedTLS in PSRAM)** — run manually, **once per core version**. It uses `esp32-arduino-lib-builder` and uploads the result to the `arduino-libs-custom` *prerelease* (`esp32s3-libs.tar.gz` and `core-version.txt`). The process is slow and can take hours.
2. **Build firmware** — on every build, the workflow downloads that package, overwrites `~/.arduino15/packages/esp32/tools/esp32s3-libs/<core version>/` with it, and then compiles the sketch.

**Installed sdkconfig options** (re-run *Build Arduino Libs* for changes to take effect):

| Option | Purpose |
|---|---|
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` | mbedTLS buffers in PSRAM (**required**). |
| `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, `..._DYNAMIC_FREE_CONFIG_DATA=y` | TLS buffers are allocated per connection and configuration data is released after the handshake. |
| `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` | Prerequisite for task stacks in PSRAM (`xTaskCreateStaticPinnedToCore`). |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` | WiFi/lwIP buffers are preferably placed in PSRAM. WiFi throughput may drop slightly; disable with the `wifi_lwip_psram=false` input. |
| `CONFIG_ELF_LOADER=y`, `CONFIG_ELF_LOADER_LOAD_PSRAM=y` (input `elf_loader`, on by default) | Adds `espressif/elf_loader` 2.0.0 so [Nyx Apps](./features/native-apps.md) can load `app.elf` into PSRAM. Without it the firmware still builds and Nyx Apps says the loader is missing. |

The `-DCONFIG_MBEDTLS_*` flags that used to be in `build-firmware.yml` were removed because they do not affect an already-built `libmbedtls.a`. Extra options only produce a *warning* if they fail to apply; only `EXTERNAL_MEM_ALLOC` fails the build.

The emulator and DOOM cores (`third_party/nofrendo_core/`, `third_party/esp_doom_core/`) are compiled with `-O2` (per-file pragma in CI); the rest of the sketch stays at `-Os` so the 3 MB app partition does not fill up.

**Safety mechanism.** The package is only installed if its Release exists, the core version matches `core-version.txt`, and the `sdkconfig` inside the package contains the required options. If any condition fails, the build **still runs with the stock libraries** and only emits a *notice* or *warning*. A **green build therefore does not necessarily use the custom libraries**. Check these log steps:

| Log step | Expected result |
|---|---|
| *Pasang library core kustom* ("install custom core library") | `Library kustom (mbedTLS di PSRAM) terpasang utk core X.Y.Z` appears. |
| *Verifikasi library kustom terpasang* ("verify custom library installed") | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1` in `sdkconfig.h`, and the installed `libmbedtls.a` hash **differs** from the original. For Nyx Apps it also prints `elf_loader ADA di library kustom` (`esp_elf.h` and `libelf_loader.a` found). |

Re-run the libs workflow only when the core version changes or the `sdkconfig` options change. **Build Firmware Binaries** also offers the manual `use_custom_libs` input (`false` for an A/B comparison with the stock libraries).

**Early test results.** Measured with HWmonitor while the Astronomy app loads data over HTTPS, compared with the firmware before the custom libraries:

| Metric | Before | After |
|---|---|---|
| Internal RAM in use | 167 KB | 127 KB |
| Largest free block | 21 KB | 30 KB |

These figures come from a single snapshot per firmware on one unit. Treat them as indicative, not exact.

## Working on the code

**Source layout.** `firmware/phone/` holds one `.ino` per feature (`*_renphone.ino`) plus shared headers. Arduino concatenates all `.ino` files in a sketch folder, so there is no `#include` between them.

**Adding a new source file.** A new `.ino` file **must** be registered in the `cp … build_sketch/` list of `build-firmware.yml`; so must any header `.h` that the sketch `#include`s (for example `gutdl_core.h`). If it is missed, the build fails at the *link* stage with `undefined reference to ...` for functions defined in that file (this happened with `ptr_renphone.ino`). The `arduino-cli` version, the ESP32 core and the libraries in CI are **not pinned**, so upstream updates can break the build without any change in this repository.

**Version.** The project version lives in [`VERSION`](../VERSION) and is validated by CI (`dev` or `N.N.N[-suffix]`).

**Legacy patch scripts.** Earlier features were installed through idempotent `apply_*.py` scripts run from the repository root. They have already been applied to the sources and are archived in [`tools/legacy-patches/`](../tools/legacy-patches/README.md). They were written for the old flat layout and are kept for reference.

**Host-side tests.** Logic that does not need the hardware (backup format and crypto, chess engine, ebook engine, Gutenberg logic, camera protocol, keyboard dictionary) is tested on a PC; see [`tests/README.md`](../tests/README.md).

## Over-the-air updates

Firmware can be updated from GitHub without a cable, with several layers of protection. See [System tools → OTA updates](./features/system-tools.md#ota-updates-per-channel) for the update flow, safeguards and rollback behaviour.
