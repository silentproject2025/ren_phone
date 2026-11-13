#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_remove_filter_patch.py -- hapus filter sentuhan (deadzone/ghost/latch v2-v5).
Isi blok dari "// ---- filter sentuhan v2" sampai akhir getInputPoint() diganti
getInputPoint() sederhana: Remote Control > touchscreen fisik, tanpa blokir apa pun.
Kalibrasi touch resistif (TOUCH_CAL_V4) TIDAK disentuh. Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "TOUCH_SIMPLE_V6" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

a = s.find("// ---- filter sentuhan v2")
b = s.find("\nvoid setup(){", a)
if a < 0 or b < 0 or s.count("bool getInputPoint(lgfx::touch_point_t &tp){") != 1:
    sys.exit("ERROR: blok filter sentuhan tidak ketemu / tidak seperti yang diharapkan. Dibatalkan.")
old = s[a:b]
for need in ("touchBlocked", "TOUCH_LATCH_MS", "bool getInputPoint("):
    if need not in old:
        sys.exit("ERROR: blok tidak memuat %s -- dibatalkan." % need)

new = '''// TOUCH_SIMPLE_V6 (apply_touch_remove_filter_patch.py): filter deadzone/ghost/latch dihapus.
// Remote Control (web) menang atas touchscreen fisik; selebihnya pass-through apa adanya.
bool getInputPoint(lgfx::touch_point_t &tp){
  { int rx=0,ry=0;
    if(rcInputPoll(&rx,&ry)){ tp.x=rx; tp.y=ry; tp.size=64; return true; } }
  return display.getTouch(&tp);
}
'''
s = s[:a] + new + s[b:]
if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: filter sentuhan dihapus, getInputPoint() disederhanakan")
