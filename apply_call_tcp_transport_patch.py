#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_tcp_transport_patch.py -- SIP ke SERVER lewat TCP polos (tanpa TLS) di app Telepon.
#
# Yang berubah:
#  - REGISTER/INVITE/ACK/BYE/dll ke server Linphone dikirim lewat 1 sambungan TCP yang dijaga tetap hidup
#    (Via jadi SIP/2.0/TCP, Contact diberi ;transport=tcp). Panggilan masuk datang lewat sambungan itu,
#    jadi tidak bergantung lubang NAT UDP.
#  - Retransmisi UDP dimatikan utk pesan yg lewat TCP (TCP sudah andal).
#  - Sambungan putus -> otomatis sambung ulang + REGISTER ulang.
#  - TCP gagal konek 3x berturut-turut -> otomatis jatuh ke UDP (sampai config disimpan / WiFi putus-nyambung).
#  - Mode LAN antar-HP, scan, dan RTP tetap UDP seperti sebelumnya.
#  - Pilihan transport: default TCP. Ganti lewat /sip.cfg ("transport=udp" atau "transport=tcp"),
#    lalu tekan tombol muat dari SD di halaman Atur.
#
#     python3 apply_call_tcp_transport_patch.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()

if 'callTcpPref' in s:
    print("Patch TCP sudah terpasang, dilewati"); sys.exit(0)

edits = []   # (nama, old, new)

# 1. preferensi transport ------------------------------------------------------------------------
edits.append(("pref transport",
"static bool   callStandby = true;\n",
"""static bool   callStandby = true;
#ifndef CALL_TCP_DEFAULT
#define CALL_TCP_DEFAULT 1
#endif
static bool   callTcpPref = (CALL_TCP_DEFAULT != 0);   // SIP ke server lewat TCP (false = UDP); sip.cfg: transport=udp|tcp
"""))

# 2. transport TCP + sipTxTo/sipSendKeep/sipResend -----------------------------------------------
OLD_NET = """static void sipTxTo(const char* ip, int port, const char* data, int len) {
  if (!sipOpen || len <= 0) return;
  IPAddress a;
  if (!a.fromString(ip)) return;
  sipUdp.beginPacket(a, (uint16_t)port);
  sipUdp.write((const uint8_t*)data, (size_t)len);
  sipUdp.endPacket();
}

// kirim sipTx[0..len) dan simpan utk retransmisi
static void sipSendKeep(const char* ip, int port, int len) {
  if (len <= 0 || len >= SIP_BUF) return;
  sipTxTo(ip, port, sipTx, len);
  memcpy(sipRtx, sipTx, (size_t)len); sipRtx[len] = 0;
  sipRtxLen = len; callCopy(sipRtxIp, sizeof(sipRtxIp), ip); sipRtxPort = port;
}

static void sipResend() {
  if (sipRtxLen > 0) sipTxTo(sipRtxIp, sipRtxPort, sipRtx, sipRtxLen);
}
"""
NEW_NET = """// ---------------------------------------------------------------------
// TRANSPORT TCP KE SERVER (opsional; SIP LAN antar-HP, scan & RTP tetap UDP)
// ---------------------------------------------------------------------
static WiFiClient srvTc;                         // sambungan ke server SIP (nama 'sipTcp' sdh dipakai server diagnosa)
static bool     tcpUp = false, tcpFallback = false;
static uint8_t  tcpFails = 0;
static uint32_t tcpNextTry = 0;
static char*    tcpBuf = nullptr;                // aliran masuk (SIP_BUF * 2)
static char*    tcpTxBuf = nullptr;              // salinan keluar utk ganti Via UDP -> TCP (SIP_BUF)
static int      tcpLen = 0;
static bool     sipRtxTcp = false;               // pesan retransmisi terakhir lewat TCP? (kalau ya, tdk perlu dikirim ulang)
static void callOnSip(const char* msg, int len, const char* srcIp, int srcPort);   // deklarasi maju

static bool callTcpActive() { return callTcpPref && !tcpFallback && tcpBuf != nullptr; }
static bool callViaTcp(const char* ip) { return callTcpActive() && srvOk && ip && strcmp(ip, srvIpStr) == 0; }

static void tcpReset() {
  srvTc.stop(); tcpUp = false; tcpLen = 0; tcpFails = 0; tcpFallback = false; tcpNextTry = 0;
}

static void tcpClose(const char* why) {
  bool was = tcpUp;
  srvTc.stop(); tcpUp = false; tcpLen = 0;
  if (was) {
    if (why && why[0]) callDbgPush(why);
    regPending = false; regNextAt = 0;           // ikatan registrasi ikut hilang bersama sambungan -> daftar ulang
  }
}

static bool tcpConnect(int timeoutMs) {
  if (tcpUp) return true;
  if (!srvOk) return false;
  uint32_t now = millis();
  if ((int32_t)(now - tcpNextTry) < 0) return false;
  srvTc.stop();
  if (srvTc.connect(srvIp, (uint16_t)tPort, timeoutMs)) {
    srvTc.setNoDelay(true);
    tcpUp = true; tcpLen = 0; tcpFails = 0;
    callDbgPush("TCP tersambung");
    regNextAt = 0;                               // sambungan baru belum punya registrasi
    return true;
  }
  tcpFails++;
  tcpNextTry = millis() + (tcpFails < 5 ? 2000UL * tcpFails : 10000UL);
  if (tcpFails >= 3) {
    tcpFallback = true; regPending = false; regNextAt = 0;
    callDbgPush("TCP gagal -> UDP");
  } else callDbgPush("TCP gagal konek");
  return false;
}

static void tcpSend(const char* data, int len) {
  if (len <= 0 || len >= SIP_BUF || !tcpTxBuf) return;
  if (!tcpUp) {
    if (len <= 4) return;                        // keepalive tdk perlu memicu sambung ulang
    if (!tcpConnect(callPhase == CPH_IDLE ? 3000 : 1000)) return;
  }
  memcpy(tcpTxBuf, data, (size_t)len); tcpTxBuf[len] = 0;
  for (char* p = tcpTxBuf; (p = strstr(p, "SIP/2.0/UDP")) != nullptr; p += 11) { p[8] = 'T'; p[9] = 'C'; p[10] = 'P'; }
  int w = (int)srvTc.write((const uint8_t*)tcpTxBuf, (size_t)len);
  if (w != len) tcpClose("TCP kirim gagal");
}

// Ambil satu pesan SIP utuh dari awal aliran TCP (framing lewat Content-Length).
// return: byte yg dikonsumsi dari awal buf; 0 = belum lengkap; -1 = aliran rusak/terlalu besar.
// *off, *mlen = posisi & panjang pesan di buf (mlen 0 = hanya CRLF keepalive/pong).
static int sipStreamTake(const char* buf, int len, int maxTotal, int* off, int* mlen) {
  int i = 0;
  while (i < len && (buf[i] == '\\r' || buf[i] == '\\n' || buf[i] == ' ')) i++;
  *off = i; *mlen = 0;
  if (i >= len) return len;
  int he = -1;
  for (int k = i; k + 3 < len; k++) {
    if (buf[k] == '\\r' && buf[k + 1] == '\\n' && buf[k + 2] == '\\r' && buf[k + 3] == '\\n') { he = k; break; }
  }
  if (he < 0) return (len - i > maxTotal) ? -1 : 0;
  int cl = 0;
  for (int k = i; k < he; ) {
    int e = k; while (e < he && buf[e] != '\\n') e++;
    int c = -1;
    if (e - k > 15 && strncasecmp(buf + k, "content-length", 14) == 0) c = k + 14;
    else if (e - k > 2 && (buf[k] == 'l' || buf[k] == 'L')) c = k + 1;
    if (c >= 0) {
      while (c < e && buf[c] == ' ') c++;
      if (c < e && buf[c] == ':') { c++; while (c < e && buf[c] == ' ') c++; cl = atoi(buf + c); }
    }
    k = e + 1;
  }
  if (cl < 0 || he + 4 + cl > maxTotal) return -1;
  int total = he + 4 + cl;
  if (len < total) return 0;
  *mlen = total - i;
  return total;
}

static void tcpPoll() {
  if (!tcpUp || !tcpBuf) return;
  int guard = 0;
  while (guard++ < 6 && srvTc.available() > 0) {
    int room = SIP_BUF * 2 - tcpLen;
    if (room <= 0) { tcpLen = 0; callDbgPush("TCP buf penuh"); break; }
    int r = srvTc.read((uint8_t*)tcpBuf + tcpLen, (size_t)room);
    if (r <= 0) break;
    tcpLen += r;
    for (int g = 0; g < 8 && tcpLen > 0; g++) {
      int off = 0, ml = 0;
      int used = sipStreamTake(tcpBuf, tcpLen, SIP_BUF * 2, &off, &ml);
      if (used < 0) { tcpLen = 0; callDbgPush("TCP aliran rusak"); break; }
      if (used == 0) break;
      if (ml > 0 && ml < SIP_BUF) {
        memcpy(sipRx, tcpBuf + off, (size_t)ml); sipRx[ml] = 0;
        callOnSip(sipRx, ml, srvIpStr, tPort);
        if (!tcpUp) { tcpLen = 0; return; }      // handler menutup sambungan
      } else if (ml >= SIP_BUF) callDbgPush("TCP pesan >1.5KB");
      tcpLen -= used;
      if (tcpLen > 0) memmove(tcpBuf, tcpBuf + used, (size_t)tcpLen);
    }
  }
  if (tcpUp && !srvTc.connected() && srvTc.available() <= 0) tcpClose("TCP putus");
}

// ---------------------------------------------------------------------
// JARINGAN DASAR
// ---------------------------------------------------------------------
static void sipTxTo(const char* ip, int port, const char* data, int len) {
  if (!sipOpen || len <= 0) return;
  if (callViaTcp(ip)) { tcpSend(data, len); return; }
  IPAddress a;
  if (!a.fromString(ip)) return;
  sipUdp.beginPacket(a, (uint16_t)port);
  sipUdp.write((const uint8_t*)data, (size_t)len);
  sipUdp.endPacket();
}

// kirim sipTx[0..len) dan simpan utk retransmisi
static void sipSendKeep(const char* ip, int port, int len) {
  if (len <= 0 || len >= SIP_BUF) return;
  sipTxTo(ip, port, sipTx, len);
  memcpy(sipRtx, sipTx, (size_t)len); sipRtx[len] = 0;
  sipRtxLen = len; callCopy(sipRtxIp, sizeof(sipRtxIp), ip); sipRtxPort = port;
  sipRtxTcp = callViaTcp(ip) && tcpUp;           // terkirim lewat TCP -> jangan dikirim ulang (nanti dobel)
}

static void sipResend() {
  if (sipRtxLen > 0 && !sipRtxTcp) sipTxTo(sipRtxIp, sipRtxPort, sipRtx, sipRtxLen);
}
"""
edits.append(("transport TCP + sipTxTo", OLD_NET, NEW_NET))

# 3. Contact -------------------------------------------------------------------------------------
edits.append(("Contact",
"""static void callContactStr(char* out, int cap, bool lan) {
  if (!lan && natIp[0] && natPort > 0)""",
"""static void callContactStr(char* out, int cap, bool lan) {
  if (!lan && callTcpActive()) {                  // TCP: rport = port acak sambungan keluar, jangan dipakai
    snprintf(out, cap, "%s:%d;transport=tcp", lip, CALL_SIP_PORT);
    return;
  }
  if (!lan && natIp[0] && natPort > 0)"""))

# 4. regStart: pastikan TCP tersambung sebelum REGISTER ----------------------------------------
edits.append(("regStart",
"""    regNextAt = millis() + 15000UL; return;
  }
  callRandHex(regCallId, 16); strcat(regCallId, "@nyx");""",
"""    regNextAt = millis() + 15000UL; return;
  }
  if (callTcpActive() && !tcpUp && !tcpConnect(callPhase == CPH_IDLE ? 3000 : 1000)) {
    if (callTcpActive()) {                        // masih TCP (belum jatuh ke UDP): coba lagi setelah jeda
      if (callRegState != 2) { callRegState = 3; callSetRegMsg("TCP belum tersambung"); }
      regNextAt = ((int32_t)(tcpNextTry - millis()) > 0) ? tcpNextTry : millis() + 2000UL;
      return;
    }
  }
  callRandHex(regCallId, 16); strcat(regCallId, "@nyx");"""))

# 5. buffer --------------------------------------------------------------------------------------
edits.append(("buffer total",
"const size_t total = (SIP_BUF * 3) + 512 + (CALL_SAMPLES * 4 * 4) + (CALL_JB * 320 * 2) + 64;",
"const size_t total = (SIP_BUF * 3) + 512 + (CALL_SAMPLES * 4 * 4) + (CALL_JB * 320 * 2) + 64 + (SIP_BUF * 3);"))
edits.append(("buffer pointer",
"  jbPcm  = (int16_t*)(b + SIP_BUF * 3 + 512 + CALL_SAMPLES * 4 * 4);\n",
"""  jbPcm  = (int16_t*)(b + SIP_BUF * 3 + 512 + CALL_SAMPLES * 4 * 4);
  tcpBuf = (char*)(b + SIP_BUF * 3 + 512 + CALL_SAMPLES * 4 * 4 + CALL_JB * 320 * 2 + 64);
  tcpTxBuf = tcpBuf + SIP_BUF * 2;
"""))

# 6. snapshot config -> reset TCP ----------------------------------------------------------------
edits.append(("snapshot cfg",
"""  srvOk = false; natIp[0] = 0; natPort = 0;
  regPending = false; regNextAt = 0; callRegState = 0;
  tCfgGen = callCfgGen;""",
"""  srvOk = false; natIp[0] = 0; natPort = 0;
  tcpReset();
  regPending = false; regNextAt = 0; callRegState = 0;
  tCfgGen = callCfgGen;"""))

# 7. task: WiFi putus -> reset TCP ---------------------------------------------------------------
edits.append(("WiFi putus",
"      if (sipOpen) { sipUdp.stop(); sipTcp.end(); sipOpen = false; }",
"      tcpReset();\n      if (sipOpen) { sipUdp.stop(); sipTcp.end(); sipOpen = false; }"))

# 8. task: baca TCP ------------------------------------------------------------------------------
edits.append(("tcpPoll",
"""      callMarkT(61);
    }
""",
"""      callMarkT(61);
    }
    tcpPoll();                                              // SIP lewat TCP dari server
"""))

# 9. konfigurasi ---------------------------------------------------------------------------------
edits.append(("NVS load",
'    callStandby = p.getBool("standby", false);\n',
'    callStandby = p.getBool("standby", false);\n    callTcpPref = p.getBool("tcp", CALL_TCP_DEFAULT != 0);\n'))
edits.append(("NVS save",
'    p.putBool("standby", callStandby); p.putString("lanip", callLanIpS);\n',
'    p.putBool("standby", callStandby); p.putString("lanip", callLanIpS);\n    p.putBool("tcp", callTcpPref);\n'))
edits.append(("sip.cfg decl",
'  String h = "", u = "", w = "", n = "";\n  while (f.available()) {\n    String l = f.readStringUntil(\'\\n\'); l.trim();\n    if (l.startsWith("host=")) h = l.substring(5);\n    else if (l.startsWith("user=")) u = l.substring(5);',
'  String h = "", u = "", w = "", n = "", tr = "";\n  while (f.available()) {\n    String l = f.readStringUntil(\'\\n\'); l.trim();\n    if (l.startsWith("host=")) h = l.substring(5);\n    else if (l.startsWith("transport=")) tr = l.substring(10);\n    else if (l.startsWith("user=")) u = l.substring(5);'))
edits.append(("sip.cfg apply",
"  if (n.length()) callNameS = n;\n  return true;",
"  if (n.length()) callNameS = n;\n  tr.trim(); if (tr.length()) callTcpPref = tr.equalsIgnoreCase(\"tcp\");\n  return true;"))

# --- terapkan: semua anchor harus ketemu tepat 1x, kalau tidak, tidak ada yang diubah -------------
for name, old, new in edits:
    c = s.count(old)
    if c != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (name, c)); sys.exit(1)
for name, old, new in edits:
    s = s.replace(old, new)
open(F, 'w', encoding='utf-8').write(s)
print("OK: SIP ke server sekarang lewat TCP (%d perubahan). Default TCP; /sip.cfg 'transport=udp' utk kembali ke UDP." % len(edits))
