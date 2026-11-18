# README-DOOM-WAD-V1: dokumentasikan perubahan terbaru di README.md
#  - pin sentuh FT6236 (SDA 6 / SCL 7) di tabel pin + bagian build ST7796
#  - urutan pencarian WAD Doom (SD dulu, FFat fallback) + preload FFat -> PSRAM
#  - batasan Doom dari FFat + 3 baris pemecahan masalah
# Jalankan dari root repo ren_phone:  py apply_readme_update.py
import sys, os
f = 'README.md'
if not os.path.exists(f):
    print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
s = open(f, encoding='utf-8').read()
if 'README-DOOM-WAD-V1' in s:
    print("README sudah diperbarui, dilewati"); sys.exit(0)
def rep(s, old, new, name):
    if s.count(old) != 1:
        print("ERROR [%s]: anchor ketemu %d kali: %r" % (name, s.count(old), old[:70])); sys.exit(1)
    return s.replace(old, new)

# 1. tabel pin
s = rep(s, "| Sentuh | SCLK 6 · MOSI 5 · MISO 4 · CS 9 |",
           "| Sentuh | ILI9341 (XPT2046, SPI): SCLK 6 · MOSI 5 · MISO 4 · CS 9 · ST7796 (FT6236, I2C): SDA 6 · SCL 7 (`Wire`) |", 'pin')

# 2. bagian build ST7796
s = rep(s, "FT6236 memakai bus I2C **sendiri** (`Wire`: SDA 15, SCL 7, alamat 0x38); RST/INT tidak dipakai.",
           "FT6236 memakai bus I2C **sendiri** (`Wire`: SDA 6, SCL 7, alamat 0x38; SDA sebelumnya GPIO 15); RST/INT tidak dipakai.", 'ft6236')

# 3. Doom: WAD
s = rep(s, "| **PPU Demo** | Peragaan mesin NyxPPU `EXP`. |\n",
"""| **PPU Demo** | Peragaan mesin NyxPPU `EXP`. |

<!-- README-DOOM-WAD-V1 -->
**WAD DOOM.** WAD disediakan sendiri oleh pengguna dan dicari otomatis di root dengan urutan nama `doom2.wad`, `doom1.wad`, `doom.wad`, `freedoom2.wad`, `freedoom1.wad`, `plutonia.wad`, `tnt.wad`. **Kartu SD diperiksa lebih dulu**; baru jika SD tidak terbaca atau tidak berisi WAD, firmware memakai **FFat** (diunggah lewat web). WAD di SD dibaca langsung dari kartu.

WAD dari FFat dimuat utuh ke PSRAM saat DOOM dibuka. Alasannya: task DOOM memakai stack di PSRAM, dan akses flash internal (FFat) mematikan cache sesaat, sehingga ESP-IDF meng-assert `esp_task_stack_is_sane_cache_disabled()` lalu perangkat restart. Konsekuensinya:

- WAD di FFat harus muat di PSRAM (`doom1.wad` ±4 MB muat; WAD yang jauh lebih besar seperti `doom2.wad` sebaiknya di SD). Jika gagal, layar menampilkan pesan *WAD di FFat gagal dimuat ke PSRAM*, bukan restart.
- Selama WAD berasal dari FFat, penyimpanan save game dan konfigurasi DOOM dinonaktifkan (agar tidak menulis flash dari task ber-stack PSRAM).
""", 'doom')

# 4. batasan
s = rep(s, "- **NyxPPU** belum dipasang pada Inferno maupun Home/Lock Screen.\n",
"- **NyxPPU** belum dipasang pada Inferno maupun Home/Lock Screen.\n"
"- **DOOM dari FFat:** WAD harus muat di PSRAM dan save game tidak tersimpan; gunakan kartu SD untuk WAD besar atau bila ingin menyimpan permainan.\n", 'batasan')

# 5. pemecahan masalah
s = rep(s, "| Persen baterai dan tegangan tidak berubah | INA219 tidak terdeteksi | Periksa pengkabelan SDA 15 dan SCL 7 serta alamat I2C. |\n",
"""| Persen baterai dan tegangan tidak berubah | INA219 tidak terdeteksi | Periksa pengkabelan SDA 15 dan SCL 7 serta alamat I2C. |
| DOOM membuat perangkat restart dengan `assert failed: spi_flash_disable_interrupts_caches_and_other_cpu ... esp_task_stack_is_sane_cache_disabled()` | Firmware lama: task DOOM (stack di PSRAM) membaca WAD dari FFat | Perbarui firmware (WAD FFat kini dimuat ke PSRAM lebih dulu), atau taruh WAD di kartu SD. |
| Layar sentuh tidak merespons (build ST7796) | Kabel SDA FT6236 belum sesuai pin: bawaan kini **SDA 6, SCL 7** | Pindahkan kabel SDA ke GPIO 6 (atau ubah `RP_TOUCH_SDA` di `display_conf.h`). Serial harus menampilkan `[touch] FT6236 @0x38 TERDETEKSI (SDA=6 SCL=7 ...)`. |
| Halaman `/remote` lewat WiFi rumah menjawab `403` | Opsi *Izinkan lewat WiFi rumah* mati | Nyalakan AP di aplikasi *Remote* (atau reboot bila *Mulai otomatis* aktif), sambung ke WiFi Accretion-Phone, buka `http://192.168.4.1/remote`, lalu nyalakan opsinya. |
""", 'tshoot')

open(f, 'w', encoding='utf-8').write(s)
print("OK: README diperbarui (pin sentuh, WAD Doom, batasan, pemecahan masalah)")
