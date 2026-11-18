#!/usr/bin/env python3
# apply_png_support.py -- tambah dukungan PNG ke app Astronomi (APOD) + Galeri
#
# Jalankan dari folder repo (yang ada phone.ino & gallery_renphone.ino):
#     python apply_png_support.py
#
# - Decoder PNG = bawaan LovyanGFX (drawPng), JADI TIDAK ADA library baru,
#   workflow GitHub Actions tidak perlu diubah.
# - Script ini aman dijalankan ulang: kalau sudah terpasang, dia berhenti
#   tanpa mengubah apa pun. Kalau ada teks acuan yang tidak ketemu, dia
#   BATAL total (file tidak disentuh) supaya tidak setengah-setengah.
import sys, os

PHONE = "phone.ino"
GALLERY = "gallery_renphone.ino"
MARK = "v118: dukungan PNG"

for fn in (PHONE, GALLERY):
    if not os.path.exists(fn):
        sys.exit("ERROR: %s tidak ditemukan -- jalankan dari folder repo." % fn)

with open(PHONE, encoding="utf-8") as f: phone = f.read()
with open(GALLERY, encoding="utf-8") as f: gal = f.read()

if MARK in phone and MARK in gal:
    sys.exit("Sudah terpasang (penanda '%s' ada). Tidak ada yang diubah." % MARK)
if (MARK in phone) != (MARK in gal):
    sys.exit("ERROR: patch terpasang SEBAGIAN (hanya di satu file). Kembalikan file dari git lalu jalankan ulang.")

def rep(text, old, new, label):
    n = text.count(old)
    if n != 1:
        sys.exit("ERROR [%s]: teks acuan ditemukan %d kali (harus 1). Patch dibatalkan, tidak ada file diubah." % (label, n))
    return text.replace(old, new, 1)

# =====================================================================
# phone.ino
# =====================================================================

# 1) helper PNG bersama (dipakai APOD & Galeri) -- ditaruh sebelum apodImgKind
phone = rep(phone,
"// v117: cek isi file gambar APOD yg ada di SD dr 4 byte awalnya:\n",
r'''// =============================================
// v118: dukungan PNG (APOD + Galeri). Decoder = bawaan LovyanGFX
// (LGFXBase::drawPng, menskala langsung saat menggambar), jadi TIDAK
// butuh library tambahan & tidak menambah .bss. File PNG dibaca utuh ke
// PSRAM dulu (maks 4 MB), lalu didecode dari buffer itu.
//  - rpPngDims()    : baca lebar/tinggi dari header IHDR (tanpa decode)
//  - rpPngLoadFile(): baca file SD ke buffer PSRAM (caller wajib heap_caps_free)
//  - rpPngFitScale(): skala "contain", tidak pernah memperbesar
// =============================================
#define RP_PNG_MAX_BYTES  (4u*1024u*1024u)
#define RP_PNG_MAX_PIXELS 8000000L

bool rpPngDims(const uint8_t* d, size_t len, int* w, int* h){
  static const uint8_t sig[8]={0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
  if(!d || len<33 || memcmp(d,sig,8)!=0 || memcmp(d+12,"IHDR",4)!=0) return false;
  uint32_t pw=((uint32_t)d[16]<<24)|((uint32_t)d[17]<<16)|((uint32_t)d[18]<<8)|(uint32_t)d[19];
  uint32_t ph=((uint32_t)d[20]<<24)|((uint32_t)d[21]<<16)|((uint32_t)d[22]<<8)|(uint32_t)d[23];
  if(pw==0 || ph==0 || pw>16384 || ph>16384) return false;
  *w=(int)pw; *h=(int)ph;
  return true;
}
uint8_t* rpPngLoadFile(const char* path, size_t* outSize, size_t maxBytes){
  *outSize=0;
  File f=SD_MMC.open(path,FILE_READ);
  if(!f) return nullptr;
  size_t sz=(size_t)f.size();
  *outSize=sz; // diisi walau gagal -> caller bisa bedakan "kebesaran" vs "gagal baca"
  if(sz<33 || sz>maxBytes){ f.close(); return nullptr; }
  uint8_t* b=(uint8_t*)heap_caps_malloc(sz,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!b){ f.close(); return nullptr; }
  size_t got=0;
  while(got<sz){
    size_t want=sz-got; if(want>16384) want=16384;
    int n=f.read(b+got,want);
    if(n<=0) break;
    got+=(size_t)n;
  }
  f.close();
  if(got!=sz){ heap_caps_free(b); return nullptr; }
  return b;
}
float rpPngFitScale(int iw,int ih,int boxW,int boxH){
  if(iw<=boxW && ih<=boxH) return 1.0f;
  float a=(float)boxW/(float)iw, b=(float)boxH/(float)ih;
  return (a<b)?a:b;
}

// v117: cek isi file gambar APOD yg ada di SD dr 4 byte awalnya:
''', "phone:helpers")

# 2) fungsi gambar PNG ke sprite APOD -- sebelum apodDecodeCachedImage
phone = rep(phone,
"bool apodDecodeCachedImage(){\n",
r'''// v118: gambar PNG (dr buffer di RAM) ke sprite apodImg, gaya "contain"
// & di tengah kotak, sama kayak jalur JPEG. Kalau gagal, apodImgNote diisi.
bool apodDrawPng(const uint8_t* buf,size_t len,int boxW,int boxH){
  int iw=0, ih=0;
  if(!rpPngDims(buf,len,&iw,&ih)){ apodImgNote="File PNG rusak"; return false; }
  if((long)iw*(long)ih>RP_PNG_MAX_PIXELS){ apodImgNote="PNG terlalu besar (resolusi)"; return false; }
  float sc=rpPngFitScale(iw,ih,boxW,boxH);
  int dw=(int)((float)iw*sc+0.5f), dh=(int)((float)ih*sc+0.5f);
  if(dw<1) dw=1; if(dh<1) dh=1;
  if(dw>boxW) dw=boxW; if(dh>boxH) dh=boxH;
  int dx=(boxW-dw)/2, dy=(boxH-dh)/2;
  if(!apodImg.drawPng(buf,(uint32_t)len,dx,dy,0,0,0,0,sc,sc)){ apodImgNote="Gagal decode PNG"; return false; }
  return true;
}
bool apodDecodeCachedImage(){
''', "phone:apodDrawPng")

# 3) jalur SD: kind==2 (PNG) sekarang didecode, bukan ditolak
phone = rep(phone,
'''    int kind=apodImgKind(path);
    if(kind!=1){
      // v117: bukan JPEG -> jelasin alasannya di layar (dulu kotak kosong tanpa penjelasan)
      apodImgNote = (kind==2)? "Format PNG belum didukung" : (kind==3)? "Format GIF belum didukung" : "File gambar rusak";
      apodImgReady=true;
      return false;
    }
''',
'''    int kind=apodImgKind(path);
    if(kind==2){
      // v118: PNG -> baca ke PSRAM lalu decode pakai LovyanGFX (lihat apodDrawPng)
      size_t plen=0;
      uint8_t* pb=rpPngLoadFile(path.c_str(),&plen,RP_PNG_MAX_BYTES);
      bool pok=false;
      if(!pb) apodImgNote=(plen>RP_PNG_MAX_BYTES)? "PNG terlalu besar (maks 4 MB)" : "Gagal baca file PNG";
      else { pok=apodDrawPng(pb,plen,boxW,boxH); heap_caps_free(pb); }
      apodImgReady=true; // tetap "ready": sukses -> gambar tampil, gagal -> pesan alasan tampil
      return pok;
    }
    if(kind!=1){
      // v117: bukan JPEG/PNG -> jelasin alasannya di layar (dulu kotak kosong tanpa penjelasan)
      apodImgNote = (kind==3)? "Format GIF belum didukung" : "File gambar rusak";
      apodImgReady=true;
      return false;
    }
''', "phone:apod sd png")

# 4) jalur tanpa SD (buffer PSRAM hasil unduhan): deteksi PNG
phone = rep(phone,
'''    if(!nasaRamImgBuf || nasaRamImgLen==0) return false;
    opened = apodJpeg->openRAM(nasaRamImgBuf, nasaRamImgLen, apodJpegDrawCallback);
''',
'''    if(!nasaRamImgBuf || nasaRamImgLen==0) return false;
    if(nasaRamImgLen>=4 && nasaRamImgBuf[0]==0x89 && nasaRamImgBuf[1]=='P' && nasaRamImgBuf[2]=='N' && nasaRamImgBuf[3]=='G'){
      // v118: PNG di buffer RAM (SD gak dipasang)
      apodImgNote="";
      bool pok=apodDrawPng(nasaRamImgBuf,nasaRamImgLen,boxW,boxH);
      apodImgReady=true;
      return pok;
    }
    opened = apodJpeg->openRAM(nasaRamImgBuf, nasaRamImgLen, apodJpegDrawCallback);
''', "phone:apod ram png")

# 5) File Explorer: ketuk .png -> buka di Galeri
phone = rep(phone,
'if(low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp")){',
'if(low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".png") || low.endsWith(".bmp")){',
"phone:file explorer")

# =====================================================================
# gallery_renphone.ino
# =====================================================================

# 6) header komentar
gal = rep(gal,
"//  * ALBUM  = folder di SD Card yang isinya ada foto/video (JPG/JPEG, BMP,\n",
"//  * ALBUM  = folder di SD Card yang isinya ada foto/video (JPG/JPEG, PNG, BMP,\n",
"gal:header")

# 7) ekstensi .png dikenali (kind 0 = gambar JPEG/PNG; formatnya dicek dari isi file)
gal = rep(gal,
'''// 0 = JPG/JPEG, 1 = BMP, 2 = video MJPEG, -1 = bukan media
static int galKindOf(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return -1;
  if (galStrCmpI(dot, ".jpg") == 0 || galStrCmpI(dot, ".jpeg") == 0) return 0;
''',
'''// 0 = JPG/JPEG/PNG (v118: formatnya dicek dari ISI file, bukan ekstensi -- cache APOD
// menyimpan PNG pun dengan nama .jpg), 1 = BMP, 2 = video MJPEG, -1 = bukan media
static int galKindOf(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return -1;
  if (galStrCmpI(dot, ".jpg") == 0 || galStrCmpI(dot, ".jpeg") == 0 || galStrCmpI(dot, ".png") == 0) return 0;
''', "gal:kindOf")

# 8) thumbnail PNG + routing di galGenJpeg
gal = rep(gal,
"static bool galGenJpeg(const char* path, uint16_t* out) {\n",
r'''// ---- v118: dukungan PNG (decoder bawaan LovyanGFX, helper rpPng* ada di phone.ino) ----
#define GAL_PNG_THUMB_MAX_BYTES (2u * 1024u * 1024u)   // PNG lebih besar dari ini: tanpa thumbnail (decode PNG tidak bisa diskala murah seperti JPEG), tetap bisa dibuka di viewer

static bool galIsPng(const char* path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  uint8_t h[4] = { 0, 0, 0, 0 };
  int n = f.read(h, 4);
  f.close();
  return n == 4 && h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G';
}

// Thumbnail PNG: gambar diskala sehingga sisi terpendek = 72 px ke sprite PSRAM
// sementara, lalu bagian TENGAHNYA disalin (crop tengah, sama dgn thumbnail JPEG).
// Format piksel buffer sprite 16-bit LovyanGFX = format slot thumbnail (byte tertukar).
static bool galGenPng(const char* path, uint16_t* out) {
  size_t len = 0;
  uint8_t* b = rpPngLoadFile(path, &len, GAL_PNG_THUMB_MAX_BYTES);
  if (!b) return false;
  int iw = 0, ih = 0;
  if (!rpPngDims(b, len, &iw, &ih) || (long)iw * (long)ih > RP_PNG_MAX_PIXELS) { heap_caps_free(b); return false; }
  const int TS = GAL_THUMB;
  int mn = (iw < ih) ? iw : ih;
  float sc = (float)TS / (float)mn;
  int sw = (int)((float)iw * sc + 0.5f), sh = (int)((float)ih * sc + 0.5f);
  if (sw < TS) sw = TS;
  if (sh < TS) sh = TS;
  if ((long)sw * (long)sh * 2L > 2L * 1024L * 1024L) { heap_caps_free(b); return false; }   // panorama ekstrem
  LGFX_Sprite t(&display);
  t.setPsram(true);
  if (!t.createSprite(sw, sh)) { heap_caps_free(b); return false; }
  t.fillSprite(0x0000);
  bool ok = t.drawPng(b, (uint32_t)len, 0, 0, 0, 0, 0, 0, sc, sc);
  heap_caps_free(b);
  if (ok) {
    const uint16_t* src = (const uint16_t*)t.getBuffer();
    if (!src) ok = false;
    else {
      int ox = (sw - TS) / 2, oy = (sh - TS) / 2;
      for (int y = 0; y < TS; y++) memcpy(out + y * TS, src + (size_t)(oy + y) * (size_t)sw + ox, TS * 2);
    }
  }
  t.deleteSprite();
  return ok;
}

static bool galGenJpeg(const char* path, uint16_t* out) {
  if (galIsPng(path)) return galGenPng(path, out);   // v118
''', "gal:genPng")

# 9) viewer PNG + routing di galDecodeJpegView
gal = rep(gal,
"static bool galDecodeJpegView(const char* path) {\n",
r'''// ---- v118: viewer PNG (gaya "contain", tanpa memperbesar -- sama dgn JPEG/BMP) ----
static const char* galFailWhy = nullptr;   // alasan gagal (diisi decoder PNG) utk toast; nullptr = pakai pesan umum

static bool galDecodePngView(const char* path) {
  size_t len = 0;
  uint8_t* b = rpPngLoadFile(path, &len, RP_PNG_MAX_BYTES);
  if (!b) { galFailWhy = (len > RP_PNG_MAX_BYTES) ? "PNG terlalu besar (maks 4 MB)" : "PNG gagal dibaca"; return false; }
  int iw = 0, ih = 0;
  if (!rpPngDims(b, len, &iw, &ih)) { heap_caps_free(b); galFailWhy = "File PNG rusak"; return false; }
  if ((long)iw * (long)ih > RP_PNG_MAX_PIXELS) { heap_caps_free(b); galFailWhy = "PNG terlalu besar (resolusi)"; return false; }
  float sc = rpPngFitScale(iw, ih, SCR_W, SCR_H);
  int tw = (int)((float)iw * sc + 0.5f), th = (int)((float)ih * sc + 0.5f);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  if (tw > SCR_W) tw = SCR_W;
  if (th > SCR_H) th = SCR_H;
  int ox = (SCR_W - tw) / 2, oy = (SCR_H - th) / 2;
  bool ok = galView.drawPng(b, (uint32_t)len, ox, oy, 0, 0, 0, 0, sc, sc);
  heap_caps_free(b);
  if (!ok) { galFailWhy = "PNG gagal didecode"; return false; }
  galViewW = iw; galViewH = ih;
  return true;
}

static bool galDecodeJpegView(const char* path) {
  if (galIsPng(path)) return galDecodePngView(path);   // v118
''', "gal:viewPng")

# 10) toast gagal pakai alasan spesifik kalau ada
gal = rep(gal,
"  bool ok = (galItems[galViewIdx].kind == 1) ? galDecodeBmpView(p) : galDecodeJpegView(p);\n",
"  galFailWhy = nullptr;\n  bool ok = (galItems[galViewIdx].kind == 1) ? galDecodeBmpView(p) : galDecodeJpegView(p);\n",
"gal:failwhy reset")
gal = rep(gal,
'showToast("Gambar tidak bisa dibuka (JPEG progresif/BMP non-24bit?)", 2200);',
'showToast(galFailWhy ? galFailWhy : "Gambar tidak bisa dibuka (JPEG progresif/BMP non-24bit?)", 2200);',
"gal:toast")

# 11) teks petunjuk format
gal = rep(gal,
's.print("Format: JPG, BMP, MJPEG.");',
's.print("Format: JPG, PNG, BMP, MJPEG.");',
"gal:format text")

# tulis hasil (baru setelah SEMUA penggantian berhasil)
with open(PHONE, "w", encoding="utf-8") as f: f.write(phone)
with open(GALLERY, "w", encoding="utf-8") as f: f.write(gal)
print("OK: dukungan PNG terpasang di phone.ino & gallery_renphone.ino")
