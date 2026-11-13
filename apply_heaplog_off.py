#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_heaplog_off.py (HEAPLOG_TOGGLE_V1) -- heaplog dinonaktifkan secara bawaan + saklar di Setting.
 - heaplog_renphone.ino: rpHL()/rpHLFlush()/rpHLLoopPoll() langsung return kalau heaplog mati.
   Tidak ada lagi catatan RAM, tulis /heaplog.txt ke SD, maupun tabel yg MEMBEKUKAN layar 60 dtk di detik ke-100.
 - Setting > KINERJA > "Heaplog (RAM)": saklar on/off, disimpan di NVS "ui"/"heaplog" (default MATI).
   Berlaku setelah restart (nilai dibaca sekali saat boot, subtitle menunjukkan "Setelah restart").
Jalankan dari root repo (ada phone.ino & heaplog_renphone.ino). Idempotent.
"""
import sys, os
for f in ("phone.ino", "heaplog_renphone.ino"):
    if not os.path.exists(f):
        sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % f)

def load(p):
    t = open(p, encoding="utf-8", newline="").read()
    c = "\r\n" in t
    return (t.replace("\r\n", "\n") if c else t), c
def save(p, t, c):
    open(p, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n") if c else t)
def rep(t, old, new, name):
    if t.count(old) != 1:
        sys.exit("ERROR: blok '%s' ketemu %d kali (harus 1). Dibatalkan, tidak ada yg diubah." % (name, t.count(old)))
    return t.replace(old, new)

ph, pc = load("phone.ino"); hl, hc = load("heaplog_renphone.ino")
if "HEAPLOG_TOGGLE_V1" in ph or "HEAPLOG_TOGGLE_V1" in hl:
    print("[skip] sudah dipatch"); sys.exit(0)

# ---------- heaplog_renphone.ino ----------
hl = rep(hl, "void rpHL(const char* label){\n  if(rphlN >= RPHL_MAX) return;\n",
"""// HEAPLOG_TOGGLE_V1 (apply_heaplog_off.py): heaplog MATI bawaan, saklar di Setting > KINERJA.
static int8_t rphlOn = -1;       // nilai yg dipakai sejak boot (dibaca sekali dari NVS)
static bool   rphlWant = false;  // nilai tersimpan (berubah kalau saklar digeser; berlaku setelah restart)
static void rphlLoadPref(){
  if(rphlOn >= 0) return;
  Preferences p; p.begin("ui", false);
  rphlWant = p.getBool("heaplog", false);
  p.end();
  rphlOn = rphlWant ? 1 : 0;
}
bool rpHLBootOn(){ rphlLoadPref(); return rphlOn == 1; }
bool rpHLWanted(){ rphlLoadPref(); return rphlWant; }
void rpHLToggle(){
  rphlLoadPref(); rphlWant = !rphlWant;
  Preferences p; p.begin("ui", false); p.putBool("heaplog", rphlWant); p.end();
}

void rpHL(const char* label){
  if(!rpHLBootOn()) return;
  if(rphlN >= RPHL_MAX) return;
""", "rpHL")
hl = rep(hl, "void rpHLFlush(){\n  if(!sdReady){", "void rpHLFlush(){\n  if(!rpHLBootOn()) return;\n  if(!sdReady){", "rpHLFlush")
hl = rep(hl, "void rpHLLoopPoll(){\n  static uint8_t st = 0;", "void rpHLLoopPoll(){\n  if(!rpHLBootOn()) return;\n  static uint8_t st = 0;", "rpHLLoopPoll")

# ---------- phone.ino ----------
ph = rep(ph, "void rpHL(const char*); void rpHLFlush(); void rpHLLoopPoll();\n",
             "void rpHL(const char*); void rpHLFlush(); void rpHLLoopPoll();\nbool rpHLBootOn(); bool rpHLWanted(); void rpHLToggle();   // HEAPLOG_TOGGLE_V1\n", "fwd decl")
ph = rep(ph, "SR_HOMEEDIT, SR_BACKUP };", "SR_HOMEEDIT, SR_BACKUP, SR_HEAPLOG };", "enum")
ph = rep(ph, "  if(spiOcPendingIdx!=spiOcIdx) settAddRow(SR_SPIAPPLY,g,34,0,nullptr,y);\n",
             "  if(spiOcPendingIdx!=spiOcIdx) settAddRow(SR_SPIAPPLY,g,34,0,nullptr,y);\n  settAddRow(SR_HEAPLOG,g,38,0,nullptr,y);   // HEAPLOG_TOGGLE_V1\n", "layout")
ph = rep(ph, "case SR_MPU: case SR_TCAL: case SR_ABOUT: case SR_POWER: case SR_BACKUP:\n      return true;",
             "case SR_MPU: case SR_TCAL: case SR_ABOUT: case SR_POWER: case SR_BACKUP: case SR_HEAPLOG:\n      return true;", "settHasChip")
ph = rep(ph, "    case SR_BACKUP:   col=CHIP_TEAL;   break;\n", "    case SR_BACKUP:   col=CHIP_TEAL;   break;\n    case SR_HEAPLOG:  col=CHIP_GRAY;   break;\n", "settChip warna")
ph = rep(ph, "    case SR_ABOUT:     // \"i\"\n",
             "    case SR_HEAPLOG:   // grafik batang\n      s.fillRect(cx-6,cy+1,3,6,W); s.fillRect(cx-1,cy-3,3,10,W); s.fillRect(cx+4,cy-7,3,14,W);\n      break;\n    case SR_ABOUT:     // \"i\"\n", "glyph")
ph = rep(ph, "      case SR_SPIAPPLY:\n        s.setTextSize(1);",
"""      case SR_HEAPLOG:
        s.setTextColor(T().text); s.setCursor(xl,ty-4); s.print("Heaplog (RAM)");
        s.setTextColor(T().subtext); s.setCursor(xl,ty+6); s.print(rpHLWanted()!=rpHLBootOn()?"Setelah restart":"Log RAM saat boot");
        iosSwitch(s,xr-34,ry+(rh-20)/2,rpHLWanted());
        break;
      case SR_SPIAPPLY:
        s.setTextSize(1);""", "draw")
ph = rep(ph, "    case SR_SPIAPPLY:\n      spiOcIdx=spiOcPendingIdx;",
"""    case SR_HEAPLOG:
      rpHLToggle();
      showToast(rpHLWanted()?"Heaplog aktif (setelah restart)":"Heaplog nonaktif (setelah restart)");
      needRedraw=true; break;
    case SR_SPIAPPLY:
      spiOcIdx=spiOcPendingIdx;""", "touch")

save("phone.ino", ph, pc); save("heaplog_renphone.ino", hl, hc)
print("OK: heaplog mati bawaan; saklar ada di Setting > KINERJA > Heaplog (RAM)")
