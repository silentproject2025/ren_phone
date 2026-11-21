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

Older scripts (written for the former flat layout) are archived in [`../legacy-patches/`](../legacy-patches/).
