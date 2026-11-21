#!/usr/bin/env python3
"""
apply_sysmem_patch.py -- pasang SysMem ("MMU software" firmware utama) ke firmware/phone/phone.ino

Pakai (dari root repo):
    python3 apply_sysmem_patch.py            # pasang
    python3 apply_sysmem_patch.py --check    # hanya cek apakah anchor cocok

Idempoten & atomik: kalau SATU anchor tidak ketemu persis sekali, TIDAK ADA yang diubah.
File baru (sysmem.h, sysmem_fw.h, sysmem_renphone.ino) harus sudah ada di firmware/phone/.
Cadangan: phone.ino.bak_sysmem. App Memory Guard lama TIDAK dihapus (SysMem menambah reclaim
berbasis blok kontigu di atasnya).
"""
import sys, os

PATH = os.path.join("firmware", "phone", "phone.ino")
MARK = "SYSMEM_V1"

EDITS = [
    ("include + prototipe",
     '#include "esp_heap_caps.h"',
     '#include "esp_heap_caps.h"\n#include "sysmem_fw.h"   // SYSMEM_V1: API manajer memori firmware (tipe sysmem:: harus terbaca sebelum prototipe otomatis)'),
    ("panggilan tick di loop()",
     '  appMemGuardTick(); // v92:',
     '  smTick(); // SYSMEM_V1: reclaim berbasis blok kontigu + log per-owner saat RAM kritis (sysmem_renphone.ino)\n  appMemGuardTick(); // v92:'),
]

def main():
    check = "--check" in sys.argv
    if not os.path.exists(PATH):
        sys.exit(f"tidak ada {PATH} -- jalankan dari root repo")
    src = open(PATH, encoding="utf-8").read()
    if MARK in src:
        print("sudah terpasang (penanda SYSMEM_V1 ada) -- tidak ada yang diubah"); return
    out = src
    for name, old, new in EDITS:
        n = out.count(old)
        if n != 1:
            sys.exit(f"GAGAL: anchor '{name}' ditemukan {n}x (harus tepat 1x). phone.ino berubah dari versi yang dipatch? Tidak ada yang diubah.")
        out = out.replace(old, new)
    for need in ("sysmem.h", "sysmem_fw.h", "sysmem_renphone.ino"):
        if not os.path.exists(os.path.join("firmware", "phone", need)):
            print(f"PERINGATAN: firmware/phone/{need} belum ada -- salin dulu sebelum build")
    if check:
        print("OK: semua anchor cocok (mode --check, tidak menulis)"); return
    open(PATH + ".bak_sysmem", "w", encoding="utf-8").write(src)
    open(PATH, "w", encoding="utf-8").write(out)
    print(f"OK: {len(EDITS)} perubahan dipasang. Cadangan: {PATH}.bak_sysmem")

if __name__ == "__main__":
    main()
