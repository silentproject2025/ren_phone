# Patch scripts

Small, **idempotent** Python scripts that wire a feature into `firmware/phone/phone.ino` (or a sibling file). All of them are already applied to the sources in this repository; they stay here because CI or the tests still use them to prove that the anchors in `phone.ino` match (`--check`), and as a record of how each feature was hooked in.

**Run them from the repository root** (they open `firmware/phone/...` relative to the current directory):

```bash
python3 tools/patches/apply_nyxapp_patch.py --check   # only check that every anchor still matches
python3 tools/patches/apply_nyxapp_patch.py           # apply (keeps a .bak_* backup; safe to re-run)
```

| Script | Marker | Used by |
|---|---|---|
| `apply_nyxapp_patch.py` | `NYXAPP_V1` | `nyxapp-tests.yml` (anchor check) |
| `apply_sysmem_patch.py` | `SYSMEM_V1` | `tests/sysmem/run.sh`, `sysmem-tests.yml` |
| `apply_hint_bri_patch.py` | `HP_HINT_V1`, `BRI_NVS_V1` | History. The Home swipe hint it installed has been removed from `pager_renphone.ino`; the `HP_HINT_V1` comment is kept on purpose so this script does not re-install it |
| `apply_home_cover_boot_patch.py` | `HP_COVER_BOOT_V1` | History |
| `apply_leave_rewrite_patch.py` | `HWS_LEAVE_REWRITE_V1` | History |
| `apply_no_vertical_flip_patch.py` | `HP_NO_VFLIP_V1` | History |
| `apply_hws_flatbg.py` | `HWS_FLATBG_V1` | History (flat backdrop on screens that use generic hardware scroll) |
| `apply_pager_di_fast.py` | `HP_DI_FAST_V1`, `HW_RC_VIEWER_V1` | History (Dynamic Island no longer blocks the hardware-scroll warm-up; only a Remote Control viewer blocks it) |
| `apply_wx_anim_hw.py` | `WX_ANIM_HW_V1` | History (weather icon animation off while hardware scroll is on) |

| `apply_wifisense_patch.py` | `WSENSE_V1` | History (registers the WiFi Sensing app in `phone.ino`) |
| `apply_wsense_v2.py` | `WSENSE_V2` | History (WiFi Sensing packet filter, adaptive baseline, saved calibration; also patches its host test) |
| `apply_wsense_csi_flag.py` | `WSENSE_FORCE` | History (adds `-DWSENSE_FORCE=1` to `build-firmware.yml`) |
| `apply_wsense_csi_input.py` | `WSENSE_CSI_INPUT` | History (adds the `wifi_csi` input to `build-arduino-libs.yml`) |
| `apply_spk_ffat_patch.py` | `SPK_FFAT_V1` | History (Music from FFat, `[SPK-RAM]` meter, calls through the speaker without TWS) |
| `apply_store_ui_patch.py` | `NYXSTORE_UI_V2` | History (Nyx Store redesign) |
| `apply_webfm_idle.py` | `WEBFM_IDLE_V1` | History (Web File Manager only answers while File Explorer is open and for 10 minutes after) |
| `apply_ram_report_patch.py` | `RAM_REPORT_V1` | History (adds the static-RAM report step to `build-firmware.yml`; needs `tools/ram_report.py`) |
| `apply_psram_bss_patch.py` | `PSRAM_BSS_V1` | History (marks seven large BSS arrays `EXT_RAM_BSS_ATTR` and enables `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` in `build-arduino-libs.yml`) |
| `apply_psram_bss2_patch.py` | `PSRAM_BSS_V2` | History (marks `dlg` and four DOOM arrays `EXT_RAM_BSS_ATTR`) |
| `apply_bt_ffat_patch.py` | `BT_FFAT_V1` | History (Music app plays FFat tracks through the Bluetooth output too) |
| `apply_call_rxlevel_patch.py` | `CALL_RXLEVEL_V1` | History (second in-call level bar for the other person, faster meter redraw) |
| `apply_webfm_upload_ux.py` | `WEBFM_UPLOAD_UX_V1` | History (drag-and-drop upload queue) |
| `apply_call_ui_patch.py` | `CALL_UI_V2` | History (Phone app redesign: dialer, call screen, banner, settings) |

Older scripts (written for the former flat layout) are archived in [`../legacy-patches/`](../legacy-patches/).
