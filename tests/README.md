# Host-side tests

These tests run on a PC (or in Termux) with `g++` and Python 3; no ESP32 is needed. They cover the logic that can be separated from the hardware. Run every command **from the repository root** unless noted.

| Suite | Folder | What it checks |
|---|---|---|
| Backup | `tests/backup/` | The `.nbk` format, CRC32, SHA-256, PBKDF2, AES-256-CTR (compared with Python `hashlib`/`cryptography`), cloud backup key derivation, base64 and JSON, and the backup screen against mocks |
| Camera | `tests/camera/` | RPLink camera protocol, header dedup, flow control, AVI writer |
| Chess | `tests/chess/` | Rules and *perft*, AI levels, style-model serialisation, Gemini match logic, second core |
| Dictionary | `tests/dictionary/` | Keyboard suggestions and autocorrect |
| Ebook | `tests/ebook/` | inflate, ZIP, EPUB, TXT, Markdown, layout, UI flow (with AddressSanitizer) |
| Gutenberg | `tests/gutenberg/` | Gutendex/OPDS parsing, download registry, CRC32 and ZIP checks, download simulation |
| Nyx Apps | `tests/nyxapp/` | `nyxmem.h` arena (quota, canaries, fuzzing, memory pressure, TLS gate), `nyxhost.h` host API (manifest, path sandbox, permissions), example apps run through `dlopen`, and a syntax check of `nyxapp_renphone.ino` against a stub |
| SysMem | `tests/sysmem/` | `sysmem.h` accounting, budgets, reserve floor, reclaim chain, fuzzing with ASan + UBSan |

The tests include the real firmware sources from `firmware/phone/` (and `firmware/cam_bt_coprocessor/` for the camera), so they exercise the code that is actually shipped.

## Commands

```bash
# Backup
g++ -std=c++11 -O1 -Ifirmware/phone -o /tmp/test_backup tests/backup/test_backup.cpp && /tmp/test_backup
python3 tests/backup/crypto_check.py
g++ -std=c++11 -O1 -D_GNU_SOURCE -Ifirmware/phone -o /tmp/test_cloud tests/backup/test_cloud.cpp && /tmp/test_cloud
python3 tests/backup/cloud_check.py
g++ -std=c++11 -Ifirmware/phone -Itests/backup -Itests/backup/stub -o /tmp/bk_glue tests/backup/glue_test.cpp && /tmp/bk_glue

# Dictionary
g++ -std=c++11 -O1 -Ifirmware/phone -o /tmp/test_kamus tests/dictionary/test_kamus.cpp && /tmp/test_kamus

# Gutenberg
(cd tests/gutenberg && g++ -std=c++11 -o /tmp/t_gx test_gutendex.cpp && /tmp/t_gx)
(cd tests/gutenberg && g++ -std=c++11 -o /tmp/t_gd test_gutdl.cpp && /tmp/t_gd)
python3 tests/gutenberg/run_sim.py

# Camera
g++ -std=c++11 -I firmware/cam_bt_coprocessor -I tests/camera/stub -I tests/camera -I firmware/phone \
    -o /tmp/test_cam tests/camera/test_cam.cpp && /tmp/test_cam

# Nyx Apps (uses $TMPDIR, so it also works in Termux; needs g++ and gcc)
(cd tests/nyxapp && ./run.sh)

# SysMem
./tests/sysmem/run.sh

# Ebook
python3 tests/ebook/test_ebook.py
python3 tests/ebook/test_ebook_v2.py

# Chess (extract.sh generates engine_ai.inc from chess_renphone.ino first)
sh tests/chess/run.sh
sh tests/chess/run_gem.sh
sh tests/chess/run_smp.sh
```

`tests/chess/ren_uci.cpp` is a small UCI-style wrapper around the engine used by `arena.py` and `match.py`. Build it with `g++ -O2 -std=c++11 -o ren_uci ren_uci.cpp -lm` inside `tests/chess/` after running `extract.sh`. `bench_nyx.cpp` and `bench_nyx2.cpp` are rough Nyx-level experiments that include the generated `engine_ai.inc`.

Generated files (`engine_ai.inc`, `chess_engine.inc`, `gem_block.inc`, the `ren_uci` binary, `sim_*.inc`) are produced by the scripts and are listed in `.gitignore`.

## Known failing tests

Three tests fail to compile **in the original flat layout as well**, so this is not caused by the folder reorganisation:

- `tests/backup/glue_test.cpp`
- `tests/dictionary/glue_test.cpp`
- `tests/ebook/run_ui_test.py`

They include the real `.ino` files, which now use the `RPTR()` translation macro (and `delay()`), and the Arduino mocks in these folders do not define them yet. Adding the missing mock definitions should fix them.

## security
`./tests/security/run.sh` — PC tests for `sec_core.h` (password hash, encrypted API keys, escalating lockout, signed `info.txt`) and for the real Web File Manager login block extracted from `phone.ino` (mock Arduino, including a reboot simulation). `./tests/security/ota_sign_check.sh` — keygen → sign → verify the way the firmware does. Needs `g++`, `openssl`, and `pip install cryptography` for the cross-checks.
