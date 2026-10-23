#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_diag_fix3.py -- riwayat diagnosa Telepon: 6 baris dan tanpa NOTIFY.
#
# Masalah: server mengirim NOTIFY berkala, sehingga 4 baris riwayat cepat penuh dan bukti INVITE tergeser
# (di screenshot terakhir tidak bisa dipastikan apakah INVITE dari Android pernah masuk).
# Patch ini: (1) NOTIFY tidak masuk riwayat (tetap dicetak ke Serial), (2) riwayat jadi 6 baris.
# Wajib dijalankan SETELAH apply_call_diag_patch.py dan apply_call_diag_fix2.py.
#
#     python3 apply_call_diag_fix3.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'callDbgPush' not in s or 'REGISTER") == nullptr' not in s:
    print("ERROR: jalankan apply_call_diag_patch.py dan apply_call_diag_fix2.py dulu"); sys.exit(1)
if 'callDbgHist[6][40]' in s:
    print("Patch diag_fix3 sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

s = rep(s, "static char callDbgHist[4][40];", "static char callDbgHist[6][40];", "array")
s = rep(s, "  for (int i = 3; i > 0; i--) memcpy(callDbgHist[i], callDbgHist[i - 1], sizeof(callDbgHist[i]));",
           "  for (int i = 5; i > 0; i--) memcpy(callDbgHist[i], callDbgHist[i - 1], sizeof(callDbgHist[i]));", "push")
s = rep(s, '''    int y0 = backY() - 4 * 12 - 2;
    s.fillRect(0, y0 - 1, SCR_W, 4 * 12 + 2, T().bg);
    s.setTextColor(T().subtext);
    for (int i = 0; i < 4; i++) {
      if (!callDbgHist[3 - i][0]) continue;
      s.setCursor(4, y0 + i * 12); s.print(callDbgHist[3 - i]);
    }''', '''    int y0 = backY() - 6 * 12 - 2;
    s.fillRect(0, y0 - 1, SCR_W, 6 * 12 + 2, T().bg);
    s.setTextColor(T().subtext);
    for (int i = 0; i < 6; i++) {
      if (!callDbgHist[5 - i][0]) continue;
      s.setCursor(4, y0 + i * 12); s.print(callDbgHist[5 - i]);
    }''', "render")
s = rep(s, 'if (strstr(callDbgRx, "REGISTER") == nullptr) {',
           'if (strstr(callDbgRx, "REGISTER") == nullptr && strstr(callDbgRx, "NOTIFY") == nullptr) {', "filter")

open(F, 'w', encoding='utf-8').write(s)
print("OK: riwayat 6 baris, NOTIFY tidak masuk riwayat")
