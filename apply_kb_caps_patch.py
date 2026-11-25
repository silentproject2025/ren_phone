#!/usr/bin/env python3
# apply_kb_caps_patch.py -- KB_CAPS_V2
# Jalankan dari root repo:  python3 apply_kb_caps_patch.py
#
# Keyboard virtual: huruf kapital dibuat seperti keyboard HP.
#   - Ketuk shift 1x  -> huruf berikutnya kapital, lalu kembali kecil.
#   - Ketuk shift 2x cepat -> CAPS LOCK (kapital terus). Ketuk shift lagi -> mati.
#
# Yang diperbaiki dari KB_UX_V1:
#  1. Saat caps lock aktif, tuts  ; , . ?  dulu berubah jadi  : ! @ #  (jadi tidak bisa ngetik
#     koma/titik/tanda tanya). Sekarang tuts tanda baca TETAP saat caps lock, seperti keyboard HP.
#     (Shift 1x tetap seperti dulu, supaya  : ! @ #  masih bisa diketik.)
#  2. Ketukan ke-2 dihitung dari WAKTU SENTUH ASLI (bukan waktu loop() memprosesnya), jadi caps lock
#     tidak meleset / salah nyala saat layar sedang berat. Jendela 2x ketuk 400 -> 450 ms.
#  3. Ikon shift kini beda tiap status: panah kosong = mati, panah penuh = 1x, panah penuh + garis
#     bawah = caps lock.
#
# Berkas yang diubah: firmware/phone/phone.ino, CHANGELOG.md, docs/features/system-ui.md.
# Aman dijalankan ulang (cek penanda). Syarat: KB_UX_V1 sudah terpasang.
import os, sys

MARK = "KB_CAPS_V2"
PH = os.path.join("firmware", "phone", "phone.ino")
CL = "CHANGELOG.md"
DOC = os.path.join("docs", "features", "system-ui.md")


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus tepat 1x). Tidak ada berkas yang diubah." % (label, n))
    return s.replace(old, new)


if not os.path.exists(PH):
    sys.exit("GAGAL: %s tidak ada. Jalankan dari root repo." % PH)
ph = rd(PH)
if MARK in ph:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)
if "KB_UX_V1" not in ph:
    sys.exit("GAGAL: KB_UX_V1 belum terpasang (jalankan tools/patches/apply_kb_ux.py dulu).")

# 1) waktu sentuh asli + jendela 2x ketuk + helper label tuts
ph = sub(ph,
         "uint32_t kbShiftTapMs=0, kbSpaceTapMs=0;   // waktu ketukan shift / spasi terakhir\n",
         "uint32_t kbShiftTapMs=0, kbSpaceTapMs=0;   // waktu ketukan shift / spasi terakhir\n"
         "#define KB_CAPS_DBL_MS 450                 // KB_CAPS_V2: jeda maks. antar 2 ketukan shift = caps lock\n"
         "uint32_t kbEvtMs=0;                        // KB_CAPS_V2: waktu sentuh asli ketukan yg sedang diputar ulang (0 = pakai millis())\n",
         "state vars")

ph = sub(ph,
         "int kbPressKey=-1;\nunsigned long kbPressUntil=0;\n",
         "// KB_CAPS_V2: tuts tanda baca (; , . ?) TIDAK berubah saat caps lock, seperti keyboard HP.\n"
         "// Shift 1x tetap memakai peta kbU (: ! @ #) supaya simbol itu masih bisa diketik.\n"
         "static inline bool kbPunctKey(int r,int c){ return (r==1&&c==9) || (r==2&&c>=7); }\n"
         "static const char* kbKeyLabel(int r,int c){\n"
         "  if(kbMode==KB_UPPER && kbCaps && kbPunctKey(r,c)) return kbL[r][c];\n"
         "  return kbMaps[(int)kbMode][r][c];\n"
         "}\n"
         "\nint kbPressKey=-1;\nunsigned long kbPressUntil=0;\n",
         "label helper")

# 2) gambar label tuts
ph = sub(ph,
         "      const char* lb=lay[r][c];\n      char elb[4];\n",
         "      const char* lb=(kbMode==KB_UPPER) ? kbKeyLabel(r,c) : lay[r][c];   // KB_CAPS_V2\n      char elb[4];\n",
         "draw label")

# 3) ikon shift: kosong / penuh / penuh + garis
ph = sub(ph,
         "      s.fillTriangle(acx,acy-6, acx-6,acy+2, acx+6,acy+2, ic);\n"
         "    s.fillRect(acx-3,acy+2,6,4,ic);\n"
         "      if(kbCaps) s.fillRect(acx-6,acy+8,12,2,ic);   // KB_UX_V1: caps lock\n",
         "      if(up){                                                                 // KB_CAPS_V2: 1x / caps lock = panah penuh\n"
         "        s.fillTriangle(acx,acy-6, acx-6,acy+2, acx+6,acy+2, ic);\n"
         "        s.fillRect(acx-3,acy+2,6,4,ic);\n"
         "        if(kbCaps) s.fillRect(acx-6,acy+8,12,2,ic);                           // caps lock: + garis bawah\n"
         "      } else {                                                                // mati = panah kosong (garis tepi)\n"
         "        s.drawLine(acx,acy-6, acx-6,acy+2, ic);  s.drawLine(acx-6,acy+2, acx-3,acy+2, ic);\n"
         "        s.drawLine(acx-3,acy+2, acx-3,acy+6, ic); s.drawLine(acx-3,acy+6, acx+3,acy+6, ic);\n"
         "        s.drawLine(acx+3,acy+6, acx+3,acy+2, ic); s.drawLine(acx+3,acy+2, acx+6,acy+2, ic);\n"
         "        s.drawLine(acx+6,acy+2, acx,acy-6, ic);\n"
         "      }\n",
         "shift icon")

# 4) logika shift: pakai waktu sentuh asli
ph = sub(ph,
         "        uint32_t nm=millis();\n"
         "        if(kbCaps){ kbCaps=false; kbMode=KB_LOWER; kbAutoOffFor=kbTarget; kbAutoOffLen=(int)kbTarget->length(); }\n"
         "        else if((uint32_t)(nm-kbShiftTapMs)<400){ kbCaps=true; kbMode=KB_UPPER; }            // 2x cepat = caps lock\n",
         "        uint32_t nm=kbEvtMs ? kbEvtMs : millis();                              // KB_CAPS_V2: waktu sentuh asli\n"
         "        int32_t dt=(int32_t)(nm-kbShiftTapMs); if(dt<0) dt=0;\n"
         "        if(kbCaps){ kbCaps=false; kbMode=KB_LOWER; kbAutoOffFor=kbTarget; kbAutoOffLen=(int)kbTarget->length(); }\n"
         "        else if(dt<KB_CAPS_DBL_MS){ kbCaps=true; kbMode=KB_UPPER; }            // 2x cepat = caps lock\n",
         "shift logic")

# 5) ketik tuts huruf/tanda baca: pakai label yang sama dgn yang digambar
ph = sub(ph,
         "  else { kbSugOnChar(*kbTarget,lay[r][c][0]); *kbTarget+=lay[r][c]; }\n",
         "  else { const char* kc=(kbMode==KB_UPPER) ? kbKeyLabel(r,c) : lay[r][c];   // KB_CAPS_V2\n"
         "         kbSugOnChar(*kbTarget,kc[0]); *kbTarget+=kc; }\n",
         "type key")

# 6) antrean sentuh cepat: teruskan waktu sentuh asli
ph = sub(ph,
         "    if(curScreen()==SCR_HOME){\n"
         "      if(kbTarget==&homeSearchInput) kbTouch(qx,qy); else continue;\n"
         "    } else {\n"
         "      int idx=appIndexForScreen(curScreen());\n"
         "      if(idx<0) continue;\n"
         "      apps[idx].touch(qx,qy,false,true);\n"
         "    }\n",
         "    kbEvtMs=qms;                                         // KB_CAPS_V2: kbTouch memakai waktu sentuh asli\n"
         "    if(curScreen()==SCR_HOME){\n"
         "      if(kbTarget==&homeSearchInput) kbTouch(qx,qy); else { kbEvtMs=0; continue; }\n"
         "    } else {\n"
         "      int idx=appIndexForScreen(curScreen());\n"
         "      if(idx<0){ kbEvtMs=0; continue; }\n"
         "      apps[idx].touch(qx,qy,false,true);\n"
         "    }\n"
         "    kbEvtMs=0;\n",
         "queue drain")

# ---------------------------------------------------------------- CHANGELOG + docs (opsional)
if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if MARK not in c and c.count(anchor) == 1:
        entry = ("- Improved: capital letters on the on-screen keyboard now work like a phone keyboard (KB_CAPS_V2). Tap Shift once for one capital letter, "
                 "tap it twice quickly for caps lock, tap again to turn it off. With caps lock on, the keys `; , . ?` keep their meaning (before, they turned into `: ! @ #`, so you could not type a comma or a full stop). "
                 "The second tap is now timed from the real touch time, so caps lock no longer misses when the screen is busy (window 450 ms). "
                 "The Shift icon shows the state: empty arrow = off, filled arrow = one capital, filled arrow with a bar = caps lock. Not yet tested on a device. Script: `apply_kb_caps_patch.py`\n")
        wr(CL, c.replace(anchor, anchor + entry))
if os.path.exists(DOC):
    d = rd(DOC)
    old_row = "Shift twice quickly = caps lock;"
    if d.count(old_row) == 1:
        wr(DOC, d.replace(old_row, "Shift once = one capital letter, shift twice quickly = caps lock (the keys `; , . ?` stay the same);"))

wr(PH, ph)
print("ok: phone.ino (+ CHANGELOG.md, docs/features/system-ui.md bila ada)")
