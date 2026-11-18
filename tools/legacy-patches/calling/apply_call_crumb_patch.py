#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_crumb_patch.py -- lacak crash/restart saat PANGGILAN MASUK (call_renphone.ino).
#
# Temuan: INVITE masuk dijawab "180 Ringing" lalu ESP32 restart. Patch ini menambah
# penanda langkah (breadcrumb) di jalur panggilan masuk dan MENAHAN pesan "Reset: CRASH
# task=N ui=M" di 4 baris riwayat halaman Telepon (sebelumnya cuma toast 6 detik).
# Wajib dijalankan SETELAH apply_call_diag_patch.py.
#
#     python3 apply_call_crumb_patch.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'callDbgPush' not in s:
    print("ERROR: jalankan apply_call_diag_patch.py dulu"); sys.exit(1)
if 'callMarkT(74)' in s:
    print("Patch crumb sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# --- task SIP: callOnInvite ---
s = rep(s, "static void callOnInvite(const char* msg, const char* srcIp, int srcPort) {\n  sipSplitReq(msg);",
           "static void callOnInvite(const char* msg, const char* srcIp, int srcPort) {\n  sipSplitReq(msg);\n  callMarkT(70);", "invite entry")
s = rep(s, '  int n = sipBuildReply(1, 180, "Ringing", nullptr, nullptr, sipTx, SIP_BUF);\n  sipTxTo(srcIp, srcPort, sipTx, n);\n\n  callPhase = CPH_INCOMING;',
           '  callMarkT(71);\n  int n = sipBuildReply(1, 180, "Ringing", nullptr, nullptr, sipTx, SIP_BUF);\n  sipTxTo(srcIp, srcPort, sipTx, n);\n  callMarkT(72);\n\n  callPhase = CPH_INCOMING;', "invite 180")
s = rep(s, "  if (rc == 0) { toneMode = 2; toneT = 0; toneLastMs = 0; }\n  callIncomingNew = true;",
           "  callMarkT(73);\n  if (rc == 0) { toneMode = 2; toneT = 0; toneLastMs = 0; }\n  callIncomingNew = true;\n  callMarkT(74);", "invite audio")

# --- UI thread: pemicu dering ---
s = rep(s, "    callIncomingNew = false;\n    if (callGameBusy()) {",
           "    callIncomingNew = false;\n    callMarkU(70);\n    if (callGameBusy()) {", "ui incoming")
s = rep(s, "      callOvl = true; callOvlLocked = locked;\n      homeAodWake();\n      callLastRingVib = 0;",
           "      callOvl = true; callOvlLocked = locked;\n      callMarkU(71);\n      homeAodWake();\n      callMarkU(72);\n      callLastRingVib = 0;", "ui aod")
s = rep(s, "    vibPlayPattern(rp, 4);\n  }\n  if (ph == CPH_ACTIVE && callPrevPhase",
           "    callMarkU(73);\n    vibPlayPattern(rp, 4);\n    callMarkU(74);\n  }\n  if (ph == CPH_ACTIVE && callPrevPhase", "ui vib")
s = rep(s, "  s.setTextWrap(false);\n  callDrawInCall(s);\n  drawToast(s);",
           "  s.setTextWrap(false);\n  callMarkU(75);\n  callDrawInCall(s);\n  callMarkU(76);\n  drawToast(s);", "ui draw")

# --- tahan pesan reset di riwayat ---
s = rep(s, "if (!callBootShown && callBootMsg[0]) { callBootShown = true; showToast(callBootMsg, 6000); }",
           "if (!callBootShown && callBootMsg[0]) { callBootShown = true; showToast(callBootMsg, 6000); callDbgPush(callBootMsg); }", "boot msg")

open(F, 'w', encoding='utf-8').write(s)
print("OK: penanda crash jalur panggilan masuk terpasang")
