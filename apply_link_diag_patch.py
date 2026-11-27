#!/usr/bin/env python3
# apply_link_diag_patch.py -- marker LINK_DIAG_V1
# Tambah baris [LINK] di log DIAG WROOM/CAM: pin RX/TX yang dipakai + jumlah byte & frame yang masuk dari S3.
import sys
INO = "firmware/cam_bt_coprocessor/cam_bt_coprocessor.ino"
s = open(INO, encoding="utf-8").read()
if "LINK_DIAG_V1" in s: print("Sudah terpasang."); sys.exit(0)
def rep(s, old, new):
    if s.count(old) != 1: sys.exit("GAGAL: potongan tidak ditemukan 1x: " + old[:50])
    return s.replace(old, new)
s = rep(s, "static void linkRxTask(void* param) {\n",
 "static volatile uint32_t linkRxBytes = 0, linkRxFrames = 0;   // LINK_DIAG_V1\nstatic void linkRxTask(void* param) {\n")
s = rep(s, "    n = Serial2.readBytes(tmp, n);\n    for (int i = 0; i < n; i++)\n      if (parser.feed(tmp[i])) { camModNoteRx();",
 "    n = Serial2.readBytes(tmp, n);\n    linkRxBytes += n;\n    for (int i = 0; i < n; i++)\n      if (parser.feed(tmp[i])) { linkRxFrames++; camModNoteRx();")
s = rep(s, "        (unsigned)parser.errors, (int)decErr, (int)btConnected);\n",
 "        (unsigned)parser.errors, (int)decErr, (int)btConnected);\n"
 "      Serial.printf(\"[LINK] RX=%d TX=%d | byte masuk=%u frame ok=%u\\n\", (int)LINK_RX_PIN, (int)LINK_TX_PIN, (unsigned)linkRxBytes, (unsigned)linkRxFrames);\n")
open(INO, "w", encoding="utf-8", newline="\n").write(s)
print("OK: baris [LINK] ditambahkan ke log DIAG.")
