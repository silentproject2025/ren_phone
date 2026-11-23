#!/usr/bin/env python3
# WSENSE_V1 -- mendaftarkan app "WiFi Sensing" (firmware/phone/wifisense_renphone.ino) ke phone.ino:
#   enum Screen      <- SCR_WSENSE
#   forward decl     <- wsenseEnter/Exit/Touch/LoopPoll + drawWsense
#   apps[] + COUNT   <- { "WiFi Sensing", 'x', ... } dan APP_COUNT 46 -> 47
#   drawAppIcon()    <- case 'x' (tiga busur WiFi + titik)
#   kategori Home    <- Sistem
#   loop()           <- wsenseLoopPoll() saat app terbuka
# Pakai (dari root repo):  python3 apply_wifisense_patch.py
import sys, shutil

P = "firmware/phone/phone.ino"
s = open(P, encoding="utf-8").read()

if "WSENSE_V1" in s:
    print("Sudah terpasang (phone.ino).")
    sys.exit(0)

def rep(old, new):
    global s
    c = s.count(old)
    if c != 1:
        sys.exit("GAGAL: pola ditemukan %d kali (harus 1):\n%s" % (c, old[:170]))
    s = s.replace(old, new)

shutil.copyfile(P, P + ".bak_wsense")

# 1. enum Screen (selalu ditambah di akhir -> nilai enum lama tidak berubah)
rep("              SCR_NYX };\n",
    "              SCR_NYX,\n"
    "              // WiFi Sensing (CSI: gerak + napas) -- lihat wifisense_renphone.ino  // WSENSE_V1\n"
    "              SCR_WSENSE };\n")

# 2. forward declaration
rep("void aboutLoopPoll();\n",
    "void aboutLoopPoll();\n"
    "// ---- wifisense_renphone.ino: WiFi Sensing (CSI) ----  // WSENSE_V1\n"
    "void wsenseEnter(); void wsenseExit(); void drawWsense(LGFX_Sprite&); void wsenseTouch(int,int,bool,bool);\n"
    "void wsenseLoopPoll();\n")

# 3. apps[] + APP_COUNT
rep('  { "Nyx Apps", \'n\', 0, nyxAppEnter, nyxAppExit, drawNyxApp, nyxAppTouch, SCR_NYX },\n};\n#define APP_COUNT 46\n',
    '  { "Nyx Apps", \'n\', 0, nyxAppEnter, nyxAppExit, drawNyxApp, nyxAppTouch, SCR_NYX },\n'
    '  // ---- WiFi Sensing: deteksi gerak / kehadiran + napas dari CSI WiFi ----  // WSENSE_V1\n'
    '  { "WiFi Sensing", \'x\', 0, wsenseEnter, wsenseExit, drawWsense, wsenseTouch, SCR_WSENSE },\n'
    '};\n#define APP_COUNT 47\n')

# 4. ikon
rep("    case 'i': { // About Phone\n",
    "    case 'x': { // WiFi Sensing: tiga busur + titik  // WSENSE_V1\n"
    "      int rr=max(8,r-6); int by=cy+rr/2;\n"
    "      for(int k=1;k<=3;k++){ int ro=rr*k/3+1, ri=ro-max(2,rr/8); s.fillArc(cx,by,ri,ro,225.0f,315.0f,ic); }\n"
    "      nyxAaCircle(s, cx,by,max(2,rr/8),ic);\n"
    "      break;\n"
    "    }\n"
    "    case 'i': { // About Phone\n")

# 5. kategori Home -> Sistem
rep("    case SCR_MICLEVEL: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE: case SCR_BACKUP:\n      return 5; // Sistem",
    "    case SCR_MICLEVEL: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE: case SCR_BACKUP: case SCR_WSENSE:\n      return 5; // Sistem")

# 6. loop
rep("    if(curScreen()==SCR_ABOUT) aboutLoopPoll();\n",
    "    if(curScreen()==SCR_ABOUT) aboutLoopPoll();\n"
    "    if(curScreen()==SCR_WSENSE) wsenseLoopPoll(); // WSENSE_V1: bin CSI 10 Hz + deteksi gerak/napas\n")

open(P, "w", encoding="utf-8").write(s)
print("OK: WiFi Sensing terpasang. Cadangan: " + P + ".bak_wsense")
print("Build ulang firmware. Jika app menampilkan 'CSI belum aktif di build ini', lihat catatan di wifisense_renphone.ino.")
