#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Perbaiki phone.ino yang terpotong + berisi byte NUL di ujungnya.

Jalankan DARI DALAM folder repo ren_phone (Termux):
    python3 fix_phone_ino.py

Cara kerja:
  1. Baca phone.ino sekarang, buang bagian NUL, ambil baris lengkap terakhir.
  2. Cari di riwayat git commit terbaru yang phone.ino-nya SEHAT
     (tanpa NUL, kurung kurawal seimbang, ada loop() utuh).
  3. Sambung: bagian awal dari file sekarang (edit terbaru tetap aman)
     + sisa file (ekor loop() dst.) dari commit sehat itu.
  4. Cek hasilnya (tanpa NUL, kurung seimbang), simpan backup phone.ino.broken.
  5. Tambah penjaga NUL di workflow supaya kasus ini langsung ketahuan di CI.
"""
import subprocess, sys, os, re

F = "phone.ino"

def sh(*a):
    return subprocess.run(a, capture_output=True)

def balance(text):
    """Selisih { } di luar komentar/string/char. 0 = seimbang."""
    d = 0; i = 0; n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i); i = n if j < 0 else j; continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2); i = n if j < 0 else j + 2; continue
        if c in "\"'":
            q = c; i += 1
            while i < n and text[i] != q:
                if text[i] == "\\": i += 1
                i += 1
            i += 1; continue
        if c == "{": d += 1
        elif c == "}": d -= 1
        i += 1
    return d

def healthy(data):
    if b"\0" in data: return False
    t = data.decode("utf-8", "replace")
    return "void loop(){" in t and balance(t) == 0

def main():
    if not os.path.exists(F) or sh("git", "rev-parse", "--git-dir").returncode != 0:
        print("Jalankan dari folder repo ren_phone (harus ada phone.ino & .git)"); return 1

    cur = open(F, "rb").read()
    if healthy(cur):
        print("phone.ino sudah sehat, tidak ada yang perlu diperbaiki."); 
        patch_workflow(); return 0

    head_raw = cur.split(b"\0")[0]
    head = head_raw.decode("utf-8", "replace")
    lines = head.split("\n")
    lines = lines[:-1]            # baris terakhir pasti terpotong di tengah -> buang
    print("File sekarang: %d baris utuh sebelum NUL, kurung belum tertutup: %d"
          % (len(lines), balance("\n".join(lines))))

    # kandidat commit (terbaru dulu) yang menyentuh phone.ino
    revs = sh("git", "log", "--format=%h", "-n", "60", "--", F).stdout.decode().split()
    good = None
    for r in revs:
        d = sh("git", "show", "%s:%s" % (r, F)).stdout
        if healthy(d):
            good = (r, d.decode("utf-8", "replace")); break
    if not good:
        print("Tidak ada commit sehat di 60 commit terakhir. Coba: git fetch --all --unshallow")
        return 2
    rev, gtxt = good
    glines = gtxt.split("\n")
    print("Commit sehat terbaru:", rev)

    # titik sambung: cari baris di file lama (mundur dari ujung, di dalam loop) yang
    # cocok persis & unik di file sehat setelah 'void loop(){'
    gl = next(i for i, l in enumerate(glines) if l.startswith("void loop(){"))
    cl = next(i for i, l in enumerate(lines) if l.startswith("void loop(){"))
    cut = None
    for k in range(len(lines) - 1, cl, -1):
        l = lines[k]
        if len(l.strip()) < 25: continue
        hits = [i for i in range(gl, len(glines)) if glines[i] == l]
        if len(hits) == 1:
            cut = (k, hits[0]); break
    if not cut:
        print("Titik sambung tidak ketemu (loop() sudah banyak berubah). Pakai:")
        print("  git checkout %s -- phone.ino" % rev); return 3
    k, g = cut
    print("Sambung di baris %d (file sekarang) = baris %d (commit %s)" % (k + 1, g + 1, rev))

    new = "\n".join(lines[:k + 1] + glines[g + 1:])
    if b"\0" in new.encode() or balance(new) != 0 or "void loop(){" not in new:
        print("Hasil sambungan tidak lolos cek. Pakai:  git checkout %s -- phone.ino" % rev)
        return 4
    os.replace(F, F + ".broken")
    with open(F, "w", encoding="utf-8", newline="\n") as f: f.write(new)
    print("OK: phone.ino diperbaiki (%d baris). Backup: phone.ino.broken" % new.count("\n"))
    patch_workflow()
    return 0

def patch_workflow():
    p = ".github/workflows/build-firmware.yml"
    if not os.path.exists(p): return
    s = open(p, encoding="utf-8").read()
    if "byte NUL" in s: return
    anchor = "          mkdir -p build_sketch\n"
    guard = ("          for f in *.ino *.h; do\n"
             "            if grep -qaP '\\x00' \"$f\"; then echo \"::error::$f berisi byte NUL (file terpotong)\"; exit 1; fi\n"
             "          done\n")
    if anchor in s:
        s = s.replace(anchor, guard + anchor, 1)
        open(p, "w", encoding="utf-8", newline="\n").write(s)
        print("Penjaga NUL ditambahkan ke", p)

if __name__ == "__main__":
    sys.exit(main())
