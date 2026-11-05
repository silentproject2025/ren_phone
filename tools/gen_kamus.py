#!/usr/bin/env python3
"""gen_kamus.py -- membuat kamus_data.h dari tools/kamus_id.txt dan tools/kamus_en.txt.

Pemakaian (dari folder repo):   python3 tools/gen_kamus.py
Aturan:
  - baris berawalan '#' diabaikan; kata dipisah spasi
  - huruf besar -> kecil; kata ber-tanda hubung dipecah ("tiba-tiba" -> "tiba")
  - kata ber-apostrof (don't, i'm) dibuang: keyboard memecah kata di apostrof
  - hanya a-z, panjang 2..24; duplikat dibuang (urutan pertama menang = peringkat frekuensi)
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def load(path):
    seen, out = set(), []
    for line in open(path, encoding="utf-8"):
        if line.lstrip().startswith("#"):
            continue
        for tok in line.split():
            tok = tok.lower()
            if "'" in tok or "\u2019" in tok:
                continue
            for part in tok.split("-"):
                if re.fullmatch(r"[a-z]{2,24}", part) and part not in seen:
                    seen.add(part); out.append(part)
    return out

def emit(name, words):
    lines, cur = [], "  "
    for w in words:
        item = '"%s",' % w
        if len(cur) + len(item) > 110:
            lines.append(cur.rstrip()); cur = "  "
        cur += item
    if cur.strip():
        lines.append(cur.rstrip())
    return "static const char* const %s[] = {\n%s\n};\n" % (name, "\n".join(lines))

def main():
    ids = load(os.path.join(HERE, "kamus_id.txt"))
    en  = load(os.path.join(HERE, "kamus_en.txt"))
    h = []
    h.append("// kamus_data.h -- DIBUAT OTOMATIS oleh tools/gen_kamus.py, jangan diedit manual.")
    h.append("// Sumber: tools/kamus_id.txt (%d kata) dan tools/kamus_en.txt (%d kata)." % (len(ids), len(en)))
    h.append("// Urutan = perkiraan frekuensi (paling sering di depan); dipakai sebagai peringkat saran.")
    h.append("#pragma once")
    h.append("#define KMS_ID_N %d" % len(ids))
    h.append("#define KMS_EN_N %d" % len(en))
    h.append(emit("kmsWordsID", ids))
    h.append(emit("kmsWordsEN", en))
    out = os.path.join(ROOT, "kamus_data.h")
    open(out, "w", encoding="utf-8").write("\n".join(h))
    print("OK: %s  (ID=%d, EN=%d)" % (out, len(ids), len(en)))

if __name__ == "__main__":
    main()
