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
};

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
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++){ nyxPpuSprites[i].visible=false; nyxPpuSprites[i].scale=1; }
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
void nyxPpuSpriteHide(int idx){ if(idx>=0&&idx<NYX_PPU_MAX_SPRITES) nyxPpuSprites[idx].visible=false; }
int  nyxPpuSpriteFindFree(){
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++) if(!nyxPpuSprites[i].visible) return i;
  return -1;
}

// ---- render internals ----
static void nyxPpuBlitTilePixel(int srcTx, int srcTy, int tileId, bool flipX, bool flipY, uint16_t& outColor){
  int sx = flipX ? (NYX_PPU_TILE-1-srcTx) : srcTx;
  int sy = flipY ? (NYX_PPU_TILE-1-srcTy) : srcTy;
  outColor = nyxPpuTiles[(size_t)tileId*NYX_PPU_TILE*NYX_PPU_TILE + sy*NYX_PPU_TILE + sx];
}

// Gambar 1 layer BG (opaque, kecuali skipZero=true -> tileId 0 dilubangi
// biar layer belakang/sprite kelihatan tembus -- ini yg dipakai BG1 biar
// jadi "layer depan berlubang", jawaban user soal Layering).
static void nyxPpuDrawLayer(LGFX_Sprite& s, int layer, int vx, int vy, int vw, int vh, bool skipZero){
  int sx = ((nyxPpuScrollX[layer] % (NYX_PPU_LAYER_W*NYX_PPU_TILE)) + NYX_PPU_LAYER_W*NYX_PPU_TILE) % (NYX_PPU_LAYER_W*NYX_PPU_TILE);
  int sy = ((nyxPpuScrollY[layer] % (NYX_PPU_LAYER_H*NYX_PPU_TILE)) + NYX_PPU_LAYER_H*NYX_PPU_TILE) % (NYX_PPU_LAYER_H*NYX_PPU_TILE);
  for(int py=0; py<vh; py++){
    int wy = (py+sy) % (NYX_PPU_LAYER_H*NYX_PPU_TILE);
    int row = wy / NYX_PPU_TILE, ty = wy % NYX_PPU_TILE;
    for(int px=0; px<vw; px++){
      int wx = (px+sx) % (NYX_PPU_LAYER_W*NYX_PPU_TILE);
      int col = wx / NYX_PPU_TILE, tx = wx % NYX_PPU_TILE;
      int cellIdx = row*NYX_PPU_LAYER_W+col;
      uint8_t tileId = nyxPpuLayerTileId[layer][cellIdx];
      if(skipZero && tileId==0) continue;
      uint8_t attr = nyxPpuLayerAttr[layer][cellIdx];
      uint16_t c; nyxPpuBlitTilePixel(tx,ty,tileId,attr&1,attr&2,c);
      s.drawPixel(vx+px, vy+py, c);
    }
  }
}

static void nyxPpuDrawSprites(LGFX_Sprite& s, int vx, int vy, int vw, int vh, bool frontPass){
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++){
    NyxSprite& sp = nyxPpuSprites[i];
    if(!sp.visible) continue;
    if(sp.priorityFront != frontPass) continue;
    int size = NYX_PPU_TILE * sp.scale;
    if(sp.x+size<0 || sp.y+size<0 || sp.x>=vw || sp.y>=vh) continue; // culling di luar viewport
    for(int py=0; py<size; py++){
      int sy2 = sp.y+py; if(sy2<0||sy2>=vh) continue;
      int srcTy = (py*NYX_PPU_TILE)/size;
      for(int px2=0; px2<size; px2++){
        int sx2 = sp.x+px2; if(sx2<0||sx2>=vw) continue;
        int srcTx = (px2*NYX_PPU_TILE)/size;
        uint16_t c; nyxPpuBlitTilePixel(srcTx,srcTy,sp.tileId,sp.flipX,sp.flipY,c);
        if(c==NYX_PPU_TRANSPARENT) continue;
        if(sp.hasTint) c = blend565(c, sp.tint, sp.tintAmt);
        s.drawPixel(vx+sx2, vy+sy2, c);
      }
    }
  }
}

// Panggilan utama: 1x per frame. Urutan gambar (niru prioritas PPU asli):
// BG0 -> sprite(priority=belakang) -> BG1(berlubang di tileId 0) -> sprite(priority=depan, default)
void nyxPpuRender(LGFX_Sprite& s, int vx, int vy, int vw, int vh){
  if(!nyxPpuReady) return;
  nyxPpuDrawLayer(s, 0, vx,vy,vw,vh, false);
  nyxPpuDrawSprites(s, vx,vy,vw,vh, false);
  nyxPpuDrawLayer(s, 1, vx,vy,vw,vh, true);
  nyxPpuDrawSprites(s, vx,vy,vw,vh, true);
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
    nyxPpuSetLayerTile(1, random(0,NYX_PPU_LAYER_W), random(0,NYX_PPU_LAYER_H), ppuTileCloud);
  }
  nyxPpuSetScroll(0,0,0); nyxPpuSetScroll(1,0,0);
  ppuEntCount = 0;
  for(int i=0;i<NYX_PPU_MAX_SPRITES;i++) nyxPpuSpriteHide(i);
  for(int i=0;i<6;i++) ppuSpawnEntity(40+i*36, 60+ (i%3)*40);
  ppuLastTick = millis();
}
void ppuAppExit(){ exitGameMode(); }

void drawPpuApp(LGFX_Sprite& s){
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

  char buf[48];
  sprintf(buf,"NyxPPU v1 - %d sprite (ketuk utk nambah)", ppuEntCount);
  s.setTextColor(T().text); s.setTextSize(1); s.setCursor(6, vy+4); s.print(buf);

  drawBack(s);
}

void ppuAppTouch(int x, int y, bool held, bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  ppuSpawnEntity(x, y-STATUS_H);
}
