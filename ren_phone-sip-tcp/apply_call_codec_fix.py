#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_codec_fix.py -- perbaiki "Codec tidak cocok" di app Telepon (call_renphone.ino).
#
# Jalankan dari folder repo (ada call_renphone.ino):
#     python3 apply_call_codec_fix.py
# Idempoten & semua-atau-tidak-sama-sekali (anchor harus ketemu TEPAT 1x).
#
# Yang diubah:
#  1. callParseSdp() ditulis ulang: tahan baris "\n" saja, lewati stream audio port 0,
#     terima alamat c= berupa hostname, dan MENCATAT alasan gagal (sdpWhy).
#  2. Toast sekarang menyebut penyebab asli (SRTP / bukan G.711 / audio ditolak / dst),
#     bukan selalu "Codec tidak cocok".
#  3. Balasan SIP 488 menampilkan header Warning dari lawan (kalau ada) + dicetak ke Serial.
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)

s = open(F, encoding='utf-8').read()
if 'sdpWhy' in s:
    print("Patch codec sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# ---- 1. ganti seluruh callParseSdp ----
a = s.find("static bool callParseSdp(const char* body) {")
if a < 0 or s.count("static bool callParseSdp(const char* body) {") != 1:
    print("ERROR: callParseSdp tidak ketemu"); sys.exit(1)
b = s.find("\n}\n", a)
if b < 0:
    print("ERROR: akhir callParseSdp tidak ketemu"); sys.exit(1)
b += 3

NEW = r'''// alasan SDP lawan ditolak (utk toast). nullptr = tidak ada masalah
static const char* sdpWhy = nullptr;

static const char* sdpEol(const char* p) {            // akhir baris: \r atau \n
  while (*p && *p != '\r' && *p != '\n') p++;
  return p;
}

static bool callParseSdp(const char* body) {
  sdpWhy = nullptr;
  if (!body || !body[0]) { sdpWhy = "Lawan tdk kirim SDP"; return false; }

  // cari baris m=audio pertama yang port-nya > 0; kalau profilnya SRTP (SAVP) catat saja
  const char* m = body;
  const char* hit = nullptr;
  bool sawSrtp = false, sawZeroPort = false;
  while ((m = strstr(m, "m=audio ")) != nullptr) {
    int port = atoi(m + 8);
    const char* eol = sdpEol(m);
    bool plain = false;
    for (const char* q = m; q + 8 <= eol; q++) {
      if (!strncmp(q, "RTP/AVP", 7)) { plain = true; break; }   // juga cocok "RTP/AVPF"
      if (!strncmp(q, "RTP/SAVP", 8)) { sawSrtp = true; }
    }
    if (port <= 0) sawZeroPort = true;
    else if (plain) { hit = m; break; }
    m = eol;
  }
  if (!hit) {
    sdpWhy = sawSrtp ? "Lawan wajib SRTP (enkripsi)"
           : sawZeroPort ? "Lawan menolak audio"
           : "SDP lawan tanpa audio";
    return false;
  }
  m = hit;
  int port = atoi(m + 8);

  // alamat media: c= di bagian media dulu, lalu c= tingkat sesi
  const char* c = nullptr;
  const char* mEol = sdpEol(m);
  const char* nextM = strstr(mEol, "\nm=");
  const char* cm = strstr(mEol, "c=IN IP4 ");
  if (cm && (!nextM || cm < nextM)) c = cm;
  if (!c) c = strstr(body, "c=IN IP4 ");
  if (!c) { sdpWhy = "SDP lawan tanpa alamat IPv4"; return false; }
  char host[64]; int n = 0; c += 9;
  while (*c && *c != '\r' && *c != '\n' && *c != '/' && *c != ' ' && n < 63) host[n++] = *c++;
  host[n] = 0;
  char ip[16];
  if (!callResolve(host, ip) || !ip[0]) { sdpWhy = "Alamat media lawan tak valid"; return false; }
  if (strcmp(ip, "0.0.0.0") == 0) { sdpWhy = "Lawan minta panggilan ditahan"; return false; }

  // daftar payload di baris m= : ambil PCMU(0) / PCMA(8)
  const char* av = strstr(m, "RTP/AVP");
  int pick = -1;
  if (av) {
    const char* p = av + 7;
    while (*p && *p != '\r' && *p != '\n') {
      while (*p == ' ') p++;
      if (*p == 'F' && p[-1] == 'P') p++;          // "RTP/AVPF"
      while (*p == ' ') p++;
      if (!isdigit((unsigned char)*p)) break;
      int v = atoi(p);
      if (pick < 0 && (v == 0 || v == 8)) pick = v;
      while (isdigit((unsigned char)*p)) p++;
    }
  }
  if (pick < 0) { sdpWhy = "Lawan tdk dukung G.711"; return false; }
  callCopy(dlg.rtpIp, sizeof(dlg.rtpIp), ip);
  dlg.rtpPort = port;
  dlg.pt = (uint8_t)pick;
  dlg.has101 = strstr(body, "telephone-event/8000") != nullptr;
  return true;
}
'''
s = s[:a] + NEW + s[b:]

# ---- 2. toast 200 OK: alasan asli ----
s = rep(s,
  'callFinish(sdpOk ? "Dibatalkan" : "Codec tidak cocok");',
  'callFinish(sdpOk ? "Dibatalkan" : (sdpWhy ? sdpWhy : "Codec tidak cocok"));', "toast 200 OK")

# ---- 3. 488: tampilkan Warning lawan + log Serial ----
s = rep(s,
  '    case 488: callCopy(b, sizeof(b), "Codec tidak cocok"); break;',
  '''    case 488: {
      char wn[80]; wn[0] = 0;
      sipHdr(msg, "Warning", "", 0, wn, sizeof(wn));
      Serial.printf("[CALL] 488 dari lawan. Warning: %s\\n", wn[0] ? wn : "(kosong)");
      const char* why = "Ditolak 488: codec/enkripsi";
      if (strstr(wn, "301") || strstr(wn, "302") || strstr(wn, "304")) why = "Lawan tdk cocok (media)";
      callCopy(b, sizeof(b), why);
    } break;''', "case 488")

open(F, 'w', encoding='utf-8').write(s)
print("OK: call_renphone.ino diperbarui (callParseSdp + toast alasan asli + log 488)")
