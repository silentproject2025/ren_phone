# Third-party code

Vendored code that is copied into the firmware's `src/` folder by CI. Each component keeps its own upstream license. No license files are included in these folders (only `nofrendo_core/AUTHORS`), so check the upstream projects when redistributing.

| Folder | Component | Used by |
|---|---|---|
| `nofrendo_core/` | Nofrendo NES emulator core | NES app |
| `esp_doom_core/` | ESP-DOOM / doomgeneric port | DOOM app |

Do not edit these files for project-specific behaviour unless necessary. The small patches applied during the build (file exclusions, `-O2` pragmas, symbol renames) are defined in the **Compile Sketch** step of `.github/workflows/build-firmware.yml`.
