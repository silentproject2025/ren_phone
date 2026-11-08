#!/usr/bin/env python3
"""Live scan BT (koprosesor CAM) + sambung TWS langsung + live scan WiFi (phone.ino).

Pakai dari folder proyek:   python apply_live_scan_patch.py
Aman dijalankan ulang (kalau sudah terpasang, dilewati). Kalau ada pola yg tidak cocok, berhenti
dengan pesan jelas dan TIDAK menulis file apa pun (semua-atau-tidak-sama-sekali).
"""
import os, sys

CAM  = "cam_bt_coprocessor/cam_bt_coprocessor.ino"
PH   = "phone.ino"
WF   = ".github/workflows/build-cam-coprocessor.yml"
MARK = "LIVE-SCAN"   # penanda sudah terpasang

def rd(p):
    with open(p, encoding="utf-8", newline="") as f: return f.read()

def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        sys.exit("GAGAL [%s]: pola ditemukan %d kali (harus 1). Kemungkinan file sudah beda dari versi yg saya baca." % (label, n))
    return s.replace(old, new)

out = {}

# =====================================================================
# 1) KOPROSESOR CAM: scan streaming + sambung langsung by-alamat
# =====================================================================
s = rd(CAM)
if MARK in s:
    print("[CAM] sudah terpasang, dilewati")
else:
    s = sub(s,
"static volatile int scannedCount = 0;\n",
"""static volatile int scannedCount = 0;
// LIVE-SCAN: perangkat dikirim ke S3 begitu ketemu (bukan nunggu scan habis) + sambung langsung by-alamat
static bool     scanSent[BT_MAX_DEVICES];
static bool     savedBdaValid = false;
static uint8_t  savedBda[6];
static volatile bool pendingConnect = false;
static uint32_t pendingConnectAt = 0;
""", "cam globals")

    # race fix: isi struct dulu, baru naikkan counter
    s = sub(s,
"    if (!dup) scanned[scannedCount++] = dev;\n",
"    if (!dup) { int k = scannedCount; scanned[k] = dev; scannedCount = k + 1; }   // LIVE-SCAN: tulis dulu, baru naikkan counter\n",
"cam append")

    # GAP callback sekarang aktif setelah scan -> pairing SSP/PIN harus dijawab di sini juga
    s = sub(s,
"    else if (p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) { isScanning = true; scanDone = false; }\n  }\n}\n",
"""    else if (p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) { isScanning = true; scanDone = false; }
  } else if (ev == ESP_BT_GAP_CFM_REQ_EVT) {            // pairing SSP (Just Works) -> setujui
    esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);
  } else if (ev == ESP_BT_GAP_PIN_REQ_EVT) {            // pairing legacy -> PIN 1234
    esp_bt_pin_code_t pin = {'1', '2', '3', '4'};
    esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin);
  }
}
""", "cam gap cb")

    s = sub(s,
"  scannedCount = 0;\n  memset(scanned, 0, sizeof(scanned));\n",
"  scannedCount = 0;\n  memset(scanned, 0, sizeof(scanned));\n  memset(scanSent, 0, sizeof(scanSent));\n",
"cam startScan")

    # kirim bertahap
    s = sub(s,
"""static void sendScanResults() {
  for (int i = 0; i < scannedCount; i++) {
    uint8_t b[1 + 40];
    b[0] = (uint8_t)(int8_t)scanned[i].rssi;
    int nl = strlen(scanned[i].name); if (nl > 39) nl = 39;
    memcpy(b + 1, scanned[i].name, nl);
    rpSend(Serial2, RP_SCANRES, b, 1 + nl);
  }
  rpSend(Serial2, RP_SCANDONE, nullptr, 0);
}
""",
"""// LIVE-SCAN: dipanggil dari loop() (satu-satunya pengirim UART). final=false -> kirim hanya yg sudah punya nama;
// final=true (scan selesai) -> kirim sisanya + RP_SCANDONE.
static void sendScanPartial(bool final) {
  int n = scannedCount;
  for (int i = 0; i < n; i++) {
    if (scanSent[i]) continue;
    if (!final && !scanned[i].hasName) continue;     // tunggu nama asli dulu, jangan kirim "BT:xx:xx:xx" kecuali scan habis
    scanSent[i] = true;
    uint8_t b[1 + 40];
    b[0] = (uint8_t)(int8_t)scanned[i].rssi;
    int nl = strlen(scanned[i].name); if (nl > 39) nl = 39;
    memcpy(b + 1, scanned[i].name, nl);
    rpSend(Serial2, RP_SCANRES, b, 1 + nl);
  }
  if (final) rpSend(Serial2, RP_SCANDONE, nullptr, 0);
}
static void sendScanResults() { sendScanPartial(true); }
""", "cam sendScan")

    # sambung langsung
    s = sub(s,
"""  btEnabled = true;                  // menyambung ke TWS = BT otomatis nyala
  a2dp.set_auto_reconnect(true);
  a2dp.start(savedBTName);
}
""",
"""  btEnabled = true;                  // menyambung ke TWS = BT otomatis nyala
  a2dp.set_auto_reconnect(true);
  // LIVE-SCAN: cari alamat (BDA) dari hasil scan tadi -> sambung LANGSUNG, tanpa a2dp.start() (yg bikin jeda + scan ulang)
  int idx = -1;
  for (int i = 0; i < scannedCount; i++)
    if (scanned[i].hasName && strcmp(scanned[i].name, name) == 0) { idx = i; break; }
  if (idx >= 0 && !btConnected) {
    memcpy(savedBda, scanned[idx].bda, 6); savedBdaValid = true;
    a2dp.set_auto_reconnect(savedBda);                 // simpan alamat utk reconnect otomatis
    if (isScanning) esp_bt_gap_cancel_discovery();     // inquiry yg masih jalan bikin connect lambat
    pendingConnect = true; pendingConnectAt = millis();
    btDisconnectedAt = millis();                       // kalau gagal, loop() akan reconnect() by-alamat
    Serial.printf("[BT] sambung langsung ke %s\\n", name);
    return;
  }
  savedBdaValid = false;                               // jalur lama (nama tdk ada di hasil scan / sedang tersambung)
  a2dp.start(savedBTName);
}
""", "cam connectTo")

    # prefs: simpan/baca BDA
    s = sub(s,
"  p.putInt(\"txgain\", txGain);\n  p.end();\n}\n",
"  p.putInt(\"txgain\", txGain);\n  if (savedBdaValid) p.putBytes(\"btbda\", savedBda, 6); else p.remove(\"btbda\");\n  p.end();\n}\n",
"cam savePrefs")
    s = sub(s,
"    txGain = constrain(p.getInt(\"txgain\", TX_GAIN_DEFAULT), 0, TX_GAIN_LEVELS - 1);\n    p.end();\n",
"    txGain = constrain(p.getInt(\"txgain\", TX_GAIN_DEFAULT), 0, TX_GAIN_LEVELS - 1);\n    if (p.getBytesLength(\"btbda\") == 6) { p.getBytes(\"btbda\", savedBda, 6); savedBdaValid = true; }   // LIVE-SCAN\n    p.end();\n",
"cam loadPrefs")
    s = sub(s,
"  a2dp.start(savedBTName[0] ? savedBTName : BT_DEFAULT_NAME);\n  delay(300);\n",
"  if (savedBdaValid) a2dp.set_auto_reconnect(savedBda);   // LIVE-SCAN: boot langsung reconnect by-alamat (tanpa scan nama)\n  a2dp.start(savedBTName[0] ? savedBTName : BT_DEFAULT_NAME);\n  delay(300);\n",
"cam boot start")

    # loop: connect tertunda + kirim bertahap
    s = sub(s,
"""    else if (c.type == RP_BTMODE && c.len >= 1) btSetEnabled(c.data[0] != 0);
  }
""",
"""    else if (c.type == RP_BTMODE && c.len >= 1) btSetEnabled(c.data[0] != 0);
  }

  // LIVE-SCAN: sambung langsung setelah inquiry berhenti (maks tunggu 1,5 dtk)
  if (pendingConnect && (!isScanning || millis() - pendingConnectAt > 1500)) {
    pendingConnect = false;
    a2dp.connect_to(savedBda);
  }
""", "cam loop connect")
    s = sub(s,
"  if (scanDone) { scanDone = false; sendScanResults(); }\n",
"  if (isScanning) sendScanPartial(false);                  // LIVE-SCAN: kirim perangkat begitu ketemu\n  if (scanDone) { scanDone = false; sendScanResults(); }\n",
"cam loop scan")
    out[CAM] = s
    print("[CAM] siap ditulis")

# =====================================================================
# 2) phone.ino: scan WiFi live (rescan otomatis + merge stabil)
# =====================================================================
s = rd(PH)
if MARK in s:
    print("[WIFI] sudah terpasang, dilewati")
else:
    a = s.find("void startWifiScan() {")
    b = s.find("void loadWifiCreds(){")
    if a < 0 or b < 0 or b < a or s.count("void startWifiScan() {") != 1:
        sys.exit("GAGAL [wifi]: blok startWifiScan/loadWifiCreds tidak ditemukan")
    NEW = r'''// LIVE-SCAN: scan WiFi gaya HP -- scan singkat (110 ms/channel, default 300) diulang otomatis tiap ~2,5 dtk
// selama layar Pengaturan terbuka (maks 60 dtk, diperpanjang tiap ketuk "Pindai Jaringan"). Hasil DIGABUNG ke daftar:
// urutan tidak berubah (target ketuk stabil), jaringan baru ditambah di bawah, sinyal diperbarui, yg hilang
// 3 putaran berturut-turut dibuang. Tidak mengulang saat sedang menyambung WiFi / mode pesawat / keyboard terbuka.
#define WIFI_SCAN_MS_PER_CHAN 110
#define WIFI_RESCAN_GAP_MS    2500UL
#define WIFI_LIVE_WINDOW_MS   60000UL
#define WIFI_MISS_DROP        3
bool wifiScanLive = false;
unsigned long wifiScanNextMs = 0, wifiScanLiveUntil = 0;
uint8_t wifiMiss[MAX_SCANNED_WIFI];

static void wifiKickScan() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false, false, WIFI_SCAN_MS_PER_CHAN);   // async, tanpa hidden, aktif, 110 ms/channel
  wifiScanning = true;
}

void startWifiScan() {            // scan baru dari nol (buka Pengaturan / ketuk Pindai)
  wifiKickScan();
  scannedWifiNum = 0;
  memset(wifiMiss, 0, sizeof(wifiMiss));
  wifiScanLive = true;
  wifiScanLiveUntil = millis() + WIFI_LIVE_WINDOW_MS;
  needRedrawNow();
}

void checkWifiScanComplete() {
  if (!wifiScanning) {
    if (wifiScanLive && !wifiConnecting && !airplaneMode && (long)(millis() - wifiScanNextMs) >= 0) {
      if ((long)(millis() - wifiScanLiveUntil) >= 0) wifiScanLive = false;
      else wifiKickScan();
    }
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  wifiScanning = false;
  wifiScanNextMs = millis() + WIFI_RESCAN_GAP_MS;
  if (n < 0) { WiFi.scanDelete(); needRedrawNow(); return; }      // scan gagal -> coba lagi di putaran berikut

  bool seen[MAX_SCANNED_WIFI];
  memset(seen, 0, sizeof(seen));
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    int rssi = WiFi.RSSI(i);
    bool enc = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    int j = -1;
    for (int k = 0; k < scannedWifiNum; k++) if (scannedWifis[k].ssid == ssid) { j = k; break; }
    if (j >= 0) {
      scannedWifis[j].rssi = rssi; scannedWifis[j].isEncrypted = enc;
      seen[j] = true; wifiMiss[j] = 0;
    } else if (scannedWifiNum < MAX_SCANNED_WIFI) {
      j = scannedWifiNum++;
      scannedWifis[j].ssid = ssid; scannedWifis[j].rssi = rssi; scannedWifis[j].isEncrypted = enc;
      seen[j] = true; wifiMiss[j] = 0;
    }
  }
  for (int j = 0; j < scannedWifiNum; j++) {                      // buang yg hilang beberapa putaran
    if (seen[j]) continue;
    if (++wifiMiss[j] >= WIFI_MISS_DROP) {
      for (int k = j; k < scannedWifiNum - 1; k++) {
        scannedWifis[k] = scannedWifis[k + 1]; wifiMiss[k] = wifiMiss[k + 1]; seen[k] = seen[k + 1];
      }
      scannedWifiNum--; j--;
    }
  }
  WiFi.scanDelete();
  needRedrawNow();
}

'''
    s = s[:a] + NEW + s[b:]
    s = sub(s, "#define MAX_SCANNED_WIFI 8\n", "#define MAX_SCANNED_WIFI 12\n", "wifi max")
    s = sub(s, "#define SETT_MAXROWS 40\n", "#define SETT_MAXROWS 48\n", "wifi maxrows")
    s = sub(s, "for(int i=0;i<scannedWifiNum && i<5;i++) settAddRow(SR_WIFI,g,32,i,nullptr,y);",
               "for(int i=0;i<scannedWifiNum && i<7;i++) settAddRow(SR_WIFI,g,32,i,nullptr,y);", "wifi rows")
    s = sub(s, "void settingsExit(){}", "void settingsExit(){ wifiScanLive=false; }", "wifi exit")
    out[PH] = s
    print("[WIFI] siap ditulis")

# =====================================================================
# 3) CI: buang jeda 10 dtk di library ESP32-A2DP (hanya build koprosesor)
# =====================================================================
s = rd(WF)
if "Percepat sambung TWS" in s:
    print("[CI] sudah terpasang, dilewati")
else:
    STEP = '''      - name: Percepat sambung TWS (hapus jeda 10 dtk di start() ESP32-A2DP)
        run: |
          F=$(find "$HOME/Arduino/libraries" -name BluetoothA2DPSource.cpp | head -n1)
          if [ -z "$F" ]; then echo "::warning::BluetoothA2DPSource.cpp tidak ketemu, patch jeda dilewati"; exit 0; fi
          grep -n "delay_ms(10000)" "$F" || echo "::warning::delay_ms(10000) tidak ada (versi library beda?)"
          sed -i 's/delay_ms(10000);/delay_ms(500);/' "$F"
          grep -n "delay_ms(500)" "$F" || true

'''
    s = sub(s, "      - name: Compile CAM sketch\n", STEP + "      - name: Compile CAM sketch\n", "ci step")
    out[WF] = s
    print("[CI] siap ditulis")

for p, txt in out.items():
    with open(p, "w", encoding="utf-8", newline="") as f: f.write(txt)
print("SELESAI: %d file diubah" % len(out))
