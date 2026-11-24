#!/usr/bin/env python3
"""CALL_RXLEVEL_V1: layar panggilan menampilkan level suara LAWAN BICARA (bar kedua) selain level mic sendiri,
dan meter di-redraw ~7x/detik saat level berubah (sebelumnya cuma 1x/detik, ikut timer durasi).
Pakai dari root repo: python3 tools/patches/apply_call_rxlevel_patch.py [--check]   (idempoten)"""
import sys, pathlib
CHECK = "--check" in sys.argv
f = pathlib.Path("firmware/phone/call_renphone.ino"); t = f.read_text()
if "CALL_RXLEVEL_V1" in t: print("sudah   call_renphone.ino"); sys.exit(0)

EDITS = [
# 1) variabel level (dibaca UI)
("static volatile uint8_t  callMicLvl = 0;            // 0..100 utk meter kecil di layar\n",
 "static volatile uint8_t  callMicLvl = 0;            // 0..100 utk meter kecil di layar\n"
 "static volatile uint8_t  callRxLvl  = 0;            // CALL_RXLEVEL_V1: 0..100 level suara lawan bicara\n"
 "static uint8_t  callShownMic = 0, callShownRx = 0;  // level yg terakhir digambar (utk memutuskan redraw)\n"
 "static uint32_t callLvlDrawMs = 0;\n"),
# 2) akumulator sisi terima
("static uint32_t lastRtpRx = 0, micYieldAt = 0, lastLvlMs = 0;\n",
 "static uint32_t lastRtpRx = 0, micYieldAt = 0, lastLvlMs = 0;\n"
 "static uint32_t rxLvlMs = 0; static int32_t rxPkAcc = 0;      // CALL_RXLEVEL_V1\n"),
# 3) ukur puncak tiap paket suara yg benar2 diterima (bukan nada dering / PLC), umumkan tiap 100 ms
("  memcpy(rxLast, pcm, (size_t)n * 2);\n  if (!aoDrop) callPcmOut(pcm, n);\n}\n",
 "  memcpy(rxLast, pcm, (size_t)n * 2);\n"
 "  {   // CALL_RXLEVEL_V1: puncak jendela 100 ms (satu paket = 20 ms, jadi tanpa jendela bar akan berkedip)\n"
 "    int32_t pk = 0;\n"
 "    for (int i = 0; i < n; i++) { int32_t a = pcm[i] < 0 ? -(int32_t)pcm[i] : (int32_t)pcm[i]; if (a > pk) pk = a; }\n"
 "    if (pk > rxPkAcc) rxPkAcc = pk;\n"
 "    uint32_t tn = millis();\n"
 "    if ((int32_t)(tn - rxLvlMs) > 100) { rxLvlMs = tn; callRxLvl = (uint8_t)min(100, (int)(rxPkAcc * 100 / 12000)); rxPkAcc = 0; }\n"
 "  }\n"
 "  if (!aoDrop) callPcmOut(pcm, n);\n}\n"),
# 4) paket berhenti (lawan diam / silence suppression) -> bar turun ke 0
("static void callRtpRxStep(uint32_t now) {\n  int guard = 0;\n",
 "static void callRtpRxStep(uint32_t now) {\n"
 "  if (callRxLvl && (int32_t)(now - rxLvlMs) > 300) { callRxLvl = 0; rxPkAcc = 0; }   // CALL_RXLEVEL_V1\n"
 "  int guard = 0;\n"),
# 5) UI: dua bar (lawan di atas, kamu di bawah), label kiri
("""  if (ph == CPH_ACTIVE) {                                      // meter mic kecil
    int bw = SCR_W - 80, bx = 40, by = cy + 94;
    nyxAaRRRing(s, bx, by, bw, 6, 3, T().subtext);
    int fw = (bw - 2) * callMicLvl / 100;
    if (fw > 0) nyxAaRR(s, bx + 1, by + 1, fw, 4, 2, callMuted ? (uint16_t)T().danger : (uint16_t)T().good);
  }
""",
"""  if (ph == CPH_ACTIVE) {                                      // CALL_RXLEVEL_V1: meter lawan (atas) + mic sendiri (bawah)
    int bw = SCR_W - 88, bx = 46;
    uint8_t rxl = callRxLvl, mcl = callMicLvl;
    callShownRx = rxl; callShownMic = mcl;
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(6, cy + 87);  s.print(RPTR("Lawan"));
    s.setCursor(6, cy + 96);  s.print(RPTR("Kamu"));
    int by1 = cy + 89, by2 = cy + 98;
    nyxAaRRRing(s, bx, by1, bw, 5, 2, T().subtext);
    int fr = (bw - 2) * rxl / 100;
    if (fr > 0) nyxAaRR(s, bx + 1, by1 + 1, fr, 3, 1, (uint16_t)T().accent);
    nyxAaRRRing(s, bx, by2, bw, 5, 2, T().subtext);
    int fm = (bw - 2) * mcl / 100;
    if (fm > 0) nyxAaRR(s, bx + 1, by2 + 1, fm, 3, 1, callMuted ? (uint16_t)T().danger : (uint16_t)T().good);
  }
"""),
# 6) redraw cepat saat level bergerak (maks ~7x/detik); timer 1 detik tetap utk durasi
("  if (visible && ph == CPH_ACTIVE && now - callTimerMs > 1000) { callTimerMs = now; needRedraw = true; }\n",
 "  if (visible && ph == CPH_ACTIVE && now - callTimerMs > 1000) { callTimerMs = now; needRedraw = true; }\n"
 "  if (visible && ph == CPH_ACTIVE && now - callLvlDrawMs > 150) {   // CALL_RXLEVEL_V1\n"
 "    int dm = (int)callMicLvl - (int)callShownMic, dr = (int)callRxLvl - (int)callShownRx;\n"
 "    if (dm >= 6 || dm <= -6 || dr >= 6 || dr <= -6) { callLvlDrawMs = now; needRedraw = true; }\n"
 "  }\n"),
]
bad = 0
for old, new in EDITS:
    if t.count(old) != 1: print(f"GAGAL   anchor tidak ditemukan tepat 1x: {old[:60]!r}"); bad += 1
if bad: sys.exit(1)
if not CHECK:
    for old, new in EDITS: t = t.replace(old, new, 1)
    f.write_text(t)
print(("cocok " if CHECK else "dipasang") + " call_renphone.ino (6 titik)")
