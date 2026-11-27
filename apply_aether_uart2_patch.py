#!/usr/bin/env python3
# apply_aether_uart2_patch.py  --  marker AETHER_UART2_V1
# Pin link UART co-prosesor WROOM khusus Aether Board dipindah ke pin asli UART2 (GPIO16 RX2 / GPIO17 TX2).
# Build WROOM biasa (pin 13/14) TIDAK berubah. Jalankan dari root repo:  python3 apply_aether_uart2_patch.py
import os, sys

M = "AETHER_UART2_V1"
def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()
def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="\n") as f: f.write(s)
def rep(p, old, new, tag=""):
    s = rd(p)
    if s.count(old) != 1:
        sys.exit("GAGAL %s: potongan '%s' ditemukan %d kali (harus 1). Repo beda versi?" % (p, tag or old[:40], s.count(old)))
    wr(p, s.replace(old, new))

INO = "firmware/cam_bt_coprocessor/cam_bt_coprocessor.ino"
CAM = ".github/workflows/build-cam-coprocessor.yml"
FLS = ".github/workflows/flasher-pages.yml"
HW  = "docs/hardware.md"
CL  = "CHANGELOG.md"
for p in (INO, CAM, FLS, HW, CL):
    if not os.path.exists(p): sys.exit("File tidak ada: %s (jalankan dari root repo ren_phone)" % p)
if M in rd(INO):
    print("Sudah terpasang (%s). Tidak ada yang diubah." % M); sys.exit(0)

# 1) pin link
rep(INO,
"#define LINK_RX_PIN           13      // <- TX S3\n#define LINK_TX_PIN           14      // -> RX S3\n",
"""#ifdef RP_LINK_UART2   // AETHER_UART2_V1: Aether Board + WROOM -> pin asli UART2 (RX2/TX2)
  #define LINK_RX_PIN         16      // <- TX S3 (RX2)
  #define LINK_TX_PIN         17      // -> RX S3 (TX2)
#else
  #define LINK_RX_PIN         13      // <- TX S3
  #define LINK_TX_PIN         14      // -> RX S3
#endif
""", "pin link")

# 2) bangun dari tidur: GPIO16/17 BUKAN pin RTC -> ext0 deep sleep tak bisa
rep(INO,
"""  rtc_gpio_pullup_en(GPIO_NUM_13);               // RX dari S3 (GPIO13) idle HIGH
  rtc_gpio_pulldown_dis(GPIO_NUM_13);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);  // bangun begitu S3 mulai kirim data (start-bit LOW)
  esp_deep_sleep_start();
""",
"""#ifdef RP_LINK_UART2
  // AETHER_UART2_V1: GPIO16 bukan pin RTC -> ext0 deep sleep tidak bisa. Pakai light sleep + wakeup GPIO
  // (level LOW = start-bit dari S3), lalu restart supaya BT/UART mulai bersih. Arus tidur lebih besar dari deep sleep.
  esp_bluedroid_disable(); esp_bluedroid_deinit();
  esp_bt_controller_disable(); esp_bt_controller_deinit();
  Serial2.end();
  pinMode(LINK_RX_PIN, INPUT_PULLUP);
  { uint32_t t0 = millis(); while (digitalRead(LINK_RX_PIN) == 0 && millis() - t0 < 500) delay(5); }
  gpio_wakeup_enable((gpio_num_t)LINK_RX_PIN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  esp_restart();
#else
  rtc_gpio_pullup_en(GPIO_NUM_13);               // RX dari S3 (GPIO13) idle HIGH
  rtc_gpio_pulldown_dis(GPIO_NUM_13);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);  // bangun begitu S3 mulai kirim data (start-bit LOW)
  esp_deep_sleep_start();
#endif
""", "ext0 wakeup")

# 3) komentar kepala file
rep(INO,
" *    Build flag: -DRP_BT_ONLY=1   (modul kamera tidak ikut dikompilasi)\n",
""" *    Build flag: -DRP_BT_ONLY=1   (modul kamera tidak ikut dikompilasi)
 *
 *  VARIAN WROOM untuk AETHER BOARD (AETHER_UART2_V1): tambah -DRP_LINK_UART2=1
 *    Link pindah ke pin asli UART2:  S3 GPIO41 (TX) -> WROOM GPIO16 (RX2)
 *                                    S3 GPIO42 (RX) <- WROOM GPIO17 (TX2)
 *    Wajib modul TANPA PSRAM (WROOM-32); di WROVER GPIO16/17 dipakai PSRAM.
 *    Catatan: GPIO16 bukan pin RTC, jadi "matikan" dari S3 memakai light sleep (bukan deep sleep).
""", "komentar")

# 4) workflow build
rep(CAM,
"""      - name: Upload Binary Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: cam-bt-coprocessor-binaries""",
"""      - name: Compile varian WROOM Aether (link di UART2 GPIO16/17)
        run: |
          arduino-cli compile --fqbn esp32:esp32:esp32 \\
            --board-options "PartitionScheme=huge_app" \\
            --build-property "compiler.cpp.extra_flags=-DRP_BT_ONLY=1 -DRP_LINK_UART2=1" \\
            firmware/cam_bt_coprocessor --output-dir build_wroom_aether

      - name: Upload Binary Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: cam-bt-coprocessor-binaries""", "step compile")
rep(CAM,
"          cp build_wroom/cam_bt_coprocessor.ino.merged.bin bt_wroom_merged.bin\n",
"""          cp build_wroom/cam_bt_coprocessor.ino.merged.bin bt_wroom_merged.bin
          test -f build_wroom_aether/cam_bt_coprocessor.ino.merged.bin || (echo "::error::merged WROOM Aether tidak ada" && ls build_wroom_aether && exit 1)
          cp build_wroom_aether/cam_bt_coprocessor.ino.merged.bin bt_wroom_aether_merged.bin
""", "cp merged")
rep(CAM, 'gh release upload "$TAG" cam_merged.bin bt_wroom_merged.bin --clobber',
         'gh release upload "$TAG" cam_merged.bin bt_wroom_merged.bin bt_wroom_aether_merged.bin --clobber', "upload")
rep(CAM, 'gh release create "$TAG" cam_merged.bin bt_wroom_merged.bin \\',
         'gh release create "$TAG" cam_merged.bin bt_wroom_merged.bin bt_wroom_aether_merged.bin \\', "create")

# 5) web flasher
rep(FLS,
"""               "title": "Co-prosesor Bluetooth (WROOM)", "sub": "ESP32 WROOM &middot; tanpa kamera, tanpa PSRAM", "short": "BT WROOM"},
""",
"""               "title": "Co-prosesor Bluetooth (WROOM)", "sub": "ESP32 WROOM &middot; tanpa kamera, tanpa PSRAM", "short": "BT WROOM"},
              {"key": "cam_wroom_aether", "prefix": "cam-", "asset": "bt_wroom_aether_merged.bin", "chip": "ESP32", "esp": "esp32",
               "title": "Co-prosesor Bluetooth (WROOM, Aether)", "sub": "ESP32 WROOM &middot; link UART2 GPIO16/17 &middot; untuk Aether Board", "short": "BT WROOM Aether"},
""", "flasher")

# 6) docs
rep(HW,
"| Touch FT6336U (I2C) | SDA 8 · SCL 9 · RST 5 · INT 6 (not used; touch is polled) |\n",
"""| Touch FT6336U (I2C) | SDA 8 · SCL 9 · RST 5 · INT 6 (not used; touch is polled) |
| Link to WROOM co-processor (UART2) | S3 TX 41 → WROOM RX2 **GPIO16** · S3 RX 42 ← WROOM TX2 **GPIO17** · common GND. Flash the *WROOM, Aether* file (`bt_wroom_aether_merged.bin`, built with `-DRP_LINK_UART2=1`); the regular WROOM file uses GPIO 13/14. Use a module without PSRAM (WROOM-32): on a WROVER, GPIO16/17 belong to PSRAM. GPIO16 is not an RTC pin, so *power off* from the S3 puts the WROOM in light sleep (wakes on the first byte, then restarts) instead of deep sleep |
""", "docs")

# 7) changelog
rep(CL, "## Unreleased\n",
"""## Unreleased
- Changed: on the Aether Board, the WROOM Bluetooth co-processor link now uses the real UART2 pins (AETHER_UART2_V1): S3 GPIO41 (TX) to WROOM GPIO16 (RX2), S3 GPIO42 (RX) from WROOM GPIO17 (TX2). It is a separate build, `bt_wroom_aether_merged.bin` (`-DRP_BT_ONLY=1 -DRP_LINK_UART2=1`), also in the web flasher; the regular WROOM and ESP32-CAM builds keep GPIO 13/14. The S3 firmware is unchanged (its link pins are still 41/42). Because GPIO16 is not an RTC pin, *power off* on this build uses light sleep with a GPIO wake-up (then restarts) instead of deep sleep. Not yet compiled or tested on a device. Script: `apply_aether_uart2_patch.py`
""", "changelog")

print("OK: Aether WROOM link -> UART2 (GPIO16 RX / GPIO17 TX). Build WROOM biasa tidak berubah.")
