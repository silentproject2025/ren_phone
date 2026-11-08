#!/usr/bin/env python3
# apply_backup_patch.py -- memasang fitur BACKUP & RESTORE pengaturan ke phone.ino
#
# Jalankan dari folder repo (yang berisi phone.ino, backup_renphone.ino, backup_core.h):
#     python3 apply_backup_patch.py
#
# Yang diubah:
#   phone.ino                         enum Screen, apps[] (disembunyikan dari Home), ikon app,
#                                     daftar layar "edge-back" & kategori Sistem, baris di Pengaturan
#   .github/workflows/build-firmware.yml   menyalin backup_renphone.ino + backup_core.h ke build_sketch/
# Aman dijalankan ulang (kalau sudah terpasang, dilewati). Kalau ada patokan yang tidak ketemu,
# TIDAK ada file yang diubah sama sekali.
import sys, os

for need in ('phone.ino', 'backup_renphone.ino', 'backup_core.h'):
    if not os.path.exists(need):
        print("ERROR: %s tidak ada di folder ini" % need); sys.exit(1)

s = open('phone.ino', encoding='utf-8').read()
if 'SCR_BACKUP' in s:
    print("Backup & Restore sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: patokan '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# 1) enum Screen
s = rep(s, "              SCR_EBOOK };\nenum Orientation",
        "              SCR_EBOOK,\n              // Backup & Restore pengaturan -- lihat backup_renphone.ino + backup_core.h\n              SCR_BACKUP };\nenum Orientation",
        "enum Screen")

# 2) deklarasi maju (sebelum tabel apps[])
s = rep(s, "void aboutLoopPoll();\n",
        "void aboutLoopPoll();\n// ---- backup_renphone.ino: Backup & Restore pengaturan ----\nvoid backupEnter(); void backupExit(); void drawBackup(LGFX_Sprite&); void backupTouch(int,int,bool,bool);\n",
        "forward decl")

# 3) tabel apps[] + jumlah
s = rep(s, "AppDef apps[43] = {", "AppDef apps[44] = {", "apps[43]")
s = rep(s, '  { "Buku",    \'b\', 0, ebookEnter,  ebookExit,  drawEbook,  ebookTouch,  SCR_EBOOK  },\n};\n#define APP_COUNT 43',
        '  { "Buku",    \'b\', 0, ebookEnter,  ebookExit,  drawEbook,  ebookTouch,  SCR_EBOOK  },\n'
        '  // ---- Backup & Restore: simpan/pulihkan pengaturan ke SD (disembunyikan dari grid Home; ada di Pengaturan) ----\n'
        '  { "Backup",  \'z\', 0x357C, backupEnter, backupExit, drawBackup, backupTouch, SCR_BACKUP },\n};\n#define APP_COUNT 44',
        "apps entry + APP_COUNT")

# 4) disembunyikan dari grid Home secara default (seperti Atur Home)
s = rep(s, "homeHide[i]=(apps[i].screen==SCR_HOMEEDIT); }",
        "homeHide[i]=(apps[i].screen==SCR_HOMEEDIT || apps[i].screen==SCR_BACKUP); }", "homeLayoutDefault")

# 5) ikon app (dipakai di App Switcher dsb.)
s = rep(s, "    case 'i': { // About Phone\n",
        "    case 'z': { // Backup: panah turun ke baki\n"
        "      int rr=max(8,r-6);\n"
        "      s.fillRect(cx-max(1,rr/9),cy-rr*2/3,max(2,rr/4),rr*2/3,ic);\n"
        "      s.fillTriangle(cx-rr/2,cy,cx+rr/2,cy,cx,cy+rr/2,ic);\n"
        "      s.fillRect(cx-rr*2/3,cy+rr*2/3,rr*4/3,max(2,rr/6),ic);\n"
        "      break;\n"
        "    }\n"
        "    case 'i': { // About Phone\n", "drawAppIcon")

# 6) swipe-back dari tepi + kategori Sistem
s = rep(s, "case SCR_NASAIMG: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE:\n    case SCR_CHAT:",
        "case SCR_NASAIMG: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE: case SCR_BACKUP:\n    case SCR_CHAT:",
        "screenHasEdgeBack")
s = rep(s, "    case SCR_MICLEVEL: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE:\n      return 5;",
        "    case SCR_MICLEVEL: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE: case SCR_BACKUP:\n      return 5;",
        "homeCatOfScreen")

# 7) Pengaturan: baris "Backup & Restore" di bagian TENTANG
s = rep(s, "SR_ABOUT, SR_POWER, SR_HOMEEDIT };", "SR_ABOUT, SR_POWER, SR_HOMEEDIT, SR_BACKUP };", "enum SR")
s = rep(s, "  settAddRow(SR_ABOUT,g,34,0,nullptr,y); g++;\n",
        "  settAddRow(SR_ABOUT,g,34,0,nullptr,y);\n  settAddRow(SR_BACKUP,g,34,0,nullptr,y); g++;\n", "settBuildLayout")
s = rep(s, "case SR_TCAL: case SR_ABOUT: case SR_POWER:\n      return true;",
        "case SR_TCAL: case SR_ABOUT: case SR_POWER: case SR_BACKUP:\n      return true;", "settHasChip")
s = rep(s, "    case SR_ABOUT:    col=CHIP_GRAY;   break;\n",
        "    case SR_ABOUT:    col=CHIP_GRAY;   break;\n    case SR_BACKUP:   col=CHIP_TEAL;   break;\n", "settChip warna")
s = rep(s, "    case SR_ABOUT:     // \"i\"\n",
        "    case SR_BACKUP:    // panah turun ke baki\n"
        "      s.fillRect(cx-1,cy-8,3,8,W); s.fillTriangle(cx-5,cy-1,cx+5,cy-1,cx,cy+5,W);\n"
        "      s.fillRect(cx-7,cy+7,15,2,W);\n"
        "      break;\n"
        "    case SR_ABOUT:     // \"i\"\n", "settChip glyph")
s = rep(s, "      case SR_ABOUT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"About Phone\");",
        "      case SR_BACKUP:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"Backup & Restore\");\n"
        "        settChevron(s,xr-4,ty+4,T().subtext);\n        break;\n"
        "      case SR_ABOUT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"About Phone\");", "draw SR_ABOUT")
s = rep(s, "    case SR_ABOUT:\n      navPush(SCR_ABOUT); break;",
        "    case SR_ABOUT:\n      navPush(SCR_ABOUT); break;\n    case SR_BACKUP:\n      navPush(SCR_BACKUP); break;", "touch SR_ABOUT")

# 8) workflow GitHub Actions
wf = '.github/workflows/build-firmware.yml'
w = None
if os.path.exists(wf):
    w = open(wf, encoding='utf-8').read()
    if 'backup_renphone.ino' in w:
        w = None
    else:
        a = "          cp gutenberg_renphone.ino build_sketch/\n"
        if w.count(a) == 1:
            w = w.replace(a, a + "          cp backup_renphone.ino build_sketch/\n          cp backup_core.h build_sketch/\n")
        else:
            print("PERINGATAN: tambahkan 'cp backup_renphone.ino build_sketch/' dan 'cp backup_core.h build_sketch/' ke workflow sendiri")
            w = None
else:
    print("PERINGATAN: workflow tidak ditemukan; salin backup_renphone.ino + backup_core.h ke sketch sendiri")

open('phone.ino', 'w', encoding='utf-8').write(s)
if w:
    open(wf, 'w', encoding='utf-8').write(w)
print("OK: Backup & Restore terpasang (Pengaturan > TENTANG > Backup & Restore)")
