#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_doom_melt_patch.py -- DOOM_MELT_V1
Ganti animasi masuk Doom (ring "Menyiapkan mode game...") dengan efek MELT
ala Doom asli: splash DOOM merah-oranye meleleh turun per kolom, mengungkap
layar "Memuat...". Digambar core 0 SEBELUM task Doom dibuat (aman dari race).
Syarat: apply_doom_race_patch.py sudah terpasang.
Pakai (Termux, folder repo):  python apply_doom_melt_patch.py
"""
import os, shutil, sys
MARK = "DOOM_MELT_V1"
FN = "doom_renphone.ino"

FUNC = r'''// DOOM_MELT_V1: efek melt ala Doom asli. Dipanggil dari doomEnter() SEBELUM task dibuat
// (core 0 masih boleh menyentuh display). Gagal alokasi -> lewati animasi, tampil "Memuat..." biasa.
static void doomMeltIntro(){
  const int W = SCR_W, H = SCR_H, CW = 4;
  const int N = (W + CW - 1) / CW;
  LGFX_Sprite src(&display);
  src.setPsram(true);
  src.setColorDepth(16);
  int16_t* ys = (int16_t*)heap_caps_malloc(N * sizeof(int16_t), MALLOC_CAP_8BIT);
  if(!ys || !src.createSprite(W, H)){
    if(ys) heap_caps_free(ys);
    drawDoom(canvas); push();
    return;
  }
  // splash yang akan meleleh: gradasi merah -> gelap + tulisan DOOM
  for(int y = 0; y < H; y++){
    int r = 40 + (170 * (H - y)) / H;
    src.drawFastHLine(0, y, W, (uint16_t)((r >> 3) << 11));
  }
  src.setFont(&lgfx::fonts::Font0);
  src.setTextWrap(false);
  const int ts = (W >= 400) ? 10 : 7;
  src.setTextSize(ts);
  const int tx = (W - 4 * 6 * ts) / 2, ty = (H - 8 * ts) / 2 - 10;
  src.setTextColor(0x2000); src.setCursor(tx + ts / 2 + 2, ty + ts / 2 + 2); src.print("DOOM");
  src.setTextColor(0xFD20); src.setCursor(tx, ty);                          src.print("DOOM");

  // offset awal tiap kolom acak seperti wipe Doom (naik-turun max 1 per kolom)
  ys[0] = -(int)(esp_random() % 8);
  for(int i = 1; i < N; i++){
    int v = ys[i - 1] + (int)(esp_random() % 3) - 1;
    if(v > 0) v = 0;
    if(v < -8) v = -8;
    ys[i] = (int16_t)v;
  }
  const int step = max(8, H / 14);

  drawDoom(canvas);                       // latar yang terungkap = layar "Memuat..."
  src.pushSprite(&canvas, 0, 0); push();
  delay(220);                             // tahan splash sebentar sebelum meleleh

  for(int f = 0; f < 80; f++){
    bool alive = false;
    for(int i = 0; i < N; i++){
      if(ys[i] < 0){ ys[i]++; alive = true; }
      else if(ys[i] < H){ ys[i] += step; if(ys[i] > H) ys[i] = H; alive = true; }
    }
    drawDoom(canvas);
    for(int i = 0; i < N; i++){
      int y = ys[i];
      if(y >= H) continue;
      if(y < 0) y = 0;
      canvas.setClipRect(i * CW, 0, CW, H);
      src.pushSprite(&canvas, 0, y);
    }
    canvas.clearClipRect();
    push();
    if(!alive) break;
  }
  heap_caps_free(ys);
  src.deleteSprite();
  drawDoom(canvas); push();
}

'''

REPS = [
 ("void doomEnter(){\n  doomErrorMsg = \"\";\n", FUNC + "void doomEnter(){\n  doomErrorMsg = \"\";\n"),
 ("  enterGameMode(); // boost CPU_MHZ_GAME, sama spt Snake/Inferno/dll\n",
  "  // DOOM_MELT_V1: boost CPU sama spt enterGameMode(), tapi TANPA ring booster -> diganti efek melt.\n"
  "  if(!gameModeActive){ gameModeActive = true; setCpuFrequencyMhz(CPU_MHZ_GAME); }\n"
  "  doomMeltIntro();\n"),
]

def main():
    if not os.path.isfile(FN):
        print("GAGAL: %s tidak ada (jalankan dari folder repo)" % FN); sys.exit(1)
    txt = open(FN, encoding="utf-8", newline="").read()
    if MARK in txt:
        print("sudah dipatch (%s), dilewati." % MARK); return
    if "DOOM_RACE_FIX_V1" not in txt:
        print("GAGAL: pasang apply_doom_race_patch.py dulu."); sys.exit(1)
    crlf = "\r\n" in txt
    for old, new in REPS:
        if crlf: old = old.replace("\n", "\r\n"); new = new.replace("\n", "\r\n")
        n = txt.count(old)
        if n != 1:
            print("GAGAL: potongan kode ditemukan %d kali (harus 1): %s" % (n, old.strip().splitlines()[0][:80]))
            print("Tidak ada file yang diubah."); sys.exit(1)
        txt = txt.replace(old, new, 1)
    shutil.copyfile(FN, FN + ".bak-doommelt")
    open(FN, "w", encoding="utf-8", newline="").write(txt)
    print("OK: %s dipatch (backup: %s.bak-doommelt)" % (FN, FN))

if __name__ == "__main__":
    main()
