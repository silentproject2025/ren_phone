// =============================================
// v97 — NyxPPU ENGINE (fase 1) — "biar makin berasa HP beneran"
// =============================================
// Permintaan user: nambah "engine" ke NyxOS (dicontohkan "PPA" -- setelah
// diklarifikasi maksudnya PPU, "Pixel Processing Unit", ala konsol game
// lama), yang nantinya dipakai skala penuh (game + Home/Lock Screen).
//
// KENAPA DIPISAH JADI FASE 1 (bukan langsung rombak Inferno/Home/Lock
// Screen sekaligus): phone.ino sendiri sudah >16000 baris & TIDAK ada
// cara buat kompilasi/tes beneran di board fisik dari sisi saya (Claude).
// Merombak jeroan Inferno (raycaster, billboard sprite berbasis
// fillTriangle/fillCircle) atau Home/Lock Screen (glassmorphism, transisi)
// sekaligus, tanpa bisa dites, resikonya app yang SUDAH JALAN jadi
// rusak. Jadi fase 1 ini: engine-nya jadi NYATA & BISA DICOBA LANGSUNG
// lewat app baru "PPU Demo" (mandiri, gak nyentuh app lain sama sekali).
// Begitu dites di HP fisik & oke, fase 2 tinggal "colok" NyxPPU ke
// Inferno (ganti sprite musuh/item) & Home Screen (ikon/partikel wallpaper)
// -- API-nya memang didesain generik biar gampang dipakai ulang di app
// manapun, bukan cuma demo ini.
//
// KONSEP (niru arsitektur PPU konsol asli, disederhanakan buat ESP32-S3):
//  - Tile bank   : kumpulan "sprite sheet" kecil 8x8px RGB565 di PSRAM.
//  - 2 layer BG  : grid tile (nametable) MASING2 punya scroll X/Y sendiri
//                  -> efek parallax (BG0 di belakang, BG1 di depan, tile
//                  id 0 di BG1 dianggap transparan/lubang biar sprite
//                  kelihatan "nembus" -- lihat jawaban user soal Layering).
//  - OAM sprite  : sampai NYX_PPU_MAX_SPRITES objek independen (posisi
//                  bebas, bukan grid), tiap sprite bisa scale/flip/tint
//                  & pilih tampil DI BELAKANG atau DI DEPAN layer BG1
//                  (priority bit, sama kayak PPU asli).
//  - Semua buffer besar (tile bank & 2 nametable) dialokasikan PSRAM lewat
//    heap_caps_malloc(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) -- pola PERSIS yg
//    sudah dipakai di file ini utk mjpegBuf/stack Gemini, SENGAJA supaya
//    engine ini TIDAK ikut menggerogoti RAM internal 162KB yg udah ketat
//    (ini akar banyak bug HTTP -1 & reboot yg sudah pernah dicatat).
//
// PENTING soal signature fungsi di file ini: SEMUA fungsi publik NyxPPU
// cuma menerima tipe primitif (int/bool/uint8_t/uint16_t) + LGFX_Sprite&
// (tipe library yg sudah dikenal arduino-cli lebih awal), TIDAK ADA yang
// menerima struct custom (NyxSprite dsb) sebagai parameter. Ini SENGAJA
// buat menghindari bug prototype arduino-cli yang sama persis kayak
// kasus MazeGhost/InfEnemy/showToast yang sudah pernah kejadian & dicatat
// di file ini -- struct NyxSprite di bawah HANYA dipakai sbg penyimpanan
// internal (array static), tidak pernah lewat parameter fungsi.
// =============================================
//
// v98 — NyxPPU v2: "biar kayak GPU" (render dioptimasi + efek flagship)
// -------------------------------------------------------------------
// Permintaan user: PPU ini emang niatnya buat pixel rendering grafis --
// diminta didongkrak biar HASIL-nya smooth/halus & ada efek "mewah" ala
// HP flagship. CATATAN JUJUR dulu: ESP32-S3 TIDAK punya GPU/rasterizer
// fisik -- semua di bawah ini tetap software rendering di CPU. Yang bisa
// (& sudah) dilakukan: optimasi renderer-nya habis-habisan biar TERASA
// kayak ada akselerasi grafis, ditambah toolkit efek visual generik.
//
// APA YANG BERUBAH (nyxPpuRender & fungsi internal di bawahnya):
//  - SEBELUM: BG0 & BG1 masing2 loop piksel-per-piksel penuh 1 viewport
//    (~320x218 = ~70rb piksel/layer/frame) manggil s.drawPixel() SATU-
//    SATU (tiap panggilan ada overhead dispatch+clipping), plus modulo
//    (%) dihitung ULANG tiap piksel buat wrap scroll. Total bisa >100rb
//    panggilan drawPixel per frame cuma buat BG, belum sprite.
//  - SESUDAH: diubah jadi "scanline compositor" -- niru gaya rendering
//    PPU/GPU sungguhan (baris demi baris): tiap baris disusun dulu di
//    buffer lokal (nyxLineBuf, array biasa, nulis piksel ke situ SUPER
//    murah drpd panggil fungsi library), BARU 1x s.pushImage() per baris
//    buat nge-blit seluruh baris sekaligus. Pola pushImage() 1-baris ini
//    BUKAN eksperimen baru -- PERSIS pola yg sudah dipakai & terbukti
//    jalan cepat di app NES (lihat nesVideo_begin()/ppu_scanline_blit()
//    di nes_video_renphone.ino), jadi resikonya rendah krn sudah ada
//    presedennya di codebase yg sama. Modulo scroll jg dipindah dari
//    "per piksel" jadi "per baris" (col/tx tinggal di-increment, bukan
//    dihitung ulang tiap piksel) -- bikin BG loop jauh lebih ringan bwt
//    CPU. Urutan compose (BG0 -> sprite belakang -> BG1 berlubang ->
//    sprite depan) SENGAJA dipertahankan SAMA PERSIS spt versi lama biar
//    hasil visual identik, cuma jalurnya yg dipercepat -- resiko app yg
//    sudah jalan jadi "beda tampilan" harusnya minim.
//  - BARU (opsional, gak wajib dipakai): nyxPpuSpriteGlow() (halo lembut
//    di sekitar sprite, kesan "glow" ala UI flagship modern -- didemoin
//    di app PPU Demo), nyxPpuEaseOutCubic() (kurva easing buat animasi/
//    transisi yg halus, generik, siap dipakai app/layar manapun -- bukan
//    cuma PPU), nyxPpuFpsTick() (FPS meter reusable, ditampilkan di app
//    PPU Demo). FPS meter ini PENTING krn saya (Claude) gak punya cara
//    compile+jalanin ini di board fisik -- jadi validasi speedup-nya
//    beneran kerasa atau nggak, TETAP perlu dites langsung di HP.
// =============================================

#include <esp_heap_caps.h>

#define NYX_PPU_TILE       8      // ukuran 1 tile: 8x8 px (standar gaya PPU retro)
#define NYX_PPU_MAX_TILES  40     // bank tile (40 * 8*8*2 byte = 5120 byte PSRAM)
#define NYX_PPU_LAYER_W    48     // lebar nametable per layer (tile) -> 384px virtual
#define NYX_PPU_LAYER_H    36     // tinggi nametable per layer (tile) -> 288px virtual
#define NYX_PPU_MAX_SPRITES 32
#define NYX_PPU_TRANSPARENT 0xF81F // magenta -- key transparansi utk tile SPRITE (bukan BG)

struct NyxSprite {
  int16_t x, y;
  uint8_t tileId;
  uint8_t scale;      // 1..4 (zoom bulat, niru "size" register PPU asli)
  bool flipX, flipY;
  bool priorityFront; // true = digambar DI DEPAN layer BG1 (default), false = di belakang BG1
  bool visible;
  bool hasTint;
  uint16_t tint;
  uint8_t tintAmt;    // 0..255, seberapa kuat tint dicampur (lihat blend565 di phone.ino)
  bool hasGlow;        // v98: halo lembut opsional di sekeliling sprite (efek "flagship")
  uint16_t glowColor;
};

// v98: buffer 1 baris layar, dipakai ulang tiap panggilan nyxPpuRender
// (bukan dialokasikan tiap frame) -- ini yg ditembak ke sprite tujuan
// lewat 1x pushImage() per baris, gantiin drawPixel per piksel.
#define NYX_PPU_LINEBUF_MAX 480
static uint16_t nyxLineBuf[NYX_PPU_LINEBUF_MAX];

static uint16_t* nyxPpuTiles = nullptr;               // [NYX_PPU_MAX_TILES * TILE*TILE]
static uint8_t*  nyxPpuLayerTileId[2] = {nullptr,nullptr}; // [layer][LAYER_W*LAYER_H]
static uint8_t*  nyxPpuLayerAttr[2]   = {nullptr,nullptr}; // bit0=flipX bit1=flipY
static int nyxPpuTileCount = 0;
static int nyxPpuScrollX[2] = {0,0};
static int nyxPpuScrollY[2] = {0,0};
static NyxSprite nyxPpuSprites[NYX_PPU_MAX_SPRITES];
static bool nyxPpuReady = false;

// ---- init / alokasi PSRAM ----
void nyxPpuInit(){
  if(nyxPpuReady) return; // idempotent -- aman dipanggil tiap kali app enter
  size_t tileBytes  = (size_t)NYX_PPU_MAX_TILES * NYX_PPU_TILE * NYX_PPU_TILE * sizeof(uint16_t);
  size_t layerBytes = (size_t)NYX_PPU_LAYER_W * NYX_PPU_LAYER_H;
  nyxPpuTiles = (uint16_t*)heap_caps_malloc(tileBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  for(int L=0; L<2; L++){
    nyxPpuLayerTileId[L] = (uint8_t*)heap_caps_malloc(layerBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    nyxPpuLayerAttr[L]   = (uint8_t*)heap_caps_malloc(layerBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(nyxPpuLayerTileId[L]) memset(nyxPpuLayerTileId[L], 0, layerBytes);
    if(nyxPpuLayerAttr[L])   memset(nyxPpuLayerAttr[L], 0, layerBytes);
  }
  nyxPpuTileCount = 0;
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++){ nyxPpuSprites[i].visible=false; nyxPpuSprites[i].scale=1; nyxPpuSprites[i].hasGlow=false; }
  // Kalau PSRAM gagal (harusnya gak pernah di board ini), tandai TIDAK ready
  // supaya nyxPpuRender() jadi no-op aman drpd crash null-pointer.
  nyxPpuReady = nyxPpuTiles && nyxPpuLayerTileId[0] && nyxPpuLayerTileId[1] &&
                nyxPpuLayerAttr[0] && nyxPpuLayerAttr[1];
}

// ---- tile bank: generator prosedural (belum ada pipeline aset gambar
// eksternal di proyek ini, jadi tile dibikin lewat kode -- pola SAMA
// spirit-nya dgn drawGlassPanel/drawGlassCircle yg juga generatif) ----
int nyxPpuGenPatternTile(uint16_t colorA, uint16_t colorB, uint8_t pattern){
  if(!nyxPpuReady || nyxPpuTileCount>=NYX_PPU_MAX_TILES) return 0;
  int id = nyxPpuTileCount++;
  uint16_t* px = nyxPpuTiles + (size_t)id*NYX_PPU_TILE*NYX_PPU_TILE;
  for(int ty=0; ty<NYX_PPU_TILE; ty++){
    for(int tx=0; tx<NYX_PPU_TILE; tx++){
      bool useA;
      switch(pattern){
        case 1: useA = ((tx==3||tx==4)&&(ty==3||ty==4)); break;           // dot tengah
        case 2: useA = ((tx+ty)%4==0); break;                             // diagonal jarang
        case 3: useA = (ty%4==0) || (tx==((ty/4)%2==0?0:4)); break;       // bata
        default: useA = ((tx/4 + ty/4)%2==0); break;                      // checker (pattern 0)
      }
      px[ty*NYX_PPU_TILE+tx] = useA ? colorA : colorB;
    }
  }
  return id;
}

// tile bulat transparan (blob karakter sederhana) -- dipakai sprite demo,
// tapi generik: cocok jadi "placeholder" karakter apapun sebelum ada aset
// bitmap asli.
int nyxPpuGenBlobTile(uint16_t bodyColor, uint16_t outlineColor){
  if(!nyxPpuReady || nyxPpuTileCount>=NYX_PPU_MAX_TILES) return 0;
  int id = nyxPpuTileCount++;
  uint16_t* px = nyxPpuTiles + (size_t)id*NYX_PPU_TILE*NYX_PPU_TILE;
  const float cx=3.5f, cy=3.5f, r=3.4f;
  for(int ty=0; ty<NYX_PPU_TILE; ty++){
    for(int tx=0; tx<NYX_PPU_TILE; tx++){
      float d = sqrtf((tx-cx)*(tx-cx)+(ty-cy)*(ty-cy));
      uint16_t c;
      if(d>r) c = NYX_PPU_TRANSPARENT;
      else if(d>r-1.0f) c = outlineColor;
      else c = bodyColor;
      px[ty*NYX_PPU_TILE+tx] = c;
    }
  }
  return id;
}

void nyxPpuResetTiles(){ nyxPpuTileCount = 0; }

// ---- background layer (nametable) ----
void nyxPpuSetLayerTile(int layer, int col, int row, int tileId, bool flipX, bool flipY){
  if(!nyxPpuReady || layer<0 || layer>1) return;
  col = ((col % NYX_PPU_LAYER_W) + NYX_PPU_LAYER_W) % NYX_PPU_LAYER_W;
  row = ((row % NYX_PPU_LAYER_H) + NYX_PPU_LAYER_H) % NYX_PPU_LAYER_H;
  int idx = row*NYX_PPU_LAYER_W+col;
  nyxPpuLayerTileId[layer][idx] = (uint8_t)tileId;
  nyxPpuLayerAttr[layer][idx]   = (flipX?1:0) | (flipY?2:0);
}
void nyxPpuFillLayer(int layer, int tileId){
  if(!nyxPpuReady || layer<0 || layer>1) return;
  memset(nyxPpuLayerTileId[layer], (uint8_t)tileId, (size_t)NYX_PPU_LAYER_W*NYX_PPU_LAYER_H);
  memset(nyxPpuLayerAttr[layer], 0, (size_t)NYX_PPU_LAYER_W*NYX_PPU_LAYER_H);
}
void nyxPpuSetScroll(int layer, int x, int y){
  if(layer<0||layer>1) return;
  nyxPpuScrollX[layer]=x; nyxPpuScrollY[layer]=y;
}
void nyxPpuScrollBy(int layer, int dx, int dy){
  if(layer<0||layer>1) return;
  nyxPpuScrollX[layer]+=dx; nyxPpuScrollY[layer]+=dy;
}

// ---- sprite (OAM) ----
void nyxPpuSpriteSet(int idx, int x, int y, int tileId, uint8_t scale, bool flipX, bool flipY, bool priorityFront, bool visible){
  if(idx<0||idx>=NYX_PPU_MAX_SPRITES) return;
  NyxSprite& sp = nyxPpuSprites[idx];
  sp.x=(int16_t)x; sp.y=(int16_t)y; sp.tileId=(uint8_t)tileId;
  sp.scale = scale<1?1:(scale>4?4:scale);
  sp.flipX=flipX; sp.flipY=flipY; sp.priorityFront=priorityFront; sp.visible=visible;
}
void nyxPpuSpriteMove(int idx, int x, int y){
  if(idx<0||idx>=NYX_PPU_MAX_SPRITES) return;
  nyxPpuSprites[idx].x=(int16_t)x; nyxPpuSprites[idx].y=(int16_t)y;
}
void nyxPpuSpriteTint(int idx, uint16_t tintColor, uint8_t amount){
  if(idx<0||idx>=NYX_PPU_MAX_SPRITES) return;
  nyxPpuSprites[idx].hasTint=true; nyxPpuSprites[idx].tint=tintColor; nyxPpuSprites[idx].tintAmt=amount;
}
// v98: nyalain halo lembut di sekeliling sprite (opsional, murah) -- niru
// kesan "glow"/bloom yg sering dipakai UI HP flagship modern. Beda dari
// tint (yg ngubah warna badan sprite-nya sendiri), glow nambah cahaya
// lembut DI LUAR badan sprite, dihitung bareng proses compositing baris
// di nyxPpuCompositeSpritesRow() -- jadi gak nambah pass render terpisah.
void nyxPpuSpriteGlow(int idx, uint16_t glowColor){
  if(idx<0||idx>=NYX_PPU_MAX_SPRITES) return;
  nyxPpuSprites[idx].hasGlow=true; nyxPpuSprites[idx].glowColor=glowColor;
}
void nyxPpuSpriteHide(int idx){ if(idx>=0&&idx<NYX_PPU_MAX_SPRITES) nyxPpuSprites[idx].visible=false; }
int  nyxPpuSpriteFindFree(){
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++) if(!nyxPpuSprites[i].visible) return i;
  return -1;
}

// ---- render internals (v98: scanline compositor, lihat catatan besar
// di header file ini soal KENAPA diubah dari drawPixel per piksel) ----
static void nyxPpuBlitTilePixel(int srcTx, int srcTy, int tileId, bool flipX, bool flipY, uint16_t& outColor){
  int sx = flipX ? (NYX_PPU_TILE-1-srcTx) : srcTx;
  int sy = flipY ? (NYX_PPU_TILE-1-srcTy) : srcTy;
  outColor = nyxPpuTiles[(size_t)tileId*NYX_PPU_TILE*NYX_PPU_TILE + sy*NYX_PPU_TILE + sx];
}

// Isi SATU baris (py, lokal ke viewport) dari 1 layer BG ke lineBuf.
// overlay=false -> tulis semua piksel di baris ini (dipakai BG0, opaque).
// overlay=true  -> cuma tulis piksel yg tileId-nya != 0, sisanya DIBIARIN
//                  (dipakai BG1 -- "lubang" transparan biar yg di bawahnya
//                  kelihatan, sama spt versi lama, cuma sekarang gak perlu
//                  skip-lewat-continue di s.drawPixel krn kita nulis ke
//                  array lokal).
// Beda penting dr versi lama: col/tx TIDAK dihitung ulang pakai % tiap
// piksel -- cuma di-increment, wrap dicek pakai perbandingan biasa. Modulo
// (%) cuma jalan 1x per BARIS (buat wy/wx0), bukan 1x per PIKSEL kayak
// sebelumnya -- ini beda ~320x jumlah operasi modulo per frame per layer.
static void nyxPpuFillBgRow(uint16_t* lineBuf, int layer, int py, int vw, bool overlay){
  // NB: sengaja gak pakai nama variabel "T" (walau singkat & enak) --
  // ada fungsi global T() (tema) di phone.ino, gak mau ambigu/shadow.
  const int LW = NYX_PPU_LAYER_W, LH = NYX_PPU_LAYER_H, TS = NYX_PPU_TILE;
  int wy = ((py + nyxPpuScrollY[layer]) % (LH*TS) + (LH*TS)) % (LH*TS);
  int row = wy / TS, ty = wy % TS;
  int wx0 = (nyxPpuScrollX[layer] % (LW*TS) + (LW*TS)) % (LW*TS);
  int col = wx0 / TS, tx = wx0 % TS;
  for(int px=0; px<vw; px++){
    int cellIdx = row*LW+col;
    uint8_t tileId = nyxPpuLayerTileId[layer][cellIdx];
    if(!(overlay && tileId==0)){
      uint8_t attr = nyxPpuLayerAttr[layer][cellIdx];
      uint16_t c; nyxPpuBlitTilePixel(tx,ty,tileId,attr&1,attr&2,c);
      lineBuf[px] = c;
    }
    tx++;
    if(tx==TS){ tx=0; col++; if(col==LW) col=0; }
  }
}

// Komposit sprite (satu prioritas, belakang ATAU depan) ke lineBuf utk
// baris py. Glow (kalau ada) digambar lebih dulu sbg halo lembut, BARU
// badan sprite di-timpa di atasnya -- biar halo gak nutupin sprite-nya
// sendiri. CATATAN: glow dihitung pakai jarak kotak (bukan lingkaran
// sungguhan) biar murah -- utk sprite kotak biasa hasilnya rapi, utk
// sprite bulat (blob) halo di pojok agak kurang presisi, tapi cukup buat
// demo & tetap murah di CPU.
// v99: dulu fungsi ini scan SEMUA NYX_PPU_MAX_SPRITES slot tiap baris x 2
// pass/frame (cek visible+priorityFront), padahal biasanya cuma segelintir
// yg beneran visible -- di 32 slot x 218 baris x 2 pass = ~14rb iterasi
// "cek doang" per frame, SEBELUM sempat gambar apa2. Sekarang dikasih
// idxList (indeks sprite yg visible & prioritasnya cocok, dibangun 1x per
// frame di nyxPpuRender, bukan di-scan ulang tiap baris) -- loop di bawah
// jadi cuma sepanjang jumlah sprite yg BENERAN aktif.
static void nyxPpuCompositeSpritesRow(uint16_t* lineBuf, int vw, int py, const uint8_t* idxList, int idxCount){
  const int GLOW_PAD = 4;
  for(int ii=0; ii<idxCount; ii++){
    NyxSprite& sp = nyxPpuSprites[idxList[ii]];
    int size = NYX_PPU_TILE * sp.scale;
    int pad = sp.hasGlow ? GLOW_PAD : 0;
    if(py < sp.y-pad || py >= sp.y+size+pad) continue;      // culling vertikal (+halo)
    if(sp.x+size+pad<0 || sp.x-pad>=vw) continue;            // culling horizontal (+halo)

    if(sp.hasGlow){
      int vdist = (py<sp.y) ? (sp.y-py) : (py>=sp.y+size ? (py-(sp.y+size)+1) : 0);
      for(int gx=-pad; gx<size+pad; gx++){
        int sx2 = sp.x+gx; if(sx2<0||sx2>=vw) continue;
        int hdist = (gx<0) ? -gx : (gx>=size ? (gx-size+1) : 0);
        int d = max(hdist, vdist);
        if(d<1 || d>pad) continue; // d==0 = area badan sprite, dilewati; sisanya cincin halo
        uint8_t alpha = (uint8_t)(80 - (d-1)*22); if(alpha<12) alpha=12;
        lineBuf[sx2] = blend565(lineBuf[sx2], sp.glowColor, alpha);
      }
    }

    if(py < sp.y || py >= sp.y+size) continue; // di luar badan -> cuma halo di atas, sprite lain lanjut
    int srcTy = ((py-sp.y)*NYX_PPU_TILE)/size;
    for(int px2=0; px2<size; px2++){
      int sx2 = sp.x+px2; if(sx2<0||sx2>=vw) continue;
      int srcTx = (px2*NYX_PPU_TILE)/size;
      uint16_t c; nyxPpuBlitTilePixel(srcTx,srcTy,sp.tileId,sp.flipX,sp.flipY,c);
      if(c==NYX_PPU_TRANSPARENT) continue;
      if(sp.hasTint) c = blend565(c, sp.tint, sp.tintAmt);
      lineBuf[sx2] = c;
    }
  }
}

// Panggilan utama: 1x per frame. Urutan compose (niru prioritas PPU asli,
// DIPERTAHANKAN SAMA PERSIS spt versi lama biar visual identik):
// BG0 -> sprite(prioritas belakang) -> BG1(berlubang di tileId 0) -> sprite(prioritas depan, default)
// Bedanya: sekarang disusun per BARIS ke nyxLineBuf dulu, baru di-blit
// SEKALIGUS lewat 1x pushImage() -- pola yg sama persis dgn yg sudah
// terbukti jalan di ppu_scanline_blit() app NES (nes_video_renphone.ino).
// v99: profiling -- micros() dibungkus di sekitar isi fungsi ini (bukan
// nyxPpuFillBgRow/nyxPpuCompositeSpritesRow sendiri2, biar overhead
// pemanggilan micros() itu sendiri gak ganggu ukuran) supaya kelihatan
// beneran berapa lama COMPOSITOR (BG+sprite, murni CPU) makan waktu per
// frame, terpisah dari waktu total drawPpuApp() (yg juga ada fillSprite,
// status bar, teks). Kalau nyxPpuLastRenderUs << (1000000/fps total),
// artinya bottleneck ADA DI LUAR compositor ini (misal blit s ke layar
// fisik di tempat lain / phone.ino) -- bukan di PPU-nya.
static unsigned long nyxPpuLastRenderUs = 0;

void nyxPpuRender(LGFX_Sprite& s, int vx, int vy, int vw, int vh){
  if(!nyxPpuReady) return;
  if(vw>NYX_PPU_LINEBUF_MAX) vw=NYX_PPU_LINEBUF_MAX; // jaga2 batas buffer statis
  unsigned long t0 = micros();

  // Bangun daftar indeks sprite yg visible SEKALI per frame (bukan per
  // baris) -- lihat catatan v99 di nyxPpuCompositeSpritesRow soal kenapa.
  static uint8_t backIdx[NYX_PPU_MAX_SPRITES];
  static uint8_t frontIdx[NYX_PPU_MAX_SPRITES];
  int backCount=0, frontCount=0;
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++){
    if(!nyxPpuSprites[i].visible) continue;
    if(nyxPpuSprites[i].priorityFront) frontIdx[frontCount++] = (uint8_t)i;
    else backIdx[backCount++] = (uint8_t)i;
  }

  for(int py=0; py<vh; py++){
    nyxPpuFillBgRow(nyxLineBuf, 0, py, vw, false);
    nyxPpuCompositeSpritesRow(nyxLineBuf, vw, py, backIdx, backCount);
    nyxPpuFillBgRow(nyxLineBuf, 1, py, vw, true);
    nyxPpuCompositeSpritesRow(nyxLineBuf, vw, py, frontIdx, frontCount);
    s.pushImage(vx, vy+py, vw, 1, nyxLineBuf);
  }

  nyxPpuLastRenderUs = micros() - t0;
}

// v99: getter buat profiling di layar (drawPpuApp) -- lihat catatan di
// nyxPpuRender soal cara baca angkanya.
unsigned long nyxPpuGetLastRenderUs(){ return nyxPpuLastRenderUs; }

// v98: kurva easing generik (ease-out cubic) -- buat animasi/transisi yg
// mulai cepat lalu "mendarat" halus, kesan gerakan lebih "mewah" drpd
// linear rata. Murni matematika float, gak nyentuh render sama sekali --
// bebas dipakai app/layar manapun (parameter t: 0..1, hasil jg 0..1).
float nyxPpuEaseOutCubic(float t){
  if(t<0) t=0; else if(t>1) t=1;
  float u = 1.0f-t;
  return 1.0f - u*u*u;
}

// v98: FPS meter generik (exponential moving average biar angkanya gak
// lompat2 tiap frame). Panggil 1x per frame (mis. di awal fungsi draw
// app), nilai baliknya FPS ter-halusin. Dipakai di PPU Demo di bawah
// buat ngukur beneran seberapa ngaruh optimasi scanline compositor di
// atas -- saya (Claude) gak bisa compile+jalanin di board fisik, jadi
// angka ini yg jadi bukti nyata di HP-nya, bukan cuma klaim di komentar.
static float nyxPpuFpsValue = 0;
static unsigned long nyxPpuFpsLastMs = 0;
float nyxPpuFpsTick(){
  unsigned long now = millis();
  if(nyxPpuFpsLastMs!=0){
    unsigned long dt = now-nyxPpuFpsLastMs;
    if(dt>0){
      float inst = 1000.0f/dt;
      nyxPpuFpsValue = (nyxPpuFpsValue<=0.01f) ? inst : (nyxPpuFpsValue*0.9f + inst*0.1f);
    }
  }
  nyxPpuFpsLastMs = now;
  return nyxPpuFpsValue;
}

// =============================================
// APP DEMO: "PPU Demo" -- bukti engine beneran jalan & bisa disentuh
// langsung (bukan cuma library nganggur). Ketuk layar buat nambah sprite
// baru (sampai penuh), tombol Back keluar seperti app lain.
// =============================================
#define PPU_DEMO_MAX_ENT 20
static float ppuEntX[PPU_DEMO_MAX_ENT], ppuEntY[PPU_DEMO_MAX_ENT];
static float ppuEntVX[PPU_DEMO_MAX_ENT], ppuEntVY[PPU_DEMO_MAX_ENT];
static int   ppuEntCount = 0;
static int   ppuTileSky=0, ppuTileCloud=0, ppuTileBlob=0;
static unsigned long ppuLastTick=0;

static void ppuSpawnEntity(int x, int y){
  if(ppuEntCount>=PPU_DEMO_MAX_ENT) return;
  int i = ppuEntCount++;
  ppuEntX[i]=x; ppuEntY[i]=y;
  ppuEntVX[i] = ((random(0,2)?1:-1) * (0.6f + random(0,20)/10.0f));
  ppuEntVY[i] = ((random(0,2)?1:-1) * (0.6f + random(0,20)/10.0f));
  int slot = nyxPpuSpriteFindFree();
  if(slot>=0){
    static const uint16_t tintPool[6] = {0xF800,0x07E0,0x001F,0xFFE0,0xF81F,0x07FF};
    nyxPpuSpriteSet(slot, x, y, ppuTileBlob, 2, false,false, true, true);
    nyxPpuSpriteTint(slot, tintPool[i%6], 160);
    // v98: tiap entitas ke-3 dikasih glow -- contoh pemakaian efek baru,
    // sengaja gak SEMUA sprite biar keliatan "aksen", bukan norak/berat.
    if(i%3==0) nyxPpuSpriteGlow(slot, tintPool[i%6]);
  }
}

void ppuAppEnter(){
  enterGameMode();
  nyxPpuInit();
  nyxPpuResetTiles();
  ppuTileSky   = nyxPpuGenPatternTile(0x10A2, 0x0841, 2);   // langit gelap, motif diagonal jarang
  ppuTileCloud = nyxPpuGenPatternTile(0x39C7, 0x10A2, 1);   // "awan" -- dipakai di BG1 (id!=0)
  ppuTileBlob  = nyxPpuGenBlobTile(0xFFFF, 0x2104);
  nyxPpuFillLayer(0, ppuTileSky);
  nyxPpuFillLayer(1, 0); // BG1 default kosong/transparan semua
  // taburi beberapa tile awan acak di BG1 biar keliatan efek "lubang" (id 0
  // dilewatin, id awan digambar) + parallax scroll beda kecepatan dr BG0.
  for(int i=0;i<26;i++){
    nyxPpuSetLayerTile(1, random(0,NYX_PPU_LAYER_W), random(0,NYX_PPU_LAYER_H), ppuTileCloud, false, false);
  }
  nyxPpuSetScroll(0,0,0); nyxPpuSetScroll(1,0,0);
  ppuEntCount = 0;
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++) nyxPpuSpriteHide(i);
  for(int i=0;i<6;i++) ppuSpawnEntity(40+i*36, 60+ (i%3)*40);
  ppuLastTick = millis();
}
void ppuAppExit(){ exitGameMode(); }

unsigned long getPushLastUs(); // v99 fase 2, didefinisikan di phone.ino -- waktu canvas.pushSprite() fisik ke panel
void drawPpuApp(LGFX_Sprite& s){
  unsigned long ppuAppT0 = micros(); // v109: timing TOTAL drawPpuApp (compositor NyxPPU + status bar + overlay teks),
                                      // supaya kelihatan ada overhead di LUAR nyxPpuRender() sendiri atau tidak
  float fps = nyxPpuFpsTick(); // v98: ukur FPS beneran di HP fisik, bukti nyata hasil optimasi

  s.fillSprite(T().bg);
  drawStatusBar(s);

  unsigned long now = millis();
  float dt = min(40UL, now-ppuLastTick) / 16.0f; // clamp biar gak "meloncat" kalau ada frame drop
  ppuLastTick = now;

  int vx=0, vy=STATUS_H, vw=SCR_W, vh=SCR_H-STATUS_H;

  // parallax: BG0 (langit) geser pelan, BG1 (awan) geser lebih cepat --
  // efek kedalaman klasik ala PPU 2 layer.
  nyxPpuScrollBy(0, 1, 0);
  nyxPpuScrollBy(1, 2, 1);

  for(int i=0;i<ppuEntCount;i++){
    ppuEntX[i]+=ppuEntVX[i]*dt; ppuEntY[i]+=ppuEntVY[i]*dt;
    if(ppuEntX[i]<0||ppuEntX[i]>vw-16) ppuEntVX[i]*=-1;
    if(ppuEntY[i]<0||ppuEntY[i]>vh-16) ppuEntVY[i]*=-1;
    nyxPpuSpriteMove(i, (int)ppuEntX[i], (int)ppuEntY[i]);
  }

  nyxPpuRender(s, vx,vy,vw,vh);

  // v99: tampilkan waktu compositor (ms) di sebelah FPS -- biar kelihatan
  // langsung di HP: kalau ms ini kecil tapi FPS masih rendah, artinya
  // bottleneck-nya BUKAN di NyxPPU (ada di tempat lain, mis. blit ke
  // layar fisik) -- lihat catatan lengkap di nyxPpuRender().
  // v109: 3 angka dipisah biar bottleneck kelihatan ADA DI TAHAP MANA --
  // cpu   = nyxPpuRender() doang (compositor BG+sprite, murni CPU, lihat nyxPpuGetLastRenderUs)
  // app   = drawPpuApp() SAMPAI TITIK INI (cpu di atas + fillSprite/status bar/drawBack) -- kalau
  //         app>>cpu, overhead-nya ada DI LUAR NyxPPU, bukan di engine-nya
  // push  = canvas.pushSprite() FISIK ke panel lewat SPI, dari FRAME SEBELUMNYA (push() di phone.ino,
  //         lihat getPushLastUs) -- beban terakhir SEBELUM frame beneran nongol di layar, DILUAR
  //         kendali NyxPPU sepenuhnya. "Sebelumnya" krn push() fisik baru jalan SETELAH drawPpuApp
  //         ini selesai & di-return ke renderCurrentFrame() -- angka frame INI baru kebaca giliran
  //         teks overlay frame BERIKUTNYA.
  float renderMs = nyxPpuGetLastRenderUs() / 1000.0f;
  float appMs    = (micros() - ppuAppT0) / 1000.0f;
  float pushMs   = getPushLastUs() / 1000.0f;
  char buf[96];
  sprintf(buf,"%d spr-%.0fFPS cpu:%.1f app:%.1f push:%.1f", ppuEntCount, fps, renderMs, appMs, pushMs);
  s.setTextColor(T().text); s.setTextSize(1); s.setCursor(6, vy+4); s.print(buf);

  drawBack(s);
}

void ppuAppTouch(int x, int y, bool held, bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  ppuSpawnEntity(x, y-STATUS_H);
}
