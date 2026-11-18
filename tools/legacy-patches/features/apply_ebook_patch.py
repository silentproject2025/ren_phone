#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_ebook_patch.py -- daftarkan app "Buku" (pembaca .txt / .epub) ke phone.ino + workflow + README.
#
# Jalankan dari folder repo (yang ada phone.ino, ebook_renphone.ino, ebook_core.h):
#     python3 apply_ebook_patch.py
#
# Idempoten & semua-atau-tidak-sama-sekali, sama seperti patch lain di repo ini:
# kalau satu anchor di phone.ino tidak ketemu tepat satu kali, TIDAK ADA yang diubah.
# Butuh: apply_camera_patch.py sudah pernah dijalankan (app Kamera = anchor penyisipan).
import sys, os

for f in ('phone.ino', 'ebook_renphone.ino', 'ebook_core.h'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

s = open('phone.ino', encoding='utf-8').read()
wf = '.github/workflows/build-firmware.yml'
w = open(wf, encoding='utf-8').read() if os.path.exists(wf) else None
rf = 'README.md'
r = open(rf, encoding='utf-8').read() if os.path.exists(rf) else None

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

did = False
if 'ebookEnter' in s:
    print("App Buku sudah terdaftar di phone.ino, dilewati")
else:
    # 1. enum Screen
    s = rep(s, "              SCR_CAMERA };\nenum Orientation",
               "              SCR_CAMERA,\n              // Buku: pembaca .txt / .epub -- lihat ebook_renphone.ino + ebook_core.h\n              SCR_EBOOK };\nenum Orientation",
            "enum Screen")
    # 2. forward declarations
    s = rep(s, "void camEnter(); void camExit(); void drawCamera(LGFX_Sprite&); void camTouch(int,int,bool,bool); void camLoopPoll();\n",
               "void camEnter(); void camExit(); void drawCamera(LGFX_Sprite&); void camTouch(int,int,bool,bool); void camLoopPoll();\n"
               "// ---- Buku (ebook_renphone.ino): pembaca .txt / .epub ----\n"
               "void ebookEnter(); void ebookExit(); void drawEbook(LGFX_Sprite&); void ebookTouch(int,int,bool,bool);\n"
               "void ebookPointerTick(bool,bool,int,int); void ebookLoopPoll();\n",
            "forward decl")
    # 3. tabel apps
    s = rep(s, "AppDef apps[42] = {", "AppDef apps[43] = {", "apps[42]")
    s = rep(s, "#define APP_COUNT 42", "#define APP_COUNT 43", "APP_COUNT")
    s = rep(s, "  { \"Kamera\",  'k', 0, camEnter,    camExit,    drawCamera, camTouch,    SCR_CAMERA },\n};",
               "  { \"Kamera\",  'k', 0, camEnter,    camExit,    drawCamera, camTouch,    SCR_CAMERA },\n"
               "  { \"Buku\",    'b', 0, ebookEnter,  ebookExit,  drawEbook,  ebookTouch,  SCR_EBOOK  },\n};",
            "apps[] entry")
    # 4. warna ikon
    s = rep(s, "  apps[41].color=0xF9A0; // Kamera - oranye\n",
               "  apps[41].color=0xF9A0; // Kamera - oranye\n  apps[42].color=0xE68A; // Buku - krem kecokelatan (kertas)\n",
            "warna")
    # 5. glyph ikon 'b': buku terbuka dengan garis teks
    s = rep(s, "    case 'p': { // Telepon: gagang telepon\n",
               "    case 'b': { // Buku: dua halaman terbuka + garis teks\n"
               "      int rr=max(8,r-6);\n"
               "      int bw=rr*2, bh=rr*3/2+2, x0=cx-bw/2, y0=cy-bh/2;\n"
               "      s.fillRoundRect(x0,y0,bw/2-1,bh,2,ic);\n"
               "      s.fillRoundRect(cx+1,y0,bw/2-1,bh,2,ic);\n"
               "      for(int i=0;i<3;i++){\n"
               "        int ly=y0+4+i*max(3,bh/4);\n"
               "        s.drawFastHLine(x0+3,ly,bw/2-7,bgCircle);\n"
               "        s.drawFastHLine(cx+4,ly,bw/2-7,bgCircle);\n"
               "      }\n"
               "      break;\n"
               "    }\n"
               "    case 'p': { // Telepon: gagang telepon\n",
            "ikon")
    # 6. loop(): buka buku setelah layar \"Membuka...\" tampil
    s = rep(s, "  camLoopPoll();  // Kamera: ambil frame dari CAM, foto/rekam/gerak (no-op kalau app tertutup)\n",
               "  camLoopPoll();  // Kamera: ambil frame dari CAM, foto/rekam/gerak (no-op kalau app tertutup)\n"
               "  ebookLoopPoll(); // Buku: jalankan pembukaan buku setelah layar \"Membuka...\" sempat tampil (no-op kalau tidak ada)\n",
            "loop")
    # 7. dispatch sentuhan: ketuk diproses saat jari lepas (generik hanya kirim jari-turun/tahan)
    s = rep(s, "    if(curScreen()==SCR_HOMEEDIT) homeEditPointerTick(touched,wasTouched,tx,ty);    // Atur Home: geser daftar + ketuk saat lepas\n",
               "    if(curScreen()==SCR_HOMEEDIT) homeEditPointerTick(touched,wasTouched,tx,ty);    // Atur Home: geser daftar + ketuk saat lepas\n"
               "    if(curScreen()==SCR_EBOOK) ebookPointerTick(touched,wasTouched,tx,ty);          // Buku: geser daftar/halaman, ketuk saat lepas\n",
            "pointer tick")
    did = True

wdid = False
if w is None:
    print("PERINGATAN: workflow tidak ditemukan, lewati bagian workflow")
elif 'ebook_renphone.ino' in w:
    print("Workflow sudah memuat ebook_renphone.ino, dilewati")
else:
    w = rep(w, "          cp camera_renphone.ino build_sketch/\n",
               "          cp camera_renphone.ino build_sketch/\n          cp ebook_renphone.ino build_sketch/\n          cp ebook_core.h build_sketch/\n",
            "workflow cp")
    wdid = True

rdid = False
if r is not None and 'App Buku' not in r:
    # README bersifat dokumentasi: anchor yang hilang hanya diberi peringatan, tidak menggagalkan patch.
    def soft(src, old, new, what):
        if src.count(old) != 1:
            print("PERINGATAN: README anchor '%s' tidak ketemu, dilewati" % what); return src
        return src.replace(old, new)
    r = soft(r, "| **Aplikasi bawaan** | 42 aplikasi |", "| **Aplikasi bawaan** | 43 aplikasi |", "jumlah app")
    r = soft(r, "| SPI Overclock layar | `HW` |",
                "| Buku (pembaca `.txt` dan `.epub`) | `BETA` | Logika inti (inflate, ZIP, EPUB, HTML ke teks, tata letak) diuji di komputer; antarmuka belum diuji pada perangkat. Lihat bagian *App Buku* di bawah. |\n| SPI Overclock layar | `HW` |",
            "baris status")
    r = soft(r, "python3 apply_camera_patch.py    # Kamera\n", "python3 apply_camera_patch.py    # Kamera\npython3 apply_ebook_patch.py     # Buku (pembaca .txt / .epub)\n", "daftar patch")
    r = r.rstrip('\n') + """

---

## App Buku (pembaca `.txt` dan `.epub`) `BETA`

Salin file `.txt` atau `.epub` ke folder `/books` pada SD card (dibuat otomatis; file di root SD juga ikut terdaftar), lalu buka app **Buku**.

| Aksi | Hasil |
|---|---|
| Ketuk sepertiga kanan / geser ke kiri | Halaman berikutnya |
| Ketuk sepertiga kiri / geser ke kanan | Halaman sebelumnya |
| Ketuk tengah | Menu: slider halaman, **Isi** (daftar isi), **Aa** (ganti font), **Tema**, **Tutup** (kembali ke daftar buku) |

- **Font:** Sans, Serif, Mono, Kecil, Sans+, Serif+. **Tema baca:** Terang, Sepia, Gelap, Hitam (terpisah dari tema sistem).
- **Posisi baca** disimpan per buku di `/books/.nyxreader` (bab + offset teks, jadi tetap tepat setelah ganti font atau orientasi). Daftar buku mengurutkan buku terakhir dibaca di atas dan menampilkan persen terbaca.
- **EPUB 2 dan 3:** bab dibaca satu per satu dari ZIP (hemat RAM); daftar isi dari NCX atau `nav`. ZIP64 dan EPUB ber-DRM tidak didukung.
- **TXT:** UTF-8, UTF-16 (dengan BOM), Latin-1/Windows-1252. Teks berbaris-keras (gaya Project Gutenberg) disambung ulang menjadi paragraf. Baris seperti `Chapter 1`, `BAB IV`, `Prolog` dikenali sebagai judul dan masuk daftar isi. Batas 3 MB per file (sisanya dipotong dengan pemberitahuan).
- **Batasan layar:** font bawaan hanya ASCII. Huruf beraksen ditampilkan sebagai huruf dasar, tanda kutip dan strip melengkung diluruskan, dan aksara non-Latin (CJK, Arab, Kiril) tampil sebagai `?`. Gambar di dalam EPUB tidak ditampilkan.
- **Pengujian:** `tests_ebook/` (jalankan `python3 tests_ebook/test_ebook.py`) menguji inflate terhadap zlib, EPUB 2/3 buatan, TXT berbagai penyandian, invarian tata letak, dan fuzz dengan AddressSanitizer. Sintaks antarmuka diperiksa dengan *mock API* (`tests_ebook/mock_arduino.h`). Antarmuka belum diuji pada perangkat.
"""
    rdid = True

if did: open('phone.ino', 'w', encoding='utf-8').write(s)
if wdid: open(wf, 'w', encoding='utf-8').write(w)
if rdid: open(rf, 'w', encoding='utf-8').write(r)
print("OK: app Buku %s, workflow %s, README %s" % ("terpasang" if did else "(sudah)", "diperbarui" if wdid else "(sudah/lewat)", "diperbarui" if rdid else "(sudah/lewat)"))
