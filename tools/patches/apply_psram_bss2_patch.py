#!/usr/bin/env python3
"""PSRAM_BSS_V2: pindahkan sisa BSS yang aman ke PSRAM: dialog panggilan (dlg) + empat variabel DOOM yang tidak
dibaca tiap frame render (events, itemrespawnque, itemrespawntime, consistancy). Sekitar 4,7 KB RAM internal.
Perlu CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY (sudah diset Build Arduino Libs); tanpa itu atribut kosong, variabel tetap di internal.
Pakai dari root repo: python3 tools/patches/apply_psram_bss2_patch.py [--check]   (idempoten)"""
import re, sys, pathlib
CHECK = "--check" in sys.argv
ATTR = "EXT_RAM_BSS_ATTR"
bad = 0

# 1) dlg: struct anonim, penutupnya "} dlg;"
f = pathlib.Path("firmware/phone/call_renphone.ino"); t = f.read_text()
if "} dlg " + ATTR + ";" in t: print("sudah   call_renphone.ino: dlg")
elif t.count("} dlg;") != 1: print("GAGAL   call_renphone.ino: '} dlg;' tidak ditemukan tepat 1x"); bad += 1
else:
    if not CHECK: f.write_text(t.replace("} dlg;", "} dlg " + ATTR + ";", 1))
    print(("cocok " if CHECK else "dipasang") + " call_renphone.ino: dlg")

# 2) DOOM (C): (berkas, regex nama variabel)
D = pathlib.Path("third_party/esp_doom_core")
DOOM = [
  ("d_event.c", [r"static\s+event_t\s+events\[MAXEVENTS\]"]),
  ("p_mobj.c",  [r"mapthing_t\s+itemrespawnque\[ITEMQUESIZE\]", r"int\s+itemrespawntime\[ITEMQUESIZE\]"]),
  ("g_game.c",  [r"byte\s+consistancy\[MAXPLAYERS\]\[BACKUPTICS\]"]),
]
for fn, decls in DOOM:
    f = D / fn; t = f.read_text(); new = t
    for d in decls:
        name = re.search(r"(\w+)(?:\[|$)", d.replace(r"\s+", " ").split()[-1].replace("\\", "")).group(1)
        done = re.compile(r"(" + d + r")\s+" + ATTR + r"\s*;")
        pat = re.compile(r"^(" + d + r")[ \t]*;", re.M)
        if done.search(new): print(f"sudah   {fn}: {name}"); continue
        if len(pat.findall(new)) != 1: print(f"GAGAL   {fn}: {name} tidak ditemukan tepat 1x"); bad += 1; continue
        new = pat.sub(lambda m: m.group(1) + " " + ATTR + ";", new, count=1)
        print(("cocok " if CHECK else "dipasang") + f" {fn}: {name}")
    if new != t and 'esp_attr.h' not in new:
        new = re.sub(r"^(#include[^\n]*\n)", r'\1#include "esp_attr.h"   /* PSRAM_BSS_V2 */\n', new, count=1, flags=re.M)
    if new != t and not CHECK: f.write_text(new)
sys.exit(1 if bad else 0)
