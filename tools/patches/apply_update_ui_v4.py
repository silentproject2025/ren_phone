#!/usr/bin/env python3
"""apply_update_ui_v4.py -- UI/UX baru app Update (UPD_UI_V4). Jalankan dari root repo:
     python3 apply_update_ui_v4.py [root]
Idempotent: kalau sudah terpasang, tidak mengubah apa pun."""
import os, re, sys, subprocess
BLOCK = r'''// =====================================================================
// UI v4 app Update  (UPD_UI_V4)
//   1. SATU tombol aksi utama di dasar layar yg berubah sesuai status:
//        belum dicek -> [Cek update]   ada update -> [Unduh & Pasang]   WiFi mati -> abu2
//   2. Tab [WiFi] [SD Card] -- tiap sumber punya layar sendiri, tidak berdesakan lagi.
//   3. Semua target sentuh >= 24 px (dulu 15 px).
//   4. Opsi jarang dipakai & berisiko (layar lain, rollback, URL manual) pindah ke sheet "Lanjutan".
//   5. Kartu status menjelaskan LANGKAH BERIKUTNYA + ukuran file + status tanda tangan.
// Semua koordinat relatif terhadap SCR_W/SCR_H/backY() -> ikut layar 320x240 maupun 480x320.
// =====================================================================
#define U4_X        6
#define U4_W        (SCR_W-12)
#define U4_HERO_H   42
#define U4_TAB_Y    (UPD_HERO_Y+U4_HERO_H+6)
#define U4_TAB_H    24
#define U4_BODY_Y   (U4_TAB_Y+U4_TAB_H+6)
#define U4_BOT      (backY()-4)
#define U4_CTA_H    30
#define U4_CTA_Y    (U4_BOT-U4_CTA_H)
#define U4_CH_H     26
#define U4_ADD_W    30
#define U4_MORE_W   30
#define U4_ROW_H    24
#define U4_ROW_GAP  3
#define U4_PAGER_H  22
#define U4_NOTE_W   70
#define U4_ADV_ROW0 28      // y baris pertama di sheet Lanjutan (relatif UPD_NT_Y)
#define U4_ADV_ROWH 32
#define U4_ADV_PILL 64

static int updSdPer(){
  int per = (U4_BOT - U4_PAGER_H - 4 - U4_BODY_Y + U4_ROW_GAP) / (U4_ROW_H + U4_ROW_GAP);
  return per < 1 ? 1 : per;
}
// mode PASANG = ada update dari "Cek update", atau URL manual terisi
static bool updInstallMode(){
  return (otaState()==2 && updUrl.length()>=8) || updUrlManual;
}
static void updPill(LGFX_Sprite& s, int x, int y, int w, int h, uint16_t bg, uint16_t fg, const char* t){
  iosRR(s, x, y, w, h, 8, bg);
  s.setTextColor(fg);
  updCenterText(s, x, w, y+(h-8)/2, t);
}
static String updClip(const String& in, int maxCh){
  if(maxCh < 3) maxCh = 3;
  if((int)in.length() <= maxCh) return in;
  return in.substring(0, maxCh-2) + "..";
}

// ---- kartu atas: status + versi terpasang ----
static void updDrawHero(LGFX_Sprite& s){
  int st = otaState();
  uint16_t stCol = (st==2) ? (uint16_t)IOS_ORANGE : (st==1 ? (uint16_t)IOS_GREEN : T().accent);
  drawGlassCard(s, U4_X, UPD_HERO_Y, U4_W, U4_HERO_H, 10, T().surface, 200);
  updIconArrow(s, 26, UPD_HERO_Y+U4_HERO_H/2, stCol);
  bool notes = otaNotesAvailable();
  int chipX = SCR_W-14-U4_NOTE_W;
  int textMaxX = notes ? chipX-6 : SCR_W-14;

  char line[40];
  if(st==2)      snprintf(line, sizeof(line), RPTR("Ada update: build %d"), otaLatestBuild());
  else if(st==1) snprintf(line, sizeof(line), "%s", RPTR("Sudah versi terbaru"));
  else           snprintf(line, sizeof(line), "%s", RPTR("Firmware terpasang"));
  String heroL = String(line);
  String hb = String(" [") + otaBranchName() + "]";            // branch yg sedang jalan (kalau muat)
  if((int)(heroL.length()+hb.length()) <= (textMaxX-46)/6) heroL += hb;
  s.setTextSize(1);
  s.setTextColor(stCol); s.setCursor(46, UPD_HERO_Y+8); s.print(heroL.c_str());

  int bn = otaBuildNum();
  char bl[28];
  if(bn > 0){ char vl[16]; otaVersionLabel(vl, sizeof(vl)); snprintf(bl, sizeof(bl), "%s b%d", vl, bn); }
  else snprintf(bl, sizeof(bl), "Lokal");
  s.setTextSize(2); s.setTextColor(T().text); s.setCursor(46, UPD_HERO_Y+21); s.print(bl);
  s.setTextSize(1);

  if(notes) updPill(s, chipX, UPD_HERO_Y+(U4_HERO_H-22)/2, U4_NOTE_W, 22, T().surface2, T().accent, RPTR("What's new"));
}

// ---- tab [WiFi] [SD Card] ----
static void updDrawTabs(LGFX_Sprite& s){
  int hw = (U4_W-4)/2;
  const char* names[2] = { "WiFi", "SD Card" };
  for(int i=0;i<2;i++){
    bool on = (updTab==i);
    updPill(s, U4_X+i*(hw+4), U4_TAB_Y, hw, U4_TAB_H, on ? T().accent : T().surface2, on ? T().bg : T().subtext, names[i]);
  }
}

// ---- tab WiFi: baris channel + kartu status + tombol aksi utama ----
static void updDrawWifiTab(LGFX_Sprite& s){
  int y = U4_BODY_Y;
  int chanW = U4_W - U4_ADD_W - U4_MORE_W - 8;
  // pill channel (ketuk = ganti branch)
  iosRR(s, U4_X, y, chanW, U4_CH_H, 8, T().surface2);
  s.setTextColor(T().subtext); s.setCursor(U4_X+10, y+9); s.print("Channel");
  s.setTextColor(T().accent);
  s.setCursor(U4_X+62, y+9); s.print(updClip(otaChannelName(), (chanW-62-20)/6).c_str());
  s.setTextColor(T().subtext); s.setCursor(U4_X+chanW-14, y+9); s.print(">");
  // "+" tambah branch
  int addX = U4_X+chanW+4;
  updPill(s, addX, y, U4_ADD_W, U4_CH_H, T().surface2, T().accent, "+");
  // "..." Lanjutan (titik merah = opsi layar lain sedang aktif)
  int moreX = addX+U4_ADD_W+4;
  iosRR(s, moreX, y, U4_MORE_W, U4_CH_H, 8, T().surface2);
  for(int i=0;i<3;i++) s.fillCircle(moreX+U4_MORE_W/2-7+i*7, y+U4_CH_H/2, 2, T().text);
  if(otaCrossDispOn()) s.fillCircle(moreX+U4_MORE_W-6, y+5, 3, T().danger);

  // kartu status: apa yg terjadi & apa langkah berikutnya
  int iy = y + U4_CH_H + 6;
  int ih = U4_CTA_Y - 6 - iy;
  if(ih > 52) ih = 52;                  // layar besar (480x320): kartu tidak perlu melar
  if(ih >= 30){
    iosRR(s, U4_X, iy, U4_W, ih, 10, T().surface);
    int st = otaState();
    char t1[40]; char t2[64];
    uint16_t c1 = T().subtext;
    if(!wifiConnected){
      snprintf(t1, sizeof(t1), "%s", RPTR("WiFi tidak terhubung")); c1 = T().danger;
      snprintf(t2, sizeof(t2), "%s", RPTR("Sambungkan WiFi dulu di Setting"));
    } else if(st==2 && updUrl.length()>=8){
      snprintf(t1, sizeof(t1), "%s", RPTR("Siap diunduh")); c1 = IOS_ORANGE;
      char sz[16]; int es = otaExpectSize();
      if(es > 0) updFmtSize(sz, sizeof(sz), (size_t)es); else snprintf(sz, sizeof(sz), "?");
      snprintf(t2, sizeof(t2), "%s%s%s", sz, otaSigVerified() ? " - " : "", otaSigVerified() ? RPTR("Tanda tangan valid") : "");
    } else if(updUrlManual){
      snprintf(t1, sizeof(t1), "%s", RPTR("URL manual")); c1 = T().accent;
      snprintf(t2, sizeof(t2), "%s", updClip(updUrl, (U4_W-24)/6).c_str());
    } else if(st==1){
      snprintf(t1, sizeof(t1), "%s", RPTR("Sudah versi terbaru")); c1 = IOS_GREEN;
      snprintf(t2, sizeof(t2), "Channel: %s", otaChannelName().c_str());
    } else {
      snprintf(t1, sizeof(t1), "%s", RPTR("Belum dicek"));
      snprintf(t2, sizeof(t2), "%s", RPTR("Ketuk Cek update untuk cari versi baru"));
    }
    int ty = iy + (ih-22)/2;
    s.setTextColor(c1); s.setCursor(U4_X+12, ty); s.print(t1);
    s.setTextColor(T().subtext); s.setCursor(U4_X+12, ty+14); s.print(updClip(String(t2), (U4_W-24)/6).c_str());
  }

  // tombol aksi utama
  bool inst = updInstallMode();
  uint16_t bg, fg; const char* label;
  if(!wifiConnected){ bg = T().surface2; fg = T().subtext; label = inst ? RPTR("Unduh & Pasang") : RPTR("Cek update"); }
  else if(inst){
    bool pend = (updPendingConfirm == (String("wifi:")+updUrl)) && millis()<=updPendingUntil;
    bg = pend ? T().danger : T().accent; fg = T().bg;
    label = pend ? RPTR("Yakin? Ketuk lagi") : RPTR("Unduh & Pasang");
  } else {
    bool fresh = (otaState()==0);                 // sudah terbaru -> tombol dibuat sekunder
    bg = fresh ? T().accent : T().surface2; fg = fresh ? T().bg : T().accent;
    label = RPTR("Cek update");
  }
  iosRR(s, U4_X, U4_CTA_Y, U4_W, U4_CTA_H, 10, bg);
  s.setTextColor(fg);
  updCenterText(s, U4_X, U4_W, U4_CTA_Y+(U4_CTA_H-8)/2, label);
}

// ---- tab SD Card: daftar .bin + pager ----
#define U4_SCAN_W 48
#define U4_NAV_W  28
#define U4_SCAN_X (SCR_W-6-U4_SCAN_W)
#define U4_NEXT_X (U4_SCAN_X-4-U4_NAV_W)
#define U4_PREV_X (U4_NEXT_X-4-U4_NAV_W)
static void updDrawSdTab(LGFX_Sprite& s){
  int per = updSdPer();
  int pages = (updFileCount + per - 1) / per; if(pages < 1) pages = 1;
  if(updScrollPage >= pages) updScrollPage = pages-1;

  if(!sdReady){
    int h = 2*11 + 16;
    iosRR(s, U4_X, U4_BODY_Y, U4_W, h, 10, T().surface);
    s.setTextColor(T().danger); s.setCursor(U4_X+12, U4_BODY_Y+10); s.print(RPTR("SD Card tidak siap"));
  } else if(updFileCount == 0){
    int h = 2*11 + 16;
    iosRR(s, U4_X, U4_BODY_Y, U4_W, h, 10, T().surface);
    s.setTextColor(T().text);    s.setCursor(U4_X+12, U4_BODY_Y+10); s.print(RPTR("Belum ada .bin. Taruh di /update"));
    s.setTextColor(T().subtext); s.setCursor(U4_X+12, U4_BODY_Y+10+12); s.print(RPTR("lalu ketuk [Scan]."));
  } else {
    int startIdx = updScrollPage*per;
    int maxCh = (U4_W - 30 - 70 - 54) / 6; if(maxCh < 8) maxCh = 8;
    for(int i=0; i<per && (startIdx+i)<updFileCount; i++){
      int idx = startIdx+i;
      int ry = U4_BODY_Y + i*(U4_ROW_H+U4_ROW_GAP);
      iosRR(s, U4_X, ry, U4_W, U4_ROW_H, 8, T().surface);
      nyxAaRR(s, 13, ry+(U4_ROW_H-12)/2, 10, 12, 2, T().accent);      // ikon file
      s.fillRect(16, ry+(U4_ROW_H-12)/2+4, 4, 1, T().bg); s.fillRect(16, ry+(U4_ROW_H-12)/2+7, 4, 1, T().bg);
      s.setTextColor(T().text); s.setCursor(30, ry+8); s.print(updClip(updFileList[idx], maxCh).c_str());
      char sz[16]; updFmtSize(sz, sizeof(sz), updFileSizes[idx]);
      int bw = 58;
      s.setTextColor(T().subtext); s.setCursor(SCR_W-10-bw-8-s.textWidth(sz), ry+8); s.print(sz);
      bool pend = (updPendingConfirm == (String("sd:")+updFileList[idx])) && millis()<=updPendingUntil;
      updPill(s, SCR_W-10-bw, ry+2, bw, U4_ROW_H-4, pend ? T().danger : T().good, T().bg, pend ? RPTR("Yakin?") : RPTR("Pasang"));
    }
  }

  // pager: "/update  N file  1/2"            [<] [>] [Scan]
  int py = U4_BOT - U4_PAGER_H;
  char pg[40];
  if(sdReady && updFileCount > 0) snprintf(pg, sizeof(pg), "/update  %d file  %d/%d", updFileCount, updScrollPage+1, pages);
  else snprintf(pg, sizeof(pg), "/update");
  s.setTextColor(T().subtext); s.setCursor(U4_X+4, py+(U4_PAGER_H-8)/2); s.print(pg);
  if(sdReady && updFileCount > per){
    bool canPrev = updScrollPage > 0, canNext = (updScrollPage+1) < pages;
    updPill(s, U4_PREV_X, py, U4_NAV_W, U4_PAGER_H, T().surface2, canPrev ? T().text : T().divider, "<");
    updPill(s, U4_NEXT_X, py, U4_NAV_W, U4_PAGER_H, T().surface2, canNext ? T().text : T().divider, ">");
  }
  updPill(s, U4_SCAN_X, py, U4_SCAN_W, U4_PAGER_H, T().surface2, T().accent, RPTR("Scan"));
}

// ---- sheet "Lanjutan": layar lain, rollback, URL manual ----
static void updDrawAdv(LGFX_Sprite& s){
  iosRR(s, UPD_NT_X, UPD_NT_Y, UPD_NT_W, UPD_NT_H, 10, T().surface);
  s.setTextSize(1);
  s.setTextColor(T().accent); s.setCursor(UPD_NT_X+10, UPD_NT_Y+8); s.print(RPTR("Lanjutan"));
  s.fillRect(UPD_NT_X+10, UPD_NT_Y+20, UPD_NT_W-20, 1, T().divider);
  int px = UPD_NT_X+UPD_NT_W-10-U4_ADV_PILL;
  int maxH = (px - (UPD_NT_X+10) - 8) / 6;
  for(int i=0;i<3;i++){
    int ry = UPD_NT_Y + U4_ADV_ROW0 + i*U4_ADV_ROWH;
    const char* lab; String hint; String pill; uint16_t pbg = T().surface2, pfg = T().accent;
    if(i==0){
      bool xOn = otaCrossDispOn();
      bool xPend = (updPendingConfirm == "xdisp") && millis()<=updPendingUntil;
      char dl[8]; otaDispLabel(dl, sizeof(dl));
      lab = RPTR("Firmware layar"); hint = RPTR("Ketuk 2x untuk layar lain");
      pill = xPend ? String("?") : String(dl);
      if(xOn || xPend){ pbg = T().danger; pfg = T().bg; }
    } else if(i==1){
      bool rbOk = otaRollbackReady();
      bool rbPend = (updPendingConfirm == "rb") && millis()<=updPendingUntil;
      lab = RPTR("Kembali ke versi lama");
      hint = rbOk ? String(RPTR("Slot OTA sebelumnya")) : String(RPTR("Tidak tersedia"));
      pill = rbPend ? String(RPTR("Yakin?")) : String(RPTR("Kembalikan"));
      pfg = rbOk ? T().text : T().divider;
      if(rbPend){ pbg = T().danger; pfg = T().bg; }
    } else {
      lab = RPTR("URL manual");
      hint = updUrl.length() ? updUrl : String(RPTR("ketuk untuk isi URL..."));
      pill = updUrl.length() ? String(RPTR("Hapus")) : String(RPTR("Isi"));
    }
    s.setTextColor(T().text);    s.setCursor(UPD_NT_X+10, ry+5);  s.print(lab);
    s.setTextColor(T().subtext); s.setCursor(UPD_NT_X+10, ry+17); s.print(updClip(hint, maxH).c_str());
    updPill(s, px, ry+3, U4_ADV_PILL, 24, pbg, pfg, pill.c_str());
  }
  int btnY = UPD_NT_Y + UPD_NT_H - UPD_NT_BTN_H - 6;
  updPill(s, UPD_NT_X+10, btnY, UPD_NT_W-20, UPD_NT_BTN_H, T().surface2, T().text, RPTR("Tutup"));
}

void drawUpdate(LGFX_Sprite& s){
  otaAddBranchPoll();                     // simpan branch baru begitu keyboard tertutup
  if(updUrlEditing && !kbVisible){        // keyboard URL baru saja ditutup
    updUrlEditing = false;
    updUrlManual = (updUrl.length() >= 8);
  }
  iosBackdrop(s); drawStatusBar(s);
  s.setTextSize(1);
  if(updAdvOpen){
    updDrawAdv(s);                        // sheet Lanjutan menutup seluruh isi (tidak ada yg tembus di belakangnya)
  } else {
    updDrawHero(s);
    updDrawTabs(s);
    if(updTab == 0) updDrawWifiTab(s); else updDrawSdTab(s);
  }
  if(otaNotesIsOpen()) updDrawNotes(s);
  if(kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

void updTouch(int x,int y,bool held,bool isNew){
  if(updBusy) return;
  otaAddBranchPoll();

  // layar changelog terbuka: hanya ia yang menerima sentuhan
  if(otaNotesIsOpen()){
    if(!isNew) return;
    if(isBack(x,y)){ otaNotesClose(); updNotesPage = 0; return; }       // Kembali = tutup changelog dulu
    int btnY = UPD_NT_Y + UPD_NT_H - UPD_NT_BTN_H - 6;
    if(y >= btnY-4 && y <= btnY+UPD_NT_BTN_H+4){
      bool multi = updNotesTotal > 1;
      int bw = multi ? (UPD_NT_W-30)/2 : UPD_NT_W-20;
      if(!multi || x < UPD_NT_X+10+bw+5){ otaNotesClose(); updNotesPage = 0; return; }
      updNotesPage = (updNotesPage+1 >= updNotesTotal) ? 0 : updNotesPage+1;
      needRedraw = true; return;
    }
    return;
  }

  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){ kbVisible=false; kbTarget=nullptr; }
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;

  // sheet Lanjutan
  if(updAdvOpen){
    if(isBack(x,y)){ updAdvOpen = false; needRedraw = true; return; }
    int btnY = UPD_NT_Y + UPD_NT_H - UPD_NT_BTN_H - 6;
    if(y >= btnY-4){ updAdvOpen = false; needRedraw = true; return; }       // Tutup
    int rel = y - (UPD_NT_Y + U4_ADV_ROW0);
    if(rel < 0) return;
    int i = rel / U4_ADV_ROWH;
    int px = UPD_NT_X+UPD_NT_W-10-U4_ADV_PILL;
    if(i == 0){
      // mematikan opsi langsung; menyalakan butuh ketuk 2x (risiko layar gelap)
      if(otaCrossDispOn()) otaCrossDispToggle();
      else if(updCheckConfirm("xdisp")) otaCrossDispToggle();
    } else if(i == 1){
      if(!otaRollbackReady()){ showToast(RPTR("Tidak ada versi sebelumnya di slot lain"), 2200); return; }
      if(updCheckConfirm("rb")) otaRollbackNow(true);
    } else if(i == 2){
      if(updUrl.length() && x >= px-2){ updUrl = ""; updUrlManual = false; needRedraw = true; }   // Hapus
      else { kbTarget=&updUrl; kbVisible=true; kbMode=KB_LOWER; updUrlEditing=true; needRedraw=true; }
    }
    return;
  }

  if(isBack(x,y)){ navBack(); return; }

  // kartu atas: ketuk = buka lagi changelog (kalau ada)
  if(y>=UPD_HERO_Y && y<=UPD_HERO_Y+U4_HERO_H){
    if(otaNotesAvailable()){ updNotesPage = 0; otaNotesOpen(); }
    return;
  }

  // tab
  if(y>=U4_TAB_Y-2 && y<=U4_TAB_Y+U4_TAB_H+2){
    int hw = (U4_W-4)/2;
    int t = (x < U4_X+hw+2) ? 0 : 1;
    if(t != updTab){
      updTab = t;
      if(updTab == 1) updScanFiles();       // buka tab SD = daftar langsung segar
      needRedraw = true;
    }
    return;
  }

  if(updTab == 0){
    // baris channel: [pill channel] [+] [...]
    if(y>=U4_BODY_Y-2 && y<=U4_BODY_Y+U4_CH_H+2){
      int chanW = U4_W - U4_ADD_W - U4_MORE_W - 8;
      int addX = U4_X+chanW+4, moreX = addX+U4_ADD_W+4;
      if(x >= moreX-2){ updAdvOpen = true; needRedraw = true; }
      else if(x >= addX-2){ otaAddBranchBegin(); }
      else otaChannelCycle();
      return;
    }
    // tombol aksi utama
    if(y>=U4_CTA_Y-2 && y<=U4_CTA_Y+U4_CTA_H){
      if(!wifiConnected){ showToast(RPTR("Sambungkan WiFi dulu di Setting")); return; }
      if(updInstallMode()){
        if(updUrl.length()<8){ showToast(RPTR("Isi URL firmware dulu")); return; }
        if(updCheckConfirm("wifi:"+updUrl)) updInstallFromURL(updUrl);
      } else {
        otaCheckAndFill();
      }
      return;
    }
    return;
  }

  // tab SD Card
  int per = updSdPer();
  int py = U4_BOT - U4_PAGER_H;
  if(y>=py-2 && y<=py+U4_PAGER_H+2){
    int pages = (updFileCount + per - 1) / per; if(pages < 1) pages = 1;
    if(x>=U4_SCAN_X-2){ updScanFiles(); showToast(RPTR("Scan selesai")); needRedraw=true; return; }
    if(sdReady && updFileCount > per){
      if(x>=U4_NEXT_X-2 && x<U4_NEXT_X+U4_NAV_W+2){ if(updScrollPage+1 < pages){ updScrollPage++; needRedraw=true; } return; }
      if(x>=U4_PREV_X-2 && x<U4_PREV_X+U4_NAV_W+2){ if(updScrollPage > 0){ updScrollPage--; needRedraw=true; } return; }
    }
    return;
  }
  if(sdReady && updFileCount>0){
    int startIdx = updScrollPage*per;
    for(int i=0; i<per && (startIdx+i)<updFileCount; i++){
      int ry = U4_BODY_Y + i*(U4_ROW_H+U4_ROW_GAP);
      if(y>=ry && y<=ry+U4_ROW_H && x>=SCR_W-10-58-6){
        const String& fn = updFileList[startIdx+i];
        if(updCheckConfirm("sd:"+fn)) updInstallFromSD(fn);
        return;
      }
    }
  }
}
'''
root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
here = os.path.dirname(os.path.abspath(__file__))
P  = os.path.join(root, 'firmware', 'phone', 'phone.ino')
O  = os.path.join(root, 'firmware', 'phone', 'ota_check_renphone.ino')
I  = os.path.join(root, 'tools', 'i18n_pairs.txt')
block = BLOCK

def rd(p): return open(p, encoding='utf-8').read()
def wr(p, t): open(p, 'w', encoding='utf-8', newline='\n').write(t)

src = rd(P)
if 'UPD_UI_V4' in src:
    print('phone.ino: sudah UI v4, dilewati')
else:
    # 1. state + reset di updEnter
    a = 'void updEnter(){\n'
    assert src.count(a) == 1
    state = ('// UPD_UI_V4 state\n'
             'static int  updTab = 0;            // 0 = WiFi, 1 = SD Card\n'
             'static bool updAdvOpen = false;    // sheet "Lanjutan" terbuka\n'
             'static bool updUrlManual = false;  // URL diisi tangan (bukan dari Cek update)\n'
             'static bool updUrlEditing = false; // keyboard URL sedang terbuka\n')
    src = src.replace(a, state + a + '  updTab = 0; updAdvOpen = false;\n')
    # 2. buang baris channel lama
    i = src.index('static void updDrawChanRow(LGFX_Sprite& s){')
    j = src.index('// ---- layar changelog (English)')
    src = src[:i] + src[j:]
    # 3. ganti drawUpdate + updTouch lama
    i = src.index('void drawUpdate(LGFX_Sprite& s){')
    j = src.index('// =============================================\n// PUSH FRAME')
    src = src[:i] + block + '\n' + src[j:]
    # 4. prototipe accessor baru
    m = 'bool otaRollbackReady(); void otaRollbackRefresh(); bool otaRollbackNow(bool);\n'
    assert src.count(m) == 1
    src = src.replace(m, m + 'int otaExpectSize(); bool otaSigVerified();   // UPD_UI_V4\n')
    wr(P, src); print('phone.ino: UI v4 terpasang')

o = rd(O)
if 'otaExpectSize' in o:
    print('ota_check: sudah, dilewati')
else:
    k = 'int otaState() {      // 0 belum dicek, 1 sudah terbaru, 2 ada update\n  return otaCheckedOk ? otaStateVal : 0;\n}\n'
    assert o.count(k) == 1
    o = o.replace(k, k + '// UPD_UI_V4: info utk kartu status app Update\n'
                  'int  otaExpectSize()  { return otaCheckedOk ? otaExpSize : 0; }      // byte, dari info.txt (0 = tidak tahu)\n'
                  'bool otaSigVerified() { return otaCheckedOk && otaSigOk; }           // info.txt lolos tanda tangan\n')
    wr(O, o); print('ota_check_renphone.ino: accessor ditambah')

pairs = '''# --- UPD_UI_V4: app Update ---
Belum dicek ::: Not checked yet
Ketuk Cek update untuk cari versi baru ::: Tap Check for updates to look for a new version
Tanda tangan valid ::: Signature verified
Lanjutan ::: Advanced
Firmware layar ::: Display firmware
Ketuk 2x untuk layar lain ::: Tap twice to target the other display
Kembali ke versi lama ::: Roll back to old version
Slot OTA sebelumnya ::: Previous OTA slot
Kembalikan ::: Roll back
URL manual ::: Manual URL
Isi ::: Enter
Siap diunduh ::: Ready to download
'''
t = rd(I)
if 'UPD_UI_V4' not in t:
    wr(I, t.rstrip('\n') + '\n' + pairs)
    subprocess.check_call([sys.executable, os.path.join(root, 'tools', 'gen_i18n.py')])
else:
    print('i18n: sudah, dilewati')

C = os.path.join(root, 'CHANGELOG.md')
c = rd(C)
if 'UPD_UI_V4' not in c:
    entry = ("- Improved: new look and easier use for the Update app (UPD_UI_V4). One main button at the bottom that follows the status: *Check for updates*, then *Download & Install* when a new build is found (WiFi off: greyed out). "
             "Tabs *WiFi* and *SD Card* replace the crowded single screen, and every touch target is now at least 24 px tall (it was 15 px). The status card says what to do next, and shows the file size and *Signature verified* once an update is found. "
             "The rarely used options (firmware for the other display, roll back, manual URL) moved into an *Advanced* sheet behind the *...* button; a red dot on the button means the other-display option is on. "
             "The SD list shows as many files per page as fit (3 on 320x240). Only drawing and touch code changed; install, checks and rollback are untouched. Checked with a PC stub build and a drawn preview only, not yet on a device. Script: `tools/patches/apply_update_ui_v4.py`\n")
    k = c.index('## Unreleased\n') + len('## Unreleased\n')
    wr(C, c[:k] + entry + c[k:])
    print('CHANGELOG.md: entri ditambah')
