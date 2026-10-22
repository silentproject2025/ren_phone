#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_diag_patch.py -- riwayat diagnosa panggilan MASUK di app Telepon (call_renphone.ino).
#
# Jalankan dari folder repo (ada call_renphone.ino):
#     python3 apply_call_diag_patch.py
# Idempoten & semua-atau-tidak-sama-sekali (anchor harus ketemu TEPAT 1x).
#
# Masalah: baris "SIP rx:" cuma menampilkan 1 paket TERAKHIR, jadi INVITE tertimpa BYE.
# Patch ini menyimpan 4 kejadian terakhir (paket masuk + keputusan Ren Phone atas INVITE:
# sibuk / SDP ditolak / berdering / ditolak krn NES-DOOM) dan menampilkannya 4 baris
# di bagian bawah halaman Telepon. Juga dicetak ke Serial.
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'callDbgPush' in s:
    print("Patch diagnosa sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# 1. helper riwayat (sebelum callSetStatus; callCopy sudah ada di atasnya)
s = rep(s, "// teks status bersama (dipanggil dari task)\nstatic void callSetStatus(const char* s) {",
'''// riwayat diagnosa: 4 kejadian terakhir (paket masuk + keputusan atas INVITE)
static char callDbgHist[4][40];
static void callDbgPush(const char* m) {
  for (int i = 3; i > 0; i--) memcpy(callDbgHist[i], callDbgHist[i - 1], sizeof(callDbgHist[i]));
  callCopy(callDbgHist[0], sizeof(callDbgHist[0]), m);
  Serial.printf("[CALL] %s\\n", m);
}

// teks status bersama (dipanggil dari task)
static void callSetStatus(const char* s) {''', "helper")

# 2. INVITE: sibuk (fase bukan IDLE)
s = rep(s,
'''    } else {
      int n = sipBuildReply(0, 486, "Busy Here", nullptr, nullptr, sipTx, SIP_BUF);
      sipTxTo(srcIp, srcPort, sipTx, n);
    }
    return;
  }
  memset(&dlg, 0, sizeof(dlg));
  if (!callParseSdp(sipBody(msg))) {
    int n = sipBuildReply(0, 488, "Not Acceptable Here", nullptr, nullptr, sipTx, SIP_BUF);''',
'''    } else {
      char dg[40]; snprintf(dg, sizeof(dg), "-> 486 sibuk (fase %d)", (int)callPhase);
      callDbgPush(dg);
      int n = sipBuildReply(0, 486, "Busy Here", nullptr, nullptr, sipTx, SIP_BUF);
      sipTxTo(srcIp, srcPort, sipTx, n);
    }
    return;
  }
  memset(&dlg, 0, sizeof(dlg));
  if (!callParseSdp(sipBody(msg))) {
    { char dg[40]; snprintf(dg, sizeof(dg), "-> 488 %s", sdpWhy ? sdpWhy : "SDP"); callDbgPush(dg); }
    int n = sipBuildReply(0, 488, "Not Acceptable Here", nullptr, nullptr, sipTx, SIP_BUF);''', "invite busy/488")

# 3. INVITE: berdering
s = rep(s,
'''  callIncomingNew = true;
}

static void callAnswer() {''',
'''  callIncomingNew = true;
  callDbgPush("-> 180 berdering");
}

static void callAnswer() {''', "invite ringing")

# 4. INVITE ditolak krn NES/DOOM
s = rep(s,
'''    if (callGameBusy()) {
      callPost(CRQ_REJECT, "");''',
'''    if (callGameBusy()) {
      callDbgPush("-> ditolak: NES/DOOM jalan");
      callPost(CRQ_REJECT, "");''', "game busy")

# 5. paket masuk -> riwayat
s = rep(s,
"    callDbgRxMs = millis(); callDirty = true;\n  }\n  int code = sipStatus(msg);",
"    callDbgRxMs = millis(); callDirty = true;\n    { char dg[40]; snprintf(dg, sizeof(dg), \"rx %s\", callDbgRx); callDbgPush(dg); }\n  }\n  int code = sipStatus(msg);", "rx push")

# 6. BYE: catat fase
s = rep(s,
"static void callOnBye(const char* msg, const char* srcIp, int srcPort) {\n  sipSplitReq(msg);",
"static void callOnBye(const char* msg, const char* srcIp, int srcPort) {\n  sipSplitReq(msg);\n  { char dg[40]; snprintf(dg, sizeof(dg), \"BYE: fase %d, id %s\", (int)callPhase, strcmp(rq.callId, dlg.callId) == 0 ? \"cocok\" : \"beda\"); callDbgPush(dg); }", "bye")

# 7. tampilan: 4 baris riwayat di bawah (gantikan 1 baris lama)
s = rep(s,
'''  if (!kbVisible && callDbgRx[0]) {
    char d[64]; unsigned ag = (unsigned)((millis() - callDbgRxMs) / 1000UL);
    snprintf(d, sizeof(d), "SIP rx: %s (%us)", callDbgRx, ag);
    s.setTextColor(T().subtext); s.setCursor(backX() + BACK_W + 8, backY() + 5); s.print(d);
  }''',
'''  if (!kbVisible && callDbgHist[0][0]) {
    int y0 = backY() - 4 * 12 - 2;
    s.fillRect(0, y0 - 1, SCR_W, 4 * 12 + 2, T().bg);
    s.setTextColor(T().subtext);
    for (int i = 0; i < 4; i++) {
      if (!callDbgHist[3 - i][0]) continue;
      s.setCursor(4, y0 + i * 12); s.print(callDbgHist[3 - i]);
    }
  }''', "render")

open(F, 'w', encoding='utf-8').write(s)
print("OK: call_renphone.ino diperbarui (riwayat diagnosa 4 baris)")
