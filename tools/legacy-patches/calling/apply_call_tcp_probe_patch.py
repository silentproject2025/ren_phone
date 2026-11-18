#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_tcp_probe_patch.py -- DIAGNOSA: apakah server Linphone mencoba mengirim panggilan lewat TCP?
#
# Dugaan (BELUM terbukti): INVITE yang diteruskan server ke Ren Phone melewati batas ukuran UDP server
# (sekitar 1300 byte), sehingga server mencoba TCP port 5060 -- padahal Ren Phone hanya mendengarkan UDP.
# Patch ini membuka "pendengar" TCP di port 5060 yang hanya MENCATAT sambungan masuk (alamat + baris
# pertama) ke riwayat 4 baris halaman Telepon lalu menutupnya. Tidak mengubah alur panggilan lain.
# Wajib dijalankan SETELAH apply_call_diag_patch.py (butuh callDbgPush).
#
#     python3 apply_call_tcp_probe_patch.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'callDbgPush' not in s:
    print("ERROR: jalankan apply_call_diag_patch.py dulu"); sys.exit(1)
if 'sipTcp' in s:
    print("Patch tcp-probe sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

s = rep(s, "static void callTaskFunc(void*) {",
           "static WiFiServer sipTcp(CALL_SIP_PORT);          // hanya utk diagnosa: mencatat sambungan TCP masuk\n\nstatic void callTaskFunc(void*) {", "decl")

s = rep(s, "      sipUdp.begin(CALL_SIP_PORT);\n      sipOpen = true;",
           "      sipUdp.begin(CALL_SIP_PORT);\n      sipTcp.begin();\n      sipOpen = true;", "begin")

s = rep(s, "      if (sipOpen) { sipUdp.stop(); sipOpen = false; }",
           "      if (sipOpen) { sipUdp.stop(); sipTcp.end(); sipOpen = false; }", "end")

s = rep(s, "    now = millis();   // PENTING:",
'''    {                                                       // DIAGNOSA: sambungan TCP masuk? (catat lalu tutup)
      WiFiClient tc = sipTcp.available();
      if (tc) {
        uint32_t t0 = millis(); char b[40]; int n = 0;
        while (millis() - t0 < 200 && n < (int)sizeof(b) - 1) {
          if (tc.available()) { int c = tc.read(); if (c < 0 || c == '\\r' || c == '\\n') break; b[n++] = (char)c; }
          else vTaskDelay(pdMS_TO_TICKS(5));
        }
        b[n] = 0;
        IPAddress ra = tc.remoteIP();
        char dg[40];
        snprintf(dg, sizeof(dg), "TCP %u.%u.%u.%u %s", (unsigned)ra[0], (unsigned)ra[1], (unsigned)ra[2], (unsigned)ra[3], b);
        callDbgPush(dg);
        tc.stop();
      }
    }

    now = millis();   // PENTING:''', "accept")

open(F, 'w', encoding='utf-8').write(s)
print("OK: pendengar TCP diagnosa terpasang (hanya mencatat ke riwayat)")
