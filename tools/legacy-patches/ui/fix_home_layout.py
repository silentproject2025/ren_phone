#!/usr/bin/env python3
# Susunan kartu Home (PORTRAIT saja; landscape tidak berubah):
#  - kartu jam lebih tinggi (80 -> 88)
#  - baterai (kiri) + cuaca (kanan) digabung SATU BARIS, tinggi 76  -> hemat tinggi layar
#  - kartu cuaca punya tata letak ringkas utk lebar sempit
# Mengubah phone.ino & features_renphone.ino. Jalankan dari folder repo:  python3 fix_home_layout.py
import io, sys
def rd(p): return io.open(p,encoding="utf-8",errors="surrogateescape",newline="").read()
def wr(p,s): io.open(p,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)
P="phone.ino"; Fx="features_renphone.ino"
p=rd(P); f=rd(Fx)
if "v131: home layout" in p: print("sudah diterapkan"); sys.exit(0)
def rep(s,old,new,name):
    if s.count(old)!=1: print("pola tidak unik/ketemu (%d): %s"%(s.count(old),name)); sys.exit(1)
    return s.replace(old,new)

p=rep(p,"HomeRect homeWClock(){ HomeRect r; r.x=10; r.y=STATUS_H+28; r.h=homeIsLand()?66:80;",
        "HomeRect homeWClock(){ /* v131: home layout */ HomeRect r; r.x=10; r.y=STATUS_H+28; r.h=homeIsLand()?66:88;","clock rect")
p=rep(p,"else { r.y=STATUS_H+28+80+8; r.h=58; }","else { r.y=STATUS_H+28+88+8; r.h=58; }","music rect")
p=rep(p,"else { r.x=10; r.y=STATUS_H+28+80+8+58+8; r.w=SCR_W-20; r.h=44; }",
        "else { r.x=10; r.y=STATUS_H+28+88+8+58+8; r.w=84; r.h=76; }","batt rect")
p=rep(p,"  r.x=10; r.w=SCR_W-20; r.y=max(b.y+b.h,m.y+m.h)+8; r.h=homeIsLand()?40:48; return r;",
"""  if(homeIsLand()){ r.x=10; r.w=SCR_W-20; r.y=max(b.y+b.h,m.y+m.h)+8; r.h=40; }
  else { r.x=b.x+b.w+8; r.w=SCR_W-10-r.x; r.y=b.y; r.h=b.h; }   // v131: portrait -- di KANAN baterai, satu baris
  return r;""","weather rect")

f=rep(f,'    const char* msg = wxFetching ? "Memuat cuaca..." : (!wifiConnected ? "Cuaca: WiFi belum terhubung" : (wxFailed ? "Gagal memuat cuaca - ketuk untuk coba lagi" : "Ketuk untuk memuat cuaca"));\n',
'''    const bool nar = (w < 150);                                  // v131: kartu sempit -> pesan singkat
    const char* msg = wxFetching ? "Memuat cuaca..." : (!wifiConnected ? (nar ? "Tanpa WiFi" : "Cuaca: WiFi belum terhubung") : (wxFailed ? (nar ? "Gagal, ketuk ulang" : "Gagal memuat cuaca - ketuk untuk coba lagi") : (nar ? "Ketuk utk muat" : "Ketuk untuk memuat cuaca")));
''',"weather msg")
f=rep(f,"  int r = (h >= 46) ? 13 : 11;\n  wxIcon(s, x + 14 + r, cy, r, wxCode, wxIsDay != 0);\n",
'''  if (w < 150) {                                                 // v131: tata letak ringkas (kartu di samping baterai)
    const int rn = 12; const int icy = y + 10 + rn;
    wxIcon(s, x + 12 + rn, icy, rn, wxCode, wxIsDay != 0);
    char tn[8]; snprintf(tn, sizeof(tn), "%d", (int)lroundf(wxTemp));
    int txn = x + 12 + rn * 2 + 8;
    s.setTextSize(2); s.setTextColor(T().text);
    s.setCursor(txn, icy - 8); s.print(tn);
    s.setCursor(txn + 1, icy - 8); s.print(tn);
    int twn = s.textWidth(tn);
    s.setTextSize(1);
    s.drawCircle(txn + twn + 4, icy - 6, 2, T().text);
    s.setCursor(txn + twn + 9, icy - 8); s.print("C");
    uiText(s, x + 12, y + h - 30, wxDesc(wxCode), T().text, false, true);
    char dn[28]; snprintf(dn, sizeof(dn), "%d/%d C  Hujan %d%%", (int)lroundf(wxMax), (int)lroundf(wxMin), wxRain);
    s.setTextColor(T().subtext);
    s.setCursor(x + 12, y + h - 15); s.print(dn);
    return;
  }
  int r = (h >= 46) ? 13 : 11;
  wxIcon(s, x + 14 + r, cy, r, wxCode, wxIsDay != 0);
''',"weather narrow")
wr(P,p); wr(Fx,f)
print("OK: susunan kartu Home portrait diubah (jam 88, baterai+cuaca satu baris)")
