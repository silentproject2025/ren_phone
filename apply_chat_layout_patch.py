#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_chat_layout_patch.py -- CHAT_LAYOUT_FIX_V1
Layout Chat tidak pas: teks yang rata-kanan / di-tengah bergeser ke kiri
(jam di bubble terpotong di tepi kiri layar atau keluar dari bubble, label
tombol "Kirim"/"Atur" tidak di tengah). Penyebab: s.textWidth() di firmware
ini mengembalikan lebar yang salah (sekitar 2x) lewat pembungkus font emoji,
sedangkan seluruh layout Chat (lebar bubble, pemotongan baris) memakai sel 6 px.
Patch: lebar teks dihitung sendiri = jumlah sel x 6 px (emoji = 2 sel), sama
dengan yang dipakai untuk menggambar. Plus label "Gagal - ketuk utk kirim
ulang" (29 karakter) diberi lebar minimum yang cukup (dulu 26 -> meluber).
Pakai (Termux, folder repo):  python apply_chat_layout_patch.py
"""
import os, shutil, sys
MARK = "CHAT_LAYOUT_FIX_V1"
FN = "chat_renphone.ino"
REPS = [
 ("static void chatPrintCentered(LGFX_Sprite& s, int x, int w, int y, const char* t) {\n  s.setCursor(x + (w - s.textWidth(t)) / 2, y);\n",
  "// CHAT_LAYOUT_FIX_V1: lebar teks = sel x 6 px (ASCII 1 sel, emoji 2 sel), BUKAN s.textWidth()\n"
  "static int chatTW(const char* t) { return emjCells(t, (int)strlen(t)) * 6; }\n\n"
  "static void chatPrintCentered(LGFX_Sprite& s, int x, int w, int y, const char* t) {\n  s.setCursor(x + (w - chatTW(t)) / 2, y);\n"),
 ("bx + bw - CB_PADX - s.textWidth(st), by + CB_PADT + outN * CB_LINE)",
  "bx + bw - CB_PADX - chatTW(st), by + CB_PADT + outN * CB_LINE)"),
 ("s.setCursor(bx + bw - CB_PADX - s.textWidth(hm), by + CB_PADT + qo + n * CB_LINE); s.print(hm);",
  "s.setCursor(bx + bw - CB_PADX - chatTW(hm), by + CB_PADT + qo + n * CB_LINE); s.print(hm);"),
 ("int kx = bx + bw - CB_PADX - s.textWidth(hm) - 11,",
  "int kx = bx + bw - CB_PADX - chatTW(hm) - 11,"),
 ("int minW = (os == 2) ? 26 * 6 : 11 * 6;",
  "int minW = (os == 2) ? 29 * 6 : 11 * 6;   // CHAT_LAYOUT_FIX_V1: \"Gagal - ketuk utk kirim ulang\" = 29 karakter"),
]
def main():
    if not os.path.isfile(FN):
        print("GAGAL: %s tidak ada (jalankan dari folder repo)" % FN); sys.exit(1)
    txt = open(FN, encoding="utf-8", newline="").read()
    if MARK in txt:
        print("sudah dipatch, dilewati."); return
    crlf = "\r\n" in txt
    for old, new in REPS:
        if crlf: old = old.replace("\n", "\r\n"); new = new.replace("\n", "\r\n")
        n = txt.count(old)
        if n != 1:
            print("GAGAL: potongan kode ditemukan %d kali (harus 1): %s" % (n, old.strip().splitlines()[0][:80]))
            print("Tidak ada file yang diubah."); sys.exit(1)
        txt = txt.replace(old, new, 1)
    shutil.copyfile(FN, FN + ".bak-chatlayout")
    open(FN, "w", encoding="utf-8", newline="").write(txt)
    print("OK: %s dipatch (backup: %s.bak-chatlayout)" % (FN, FN))
if __name__ == "__main__":
    main()
