// =====================================================================
// ptr_renphone.ino -- 2 gestur sistem baru (dipasang di phone.ino lewat 3 baris hook):
//
//  1) PULL-TO-REFRESH: tarik ke bawah dari strip atas layar (y 18..70, di kiri tombol cari) pada app yg punya
//     aksi "muat ulang": Update (Cek update), NEO Asteroid, Bumi EPIC, Galeri NASA. Ada lingkaran penunjuk yg
//     turun mengikuti jari & terisi; lepas setelah penuh = refresh. Hanya menimpa gambar app (tidak menggeser isinya).
//
//  2) SWIPE-BACK dari TEPI KIRI: sentuh x<=12 lalu geser ke kanan; gelembung "<" mengikuti jari, lepas setelah
//     cukup jauh = navBack(). Hanya di app non-game/non-editor (daftar putih di edgeBackScreenOk()) supaya
//     tidak mengganggu dpad game, kanvas gambar, atau pengetikan.
//
// File ini HARUS dikompilasi SETELAH phone.ino (urutan abjad Arduino sudah menjamin: "ptr" > "phone").
// =====================================================================

#define PTR_ZONE_TOP 18
#define PTR_ZONE_BOT 70
#define PTR_MAX      56
#define PTR_TRIGGER  40
static bool  ptrTracking = false, ptrActive = false;
static int   ptrSy = 0;
static float ptrPull = 0;

static bool ptrScreenOk() {
  if (locked || appSwitcherOpen || controlCenterOpen || notifShadeOpen || diMenuOpen || powerMenuOpen || kbVisible) return false;
  switch (curScreen()) {
    case SCR_UPDATE:  return !updBusy;
    case SCR_NEOWS:   return neoPage == NEO_LIST && !numPadVisible;
    case SCR_EPIC:    return epicPage == EPIC_VIEW && !numPadVisible;
    case SCR_NASAIMG: return imgLibPage == ILIB_VIEW && imgLibQuery.length() > 0;
    default:          return false;
  }
}

static void ptrFire() {
  switch (curScreen()) {
    case SCR_UPDATE:  otaCheckAndFill(); break;
    case SCR_NEOWS:   neoRetry();        break;
    case SCR_EPIC:    epicRetry();       break;
    case SCR_NASAIMG: imgLibRetry();     break;
    default: break;
  }
}

static bool edgeBackScreenOk() {
  if (locked || appSwitcherOpen || controlCenterOpen || notifShadeOpen || diMenuOpen || powerMenuOpen || kbVisible || updBusy) return false;
  switch (curScreen()) {
    case SCR_SETTINGS: case SCR_FILEEXPLORER: case SCR_UPDATE: case SCR_BATTERY: case SCR_APOD: case SCR_NEOWS:
    case SCR_EPIC: case SCR_NASAIMG: case SCR_ABOUT: case SCR_OWNER: case SCR_HOMEEDIT: case SCR_REMOTE:
    case SCR_CHAT: case SCR_CLOCK: case SCR_SENSOR: case SCR_HWMON: case SCR_MICLEVEL: case SCR_POMODORO:
    case SCR_WAKE: case SCR_MEMO: case SCR_STOPWATCH: case SCR_MUSIC: case SCR_GALLERY:
      return true;
    default: return false;
  }
}

static bool  edgeTracking = false, edgeActive = false;
static int   edgeSx = 0, edgeSy = 0, edgeLastX = 0;
static float edgeDx = 0, edgeVel = 0;
static unsigned long edgeLastMs = 0;

void ptrPointerTick(bool touched, bool wasT, int tx, int ty) {
  bool newT = touched && !wasT;
  unsigned long now = millis();

  // ---------- swipe-back tepi kiri ----------
  if (!edgeBackScreenOk() || (curScreen() == SCR_GALLERY && galMode == GM_VIEW)   /* di penampil foto, geser dari tepi = foto sebelumnya */) {
    if (edgeActive) { edgeActive = false; needRedraw = true; }
    edgeTracking = false;
  } else if (newT) {
    edgeTracking = (tx <= 12);
    edgeActive = false; edgeSx = tx; edgeSy = ty; edgeLastX = tx; edgeLastMs = now; edgeDx = 0; edgeVel = 0;
  } else if (touched && edgeTracking) {
    int dx = tx - edgeSx, dy = ty - edgeSy;
    if (!edgeActive && dx > 12 && dx * 10 > abs(dy) * 15) edgeActive = true;
    if (edgeActive) {
      edgeDx = (float)dx;
      unsigned long dt = now - edgeLastMs;
      if (dt > 0) edgeVel = edgeVel * 0.6f + ((float)(tx - edgeLastX) / (float)dt) * 0.4f;
      edgeLastX = tx; edgeLastMs = now;
      needRedraw = true;
    }
  } else if (!touched && wasT && edgeTracking) {
    bool go = edgeActive && (edgeDx >= 70.0f || (edgeDx >= 35.0f && edgeVel > 0.5f));
    edgeTracking = false; edgeActive = false; needRedraw = true;
    if (go) {
      vibTap();
      if (curScreen() == SCR_GALLERY) galBack(); else navBack();
      return;
    }
  }

  // ---------- pull-to-refresh ----------
  if (!ptrScreenOk()) {
    if (ptrActive) { ptrActive = false; needRedraw = true; }
    ptrTracking = false;
    return;
  }
  if (newT) {
    ptrTracking = (ty >= PTR_ZONE_TOP && ty <= PTR_ZONE_BOT && tx < SCR_W - 70 && !edgeTracking);
    ptrSy = ty; ptrActive = false; ptrPull = 0;
  } else if (touched && ptrTracking) {
    int dy = ty - ptrSy;
    if (dy > 10 || ptrActive) {
      ptrActive = true;
      float p = (float)(dy - 10) * 0.8f;
      ptrPull = p < 0 ? 0 : (p > (float)PTR_MAX ? (float)PTR_MAX : p);
      needRedraw = true;
    }
  } else if (!touched && wasT && ptrTracking) {
    bool fire = ptrActive && ptrPull >= (float)PTR_TRIGGER;
    ptrActive = false; ptrTracking = false; needRedraw = true;
    if (fire) { vibTap(); ptrFire(); }
  }
}

// dipanggil renderCurrentFrame() setelah app menggambar dirinya
void ptrDraw(LGFX_Sprite& s) {
  if (ptrActive) {
    float p = ptrPull / (float)PTR_TRIGGER; if (p > 1.0f) p = 1.0f;
    int cx = SCR_W / 2 - 20, cy = PTR_ZONE_TOP + 4 + (int)ptrPull * 6 / 10;
    s.fillCircle(cx, cy, 13, T().surface2);
    if (p >= 1.0f) {
      s.fillCircle(cx, cy, 13, T().accent);
      s.drawLine(cx, cy - 5, cx, cy + 5, T().bg); s.drawLine(cx - 4, cy + 1, cx, cy + 5, T().bg); s.drawLine(cx + 4, cy + 1, cx, cy + 5, T().bg);
    } else {
      s.drawArc(cx, cy, 13, 11, 270, 270 + (int)(360.0f * p), T().accent);
      s.drawLine(cx, cy - 5, cx, cy + 5, T().subtext); s.drawLine(cx - 4, cy + 1, cx, cy + 5, T().subtext); s.drawLine(cx + 4, cy + 1, cx, cy + 5, T().subtext);
    }
  }
  if (edgeActive) {
    float p = edgeDx / 70.0f; if (p > 1.0f) p = 1.0f; if (p < 0.0f) p = 0.0f;
    int cx = (int)(edgeDx * 0.55f) - 4; if (cx > 40) cx = 40;
    int cy = edgeSy; if (cy < 30) cy = 30; if (cy > SCR_H - 30) cy = SCR_H - 30;
    uint16_t bg = (p >= 1.0f) ? (uint16_t)T().accent : (uint16_t)T().surface2;
    uint16_t fg = (p >= 1.0f) ? (uint16_t)T().bg : (uint16_t)T().text;
    s.fillCircle(cx, cy, 15, bg);
    s.drawLine(cx + 3, cy - 7, cx - 3, cy, fg); s.drawLine(cx - 3, cy, cx + 3, cy + 7, fg);
    s.drawLine(cx + 4, cy - 7, cx - 2, cy, fg); s.drawLine(cx - 2, cy, cx + 4, cy + 7, fg);
  }
}
