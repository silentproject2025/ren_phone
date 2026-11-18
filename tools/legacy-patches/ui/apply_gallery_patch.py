#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_gallery_patch.py -- pasang 2 fitur ke repo ren_phone:
  1. File Explorer SUPPORT FOLDER (masuk/naik folder, urut, drag-scroll,
     buka gambar di Galeri, hapus 2x ketuk, folder kosong bisa dihapus)
  2. App GALERI baru (gallery_renphone.ino): album per folder ala HP
     (Screenshot dst), grid thumbnail, viewer foto, putar video MJPEG,
     THUMBNAIL CACHE di SD (/.thumbs) supaya tidak decode gambar penuh terus.

Cara pakai (Termux, dari folder repo ren_phone):
    python3 apply_gallery_patch.py
Pastikan gallery_renphone.ino ada di folder yang sama dengan phone.ino.
AMAN dijalankan ulang (yang sudah dipatch dilewati). Kalau ada anchor yang
tidak ketemu, skrip BERHENTI tanpa menulis apa pun ke berkas itu.
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def read(p):
    with open(os.path.join(ROOT, p), "r", encoding="utf-8") as f:
        return f.read()


def write(p, s):
    with open(os.path.join(ROOT, p), "w", encoding="utf-8", newline="\n") as f:
        f.write(s)


def rep(text, old, new, label):
    n = text.count(old)
    if n != 1:
        sys.exit("GAGAL [%s]: anchor ketemu %d kali (harus tepat 1).\n--- anchor ---\n%s" % (label, n, old))
    return text.replace(old, new, 1)


EXPLORER_BLOCK = r'''// =============================================
// APP: FILE EXPLORER (DENGAN WEB UPLOADER)
// =============================================
// BARU: SUPPORT FOLDER. Dulu cuma menampilkan file di root SD (maks 20).
// Sekarang:
//  * ketuk folder = masuk; baris ".." = naik; tombol Kembali = naik satu
//    folder dulu (baru keluar app kalau sudah di root) -- gaya file manager HP.
//  * folder & file diurut (folder dulu), sampai 256 entri per folder, bisa
//    digeser (drag) selain tombol < Prev / Next >. Entri berawalan "." (mis.
//    /.thumbs = cache thumbnail Galeri) disembunyikan.
//  * file gambar (JPG/BMP) dibuka di Galeri, .mjpeg diputar di pemutar MJPEG,
//    file teks dibaca (maks 8 KB), file biner lain TIDAK dibuka sbg teks lagi
//    (dulu muncul karakter acak).
//  * Hapus = ketuk 2x. Folder hanya bisa dihapus kalau kosong.
// Ketuk baris diproses saat jari LEPAS (lihat fileExpPointerTick) supaya
// menggeser daftar tidak salah dianggap membuka folder/file.
// Variabel isi-file (expView*) & scroll isi file TETAP sama spt v12.
#define EXP_MAX_ENTRIES 256
#define EXP_NAME_LEN    64
#define EXP_TEXT_MAX    8192
struct ExpEntry { char name[EXP_NAME_LEN]; uint32_t size; bool dir; };
ExpEntry* expEntries = nullptr;      // dialokasikan lazy di PSRAM (~18 KB)
int  expEntryCount = 0;
bool expTruncated = false;
String expCwd = "/";
int  expScrollItem = 0;              // indeks baris pertama yg tampil (baris 0 = ".." kalau bukan di root)
bool expKeepCwdOnce = false;         // true sesaat sebelum pindah ke Galeri, supaya folder tidak reset pas kembali
int  expDelArmIdx = -1;
unsigned long expDelArmUntil = 0;
String expViewContent = "";
String expViewFileName = "";

// v12: BARU - isi file skrg bisa di-scroll (dulu teks panjang meluber
// keluar kotak & tak terbaca sama sekali). Dibungkus+di-wrap+di-clip persis
// spt pola scroll teks di AI Chat (reuse AiLine + aiWrapAppend), lengkap dgn
// momentum halus stlh jari dilepas.
#define EXP_MAX_LINES 400
AiLine expLinesBuf[EXP_MAX_LINES];
float  expViewScrollY = 0;
int    expViewMaxScroll = 0;
float  expViewScrollVel = 0;

bool expEnsureBuf(){
  if(expEntries) return true;
  expEntries = (ExpEntry*)heap_caps_calloc(EXP_MAX_ENTRIES, sizeof(ExpEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return expEntries != nullptr;
}

String expJoin(const String& dir, const char* name){
  if(dir == "/") return String("/") + name;
  return dir + "/" + name;
}

String expParentOf(const String& p){
  int i = p.lastIndexOf('/');
  if(i <= 0) return String("/");
  return p.substring(0, i);
}

// folder dulu, lalu nama (tanpa peduli huruf besar/kecil)
int expCmpKey(bool da, const char* na, bool db, const char* nb){
  if(da != db) return da ? -1 : 1;
  while(*na && *nb){
    char ca = *na, cb = *nb;
    if(ca >= 'A' && ca <= 'Z') ca += 32;
    if(cb >= 'A' && cb <= 'Z') cb += 32;
    if(ca != cb) return (unsigned char)ca - (unsigned char)cb;
    na++; nb++;
  }
  return (unsigned char)*na - (unsigned char)*nb;
}

void scanSdFiles() {
  expEntryCount = 0; expTruncated = false;
  if (!sdReady) return;
  if (!expEnsureBuf()) return;
  File dir = SD_MMC.open(expCwd.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    expCwd = "/";
    dir = SD_MMC.open("/");
    if (!dir) return;
  }
  File f = dir.openNextFile();
  while (f) {
    String nm = String(f.name());
    int sl = nm.lastIndexOf('/');
    if (sl >= 0) nm = nm.substring(sl + 1);
    bool isDir = f.isDirectory();
    uint32_t sz = isDir ? 0 : (uint32_t)f.size();
    f.close();
    if (nm.length() > 0 && nm.length() < EXP_NAME_LEN && nm[0] != '.' && nm != "System Volume Information") {
      if (expEntryCount >= EXP_MAX_ENTRIES) { expTruncated = true; break; }
      ExpEntry& e = expEntries[expEntryCount++];
      strncpy(e.name, nm.c_str(), EXP_NAME_LEN - 1); e.name[EXP_NAME_LEN - 1] = 0;
      e.size = sz; e.dir = isDir;
    }
    f = dir.openNextFile();
  }
  dir.close();
  for (int i = 1; i < expEntryCount; i++) {       // insertion sort
    ExpEntry tmp = expEntries[i];
    int j = i - 1;
    while (j >= 0 && expCmpKey(expEntries[j].dir, expEntries[j].name, tmp.dir, tmp.name) > 0) {
      expEntries[j + 1] = expEntries[j];
      j--;
    }
    expEntries[j + 1] = tmp;
  }
}

int expTotalRows(){ return expEntryCount + ((expCwd != "/") ? 1 : 0); }

int expItemsPerPage(){
  int listY = 44, itemH = 26, gap = 4;
  int pageY = backY() - 2;
  int n = (pageY - 4 - listY) / (itemH + gap);
  return n < 1 ? 1 : n;
}

void expClampScroll(){
  int maxStart = expTotalRows() - expItemsPerPage();
  if(maxStart < 0) maxStart = 0;
  if(expScrollItem > maxStart) expScrollItem = maxStart;
  if(expScrollItem < 0) expScrollItem = 0;
}

void expEnterDir(const String& path){
  expCwd = path;
  expScrollItem = 0;
  expDelArmIdx = -1;
  scanSdFiles();
  needRedraw = true;
}

void expGoUp(){
  if(expCwd == "/") return;
  String child = expCwd.substring(expCwd.lastIndexOf('/') + 1);
  expCwd = expParentOf(expCwd);
  expDelArmIdx = -1;
  scanSdFiles();
  expScrollItem = 0;
  for(int i = 0; i < expEntryCount; i++){   // posisikan daftar di folder yg barusan ditinggalkan
    if(expEntries[i].dir && child == expEntries[i].name){
      expScrollItem = (i + ((expCwd != "/") ? 1 : 0)) - expItemsPerPage() / 2;
      break;
    }
  }
  expClampScroll();
  needRedraw = true;
}

// Kembali: tutup isi file -> naik folder -> baru keluar app
void expGoBack(){
  if(expViewFileName.length() > 0){
    expViewFileName = ""; expViewContent = "";
    expViewScrollY = 0; expViewScrollVel = 0;
    needRedraw = true;
    return;
  }
  if(expCwd != "/"){ expGoUp(); return; }
  navBack();
}

bool expIsTextName(const String& lower){
  if(lower.indexOf('.') < 0) return true;      // tanpa ekstensi (README, LICENSE, dst)
  static const char* exts[] = { ".txt", ".json", ".md", ".log", ".csv", ".tsv", ".ini", ".cfg", ".conf",
                                ".h", ".c", ".cpp", ".ino", ".py", ".js", ".html", ".htm", ".css",
                                ".xml", ".yml", ".yaml", ".sh", ".bat", ".nfo" };
  for(unsigned i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) if(lower.endsWith(exts[i])) return true;
  return false;
}

void expOpenEntry(int ei){
  String path = expJoin(expCwd, expEntries[ei].name);
  String low = String(expEntries[ei].name); low.toLowerCase();

  if(low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp")){
    expKeepCwdOnce = true;                      // balik dari Galeri tetap di folder ini
    galOpenFile(path.c_str());
    return;
  }
  if(low.endsWith(".mjpeg")){
    mjpegPlayFile(path.substring(1));           // pemutar MJPEG pakai path relatif dari root
    needRedraw = true;
    return;
  }
  if(!expIsTextName(low)){
    showToast("Format ini tidak bisa dibuka di sini");
    vibWarn();
    return;
  }
  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if(!f){
    expViewFileName = path; expViewContent = "Gagal membaca isi file.";
  } else {
    size_t sz = (size_t)f.size();
    size_t n = sz > EXP_TEXT_MAX ? EXP_TEXT_MAX : sz;
    String content; content.reserve(n + 48);
    char buf[129];
    size_t left = n;
    while(left > 0){
      size_t want = left > 128 ? 128 : left;
      size_t r = f.read((uint8_t*)buf, want);
      if(r == 0) break;
      int m = 0;
      for(size_t k = 0; k < r; k++){
        unsigned char c = (unsigned char)buf[k];
        if(c == '\r') continue;
        if(c == '\t') c = ' ';
        else if(c < 32 && c != '\n') c = '.';
        buf[m++] = (char)c;
      }
      buf[m] = 0;
      content += buf;
      left -= r;
    }
    f.close();
    if(sz > n) content += String("\n\n... (dipotong, ukuran file ") + String((unsigned)(sz / 1024)) + " KB)";
    expViewFileName = path;
    expViewContent = content;
  }
  expViewScrollY = 0; expViewScrollVel = 0;
  needRedraw = true;
}

void expDeleteEntry(int vi, int ei){
  if(!(expDelArmIdx == vi && millis() < expDelArmUntil)){
    expDelArmIdx = vi; expDelArmUntil = millis() + 2500;
    showToast("Ketuk lagi untuk menghapus");
    needRedraw = true;
    return;
  }
  expDelArmIdx = -1;
  String path = expJoin(expCwd, expEntries[ei].name);
  bool wasDir = expEntries[ei].dir;
  bool ok;
  if(wasDir){
    ok = SD_MMC.rmdir(path.c_str());
    if(!ok){ showToast("Folder tidak kosong"); vibWarn(); needRedraw = true; return; }
  } else {
    galForgetFile(path.c_str());               // buang thumbnail cache-nya juga (sebelum file hilang)
    ok = SD_MMC.remove(path.c_str());
    if(!ok){ showToast("Gagal menghapus file"); vibError(); needRedraw = true; return; }
  }
  showToast(wasDir ? "Folder Dihapus" : "File Dihapus");
  scanSdFiles();
  expClampScroll();
  needRedraw = true;
}

// Ketuk baris daftar (dipanggil saat jari lepas tanpa menggeser)
void fileExpTap(int x, int y){
  int listY = 44, itemH = 26, gap = 4;
  int rel = y - listY;
  if(rel < 0) return;
  int row = rel / (itemH + gap);
  if(rel % (itemH + gap) > itemH) return;
  int per = expItemsPerPage();
  if(row >= per) return;
  int vi = expScrollItem + row;
  int upRow = (expCwd != "/") ? 1 : 0;
  if(vi < 0 || vi >= expEntryCount + upRow) return;
  if(upRow && vi == 0){ expGoUp(); return; }
  int ei = vi - upRow;
  if(x >= SCR_W - 44){                         // tombol Hps
    expDeleteEntry(vi, ei);
    return;
  }
  if(expEntries[ei].dir){
    expEnterDir(expJoin(expCwd, expEntries[ei].name));
    return;
  }
  expOpenEntry(ei);
}

bool expPtrDown = false, expPtrDrag = false;
int  expPtrSx = 0, expPtrSy = 0, expPtrLy = 0, expPtrAcc = 0;

// Dipanggil tiap loop() selagi layar = File Explorer (hook di loop(), pola sama
// dgn settingsPointerTick): geser daftar + ketuk-saat-lepas.
void fileExpPointerTick(bool touched, bool wasT, int tx, int ty){
  if(expViewFileName.length() > 0 || !sdReady){ expPtrDown = false; return; }
  int listY = 44, itemH = 26, gap = 4;
  int pageY = backY() - 2;
  if(touched){
    if(!wasT){
      expPtrDown = (ty >= listY && ty < pageY - 4);
      expPtrSx = tx; expPtrSy = ty; expPtrLy = ty; expPtrDrag = false; expPtrAcc = 0;
    } else if(expPtrDown){
      if(!expPtrDrag && abs(ty - expPtrSy) > 8) expPtrDrag = true;
      if(expPtrDrag){
        expPtrAcc += expPtrLy - ty; expPtrLy = ty;
        int step = itemH + gap, before = expScrollItem;
        while(expPtrAcc >= step){ expScrollItem++; expPtrAcc -= step; }
        while(expPtrAcc <= -step){ expScrollItem--; expPtrAcc += step; }
        expClampScroll();
        if(expScrollItem != before) needRedraw = true;
      }
    }
  } else {
    if(wasT && expPtrDown){
      expPtrDown = false;
      if(!expPtrDrag) fileExpTap(expPtrSx, expPtrSy);
    } else {
      expPtrDown = false;
    }
  }
}

void fileExpEnter(){
  expViewContent = "";
  expViewFileName = "";
  if(!expKeepCwdOnce){ expCwd = "/"; expScrollItem = 0; }
  expKeepCwdOnce = false;
  expViewScrollY = 0;    // v12
  expViewScrollVel = 0;  // v12
  expDelArmIdx = -1;
  expPtrDown = false;
  scanSdFiles();
  expClampScroll();
}
void fileExpExit(){}

void drawFileExplorer(LGFX_Sprite& s){
  iosBackdrop(s); drawStatusBar(s);
  s.setTextSize(1);
  bool atRoot = (expCwd == "/");

  if(atRoot){
    s.setTextColor(0x3ADF);s.setCursor(8,26);s.print("File Explorer");
    if(wifiConnected && webServerRunning){
      s.setTextColor(T().good);s.setCursor(110,26);
      String ipStr = "Web: " + WiFi.localIP().toString();
      s.print(ipStr.c_str());
    } else {
      s.setTextColor(T().subtext);s.setCursor(120,26);
      s.print("Web: Off (No WiFi)");
    }
  } else {
    String hdr = expCwd;                       // path penuh; kalau kepanjangan, potong dari kiri
    int maxC = (SCR_W - 16) / 6;
    if((int)hdr.length() > maxC) hdr = String("..") + hdr.substring(hdr.length() - (maxC - 2));
    s.setTextColor(0x3ADF);s.setCursor(8,26);s.print(hdr.c_str());
  }

  if(expViewFileName.length() > 0){
    iosRR(s,SCR_W-64,24,58,18,4,T().surface2);
    s.setTextColor(T().accent);s.setCursor(SCR_W-54,29);s.print("Tutup");

    int viewTop=44, viewBot=backY()-6, viewH=viewBot-viewTop;
    iosRR(s,4,viewTop,SCR_W-8,viewH,6,T().surface);
    s.setTextColor(T().accent2);s.setCursor(10,viewTop+6);s.print("File: ");
    String vn = expViewFileName;
    int maxV = (SCR_W - 60) / 6;
    if((int)vn.length() > maxV) vn = String("..") + vn.substring(vn.length() - (maxV - 2));
    s.setTextColor(T().text);s.print(vn.c_str());
    s.drawFastHLine(10,viewTop+20,SCR_W-20,T().divider);

    // v12: isi file di-wrap ke expLinesBuf (persis pola AI Chat) supaya
    // tingginya pasti & bisa discroll+diclip -> teks panjang skrg bisa
    // dibaca semua dgn geser (drag), bukan meluber keluar kotak lagi.
    int contentTop=viewTop+26, contentBot=viewBot-6;
    int lineH=10;
    int innerX=10, innerW=(SCR_W-8)-12;
    int maxChars=innerW/6; if(maxChars<6) maxChars=6;
    int n = aiWrapAppend(expLinesBuf,EXP_MAX_LINES,0,expViewContent,T().text,maxChars);

    int contentH = n*lineH;
    int viewAreaH = contentBot-contentTop;
    expViewMaxScroll = contentH-viewAreaH; if(expViewMaxScroll<0) expViewMaxScroll=0;
    if(expViewScrollY>expViewMaxScroll) expViewScrollY=(float)expViewMaxScroll;
    if(expViewScrollY<0) expViewScrollY=0;

    s.setClipRect(4,contentTop,SCR_W-8,viewAreaH);
    s.setTextWrap(false);
    for(int i=0;i<n;i++){
      int ly = contentTop+i*lineH-(int)expViewScrollY;
      if(ly+lineH < contentTop || ly > contentBot) continue;
      s.setTextColor(expLinesBuf[i].color);
      s.setCursor(innerX,ly);
      s.print(expLinesBuf[i].text.c_str());
    }
    s.clearClipRect();

    // Scrollbar tipis di kanan, sama gaya spt di AI Chat
    if(expViewMaxScroll>0){
      int trackX=SCR_W-8, trackY=contentTop, trackH=viewAreaH;
      int thumbH = max(14, (int)((float)viewAreaH/contentH*trackH));
      int thumbY = trackY + (int)((float)expViewScrollY/expViewMaxScroll*(trackH-thumbH));
      s.fillRoundRect(trackX,trackY,3,trackH,1,T().divider);
      s.fillRoundRect(trackX,thumbY,3,thumbH,1,T().accent);
    }

    drawBack(s);drawToast(s);
    return;
  }

  if(!sdReady){
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);
    s.print("SD Card Tdk Siap");
    drawBack(s);drawToast(s);
    return;
  }

  expClampScroll();
  int listY = 44, itemH = 26, gap = 4;
  int per = expItemsPerPage();
  int upRow = atRoot ? 0 : 1;
  int total = expEntryCount + upRow;

  if(total == 0){
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(20,80);
    s.print("SD Card Kosong / Belum ada file.");
  }
  for(int r = 0; r < per; r++){
    int vi = expScrollItem + r;
    if(vi >= total) break;
    int itemY = listY + r * (itemH + gap);
    iosRR(s,4,itemY,SCR_W-8,itemH,6,T().surface);

    if(upRow && vi == 0){                      // baris ".." (naik satu folder)
      s.setTextColor(T().accent2);s.setCursor(12,itemY+8);s.print("..");
      s.setTextColor(T().subtext);s.setCursor(34,itemY+8);s.print("Naik satu folder");
      continue;
    }
    int ei = vi - upRow;
    bool isDir = expEntries[ei].dir;
    bool armed = (expDelArmIdx == vi && millis() < expDelArmUntil);

    if(isDir){                                 // ikon folder
      uint16_t fc = T().accent;
      s.fillRoundRect(10,itemY+7,9,4,1,fc);
      s.fillRoundRect(10,itemY+9,17,12,2,fc);
    } else {                                   // ikon file
      uint16_t fc = T().accent2;
      s.drawRoundRect(12,itemY+6,12,15,2,fc);
      s.drawFastHLine(15,itemY+11,6,fc);
      s.drawFastHLine(15,itemY+14,6,fc);
      s.drawFastHLine(15,itemY+17,6,fc);
    }

    int nameX = 34;
    int availPx = (SCR_W - 8) - nameX - (isDir ? 62 : 92);
    int maxChars = availPx / 6; if(maxChars < 4) maxChars = 4;
    String fn = String(expEntries[ei].name);
    if((int)fn.length() > maxChars) fn = fn.substring(0, maxChars - 2) + "..";
    s.setTextColor(T().text);s.setCursor(nameX,itemY+8);s.print(fn.c_str());

    if(isDir){                                 // chevron ">"
      int cx = SCR_W - 58, cy = itemY + itemH / 2;
      s.drawLine(cx-3,cy-5,cx+2,cy,T().subtext); s.drawLine(cx+2,cy,cx-3,cy+5,T().subtext);
    } else {
      iosRR(s,SCR_W-84,itemY+3,36,20,4,T().surface2);
      s.setTextColor(T().good);s.setCursor(SCR_W-76,itemY+8);s.print("Buka");
    }
    iosRR(s,SCR_W-44,itemY+3,36,20,4,armed ? T().danger : T().surface2);
    s.setTextColor(armed ? 0xFFFF : T().danger);
    s.setCursor(armed ? SCR_W-41 : SCR_W-38, itemY+8);
    s.print(armed ? "Yakin?" : "Hps");
  }

  if(upRow && expEntryCount == 0 && per >= 2){
    s.setTextColor(T().subtext);s.setCursor(12,listY + (itemH+gap) + 10);s.print("Folder kosong");
  }

  int pageY = backY() - 2;
  if(expScrollItem > 0){
    iosRR(s,SCR_W-120,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-110,pageY+6);s.print("< Prev");
  }
  if(expScrollItem + per < total){
    iosRR(s,SCR_W-60,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-52,pageY+6);s.print("Next >");
  }
  if(total > per){                             // indikator posisi "baris terakhir tampil / total"
    int last = expScrollItem + per; if(last > total) last = total;
    char pg[20];
    snprintf(pg,sizeof(pg),"%d/%d%s",last,total,expTruncated?"+":"");
    int availW = (SCR_W - 124) - (backX() + BACK_W + 8);
    if(s.textWidth(pg) > availW) snprintf(pg,sizeof(pg),"%d",total);
    if(s.textWidth(pg) <= availW){
      s.setTextColor(T().subtext);s.setCursor(backX()+BACK_W+8,pageY+7);s.print(pg);
    }
  }

  drawBack(s);drawToast(s);
}

void fileExpTouch(int x,int y,bool held,bool isNew){
  static int dragLastY=0;
  int viewTop=44, viewBot=backY()-6; // v12: sama dgn area kotak isi file di drawFileExplorer

  if(isNew){
    if(isBack(x,y)){ expGoBack(); return; }

    if(expViewFileName.length() > 0){
      if(x>=SCR_W-64 && x<=SCR_W-4 && y>=24 && y<=42){
        expViewFileName = "";
        expViewContent = "";
        expViewScrollY = 0; expViewScrollVel = 0; // v12
        needRedraw = true;
        return;
      }
      // v12: sentuh area isi file -> mulai drag utk scroll (spt AI Chat)
      if(y>=viewTop && y<=viewBot){
        dragLastY = y;
        expViewScrollVel = 0; // reset sisa momentum di awal sentuhan baru
      }
      return;
    }

    if(!sdReady) return;

    // Prev/Next (tombol langsung). Ketuk baris & geser daftar: lihat fileExpPointerTick().
    int per = expItemsPerPage(), total = expTotalRows();
    int pageY = backY() - 2;
    if(expScrollItem > 0 && x>=SCR_W-120 && x<=SCR_W-66 && y>=pageY){
      expScrollItem -= per; expClampScroll(); needRedraw = true; return;
    }
    if(expScrollItem + per < total && x>=SCR_W-60 && x<=SCR_W-6 && y>=pageY){
      expScrollItem += per; expClampScroll(); needRedraw = true; return;
    }
    return;
  }

  // v12: Frame lanjutan selagi jari ditahan -> geser scroll isi file
  // (dulu file explorer tidak menangani frame held sama sekali).
  if(held && expViewFileName.length() > 0 && y>=viewTop-20 && y<=viewBot+20){
    int dy = dragLastY - y;
    if(dy!=0){
      expViewScrollVel = expViewScrollVel*0.5f + dy*0.5f; // low-pass, sama gaya dgn Home/AI Chat
      expViewScrollY = constrain(expViewScrollY + dy, 0.0f, (float)expViewMaxScroll);
      dragLastY = y;
      needRedraw = true;
    }
  }
}

'''

EXP_START = "// =============================================\n// APP: FILE EXPLORER (DENGAN WEB UPLOADER)\n"
EXP_END = "// =============================================\n// APP: MJPEG PLAYER (FIXED v7)\n"


def patch_phone():
    p = "phone.ino"
    s = read(p)
    if "SCR_GALLERY" in s:
        print("phone.ino: sudah dipatch, dilewati")
        return
    if not os.path.exists(os.path.join(ROOT, "gallery_renphone.ino")):
        sys.exit("gallery_renphone.ino tidak ada di folder ini.")

    # ---- enum Screen ----
    s = rep(s, "              SCR_STOPWATCH, SCR_MEMO };",
            "              SCR_STOPWATCH, SCR_MEMO,\n"
            "              // Galeri foto/video + thumbnail cache -- lihat gallery_renphone.ino\n"
            "              SCR_GALLERY };", "enum Screen")

    # ---- forward decl + apps[] ----
    s = rep(s, "\nAppDef apps[34] = {",
            "\n// ---- gallery_renphone.ino: Galeri + thumbnail cache; hook File Explorer berfolder ----\n"
            "void galEnter(); void galExit(); void drawGallery(LGFX_Sprite&); void galTouch(int,int,bool,bool);\n"
            "void galPointerTick(bool,bool,int,int); void galOpenFile(const char*); void galForgetFile(const char*);\n"
            "void fileExpPointerTick(bool,bool,int,int);\n"
            "void mjpegPlayFile(const String& filename);\n"
            "\nAppDef apps[35] = {", "apps decl")
    s = rep(s,
            "  { \"Voice Memo\", 'v', 0, memoEnter,    memoExit,     drawMemo,         memoTouch,     SCR_MEMO },\n};\n#define APP_COUNT 34",
            "  { \"Voice Memo\", 'v', 0, memoEnter,    memoExit,     drawMemo,         memoTouch,     SCR_MEMO },\n"
            "  // ---- Galeri: album per folder, thumbnail cache di SD ----\n"
            "  { \"Galeri\",     'g', 0, galEnter,     galExit,      drawGallery,      galTouch,      SCR_GALLERY },\n"
            "};\n#define APP_COUNT 35", "apps entry")
    s = rep(s, "  apps[33].color=0xF9A6; // Voice Memo - merah rekam\n}",
            "  apps[33].color=0xF9A6; // Voice Memo - merah rekam\n"
            "  apps[34].color=0x565F; // Galeri - biru langit\n}", "app color")

    # ---- ikon app 'g' ----
    s = rep(s,
            "    default: {\n      s.setTextColor(ic); s.setTextSize(2);\n      char b[2]={sym,0};",
            "    case 'g': { // Galeri: bingkai foto + matahari + dua gunung\n"
            "      int w=r-5;\n"
            "      s.drawRoundRect(cx-w,cy-w+3,w*2,w*2-6,3,ic);\n"
            "      s.fillCircle(cx+w/2,cy-w/2+3,2,ic);\n"
            "      s.fillTriangle(cx-w+2,cy+w-5,cx-w/3,cy-2,cx+1,cy+w-5,ic);\n"
            "      s.fillTriangle(cx-2,cy+w-5,cx+w/3+1,cy+1,cx+w-2,cy+w-5,ic);\n"
            "      break;\n"
            "    }\n"
            "    default: {\n      s.setTextColor(ic); s.setTextSize(2);\n      char b[2]={sym,0};", "app icon")

    # ---- File Explorer: ganti seluruh blok ----
    a = s.find(EXP_START)
    b = s.find(EXP_END)
    if a < 0 or b < 0 or s.count(EXP_START) != 1 or s.count(EXP_END) != 1 or b < a:
        sys.exit("GAGAL [explorer]: penanda blok File Explorer / MJPEG Player tidak ketemu tepat 1x.")
    s = s[:a] + EXPLORER_BLOCK + s[b:]

    # ---- hook loop(): scroll + tap-saat-lepas ----
    anchor = "    if(curScreen()==SCR_SETTINGS && !kbVisible) settingsPointerTick(touched,wasTouched,tx,ty);"
    if s.count(anchor) != 1:
        sys.exit("GAGAL [loop hook]: anchor settingsPointerTick ketemu %d kali." % s.count(anchor))
    i = s.index(anchor)
    eol = s.index("\n", i) + 1          # sisipkan SETELAH seluruh baris asli (komentar ikut tetap di barisnya)
    s = (s[:eol] +
         "    if(curScreen()==SCR_FILEEXPLORER) fileExpPointerTick(touched,wasTouched,tx,ty); // File Explorer: drag-scroll + ketuk saat lepas\n"
         "    if(curScreen()==SCR_GALLERY) galPointerTick(touched,wasTouched,tx,ty);          // Galeri: scroll/ketuk + pemuat thumbnail\n" +
         s[eol:])

    write(p, s)
    print("phone.ino: OK")


def patch_workflow():
    p = ".github/workflows/build-firmware.yml"
    if not os.path.exists(os.path.join(ROOT, p)):
        print(p + ": tidak ada, dilewati (tambahkan 'cp gallery_renphone.ino build_sketch/' manual)")
        return
    s = read(p)
    if "gallery_renphone.ino" in s:
        print(p + ": sudah dipatch, dilewati")
        return
    s = rep(s, "          cp features_renphone.ino build_sketch/\n",
            "          cp features_renphone.ino build_sketch/\n"
            "          cp gallery_renphone.ino build_sketch/\n", "workflow cp")
    write(p, s)
    print(p + ": OK")


if __name__ == "__main__":
    patch_phone()
    patch_workflow()
    print("Selesai. Commit: phone.ino, gallery_renphone.ino, .github/workflows/build-firmware.yml")
