#!/usr/bin/env python3
# Tambah forward declaration uiBoostKick() di phone.ino (sebelum pemakaian pertama).
# Jalankan dari folder repo:  python3 fix_uiboost_proto.py
import io, sys
F = "phone.ino"
with io.open(F, encoding="utf-8", errors="surrogateescape", newline="") as f:
    s = f.read()
L = s.split("\n")
if any(l.strip() == "void uiBoostKick(); // fwd-decl (CI)" for l in L):
    print("sudah ada, lewati"); sys.exit(0)
t = next(i for i, l in enumerate(L) if l.startswith("struct Theme {"))
e = next(i for i in range(t, len(L)) if L[i].strip() == "};")
first_use = next(i for i, l in enumerate(L) if "uiBoostKick()" in l and not l.lstrip().startswith("//") and i > e)
assert first_use > e
L.insert(e + 1, "void uiBoostKick(); // fwd-decl (CI)")
with io.open(F, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
    f.write("\n".join(L))
print("OK: deklarasi disisipkan setelah baris %d (pemakaian pertama di baris %d)" % (e + 1, first_use + 2))
