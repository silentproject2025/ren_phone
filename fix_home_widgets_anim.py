#!/usr/bin/env python3
# Animasi widget Home: (1) angka jam berganti halus, (2) cincin baterai terisi saat Home dibuka,
# (3) ikon cuaca bergerak pelan. Mengubah phone.ino DAN features_renphone.ino.
# Jalankan SETELAH fix_loading_widget.py, dari folder repo:  python3 fix_home_widgets_anim.py
import io, sys
def rd(p): return io.open(p,encoding="utf-8",errors="surrogateescape",newline="").read()
def wr(p,s): io.open(p,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)

P="phone.ino"; Fx="features_renphone.ino"
p=rd(P); f=rd(Fx)
if "v130: home anim" in p: print("sudah diterapkan"); sys.exit(0)
if "v129: ujung arc membulat" not in p: print("jalankan fix_loading_widget.py dulu"); sys.exit(1)

def rep(s,old,new,name):
    if s.count(old)!=1: print("pola tidak unik/ketemu (%d): %s"%(s.count(old),name)); sys.exit(1)
    return s.replace(old,new)

# ---------- phone.ino ----------
# 0) penanda "baru masuk Home" + hook di loop()
p=rep(p,"void drawClockCard(LGFX_Sprite& s,int x,int y,int w,int h){\n",
"unsigned long homeEnterMs=0; // v130: home anim -- diisi loop() saat layar berpindah KE Home\nvoid drawClockCard(LGFX_Sprite& s,int x,int y,int w,int h){\n","clock head")
p=rep(p,"void loop(){\n",
"void loop(){\n  { static Screen s_hePrev=SCR_HOME; Screen s_heC=curScreen();   // v130: home anim -- catat saat masuk Home\n    if(s_heC!=s_hePrev){ if(s_heC==SCR_HOME) homeEnterMs=millis(); s_hePrev=s_heC; } }\n","loop head")

# 1) angka jam: tiap digit yg berubah naik pelan + memudar masuk (350 ms)
p=rep(p,"  s.setTextColor(T().text);\n  s.setTextDatum(lgfx::textdatum_t::baseline_left);\n  s.drawString(tb,x+16,y+h-12);\n  s.setTextDatum(lgfx::textdatum_t::top_left);\n",
"""  s.setTextColor(T().text);
  s.setTextDatum(lgfx::textdatum_t::baseline_left);
  {                                                  // v130: home anim -- digit berubah naik+memudar masuk
    static char lastTb[6]=""; static unsigned long chgMs[5]={0,0,0,0,0};
    const unsigned long nowC=millis();
    if(ok){
      if(lastTb[0]){ for(int i=0;i<5;i++) if(tb[i]!=lastTb[i]) chgMs[i]=nowC; }
      memcpy(lastTb,tb,6);
    }
    bool animC=false;
    for(int i=0;i<5;i++){
      char pre[6]; memcpy(pre,tb,i); pre[i]=0;
      int px=x+16+(i? s.textWidth(pre):0);
      char ch[2]={tb[i],0};
      if(ok && chgMs[i] && nowC-chgMs[i]<350UL){
        float ik=1.0f-(float)(nowC-chgMs[i])/350.0f; float ee=1.0f-ik*ik*ik;   // ease-out cubic
        s.setTextColor(blend565(T().subtext,T().text,(uint8_t)(ee*255.0f)));
        s.drawString(ch,px,y+h-12+(int)lroundf((1.0f-ee)*8.0f));
        animC=true;
      } else { s.setTextColor(T().text); s.drawString(ch,px,y+h-12); }
    }
    s.setTextColor(T().text);
    if(animC) needRedraw=true;
  }
  s.setTextDatum(lgfx::textdatum_t::top_left);
""","clock digits")

# 2) cincin baterai terisi saat Home dibuka (700 ms)
p=rep(p,"    float a1=270.0f+battPercent*3.6f;\n    if(battPercent>0){\n",
"""    float shown=(float)battPercent;                  // v130: home anim -- cincin terisi saat Home dibuka
    { unsigned long dB=millis()-homeEnterMs;
      if(homeEnterMs && dB<700UL){ float ikB=1.0f-(float)dB/700.0f; shown=battPercent*(1.0f-ikB*ikB*ikB); needRedraw=true; } }
    float a1=270.0f+shown*3.6f;
    if(shown>0.5f){
""","batt arc")
p=rep(p,"      if(battPercent<100){                                   // v129: ujung arc membulat\n",
       "      if(shown<99.5f){                                       // v129: ujung arc membulat\n","batt caps")

# ---------- features_renphone.ino ----------
f=rep(f,"static void wxIcon(LGFX_Sprite& s, int cx, int cy, int r, int code, bool day) {\n",
"""#define WX_ANIM_MS 160UL   // v130: home anim -- jeda redraw ikon cuaca di Home (~6 fps). 0 = animasi MATI (hemat baterai)
static void wxIcon(LGFX_Sprite& s, int cx, int cy, int r, int code, bool day) {
  const unsigned long wms = millis();
  const int bob = (int)lroundf(sinf(wms * 0.0021f) * 1.0f);   // awan naik-turun 1 px
""","wxIcon head")
f=rep(f,"        float a = i * 0.785398f;\n","        float a = i * 0.785398f + wms * 0.0007f;   // sinar matahari berputar pelan\n","sun rays")
f=rep(f,"    if (code == 1) wxCloud(s, cx + r / 3, cy + r / 3, r * 2 / 3, CLOUD);\n",
        "    if (code == 1) wxCloud(s, cx + r / 3, cy + r / 3 + bob, r * 2 / 3, CLOUD);\n","cloud1")
f=rep(f,"  wxCloud(s, cx, cy - (rain || snow || storm ? r / 5 : 0), r, (rain || storm) ? DARK : CLOUD);\n",
        "  wxCloud(s, cx, cy - (rain || snow || storm ? r / 5 : 0) + bob, r, (rain || storm) ? DARK : CLOUD);\n","cloud2")
f=rep(f,"    for (int i = -1; i <= 1; i++) s.drawLine(cx + i * (r / 2), by, cx + i * (r / 2) - 2, by + r / 2, DROP);\n",
"""    const int hh = (r / 2 > 1) ? r / 2 : 1;                  // tetes hujan jatuh bergantian
    for (int i = -1; i <= 1; i++) {
      float ph = fmodf(wms * 0.0022f + (i + 1) * 0.37f, 1.0f); int dy = (int)(ph * hh); int x0 = cx + i * (r / 2);
      s.drawLine(x0 - (2 * dy) / hh, by + dy, x0 - (2 * (dy + hh / 2)) / hh, by + dy + hh / 2, DROP);
    }
""","rain")
f=rep(f,"    for (int i = -1; i <= 1; i++) s.fillCircle(cx + i * (r / 2), by + r / 4, 1, 0xFFFF);\n",
"""    for (int i = -1; i <= 1; i++)                              // salju turun sambil melayang
      s.fillCircle(cx + i * (r / 2) + (int)lroundf(sinf(wms * 0.003f + i * 1.7f) * 1.5f),
                   by + (int)(fmodf(wms * 0.0012f + (i + 1) * 0.31f, 1.0f) * (r / 2)), 1, 0xFFFF);
""","snow")
f=rep(f,"  } else if (storm) {\n    s.fillTriangle(cx + 1, by - 1, cx - 4, by + r / 2, cx, by + r / 2, SUN);\n    s.fillTriangle(cx, by + r / 2, cx + 5, by + 1, cx + 1, by + 1, SUN);\n  }\n",
"  } else if (storm && (wms / 300UL) % 5 != 4) {              // petir berkedip\n    s.fillTriangle(cx + 1, by - 1, cx - 4, by + r / 2, cx, by + r / 2, SUN);\n    s.fillTriangle(cx, by + r / 2, cx + 5, by + 1, cx + 1, by + 1, SUN);\n  }\n","storm")
f=rep(f,"void featuresLoopPoll() {\n",
"""void featuresLoopPoll() {
#if WX_ANIM_MS > 0
  { static unsigned long wxAnimMs = 0; unsigned long nA = millis();     // v130: home anim -- redraw terbatas utk ikon cuaca
    if (wxValid && !locked && !aodActive && curScreen() == SCR_HOME && nA - wxAnimMs >= WX_ANIM_MS) { wxAnimMs = nA; needRedraw = true; } }
#endif
""","featuresLoopPoll")
p=p.replace("unsigned long homeEnterMs=0; // v130: home anim","unsigned long homeEnterMs=0; // v130: home anim",1)
wr(P,p); wr(Fx,f)
print("OK: animasi jam, cincin baterai, dan ikon cuaca diterapkan")
