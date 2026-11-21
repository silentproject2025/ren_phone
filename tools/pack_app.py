#!/usr/bin/env python3
"""
pack_app.py -- susun folder app Nyx di kartu SD:  <sd>/apps/<id>/{app.elf,manifest.txt,data/}

  python3 tools/pack_app.py --id hello --elf build/nyxapp.app.elf \
          --manifest examples/native/hello/manifest.txt --sd /media/SDCARD

Validasi ringan di sisi PC (validasi sebenarnya tetap di firmware, nyxhost.h parseManifest):
id hanya [A-Za-z0-9_-] <24 karakter, manifest punya name=, ELF berawalan magic \\x7fELF dan <= 1 MB.
"""
import argparse, os, re, shutil, sys

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--id", required=True); ap.add_argument("--elf", required=True)
    ap.add_argument("--manifest", required=True); ap.add_argument("--sd", required=True)
    a = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,23}", a.id): sys.exit("id tidak valid (A-Z a-z 0-9 _ -, maks 23)")
    elf = open(a.elf, "rb").read()
    if elf[:4] != b"\x7fELF": sys.exit("bukan berkas ELF")
    if len(elf) > 1024 * 1024: sys.exit("ELF > 1 MB (batas loader di firmware)")
    mf = open(a.manifest, encoding="utf-8").read()
    if not re.search(r"^\s*name\s*=\s*\S", mf, re.M): sys.exit("manifest tanpa name=")
    d = os.path.join(a.sd, "apps", a.id)
    os.makedirs(os.path.join(d, "data"), exist_ok=True)
    shutil.copyfile(a.manifest, os.path.join(d, "manifest.txt"))
    open(os.path.join(d, "app.elf"), "wb").write(elf)
    print(f"OK: {d}  (ELF {len(elf)} B). Cabut SD dengan aman, lalu buka Nyx Apps di HP.")

if __name__ == "__main__":
    main()
