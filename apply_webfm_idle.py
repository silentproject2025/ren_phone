#!/usr/bin/env python3
"""WEBFM_IDLE_V1 -- Web File Manager hanya aktif saat layar File Explorer terbuka, dan 10 menit
setelah keluar / setelah aktivitas terakhir. Selebihnya server menjawab 403. Remote Control (/remote, /rc/*)
dan gamepad (/pad) memakai server yang sama dan TIDAK terpengaruh. Idempoten.
Jalankan dari ROOT repo:  python3 apply_webfm_idle.py"""
import sys, os, re
f = "firmware/phone/phone.ino"
if not os.path.exists(f): sys.exit("Jalankan dari root repo (folder berisi firmware/phone/phone.ino)")
s = open(f, encoding="utf-8").read()
if "WEBFM_IDLE_V1" in s: print("SKIP", f, "(sudah terpasang)"); sys.exit(0)

def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit("GAGAL: anchor tidak cocok persis 1x:\n  " + old[:90])
    s = s.replace(old, new)

rep('''static bool webGuard(bool write) {
  if (!webAuthOk()) return false;''',
'''// WEBFM_IDLE_V1 -- File Manager web aktif hanya selama File Explorer terbuka + 10 menit sesudahnya
#include "esp_timer.h"
Screen curScreen();
#define WEBFM_IDLE_MS 600000ULL
static uint64_t webFmUntilMs = 0;                                 // esp_timer 64-bit (tidak wrap); 0 = belum pernah dibuka
static uint64_t webFmNowMs() { return (uint64_t)(esp_timer_get_time() / 1000); }
void webFmArm() { webFmUntilMs = webFmNowMs() + WEBFM_IDLE_MS; }
static bool webFmActive() {
  if (curScreen() == SCR_FILEEXPLORER) return true;
  return webFmUntilMs && webFmNowMs() < webFmUntilMs;
}
static bool webGuard(bool write) {
  if (!webFmActive()) {
    webServer.send(403, "text/plain", "Web File Manager nonaktif. Buka File Explorer di HP untuk mengaktifkannya.");
    return false;
  }
  if (!webAuthOk()) return false;
  webFmArm();                                                     // aktivitas -> perpanjang 10 menit''')

s, n = re.subn(r'webUpAuthed = (webServer\.authenticate\("admin", webPw\(\)\)|webCheckCred\(\)) && webCsrfOk\(\);',
               r'webUpAuthed = webFmActive() && \1 && webCsrfOk();', s)
if n != 1: sys.exit("GAGAL: baris upload (webUpAuthed) tidak ditemukan 1x")

rep("void fileExpExit(){", "void fileExpExit(){ webFmArm(); /*WEBFM_IDLE_V1*/")
open(f, "w", encoding="utf-8").write(s); print("OK  ", f)

p = "docs/security.md"; d = open(p, encoding="utf-8").read()
a = "Cross-site requests that change data are refused."
if "File Explorer is open" not in d and d.count(a) == 1:
    d = d.replace(a, a + " The web interface answers only while the File Explorer screen is open and for 10 minutes after leaving it or after the last request (403 otherwise); Remote Control and the gamepad page are not affected.")
    open(p, "w", encoding="utf-8").write(d); print("OK  ", p)
p = "CHANGELOG.md"; c = open(p, encoding="utf-8").read()
if "File Explorer is open" not in c and "## Unreleased\n" in c:
    c = c.replace("## Unreleased\n", "## Unreleased\n- Changed: the Web File Manager only responds while File Explorer is open (and for 10 minutes afterwards); open File Explorer on the phone to enable it again\n", 1)
    open(p, "w", encoding="utf-8").write(c); print("OK  ", p)
