#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_responsive.py -- sentuh FT6236 (ST7796) lebih konsisten: tidak "kadang respons kadang tidak".
Penyebab yg ditangani (semua di kelas LGFX, phone.ino):
 1) Ketukan cepat hilang: loop() cuma membaca sentuh 1x per putaran (putaran = render + push ~50 ms),
    jadi ketukan yg turun-naik di antara dua pembacaan tidak pernah terlihat. Sekarang ketukan seperti itu
    ditahan 1 pembacaan ("latch") + sentuh dibaca juga sebelum & sesudah pushSprite() di push().
 2) Gagal baca I2C sesaat (bus dipakai bareng MPU6050/INA219) dulu dianggap "jari diangkat" -> ketukan
    putus lalu muncul lagi sbg sentuhan baru. Sekarang gagal baca <=5x berturut-turut diabaikan.
 3) FT6236 kadang menukar id jari saat bergeser -> slot dilepas & dibuat ulang. Sekarang id di-rebind
    kalau cuma 1 jari aktif & 1 titik terbaca.
 4) Re-entrancy guard (touchPoll dipanggil dari push() & loop()) supaya Wire tidak dipakai bersamaan.
Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "TOUCH_RESP_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

# 1) touchSlot + touchPoll -> versi baru
a = s.find("  bool touchSlot(int s, lgfx::touch_point_t* tp){")
b = s.find("  // koordinat NATIVE -> koordinat layar pada rotasi aktif")
c = s.find("  void touchPoll(){")
d = s.find("private:\n  bool _touchOk=false;")
if min(a, b, c, d) < 0 or not (a < b < c < d) or s.count("  void touchPoll(){") != 1:
    sys.exit("ERROR: blok touchSlot/touchPoll tidak ketemu / berbeda dari yg diharapkan. Dibatalkan.")

new_slot = '''  // TOUCH_RESP_V1 (apply_touch_responsive.py)
  bool touchSlot(int s, lgfx::touch_point_t* tp){
    touchPoll();
    if(s<0||s>1) return false;
    if(_sDown[s]){ *tp=_sPt[s]; _sNew[s]=false; return true; }
    if(_sHold[s]){ *tp=_sPt[s]; _sHold[s]=false; _sNew[s]=false; return true; }   // ketukan kilat: tampil 1 pembacaan
    return false;
  }
'''
new_poll = '''  void touchPoll(){
    if(!_touchOk) return;
    uint32_t now=millis();
    if(now-_tPollMs < 4) return;               // jangan banjiri bus I2C: pakai hasil terakhir
    static portMUX_TYPE _tMux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&_tMux); bool busy=_tBusy; _tBusy=true; portEXIT_CRITICAL(&_tMux);
    if(busy) return;                           // sedang dibaca konteks lain
    _tPollMs=now;
    touchPollInner();
    _tBusy=false;
  }
  void touchPollInner(){
    uint8_t b[13];
    Wire.beginTransmission(RP_TOUCH_ADDR); Wire.write((uint8_t)0x02);
    bool ok = (Wire.endTransmission(false)==0) && (Wire.requestFrom((int)RP_TOUCH_ADDR,13)==13);
    if(!ok){
      while(Wire.available()) Wire.read();
      // gagal baca sesaat BUKAN "jari diangkat": pertahankan keadaan terakhir (maks 5x berturut-turut)
      if(_tFail<255) _tFail++;
      if(_tFail<=5) return;
    } else _tFail=0;
    int n=0; int rid[2]={0,1}, rx[2]={0,0}, ry[2]={0,0};
    if(ok){
      for(int i=0;i<13;i++) b[i]=Wire.read();
      n=b[0]&0x0F; if(n>2) n=0;
      for(int i=0;i<n;i++){
        const uint8_t* q=b+1+i*6;              // XH XL YH YL weight misc
        rx[i]=((q[0]&0x0F)<<8)|q[1];
        ry[i]=((q[2]&0x0F)<<8)|q[3];
        rid[i]=(q[2]>>4)&0x0F;
      }
      if(n==2 && rid[0]==rid[1]) rid[1]=rid[0]^1;
    }
#if !RP_MULTITOUCH
    if(n>1) n=1;
#endif
    // 1) slot yg sudah ada: cari titik dgn id yg sama
    bool used[2]={false,false};
    int act=(_sDown[0]?1:0)+(_sDown[1]?1:0);
    for(int s=0;s<2;s++){
      if(!_sDown[s]) continue;
      int found=-1;
      for(int i=0;i<n;i++) if(!used[i] && rid[i]==_sHw[s]){ found=i; break; }
      if(found<0 && act==1 && n==1 && !used[0]){ found=0; _sHw[s]=rid[0]; }   // FT6236 menukar id -> rebind, jangan putus
      if(found<0){                                // jari diangkat
        _sDown[s]=false;
        if(_sNew[s]) _sHold[s]=true;              // belum sempat dibaca loop() -> tahan 1 pembacaan
        continue;
      }
      used[found]=true;
      int x,y; touchMap(rx[found],ry[found],x,y);
      _sPt[s].x=x; _sPt[s].y=y; _sPt[s].size=64; _sPt[s].id=s;
    }
    // 2) titik baru -> slot kosong pertama
    for(int i=0;i<n;i++){
      if(used[i]) continue;
      int s=!_sDown[0]?0:(!_sDown[1]?1:-1);
      if(s<0) break;
      _sDown[s]=true; _sNew[s]=true; _sHw[s]=rid[i];
      int x,y; touchMap(rx[i],ry[i],x,y);
      _sPt[s].x=x; _sPt[s].y=y; _sPt[s].size=64; _sPt[s].id=s;
    }
  }
'''
s = s[:a] + new_slot + s[b:c] + new_poll + s[d:]

old_priv = "  bool _sDown[2]={false,false};\n  int  _sHw[2]={0,0};\n"
if s.count(old_priv) != 1:
    sys.exit("ERROR: anggota private touch tidak ketemu. Dibatalkan.")
s = s.replace(old_priv, old_priv + "  bool _sNew[2]={false,false};   // TOUCH_RESP_V1: turun tapi belum dibaca loop()\n  bool _sHold[2]={false,false};  // TOUCH_RESP_V1: sudah naik, tapi 1 pembacaan terakhir masih ditahan\n  uint8_t _tFail=0;\n  volatile bool _tBusy=false;\n")

# 2) push(): baca sentuh sebelum & sesudah pushSprite
old_push = "void push(){ if(nyxThFreezeFrame()) return; unsigned long t0=micros(); canvas.pushSprite(0,0); nyxPushLastUs=micros()-t0; rcFrameMark(); }"
if s.count(old_push) != 1:
    sys.exit("ERROR: fungsi push() tidak sama dgn yg diharapkan. Dibatalkan.")
new_push = ("void push(){ if(nyxThFreezeFrame()) return;\n"
            "#if RP_TOUCH_CAPACITIVE\n  display.touchPoll();   // TOUCH_RESP_V1: sampling lebih rapat di tengah putaran render\n#endif\n"
            "  unsigned long t0=micros(); canvas.pushSprite(0,0); nyxPushLastUs=micros()-t0;\n"
            "#if RP_TOUCH_CAPACITIVE\n  display.touchPoll();\n#endif\n"
            "  rcFrameMark(); }")
s = s.replace(old_push, new_push)

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: sentuh FT6236 diperbaiki (latch ketukan cepat, toleransi gagal I2C, rebind id, polling di push)")
