#!/usr/bin/env python3
"""
nyxapps_release.py -- susun hasil build CI app contoh Nyx menjadi paket siap unduh.

  python3 tools/nyxapps_release.py --elfs artifacts --examples examples/native --out dist \
          --commit abc1234 --branch main --idf v5.5.5

Masukan : <elfs>/<id>/nyxapp.app.elf  (satu folder per app, hasil `idf.py elf`)
Keluaran: <out>/<id>.zip   berisi apps/<id>/{app.elf,manifest.txt,data/}
                           -> salin isi zip ke akar SD, atau upload lewat Web File Manager
          <out>/nyxapps.json   indeks (nama, ikon, izin, ukuran, SHA-256) untuk halaman web flasher

Validasi (ELF, ukuran <= 1 MB, manifest punya name=) dikerjakan tools/pack_app.py.
"""
import argparse, hashlib, json, os, re, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
# app uji internal (sengaja bocor / menghabiskan memori): tetap dipublikasikan tapi diberi label "uji"
TEST_ONLY = {"memhog", "leaky"}
ELF_LOADER = "2.0.0"   # sama dengan pin di build-arduino-libs.yml


def parse_manifest(text):
    mf = {}
    for ln in text.splitlines():
        ln = ln.split("#", 1)[0].strip()
        if "=" in ln:
            k, v = ln.split("=", 1)
            mf[k.strip()] = v.strip()
    return mf


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elfs", required=True); ap.add_argument("--examples", required=True)
    ap.add_argument("--out", required=True); ap.add_argument("--commit", default="")
    ap.add_argument("--branch", default=""); ap.add_argument("--idf", default="")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    apps = []
    ids = sorted(d for d in os.listdir(a.elfs) if os.path.isdir(os.path.join(a.elfs, d)))
    if not ids:
        sys.exit("tidak ada ELF di %s" % a.elfs)
    for aid in ids:
        elf = os.path.join(a.elfs, aid, "nyxapp.app.elf")
        mfp = os.path.join(a.examples, aid, "manifest.txt")
        if not os.path.isfile(elf):
            sys.exit("ELF tidak ditemukan: %s" % elf)
        if not os.path.isfile(mfp):
            sys.exit("manifest tidak ditemukan: %s" % mfp)
        with tempfile.TemporaryDirectory() as stage:
            subprocess.run([sys.executable, os.path.join(HERE, "pack_app.py"), "--id", aid, "--elf", elf,
                            "--manifest", mfp, "--sd", stage], check=True, stdout=subprocess.DEVNULL)
            zpath = os.path.join(a.out, aid + ".zip")
            with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
                base = os.path.join(stage, "apps", aid)
                for root, dirs, files in os.walk(base):
                    dirs.sort()
                    rel = os.path.relpath(root, stage).replace(os.sep, "/")
                    if not files and not dirs:
                        z.writestr(rel + "/", "")            # folder kosong (data/)
                    for fn in sorted(files):
                        z.write(os.path.join(root, fn), rel + "/" + fn)
        mf = parse_manifest(open(mfp, encoding="utf-8").read())
        perms = [p for p in re.split(r"[,\s]+", mf.get("perms", "")) if p]
        apps.append({
            "id": aid, "name": mf.get("name", aid), "sym": mf.get("sym", aid[:1].upper()),
            "color": mf.get("color", "808080"), "perms": perms,
            "mem_kb": int(mf.get("mem_kb", "0") or 0), "stack_kb": int(mf.get("stack_kb", "8") or 8),
            "elf_size": os.path.getsize(elf), "sha256": sha256_file(elf),
            "zip": aid + ".zip", "kind": "uji" if aid in TEST_ONLY else "contoh",
        })
        print("OK %-10s elf=%d B  zip=%d B" % (aid, os.path.getsize(elf), os.path.getsize(zpath)))
    # contoh dulu, app uji di belakang
    apps.sort(key=lambda x: (x["kind"] != "contoh", x["id"]))
    idx = {"commit": a.commit, "branch": a.branch, "idf": a.idf, "elf_loader": ELF_LOADER, "apps": apps}
    with open(os.path.join(a.out, "nyxapps.json"), "w", encoding="utf-8") as f:
        json.dump(idx, f, indent=2, ensure_ascii=False)
    print("indeks: %d app" % len(apps))


if __name__ == "__main__":
    main()
