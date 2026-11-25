#!/usr/bin/env python3
# apply_aod_fx.py -- AOD_FX_V1
# Jalankan dari root repo:  python3 tools/patches/apply_aod_fx.py
#
# Transisi layar <-> AOD (lock screen DAN Home) dibuat halus, dulu loncat instan:
#   1. Masuk AOD: frame terakhir layar dipotret, lalu memudar ke hitam selama 0,9 dtk sambil lampu latar
#      turun bertahap (interpolasi di ruang persepsi, jadi redupnya terasa rata, bukan jatuh di awal).
#      Setelah itu jam AOD muncul pelan (0,45 dtk).
#   2. Bangun dari AOD (sentuhan): layar hidup dirender sekali, lalu memudar naik dari hitam selama 0,3 dtk
#      sambil lampu latar naik; jam AOD ikut memudar. Memudar dari tengah jalan juga mulus (mulai dari
#      kecerahan saat itu, bukan loncat).
#   3. Wajah AOD baru (lock & Home sama): jam, tanggal, jumlah notifikasi, dan IP WiFi, semuanya abu redup.
#   4. Kalau PSRAM tidak cukup untuk potret layar (~300 KB), otomatis jatuh ke perilaku lama (instan).
# Pemanggil lain (panggilan masuk, remote, charger, dll) tetap memakai bangun instan, tidak berubah.
# Berkas: firmware/phone/phone.ino, CHANGELOG.md, docs/features/system-ui.md.
# Aman dijalankan ulang (cek penanda). Jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

MARK = "AOD_FX_V1"
PH = os.path.join("firmware", "phone", "phone.ino")
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


for p in (PH, CL, DOC):
    if not os.path.exists(p):
        sys.exit("GAGAL: %s tidak ada. Jalankan dari root repo. Tidak ada berkas yang diubah." % p)

s = rd(PH)
if MARK in s:
    print("%s sudah terpasang, tidak ada yang diubah." % MARK)
    sys.exit(0)

# ---------------------------------------------------------------------------------------------
# A. inti fx (state, potret layar, langkah animasi) -- sebelum lockScreenReset()
# ---------------------------------------------------------------------------------------------
BLOCK_A = r'''unsigned long lastLockActivityMs=0;
unsigned long lastLockClockMs=0;

// =================================================================
// AOD_FX_V1: transisi halus layar <-> AOD (lock screen & Home).
//  MASUK : OUT (0,9 dtk) frame terakhir memudar ke hitam + lampu latar turun  ->  CLOCK (0,45 dtk) jam AOD muncul
//  BANGUN: WAKE (0,3 dtk) layar hidup memudar naik dari hitam + lampu latar naik, lalu flag AOD dimatikan
// Selama fx jalan flag aodActive / homeAodActive SUDAH true, jadi semua syarat lama (hardware scroll, overlay,
// pager, dll yg berhenti saat AOD) tetap berlaku; gambarnya saja yg dipegang fungsi aodFxFrame().
// Potret layar disimpan di aodSnap (PSRAM); kalau tidak muat, jatuh ke perilaku lama (instan).
// =================================================================
#define AODFX_NONE     0
#define AODFX_OUT      1
#define AODFX_CLOCK    2
#define AODFX_WAKE     3
#define AODFX_OUT_MS   900UL
#define AODFX_CLOCK_MS 450UL
#define AODFX_WAKE_MS  300UL
extern bool homeAodActive;   // definisi di bawah (v99)
uint8_t aodFxMode=AODFX_NONE;
bool    aodFxHome=false;       // fx milik AOD Home (true) atau lock screen (false)
bool    aodFxNeedSnap=false;   // WAKE: frame hidup belum dirender
unsigned long aodFxT0=0;
float   aodFxBlCur=200.0f, aodFxBl0=200.0f;
LGFX_Sprite aodSnap(&display);
bool    aodSnapOk=false;

static void aodSnapFree(){ if(aodSnap.getBuffer()) aodSnap.deleteSprite(); aodSnapOk=false; }
static bool aodSnapAlloc(){
  if(canvas.getColorDepth()!=16 || !canvas.getBuffer()) return false;
  const int W=canvas.width(), H=canvas.height();
  if(aodSnap.getBuffer() && (aodSnap.width()!=W || aodSnap.height()!=H)) aodSnapFree();
  if(!aodSnap.getBuffer()){
    const size_t bytes=(size_t)W*(size_t)H*2;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + 1500000UL) return false;   // sisakan PSRAM utk app lain
    aodSnap.setColorDepth(16); aodSnap.setPsram(true);
    if(!aodSnap.createSprite(W,H)){ aodSnapFree(); return false; }
  }
  return true;
}
static bool aodSnapTake(){   // salin frame terakhir di canvas
  if(!aodSnapAlloc()) return false;
  memcpy(aodSnap.getBuffer(), canvas.getBuffer(), (size_t)canvas.width()*(size_t)canvas.height()*2);
  aodSnapOk=true;
  return true;
}
static float aodEase(float t){ if(t<0.0f) t=0.0f; if(t>1.0f) t=1.0f; return t*t*(3.0f-2.0f*t); }
static float aodFxP(unsigned long dur){
  float p=(float)(millis()-aodFxT0)/(float)dur;
  return p<0.0f ? 0.0f : (p>1.0f ? 1.0f : p);
}
// Interpolasi kecerahan di ruang persepsi (gamma ~2,2) supaya redup/terangnya terasa rata.
static float aodBlMix(float a,float b,float e){
  float pa=powf(a<1.0f?1.0f:a,0.45f), pb=powf(b<1.0f?1.0f:b,0.45f);
  float v=powf(pa+(pb-pa)*e,1.0f/0.45f);
  if(v<1.0f) v=1.0f; if(v>255.0f) v=255.0f;
  return v;
}
static void aodSetBl(float v){
  if(v<0.0f) v=0.0f; if(v>255.0f) v=255.0f;
  aodFxBlCur=v; display.setBrightness((uint8_t)(v+0.5f));
}
void aodFxReset(bool home){   // bangun instan / kunci ulang membatalkan fx milik pemanggil yg sama
  if(aodFxMode!=AODFX_NONE && aodFxHome!=home) return;
  aodFxMode=AODFX_NONE; aodFxNeedSnap=false; aodSnapFree();
}
// Dipanggil SESAAT SEBELUM flag AOD dinyalakan, saat canvas masih berisi frame terakhir.
bool aodFxBegin(bool home){
  aodFxHome=home; aodFxNeedSnap=false;
  if(aodSnapTake()){
    aodFxMode=AODFX_OUT; aodFxT0=millis(); aodFxBl0=(float)brightness; aodFxBlCur=aodFxBl0;
    return true;
  }
  aodFxMode=AODFX_NONE; aodSetBl((float)AOD_BRIGHTNESS);   // fallback: instan seperti dulu
  return false;
}
// Mulai bangun halus. false = tak bisa (PSRAM tipis) -> pemanggil memakai bangun instan.
bool aodFxWake(bool home){
  if(aodFxMode==AODFX_WAKE) return true;
  if(!aodSnapAlloc()) return false;
  aodFxHome=home; aodFxMode=AODFX_WAKE; aodFxNeedSnap=true;
  aodFxBl0=aodFxBlCur; aodFxT0=millis();
  needRedraw=true;
  return true;
}
// Dipanggil tiap loop() dari lockIdleTick() / homeIdleTick().
void aodFxStep(){
  if(aodFxMode==AODFX_NONE) return;
  uiBoostKick(); needRedraw=true;
  if(aodFxMode==AODFX_OUT){
    float p=aodFxP(AODFX_OUT_MS);
    if(p>=1.0f){ aodSetBl((float)AOD_BRIGHTNESS); aodSnapFree(); aodFxMode=AODFX_CLOCK; aodFxT0=millis(); }
    else aodSetBl(aodBlMix(aodFxBl0,(float)AOD_BRIGHTNESS,aodEase(p)));
  } else if(aodFxMode==AODFX_CLOCK){
    if(aodFxP(AODFX_CLOCK_MS)>=1.0f) aodFxMode=AODFX_NONE;
  } else if(aodFxMode==AODFX_WAKE){
    if(aodFxNeedSnap) return;   // frame hidup belum siap: timer mulai setelah itu (lihat aodFxFrame)
    float p=aodFxP(AODFX_WAKE_MS);
    if(p>=1.0f){
      aodFxMode=AODFX_NONE; aodSnapFree(); aodSetBl((float)brightness);
      if(aodFxHome) homeAodActive=false; else aodActive=false;
      needRedraw=true;
    } else aodSetBl(aodBlMix(aodFxBl0,(float)brightness,aodEase(p)));
  }
}
'''
s = sub(s, "unsigned long lastLockActivityMs=0;\nunsigned long lastLockClockMs=0;\n", BLOCK_A, "blok-A")

# ---------------------------------------------------------------------------------------------
# B. lock: reset, bangun instan + halus, idle tick
# ---------------------------------------------------------------------------------------------
s = sub(s,
        "void lockScreenReset(){\n"
        "  homeJiggle=false; homeGoneIdx=-1;\n"
        "  lastLockActivityMs=millis(); lastLockClockMs=millis();\n"
        "  if(aodActive){ aodActive=false; display.setBrightness((uint8_t)brightness); }\n"
        "}\n"
        "void aodWake(){\n"
        "  if(aodActive){ aodActive=false; display.setBrightness((uint8_t)brightness); needRedraw=true; }\n"
        "  lastLockActivityMs=millis();\n"
        "}\n",
        "void lockScreenReset(){\n"
        "  homeJiggle=false; homeGoneIdx=-1;\n"
        "  lastLockActivityMs=millis(); lastLockClockMs=millis();\n"
        "  aodFxMode=AODFX_NONE; aodFxNeedSnap=false; aodSnapFree();   // AOD_FX_V1\n"
        "  if(aodActive || homeAodActive){ aodActive=false; homeAodActive=false; display.setBrightness((uint8_t)brightness); }\n"
        "}\n"
        "void aodWake(){   // instan -- dipakai pemanggil lain (panggilan, remote, charger)\n"
        "  aodFxReset(false);   // AOD_FX_V1\n"
        "  if(aodActive){ aodActive=false; display.setBrightness((uint8_t)brightness); needRedraw=true; }\n"
        "  lastLockActivityMs=millis();\n"
        "}\n"
        "void aodWakeSmooth(){   // AOD_FX_V1: sentuhan = bangun halus\n"
        "  lastLockActivityMs=millis();\n"
        "  if(!aodActive) return;\n"
        "  if(aodFxMode==AODFX_WAKE) return;\n"
        "  if(aodFxWake(false)) return;\n"
        "  aodWake();   // fallback instan\n"
        "}\n", "lock-reset-wake")

s = sub(s,
        "  lockNotifTick();   // NOTIF-LOCK: animasi kartu masuk / hapus / balik\n"
        "  if(touched){ aodWake(); return; }\n",
        "  lockNotifTick();   // NOTIF-LOCK: animasi kartu masuk / hapus / balik\n"
        "  aodFxStep();       // AOD_FX_V1: langkah animasi redup / bangun\n"
        "  if(touched){ aodWakeSmooth(); return; }\n", "lock-tick-head")

s = sub(s,
        "  if(!aodActive && millis()-lastLockActivityMs>AOD_IDLE_MS){\n"
        "    aodActive=true;\n"
        "    display.setBrightness(AOD_BRIGHTNESS);\n"
        "    needRedraw=true;\n"
        "  }\n",
        "  if(!aodActive && millis()-lastLockActivityMs>AOD_IDLE_MS){\n"
        "    aodFxBegin(false);   // AOD_FX_V1: potret frame + mulai redup halus (atau instan kalau PSRAM tipis)\n"
        "    aodActive=true;\n"
        "    needRedraw=true;\n"
        "  }\n", "lock-tick-idle")

# ---------------------------------------------------------------------------------------------
# C. Home: bangun instan + halus, idle tick
# ---------------------------------------------------------------------------------------------
s = sub(s,
        "void homeAodWake(){\n"
        "  if(homeAodActive){ homeAodActive=false; display.setBrightness((uint8_t)brightness); needRedraw=true; }\n"
        "  lastHomeActivityMs=millis();\n"
        "}\n",
        "void homeAodWake(){   // instan -- dipakai pemanggil lain (nav, panggilan, remote, dll)\n"
        "  aodFxReset(true);   // AOD_FX_V1\n"
        "  if(homeAodActive){ homeAodActive=false; display.setBrightness((uint8_t)brightness); needRedraw=true; }\n"
        "  lastHomeActivityMs=millis();\n"
        "}\n"
        "void homeAodWakeSmooth(){   // AOD_FX_V1: sentuhan = bangun halus\n"
        "  lastHomeActivityMs=millis();\n"
        "  if(!homeAodActive) return;\n"
        "  if(aodFxMode==AODFX_WAKE) return;\n"
        "  if(aodFxWake(true)) return;\n"
        "  homeAodWake();   // fallback instan\n"
        "}\n", "home-wake")

s = sub(s,
        "  if(touched){ homeAodWake(); return; }\n"
        "  if(!homeAodActive && millis()-lastHomeActivityMs>HOME_AOD_IDLE_MS){\n"
        "    homeAodActive=true;\n"
        "    display.setBrightness(AOD_BRIGHTNESS);\n"
        "    needRedraw=true;\n"
        "  }\n",
        "  aodFxStep();   // AOD_FX_V1\n"
        "  if(touched){ homeAodWakeSmooth(); return; }\n"
        "  if(!homeAodActive && millis()-lastHomeActivityMs>HOME_AOD_IDLE_MS){\n"
        "    aodFxBegin(true);   // AOD_FX_V1\n"
        "    homeAodActive=true;\n"
        "    needRedraw=true;\n"
        "  }\n", "home-tick")

# drawHomeAod lama -> deklarasi maju (definisi baru ada setelah lockIpString)
OLD_HOME_AOD = (
    "void drawHomeAod(LGFX_Sprite& s){\n"
    "  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);\n"
    "  char tb[6]; if(ok) sprintf(tb,\"%02d:%02d\",t.tm_hour,t.tm_min); else strcpy(tb,\"--:--\");\n"
    "  s.fillSprite(0x0000);\n"
    "  s.setFont(emjF(&lgfx::fonts::FreeSansBold9pt7b));\n"
    "  s.setTextSize(2);\n"
    "  int tw=s.textWidth(tb);\n"
    "  s.setTextColor(0x39C7); // abu redup -- konsisten sama AOD lock screen\n"
    "  s.setCursor(SCR_W/2-tw/2, SCR_H/2-16);\n"
    "  s.print(tb);\n"
    "  s.setFont(emjF(&lgfx::fonts::Font0));\n"
    "}\n")
s = sub(s, OLD_HOME_AOD,
        "void drawHomeAod(LGFX_Sprite& s);   // AOD_FX_V1: definisi dipindah ke bawah, setelah lockIpString()\n", "home-aod-lama")

# ---------------------------------------------------------------------------------------------
# D. wajah AOD + penggambar fx (setelah lockIpString) + drawHomeAod baru
# ---------------------------------------------------------------------------------------------
BLOCK_D = r'''  snprintf(out,n,"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
  return true;
}

// AOD_FX_V1: wajah AOD (dipakai lock screen & Home): jam, tanggal, jumlah notifikasi, IP -- abu redup.
extern int notifHistCount;   // phone_notif_center.ino
void drawLockScreen(LGFX_Sprite& s);
void drawHome(LGFX_Sprite& s,float sc);
static void aodFaceText(LGFX_Sprite& s,uint8_t k){   // hanya teks (tanpa mengisi latar); k = 0..255 kecerahan
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  const uint16_t col=blend565(0x0000,0x39C7,k);   // abu redup, sama spt AOD lama
  s.setFont(emjF(&lgfx::fonts::FreeSansBold9pt7b));
  s.setTextSize(2);
  int tw=s.textWidth(tb);
  s.setTextColor(col);
  s.setCursor(SCR_W/2-tw/2, SCR_H/2-16);
  s.print(tb);
  s.setFont(emjF(&lgfx::fonts::Font0));
  s.setTextSize(1); s.setTextColor(col);
  int y=SCR_H/2+34;
  if(ok){
    const char* dn[]={RPTR("Minggu"),RPTR("Senin"),RPTR("Selasa"),RPTR("Rabu"),RPTR("Kamis"),RPTR("Jumat"),RPTR("Sabtu")};
    const char* mn[]={"Jan","Feb","Mar","Apr",RPTR("Mei"),"Jun","Jul",RPTR("Agu"),"Sep",RPTR("Okt"),"Nov",RPTR("Des")};
    char db[32]; snprintf(db,sizeof(db),"%s, %d %s",dn[t.tm_wday],t.tm_mday,mn[t.tm_mon]);
    s.setCursor(SCR_W/2-(int)s.textWidth(db)/2, y); s.print(db); y+=14;
  }
  if(notifHistCount>0){
    char nb[32]; snprintf(nb,sizeof(nb),"%d %s",notifHistCount,RPTR("Notifikasi"));
    s.setCursor(SCR_W/2-(int)s.textWidth(nb)/2, y); s.print(nb); y+=14;
  }
  char ipb[20];
  if(lockIpString(ipb,sizeof(ipb))){
    s.setCursor(SCR_W/2-(int)s.textWidth(ipb)/2, y); s.print(ipb);
  }
}
static void aodFace(LGFX_Sprite& s,uint8_t k){ s.fillSprite(0x0000); aodFaceText(s,k); }

// Gambar frame saat fx jalan. true = frame sudah digambar (pemanggil langsung return).
static bool aodFxFrame(LGFX_Sprite& s,bool home){
  if(aodFxMode==AODFX_NONE) return false;
  if(aodFxMode==AODFX_OUT){
    if(!aodSnapOk) return false;
    float e=aodEase(aodFxP(AODFX_OUT_MS));
    aodSnap.pushSprite(&s,0,0);
    s.fillRectAlpha(0,0,SCR_W,SCR_H,(uint8_t)(e*255.0f),0x0000);
    return true;
  }
  if(aodFxMode==AODFX_CLOCK){
    aodFace(s,(uint8_t)(aodEase(aodFxP(AODFX_CLOCK_MS))*255.0f));
    return true;
  }
  // WAKE
  if(aodFxNeedSnap){   // render layar hidup SEKALI, simpan; frame ini masih tampil sbg AOD biasa (timer mulai sesudahnya)
    if(home){ homeAodActive=false; drawHome(s,homeScrollY); hpHomeCover(s); hpDrawDots(s); homeAodActive=true; }
    else    { aodActive=false; drawLockScreen(s); aodActive=true; }
    s.pushSprite(&aodSnap,0,0); aodSnapOk=true;
    aodFxNeedSnap=false; aodFxT0=millis();
    aodFace(s,255);
    return true;
  }
  if(!aodSnapOk) return false;
  float e=aodEase(aodFxP(AODFX_WAKE_MS));
  uint8_t inv=(uint8_t)((1.0f-e)*255.0f);
  aodSnap.pushSprite(&s,0,0);
  s.fillRectAlpha(0,0,SCR_W,SCR_H,inv,0x0000);
  if(inv>8) aodFaceText(s,inv);   // jam AOD memudar keluar selagi layar hidup memudar masuk
  return true;
}

// Tampilan AOD Home -- wajah yang sama dgn AOD lock screen, plus fx transisi.
void drawHomeAod(LGFX_Sprite& s){
  if(aodFxFrame(s,true)) return;
  aodFace(s,255);
}
'''
s = sub(s, '  snprintf(out,n,"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);\n  return true;\n}\n', BLOCK_D, "blok-D")

# ---------------------------------------------------------------------------------------------
# E. drawLockScreen: cabang AOD
# ---------------------------------------------------------------------------------------------
OLD_LOCK_AOD = (
    "  if(aodActive){\n"
    "    s.fillSprite(0x0000);\n"
    "    s.setFont(emjF(&lgfx::fonts::FreeSansBold9pt7b));\n"
    "    s.setTextSize(2);\n"
    "    int tw=s.textWidth(tb);\n"
    "    s.setTextColor(0x39C7); // abu redup -- sengaja gak putih penuh, senada semangat \"hemat/AOD\"\n"
    "    s.setCursor(SCR_W/2-tw/2, SCR_H/2-16);\n"
    "    s.print(tb);\n"
    "    s.setFont(emjF(&lgfx::fonts::Font0));\n"
    "    {\n"
    "      char ipb[20];\n"
    "      if(lockIpString(ipb,sizeof(ipb))){\n"
    "        s.setTextSize(1); s.setTextColor(0x39C7);\n"
    "        int iw=(int)s.textWidth(ipb);\n"
    "        s.setCursor(SCR_W/2-iw/2, SCR_H/2+50);\n"
    "        s.print(ipb);\n"
    "      }\n"
    "    }\n"
    "    return;\n"
    "  }\n")
s = sub(s, OLD_LOCK_AOD,
        "  if(aodActive){\n"
        "    if(aodFxFrame(s,false)) return;   // AOD_FX_V1: transisi redup / bangun\n"
        "    aodFace(s,255);                   // AOD_FX_V1: wajah AOD baru (jam, tanggal, notifikasi, IP)\n"
        "    return;\n"
        "  }\n", "lock-aod")

# ---------------------------------------------------------------------------------------------
# F. sentuhan pertama = bangun halus
# ---------------------------------------------------------------------------------------------
s = sub(s,
        "  if(newT && aodActive){ aodWake(); return; } // v96:",
        "  if(newT && aodActive){ aodWakeSmooth(); return; } // AOD_FX_V1 (dulu aodWake instan); v96:", "lock-input")
s = sub(s,
        "      if(newT) homeAodWake(); // sentuhan pertama cuma bangunin",
        "      if(newT) homeAodWakeSmooth(); // AOD_FX_V1 (dulu homeAodWake instan); sentuhan pertama cuma bangunin", "home-input")

# ---------------------------------------------------------------------------------------------
# CHANGELOG + docs
# ---------------------------------------------------------------------------------------------
cl = rd(CL)
entry = ("- Improved: smooth transitions between the screen and the Always-On Display, for both the lock screen and Home (" + MARK + "). Going to AOD, the last frame now fades to black over 0.9 s while the backlight steps down (stepped in perceived brightness, so it feels even), then the AOD clock fades in. Waking by touch renders the live screen once and fades it in over 0.3 s while the backlight comes up and the AOD clock fades out; waking in the middle of the fade starts from the current brightness instead of jumping. The AOD face is new and the same on lock screen and Home: clock, date, notification count and Wi-Fi IP, all in dim grey. The fade keeps one screenshot (about 300 KB) in PSRAM only while it runs; if PSRAM is too low it falls back to the old instant switch. Other wake-ups (incoming call, Remote, charger, navigation) are still instant. Only drawing and brightness code changed. Not yet compiled on the real build or tested on a device. Script: `tools/patches/apply_aod_fx.py`\n")
if "## Unreleased\n" not in cl:
    sys.exit("GAGAL: bagian '## Unreleased' tidak ada di CHANGELOG.md. Tidak ada berkas yang diubah.")
cl = cl.replace("## Unreleased\n", "## Unreleased\n" + entry, 1)

doc = rd(DOC)
old = "| **Always-On Display** | Low-power display while the screen is dimmed. |"
new = "| **Always-On Display** | Low-power display after a few seconds idle (8 s on the lock screen, 15 s on Home). The screen fades to black while the backlight steps down, then shows a dim clock, date, notification count and Wi-Fi IP; touching it fades the screen back in. If PSRAM is low the switch is instant. |"
if old in doc:
    doc = doc.replace(old, new, 1)

wr(PH, s)
wr(CL, cl)
wr(DOC, doc)
print("OK: %s terpasang di phone.ino, CHANGELOG.md, docs" % MARK)
