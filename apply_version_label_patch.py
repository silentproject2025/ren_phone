#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_version_label_patch.py -- jalankan dari root repo.

Menambah LABEL VERSI (mis. v1.0.0) yang terpisah dari nomor build:
  - nomor build (github.run_number) TETAP dipakai OTA utk membandingkan -> tidak diubah
  - label versi dibaca dari file VERSION di root repo, disuntik CI lewat -DRP_VERSION
  - tampil di app Update ("v1.0.0 b123"), About (kartu + baris "Versi firmware"),
    judul Release ("Firmware v1.0.0 (build 123)") dan info.txt (version=)
Cara menaikkan versi nanti: ubah isi file VERSION (mis. 1.0.1), commit, push.
Aman dijalankan ulang. Semua-atau-tidak-sama-sekali.
"""
import os, sys

F_OTA, F_PHONE, F_ABOUT = "ota_check_renphone.ino", "phone.ino", "about_renphone.ino"
F_WF = ".github/workflows/build-firmware.yml"

def rd(p): return open(p, encoding="utf-8").read()
ota, phone, about, wf = rd(F_OTA), rd(F_PHONE), rd(F_ABOUT), rd(F_WF)

if "otaVersionLabel" in ota or "RP_VERSION" in wf:
    print("patch sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# ---------- ota_check_renphone.ino ----------
ota = rep(ota,
"#ifndef RP_BRANCH\n#define RP_BRANCH local\n#endif\n",
"""#ifndef RP_BRANCH
#define RP_BRANCH local
#endif
// label versi utk manusia (mis. 1.0.0), disuntik workflow dari file VERSION lewat -DRP_VERSION=...
// Beda dgn RP_BUILD_NUM: nomor build yg dipakai OTA utk membandingkan, label ini hanya tampilan.
#ifndef RP_VERSION
#define RP_VERSION dev
#endif
""", "RP_BRANCH")

ota = rep(ota,
"int otaBuildNum()    { return (int)RP_BUILD_NUM; }\n",
"""int otaBuildNum()    { return (int)RP_BUILD_NUM; }
void otaVersionLabel(char* out, int n) {   // "v1.0.0", atau "dev" utk build lokal
  const char* v = RP_STR(RP_VERSION);
  if (strcmp(v, "dev") == 0) snprintf(out, n, "dev");
  else snprintf(out, n, "v%s", v);
}
""", "otaBuildNum")

# ---------- phone.ino ----------
phone = rep(phone,
"String otaBranchName(); String otaChannelName(); void otaChannelCycle(); void otaAddBranchBegin(); void otaAddBranchPoll();",
"String otaBranchName(); String otaChannelName(); void otaChannelCycle(); void otaAddBranchBegin(); void otaAddBranchPoll(); void otaVersionLabel(char* out, int n);",
"prototipe ota")

phone = rep(phone,
"""  char bl[24];
  if(bn > 0) snprintf(bl, sizeof(bl), "Build %d", bn); else snprintf(bl, sizeof(bl), "Lokal");""",
"""  char bl[28];
  if(bn > 0){ char vl[16]; otaVersionLabel(vl, sizeof(vl)); snprintf(bl, sizeof(bl), "%s b%d", vl, bn); }
  else snprintf(bl, sizeof(bl), "Lokal");""", "label build di app Update")

# ---------- about_renphone.ino ----------
about = rep(about,
"""  snprintf(a, sizeof(a), "%s", ABOUT_FW_VERSION);
  aboutAdd("Versi firmware", a);""",
"""  { char vl[16]; otaVersionLabel(vl, sizeof(vl)); int bn = otaBuildNum();
    if (bn > 0) snprintf(a, sizeof(a), "%s (build %d)", vl, bn); else snprintf(a, sizeof(a), "%s", vl); }
  aboutAdd("Versi firmware", a);""", "baris versi firmware")

about = rep(about,
"s.print(ABOUT_FW_VERSION);",
"{ char vl[16]; otaVersionLabel(vl, sizeof(vl)); s.print(vl); }",
"kartu identitas")

# ---------- workflow ----------
wf = rep(wf,
'log_printf gagal diterapkan" && exit 1)\n\n          NOF_INCLUDE="$(pwd)/build_sketch/src"\n',
"""log_printf gagal diterapkan" && exit 1)

          RP_VERSION=dev
          [ -f VERSION ] && RP_VERSION="$(tr -d ' \\r\\n' < VERSION)"
          [ -n "$RP_VERSION" ] || RP_VERSION=dev
          echo "$RP_VERSION" | grep -Eq '^(dev|[0-9]+(\\.[0-9]+)*(-[A-Za-z0-9]+)?)$' || { echo "::error::isi file VERSION tidak valid: $RP_VERSION"; exit 1; }
          echo "Versi: $RP_VERSION"

          NOF_INCLUDE="$(pwd)/build_sketch/src"
""", "NOF_INCLUDE")

wf = rep(wf,
"-DRP_BUILD_NUM=${{ github.run_number }} -DRP_BRANCH=${RP_BRANCH} ",
"-DRP_BUILD_NUM=${{ github.run_number }} -DRP_BRANCH=${RP_BRANCH} -DRP_VERSION=${RP_VERSION} ",
"flag compile")

wf = rep(wf,
"""          MD5="$(md5sum firmware.bin | cut -d' ' -f1)"\n""",
"""          VER=dev; [ -f VERSION ] && VER="$(tr -d ' \\r\\n' < VERSION)"; [ -n "$VER" ] || VER=dev
          MD5="$(md5sum firmware.bin | cut -d' ' -f1)"\n""", "MD5")

wf = rep(wf,
'            echo "build=${{ github.run_number }}"\n',
'            echo "build=${{ github.run_number }}"\n            echo "version=${VER}"\n', "info.txt")

wf = rep(wf,
'          TAG="build-${{ github.run_number }}"\n',
'          VER=dev; [ -f VERSION ] && VER="$(tr -d \' \\r\\n\' < VERSION)"; [ -n "$VER" ] || VER=dev\n          TAG="build-${{ github.run_number }}"\n', "TAG")

wf = rep(wf,
'--title "Firmware build ${{ github.run_number }}"',
'--title "Firmware v${VER} (build ${{ github.run_number }})"', "judul release")

# ---------- tulis ----------
if not os.path.exists("VERSION"):
    open("VERSION", "w", encoding="utf-8").write("1.0.0\n")
    print("VERSION dibuat: 1.0.0")
open(F_OTA, "w", encoding="utf-8").write(ota)
open(F_PHONE, "w", encoding="utf-8").write(phone)
open(F_ABOUT, "w", encoding="utf-8").write(about)
open(F_WF, "w", encoding="utf-8").write(wf)
print("OK")
