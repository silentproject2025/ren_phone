#!/usr/bin/env python3
# Fix: teks Home numpuk setelah buka Musik (state setTextWrap bocor ke layar lain)
# Jalankan dari folder ren_phone:  python3 apply_fix_textwrap.py
import sys
def patch(path, old, new):
    s = open(path, encoding="utf-8").read()
    if s.count(old) != 1:
        sys.exit(f"GAGAL: pola di {path} ketemu {s.count(old)}x (harus 1x)")
    open(path, "w", encoding="utf-8").write(s.replace(old, new))
    print("OK:", path)

patch("musicbt_renphone.ino",
      "s.setTextWrap(true, true);                          // kembalikan default LovyanGFX",
      "s.setTextWrap(false, false);                        // samakan dgn baseline app lain (jangan bocorkan wrapY=true ke Home)")

patch("phone.ino",
      "  canvas.setFont(screenAllowsCustomFont ? uiFontList[uiFontIdx] : &lgfx::fonts::Font0);\n  if(appSwitcherOpen){",
      "  canvas.setFont(screenAllowsCustomFont ? uiFontList[uiFontIdx] : &lgfx::fonts::Font0);\n  canvas.setTextWrap(false); // reset state teks tiap frame: app yg lupa/bocorin wrap gak bikin layar lain numpuk\n  if(appSwitcherOpen){")
