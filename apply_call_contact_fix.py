#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_contact_fix.py -- perbaiki panggilan tersambung tapi tanpa suara & terputus sendiri.
#
# Bukti dari layar: "rx 200 INVITE" -> "rx 482 ACK" -> "rx BYE <ip server>". ACK untuk 200 OK dijawab
# server dengan 482 (Loop Detected), jadi ACK tak pernah sampai ke lawan; lawan lalu mengakhiri sendiri.
# Sebab: alamat tujuan ACK/BYE (Request-URI) diambil dari header Contact tapi DIPOTONG di tanda ';',
# padahal Flexisip (sip.linphone.org) menaruh parameter penting di dalam <...> (mis. fs-conn-id, gr, transport)
# agar proxy tahu ke mana meneruskannya. Tanpa parameter itu proxy salah arah -> 482.
# Selain itu buffer Contact cuma 110 byte sehingga Contact panjang ikut terpotong.
#
# Perbaikan: ambil URI utuh sampai '>' (bentuk <...>), buffer diperbesar ke 200 byte.
#
#     python3 apply_call_contact_fix.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'sipContactUri' in s:
    print("Patch contact sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# 1. buffer lebih besar
s = rep(s, "  char target[110], route[260], ltag[14], branch[28];",
           "  char target[200], route[260], ltag[14], branch[28];", "dlg.target")
s = rep(s, "  char cseq[40]; char rr[260]; char contact[110];",
           "  char cseq[40]; char rr[260]; char contact[200];", "rq.contact")

# 2. helper (sebelum callOnInvite)
s = rep(s, "static void callOnInvite(const char* msg, const char* srcIp, int srcPort) {",
'''// URI dari header Contact. Bentuk "<sip:...;param>": ambil SEMUA sampai '>' (parameter URI dipakai proxy NAT,
// mis. Flexisip fs-conn-id/gr). Bentuk tanpa kurung: berhenti di ';' / spasi / ','.
static bool sipContactUri(const char* ct, char* out, int cap) {
  const char* s = strstr(ct, "sip:");
  if (!s) s = strstr(ct, "sips:");
  if (!s) return false;
  bool br = false;
  for (const char* q = ct; q < s; q++) if (*q == '<') br = true;
  const char* e = s;
  while (*e && *e != '>' && !(!br && (*e == ';' || *e == ' ' || *e == ','))) e++;
  int l = (int)(e - s);
  if (l <= 0 || l >= cap) return false;
  memcpy(out, s, (size_t)l); out[l] = 0;
  return true;
}

static void callOnInvite(const char* msg, const char* srcIp, int srcPort) {''', "helper")

# 3. panggilan masuk: Contact penelepon
s = rep(s, "int p; char ct[110];\n    callCopy(ct, sizeof(ct), rq.contact);",
           "int p; char ct[200];\n    callCopy(ct, sizeof(ct), rq.contact);", "incoming ct")
s = rep(s, '''      char* e = ct; while (*e && *e != '>' && *e != ';') e++;   // buang parameter
      const char* s = strstr(ct, "sip:");
      int l = (int)(e - (s ? s : ct));
      if (s && l > 0 && l < (int)sizeof(dlg.target) - 1) { memcpy(dlg.target, s, (size_t)l); dlg.target[l] = 0; }
''', "      sipContactUri(ct, dlg.target, sizeof(dlg.target));\n", "incoming target")

# 4. panggilan keluar: Contact lawan di 200 OK
s = rep(s, 'char to[170], ct[110]; sipHdr(msg, "To", "t", 0, to, sizeof(to));\n    callCopy(dlg.remoteHdr',
           'char to[170], ct[200]; sipHdr(msg, "To", "t", 0, to, sizeof(to));\n    callCopy(dlg.remoteHdr', "200 ct")
s = rep(s, '''      const char* s = strstr(ct, "sip:");
      if (s) {
        const char* e = s; while (*e && *e != '>' && *e != ';') e++;
        int l = (int)(e - s);
        if (l > 0 && l < (int)sizeof(dlg.target) - 1) { memcpy(dlg.target, s, (size_t)l); dlg.target[l] = 0; }
      }
''', "      sipContactUri(ct, dlg.target, sizeof(dlg.target));\n", "200 target")

open(F, 'w', encoding='utf-8').write(s)
print("OK: Request-URI ACK/BYE sekarang memakai Contact utuh (parameter proxy tidak dibuang)")
