#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_owner_patch.py -- pasang fitur PROFIL PEMILIK (foto + nama) ke repo ren_phone (NyxOS).

Cara pakai (Termux, dari folder repo ren_phone):
    python3 apply_owner_patch.py

Syarat:
  * owner_renphone.ino ada di folder yang sama dengan phone.ino
  * apply_gallery_patch.py sudah pernah dijalankan (app Galeri ada)
  * apply_about_patch.py boleh sudah / belum -- skrip ini menyesuaikan jumlah app sendiri

Yang diubah:
  phone.ino              : enum layar, apps[] (+1 app "Profil"), warna & ikon, deklarasi,
                           kartu pemilik di Pengaturan, hook loop()
  gallery_renphone.ino   : mode pilih foto profil + tombol "Profil" di viewer
  about_renphone.ino     : foto/nama pemilik di kartu About Phone (kalau file-nya ada),
                           sekalian naikkan ABOUT_MAX_ROWS (16 -> 24; baris terakhir bisa kepotong)
  .github/workflows/build-firmware.yml : cp owner_renphone.ino

AMAN dijalankan ulang. Kalau ada anchor yang tidak ketemu tepat 1x, skrip BERHENTI
tanpa menulis apa pun ke berkas yang bermasalah.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def exists(p):
    return os.path.exists(os.path.join(ROOT, p))


def read(p):
    with open(os.path.join(ROOT, p), "r", encoding="utf-8") as f:
        return f.read()


def write(p, s):
    with open(os.path.join(ROOT, p), "w", encoding="utf-8", newline="\n") as f:
        f.write(s)


def rep(text, old, new, label):
    n = text.count(old)
    if n != 1:
        sys.exit("GAGAL [%s]: anchor ketemu %d kali (harus tepat 1).\n--- anchor ---\n%s" % (label, n, old))
    return text.replace(old, new, 1)


def rep_re(text, pattern, repl, label, flags=0):
    ms = list(re.finditer(pattern, text, flags))
    if len(ms) != 1:
        sys.exit("GAGAL [%s]: pola ketemu %d kali (harus tepat 1).\n--- pola ---\n%s" % (label, len(ms), pattern))
    m = ms[0]
    return text[:m.start()] + m.expand(repl) + text[m.end():]


FWD_DECL = (
    "// ---- owner_renphone.ino: Profil pemilik (foto + nama) ----\n"
    "void ownerEnter(); void ownerExit(); void drawOwner(LGFX_Sprite&); void ownerTouch(int,int,bool,bool);\n"
    "void ownerLoopPoll(); bool ownerHasAvatar(); const char* ownerName();\n"
    "void ownerDrawAvatar(LGFX_Sprite&,int,int,int);\n"
    "bool ownerPickActive(); void ownerPickReset(); void ownerPickChosen(const char*);\n"
    "void ownerSetAvatarUi(const char*);\n"
)

APP_ENTRY = ("  // ---- Profil: foto & nama pemilik ----\n"
             "  { \"Profil\",     'o', 0, ownerEnter,   ownerExit,    drawOwner,        ownerTouch,    SCR_OWNER },\n")

ICON_CASE = (
    "    case 'o': { // Profil: kepala + bahu\n"
    "      int rr=max(8,r-6);\n"
    "      s.fillCircle(cx,cy-rr/3,max(3,rr/3),ic);\n"
    "      s.fillRoundRect(cx-rr*2/3,cy+rr/8,rr*4/3,rr*2/3,rr/3,ic);\n"
    "      break;\n"
    "    }\n"
)


def patch_phone():
    p = "phone.ino"
    s = read(p)
    if "SCR_OWNER" in s:
        print("phone.ino: sudah dipatch, dilewati")
        return
    if not exists("owner_renphone.ino"):
        sys.exit("owner_renphone.ino tidak ada di folder ini.")

    m = re.search(r"#define APP_COUNT (\d+)", s)
    if not m:
        sys.exit("GAGAL: #define APP_COUNT tidak ketemu.")
    n = int(m.group(1))          # jumlah app sekarang; app baru = indeks n

    # ---- enum Screen: sisipkan sebelum penutup enum ----
    s = rep_re(s, r"\n(\s+)(SCR_\w+) \};\nenum Orientation",
               r"\n\1\2,\n\1// Profil pemilik (foto + nama) -- lihat owner_renphone.ino\n\1SCR_OWNER };\nenum Orientation",
               "enum Screen")

    # ---- deklarasi maju + ukuran apps[] ----
    s = rep(s, "\nAppDef apps[%d] = {" % n,
            "\n" + FWD_DECL + "\nAppDef apps[%d] = {" % (n + 1), "apps decl")

    # ---- entri apps[] ----
    s = rep(s, "};\n#define APP_COUNT %d" % n,
            APP_ENTRY + "};\n#define APP_COUNT %d" % (n + 1), "apps entry")

    # ---- warna ikon ----
    s = rep_re(s, r"(  apps\[%d\]\.color=[^\n]*\n)\}\n" % (n - 1),
               r"\1  apps[%d].color=0x5EBF; // Profil - biru muda\n}\n" % n, "app color")

    # ---- ikon app 'o' ----
    s = rep(s,
            "    default: {\n      s.setTextColor(ic); s.setTextSize(2);\n      char b[2]={sym,0};",
            ICON_CASE + "    default: {\n      s.setTextColor(ic); s.setTextSize(2);\n      char b[2]={sym,0};",
            "app icon")

    # ---- Pengaturan: tipe baris + layout + gambar + ketuk ----
    s = rep(s, "SR_MPU, SR_TCAL };", "SR_MPU, SR_TCAL, SR_OWNER };", "SR enum")
    s = rep(s, "  settAddRow(SR_TITLE,0,36,0,\"Pengaturan\",y);\n",
            "  settAddRow(SR_TITLE,0,36,0,\"Pengaturan\",y);\n"
            "  settAddRow(SR_OWNER,g,60,0,nullptr,y); g++;   // kartu pemilik (foto + nama)\n",
            "settings layout")
    s = rep(s,
            "      case SR_TCAL:\n        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(\"Kalibrasi Layar Sentuh\");",
            "      case SR_OWNER: {\n"
            "        int ar=22, acy=ry+rh/2, acx=xl+ar-4;\n"
            "        ownerDrawAvatar(s,acx,acy,ar);\n"
            "        int tx0=acx+ar+10;\n"
            "        s.setTextSize(2);\n"
            "        String onm=settFit(s,String(ownerName()),xr-18-tx0);\n"
            "        uiText(s,tx0,acy-14,onm.c_str(),T().text,false,true);\n"
            "        s.setTextSize(1);\n"
            "        s.setTextColor(T().subtext); s.setCursor(tx0,acy+6); s.print(\"Ketuk untuk atur profil\");\n"
            "        settChevron(s,xr-4,acy,T().subtext);\n"
            "        } break;\n"
            "      case SR_TCAL:\n        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(\"Kalibrasi Layar Sentuh\");",
            "settings draw")
    s = rep(s,
            "    case SR_MPU:\n      if(mpuReady){ calibrateMPU(); needRedraw=true; }",
            "    case SR_OWNER:\n      navPush(SCR_OWNER); break;\n"
            "    case SR_MPU:\n      if(mpuReady){ calibrateMPU(); needRedraw=true; }",
            "settings tap")

    # ---- hook loop() ----
    anchor = "    if(curScreen()==SCR_HWMON && millis()-hwmonLastPoll>500){ hwmonPoll(); needRedraw=true; hwmonLastPoll=millis(); }"
    if s.count(anchor) != 1:
        sys.exit("GAGAL [loop hook]: anchor HWMON ketemu %d kali (harus 1)." % s.count(anchor))
    i = s.index(anchor)
    eol = s.index("\n", i) + 1
    s = s[:eol] + "    ownerLoopPoll(); // Profil: tutup Galeri setelah foto profil dipilih\n" + s[eol:]

    write(p, s)
    print("phone.ino: OK (APP_COUNT %d -> %d)" % (n, n + 1))


def patch_gallery():
    p = "gallery_renphone.ino"
    if not exists(p):
        sys.exit(p + " tidak ada -- jalankan apply_gallery_patch.py dulu.")
    s = read(p)
    if "ownerPickActive" in s:
        print(p + ": sudah dipatch, dilewati")
        return

    # 1) mode pilih: ketuk foto di grid
    s = rep(s,
            "  } else if (galItems[idx].kind == 2) {\n    char p[GAL_PATH_MAX];\n    galItemPath(p, sizeof(p), idx);\n",
            "  } else if (ownerPickActive()) {                 // mode pilih foto profil (dari layar Profil)\n"
            "    if (galItems[idx].kind == 2) { showToast(\"Pilih foto, bukan video\"); }\n"
            "    else { char op[GAL_PATH_MAX]; galItemPath(op, sizeof(op), idx); ownerPickChosen(op); return; }\n"
            "  } else if (galItems[idx].kind == 2) {\n    char p[GAL_PATH_MAX];\n    galItemPath(p, sizeof(p), idx);\n",
            "gallery tap grid")

    # 2) viewer: fungsi + tombol "Profil"
    s = rep(s, "static void galViewTap(int x, int y) {",
            "// Jadikan foto yang sedang dibuka sebagai foto profil (tombol \"Profil\" di bar bawah viewer)\n"
            "static void galViewSetOwner() {\n"
            "  if (galViewIdx < 0 || galViewIdx >= galItemCount) return;\n"
            "  if (galItems[galViewIdx].kind == 2) { showToast(\"Video tidak bisa jadi foto profil\"); return; }\n"
            "  char op[GAL_PATH_MAX];\n"
            "  galItemPath(op, sizeof(op), galViewIdx);\n"
            "  ownerSetAvatarUi(op);\n"
            "}\n\n"
            "static void galViewTap(int x, int y) {", "gallery view fn")
    s = rep(s,
            "  if (x >= SCR_W - 64 && y >= backY() - 2) { galViewDelete(); return; }",
            "  if (x >= SCR_W - 128 && x < SCR_W - 66 && y >= backY() - 2) { galViewSetOwner(); return; }\n"
            "  if (x >= SCR_W - 64 && y >= backY() - 2) { galViewDelete(); return; }", "gallery view tap")
    s = rep(s,
            "    s.setCursor(SCR_W - 64 + 30 - s.textWidth(l) / 2, backY() + 8); s.print(l);\n",
            "    s.setCursor(SCR_W - 64 + 30 - s.textWidth(l) / 2, backY() + 8); s.print(l);\n"
            "    iosRR(s, SCR_W - 128, backY(), 60, BACK_H, 8, 0x4208);\n"
            "    s.setTextColor(T().accent);\n"
            "    s.setCursor(SCR_W - 128 + 30 - s.textWidth(\"Profil\") / 2, backY() + 8); s.print(\"Profil\");\n",
            "gallery view button")

    # 3) keluar Galeri -> mode pilih selesai
    s = rep(s, "void galExit() {\n  galScanAbort();",
            "void galExit() {\n  ownerPickReset();\n  galScanAbort();", "gallery exit")

    write(p, s)
    print(p + ": OK")


def patch_about():
    p = "about_renphone.ino"
    if not exists(p):
        print(p + ": tidak ada, dilewati (About Phone belum dipasang)")
        return
    s = read(p)
    changed = False
    if "ABOUT_MAX_ROWS     16" in s:
        s = s.replace("ABOUT_MAX_ROWS     16", "ABOUT_MAX_ROWS     24", 1)
        changed = True
    if "ownerDrawAvatar" not in s:
        old_logo = (
            "  s.fillCircle(lx, ly, lr, T().bg);\n"
            "  s.drawCircle(lx, ly, lr, T().accent);\n"
            "  s.drawCircle(lx, ly, lr - 3, T().accent2);\n"
            "  s.fillCircle(lx, ly, 5, T().bg);\n"
            "  s.fillEllipse(lx, ly, lr + 4, 3, T().accent);     // cincin miring\n"
            "  s.fillCircle(lx, ly, 4, T().bg);\n")
        new_logo = (
            "  if (ownerHasAvatar()) {\n"
            "    ownerDrawAvatar(s, lx, ly, lr + 4);              // foto pemilik menggantikan logo\n"
            "  } else {\n"
            "    s.fillCircle(lx, ly, lr, T().bg);\n"
            "    s.drawCircle(lx, ly, lr, T().accent);\n"
            "    s.drawCircle(lx, ly, lr - 3, T().accent2);\n"
            "    s.fillCircle(lx, ly, 5, T().bg);\n"
            "    s.fillEllipse(lx, ly, lr + 4, 3, T().accent);   // cincin miring\n"
            "    s.fillCircle(lx, ly, 4, T().bg);\n"
            "  }\n")
        s = rep(s, old_logo, new_logo, "about logo")
        s = rep(s, "  aboutAdd(\"Sistem\", ABOUT_OS_NAME);\n",
                "  aboutAdd(\"Sistem\", ABOUT_OS_NAME);\n  aboutAdd(\"Pemilik\", ownerName());\n", "about owner row")
        changed = True
    if changed:
        write(p, s)
        print(p + ": OK")
    else:
        print(p + ": sudah dipatch, dilewati")


def patch_workflow():
    p = ".github/workflows/build-firmware.yml"
    if not exists(p):
        print(p + ": tidak ada, dilewati (tambahkan 'cp owner_renphone.ino build_sketch/' manual)")
        return
    s = read(p)
    if "owner_renphone.ino" in s:
        print(p + ": sudah dipatch, dilewati")
        return
    if "          cp about_renphone.ino build_sketch/\n" in s:
        a = "          cp about_renphone.ino build_sketch/\n"
    else:
        a = "          cp gallery_renphone.ino build_sketch/\n"
    s = rep(s, a, a + "          cp owner_renphone.ino build_sketch/\n", "workflow cp")
    write(p, s)
    print(p + ": OK")


if __name__ == "__main__":
    # tiap berkas dipatch terpisah; kalau satu anchor gagal, berkas itu tidak ditulis
    patch_phone()
    patch_gallery()
    patch_about()
    patch_workflow()
    print("Selesai. Commit & push, lalu build.")
