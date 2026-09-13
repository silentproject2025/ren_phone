// =================================================================
// nes_input_renphone.ino
// Jembatan input: touch XPT2046 Ren Phone -> event controller NES.
//
// DESAIN ZONA TOUCH (gambar dgn kata2, layar landscape 320x240):
//   - Frame NES (256x224) ada di TENGAH layar (lihat nes_video_renphone).
//   - Kita TIDAK gambar dpad permanen yg nutupin game -- overlay-nya
//     transparan/garis tipis aja, digambar ULANG tiap akhir frame
//     (dipanggil dari ppu_scanline_blit di file video) SUPAYA gak
//     ketiban/ketimpa render NES yg jalan 60x/detik.
//   - Zona SENTUH (invisible hit-area, lebih gede dari yg digambar)
//     ada di:
//       kiri-bawah  -> D-Pad (up/down/left/right berdasar sudut sentuh
//                      relatif ke titik tengah dpad)
//       kanan-bawah -> tombol A (bawah) & B (atas), gaya SNES
//       tengah-atas -> Start (kanan) & Select (kiri), area kecil
// =================================================================

extern "C" {
  #include <noftypes.h>
  #include <nesinput.h>
  #include <event.h>
}

// --- Titik tengah & radius tiap zona (sesuaikan kalau tangan kamu
//     lebih nyaman geser posisinya) ---
static const int DPAD_CX = 55,  DPAD_CY = 175, DPAD_R = 45;
static const int BTN_A_CX = 275, BTN_A_CY = 185, BTN_A_R = 24;
static const int BTN_B_CX = 235, BTN_B_CY = 155, BTN_B_R = 24;
static const int BTN_START_CX = 175, BTN_START_CY = 20, BTN_R_SMALL = 14;
static const int BTN_SELECT_CX = 145, BTN_SELECT_CY = 20;

// Bitmask status tombol NES saat ini (di-update tiap ada event touch).
static volatile uint8_t nesBtnState = 0;
enum { NB_UP=1, NB_DOWN=2, NB_LEFT=4, NB_RIGHT=8, NB_A=16, NB_B=32, NB_START=64, NB_SELECT=128 };

static inline bool inCircle(int tx,int ty,int cx,int cy,int r){
  int dx=tx-cx, dy=ty-cy;
  return (dx*dx+dy*dy) <= r*r;
}

// -----------------------------------------------------------------
// nesInput_touch(): DIPANGGIL DARI nesTouch() di nes_app_renphone.ino,
// pola SAMA PERSIS dengan apps[idx].touch(tx,ty,held,newT) yg lain di
// firmware kamu. Kita hitung ulang bitmask dari NOL tiap panggilan
// (bukan cuma nambahin bit) supaya begitu jari diangkat / geser keluar
// zona, tombol otomatis "lepas" -- gak perlu deteksi touch-up manual.
// -----------------------------------------------------------------
void nesInput_touch(int tx, int ty, bool held, bool newT) {
  uint8_t s = 0;
  bool touching = held || newT;

  if (touching) {
    // D-pad: cek jarak ke titik tengah dulu (deadzone tengah kecil),
    // lalu tentukan arah dominan (atas/bawah/kiri/kanan) dari sudut.
    int dx = tx - DPAD_CX, dy = ty - DPAD_CY;
    if ((dx*dx + dy*dy) <= (DPAD_R*DPAD_R) && (dx*dx+dy*dy) > 25) {
      if (abs(dx) > abs(dy)) s |= (dx > 0) ? NB_RIGHT : NB_LEFT;
      else                   s |= (dy > 0) ? NB_DOWN  : NB_UP;
    }
    if (inCircle(tx,ty,BTN_A_CX,BTN_A_CY,BTN_A_R)) s |= NB_A;
    if (inCircle(tx,ty,BTN_B_CX,BTN_B_CY,BTN_B_R)) s |= NB_B;
    if (inCircle(tx,ty,BTN_START_CX,BTN_START_CY,BTN_R_SMALL)) s |= NB_START;
    if (inCircle(tx,ty,BTN_SELECT_CX,BTN_SELECT_CY,BTN_R_SMALL)) s |= NB_SELECT;
  }
  nesBtnState = s;
}

// -----------------------------------------------------------------
// osd_getinput(): DIPANGGIL LANGSUNG oleh core nofrendo, SEKALI PER
// FRAME (nes.c baris ~385). Pola edge-detect ini disalin persis dari
// keyboard.c referensi -- bandingin state lama vs baru, kirim event
// MAKE/BREAK cuma pas ada PERUBAHAN (bukan tiap frame tombol ditekan).
// -----------------------------------------------------------------
extern volatile bool nesExitRequested; // dari nes_app_renphone.ino

extern "C" void osd_getinput(void) {
  // Cek permintaan keluar (dari nesExit(), thread UI) DI SINI -- ini
  // titik yg AMAN buat manggil nes_poweroff() krn osd_getinput() jalan
  // di thread/task yg SAMA dgn nes_emulate() (bukan dari luar/thread
  // lain), jadi gak ada race condition dgn struktur internal NES.
  if (nesExitRequested) {
    extern "C" void nes_poweroff(void);
    nes_poweroff();
  }

  struct { uint8_t mask; int event; } table[] = {
    { NB_UP,     event_joypad1_up },
    { NB_DOWN,   event_joypad1_down },
    { NB_LEFT,   event_joypad1_left },
    { NB_RIGHT,  event_joypad1_right },
    { NB_A,      event_joypad1_a },
    { NB_B,      event_joypad1_b },
    { NB_START,  event_joypad1_start },
    { NB_SELECT, event_joypad1_select },
  };

  static uint8_t oldState = 0;
  uint8_t newState = nesBtnState; // salin sekali, hindari race dgn task lain

  for (int i = 0; i < 8; i++) {
    bool was = oldState & table[i].mask;
    bool now = newState & table[i].mask;
    if (was != now) {
      event_t evt = event_get(table[i].event);
      if (evt) evt(now ? INP_STATE_MAKE : INP_STATE_BREAK);
    }
  }
  oldState = newState;
}

// -----------------------------------------------------------------
// nesOverlay_afterFrame(): digambar ulang tiap akhir frame (dipanggil
// dari nes_video_renphone.ino). SENGAJA cuma garis outline tipis biar
// gak nutupin game & gak berat -- highlight nyala pas lagi disentuh
// biar ada feedback visual tombol kena/nggak.
// -----------------------------------------------------------------
void nesOverlay_afterFrame() {
  uint8_t s = nesBtnState;
  uint16_t idle = TFT_DARKGREY, active = TFT_WHITE;

  // D-pad: gambar 4 anak panah kecil (bukan lingkaran penuh, biar area
  // game di baliknya tetep sebagian kelihatan)
  display.drawCircle(DPAD_CX, DPAD_CY, DPAD_R, idle);
  display.fillTriangle(DPAD_CX,DPAD_CY-DPAD_R+6, DPAD_CX-8,DPAD_CY-DPAD_R+18, DPAD_CX+8,DPAD_CY-DPAD_R+18, (s&NB_UP)?active:idle);
  display.fillTriangle(DPAD_CX,DPAD_CY+DPAD_R-6, DPAD_CX-8,DPAD_CY+DPAD_R-18, DPAD_CX+8,DPAD_CY+DPAD_R-18, (s&NB_DOWN)?active:idle);
  display.fillTriangle(DPAD_CX-DPAD_R+6,DPAD_CY, DPAD_CX-DPAD_R+18,DPAD_CY-8, DPAD_CX-DPAD_R+18,DPAD_CY+8, (s&NB_LEFT)?active:idle);
  display.fillTriangle(DPAD_CX+DPAD_R-6,DPAD_CY, DPAD_CX+DPAD_R-18,DPAD_CY-8, DPAD_CX+DPAD_R-18,DPAD_CY+8, (s&NB_RIGHT)?active:idle);

  display.drawCircle(BTN_A_CX, BTN_A_CY, BTN_A_R, (s&NB_A)?active:idle);
  display.setTextColor((s&NB_A)?active:idle); display.setCursor(BTN_A_CX-4,BTN_A_CY-4); display.print("A");
  display.drawCircle(BTN_B_CX, BTN_B_CY, BTN_B_R, (s&NB_B)?active:idle);
  display.setTextColor((s&NB_B)?active:idle); display.setCursor(BTN_B_CX-4,BTN_B_CY-4); display.print("B");

  display.drawCircle(BTN_START_CX, BTN_START_CY, BTN_R_SMALL, (s&NB_START)?active:idle);
  display.drawCircle(BTN_SELECT_CX, BTN_SELECT_CY, BTN_R_SMALL, (s&NB_SELECT)?active:idle);
}
