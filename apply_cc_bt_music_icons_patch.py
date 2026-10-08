#!/usr/bin/env python3
"""
Patch Ren Phone / Accretion:
  1. Control Center: tambah tombol BLUETOOTH (index 9) + layout 5x2 (landscape) / 3x4 (portrait)
  2. Pemutar Musik: bug judul "ganda" (1 statis + 1 bergulir) -> judul dibuat 1 salinan saja
  3. Ikon aplikasi hitam (Mic Level, DOOM, PPU, Pomodoro, Wake Word, Musik) -> diberi warna terang,
     warna yang pudar dicerahkan, dan warna glyph ikon dipilih otomatis (gelap/putih) sesuai kecerahan lingkaran.
  4. rplink.h: tambah RP_BTMODE (+ RP_PING yang sebelumnya dipakai tapi belum didefinisikan)

Jalankan di root repo (folder yang berisi phone.ino):
    python apply_cc_bt_music_icons_patch.py

Idempotent: aman dijalankan ulang (yang sudah terpasang di-skip).
PENTING: rplink.h & cam_bt_coprocessor.ino ikut berubah -> flash ULANG ESP32-CAM juga,
supaya tombol Bluetooth benar-benar mematikan/menyalakan BT-nya.
"""
import os
import sys

ROOT = "."
cache = {}


def load(path):
    if path not in cache:
        full = os.path.join(ROOT, path)
        if not os.path.exists(full):
            sys.exit(f"[GAGAL] {path} tidak ketemu. Jalankan dari root repo.")
        with open(full, encoding="utf-8", newline="") as f:
            cache[path] = f.read()
    return cache[path]


def edit(name, path, old, new, marker):
    s = load(path)
    if marker in s:
        print(f"[skip ] {name}")
        return
    n = s.count(old)
    if n != 1:
        sys.exit(f"[GAGAL] {name}: anchor ditemukan {n}x di {path} (harus 1x). "
                 f"File kamu mungkin sudah beda dari versi yang saya baca.")
    cache[path] = s.replace(old, new)
    print(f"[ok   ] {name}")


def rgb565(r, g, b):
    return "0x%04X" % (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))


PH = "phone.ino"
MU = "musicbt_renphone.ino"
CAM = "cam_bt_coprocessor/cam_bt_coprocessor.ino"

# ---------------------------------------------------------------- rplink.h (2 salinan, harus identik)
for rp in ("rplink.h", "cam_bt_coprocessor/rplink.h"):
    edit(f"{rp}: RP_BTMODE + RP_PING", rp,
         "#define RP_TXGAIN   'T'   // payload: [0..7]\n",
         "#define RP_TXGAIN   'T'   // payload: [0..7]\n"
         "#define RP_BTMODE   'B'   // payload: [1=Bluetooth nyala, 0=mati] (tombol BT di Control Center)\n"
         "#ifndef RP_PING\n"
         "#define RP_PING     'G'   // keepalive S3 -> CAM, tanpa payload (CAM mengabaikannya)\n"
         "#endif\n",
         "RP_BTMODE")

# ---------------------------------------------------------------- CAM firmware
edit("CAM: variabel btEnabled", CAM,
     "static uint32_t          btDisconnectedAt = 0;\n",
     "static uint32_t          btDisconnectedAt = 0;\n"
     "static volatile bool     btEnabled = true;   // saklar BT dari S3 (tombol Control Center)\n",
     "btEnabled = true;   // saklar BT")

edit("CAM: fungsi btSetEnabled + connectTo", CAM,
     "static void connectTo(const char* name) {\n"
     "  strncpy(savedBTName, name, sizeof(savedBTName) - 1);\n"
     "  savedBTName[sizeof(savedBTName) - 1] = 0;\n"
     "  prefsDirty = true;\n"
     "  a2dp.set_auto_reconnect(true);\n"
     "  a2dp.start(savedBTName);\n"
     "}\n",
     "// Saklar Bluetooth dari tombol Control Center (RP_BTMODE).\n"
     "static void btSetEnabled(bool on) {\n"
     "  btEnabled = on;\n"
     "  if (on) {\n"
     "    a2dp.set_auto_reconnect(true);\n"
     "    btDisconnectedAt = millis();     // loop() akan memanggil reconnect() setelah jeda\n"
     "  } else {\n"
     "    a2dp.set_auto_reconnect(false);\n"
     "    btDisconnectedAt = 0;            // hentikan upaya reconnect manual\n"
     "    if (btConnected) a2dp.disconnect();\n"
     "  }\n"
     "}\n\n"
     "static void connectTo(const char* name) {\n"
     "  strncpy(savedBTName, name, sizeof(savedBTName) - 1);\n"
     "  savedBTName[sizeof(savedBTName) - 1] = 0;\n"
     "  prefsDirty = true;\n"
     "  btEnabled = true;                  // menyambung ke TWS = BT otomatis nyala\n"
     "  a2dp.set_auto_reconnect(true);\n"
     "  a2dp.start(savedBTName);\n"
     "}\n",
     "static void btSetEnabled(bool on)")

edit("CAM: terima RP_BTMODE di link", CAM,
     "    case RP_SCAN: case RP_CONNECT: case RP_TXGAIN:\n",
     "    case RP_SCAN: case RP_CONNECT: case RP_TXGAIN: case RP_BTMODE:\n",
     "case RP_TXGAIN: case RP_BTMODE:")

edit("CAM: proses RP_BTMODE di loop", CAM,
     "    else if (c.type == RP_TXGAIN && c.len >= 1) { applyTxGain(c.data[0]); prefsDirty = true; }\n",
     "    else if (c.type == RP_TXGAIN && c.len >= 1) { applyTxGain(c.data[0]); prefsDirty = true; }\n"
     "    else if (c.type == RP_BTMODE && c.len >= 1) btSetEnabled(c.data[0] != 0);\n",
     "c.type == RP_BTMODE")

edit("CAM: reconnect hanya kalau BT nyala", CAM,
     "  if (!btConnected && btDisconnectedAt > 0 && now - btDisconnectedAt > BT_RECONNECT_MS) {",
     "  if (btEnabled && !btConnected && btDisconnectedAt > 0 && now - btDisconnectedAt > BT_RECONNECT_MS) {",
     "if (btEnabled && !btConnected")

# ---------------------------------------------------------------- musicbt_renphone.ino
edit("Musik: state musBtOn", MU,
     "static volatile bool     musBtConn = false;\n",
     "static volatile bool     musBtConn = false;\n"
     "static volatile bool     musBtOn = true;      // saklar BT dari Control Center (NVS \"music\"/\"bton\")\n"
     "static bool              musBtOnLoaded = false;\n",
     "musBtOnLoaded = false;")

edit("Musik: request 'B' (saklar BT)", MU,
     "    case 'r': musScanPlaylist(); break;\n",
     "    case 'B': {                                   // saklar Bluetooth (Control Center)\n"
     "      uint8_t m = a ? 1 : 0;\n"
     "      rpSend(Serial1, RP_BTMODE, &m, 1);\n"
     "      if (!m) musConnecting = false;\n"
     "      break;\n"
     "    }\n"
     "    case 'r': musScanPlaylist(); break;\n",
     "case 'B': {                                   // saklar")

edit("Musik: connect manual = BT nyala", MU,
     "    case 'c':\n      musConnecting = true; musConnectStart = millis();\n",
     "    case 'c':\n      musBtOn = true; // menyambung ke TWS = BT nyala (disimpan di musicLoopPoll)\n"
     "      musConnecting = true; musConnectStart = millis();\n",
     "musBtOn = true; // menyambung")

edit("Musik: kirim ulang status BT mati saat CAM (re)boot", MU,
     "      uint8_t v = (uint8_t)musVol; rpSend(Serial1, RP_VOLUME, &v, 1);\n"
     "    } else if (musCamAlive && !prevAlive) {\n"
     "      uint8_t v = (uint8_t)musVol; rpSend(Serial1, RP_VOLUME, &v, 1);\n"
     "    }\n",
     "      uint8_t v = (uint8_t)musVol; rpSend(Serial1, RP_VOLUME, &v, 1);\n"
     "      if (!musBtOn) { uint8_t m = 0; rpSend(Serial1, RP_BTMODE, &m, 1); }\n"
     "    } else if (musCamAlive && !prevAlive) {\n"
     "      uint8_t v = (uint8_t)musVol; rpSend(Serial1, RP_VOLUME, &v, 1);\n"
     "      if (!musBtOn) { uint8_t m = 0; rpSend(Serial1, RP_BTMODE, &m, 1); }\n"
     "    }\n",
     "if (!musBtOn) { uint8_t m = 0;")

edit("Musik: simpan BT nyala kalau TWS tersambung manual", MU,
     "  if (musBtConn != prevConn) {\n    prevConn = musBtConn;\n",
     "  if (musBtConn != prevConn) {\n    prevConn = musBtConn;\n"
     "    if (musBtConn) {                             // tersambung = BT pasti nyala -> samakan & simpan\n"
     "      Preferences bp; bp.begin(\"music\", true); bool saved = bp.getBool(\"bton\", true); bp.end();\n"
     "      if (!saved) { Preferences bw; bw.begin(\"music\", false); bw.putBool(\"bton\", true); bw.end(); }\n"
     "      musBtOn = true; musBtOnLoaded = true;\n"
     "    }\n",
     "tersambung = BT pasti nyala")

edit("Musik: API publik utk Control Center", MU,
     "void musicExit() {}\n",
     "void musicExit() {}\n\n"
     "// ---- API untuk tombol Bluetooth di Control Center (dipanggil dari loop utama, bukan task) ----\n"
     "bool btCcEnabled() {\n"
     "  if (!musBtOnLoaded) {\n"
     "    musBtOnLoaded = true;\n"
     "    Preferences p; p.begin(\"music\", true);\n"
     "    musBtOn = p.getBool(\"bton\", true);\n"
     "    p.end();\n"
     "    if (!musBtOn) musBegin();   // link harus hidup supaya CAM diberi tahu BT mati setelah boot\n"
     "  }\n"
     "  return musBtOn;\n"
     "}\n"
     "bool btCcConnected() { return musStarted && musBtOn && musBtConn; }\n"
     "void btCcToggle() {\n"
     "  bool wasStarted = musStarted;\n"
     "  btCcEnabled();                 // pastikan nilai tersimpan sudah ter-load\n"
     "  musBegin();                    // nyalakan link ke CAM kalau belum (no-op kalau sudah)\n"
     "  musBtOn = !musBtOn;\n"
     "  { Preferences p; p.begin(\"music\", false); p.putBool(\"bton\", musBtOn); p.end(); }\n"
     "  musPost('B', musBtOn ? 1 : 0, nullptr);\n"
     "  if (wasStarted && !musCamAlive) showToast(\"Modul BT tidak terdeteksi\");\n"
     "  else showToast(musBtOn ? \"Bluetooth aktif\" : \"Bluetooth mati\");\n"
     "}\n",
     "bool btCcEnabled() {")

# Bug judul ganda: 1 salinan saja + matikan text-wrap
edit("Musik: judul 1 salinan (bukan 2) + setTextWrap(false)", MU,
     "  // judul (gulir kalau kepanjangan)\n"
     "  s.setTextSize(2); s.setTextColor(T().text);\n"
     "  int tw = s.textWidth(title);\n"
     "  s.setClipRect(mTitX, mTitY - 1, mTitW, 19);\n"
     "  if (tw <= mTitW) {\n"
     "    int tx = mLand ? mTitX : mTitX + (mTitW - tw) / 2;\n"
     "    s.setCursor(tx, mTitY); s.print(title);\n"
     "  } else {\n"
     "    int span = tw + 40;\n"
     "    int off = (int)((millis() / 25) % (uint32_t)span);\n"
     "    s.setCursor(mTitX - off, mTitY);        s.print(title);\n"
     "    s.setCursor(mTitX - off + span, mTitY); s.print(title);\n"
     "  }\n"
     "  s.clearClipRect();\n",
     "  // judul: kalau muat -> diam di tengah/kiri. Kalau kepanjangan -> SATU salinan saja yang\n"
     "  // bergeser (jeda di awal, geser ke kiri sampai ujung, jeda, lalu balik). Dulu 2 salinan\n"
     "  // digambar bersamaan (efek loop) dan text-wrap masih aktif -> kelihatan seperti 2 judul.\n"
     "  s.setTextSize(2); s.setTextColor(T().text);\n"
     "  s.setTextWrap(false, false);\n"
     "  int tw = s.textWidth(title);\n"
     "  s.setClipRect(mTitX, mTitY - 1, mTitW, 19);\n"
     "  if (tw <= mTitW) {\n"
     "    int tx = mLand ? mTitX : mTitX + (mTitW - tw) / 2;\n"
     "    s.setCursor(tx, mTitY); s.print(title);\n"
     "  } else {\n"
     "    int range = tw - mTitW + 6;                       // jarak geser total (piksel)\n"
     "    const uint32_t TT_PAUSE = 1200, TT_MS_PER_PX = 30;\n"
     "    uint32_t moveMs = (uint32_t)range * TT_MS_PER_PX;\n"
     "    uint32_t ph = millis() % (2 * (TT_PAUSE + moveMs));\n"
     "    int off;\n"
     "    if (ph < TT_PAUSE)                    off = 0;\n"
     "    else if (ph < TT_PAUSE + moveMs)      off = (int)((ph - TT_PAUSE) / TT_MS_PER_PX);\n"
     "    else if (ph < 2 * TT_PAUSE + moveMs)  off = range;\n"
     "    else                                  off = range - (int)((ph - 2 * TT_PAUSE - moveMs) / TT_MS_PER_PX);\n"
     "    if (off < 0) off = 0; if (off > range) off = range;\n"
     "    s.setCursor(mTitX - off, mTitY); s.print(title);\n"
     "  }\n"
     "  s.clearClipRect();\n"
     "  s.setTextWrap(true, true);                          // kembalikan default LovyanGFX\n",
     "TT_MS_PER_PX")

# ---------------------------------------------------------------- phone.ino : Control Center
edit("CC: grid dinamis (5x2 landscape / 3x4 portrait) + 10 item", PH,
     "#define CC_COLS 3\n#define CC_ROWS 3\n#define CC_ITEMS 9\n",
     "#define CC_ITEMS 10 // +Bluetooth (index 9)\n"
     "int ccCols(){ return currentOrient==ORIENT_LANDSCAPE ? 5 : 3; } // landscape 5x2, portrait 3x4 -> 10 tombol muat\n"
     "#define CC_COLS ccCols()\n"
     "#define CC_ROWS ((CC_ITEMS+CC_COLS-1)/CC_COLS)\n"
     "// Bluetooth (modul ESP32-CAM) -- definisi di musicbt_renphone.ino\n"
     "bool btCcEnabled(); bool btCcConnected(); void btCcToggle();\n",
     "bool btCcEnabled(); bool btCcConnected();")

edit("CC: aksi Bluetooth", PH,
     "void ccActShake(){",
     "void ccActBluetooth(){ btCcToggle(); } // nyalakan/matikan Bluetooth (modul CAM)\nvoid ccActShake(){",
     "void ccActBluetooth()")

edit("CC: array animasi 10 item", PH,
     "float ccBtnAnim[CC_ITEMS]={0,0,0,0,0,0,0,0,0};",
     "float ccBtnAnim[CC_ITEMS]={0};",
     "float ccBtnAnim[CC_ITEMS]={0};")

edit("CC: status aktif Bluetooth", PH,
     "    case 8: return !vibEnabled;\n",
     "    case 8: return !vibEnabled;\n    case 9: return btCcEnabled(); // Bluetooth nyala = tombol terisi\n",
     "case 9: return btCcEnabled()")

edit("CC: stagger pop-in lebih rapat (10 tombol)", PH,
     "float li=(openP-0.20f-0.035f*i)/0.45f;",
     "float li=(openP-0.20f-0.03f*i)/0.45f;",
     "float li=(openP-0.20f-0.03f*i)/0.45f;")

edit("CC: ikon Bluetooth", PH,
     "    case 8: { // Mode Senyap: lonceng",
     "    case 9: { // Bluetooth: rune \"B\" (+ titik hijau kalau TWS terhubung)\n"
     "      int h=r-3; if(h<6) h=6;\n"
     "      int w=(h*55)/100; if(w<3) w=3;\n"
     "      int q=h/2;\n"
     "      for(int dx=0;dx<2;dx++){ // 2 garis berdampingan -> rune lebih tebal & jelas\n"
     "        s.drawLine(cx+dx,cy-h, cx+dx,cy+h, ic);\n"
     "        s.drawLine(cx+dx,cy-h, cx+w+dx,cy-q, ic);\n"
     "        s.drawLine(cx+w+dx,cy-q, cx-w+dx,cy+q, ic);\n"
     "        s.drawLine(cx+dx,cy+h, cx+w+dx,cy+q, ic);\n"
     "        s.drawLine(cx+w+dx,cy+q, cx-w+dx,cy-q, ic);\n"
     "      }\n"
     "      if(btCcConnected()){\n"
     "        s.fillCircle(cx+r-1,cy-r+3,4,fillCol);\n"
     "        s.fillCircle(cx+r-1,cy-r+3,3,T().good);\n"
     "      }\n"
     "      break;\n"
     "    }\n"
     "    case 8: { // Mode Senyap: lonceng",
     "case 9: { // Bluetooth: rune")

edit("CC: daftar aksi sentuh", PH,
     "= { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, ccActShake, nullptr, ccActNeopixel, ccActSilent };",
     "= { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, ccActShake, nullptr, ccActNeopixel, ccActSilent, ccActBluetooth };",
     "ccActSilent, ccActBluetooth };")

# ---------------------------------------------------------------- phone.ino : ikon aplikasi
edit("Ikon: glyph otomatis kontras terhadap warna lingkaran", PH,
     "void drawAppIcon(LGFX_Sprite& s, char sym, int cx, int cy, int r, uint16_t bgCircle){\n"
     "  s.fillCircle(cx,cy,r,bgCircle);\n"
     "  uint16_t ic = T().bg; // warna vektor ikon (kontras dgn lingkaran berwarna)\n",
     "// Warna glyph ikon dipilih dari KECERAHAN lingkaran: lingkaran terang -> glyph gelap,\n"
     "// lingkaran gelap -> glyph putih. Dulu selalu T().bg, jadi di tema Light/Pastel (bg terang)\n"
     "// glyph nyaris tak terlihat, dan di App Switcher (lingkaran surface2 gelap) glyph gelap-on-gelap.\n"
     "uint16_t appIconInk(uint16_t c){\n"
     "  int r=((c>>11)&31)*255/31, g=((c>>5)&63)*255/63, b=(c&31)*255/31;\n"
     "  int lum=(r*299+g*587+b*114)/1000;\n"
     "  return lum>=120 ? (uint16_t)0x1082 : (uint16_t)0xFFFF;\n"
     "}\n"
     "void drawAppIcon(LGFX_Sprite& s, char sym, int cx, int cy, int r, uint16_t bgCircle){\n"
     "  s.fillCircle(cx,cy,r,bgCircle);\n"
     "  uint16_t ic = appIconInk(bgCircle); // warna vektor ikon (kontras dgn lingkaran berwarna)\n",
     "uint16_t appIconInk(uint16_t c)")

# warna lama yang pudar -> lebih terang
edit("Ikon: Files lebih terang", PH,
     "apps[7].color=0x3ADF;", f"apps[7].color={rgb565(90,175,255)};", f"apps[7].color={rgb565(90,175,255)};")
edit("Ikon: Astronomi lebih terang", PH,
     "apps[17].color=0x3A5F;", f"apps[17].color={rgb565(120,150,255)};", f"apps[17].color={rgb565(120,150,255)};")
edit("Ikon: Galeri NASA lebih terang", PH,
     "apps[20].color=0x781F;", f"apps[20].color={rgb565(195,110,255)};", f"apps[20].color={rgb565(195,110,255)};")

edit("Ikon: warna app yang tadinya HITAM + yang pudar", PH,
     "  apps[24].color=0x4A1F; // Labirin - biru-ungu spooky (kesan kejar-kejaran/hantu)\n"
     "  apps[25].color=0xC2E2; // Inferno - oranye membara ala lorong neraka (BARU v74)\n"
     "  apps[26].color=0xE1C6; // NES - merah klasik ala kaset NES (BARU v86)\n"
     "}\n",
     f"  apps[24].color={rgb565(140,120,255)}; // Labirin - biru-ungu terang\n"
     f"  apps[25].color={rgb565(255,115,0)}; // Inferno - oranye membara\n"
     f"  apps[26].color={rgb565(240,50,60)}; // NES - merah klasik\n"
     "  // v-baru: index 22 & 27-31 dulu TIDAK PERNAH diisi -> default 0 = HITAM (ikon gak keliatan).\n"
     f"  apps[22].color={rgb565(255,100,115)}; // Mic Level - merah koral\n"
     f"  apps[27].color={rgb565(160,235,40)}; // DOOM - hijau limau\n"
     f"  apps[28].color={rgb565(60,255,205)}; // PPU Demo - mint/aqua\n"
     f"  apps[29].color={rgb565(255,100,70)}; // Pomodoro - tomat\n"
     f"  apps[30].color={rgb565(180,130,255)}; // Wake Word - ungu muda\n"
     f"  apps[31].color={rgb565(255,65,150)}; // Musik - pink terang\n"
     "}\n",
     "apps[31].color=")

# ---------------------------------------------------------------- tulis semua
for path, content in cache.items():
    with open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as f:
        f.write(content)
print("\nSelesai. Compile lagi firmware S3 (phone.ino) DAN flash ulang ESP32-CAM (cam_bt_coprocessor).")
