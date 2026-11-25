#!/usr/bin/env python3
# apply_kb_float_branch.py -- KB_FLOAT_BRANCH_V1
# Jalankan dari root repo:  python3 tools/patches/apply_kb_float_branch.py
# Di app Update, tombol "+" (tambah branch) membuka keyboard TANPA kotak input yang terlihat, jadi nama
# branch diketik buta. Perbaikan: pakai baris input mengambang (KB_FLOAT_V1) juga untuk field itu (jenis 3).
import os, sys
M = "KB_FLOAT_BRANCH_V1"
PH = os.path.join("firmware", "phone", "phone.ino")
OT = os.path.join("firmware", "phone", "ota_check_renphone.ino")
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph, ot = rd(PH), rd(OT)
if M in ph and M in ot: print("sudah terpasang (%s)" % M); sys.exit(0)
ot = sub(ot, "// buka keyboard utk mengetik nama branch baru\n",
 "// " + M + ": true bila keyboard sedang mengetik nama branch baru (dipakai kbFloatKind() di phone.ino)\nbool otaIsNewBranchTarget(String* t) { return t == &otaNewBranch; }\n\n// buka keyboard utk mengetik nama branch baru\n", "ota")
ph = sub(ph, "  if(kbTarget==&geminiApiKey) return 2;\n  return 0;\n",
 "  if(kbTarget==&geminiApiKey) return 2;\n  if(otaIsNewBranchTarget(kbTarget)) return 3;   // " + M + "\n  return 0;\n", "kind")
ph = sub(ph, "      if(empty) d = (fk==1) ? String(\"Ketik pertanyaan...\") : String(\"Tempel/ketik API key...\");\n",
 "      if(empty) d = (fk==1) ? String(\"Ketik pertanyaan...\") : (fk==3 ? String(\"Ketik nama branch...\") : String(\"Tempel/ketik API key...\"));\n", "placeholder")
wr(PH, ph); wr(OT, ot); print("terpasang (%s)" % M)
