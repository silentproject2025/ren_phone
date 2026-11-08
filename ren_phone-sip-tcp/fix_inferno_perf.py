#!/usr/bin/env python3
# Inferno: pindahkan hitungan yang sama untuk SEMUA kolom (millis, sinf kedip obor, kilatan, pulsa dinding)
# ke luar loop 320 kolom. Hasil visual identik. Jalankan dari folder repo:  python3 fix_inferno_perf.py
import io, sys
F="phone.ino"
s=io.open(F,encoding="utf-8",errors="surrogateescape",newline="").read()
if "v128: inferno perf" in s: print("sudah diterapkan"); sys.exit(0)

def rep(old,new):
    global s
    if s.count(old)!=1: print("pola tidak unik/ketemu (%d): %s"%(s.count(old),old[:60])); sys.exit(1)
    s=s.replace(old,new)

# 1) konstanta per-frame sebelum loop kolom
rep("  // ---- Dinding: 1 sinar per kolom layar, algoritma DDA (tanpa sqrt) ----\n  for(int x=0;x<SCR_W;x++){\n",
"""  // ---- Dinding: 1 sinar per kolom layar, algoritma DDA (tanpa sqrt) ----
  // v128: inferno perf -- nilai yg SAMA untuk semua kolom dihitung SEKALI per frame (dulu per kolom:
  // millis() berkali-kali + sinf kedip obor s/d 4x per kolom).
  const unsigned long nowMs=millis();
  float torchFlick[INF_TORCH_COUNT];
  for(int ti=0;ti<INF_TORCH_COUNT;ti++) torchFlick[ti]=0.85f+0.15f*sinf(nowMs*0.006f+ti*2.1f);
  const bool flashOn = nowMs<infMuzzleFlashUntil;
  float flashT0 = 0.0f;
  if(flashOn){ flashT0=(float)(infMuzzleFlashUntil-nowMs)/90.0f; if(flashT0>1.0f) flashT0=1.0f; }
  int pcMX=-9999, pcMY=-9999; float pulse4=0.0f, pulseD=0.0f;   // cache pulsa dinding per tile
  for(int x=0;x<SCR_W;x++){
""")
# 2) pulsa dinding '4' dan 'D' dari cache (hanya dihitung saat tile berubah)
rep("    uint16_t baseCol;\n    switch(hitTile){\n",
"""    uint16_t baseCol;
    if((hitTile=='4'||hitTile=='D') && (mapX!=pcMX||mapY!=pcMY)){
      pcMX=mapX; pcMY=mapY;
      pulse4=0.5f+0.5f*sinf(nowMs*0.004f+mapX*0.7f+mapY*0.5f);
      pulseD=0.5f+0.5f*sinf(nowMs*0.003f+mapX*0.9f+mapY*0.6f);
    }
    switch(hitTile){
""")
rep("        float pulse=0.5f+0.5f*sinf(millis()*0.004f+mapX*0.7f+mapY*0.5f);\n        baseCol=blend565(INF_WALL_FLESH,0xF800,(uint8_t)(pulse*45.0f));",
    "        float pulse=pulse4;\n        baseCol=blend565(INF_WALL_FLESH,0xF800,(uint8_t)(pulse*45.0f));")
rep("        float pulse=0.5f+0.5f*sinf(millis()*0.003f+mapX*0.9f+mapY*0.6f);\n        baseCol=blend565(INF_WALL_DOOR,0xFFFF,(uint8_t)(pulse*60.0f));",
    "        float pulse=pulseD;\n        baseCol=blend565(INF_WALL_DOOR,0xFFFF,(uint8_t)(pulse*60.0f));")
# 3) kedip obor
rep("      float flick=0.85f+0.15f*sinf(millis()*0.006f+ti*2.1f); // tiap obor beda fase, gak kompak berkedip\n",
    "      float flick=torchFlick[ti]; // tiap obor beda fase, gak kompak berkedip (dihitung 1x per frame, lihat atas)\n")
# 4) kilatan tembakan
rep("    if(millis()<infMuzzleFlashUntil){\n      float flashT=(float)(infMuzzleFlashUntil-millis())/90.0f;\n      if(flashT>1.0f) flashT=1.0f;\n",
    "    if(flashOn){\n      float flashT=flashT0;\n")
io.open(F,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)
print("OK: Inferno -- hitungan per-frame dipindah keluar loop kolom")
