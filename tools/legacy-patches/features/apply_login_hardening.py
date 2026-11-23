#!/usr/bin/env python3
# ARSIP: sudah terpasang; anchor-nya sudah tidak ada di phone.ino saat ini, jadi skrip ini akan GAGAL kalau dijalankan lagi.
"""LOGINLOCK_V1 -- (1) kunci login Web File Manager 60 dtk setelah 5x sandi salah,
(2) hapus sandi dari log Serial. Idempoten. Jalankan dari ROOT repo:  python3 tools/legacy-patches/features/apply_login_hardening.py"""
import sys, os

def edit(path, pairs, post=None):
    s = open(path, encoding="utf-8").read(); changed = False
    for old, new in pairs:
        if new in s and old not in s:
            continue                                   # sudah terpasang
        if s.count(old) != 1:
            sys.exit("GAGAL: anchor tidak cocok persis 1x di %s:\n  %s" % (path, old[:80]))
        s = s.replace(old, new); changed = True
    if post:
        s2 = post(s)
        if s2 != s: s, changed = s2, True
    if changed: open(path, "w", encoding="utf-8").write(s)
    print("OK  " if changed else "SKIP", path)

D = "firmware/phone/"
if not os.path.exists(D + "phone.ino"):
    sys.exit("Jalankan dari root repo (folder yang berisi firmware/phone/phone.ino)")

import re
def _upload(s):
    if re.search(r'webUpAuthed = [^;]*webCheckCred\(\)', s): return s
    s2, n = re.subn(r'(webUpAuthed = [^;]*?)webServer\.authenticate\("admin", webPw\(\)\)', r'\1webCheckCred()', s)
    if n != 1: sys.exit("GAGAL: baris upload (webUpAuthed) tidak ditemukan 1x")
    return s2

edit(D + "phone.ino", [
('''static bool webAuthOk() {
  if (webServer.authenticate("admin", webPw())) return true;
  webServer.requestAuthentication(BASIC_AUTH, "Accretion Phone");
  return false;
}''',
'''#include "esp_timer.h"
// LOGINLOCK_V1 -- 5x sandi salah -> login dikunci 60 dtk (hanya hitung kalau ada header Authorization)
static uint8_t  webFailN = 0;
static uint64_t webLockUntilMs = 0;                             // esp_timer 64-bit (tidak wrap); 0 = tidak dikunci
static bool webLocked() {
  if (webLockUntilMs && (uint64_t)(esp_timer_get_time() / 1000) >= webLockUntilMs) webLockUntilMs = 0;
  return webLockUntilMs != 0;
}
static bool webCheckCred() {                                    // true = sandi benar
  if (webLocked()) return false;
  if (webServer.authenticate("admin", webPw())) { webFailN = 0; return true; }
  if (webServer.hasHeader("Authorization") && ++webFailN >= 5) {
    webFailN = 0; webLockUntilMs = (uint64_t)(esp_timer_get_time() / 1000) + 60000ULL;
    Serial.println("[Web] 5x sandi salah -> login dikunci 60 dtk");
  }
  return false;
}
static bool webAuthOk() {
  if (webCheckCred()) return true;
  if (webLocked()) { webServer.send(429, "text/plain", "Terlalu banyak percobaan. Coba lagi 1 menit lagi."); return false; }
  webServer.requestAuthentication(BASIC_AUTH, "Accretion Phone");
  return false;
}'''),
('{ static const char* hk[] = { "Sec-Fetch-Site", "Origin", "Host" }; webServer.collectHeaders(hk, 3); }',
 '{ static const char* hk[] = { "Sec-Fetch-Site", "Origin", "Host", "Authorization" }; webServer.collectHeaders(hk, 4); }'),
('Serial.printf("[Web] login File Manager: admin / %s\\n", webPw());',
 'Serial.println("[Web] login File Manager aktif (sandi: lihat layar File Explorer)");'),
], post=_upload)

edit(D + "remote_renphone.ino", [
('Serial.printf("[RC] sandi AP baru: %s\\n", rcCfg.pass);',
 'Serial.println("[RC] sandi AP baru dibuat (lihat layar Remote Control)");'),
])

p = "docs/security.md"; s = open(p, encoding="utf-8").read()
a = "Cross-site requests that change data are refused."
if "locked for 60 seconds" not in s and s.count(a) == 1:
    s = s.replace(a, a + " After 5 wrong passwords the login is locked for 60 seconds."); open(p, "w", encoding="utf-8").write(s); print("OK", p)
p = "CHANGELOG.md"; s = open(p, encoding="utf-8").read()
if "locked for 60 seconds" not in s and "## Unreleased\n" in s:
    s = s.replace("## Unreleased\n", "## Unreleased\n- New: Web File Manager login is locked for 60 seconds after 5 wrong passwords; passwords are no longer printed to the serial log\n", 1)
    open(p, "w", encoding="utf-8").write(s); print("OK", p)
