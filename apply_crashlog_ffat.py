#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""apply_crashlog_ffat.py (CRASHLOG_FFAT_V1): /crashlog.txt disimpan di FFat (flash internal) kalau tersedia,
fallback ke SD. Jadi log tetap tercatat walau SD bermasalah. Bisa diunduh dari web file manager
(kartu 'Daftar File Flash (FFat)', butuh apply_ffat_upload.py). Jalankan dari root repo. Idempotent."""
import sys, os
P = "nyxextras_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu" % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
if "CRASHLOG_FFAT_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
def rep(old, new, label, cnt=1):
    global s
    n = s.count(old)
    if n != cnt: sys.exit("ERROR: '%s' ketemu %d kali (harus %d). Dibatalkan." % (label, n, cnt))
    s = s.replace(old, new)

rep("static void nyxCountLogLines() {\n", '''// CRASHLOG_FFAT_V1: log crash diutamakan di FFat (flash internal), fallback SD.
static fs::FS& nyxFs() { if (ffatReady) return FFat; return SD_MMC; }

static void nyxCountLogLines() {
''', "helper")
rep('  File f = SD_MMC.open("/crashlog.txt", FILE_READ);\n  if (!f) return;', '  File f = nyxFs().open("/crashlog.txt", FILE_READ);\n  if (!f) return;', "count")
rep("  if (!sdReady) { if (millis() > 120000UL) nyxFlushDone = true; return; }",
    "  if (!sdReady && !ffatReady) { if (millis() > 120000UL) nyxFlushDone = true; return; }", "ready")
rep('    File r = SD_MMC.open("/crashlog.txt", FILE_READ);', '    File r = nyxFs().open("/crashlog.txt", FILE_READ);', "baca")
rep('if (sz > 32768) { SD_MMC.remove("/crashlog.old"); SD_MMC.rename("/crashlog.txt", "/crashlog.old"); }',
    'if (sz > 32768) { nyxFs().remove("/crashlog.old"); nyxFs().rename("/crashlog.txt", "/crashlog.old"); }', "rotasi")
rep('    File f = SD_MMC.open("/crashlog.txt", FILE_APPEND);', '    File f = nyxFs().open("/crashlog.txt", FILE_APPEND);', "tulis")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: CRASHLOG_FFAT_V1 diterapkan")
