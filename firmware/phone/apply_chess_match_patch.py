#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_chess_match_patch.py -- app Catur: (1) MATCHMAKING "Cari Lawan", (2) mode "2 Pemain" (pass & play, 1 HP gantian,
# tanpa internet). Kode room yang lama TIDAK diubah (tombol Acak + Mulai tetap).   Marker: CHESS_MATCH_V1
#
# Jalankan dari folder yang berisi chess_renphone.ino (firmware/phone):
#     python3 apply_chess_match_patch.py
# Semua-atau-tidak-sama-sekali: kalau satu jangkar tidak ketemu persis 1 kali, TIDAK ada file yang diubah. Idempoten.
#
# MATCHMAKING (tanpa server baru, pakai broker Catur yang sama):
#   renphone/chess/_lobby/q        RETAINED "<pid>|<nama>"  = slot 1 orang yang sedang menunggu
#   renphone/chess/_lobby/m/<pid>  RETAINED "<room>"        = pesan "kamu sudah dipasangkan, masuk room ini"
#   * Pemain pertama -> menaruh dirinya di slot q (Last Will payload kosong: kalau putus, slot bersih sendiri) lalu menunggu.
#   * Pemain kedua  -> melihat slot terisi -> membuat room acak 8 karakter ("m"+7), mengirimnya ke m/<pid penunggu>,
#     mengosongkan slot, lalu keduanya pindah ke alur room biasa (kursi, langkah, retained -- tidak ada yang berubah).
#   * Dua orang menekan bersamaan: pid yang lebih kecil bertindak sebagai "penunggu", yang lain mengalah dan bergabung.
#   * Tidak ada lawan 90 detik -> berhenti, tampil "Tidak ada lawan". Tombol Menu = batal (slot dikosongkan).
#   ACL broker: renphone/chess/# sudah mencakup _lobby (tidak perlu izin baru).
import sys, os

F = 'chess_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini (jalankan dari firmware/phone)" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'CHESS_MATCH_V1' in s:
    print("Sudah terpasang (CHESS_MATCH_V1), dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: jangkar '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# ---------------------------------------------------------------- 1. state
s = rep(s,
"static bool          chsVsAI = false;          // true = lawan AI (tanpa jaringan)\n",
"static bool          chsVsAI = false;          // true = lawan AI (tanpa jaringan)\n"
"static bool          chsHot = false;           // CHESS_MATCH_V1: 2 pemain 1 HP gantian (chsVsAI ikut true, tanpa AI & tanpa jaringan; kursi chsRole = 1)\n"
"static volatile uint8_t chsMatch = 0;          // CHESS_MATCH_V1: 1 = sedang mencari lawan (lobi), 0 = tidak\n",
"state")

# ---------------------------------------------------------------- 2. mesin lobi + hook di task jaringan
LOBBY = r'''
// ==== MATCHMAKING BEGIN (CHESS_MATCH_V1) ====
#define CHS_LOBBY_Q "renphone/chess/_lobby/q"
static char chsMqSlot[48] = "";   static bool chsMqSlotF = false;
static char chsMqRoom[12] = "";   static bool chsMqRoomF = false;
static char chsMqMyM[48]  = "";
static bool chsMqRoomOk(const char* r) {          // 3..8 karakter [a-z0-9]
  size_t n = strlen(r);
  if (n < 3 || n > 8) return false;
  for (size_t i = 0; i < n; i++) { char c = r[i]; if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false; }
  return true;
}
static bool chsMqSlotPid(char* out) {              // ambil pid 8 hex dari isi slot "<pid>|<nama>"
  if (strlen(chsMqSlot) < 9 || chsMqSlot[8] != '|') return false;
  memcpy(out, chsMqSlot, 8); out[8] = 0;
  return true;
}
static void chsMatchCb(char* topic, uint8_t* pl, unsigned int len) {   // jalan di task jaringan, dari dalam mqtt.loop() -> tanpa mutex
  if (!strcmp(topic, CHS_LOBBY_Q)) {
    unsigned n = len > 46 ? 46 : len; memcpy(chsMqSlot, pl, n); chsMqSlot[n] = 0; chsMqSlotF = true;
  } else if (!strcmp(topic, chsMqMyM)) {
    if (len >= 3 && len <= 10) { memcpy(chsMqRoom, pl, len); chsMqRoom[len] = 0; chsMqRoomF = true; }
  }
}
// return: 1 = dapat room (chsRoom terisi) | 2 = dibatalkan | 3 = tidak ada lawan (berhenti) | 0 = gagal sementara (diulang)
static int chsMatchRun(const char* host, int port, const char* user, const char* pass) {
  WiFiClientSecure net;
  net.setInsecure();
  PubSubClient mqtt(net);
  mqtt.setServer(host, port);
  mqtt.setBufferSize(256);
  mqtt.setKeepAlive(20);
  mqtt.setSocketTimeout(8);
  mqtt.setCallback(chsMatchCb);
  char cid[28];
  snprintf(cid, sizeof(cid), "rpmatch-%08x", (unsigned)esp_random());
  snprintf(chsMqMyM, sizeof(chsMqMyM), "renphone/chess/_lobby/m/%s", chsPid);
  chsMqSlotF = false; chsMqRoomF = false; chsMqSlot[0] = 0; chsMqRoom[0] = 0;
  if (!mqtt.connect(cid, user, pass, CHS_LOBBY_Q, 1, true, "")) {     // Last Will payload kosong + retained = slot dibersihkan broker kalau kita putus
    chsErrFromState(mqtt.state()); chsDirty = true;
    return 0;
  }
  mqtt.subscribe(CHS_LOBBY_Q, 1);
  mqtt.subscribe(chsMqMyM, 1);
  chsConn = 1; chsErr[0] = 0; chsDirty = true;
  static const char al[] = "abcdefghjkmnpqrstuvwxyz23456789";
  bool published = false;
  int result = 0;
  uint32_t t0 = millis(), tPub = 0;
  char other[9] = "";
  char nm[24];
  strncpy(nm, chatName.c_str(), sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
  for (char* q = nm; *q; q++) if (*q == '|') *q = '/';
  for (;;) {
    if (!chsActive || chsMatch != 1) { result = 2; break; }
    if (!mqtt.connected() || WiFi.status() != WL_CONNECTED) { chsSetErr("Koneksi lobi terputus"); chsDirty = true; result = 0; break; }
    mqtt.loop();
    if (chsMqRoomF && chsMqRoomOk(chsMqRoom)) {                         // ada yang menemukan kita
      strncpy(chsRoom, chsMqRoom, 9); chsRoom[9] = 0;
      mqtt.publish(chsMqMyM, (const uint8_t*)"", 0, true);              // hapus pesan retained milik kita
      result = 1; break;
    }
    chsMqRoomF = false;
    bool joinOther = false;
    if (!published) {
      if ((int32_t)(millis() - t0) >= 1500) {                           // retained slot sudah sempat masuk
        if (chsMqSlotF && chsMqSlotPid(other) && strcmp(other, chsPid) != 0) joinOther = true;
        else {
          char pl[40]; snprintf(pl, sizeof(pl), "%s|%s", chsPid, nm);
          mqtt.publish(CHS_LOBBY_Q, (const uint8_t*)pl, (unsigned)strlen(pl), true);
          published = true; tPub = millis();
        }
      }
    } else {
      if (chsMqSlotF && chsMqSlotPid(other) && strcmp(other, chsPid) != 0 && strcmp(other, chsPid) < 0) joinOther = true;   // dua penunggu bersamaan: pid lebih kecil jadi penunggu
      if ((int32_t)(millis() - tPub) > 90000) { result = 3; break; }
    }
    if (joinOther) {
      char room[10]; room[0] = 'm';
      for (int i = 1; i < 8; i++) room[i] = al[esp_random() % (sizeof(al) - 1)];
      room[8] = 0;
      char t[56]; snprintf(t, sizeof(t), "renphone/chess/_lobby/m/%s", other);
      mqtt.publish(t, (const uint8_t*)room, 8, true);
      mqtt.publish(CHS_LOBBY_Q, (const uint8_t*)"", 0, true);           // slot kosong lagi
      strncpy(chsRoom, room, 9); chsRoom[9] = 0;
      result = 1; break;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (mqtt.connected()) {
    if (published && result != 1) mqtt.publish(CHS_LOBBY_Q, (const uint8_t*)"", 0, true);   // batal / habis waktu -> bersihkan slot
    for (int i = 0; i < 5; i++) { mqtt.loop(); vTaskDelay(pdMS_TO_TICKS(30)); }
  }
  mqtt.disconnect();
  net.stop();
  return result;
}
// ==== MATCHMAKING END ====

static void chsTaskFunc(void*) {
'''
s = rep(s, "\nstatic void chsTaskFunc(void*) {\n", LOBBY, "chsTaskFunc")

s = rep(s,
"    String user = chsBUser, pass = chsBPass;\n    String base = String(\"renphone/chess/\") + chsRoom + \"/\";\n",
"    String user = chsBUser, pass = chsBPass;\n"
"    if (chsMatch == 1) {                                  // CHESS_MATCH_V1: cari lawan lewat lobi dulu, baru sambung ke room hasilnya\n"
"      chsConn = 0; chsReady = false; chsDirty = true;\n"
"      int mr = chsMatchRun(host.c_str(), port, user.c_str(), pass.c_str());\n"
"      if (mr == 1) { chsMatch = 0; chsConn = 0; chsErr[0] = 0; chsDirty = true; continue; }          // room terisi -> iterasi berikut = alur room biasa\n"
"      if (mr == 2) { chsMatch = 0; chsConn = 0; chsDirty = true; continue; }                           // dibatalkan\n"
"      if (mr == 3) { chsMatch = 0; chsActive = false; chsConn = 2; chsSetErr(\"Tidak ada lawan. Coba lagi\"); chsDirty = true; continue; }\n"
"      chsConn = 2; chsDirty = true;                                                                    // gagal sementara: ulangi pelan-pelan\n"
"      for (int w = 0; w < 3000 && chsActive && chsMatch == 1; w += 100) vTaskDelay(pdMS_TO_TICKS(100));\n"
"      continue;\n"
"    }\n"
"    String base = String(\"renphone/chess/\") + chsRoom + \"/\";\n",
"hook task")

# ---------------------------------------------------------------- 3. pass & play: aturan giliran / kursi
s = rep(s,
"chsOppPid()[0] && chsEnd == 0 && ((chsRole == 1) == chsWhite);",
"chsOppPid()[0] && chsEnd == 0 && (chsHot || ((chsRole == 1) == chsWhite));",
"chsCanMove")
s = rep(s,
"  bool mine = p && ((p > 0) == (chsRole == 1));\n",
"  bool mine = p && ((p > 0) == (chsHot ? chsWhite : (chsRole == 1)));     // 2 pemain: bidak milik pihak yang sedang jalan\n",
"chsTapBoard mine")
s = rep(s,
"static void chsCommit(uint16_t m) { chsCommitEx(m, true); }",
"static void chsCommit(uint16_t m) { chsCommitEx(m, !chsHot); }",
"chsCommit")
s = rep(s,
"  strcpy(chsRes, chsRole == 1 ? \"RW\" : \"RB\");\n",
"  strcpy(chsRes, (chsHot ? chsWhite : (chsRole == 1)) ? \"RW\" : \"RB\");\n",
"chsResign")
s = rep(s,
"static void chsRematch() {\n  if (chsVsAI) {",
"static void chsRematch() {\n  if (chsHot) { chsNewGameLocal(); needRedraw = true; return; }\n  if (chsVsAI) {",
"chsRematch")
s = rep(s,
"static bool chsAiTurn() { return chsVsAI && chsEnd == 0",
"static bool chsAiTurn() { return chsVsAI && !chsHot && chsEnd == 0",
"chsAiTurn")
s = rep(s,
"  if (!chsVsAI || chsEnd != 0 || chsAnimOn || chsAiLevel > 4",
"  if (!chsVsAI || chsHot || chsEnd != 0 || chsAnimOn || chsAiLevel > 4",
"chsAiPonder")

# ---------------------------------------------------------------- 4. fungsi mulai (2 pemain & cari lawan)
s = rep(s,
"// Mulai partai lawan AI (tanpa broker / jaringan)\n",
"""// CHESS_MATCH_V1: 2 pemain 1 HP gantian (tanpa broker / jaringan / AI)
static void chsStartHot() {
  if (!chsMoves) { showToast(RPTR("RAM tidak cukup utk Catur"), 2200); return; }
  chsActive = false; chsMatch = 0;
  chsVsAI = true; chsHot = true; chsGem = false; chsAfkAI = false; chsAiQueued = false;
  chsRole = 1;                                                    // papan selalu Putih di bawah
  strcpy(chsWPid, chsPid); strcpy(chsBPid, "hot");
  strcpy(chsWName, "Putih"); strcpy(chsBName, "Hitam");
  chsGenNo = 0; chsNewGameLocal();
  chsClaimW = chsClaimB = false; chsRemoteGen = 0; chsRemotePly = 0; chsRemoteRes[0] = 0;
  chsSendG = false; chsPubSeat = 0;
  chsReady = true; chsConn = 1; chsErr[0] = 0;
  chsLastTouchMs = millis();
  chsScr = 1;
  needRedraw = true;
}

// CHESS_MATCH_V1: cari lawan acak (lobi MQTT, lalu masuk room otomatis)
static void chsStartMatch() {
  if (!chsMoves) { showToast(RPTR("RAM tidak cukup utk Catur"), 2200); return; }
  if (!chsBrokerOk() && chsBcWait) { showToast(RPTR("Mengambil broker Catur..."), 1800); return; }
  if (!chsBrokerOk()) { showToast(RPTR("Atur broker Catur dulu"), 2400); return; }
  if (WiFi.status() != WL_CONNECTED) { showToast(RPTR("WiFi belum tersambung"), 2000); return; }
  chsRoom[0] = 0;
  chsVsAI = false; chsHot = false; chsGem = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false; chsLastTouchMs = millis(); chsTurnStartMs = millis();
  chsGenNo = 0; chsNewGameLocal();
  chsRole = 0; chsClaimW = chsClaimB = false; chsClaimMs = 0;
  chsWPid[0] = chsBPid[0] = chsWName[0] = chsBName[0] = 0;
  chsRemoteGen = 0; chsRemotePly = 0; chsRemoteRes[0] = 0;
  for (int i = 0; i < 4; i++) { chsOnPid[i][0] = 0; chsOnVal[i] = 0; }
  chsRxWf = chsRxBf = chsRxGf = false; chsSendG = false; chsPubSeat = 0;
  chsReady = false; chsConn = 0; chsErr[0] = 0;
  chsMatch = 1;
  chsScr = 1;
  chsActive = true;
  needRedraw = true;
}

// Mulai partai lawan AI (tanpa broker / jaringan)
""", "start funcs")

# reset chsHot/chsMatch di tiap mode lain
s = rep(s,
"  chsVsAI = false; chsGem = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false; chsLastTouchMs = millis(); chsTurnStartMs = millis();\n  chsGenNo = 0; chsNewGameLocal();\n  chsRole = 0; chsClaimW",
"  chsVsAI = false; chsHot = false; chsMatch = 0; chsGem = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false; chsLastTouchMs = millis(); chsTurnStartMs = millis();\n  chsGenNo = 0; chsNewGameLocal();\n  chsRole = 0; chsClaimW",
"reset di chsStartGame")
s = rep(s,
"  chsVsAI = true; chsGem = false; chsAfkAI = false; chsAiQueued = false;\n  chsAiSeats();",
"  chsVsAI = true; chsHot = false; chsMatch = 0; chsGem = false; chsAfkAI = false; chsAiQueued = false;\n  chsAiSeats();",
"reset di chsStartAi")
s = rep(s,
"  chsVsAI = true; chsGem = true; chsAfkAI = false; chsAiQueued = false;\n",
"  chsVsAI = true; chsHot = false; chsMatch = 0; chsGem = true; chsAfkAI = false; chsAiQueued = false;\n",
"reset di chsStartGem")
s = rep(s,
"  chsActive = false; chsConn = 0; chsReady = false;\n  chsScr = 0; chsSel = -1;",
"  chsActive = false; chsConn = 0; chsReady = false; chsMatch = 0; chsHot = false;\n  chsScr = 0; chsSel = -1;",
"reset di chessEnter")
s = rep(s,
"  chsActive = false;        // task melepas koneksi TLS sendiri\n",
"  chsActive = false;        // task melepas koneksi TLS sendiri\n  chsMatch = 0; chsHot = false;\n",
"reset di chessExit")
s = rep(s,
"    chsGemStop();\n    chsVsAI = false; chsAfkAI = false; chsAiQueued = false;\n    chsStyleSave();",
"    chsGemStop();\n    chsMatch = 0; chsHot = false;\n    chsVsAI = false; chsAfkAI = false; chsAiQueued = false;\n    chsStyleSave();",
"tombol Menu")

# ---------------------------------------------------------------- 5. teks status / panel
s = rep(s,
"  else if (chsVsAI) {\n    if (chsEnd)                       { t = chsEndShort();",
"  else if (chsHot) {\n"
"    if (chsEnd)                       { t = chsEndShort(); c = T().good; }\n"
"    else if (chsInCheck())            { t = chsWhite ? \"SKAK! Putih jalan\" : \"SKAK! Hitam jalan\"; c = T().danger; }\n"
"    else                              { t = chsWhite ? \"Giliran Putih\" : \"Giliran Hitam\"; c = T().good; }\n"
"  }\n"
"  else if (chsVsAI) {\n    if (chsEnd)                       { t = chsEndShort();",
"status hot")
s = rep(s,
"  else if (chsConn == 2)              { chsClip(chsErr[0] ? chsErr : \"Terputus\", eb, 46); t = eb; c = T().danger; }\n",
"  else if (chsConn == 2)              { chsClip(chsErr[0] ? chsErr : \"Terputus\", eb, 46); t = eb; c = T().danger; }\n"
"  else if (chsMatch)                  { t = \"Mencari lawan...\"; c = T().accent; }\n",
"status match")
s = rep(s,
"  if (chsGem) return r == 1 ? \"Mesin menang!\"",
"  if (chsHot && chsEnd) {\n    if ((chsEnd >= 2 && chsEnd <= 5) || chsEnd == 7) return \"Seri\";\n    bool ww = (chsEnd == 1) ? !chsWhite : (chsRes[1] == 'B');\n    return ww ? \"Putih menang!\" : \"Hitam menang!\";\n  }\n"
"  if (chsGem) return r == 1 ? \"Mesin menang!\"",
"endshort hot")
s = rep(s,
"  else snprintf(nb, sizeof(nb), \"Kamu : %s\", myN[0] ? myN : (chatName.length() ? chatName.c_str() : \"HP\"));",
"  else if (chsHot) snprintf(nb, sizeof(nb), \"Putih (bawah)\");\n"
"  else snprintf(nb, sizeof(nb), \"Kamu : %s\", myN[0] ? myN : (chatName.length() ? chatName.c_str() : \"HP\"));",
"nama saya hot")
s = rep(s,
"  if (chsVsAI) snprintf(nb, sizeof(nb), \"Lawan: %s\", opN);\n",
"  if (chsHot) snprintf(nb, sizeof(nb), \"Hitam (atas)\");\n  else if (chsVsAI) snprintf(nb, sizeof(nb), \"Lawan: %s\", opN);\n",
"nama lawan hot")
s = rep(s,
"  else if (chsVsAI) snprintf(nb, sizeof(nb), \"Lawan AI  #%u %s\", (unsigned)chsPly, lt);\n",
"  else if (chsHot) snprintf(nb, sizeof(nb), \"2 Pemain  #%u %s\", (unsigned)chsPly, lt);\n"
"  else if (chsVsAI) snprintf(nb, sizeof(nb), \"Lawan AI  #%u %s\", (unsigned)chsPly, lt);\n"
"  else if (chsMatch) snprintf(nb, sizeof(nb), \"Mencari lawan...\");\n",
"baris room")

# ---------------------------------------------------------------- 6. layar setup (gambar + sentuh)
s = rep(s,
"""  iosRR(s, 8, 134, 70, 26, 7, chsPressCol(3, T().surface2));
  s.setTextColor(T().accent); chsPrintC(s, 8, 70, 143, RPTR("Acak"));
  bool ok = chsBrokerOk() && chsRoomStr.length() >= 3;
  iosRR(s, 86, 134, SCR_W - 94, 26, 7, chsPressCol(4, ok ? (uint16_t)T().accent : (uint16_t)T().surface2));
  s.setTextColor(ok ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, 86, SCR_W - 94, 143, RPTR("Mulai"));
""",
"""  int aw = 52, w2 = (SCR_W - 16 - aw - 12) / 2, xm = 8 + aw + 6, xc = xm + w2 + 6;     // CHESS_MATCH_V1: Acak | Mulai (room) | Cari Lawan
  iosRR(s, 8, 134, aw, 26, 7, chsPressCol(3, T().surface2));
  s.setTextColor(T().accent); chsPrintC(s, 8, aw, 143, RPTR("Acak"));
  bool ok = chsBrokerOk() && chsRoomStr.length() >= 3;
  iosRR(s, xm, 134, w2, 26, 7, chsPressCol(4, ok ? (uint16_t)T().accent : (uint16_t)T().surface2));
  s.setTextColor(ok ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, xm, w2, 143, RPTR("Mulai"));
  bool okm = chsBrokerOk();
  iosRR(s, xc, 134, SCR_W - 8 - xc, 26, 7, chsPressCol(12, okm ? (uint16_t)T().accent : (uint16_t)T().surface2));
  s.setTextColor(okm ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, xc, SCR_W - 8 - xc, 143, RPTR("Cari Lawan"));
""", "setup draw row1")

s = rep(s,
"""    int gw = (SCR_W - 16 - 12) / 3, x0 = 8, x1 = 8 + gw + 6, x2 = 8 + 2 * (gw + 6), by2 = 166;
    iosRR(s, x0, by2, gw, 24, 7, chsPressCol(6, (uint16_t)T().accent));
    s.setTextColor(T().bg); chsPrintC(s, x0, gw, by2 + 8, RPTR("Lawan AI"));
    iosRR(s, x1, by2, gw, 24, 7, chsPressCol(7, (uint16_t)T().surface2));
    s.setTextColor(T().accent); chsPrintC(s, x1, gw, by2 + 8, chsLvName[chsAiLevel]);
    iosRR(s, x2, by2, gw, 24, 7, chsPressCol(8, (uint16_t)T().surface2));
    char ab[14];""",
"""    int gw = (SCR_W - 16 - 18) / 4, x0 = 8, x1 = 8 + gw + 6, x3 = 8 + 2 * (gw + 6), x2 = 8 + 3 * (gw + 6), by2 = 166;   // CHESS_MATCH_V1: 4 tombol (x3 = 2 Pemain)
    iosRR(s, x0, by2, gw, 24, 7, chsPressCol(6, (uint16_t)T().accent));
    s.setTextColor(T().bg); chsPrintC(s, x0, gw, by2 + 8, RPTR("Lawan AI"));
    iosRR(s, x1, by2, gw, 24, 7, chsPressCol(7, (uint16_t)T().surface2));
    s.setTextColor(T().accent); chsPrintC(s, x1, gw, by2 + 8, chsLvName[chsAiLevel]);
    iosRR(s, x3, by2, gw, 24, 7, chsPressCol(11, (uint16_t)T().surface2));
    s.setTextColor(T().accent); chsPrintC(s, x3, gw, by2 + 8, RPTR("2 Pemain"));
    iosRR(s, x2, by2, gw, 24, 7, chsPressCol(8, (uint16_t)T().surface2));
    char ab[14];""", "setup draw row AI")

s = rep(s,
"""  if (y >= 134 && y <= 160) {
    if (x >= 8 && x <= 78) { chsPress(3); chsRandomRoom(); needRedraw = true; }
    else if (x >= 86) { chsPress(4); chsStartGame(); }
    return;
  }""",
"""  if (y >= 134 && y <= 160) {
    int aw = 52, w2 = (SCR_W - 16 - aw - 12) / 2, xm = 8 + aw + 6, xc = xm + w2 + 6;                     // CHESS_MATCH_V1: sama dgn geometri gambar
    if (x >= 8 && x < 8 + aw + 3) { chsPress(3); chsRandomRoom(); needRedraw = true; }
    else if (x >= xm - 3 && x < xc - 3) { chsPress(4); chsStartGame(); }
    else if (x >= xc - 3) { chsPress(12); chsStartMatch(); }
    return;
  }""", "setup touch row1")

s = rep(s,
"""    int gw = (SCR_W - 16 - 12) / 3;
    if (x >= 8 && x < 8 + gw) { chsPress(6); chsStartAi(); }
    else if (x >= 8 + gw + 6 && x < 8 + 2 * gw + 6) { chsPress(7); chsAiLevel = (uint8_t)((chsAiLevel + 1) % 5); chsPrefsSaveCfg(); needRedraw = true; }
    else if (x >= 8 + 2 * (gw + 6)) { chsPress(8); chsAfkIdx = (uint8_t)((chsAfkIdx + 1) & 3); chsPrefsSaveCfg(); needRedraw = true; }""",
"""    int gw = (SCR_W - 16 - 18) / 4;                                                                     // CHESS_MATCH_V1: 4 tombol
    if (x >= 8 && x < 8 + gw + 3) { chsPress(6); chsStartAi(); }
    else if (x >= 8 + gw + 3 && x < 8 + 2 * gw + 9) { chsPress(7); chsAiLevel = (uint8_t)((chsAiLevel + 1) % 5); chsPrefsSaveCfg(); needRedraw = true; }
    else if (x >= 8 + 2 * gw + 9 && x < 8 + 3 * gw + 15) { chsPress(11); chsStartHot(); }
    else if (x >= 8 + 3 * gw + 15) { chsPress(8); chsAfkIdx = (uint8_t)((chsAfkIdx + 1) & 3); chsPrefsSaveCfg(); needRedraw = true; }""",
"setup touch row AI")

s = rep(s,
'  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(10, 46); s.print(RPTR("Online: kode room sama. Atau lawan AI"));',
'  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(10, 46); s.print(RPTR("Kode room / Cari Lawan / AI / 2 Pemain"));',
"subjudul")

open(F, 'w', encoding='utf-8').write(s)
print("OK: Catur -> Cari Lawan (matchmaking) + 2 Pemain (1 HP gantian) terpasang (CHESS_MATCH_V1).")
print("Build seperti biasa. Kode room lama tidak berubah.")
