// ==== GEM BEGIN ====
static bool          chsGemEngWhite = true;      // mesin memegang putih?
static bool          chsGemPaused = false;
static volatile uint8_t chsGemSt = 0;            // 0 diam | 1 permintaan ke Gemini berjalan | 2 balasan siap diproses
static volatile bool chsGemOk = false;
static uint32_t      chsGemGen = 0;              // naik tiap partai baru / berhenti: balasan lama diabaikan
static uint32_t      chsGemReqGen = 0;
static String        chsGemPrompt, chsGemReply;
static uint8_t       chsGemBad = 0;              // balasan tak sah beruntun utk langkah ini
static char          chsGemBadMv[14] = "";
static unsigned long chsGemReqMs = 0, chsGemDueMs = 0, chsGemSecMs = 0;
static bool          chsGemQueued = false, chsGemRec = false;
static char          chsGemMsg[48] = "";
static uint16_t      chsGemStat[5][4];           // per level: mesin menang | Gemini menang | seri | Gemini kalah krn tak sah

static void chsGemSetMsg(const char* a, const char* b) {
  size_t k = 0;
  for (; a && a[k] && k < sizeof(chsGemMsg) - 1; k++) chsGemMsg[k] = a[k];
  for (size_t j = 0; b && b[j] && k < sizeof(chsGemMsg) - 1; j++) { char c = b[j]; chsGemMsg[k++] = (c == '\n' || c == '\r') ? ' ' : c; }
  chsGemMsg[k] = 0;
}

static void chsGemBuildPrompt() {
  char fen[104], legal[1200];
  chsFen(fen, sizeof(fen), (int)chsPly);
  chsLegalList(legal, sizeof(legal));
  const char* hist = chsMoves ? chsMoves : "";
  size_t hl = strlen(hist);
  if (hl > 400) { hist += hl - 400; while (*hist && *hist != ' ') hist++; if (*hist == ' ') hist++; }   // 400 huruf terakhir, mulai di batas langkah
  String p;
  p.reserve(1700);
  p = "You are playing a chess game as ";
  p += chsWhite ? "White" : "Black";
  p += " against a chess engine. Choose the strongest move.\n";
  p += "Position (FEN): "; p += fen; p += "\n";
  p += "Recent moves (UCI): "; p += (*hist ? hist : "(game start)"); p += "\n";
  p += "Legal moves (UCI): "; p += legal; p += "\n";
  if (chsGemBad) { p += "Your previous answer \""; p += chsGemBadMv; p += "\" was NOT in the legal moves list. "; }
  p += "Reply with exactly ONE move copied from the legal moves list, in UCI notation (for example e2e4 or e7e8q), and nothing else.";
  chsGemPrompt = p;
}

static void chsGemFinish() {                      // catat skor sekali per partai
  if (chsGemRec) return;
  chsGemRec = true;
  int r = chsResultForMe();                       // sudut pandang MESIN (chsRole = kursi mesin)
  int lv = chsAiLevel > 4 ? 4 : chsAiLevel;
  if (r == 1)       { if (chsGemStat[lv][0] < 65535) chsGemStat[lv][0]++; if (chsGemForfeit && chsGemStat[lv][3] < 65535) chsGemStat[lv][3]++; }
  else if (r == -1) { if (chsGemStat[lv][1] < 65535) chsGemStat[lv][1]++; }
  else if (r == 0)  { if (chsGemStat[lv][2] < 65535) chsGemStat[lv][2]++; }
  chsGemStatSave();
  needRedraw = true;
}

static void chsGemPoll() {
  if (!chsGem) return;
  if (chsEnd != 0) { chsGemFinish(); return; }
  if (chsGemSt == 2) {                            // balasan (atau galat) dari task Gemini
    chsGemSt = 0;
    if (chsGemReqGen != chsGemGen) return;        // dari partai/sesi lama
    if (!chsGemOk) {
      chsGemPaused = true;
      chsGemSetMsg("Gemini gagal: ", chsGemReply.c_str());
      needRedraw = true;
      return;
    }
    uint16_t m;
    if (chsPickFromReply(chsGemReply.c_str(), &m)) {
      chsGemBad = 0; chsGemMsg[0] = 0;
      chsCommitEx(m, false);
      chsGemDueMs = millis() + 450UL;
      return;
    }
    chsGemBad++;
    {                                             // simpan sebagian balasan utk ditegur di prompt berikutnya
      size_t k = 0; const char* r = chsGemReply.c_str();
      for (; r[k] && k < sizeof(chsGemBadMv) - 1; k++) { char c = r[k]; chsGemBadMv[k] = (c == '"' || c == '\n' || c == '\r') ? ' ' : c; }
      chsGemBadMv[k] = 0;
    }
    if (chsGemBad >= 3) {                         // 3x berturut-turut: Gemini kalah
      strcpy(chsRes, chsWhite ? "RW" : "RB");
      chsGemForfeit = true;
      chsUpdateEnd();
    } else chsGemDueMs = millis() + 400UL;
    needRedraw = true;
    return;
  }
  if (chsGemSt == 1) {                            // menunggu: segarkan hitungan detik tiap 1 dtk
    if (millis() - chsGemSecMs >= 1000UL) { chsGemSecMs = millis(); needRedraw = true; }
    return;
  }
  if (chsGemPaused || chsAnimOn || chsPromoOn) return;
  if ((long)(millis() - chsGemDueMs) < 0) return;
  if (chsWhite == chsGemEngWhite) {               // giliran mesin
    if (!chsGemQueued) { chsGemQueued = true; chsGemDueMs = millis() + 450UL; needRedraw = true; return; }
    chsGemQueued = false;
    uint16_t m = chsAiMoveFor(chsAiLevel);
    if (m) chsCommitEx(m, false);
    chsGemDueMs = millis();
    return;
  }
  chsGemBuildPrompt();                            // giliran Gemini
  if (!chsGemLaunch()) {
    chsGemPaused = true;
    chsGemSetMsg(WiFi.status() == WL_CONNECTED ? "Gagal membuat task Gemini" : "WiFi belum terhubung", "");
    needRedraw = true;
  }
}
// ==== GEM END ====
