# Bilingual UI (`I18N_V1`)

**Default: English.** Change it in **Settings → Language** (under DISPLAY, below *Font*). Tapping the row toggles English ⇄ Indonesian. The choice is stored in NVS (`ui` / `lang`), so it survives restarts.

## Files

| File | Purpose |
|---|---|
| `firmware/phone/i18n.h` | The language engine: `uiLang`, `RPTR("text")`, saving/loading the choice |
| `firmware/phone/i18n_dict.h` | The ID↔EN dictionary (**generated**, do not edit by hand) |
| `tools/i18n_pairs.txt` | **The dictionary source** — one line per pair: `INDONESIAN_TEXT ::: ENGLISH` |
| `tools/gen_i18n.py` | `i18n_pairs.txt` → `i18n_dict.h` |
| `tools/legacy-patches/features/apply_i18n_patch.py` | Wrapped UI literals with `RPTR()` and installed the Language row in Settings (idempotent). Archived; written for the old flat layout, so run it from `firmware/phone/` if you need it again. |

## How it works

`RPTR("Pengaturan")` returns `"Settings"` (English) or `"Pengaturan"` (Indonesian). Text that is **not in the dictionary is returned unchanged** (it is never broken). The source text in the code may be Indonesian or English; both are matched. The result is cached per literal address, so it is cheap even when called every frame. The Home / Control Center image caches are discarded automatically when the language changes.

## Adding or fixing a translation

1. Add a line to `tools/i18n_pairs.txt` (the text must **exactly match** the literal in the code, including trailing spaces).
2. `python3 tools/gen_i18n.py`
3. `python3 tools/legacy-patches/features/apply_i18n_patch.py` from inside `firmware/phone/` (wraps new literals that are already in the dictionary; safe to repeat).
4. Commit `i18n_dict.h` + the changed `.ino` files.

For new text you write yourself in the code: simply write `RPTR("text")` directly (inside a function, not in a global/`static` initialiser). For dynamic text (a buffer whose content changes): `RPTRD(buf)` (no cache).

## Not yet translated (left as-is)

- Text assembled from `String + number` / `snprintf` whose format is not in the dictionary.
- Serial logs `[...]`, the contents of config files, the SIP protocol, prompts to Gemini.
- The Remote (`RC_PAGE`) and Gamepad (`PAD_PAGE`) web pages: their own HTML/JS.
- The dictionary data `kamus_data.h`, the Trivia question list, book contents.

If some text is still Indonesian in English mode, add it to the dictionary (steps above).
