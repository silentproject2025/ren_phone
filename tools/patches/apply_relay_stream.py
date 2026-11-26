#!/usr/bin/env python3
# apply_relay_stream.py -- RELAY_STREAM_V1
# Jalankan dari root repo:  python3 tools/patches/apply_relay_stream.py
# App Video: tombol [Relay] di header memutar stream MJPEG dari relay (tools/relay/relay_yt.py) lewat WiFi.
# Alamat relay: otomatis = gateway WiFi:8080 (relay jalan di HP pemilik hotspot), atau isi /relay.txt di SD ("192.168.x.x:8080").
# Sentuh layar saat memutar = berhenti; swipe-atas dari tepi bawah = Home.
import os, sys
M = "RELAY_STREAM_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)

FN = r'''// RELAY_STREAM_V1: putar stream MJPEG dari relay (tools/relay/relay_yt.py) lewat WiFi.
// Decoder sama dgn pemutar file: MjpegClass membaca dari Stream, di sini WiFiClient.
void mjpegPlayRelay(){
  if(!wifiConnected){ showToast(RPTR("WiFi belum tersambung")); return; }
  String hp = "";
  if(sdReady && SD_MMC.exists("/relay.txt")){
    File rf = SD_MMC.open("/relay.txt", FILE_READ);
    if(rf){ hp = rf.readStringUntil('\n'); hp.trim(); rf.close(); }
  }
  if(hp.length() == 0) hp = WiFi.gatewayIP().toString() + ":8080";
  String host = hp; uint16_t port = 8080;
  int ci = hp.indexOf(':');
  if(ci > 0){ host = hp.substring(0, ci); port = (uint16_t)hp.substring(ci + 1).toInt(); }
  if(!mjpegEnsureBuffer()){ showToast(RPTR("RAM tidak cukup utk MJPEG")); return; }

  WiFiClient rc;
  rc.setTimeout(4);
  if(!rc.connect(host.c_str(), port, 4000)){
    Serial.printf("[RELAY] gagal connect %s:%u\n", host.c_str(), (unsigned)port);
    showToast(RPTR("Relay tak terjangkau"));
    return;
  }
  rc.print(String("GET /live?w=") + (int)RP_DISP_NATIVE_W + "&h=" + (int)RP_DISP_NATIVE_H +   // ukuran panel portrait (v2: 320x480)
           " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
  String st = rc.readStringUntil('\n');
  if(st.indexOf(" 200") < 0){
    rc.stop();
    showToast(RPTR("Relay kosong: pilih video di browser"));
    return;
  }
  while(rc.connected() || rc.available()){          // lewati header HTTP
    String hl = rc.readStringUntil('\n');
    if(hl.length() <= 1) break;
  }
  if(!mjpegAcquire()){ rc.stop(); showToast(RPTR("RAM tidak cukup utk MJPEG")); return; }

  int prevRotation = display.getRotation();
  display.setRotation(0);                           // panel native 240x320 portrait (sama dgn pemutar file)
  display.fillScreen(TFT_BLACK);

  mjpegPlaying       = true;
  mjpegStopRequested = false;
  mjpegTotalFrames   = 0;
  mjpegStartMs       = millis();
  mjpegObj->setup(&rc, mjpegBuf, mjpegDrawCallback, true, 0, 0, display.width(), display.height());

  int frameCheckCounter = 0;
  bool goHomeAfter = false;
  while(!mjpegStopRequested && (rc.connected() || rc.available()) && mjpegObj->readMjpegBuf()){
    mjpegObj->drawJpg();
    mjpegTotalFrames++;
    if(++frameCheckCounter >= 2){
      frameCheckCounter = 0;
      lgfx::touch_point_t tp;
      if(getInputPoint(tp)){
        int g = mjpegClassifyTouch(tp);
        mjpegStopRequested = true;                  // sentuhan apa pun = berhenti
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
  display.setRotation(prevRotation);
  unsigned long used = millis() - mjpegStartMs;
  Serial.printf("[RELAY] selesai | frame=%lu | %lums | fps=%.1f\n", mjpegTotalFrames, used,
                used > 0 ? (1000.0f * mjpegTotalFrames / used) : 0.0f);
  if(goHomeAfter) navGoHome(); else needRedrawNow();
}

void mjpegEnter(){
'''
ph = sub(ph, "void mjpegEnter(){\n", FN, "mjpegEnter")
ph = sub(ph, 's.setTextColor(0xFBE0);s.setTextSize(1);s.setCursor(8,26);s.print("MJPEG / AVI Player");\n\n  iosRR(s,SCR_W-70,24,64,18,4,T().surface2);',
 's.setTextColor(0xFBE0);s.setTextSize(1);s.setCursor(8,26);s.print("Video");\n\n'
 '  iosRR(s,SCR_W-140,24,64,18,4,T().surface2);   // RELAY_STREAM_V1\n'
 '  s.setTextColor(T().accent);s.setCursor(SCR_W-130,29);s.print("Relay");\n'
 '  iosRR(s,SCR_W-70,24,64,18,4,T().surface2);', "header")
ph = sub(ph, "  if(x>=SCR_W-70 && x<=SCR_W-6 && y>=24 && y<=42){\n    mjpegScanFiles();",
 "  if(x>=SCR_W-140 && x<=SCR_W-76 && y>=24 && y<=42){   // RELAY_STREAM_V1\n    mjpegPlayRelay();\n    needRedraw = true;\n    return;\n  }\n\n  if(x>=SCR_W-70 && x<=SCR_W-6 && y>=24 && y<=42){\n    mjpegScanFiles();", "touch")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Added: live video from a relay (RELAY_STREAM_V1). The Video app has a new *Relay* button next to *Scan*. It connects over WiFi to `tools/relay/relay_yt.py` (run it in Termux, or on a PC), which turns a YouTube link or any video into an MJPEG stream (sized to the panel: 320x480 on Accretion v2, 240x320 on v1; about 10 to 12 fps, video only, no sound). The relay address is the WiFi gateway on port 8080 by default (relay on the phone that hosts the hotspot), or the first line of `/relay.txt` on the SD card (`192.168.x.x:8080`). Touch the screen to stop. The decoder is the same as the file player. Not yet compiled or tested on a device. Script: `tools/patches/apply_relay_stream.py`\n", "changelog")
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
