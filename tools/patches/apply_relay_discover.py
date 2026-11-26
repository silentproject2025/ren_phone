#!/usr/bin/env python3
# apply_relay_discover.py -- RELAY_DISCOVER_V1  (butuh apply_relay_stream.py sudah terpasang)
# Jalankan dari root repo:  python3 tools/patches/apply_relay_discover.py
# Tombol [Relay]: urutan alamat = /relay.txt (kalau ada) -> temukan otomatis lewat UDP 8081 (relay_yt.py memancarkan
# "RENRELAY <port>" tiap 1 dtk; tunggu maks 3,5 dtk) -> gateway WiFi:8080 (relay di HP pemilik hotspot).
import os, sys
M = "RELAY_DISCOVER_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
if "RELAY_STREAM_V1" not in ph: sys.exit("GAGAL: jalankan apply_relay_stream.py dulu.")
old = '  if(hp.length() == 0) hp = WiFi.gatewayIP().toString() + ":8080";\n'
new = '''  if(hp.length() == 0){            // RELAY_DISCOVER_V1: relay memancarkan "RENRELAY <port>" ke UDP 8081 tiap 1 dtk
    WiFiUDP du;
    if(du.begin(8081)){
      uint32_t t0 = millis();
      while(millis() - t0 < 3500){
        int n = du.parsePacket();
        if(n > 0){
          char b[40]; int m = du.read(b, sizeof(b) - 1); if(m < 0) m = 0; b[m] = 0;
          if(strncmp(b, "RENRELAY ", 9) == 0){
            hp = du.remoteIP().toString() + ":" + String(atoi(b + 9));
            break;
          }
        }
        delay(20); yield();
      }
      du.stop();
    }
  }
  if(hp.length() == 0) hp = WiFi.gatewayIP().toString() + ":8080";
'''
ph = sub(ph, old, new, "alamat relay")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Added: the *Relay* button finds the relay by itself on the same WiFi (RELAY_DISCOVER_V1). `relay_yt.py` broadcasts its address on UDP port 8081 once a second, and the phone listens for up to 3.5 s when you tap *Relay*. Order: `/relay.txt` on the SD card, then this discovery, then the WiFi gateway on port 8080. Not yet compiled or tested on a device. Script: `tools/patches/apply_relay_discover.py`\n", "changelog")
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
