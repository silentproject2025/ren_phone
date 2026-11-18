# DOOM_FIRE_LATCH_V1: tombol Fire/Use/OK/Pause di Doom sering "kadang ketekan kadang ngga".
#  1) Latch 90 ms: tombol aksi yg baru ditekan dianggap tetap ditekan minimal 90 ms, supaya ketukan
#     singkat tidak hilang di antara dua pembacaan tick Doom.
#  2) Area sentuh tombol diperbesar (gambar tombol TIDAK berubah): Fire 22->30 px, Use 16->20 px.
# Jalankan dari root repo:  py apply_doom_fire_latch.py
import sys, os
f = 'doom_renphone.ino'
if not os.path.exists(f):
    print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
s = open(f, encoding='utf-8').read()
if 'DOOM_FIRE_LATCH_V1' in s:
    print("Sudah terpasang, dilewati"); sys.exit(0)
def rep(s, old, new, name):
    if s.count(old) != 1:
        print("ERROR [%s]: anchor ketemu %d kali: %r" % (name, s.count(old), old[:60])); sys.exit(1)
    return s.replace(old, new)

s = rep(s, """  volatile bool raw[DBTN_COUNT] = {false};
  void begin() override { for(int i=0;i<DBTN_COUNT;i++){ raw[i]=false; cur[i]=false; prev[i]=false; } }
  void update() override { uint8_t pad=rcPadMask; for(int i=0;i<DBTN_COUNT;i++){ prev[i]=cur[i]; cur[i]=raw[i] || ((pad>>i)&1); } }
""", """  volatile bool raw[DBTN_COUNT] = {false};
  volatile uint32_t latchUntil[DBTN_COUNT] = {0};   // DOOM_FIRE_LATCH_V1: tombol aksi tetap "ditekan" sampai waktu ini (millis)
  void begin() override { for(int i=0;i<DBTN_COUNT;i++){ raw[i]=false; cur[i]=false; prev[i]=false; latchUntil[i]=0; } }
  void update() override { uint8_t pad=rcPadMask; uint32_t now=millis(); for(int i=0;i<DBTN_COUNT;i++){ prev[i]=cur[i]; cur[i]=raw[i] || ((pad>>i)&1) || ((int32_t)(latchUntil[i]-now)>0); } }
""", 'class')

s = rep(s, """static void doomApplyMasks(){
  uint8_t m = doomSlotMask[0] | doomSlotMask[1];
  for(int i=0;i<DBTN_COUNT;i++) doomInput.raw[i] = (m>>i)&1;
}""", """#define DOOM_LATCH_MS 90   // DOOM_FIRE_LATCH_V1
static void doomApplyMasks(){
  uint8_t m = doomSlotMask[0] | doomSlotMask[1];
  for(int i=0;i<DBTN_COUNT;i++){
    bool nv = (m>>i)&1;
    // tombol AKSI (bukan arah) baru ditekan -> tahan minimal DOOM_LATCH_MS agar tak hilang antar tick
    if(nv && !doomInput.raw[i] && ((1u<<i) & ~DOOM_DIR_MASK)){
      uint32_t u = millis() + DOOM_LATCH_MS; if(u==0) u=1;
      doomInput.latchUntil[i] = u;
    }
    doomInput.raw[i] = nv;
  }
}""", 'apply')

s = rep(s, "  if(dx*dx+dy*dy <= 22*22){ m |= (1u<<DBTN_FIRE); doomApplyMasks(); return; }",
           "  if(dx*dx+dy*dy <= 30*30){ m |= (1u<<DBTN_FIRE); doomApplyMasks(); return; }   // DOOM_FIRE_LATCH_V1: hit area diperbesar (gambar tetap 22)", 'fire')
s = rep(s, "  if(dx*dx+dy*dy <= 16*16){ if(isNew) m |= (1u<<DBTN_USE); doomApplyMasks(); return; }",
           "  if(dx*dx+dy*dy <= 20*20){ if(isNew) m |= (1u<<DBTN_USE); doomApplyMasks(); return; }   // hit area diperbesar (gambar tetap 16)", 'use')
open(f, 'w', encoding='utf-8').write(s)
print("OK: latch 90 ms + hit area Fire/Use diperbesar")
