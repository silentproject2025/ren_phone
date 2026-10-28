#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_camera_patch.py -- pasang app "Kamera" (OV2640 di ESP32-CAM) ke seluruh proyek.
#
# Jalankan dari folder repo (harus ada phone.ino, musicbt_renphone.ino, camera_renphone.ino,
# cam_rx.h, cam_avi.h, cam_bt_coprocessor/cam_module.h, cam_bt_coprocessor/cam_tx.h):
#     python3 apply_camera_patch.py
# Idempoten & semua-atau-tidak-sama-sekali (tiap anchor harus ketemu TEPAT 1x), pola sama dgn apply_call_patch.py.
import sys, os

NEED = ('phone.ino', 'musicbt_renphone.ino', 'camera_renphone.ino', 'cam_rx.h', 'cam_avi.h', 'rplink.h',
        'cam_bt_coprocessor/cam_bt_coprocessor.ino', 'cam_bt_coprocessor/cam_module.h', 'cam_bt_coprocessor/cam_tx.h',
        'cam_bt_coprocessor/rplink.h')
for f in NEED:
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

def rd(p): return open(p, encoding='utf-8').read()
def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

out = {}

RPLINK_BLOCK = '// ---- Kamera OV2640 di ESP32-CAM (app "Kamera": camera_renphone.ino <-> cam_module.h) ----\n// S3 -> CAM\n#define RP_CAM_OPEN   \'J\'   // payload: [xclk MHz][res RPR_*][quality][flags b0=langsung stream]. Nyalakan kamera\n#define RP_CAM_CLOSE  \'j\'   // matikan kamera & lepas RAM\n#define RP_CAM_SET    \'k\'   // payload: [id RPC_*][nilai int8]\n#define RP_CAM_SHOT   \'O\'   // payload: [res RPR_*][quality][flash 0..255][flags]. Foto 1x resolusi tinggi\n#define RP_CAM_ACK    \'Q\'   // payload: [seq]. S3 sudah mengambil 1 frame preview (kredit +1)\n#define RP_CAM_FLASH  \'N\'   // payload: [0..255] LED flash GPIO4 (PWM)\n#define RP_CAM_BAUD   \'H\'   // payload: [u32 LE baud]. CAM menjawab RP_CAM_BAUDOK lalu pindah kecepatan\n// CAM -> S3\n#define RP_CAM_FRAME  \'M\'   // payload: [seq][flags b0 awal b1 akhir b2 foto][u32 LE offset][data...]\n#define RP_CAM_STAT   \'m\'   // 12 byte (lihat camSendStat di cam_module.h)\n#define RP_CAM_BAUDOK \'h\'   // payload: [u32 LE baud]\n\n// kode resolusi (stabil; CAM memetakannya ke framesize_t milik library kamera)\n#define RPR_QQVGA 0   // 160x120\n#define RPR_QVGA  1   // 320x240\n#define RPR_CIF   2   // 400x296\n#define RPR_HVGA  3   // 480x320\n#define RPR_VGA   4   // 640x480\n#define RPR_SVGA  5   // 800x600\n#define RPR_XGA   6   // 1024x768\n#define RPR_HD    7   // 1280x720\n#define RPR_SXGA  8   // 1280x1024\n#define RPR_UXGA  9   // 1600x1200\n#define RPR_QXGA  10  // 2048x1536 (hanya sensor 3MP: OV3660/OV5640)\n\n// id untuk RP_CAM_SET\n#define RPC_BRIGHT   0    // -2..2\n#define RPC_CONTRAST 1    // -2..2\n#define RPC_SATUR    2    // -2..2\n#define RPC_SHARP    3    // -2..2\n#define RPC_WBMODE   4    // 0 auto, 1 cerah, 2 mendung, 3 kantor, 4 rumah\n#define RPC_EFFECT   5    // 0 normal, 1 negatif, 2 abu-abu, 3 merah, 4 hijau, 5 biru, 6 sepia\n#define RPC_HMIRROR  6    // 0/1\n#define RPC_VFLIP    7    // 0/1\n#define RPC_NIGHT    8    // 0/1 mode malam (AEC2 + gain tinggi)\n#define RPC_LENC     9    // 0/1 koreksi lensa\n#define RPC_AELEVEL  10   // -2..2\n#define RPC_PRVRES   11   // resolusi preview (RPR_*)\n#define RPC_PRVQ     12   // kualitas JPEG preview (4..63, makin kecil makin bagus)\n#define RPC_FPSCAP   13   // 0 = secepat mungkin, selain itu batas fps\n#define RPC_COLORBAR 14   // 0/1 pola uji\n#define RPC_COUNT    32\n\n'

# ---------- rplink.h (HARUS identik di dua sisi) ----------
for rp in ('rplink.h', 'cam_bt_coprocessor/rplink.h'):
    r = rd(rp)
    if 'RP_CAM_OPEN' in r: print("%s sudah punya protokol kamera, dilewati" % rp)
    else: out[rp] = rep(r, "struct RpParser {", RPLINK_BLOCK + "struct RpParser {", "rplink " + rp)

# ---------- phone.ino ----------
s = rd('phone.ino')
if 'camLoopPoll' in s:
    print("App Kamera sudah terdaftar di phone.ino, dilewati")
else:
    s = rep(s, "              SCR_CALL };\nenum Orientation",
               "              SCR_CALL,\n              // Kamera OV2640 (ESP32-CAM) -- lihat camera_renphone.ino\n              SCR_CAMERA };\nenum Orientation", "enum Screen")
    s = rep(s, "bool callOverlayActive(); void drawCallOverlay(LGFX_Sprite&); void callOverlayTouch(bool,int,int);\n",
               "bool callOverlayActive(); void drawCallOverlay(LGFX_Sprite&); void callOverlayTouch(bool,int,int);\n"
               "// ---- Kamera: forward declarations (camera_renphone.ino) ----\n"
               "void camEnter(); void camExit(); void drawCamera(LGFX_Sprite&); void camTouch(int,int,bool,bool); void camLoopPoll();\n", "forward decl")
    s = rep(s, "AppDef apps[41] = {", "AppDef apps[42] = {", "apps[41]")
    s = rep(s, "#define APP_COUNT 41", "#define APP_COUNT 42", "APP_COUNT")
    s = rep(s, "  { \"Telepon\", 'p', 0, callEnter,   callExit,   drawCall,   callTouch,   SCR_CALL   },\n};",
               "  { \"Telepon\", 'p', 0, callEnter,   callExit,   drawCall,   callTouch,   SCR_CALL   },\n"
               "  { \"Kamera\",  'k', 0, camEnter,    camExit,    drawCamera, camTouch,    SCR_CAMERA },\n};", "apps[] entry")
    s = rep(s, "  apps[40].color=0x2E8B; // Telepon - hijau\n",
               "  apps[40].color=0x2E8B; // Telepon - hijau\n  apps[41].color=0xF9A0; // Kamera - oranye\n", "warna")
    s = rep(s, "    case 'p': { // Telepon: gagang telepon\n",
               "    case 'k': { // Kamera: badan + tonjolan atas + lensa\n"
               "      int bw=max(14,r+4), bh=max(10,r-2);\n"
               "      s.fillRoundRect(cx-bw/2,cy-bh/2+2,bw,bh,3,ic);\n"
               "      s.fillRoundRect(cx-bw/6,cy-bh/2-1,bw/3,4,1,ic);\n"
               "      s.fillCircle(cx,cy+2,max(3,bh/3),bgCircle);\n"
               "      s.drawCircle(cx,cy+2,max(2,bh/5),ic);\n"
               "      break;\n"
               "    }\n"
               "    case 'p': { // Telepon: gagang telepon\n", "ikon")
    s = rep(s, "  callLoopPoll(); // Telepon: panggilan masuk, dering, notifikasi, siaga (no-op kalau belum dipakai)\n",
               "  callLoopPoll(); // Telepon: panggilan masuk, dering, notifikasi, siaga (no-op kalau belum dipakai)\n"
               "  camLoopPoll();  // Kamera: ambil frame dari CAM, foto/rekam/gerak (no-op kalau app tertutup)\n", "loop poll")
    out['phone.ino'] = s

# ---------- musicbt_renphone.ino: hook penerimaan frame kamera + baca blok ----------
m = rd('musicbt_renphone.ino')
if 'camRxFrame' in m:
    print("musicbt sudah punya hook kamera, dilewati")
else:
    m = rep(m, "    int avail = Serial1.available();\n    while (avail-- > 0) {\n      int c = Serial1.read();\n      if (c < 0) break;\n      musRxBytes++;\n      if (!P.feed((uint8_t)c)) { musRxErr = P.errors; continue; }\n      musRxFrames++;\n      switch (P.type) {\n",
               "    // v-kamera: baca per BLOK (bukan per byte) -- di 2-3 Mbaud, Serial1.read() per byte menghabiskan CPU\n"
               "    static uint8_t rxb[256];\n"
               "    int avail = Serial1.available();\n"
               "    while (avail > 0) {\n"
               "     int got = Serial1.readBytes(rxb, avail > 256 ? 256 : avail);\n"
               "     if (got <= 0) break;\n"
               "     avail -= got;\n"
               "     for (int bi = 0; bi < got; bi++) {\n"
               "      int c = rxb[bi];\n"
               "      musRxBytes++;\n"
               "      if (!P.feed((uint8_t)c)) { musRxErr = P.errors; continue; }\n"
               "      musRxFrames++;\n"
               "      camNoteRx();   // frame valid dari CAM (dipakai verifikasi baud tinggi app Kamera)\n"
               "      switch (P.type) {\n", "musTask RX head")
    m = rep(m, "        default: break;\n      }\n    }\n    if (musCamAlive && millis() - musLastStatusMs > 1500) musCamAlive = false;\n",
               "        default: camRxFrame(P.type, P.payload, P.len); break;   // RP_CAM_* -> camera_renphone.ino\n      }\n     }\n    }\n    if (musCamAlive && millis() - musLastStatusMs > 1500) musCamAlive = false;\n", "musTask RX tail")
    m = rep(m, "  Serial1.setRxBufferSize(2048);", "  Serial1.setRxBufferSize(CAM_UART_RXBUF);   // 6144 (didefinisikan di camera_renphone.ino); 2048 terlalu kecil utk video di 2-3 Mbaud", "RX buffer")
    m = rep(m, "    vTaskDelay(pdMS_TO_TICKS(5));\n  }\n}\n\n// =====================================================================\n//  START (lazy",
               "    vTaskDelay(camLinkBusy() ? 1 : pdMS_TO_TICKS(5));   // app Kamera terbuka: kuras buffer UART lebih sering\n  }\n}\n\n// =====================================================================\n//  START (lazy", "musTask delay")
    out['musicbt_renphone.ino'] = m

# ---------- firmware ESP32-CAM ----------
cam = 'cam_bt_coprocessor/cam_bt_coprocessor.ino'
c = rd(cam)
if 'camModHandle' in c:
    print("Firmware CAM sudah memuat modul kamera, dilewati")
else:
    c = rep(c, '#include "ring.h"\n', '#include "ring.h"\n#include "cam_module.h"   // kamera OV2640 (RP_CAM_*)\n', "include")
    c = rep(c, "    case RP_POWEROFF:\n      pwrOffReq = true;\n      break;\n    default: break;\n  }\n}\n",
               "    case RP_POWEROFF:\n      pwrOffReq = true;\n      break;\n    default: camModHandle(type, p, len); break;   // RP_CAM_*\n  }\n}\n", "handleFrame default")
    c = rep(c, "      if (parser.feed(tmp[i])) handleFrame(parser.type, parser.payload, parser.len);\n",
               "      if (parser.feed(tmp[i])) { camModNoteRx(); handleFrame(parser.type, parser.payload, parser.len); }\n", "linkRxTask")
    c = rep(c, "  if (scanDone) { scanDone = false; sendScanResults(); }\n",
               "  if (scanDone) { scanDone = false; sendScanResults(); }\n  camModTick();   // status kamera, fallback baud, senter otomatis mati\n", "loop")
    c = rep(c, '  Serial.println("[PWR] deep sleep atas perintah S3");\n',
               '  Serial.println("[PWR] deep sleep atas perintah S3");\n  camModShutdown();\n', "camPowerOff")
    out[cam] = c

# ---------- workflow ----------
wf = '.github/workflows/build-firmware.yml'
if os.path.exists(wf):
    w = rd(wf)
    if 'camera_renphone.ino' in w: print("Workflow sudah memuat camera_renphone.ino, dilewati")
    else:
        w = rep(w, "          cp doom_renphone.ino build_sketch/\n",
                   "          cp doom_renphone.ino build_sketch/\n          cp camera_renphone.ino build_sketch/\n", "workflow cp ino")
        w = rep(w, "          cp rplink.h build_sketch/\n",
                   "          cp rplink.h build_sketch/\n          cp cam_rx.h build_sketch/\n          cp cam_avi.h build_sketch/\n", "workflow cp h")
        out[wf] = w
else:
    print("PERINGATAN: workflow tidak ditemukan, lewati")

for p, txt in out.items():
    open(p, 'w', encoding='utf-8', newline='\n').write(txt)
print("OK: Kamera terpasang. File diubah: " + (", ".join(out.keys()) or "(tidak ada)"))
print("PENTING: flash ulang BOTH: firmware S3 (phone) DAN firmware ESP32-CAM (cam_bt_coprocessor). Lihat KAMERA.md")
