#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_ota_channel_patch.py -- pasang PEMILIH BRANCH + PENGAMAN UPDATE ke phone.ino
#
# Jalankan dari folder repo (yang ada phone.ino + ota_check_renphone.ino BARU):
#     python3 apply_ota_channel_patch.py
#
# Idempoten (kalau sudah terpasang, dilewati). Semua-atau-tidak-sama-sekali:
# kalau ada anchor yang tidak ketemu persis 1x, TIDAK ADA yang diubah.
import sys, os

if not os.path.exists('phone.ino') or not os.path.exists('ota_check_renphone.ino'):
    print("ERROR: phone.ino / ota_check_renphone.ino tidak ada di folder ini"); sys.exit(1)
if 'RP_BASE_REV' not in open('ota_check_renphone.ino', encoding='utf-8').read():
    print("ERROR: ota_check_renphone.ino masih versi lama. Timpa dulu dgn versi baru."); sys.exit(1)

s = open('phone.ino', encoding='utf-8').read()
if 'otaGuardBoot' in s:
    print("Pemilih branch + pengaman update sudah terpasang di phone.ino, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n))
        sys.exit(1)
    return src.replace(old, new)

# 1. include: tipe esp_partition_t harus dikenal SEBELUM blok prototype otomatis arduino-cli
s = rep(s, "#include <Update.h>   // OTA: flashing firmware baru (dari SD lokal atau WiFi)\n",
           "#include <Update.h>   // OTA: flashing firmware baru (dari SD lokal atau WiFi)\n"
           "#include <esp_ota_ops.h> // OTA guard/rollback (ota_check_renphone.ino): tipe esp_partition_t dipakai di prototype\n",
        "include Update.h")

# 2. forward declarations
s = rep(s, "void drawUpdate(LGFX_Sprite&); void updTouch(int,int,bool,bool);\n",
           "void drawUpdate(LGFX_Sprite&); void updTouch(int,int,bool,bool);\n"
           "// ---- ota_check_renphone.ino: channel (branch) + pengaman boot/rollback ----\n"
           "void otaGuardBoot(); void otaGuardPoll(); void otaGuardArm();\n"
           "String otaBranchName(); String otaChannelName(); void otaChannelCycle(); void otaAddBranchBegin(); void otaAddBranchPoll();\n"
           "bool otaRollbackReady(); void otaRollbackRefresh(); bool otaRollbackNow(bool);\n"
           "void otaHdrReset(); bool otaHdrFeed(const uint8_t*,size_t,size_t); bool otaPreInstall(const String&,int,String&); void otaApplyExpect(const String&);\n",
        "forward decl")

# 3. setup(): pengaman boot PALING AWAL
s = rep(s, "void setup(){\n  Serial.begin(115200);\n",
           "void setup(){\n  Serial.begin(115200);\n"
           "  otaGuardBoot(); // PALING AWAL: pengaman update -- rollback otomatis kalau firmware baru tak kunjung sehat\n",
        "setup()")

# 4. loop(): tandai sehat setelah 30 detik (di luar if-else chain per layar -> jalan juga di Home/Lock)
s = rep(s, "  rcLoopPoll(); // Remote Control: aksi tombol, status penonton, keep-awake\n",
           "  otaGuardPoll(); // OTA guard: tandai firmware sehat setelah hidup 30 dtk (+ toast kalau barusan di-rollback)\n"
           "  rcLoopPoll(); // Remote Control: aksi tombol, status penonton, keep-awake\n",
        "loop rcLoopPoll")

# 5. updShowResult sukses -> tandai firmware baru "pending"
s = rep(s, "  if(success){\n    display.fillCircle(cx, cy, 28, 0x07F5);\n",
           "  if(success){\n    otaGuardArm(); // firmware baru ditandai 'pending' -> otaGuardBoot()/otaGuardPoll() di boot berikutnya\n"
           "    display.fillCircle(cx, cy, 28, 0x07F5);\n",
        "updShowResult success")

# 6a. URL: ukuran harus cocok dgn info channel
s = rep(s, "    updShowResult(false, \"Server tidak mengirim Content-Length\");\n    return false;\n  }\n",
           "    updShowResult(false, \"Server tidak mengirim Content-Length\");\n    return false;\n  }\n"
           "  { String otaWhy; if(!otaPreInstall(url, contentLen, otaWhy)){ http.end(); updShowResult(false, otaWhy); return false; } }\n",
        "contentLen")
# 6b. URL: MD5 yang diharapkan + reset penampung header
s = rep(s, "  WiFiClient* stream = http.getStreamPtr();\n  WiFi.setSleep(false);",
           "  otaApplyExpect(url); otaHdrReset(); // MD5 dari info channel (kalau ada) + penampung 36 byte header image\n"
           "  WiFiClient* stream = http.getStreamPtr();\n  WiFi.setSleep(false);",
        "stream ptr")
# 6c. URL: header image harus image ESP32-S3 valid sebelum menulis potongan yg melengkapinya
s = rep(s, "      if(n > 0){\n        if(Update.write(buf, n) != (size_t)n){\n",
           "      if(n > 0){\n"
           "        if(!otaHdrFeed(buf, (size_t)n, written)){\n"
           "          Update.abort(); http.end();\n"
           "          updShowResult(false, \"Header bukan image ESP32-S3 yg valid. Dibatalkan, firmware lama aman.\");\n"
           "          return false;\n"
           "        }\n"
           "        if(Update.write(buf, n) != (size_t)n){\n",
        "url write loop")

# 7. SD: reset + cek header
s = rep(s, "  uint8_t buf[4096];\n  size_t written = 0;\n  updLastPct = -1; updBarFill = 0;\n  while(f.available()){\n",
           "  uint8_t buf[4096];\n  size_t written = 0;\n  updLastPct = -1; updBarFill = 0;\n  otaHdrReset();\n  while(f.available()){\n",
        "sd loop start")
s = rep(s, "    if(Update.write(buf, n) != n){\n      Update.abort();\n      f.close();\n",
           "    if(!otaHdrFeed(buf, n, written)){\n"
           "      Update.abort(); f.close();\n"
           "      updShowResult(false, \"File .bin bukan image ESP32-S3 yg valid. Dibatalkan, firmware lama aman.\");\n"
           "      return false;\n"
           "    }\n"
           "    if(Update.write(buf, n) != n){\n      Update.abort();\n      f.close();\n",
        "sd write")

# 8. updEnter: hitung ketersediaan rollback
s = rep(s, "void updEnter(){\n  updScanFiles();\n",
           "void updEnter(){\n  otaRollbackRefresh(); // slot OTA lain berisi firmware sebelumnya yg valid?\n  updScanFiles();\n",
        "updEnter")

# 9. makro layout baris channel
s = rep(s, "#define UPD_BTN_H        24\n",
           "#define UPD_BTN_H        24\n"
           "// baris channel (di atas kolom URL WiFi): [pill channel] [+ tambah branch] [rollback]\n"
           "#define UPD_CH_Y         (UPD_WIFI_LABEL_Y-4)\n"
           "#define UPD_CH_H         15\n"
           "#define UPD_RB_W         26\n"
           "#define UPD_ADD_W        22\n"
           "#define UPD_CHP_W        108\n"
           "#define UPD_RB_X         (SCR_W-6-UPD_RB_W)\n"
           "#define UPD_ADD_X        (UPD_RB_X-4-UPD_ADD_W)\n"
           "#define UPD_CHP_X        (UPD_ADD_X-4-UPD_CHP_W)\n",
        "UPD_BTN_H")

# 10. helper gambar baris channel + poll di drawUpdate + info branch di kartu atas
helper = '''static void updDrawChanRow(LGFX_Sprite& s){
  // pill channel (ketuk = ganti branch sumber firmware)
  iosRR(s, UPD_CHP_X, UPD_CH_Y, UPD_CHP_W, UPD_CH_H, 6, T().surface2);
  String cn = otaChannelName();
  int maxC = (UPD_CHP_W-18)/6;
  if((int)cn.length() > maxC) cn = cn.substring(0, maxC-2) + "..";
  s.setTextColor(T().accent);
  s.setCursor(UPD_CHP_X+6, UPD_CH_Y+4); s.print(cn.c_str());
  s.setTextColor(T().subtext);
  s.setCursor(UPD_CHP_X+UPD_CHP_W-10, UPD_CH_Y+4); s.print(">");
  // "+" tambah branch
  iosRR(s, UPD_ADD_X, UPD_CH_Y, UPD_ADD_W, UPD_CH_H, 6, T().surface2);
  s.setTextColor(T().accent);
  updCenterText(s, UPD_ADD_X, UPD_ADD_W, UPD_CH_Y+4, "+");
  // panah balik = kembalikan ke firmware sebelumnya (slot OTA lain)
  bool rbOk   = otaRollbackReady();
  bool rbPend = (updPendingConfirm == "rb") && millis()<=updPendingUntil;
  uint16_t rbBg = rbPend ? T().danger : T().surface2;
  uint16_t rbFg = rbPend ? T().bg : (rbOk ? T().text : T().divider);
  iosRR(s, UPD_RB_X, UPD_CH_Y, UPD_RB_W, UPD_CH_H, 6, rbBg);
  int acx = UPD_RB_X+UPD_RB_W/2, acy = UPD_CH_Y+UPD_CH_H/2;
  s.fillTriangle(acx-7, acy, acx-2, acy-4, acx-2, acy+4, rbFg);
  s.fillRect(acx-2, acy-1, 8, 3, rbFg);
}

void drawUpdate(LGFX_Sprite& s){
  otaAddBranchPoll(); // simpan branch baru begitu keyboard tertutup
'''
s = rep(s, "void drawUpdate(LGFX_Sprite& s){\n", helper, "drawUpdate()")

s = rep(s, "  s.setTextColor(stCol); s.setCursor(46, UPD_HERO_Y+7); s.print(line);\n",
           "  String heroL = String(line);\n"
           "  { String hb = String(\" [\") + otaBranchName() + \"]\"; // branch firmware yg sedang jalan (kalau muat)\n"
           "    int maxHc = (UPD_HB_X - 46 - 4) / 6;\n"
           "    if((int)(heroL.length()+hb.length()) <= maxHc) heroL += hb; }\n"
           "  s.setTextColor(stCol); s.setCursor(46, UPD_HERO_Y+7); s.print(heroL.c_str());\n",
        "hero line")

s = rep(s, "  s.setTextColor(T().subtext); s.setCursor(10, UPD_WIFI_LABEL_Y); s.print(\"DARI WIFI  -  URL firmware.bin\");\n",
           "  s.setTextColor(T().subtext); s.setCursor(10, UPD_WIFI_LABEL_Y); s.print(\"DARI WIFI\");\n"
           "  updDrawChanRow(s);\n",
        "wifi label")

# 11. updTouch: poll + baris channel
s = rep(s, "void updTouch(int x,int y,bool held,bool isNew){\n  if(updBusy) return;\n",
           "void updTouch(int x,int y,bool held,bool isNew){\n  if(updBusy) return;\n  otaAddBranchPoll();\n",
        "updTouch head")
s = rep(s, "  if(y>=UPD_URL_Y && y<=UPD_URL_Y+UPD_URL_H){\n    kbTarget=&updUrl;",
           "  // baris channel: [pill channel] [+ tambah branch] [rollback]\n"
           "  if(y>=UPD_CH_Y-3 && y<=UPD_CH_Y+UPD_CH_H+1 && x>=UPD_CHP_X-2){\n"
           "    if(x>=UPD_RB_X-2){\n"
           "      if(!otaRollbackReady()){ showToast(\"Tidak ada versi sebelumnya di slot lain\", 2200); return; }\n"
           "      if(updCheckConfirm(\"rb\")) otaRollbackNow(true);\n"
           "    } else if(x>=UPD_ADD_X-2){ otaAddBranchBegin(); }\n"
           "    else { otaChannelCycle(); }\n"
           "    return;\n"
           "  }\n\n"
           "  if(y>=UPD_URL_Y && y<=UPD_URL_Y+UPD_URL_H){\n    kbTarget=&updUrl;",
        "updTouch url")

open('phone.ino', 'w', encoding='utf-8').write(s)
print("OK: pemilih branch + pengaman update terpasang di phone.ino")
