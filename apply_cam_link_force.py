#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_link_force.py -- LINK_FORCE_V1: opsi link kamera "5 Mbaud PAKSA" (tanpa turun otomatis).

Di app Kamera > tab Sistem > baris Link, ada pilihan baru PALING AKHIR: "5 Mbaud PAKSA".
  * Frame rusak (bit hilang di kabel) TIDAK lagi menurunkan link satu anak tangga; batas atas tetap 5 Mbaud.
  * Link sunyi / CAM hilang: S3 tetap mundur sebentar ke 921k (wajib, supaya link bisa pulih), lalu otomatis
    mencoba 5 Mbaud lagi tiap ~3,5 dtk. Batas atas TIDAK pernah diturunkan dan tidak ditandai "cepat gagal".
  * Pilihan lama (921k ... 5 Mbaud maks) berperilaku persis seperti sebelumnya. Config NVS lama tetap valid (tanpa migrasi).
Tidak ada perubahan firmware CAM. Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "camera_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "LINK_FORCE_V1" in s:
    print("[skip] LINK_FORCE_V1 sudah terpasang"); sys.exit(0)

def rep(s, old, new, tag):
    n = s.count(old)
    if n != 1: sys.exit("ERROR [%s]: anchor ketemu %d kali (harus 1). Dibatalkan, tidak ada file diubah." % (tag, n))
    return s.replace(old, new)

s = rep(s, '#define CAM_LINK_N 5\nstatic const uint32_t       CAM_LINK_BAUD[CAM_LINK_N] = {RP_BAUD, 2000000, 3000000, 4000000, 5000000};',
           '#define CAM_LINK_N 5      // jumlah anak tangga (urutan turun otomatis)\n'
           '#define CAM_LINK_OPT_N 6  // LINK_FORCE_V1: pilihan di menu = anak tangga + "5 Mbaud PAKSA" (indeks CAM_LINK_N, tidak ikut tangga)\n'
           'static const uint32_t       CAM_LINK_BAUD[CAM_LINK_OPT_N] = {RP_BAUD, 2000000, 3000000, 4000000, 5000000, 5000000};', "baud")
s = rep(s, 'static const char*    const CAM_LINK_NAME[CAM_LINK_N] = {"921 kbaud (aman)", "2 Mbaud", "3 Mbaud", "4 Mbaud", "5 Mbaud (maks)"};',
           'static const char*    const CAM_LINK_NAME[CAM_LINK_OPT_N] = {"921 kbaud (aman)", "2 Mbaud", "3 Mbaud", "4 Mbaud", "5 Mbaud (maks)", "5 Mbaud PAKSA"};', "name")
s = rep(s, "  if (camCfg.link < 0 || camCfg.link >= CAM_LINK_N) camCfg.link = CAM_LINK_N - 1;",
           "  if (camCfg.link < 0 || camCfg.link >= CAM_LINK_OPT_N) camCfg.link = CAM_LINK_N - 1;", "load")
s = rep(s, "static int camLinkTarget() {",
           "static inline bool camForce() { return camCfg.link >= CAM_LINK_N; }   // LINK_FORCE_V1: \"5 Mbaud PAKSA\"\nstatic int camLinkTarget() {", "force-fn")
s = rep(s, "static void camLinkStepDown(uint32_t now, int fromRung, bool alive, const char* why) {   // CAM_LINK_DIAG_V1: +why\n",
           "static void camLinkStepDown(uint32_t now, int fromRung, bool alive, const char* why) {   // CAM_LINK_DIAG_V1: +why\n"
           "  if (camForce()) {   // LINK_FORCE_V1: batas atas TIDAK diturunkan, tidak ada tanda \"cepat gagal\"\n"
           "    camBaudReq = false;\n"
           "    if (!alive) camRetryAt = now + 3500;   // link sunyi: S3 sudah di 921k (agar bisa pulih); coba 5M lagi setelah CAM mundur sendiri (<=3 dtk)\n"
           "    static uint32_t lastToast = 0;\n"
           "    if (now - lastToast > 8000UL) { lastToast = now; char m[64]; snprintf(m, sizeof(m), \"Link 5M (paksa) tak bersih: %s\", why); showToast(m, 1800); }\n"
           "    return;\n"
           "  }\n", "stepdown")
s = rep(s, "case 20: camCfg.link = camStepv(camCfg.link, dir, 0, CAM_LINK_N - 1, 1, true);",
           "case 20: camCfg.link = camStepv(camCfg.link, dir, 0, CAM_LINK_OPT_N - 1, 1, true);", "stepper")
s = rep(s, 'camLinkFailed ? " (cepat gagal)" : (camLinkCeil < camCfg.link ? " (turun otomatis)" : ""));',
           'camLinkFailed ? " (cepat gagal)" : (camForce() ? " (PAKSA)" : (camLinkCeil < camCfg.link ? " (turun otomatis)" : "")));', "hud")

if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)

try:
    d = open("KAMERA.md", encoding="utf-8", newline="").read()
    if "5 Mbaud PAKSA" not in d:
        d = d.rstrip("\n") + """

## Link "5 Mbaud PAKSA" (LINK_FORCE_V1)
Atur > Sistem > Link: pilihan terakhir **5 Mbaud PAKSA**. Frame rusak tidak lagi menurunkan link satu anak tangga (5M -> 4M -> ...); batas atas
tetap 5 Mbaud. Kalau link sunyi / CAM hilang, S3 tetap mundur sebentar ke 921k (supaya link bisa pulih, CAM juga mundur sendiri setelah 3 dtk) lalu
mencoba 5 Mbaud lagi tiap ~3,5 dtk; batas atas tidak pernah diturunkan. HUD menampilkan "(PAKSA)" dan toast peringatan (maks 1x / 8 dtk) kalau link tidak bersih.
Konsekuensi: kabel yang buruk = preview patah-patah / ada frame rusak, bukan turun diam-diam ke baud lebih rendah. Pilihan lama berperilaku seperti biasa.
"""
        open("KAMERA.md", "w", encoding="utf-8", newline="").write(d)
except OSError:
    pass
print("OK: LINK_FORCE_V1 terpasang (camera_renphone.ino, KAMERA.md). Flash ulang S3 saja; firmware CAM tidak berubah.")
