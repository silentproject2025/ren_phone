#!/usr/bin/env python3
# apply_relay_v3.py -- RELAY_STREAM_V3  (butuh apply_relay_stream.py sudah terpasang; sudah termasuk RELAY_DISCOVER_V1)
# Jalankan dari root repo:  python3 tools/patches/apply_relay_v3.py
# Mengganti mjpegPlayRelay(): status tampil di LAYAR (mencari / menyambung / menunggu frame), alasan berhenti tampil 1,5 dtk,
# sentuhan baru dibaca setelah 0,8 dtk (jari yg baru mengetuk tombol tak langsung menghentikan), plus penemuan relay otomatis (UDP 8081).
import os, sys
M = "RELAY_STREAM_V3"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
if "RELAY_STREAM_V1" not in ph: sys.exit("GAGAL: jalankan apply_relay_stream.py dulu.")
a = ph.find("void mjpegPlayRelay(){"); b = ph.find("void mjpegEnter(){")
if a < 0 or b < 0 or ph.count("void mjpegPlayRelay(){") != 1 or b < a: sys.exit("GAGAL: fungsi mjpegPlayRelay tidak ditemukan 1x.")
FN = r'''void mjpegPlayRelay(){   // RELAY_STREAM_V3 (termasuk RELAY_DISCOVER_V1): status tampil di layar
  if(!wifiConnected){ showToast(RPTR("WiFi belum tersambung")); return; }
  if(!mjpegEnsureBuffer()){ showToast(RPTR("RAM tidak cukup utk MJPEG")); return; }
  int prevRotation = display.getRotation();
  display.setRotation(0);                           // panel native portrait (sama dgn pemutar file)
  auto msg = [&](const String& l1, const String& l2, uint16_t col){
    display.fillScreen(TFT_BLACK);
    display.setTextColor(col); display.setTextSize(1);
    display.setCursor(10, 20); display.print(l1);
    display.setCursor(10, 38); display.print(l2);
  };
  auto bye = [&](const String& l1, const String& l2){   // tampilkan alasan sebentar lalu kembali ke daftar
    msg(l1, l2, 0xFFE0); delay(1500);
    display.setRotation(prevRotation); needRedrawNow();
  };

  String hp = "";
  if(sdReady && SD_MMC.exists("/relay.txt")){
    File rf = SD_MMC.open("/relay.txt", FILE_READ);
    if(rf){ hp = rf.readStringUntil('\n'); hp.trim(); rf.close(); }
  }
  if(hp.length() == 0){            // relay memancarkan "RENRELAY <port>" ke UDP 8081 tiap 1 dtk
    msg("Mencari relay...", "(maks 3,5 dtk)", 0xFFFF);
    WiFiUDP du;
    if(du.begin(8081)){
      uint32_t t0 = millis();
      while(millis() - t0 < 3500){
        int n = du.parsePacket();
        if(n > 0){
          char bf[40]; int m = du.read(bf, sizeof(bf) - 1); if(m < 0) m = 0; bf[m] = 0;
          if(strncmp(bf, "RENRELAY ", 9) == 0){
            hp = du.remoteIP().toString() + ":" + String(atoi(bf + 9));
            break;
          }
        }
        delay(20); yield();
      }
      du.stop();
    }
  }
  if(hp.length() == 0) hp = WiFi.gatewayIP().toString() + ":8080";
  String host = hp; uint16_t port = 8080;
  int ci = hp.indexOf(':');
  if(ci > 0){ host = hp.substring(0, ci); port = (uint16_t)hp.substring(ci + 1).toInt(); }

  msg("Menyambung ke relay", hp, 0xFFFF);
  WiFiClient rc;
  rc.setTimeout(4);
  if(!rc.connect(host.c_str(), port, 4000)){
    Serial.printf("[RELAY] gagal connect %s:%u\n", host.c_str(), (unsigned)port);
    bye("Relay tak terjangkau", hp);
    return;
  }
  rc.print(String("GET /live?w=") + (int)RP_DISP_NATIVE_W + "&h=" + (int)RP_DISP_NATIVE_H +   // ukuran panel portrait (v2: 320x480)
           " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
  String st = rc.readStringUntil('\n');
  if(st.indexOf(" 200") < 0){
    rc.stop();
    bye("Relay kosong", "Pilih video di browser relay");
    return;
  }
  while(rc.connected() || rc.available()){          // lewati header HTTP
    String hl = rc.readStringUntil('\n');
    if(hl.length() <= 1) break;
  }
  if(!mjpegAcquire()){ rc.stop(); bye("RAM tidak cukup", "utk pemutar MJPEG"); return; }

  msg("Terhubung ke relay", hp + "  | menunggu frame...", 0xFFFF);
  mjpegPlaying       = true;
  mjpegStopRequested = false;
  mjpegTotalFrames   = 0;
  mjpegStartMs       = millis();
  mjpegObj->setup(&rc, mjpegBuf, mjpegDrawCallback, true, 0, 0, display.width(), display.height());

  int frameCheckCounter = 0;
  bool goHomeAfter = false, byTouch = false;
  uint32_t armMs = millis() + 800;                  // abaikan sentuhan 0,8 dtk pertama (jari yg baru mengetuk tombol)
  while(!mjpegStopRequested && (rc.connected() || rc.available()) && mjpegObj->readMjpegBuf()){
    mjpegObj->drawJpg();
    mjpegTotalFrames++;
    if(++frameCheckCounter >= 2){
      frameCheckCounter = 0;
      lgfx::touch_point_t tp;
      if((int32_t)(millis() - armMs) > 0 && getInputPoint(tp)){
        int g = mjpegClassifyTouch(tp);
        mjpegStopRequested = true; byTouch = true;  // sentuhan = berhenti
        if(g == MJPEG_GESTURE_SWIPE_HOME) goHomeAfter = true;
      }
    }
    if(wifiConnected && webServerRunning) webServer.handleClient();
    yield();
  }
  rc.stop();
  mjpegRelease();
  mjpegPlaying       = false;
  mjpegStopRequested = false;
  unsigned long used = millis() - mjpegStartMs;
  Serial.printf("[RELAY] selesai | frame=%lu | %lums | fps=%.1f\n", mjpegTotalFrames, used,
                used > 0 ? (1000.0f * mjpegTotalFrames / used) : 0.0f);
  if(goHomeAfter){ display.setRotation(prevRotation); navGoHome(); return; }
  if(byTouch){ display.setRotation(prevRotation); needRedrawNow(); return; }
  bye(mjpegTotalFrames == 0 ? "Relay tidak mengirim frame" : "Stream berhenti", String("frame: ") + (unsigned long)mjpegTotalFrames);
}

'''
ph = ph[:a] + FN + ph[b:]
i = cl.find("## Unreleased\n")
if i < 0: sys.exit("GAGAL: CHANGELOG")
cl = cl.replace("## Unreleased\n", "## Unreleased\n- Changed: the Relay player shows what it is doing on the screen (looking for the relay, connecting, waiting for the first frame) and, when it stops by itself, why and after how many frames (RELAY_STREAM_V3). Touches in the first 0.8 s are ignored so the finger that tapped the button does not stop the video at once. Not yet compiled or tested on a device. Script: `tools/patches/apply_relay_v3.py`\n", 1)
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
