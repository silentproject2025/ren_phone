#!/usr/bin/env python3
# apply_kb_ux.py -- KB_UX_V1
# Jalankan dari root repo:  python3 tools/patches/apply_kb_ux.py
#
# Keyboard virtual: tampilan & cara pakai ditingkatkan, TANPA mengubah tata letak tuts
# (kbY()/kbKeysY()/ukuran tuts sama persis, jadi semua app yang memakai keyboard tidak bergeser).
#
#  1. CAPS LOCK      : ketuk shift 2x cepat -> huruf kapital terus. Shift menampilkan garis di bawah
#                      panah saat caps lock aktif. Ketuk shift lagi -> mati.
#  2. KAPITAL OTOMATIS: awal kalimat (field kosong, setelah ". " "! " "? " atau baris baru) shift
#                      menyala sendiri. Hanya field teks bebas (Notepad, Chat); sandi/URL/SSID/kunci
#                      API tidak disentuh. Ketuk shift sekali di awal kalimat = batal.
#  3. SPASI 2x       : spasi dua kali cepat setelah huruf/angka -> ". " (hanya Notepad, Chat).
#  4. TAHAN HAPUS    : tahan tombol hapus -> hapus berulang, makin lama makin cepat
#                      (jeda 380 ms, lalu 90 -> 55 -> 35 ms per karakter).
#
# Tiap fitur bisa dimatikan lewat #define di blok "KB_UX_V1" (KB_UX_AUTOCAP, KB_UX_DBLSPACE,
# KB_UX_HOLDDEL = 0). Berkas yang diubah: phone.ino, kamus_renphone.ino, CHANGELOG.md,
# docs/features/system-ui.md. Aman dijalankan ulang (cek penanda).
import os, sys

MARK = "KB_UX_V1"
PH = os.path.join("firmware", "phone", "phone.ino")
KM = os.path.join("firmware", "phone", "kamus_renphone.ino")
CL = "CHANGELOG.md"
DOC = os.path.join("docs", "features", "system-ui.md")


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus tepat 1x). Tidak ada berkas yang diubah." % (label, n))
    return s.replace(old, new)


ph = rd(PH)
km = rd(KM)
if MARK in ph:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)

# ---------------------------------------------------------------- phone.ino
# 1) status + fungsi bantu, sebelum blok efek tekan
ph = sub(ph, "int kbPressKey=-1;", r'''// ---- KB_UX_V1: caps lock, kapital otomatis, spasi 2x -> ". ", tahan hapus ----
#define KB_UX_AUTOCAP   1   // 1 = awal kalimat otomatis kapital (hanya Notepad & Chat)
#define KB_UX_DBLSPACE  1   // 1 = spasi 2x cepat setelah huruf/angka -> ". " (hanya Notepad & Chat)
#define KB_UX_HOLDDEL   1   // 1 = tahan HAPUS -> hapus berulang, makin cepat
bool kbProseField();                       // kamus_renphone.ino: true utk field teks bebas (Notepad, Chat)
bool kbCaps=false;                         // caps lock (ketuk shift 2x cepat)
uint32_t kbShiftTapMs=0, kbSpaceTapMs=0;   // waktu ketukan shift / spasi terakhir
uint32_t kbRepArm=0;                       // kapan hapus-tahan berikutnya (0 = tidak aktif)
int      kbRepN=0;                         // jumlah karakter yang sudah dihapus oleh tahan-hapus
String*  kbAutoOffFor=nullptr;             // shift dimatikan manual di awal kalimat: berlaku utk field+panjang teks ini saja
int      kbAutoOffLen=-1;
// true bila teks berakhir di awal kalimat: kosong, baris baru, atau tanda . ! ? lalu spasi
static bool kbSentenceStart(const String& t){
  int n=(int)t.length();
  if(n==0) return true;
  if(t.charAt(n-1)=='\n') return true;
  int i=n-1; while(i>=0 && t.charAt(i)==' ') i--;
  if(i==n-1 || i<0) return false;          // tidak ada spasi di ujung, atau isinya spasi semua
  char p=t.charAt(i);
  return p=='.'||p=='!'||p=='?';
}
// Menaikkan LOWER -> UPPER di awal kalimat (tidak pernah menurunkan). Dipanggil dari drawKb() & kbTouch() supaya
// yang digambar dan yang diketik selalu sama.
static void kbAutoCapSync(){
#if KB_UX_AUTOCAP
  if(!kbTarget || kbCaps || kbMode!=KB_LOWER || !kbProseField()) return;
  if(kbAutoOffFor==kbTarget && kbAutoOffLen==(int)kbTarget->length()) return;   // user baru mematikannya di sini
  if(kbSentenceStart(*kbTarget)) kbMode=KB_UPPER;
#endif
}
static void kbUxNormalize(){
  if(kbCaps && kbMode!=KB_UPPER) kbCaps=false;   // pindah ke 123/emoji atau keyboard dibuka ulang
  kbAutoCapSync();
}

int kbPressKey=-1;''', "int kbPressKey=-1;")

# 2) drawKb: sinkron mode di awal
ph = sub(ph, "void drawKb(LGFX_Sprite& s){\n  int yTop=kbY();",
         "void drawKb(LGFX_Sprite& s){\n  kbUxNormalize();                                 // KB_UX_V1\n  int yTop=kbY();", "drawKb")

# 3) drawKb: garis caps lock di bawah panah shift
ph = sub(ph, "    s.fillRect(acx-3,acy+2,6,4,ic);\n    }",
         "    s.fillRect(acx-3,acy+2,6,4,ic);\n      if(kbCaps) s.fillRect(acx-6,acy+8,12,2,ic);   // KB_UX_V1: caps lock\n    }", "shift draw")

# 4) kbTouch: normalisasi di awal
ph = sub(ph, "void kbTouch(int x,int y){\n  if(!kbVisible||!kbTarget) return;\n",
         "void kbTouch(int x,int y){\n  if(!kbVisible||!kbTarget) return;\n  kbUxNormalize();                                 // KB_UX_V1\n", "kbTouch head")

# 5) shift: caps lock + batal kapital otomatis
ph = sub(ph,
         "      else    kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER);\n      kbPressKey=100;",
         r'''      else {                                                                  // KB_UX_V1: shift / caps lock
        uint32_t nm=millis();
        if(kbCaps){ kbCaps=false; kbMode=KB_LOWER; kbAutoOffFor=kbTarget; kbAutoOffLen=(int)kbTarget->length(); }
        else if((uint32_t)(nm-kbShiftTapMs)<400){ kbCaps=true; kbMode=KB_UPPER; }            // 2x cepat = caps lock
        else if(kbMode==KB_UPPER){ kbMode=KB_LOWER; kbAutoOffFor=kbTarget; kbAutoOffLen=(int)kbTarget->length(); }
        else kbMode=KB_UPPER;
        kbShiftTapMs=nm;
      }
      kbPressKey=100;''', "shift branch")

# 6) spasi: spasi 2x -> ". "
ph = sub(ph,
         "    else if(x<dlX-2){       kbSugOnChar(*kbTarget,' '); *kbTarget+=\" \";  kbPressKey=102; }",
         r'''    else if(x<dlX-2){                                                         // KB_UX_V1: spasi (2x cepat -> ". ")
      uint32_t nm=millis();
      bool dbl=false;
#if KB_UX_DBLSPACE
      { int n=(int)kbTarget->length();
        if(kbProseField() && !emo && kbMode!=KB_NUM && n>=2 && (uint32_t)(nm-kbSpaceTapMs)<450 && kbTarget->charAt(n-1)==' '){
          char pc=kbTarget->charAt(n-2);
          if((unsigned char)pc<128 && isalnum((unsigned char)pc)) dbl=true;
        }
      }
#endif
      if(dbl){ kbTarget->remove((unsigned int)(kbTarget->length()-1)); *kbTarget+=". "; }
      else   { kbSugOnChar(*kbTarget,' '); *kbTarget+=" "; }
      kbSpaceTapMs=nm; kbPressKey=102;
      kbAutoCapSync();
    }''', "space branch")

# 7) hapus: kapital otomatis + mulai tahan-hapus
ph = sub(ph,
         "      kbPressKey=103;\n    }\n    kbPressUntil=millis()+KB_PRESS_MS;",
         "      kbPressKey=103;\n      kbAutoCapSync();                                         // KB_UX_V1\n#if KB_UX_HOLDDEL\n      kbRepArm=millis()+380; kbRepN=0;                         // KB_UX_V1: tahan hapus mulai berulang setelah 380 ms\n#endif\n    }\n    kbPressUntil=millis()+KB_PRESS_MS;",
         "backspace branch")

# 8) huruf diketik: caps lock tidak turun, lalu cek kapital otomatis
ph = sub(ph,
         "  if(kbMode==KB_UPPER)kbMode=KB_LOWER;\n  needRedraw=true;\n}",
         "  if(kbMode==KB_UPPER && !kbCaps) kbMode=KB_LOWER;   // KB_UX_V1: caps lock tetap\n  kbAutoCapSync();                                    // KB_UX_V1\n  needRedraw=true;\n}", "letter tail")

# 9) fungsi tahan-hapus (setelah kbTouch, sebelum blok TOAST)
ph = sub(ph, "// =============================================\n// TOAST\n", r'''// KB_UX_V1: tombol hapus (dipakai tahan-hapus di loop)
bool kbBackspaceHit(int x,int y){
  if(!kbVisible||!kbTarget) return false;
  int cell=kbKeyH()+2, cy=kbKeysY()+3*cell, R=SCR_W-kbLeft();
  return y>=cy && x>=R-84-2;
}
void kbHoldDeleteStep(){
  if(!kbVisible||!kbTarget) return;
  emjBackspace(*kbTarget);
  kbPressKey=103; kbPressUntil=millis()+KB_PRESS_MS;
  kbAutoCapSync();
  needRedraw=true;
}

// =============================================
// TOAST
''', "toast anchor")

# 10) loop: panggil tahan-hapus + definisinya
ph = sub(ph,
         "  kbFastTick();                              // KB_FASTPATH_V1: lepas efek tekan (area keyboard saja)\n",
         "  kbFastTick();                              // KB_FASTPATH_V1: lepas efek tekan (area keyboard saja)\n  kbHoldTick(touched,tx,ty);                 // KB_UX_V1: tahan HAPUS = hapus berulang\n",
         "loop call")
ph = sub(ph, "// TAP-FIX: true kalau jari yg barusan lepas", r'''// KB_UX_V1: tahan HAPUS. kbRepArm diisi kbTouch() saat HAPUS diketuk; di sini jari harus tetap di tombol hapus.
static void kbHoldTick(bool touched,int tx,int ty){
#if KB_UX_HOLDDEL
  if(!kbRepArm) return;
  if(!touched || !(kbVisible && kbTarget) || !kbFastOverlaysIdle() || !kbBackspaceHit(tx,ty)){ kbRepArm=0; return; }
  uint32_t now=millis();
  if((int32_t)(now-kbRepArm)<0) return;
  kbHoldDeleteStep();
  kbRepN++;
  kbRepArm=now+(kbRepN<6 ? 90 : (kbRepN<16 ? 55 : 35));       // makin lama makin cepat
#else
  (void)touched; (void)tx; (void)ty;
#endif
}

// TAP-FIX: true kalau jari yg barusan lepas''', "tap-fix anchor")

# ---------------------------------------------------------------- kamus_renphone.ino
old = "int kbBarH() { return (kbVisible && kmsPolicy() > 0) ? KMS_BAR_H : 0; }"
km = sub(km, old, old + "\n// KB_UX_V1: field teks bebas (kapital otomatis & spasi 2x hanya di sini; sandi/URL/SSID/kunci API tidak).\nbool kbProseField() { return kbTarget && (kbTarget == &noteText || kbTarget == &chatInput); }", "kbBarH")

# ---------------------------------------------------------------- CHANGELOG + docs (opsional, dilewati bila pola tak ada)
if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if MARK not in c and c.count(anchor) == 1:
        entry = ("- Improved: the on-screen keyboard is easier to use (KB_UX_V1). Tap Shift twice quickly for caps lock (a bar under the arrow shows it; tap again to turn it off). "
                 "In Notepad and Chat the first letter of a sentence is capitalised automatically (empty field, or after `. ` `! ` `? ` or a new line; tap Shift once to cancel), "
                 "and tapping Space twice after a letter or digit types `. `. Holding Backspace now repeats, faster the longer you hold. Password, URL, SSID and API-key fields are not touched. "
                 "The key layout and size are unchanged. Not yet tested on a device. Script: `tools/patches/apply_kb_ux.py`\n")
        wr(CL, c.replace(anchor, anchor + entry))
if os.path.exists(DOC):
    d = rd(DOC)
    old_row = "| **On-screen keyboard** | Available in every app. |"
    if d.count(old_row) == 1:
        wr(DOC, d.replace(old_row, "| **On-screen keyboard** | Available in every app. Shift twice quickly = caps lock; in Notepad and Chat the first letter of a sentence is capitalised automatically and a double space types `. `; holding Backspace repeats. |"))

wr(PH, ph)
wr(KM, km)
print("ok: phone.ino, kamus_renphone.ino (+ CHANGELOG.md, docs/features/system-ui.md bila ada)")
