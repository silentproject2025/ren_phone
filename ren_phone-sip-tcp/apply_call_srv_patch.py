#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_srv_patch.py -- cari server SIP lewat DNS SRV (RFC 3263), seperti app Linphone.
#
# Kenapa: sip2sip.info menyuruh perangkat mencari servernya lewat DNS (SRV), bukan alamat/port tetap.
# Sebelumnya Ren Phone cuma lookup A biasa lalu konek ke port 5060.
#
# Yang berubah:
#  - Host tanpa port eksplisit & tanpa "domain/proxy" -> tanya DNS: _sip._tcp.<domain> (kalau mode TCP)
#    atau _sip._udp.<domain> (kalau UDP). Ambil record prioritas terendah (bobot tertinggi), port dari SRV.
#  - Paket DNS dibuat sendiri (ESP32 tdk punya fungsi SRV). Server DNS: dari WiFi, lalu cadangan 8.8.8.8.
#  - SRV tdk ada / gagal -> otomatis pakai lookup A + port seperti dulu.
#  - Hasil lookup disimpan 10 menit; kalau server berubah, sambungan TCP ditutup & daftar ulang.
#  - Baris diagnosa menampilkan "SRV <ip>:<port>" atau "SRV tdk ada, pakai A".
#
# WAJIB dipasang SETELAH apply_call_tcp_transport_patch.py.
#     python3 apply_call_srv_patch.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()

if 'callSrvLookup' in s:
    print("Patch SRV sudah terpasang, dilewati"); sys.exit(0)
if 'callTcpPref' not in s:
    print("ERROR: pasang dulu apply_call_tcp_transport_patch.py"); sys.exit(1)

edits = []

# 1. variabel port konfigurasi -------------------------------------------------------------------
edits.append(("var port cfg",
"static int  tPort = CALL_SIP_PORT;\n",
"static int  tPort = CALL_SIP_PORT;\nstatic int  tPortCfg = CALL_SIP_PORT;           // port dari konfigurasi (tPort bisa diganti hasil SRV)\nstatic bool tPortExplicit = false;               // host ditulis dgn \":port\" -> SRV dilewati\n"))

edits.append(("snapshot port",
"  if (c) { tPort = atoi(c + 1); *c = 0; if (tPort <= 0) tPort = CALL_SIP_PORT; }\n",
"  if (c) { tPort = atoi(c + 1); *c = 0; if (tPort <= 0) tPort = CALL_SIP_PORT; }\n  tPortExplicit = (c != nullptr); tPortCfg = tPort;\n"))

# 2. DNS SRV + callResolveServer baru --------------------------------------------------------------
OLD_RS = """static bool callResolveServer() {
  if (srvOk && millis() - srvAt < 600000UL) return true;
  if (!tHostName[0]) return false;
  char ip[16];
  if (!callResolve(tProxyName[0] ? tProxyName : tHostName, ip)) { srvOk = false; return false; }
  srvOk = true; srvAt = millis();
  callCopy(srvIpStr, sizeof(srvIpStr), ip);
  srvIp.fromString(ip);
  return true;
}
"""
NEW_RS = """// ---------------------------------------------------------------------
// DNS SRV (RFC 3263). ESP32 tdk punya fungsi SRV bawaan, jadi paket DNS dibuat & dibaca sendiri.
// ---------------------------------------------------------------------
static WiFiUDP dnsUdp;

// lewati nama DNS (label / pointer kompresi) mulai p; return posisi setelah nama, -1 kalau rusak
static int dnsSkipName(const uint8_t* b, int len, int p) {
  for (int g = 0; g < 64 && p < len; g++) {
    uint8_t c = b[p];
    if (c == 0) return p + 1;
    if ((c & 0xC0) == 0xC0) return (p + 2 <= len) ? p + 2 : -1;
    p += 1 + c;
  }
  return -1;
}

// baca nama DNS (dengan kompresi) ke out
static bool dnsReadName(const uint8_t* b, int len, int p, char* out, int cap) {
  int n = 0, jumps = 0;
  while (p < len) {
    uint8_t c = b[p];
    if (c == 0) { out[n] = 0; return n > 0; }
    if ((c & 0xC0) == 0xC0) {
      if (p + 1 >= len || ++jumps > 8) return false;
      p = ((c & 0x3F) << 8) | b[p + 1];
      continue;
    }
    if (c > 63 || p + 1 + c > len || n + c + 2 > cap) return false;
    if (n) out[n++] = '.';
    memcpy(out + n, b + p + 1, c); n += c; p += 1 + c;
  }
  return false;
}

// susun query SRV (type 33, class IN). return panjang paket, 0 kalau nama tdk valid
static int dnsBuildSrvQuery(uint8_t* out, int cap, uint16_t id, const char* qname) {
  if (cap < 20) return 0;
  memset(out, 0, 12);
  out[0] = (uint8_t)(id >> 8); out[1] = (uint8_t)id;
  out[2] = 0x01;                                    // RD (minta rekursif)
  out[5] = 1;                                       // QDCOUNT = 1
  int p = 12;
  const char* s = qname;
  while (*s) {
    const char* e = strchr(s, '.');
    int l = e ? (int)(e - s) : (int)strlen(s);
    if (l <= 0 || l > 63 || p + 1 + l + 5 > cap) return 0;
    out[p++] = (uint8_t)l; memcpy(out + p, s, (size_t)l); p += l;
    s += l; if (*s == '.') s++;
  }
  out[p++] = 0;
  out[p++] = 0; out[p++] = 33;                      // QTYPE SRV
  out[p++] = 0; out[p++] = 1;                       // QCLASS IN
  return p;
}

// baca balasan: pilih SRV prioritas terendah (bobot tertinggi). host = nama target, port dari record
static bool dnsParseSrv(const uint8_t* b, int len, uint16_t id, char* host, int hcap, int* port) {
  if (len < 12 || (uint16_t)((b[0] << 8) | b[1]) != id) return false;
  if (!(b[2] & 0x80)) return false;                 // bukan balasan
  if (b[2] & 0x02) return false;                    // terpotong (TC) -> anggap gagal
  if ((b[3] & 0x0F) != 0) return false;             // RCODE error (mis. NXDOMAIN)
  int qd = (b[4] << 8) | b[5], an = (b[6] << 8) | b[7];
  int p = 12;
  for (int i = 0; i < qd; i++) { p = dnsSkipName(b, len, p); if (p < 0) return false; p += 4; if (p > len) return false; }
  int bestPri = 65536, bestW = -1, bestPort = 0; bool found = false; char tmp[64];
  for (int i = 0; i < an; i++) {
    p = dnsSkipName(b, len, p);
    if (p < 0 || p + 10 > len) return false;
    int type = (b[p] << 8) | b[p + 1], rdl = (b[p + 8] << 8) | b[p + 9];
    p += 10;
    if (p + rdl > len) return false;
    if (type == 33 && rdl >= 7) {
      int pri = (b[p] << 8) | b[p + 1], w = (b[p + 2] << 8) | b[p + 3], prt = (b[p + 4] << 8) | b[p + 5];
      if (prt > 0 && dnsReadName(b, len, p + 6, tmp, sizeof(tmp)) && (pri < bestPri || (pri == bestPri && w > bestW))) {
        bestPri = pri; bestW = w; bestPort = prt; found = true;
        snprintf(host, (size_t)hcap, "%s", tmp);
      }
    }
    p += rdl;
  }
  if (!found) return false;
  *port = bestPort;
  return true;
}

// cari _sip._tcp / _sip._udp.<domain>; hasil: IP target (A-lookup) + port SRV
static bool callSrvLookup(const char* domain, bool tcp, char* ipOut, int* portOut) {
  char qn[160];
  if (snprintf(qn, sizeof(qn), "%s.%s", tcp ? "_sip._tcp" : "_sip._udp", domain) >= (int)sizeof(qn)) return false;
  uint8_t pkt[300], rx[512];
  uint16_t id = (uint16_t)(esp_random() & 0xFFFF);
  int ql = dnsBuildSrvQuery(pkt, sizeof(pkt), id, qn);
  if (ql <= 0) return false;
  IPAddress servers[2] = { WiFi.dnsIP(0), IPAddress(8, 8, 8, 8) };
  for (int sv = 0; sv < 2; sv++) {
    if (servers[sv] == IPAddress(0, 0, 0, 0)) continue;
    if (sv == 1 && servers[0] == servers[1]) continue;
    dnsUdp.begin(41234);
    dnsUdp.beginPacket(servers[sv], 53);
    dnsUdp.write(pkt, (size_t)ql);
    dnsUdp.endPacket();
    uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 1200UL) {
      if (dnsUdp.parsePacket() > 0) {
        int r = dnsUdp.read(rx, sizeof(rx));
        char host[64]; int port = 0;
        bool ok = (r > 0) && dnsParseSrv(rx, r, id, host, sizeof(host), &port);
        dnsUdp.stop();
        if (ok && callResolve(host, ipOut)) { *portOut = port; return true; }
        if (ok) return false;                       // SRV ada tapi targetnya tdk ter-resolve
        break;                                      // balasan tdk berguna (NXDOMAIN dll) -> coba server DNS berikutnya
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    dnsUdp.stop();
  }
  return false;
}

static bool callResolveServer() {
  if (srvOk && millis() - srvAt < 600000UL) return true;
  if (!tHostName[0]) return false;
  bool wasOk = srvOk;
  srvOk = false;                                     // paksa lookup sungguhan (callResolve() mengembalikan cache kalau srvOk true)
  char ip[16]; int port = tPortCfg;
  bool viaSrv = false;
  if (!tProxyName[0] && !tPortExplicit && !callLooksLikeIp(tHostName)) {
    viaSrv = callSrvLookup(tHostName, callTcpActive(), ip, &port);
    char dg[40];
    if (viaSrv) snprintf(dg, sizeof(dg), "SRV %s:%d", ip, port);
    else snprintf(dg, sizeof(dg), "SRV tdk ada, pakai A");
    callDbgPush(dg);
  }
  if (!viaSrv) {
    port = tPortCfg;
    if (!callResolve(tProxyName[0] ? tProxyName : tHostName, ip)) {
      if (wasOk) { srvOk = true; srvAt = millis() - 540000UL; return true; }   // DNS sesaat gagal: pakai yg lama, coba lagi 1 menit lagi
      return false;
    }
  }
  bool changed = (strcmp(ip, srvIpStr) != 0) || (port != tPort);
  srvOk = true; srvAt = millis();
  callCopy(srvIpStr, sizeof(srvIpStr), ip);
  srvIp.fromString(ip);
  tPort = port;
  if (changed) tcpClose("server berubah");           // sambungan lama menuju alamat lama
  return true;
}
"""
edits.append(("callResolveServer", OLD_RS, NEW_RS))

# 3. jatuh ke UDP -> alamat/port SRV utk UDP bisa beda -> lookup ulang -------------------------------
edits.append(("fallback invalidasi cache",
"""    tcpFallback = true; regPending = false; regNextAt = 0;
    callDbgPush("TCP gagal -> UDP");""",
"""    tcpFallback = true; regPending = false; regNextAt = 0;
    srvOk = false;                               // SRV utk UDP bisa menunjuk alamat/port lain -> lookup ulang
    callDbgPush("TCP gagal -> UDP");"""))

edits.append(("regStart lookup ulang",
"""  callRandHex(regCallId, 16); strcat(regCallId, "@nyx");""",
"""  if (!srvOk && !callResolveServer()) {
    callRegState = 3; callSetRegMsg("DNS server gagal");
    regNextAt = millis() + 15000UL; return;
  }
  callRandHex(regCallId, 16); strcat(regCallId, "@nyx");"""))

for name, old, new in edits:
    c = s.count(old)
    if c != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (name, c)); sys.exit(1)
for name, old, new in edits:
    s = s.replace(old, new)
open(F, 'w', encoding='utf-8').write(s)
print("OK: server SIP sekarang dicari lewat DNS SRV (%d perubahan), cadangan lookup A." % len(edits))
