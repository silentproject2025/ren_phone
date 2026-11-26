#!/usr/bin/env python3
# remove_relay.py -- hapus fitur Relay (stream video via relay_yt.py) dari repo ren_phone.
# Jalankan dari folder utama repo:  python remove_relay.py
# Yang dihapus: tombol Relay + fungsi mjpegPlayRelay di phone.ino, folder tools/relay,
#               tools/patches/apply_relay_*.py, dan 3 baris Relay di CHANGELOG.md (Unreleased).
# Tidak disentuh: TURN "relay" di fitur telepon (itu hal lain).
import os, re, shutil, sys

INO = "firmware/phone/phone.ino"
if not os.path.isfile(INO):
    sys.exit("Jalankan dari folder utama repo (firmware/phone/phone.ino tidak ketemu).")

s = open(INO, encoding="utf-8", newline="").read()
if "mjpegPlayRelay" not in s:
    print("phone.ino: sudah bersih dari Relay, dilewati.")
else:
    # 1) fungsi mjpegPlayRelay (+ komentar di atasnya)
    a = s.index("// RELAY_STREAM_V1: putar stream MJPEG dari relay")
    b = s.index("void mjpegEnter(){")
    assert a < b and "mjpegPlayRelay" in s[a:b]
    s = s[:a] + s[b:]
    # 2) tombol Relay di layar daftar video
    old = ('  iosRR(s,SCR_W-140,24,64,18,4,T().surface2);   // RELAY_STREAM_V1\n'
           '  s.setTextColor(T().accent);s.setCursor(SCR_W-130,29);s.print("Relay");\n')
    assert s.count(old) == 1, "blok tombol Relay tidak ketemu persis"
    s = s.replace(old, "")
    # 3) sentuhan tombol Relay
    old = ('  if(x>=SCR_W-140 && x<=SCR_W-76 && y>=24 && y<=42){   // RELAY_STREAM_V1\n'
           '    mjpegPlayRelay();\n'
           '    needRedraw = true;\n'
           '    return;\n'
           '  }\n\n')
    assert s.count(old) == 1, "blok sentuh Relay tidak ketemu persis"
    s = s.replace(old, "")
    assert "mjpegPlayRelay" not in s and "RELAY_" not in s
    open(INO, "w", encoding="utf-8", newline="").write(s)
    print("phone.ino: fitur Relay dihapus.")

# folder & patch lama
for p in ["tools/relay"]:
    if os.path.isdir(p): shutil.rmtree(p); print("dihapus:", p)
pd = "tools/patches"
if os.path.isdir(pd):
    for f in os.listdir(pd):
        if f.startswith("apply_relay_"):
            os.remove(os.path.join(pd, f)); print("dihapus:", f)

# changelog (belum rilis, jadi barisnya dibuang saja)
cl = "CHANGELOG.md"
if os.path.isfile(cl):
    ln = open(cl, encoding="utf-8", newline="").read().split("\n")
    keep = [l for l in ln if not (l.startswith("- ") and ("RELAY_STREAM_V" in l or "RELAY_DISCOVER_V1" in l))]
    if len(keep) != len(ln):
        open(cl, "w", encoding="utf-8", newline="").write("\n".join(keep)); print("CHANGELOG.md: %d baris Relay dibuang." % (len(ln) - len(keep)))
print("Selesai. Cek dengan:  git status   lalu compile firmware.")
