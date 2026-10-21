// =====================================================================
// chat_renphone.ino -- app "Chat": chat pribadi antar HP lewat broker MQTT (bebas pilih broker)
//
// Tidak ada server buatan sendiri: sebuah broker MQTT (TLS port 8883, login username+password)
// yang jadi "servernya". Tidak terikat ke satu penyedia; contoh gratis: EMQX Cloud Serverless.
// Lihat chat-mqtt/README.md. (HiveMQ Cloud Serverless gratis sudah dihentikan Des 2026.)
//
// SKEMA TOPIK
//   renphone/chat/m/<slot>   slot = seq % 24   -> RING 24 slot, SETIAP pesan dikirim RETAINED
//   payload                  "<seq>|<ts epoch>|<nama>|<pesan>"
//   Tiap HP subscribe renphone/chat/m/+ . Saat tersambung, broker langsung mengirim isi 24
//   slot retained = RIWAYAT 24 pesan terakhir (termasuk yg dikirim waktu app ini tertutup).
//   Pesan live masuk lewat topik yg sama. Urutan & duplikat ditangani lewat nomor seq.
//   seq berikutnya = seq tertinggi yg pernah dilihat + 1. Kalau dua HP kirim di detik yg
//   persis sama bisa bertabrakan di slot yg sama (salah satu hilang) -- jarang utk pemakaian
//   pribadi. Kirim DIBLOKIR ~1,5 dtk setelah tersambung supaya riwayat selesai diterima dulu.
//
// Cara kerja di HP:
//   * Satu task FreeRTOS permanen (stack PSRAM, core 0) mengurus jaringan. Task HANYA
//     tersambung selagi app Chat terbuka (chatActive); app ditutup -> koneksi TLS dilepas
//     supaya RAM internal (~35 KB utk TLS) kembali. UI tidak pernah menunggu jaringan.
//   * Pesan disimpan di PSRAM (40 terakhir) + mutex, UI hanya membaca.
//   * Konfigurasi (broker, username, password, nama) di NVS "chat". Mengetik di keyboard
//     layar itu menyiksa -> bisa juga taruh file /chat.cfg di SD card:
//         host=xxxxxxxx.ala.eu-central-1.emqxsl.com
//         user=namauser
//         pass=passwordnya
//         name=Ren
//
// KEAMANAN: koneksi memakai setInsecure() (sertifikat broker TIDAK diverifikasi), sama spt
// fitur jaringan lain di firmware ini. Buat kredensial khusus HP dengan izin HANYA ke topik
// renphone/chat/# (kalau broker-nya menyediakan ACL). Jangan kirim hal sensitif.
//
// Aturan arduino-cli di proyek ini: fungsi di file ini TIDAK boleh menerima/mengembalikan
// tipe struct/kelas library buatan sendiri (prototype otomatis disisipkan sebelum tipenya
// dikenal) -> PubSubClient hanya dipakai sbg variabel lokal di dalam chatTaskFunc().
// =====================================================================
#include <PubSubClient.h>

#define CHAT_MAX_MSG   40
#define CHAT_SLOTS     24
#define CHAT_USER_LEN  18
#define CHAT_TEXT_LEN  202
#define CHAT_TZ_SEC    (7*3600)      // WIB (UTC+7); ubah kalau beda zona waktu
#define CHAT_TOP       (STATUS_H+22)
#define CHAT_PORT      8883
#define CHAT_TOPIC_BASE "renphone/chat/m/"

struct ChatMsg { uint32_t seq; uint32_t ts; char user[CHAT_USER_LEN]; char text[CHAT_TEXT_LEN]; };

static ChatMsg*          chatMsgs = nullptr;        // PSRAM
static int               chatCount = 0;
static uint32_t          chatLastSeq = 0;
static SemaphoreHandle_t chatMx = nullptr;
static TaskHandle_t      chatTask = nullptr;
static StackType_t*      chatStack = nullptr;
static StaticTask_t      chatTCB;

static String chatHost = "", chatUser = "", chatPass = "", chatName = "", chatInput = "";
static String chatSendQ = "";
static bool   chatCfgLoaded = false;
static bool   chatCfgMode = false;
static int    chatCfgEdit = -1;                     // field yg sedang diketik (-1 = tidak ada)
static int    chatScroll = 0;

static volatile bool     chatActive = false;        // app terbuka & terkonfigurasi
static volatile uint8_t  chatConn = 0;              // 0 menyambung, 1 siap, 2 error
static volatile bool     chatDirty = false;         // ada data baru -> redraw
static volatile bool     chatSendPending = false;
static volatile bool     chatToastFlag = false;
static char              chatErrBuf[44] = "";
static char              chatToastBuf[44] = "";

// ---------------------------------------------------------------------
// konfigurasi
// ---------------------------------------------------------------------
static void chatCfgLoad() {
  Preferences p;
  if (p.begin("chat", false)) {
    chatHost = p.getString("host", "");
    chatUser = p.getString("user", "");
    chatPass = p.getString("pass", "");
    chatName = p.getString("name", "");
    p.end();
  }
  if (!chatName.length()) { chatName = ownerName(); chatName.trim(); }
  if (!chatName.length()) chatName = "HP";
  chatCfgLoaded = true;
}

static void chatCfgSave() {
  chatHost.trim(); chatUser.trim(); chatPass.trim(); chatName.trim();
  if (chatName.length() > 16) chatName = chatName.substring(0, 16);
  if (!chatName.length()) chatName = "HP";
  Preferences p;
  if (p.begin("chat", false)) {
    p.putString("host", chatHost);
    p.putString("user", chatUser);
    p.putString("pass", chatPass);
    p.putString("name", chatName);
    p.end();
  }
}

static bool chatConfigured() { return chatHost.length() > 3 && chatUser.length() > 0 && chatPass.length() > 0; }

static bool chatCfgFromSd() {
  if (!sdReady || !SD_MMC.exists("/chat.cfg")) return false;
  File f = SD_MMC.open("/chat.cfg", FILE_READ);
  if (!f) return false;
  String h = "", u = "", w = "", n = "";
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.startsWith("host="))      h = l.substring(5);
    else if (l.startsWith("user=")) u = l.substring(5);
    else if (l.startsWith("pass=")) w = l.substring(5);
    else if (l.startsWith("name=")) n = l.substring(5);
  }
  f.close();
  h.trim(); u.trim(); w.trim(); n.trim();
  if (!h.length() || !u.length() || !w.length()) return false;
  chatHost = h; chatUser = u; chatPass = w;
  if (n.length()) chatName = n;
  return true;
}

// host bersih: tanpa skema & tanpa "/" / ":port" (port selalu 8883 TLS)
static String chatCleanHost() {
  String h = chatHost;
  h.trim();
  int i = h.indexOf("://");
  if (i >= 0) h = h.substring(i + 3);
  int sl = h.indexOf('/');
  if (sl >= 0) h = h.substring(0, sl);
  int co = h.indexOf(':');
  if (co >= 0) h = h.substring(0, co);
  return h;
}

static void chatSetErr(const char* m) {
  strncpy(chatErrBuf, m, sizeof(chatErrBuf) - 1);
  chatErrBuf[sizeof(chatErrBuf) - 1] = 0;
}

static void chatToast(const char* m) {
  strncpy(chatToastBuf, m, sizeof(chatToastBuf) - 1);
  chatToastBuf[sizeof(chatToastBuf) - 1] = 0;
  chatToastFlag = true;
  chatDirty = true;
}

// ---------------------------------------------------------------------
// pesan masuk (dipanggil dari callback MQTT di task jaringan)
// ---------------------------------------------------------------------
static void chatCopyAscii(char* dst, int dstSize, const char* src, int len) {
  int n = 0;
  for (int i = 0; i < len && n < dstSize - 1; i++) {
    unsigned char c = (unsigned char)src[i];
    dst[n++] = (c >= 32 && c <= 126) ? (char)c : '?';
  }
  dst[n] = 0;
}

// sisipkan urut berdasarkan seq; seq yg sudah ada diabaikan (retained dikirim ulang saat reconnect)
static void chatInsert(uint32_t seq, uint32_t ts, const char* user, int ulen, const char* text, int tlen) {
  if (!chatMx || !chatMsgs || seq == 0) return;
  xSemaphoreTake(chatMx, portMAX_DELAY);
  bool dup = false;
  for (int i = 0; i < chatCount; i++) if (chatMsgs[i].seq == seq) { dup = true; break; }
  if (!dup) {
    int pos = chatCount;
    while (pos > 0 && chatMsgs[pos - 1].seq > seq) pos--;
    bool ok = true;
    if (chatCount >= CHAT_MAX_MSG) {
      if (pos == 0) ok = false;                     // lebih tua dari semua yg kita simpan
      else {
        memmove(chatMsgs, chatMsgs + 1, sizeof(ChatMsg) * (CHAT_MAX_MSG - 1));
        chatCount = CHAT_MAX_MSG - 1;
        pos--;
      }
    }
    if (ok) {
      memmove(chatMsgs + pos + 1, chatMsgs + pos, sizeof(ChatMsg) * (chatCount - pos));
      ChatMsg& m = chatMsgs[pos];
      m.seq = seq; m.ts = ts;
      chatCopyAscii(m.user, CHAT_USER_LEN, user, ulen);
      chatCopyAscii(m.text, CHAT_TEXT_LEN, text, tlen);
      chatCount++;
      if (seq > chatLastSeq) chatLastSeq = seq;
      chatDirty = true;
    }
  }
  xSemaphoreGive(chatMx);
}

// payload "<seq>|<ts>|<nama>|<pesan>" -- pesan boleh mengandung '|'
static void chatOnPayload(const char* payload, unsigned int len) {
  char buf[300];
  if (len == 0 || len >= sizeof(buf)) return;
  memcpy(buf, payload, len);
  buf[len] = 0;
  char* p1 = strchr(buf, '|');
  char* p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
  char* p3 = p2 ? strchr(p2 + 1, '|') : nullptr;
  if (!p1 || !p2 || !p3) return;
  uint32_t seq = (uint32_t)strtoul(buf, NULL, 10);
  uint32_t ts  = (uint32_t)strtoul(p1 + 1, NULL, 10);
  chatInsert(seq, ts, p2 + 1, (int)(p3 - (p2 + 1)), p3 + 1, (int)strlen(p3 + 1));
}

static void chatMqttCb(char* topic, uint8_t* payload, unsigned int len) {
  (void)topic;
  chatOnPayload((const char*)payload, len);
}

// ---------------------------------------------------------------------
// task jaringan
// ---------------------------------------------------------------------
static void chatErrFromState(int st) {
  if (st == 4 || st == 5) chatSetErr("Username/password salah");
  else if (st == -2)      chatSetErr("Tidak bisa menghubungi broker");
  else if (st == 3)       chatSetErr("Broker tidak tersedia");
  else { char b[28]; snprintf(b, sizeof(b), "MQTT error %d", st); chatSetErr(b); }
}

static void chatTaskFunc(void*) {
  for (;;) {
    if (!chatActive) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
    if (WiFi.status() != WL_CONNECTED) {
      chatConn = 2; chatSetErr("WiFi belum terhubung"); chatDirty = true;
      vTaskDelay(pdMS_TO_TICKS(1500)); continue;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 30000) {
      chatConn = 2; chatSetErr("RAM padat, tutup app lain"); chatDirty = true;
      vTaskDelay(pdMS_TO_TICKS(2000)); continue;
    }
    String host = chatCleanHost(), user = chatUser, pass = chatPass;
    chatConn = 0; chatDirty = true;
    int fails = 0;
    {
      WiFiClientSecure net;             // dilepas (buffer TLS) begitu blok ini selesai
      net.setInsecure();
      PubSubClient mqtt(net);
      mqtt.setServer(host.c_str(), CHAT_PORT);
      mqtt.setBufferSize(512);
      mqtt.setKeepAlive(30);
      mqtt.setSocketTimeout(8);
      mqtt.setCallback(chatMqttCb);
      char cid[28];
      snprintf(cid, sizeof(cid), "renphone-%08x", (unsigned)esp_random());
      if (mqtt.connect(cid, user.c_str(), pass.c_str())) {
        mqtt.subscribe(CHAT_TOPIC_BASE "+", 1);
        uint32_t readyAt = millis() + 1500UL;          // beri waktu riwayat retained masuk
        bool ready = false;
        while (chatActive && mqtt.connected() && WiFi.status() == WL_CONNECTED) {
          mqtt.loop();
          if (!ready && (int32_t)(millis() - readyAt) >= 0) {
            ready = true; chatConn = 1; chatErrBuf[0] = 0; chatDirty = true;
          }
          if (ready && chatSendPending) {
            String body = "";
            xSemaphoreTake(chatMx, portMAX_DELAY);
            body = chatSendQ;                            // "<nama>\n<pesan>"
            uint32_t seq = chatLastSeq + 1;
            xSemaphoreGive(chatMx);
            int nl = body.indexOf('\n');
            String nm = nl >= 0 ? body.substring(0, nl) : String("HP");
            String tx = nl >= 0 ? body.substring(nl + 1) : body;
            tx.replace("\n", " ");
            time_t nowT = time(nullptr);
            uint32_t ts = (nowT > 1700000000) ? (uint32_t)nowT : 0;
            char topic[40];
            snprintf(topic, sizeof(topic), CHAT_TOPIC_BASE "%u", (unsigned)(seq % CHAT_SLOTS));
            String payload = String((unsigned long)seq) + "|" + String((unsigned long)ts) + "|" + nm + "|" + tx;
            if (mqtt.publish(topic, (const uint8_t*)payload.c_str(), (unsigned int)payload.length(), true)) {
              // pesan kita sendiri ikut kembali lewat langganan -> chatInsert() yg memasukkannya
              chatSendPending = false;
            } else {
              chatSendPending = false;
              chatToast("Pesan gagal terkirim");
            }
          }
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (chatActive) {
          fails++;
          chatConn = 2;
          if (WiFi.status() != WL_CONNECTED) chatSetErr("WiFi putus");
          else chatSetErr("Koneksi ke broker terputus");
          chatDirty = true;
        }
      } else {
        fails++;
        chatConn = 2;
        chatErrFromState(mqtt.state());
        chatDirty = true;
      }
      mqtt.disconnect();
      net.stop();
    }
    if (chatActive) {
      int waitMs = 3000 * (fails < 5 ? (fails > 0 ? fails : 1) : 5);   // 3..15 dtk
      for (int w = 0; w < waitMs && chatActive; w += 100) vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

static bool chatEnsureInit() {
  if (!chatMx) chatMx = xSemaphoreCreateMutex();
  if (!chatMx) return false;
  if (!chatMsgs) {
    chatMsgs = (ChatMsg*)heap_caps_calloc(CHAT_MAX_MSG, sizeof(ChatMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!chatMsgs) chatMsgs = (ChatMsg*)calloc(CHAT_MAX_MSG, sizeof(ChatMsg));   // cadangan: RAM internal (~9 KB)
    if (!chatMsgs) return false;
  }
  if (!chatCfgLoaded) chatCfgLoad();
  if (!chatTask) {
    BaseType_t r = createTaskPsramStack(chatTaskFunc, "chatTask", 16384, NULL, &chatStack, &chatTCB, &chatTask, 0);
    if (r != pdPASS) { chatTask = nullptr; return false; }
  }
  return true;
}

// ---------------------------------------------------------------------
// siklus hidup app
// ---------------------------------------------------------------------
void chatEnter() {
  if (!chatEnsureInit()) { showToast("RAM tidak cukup utk Chat", 2500); return; }
  if (!chatConfigured() && chatCfgFromSd()) { chatCfgSave(); showToast("Konfigurasi dimuat dari SD", 2200); }
  chatScroll = 0;
  chatCfgEdit = -1;
  chatCfgMode = !chatConfigured();
  chatConn = 0;
  chatErrBuf[0] = 0;
  chatActive = !chatCfgMode;
}

void chatExit() {
  chatActive = false;     // task melepas koneksi TLS sendiri
  chatConn = 0;
}

// dipanggil tiap putaran loop() (lihat apply_chat_patch.py)
void chatLoopPoll() {
  if (chatDirty) {
    chatDirty = false;
    if (curScreen() == SCR_CHAT) needRedraw = true;
  }
}

// ---------------------------------------------------------------------
// gambar
// ---------------------------------------------------------------------
static int chatWrap(const char* t, int maxCh, int* st, int* ln, int maxL) {
  int n = 0, i = 0, L = (int)strlen(t);
  while (i < L && n < maxL) {
    int rem = L - i;
    if (rem <= maxCh) { st[n] = i; ln[n] = rem; n++; break; }
    int cut = maxCh;
    for (int k = maxCh; k > maxCh / 2; k--) { if (t[i + k] == ' ') { cut = k; break; } }
    st[n] = i; ln[n] = cut; n++;
    i += cut;
    while (i < L && t[i] == ' ') i++;
  }
  return n;
}

static int chatMsgHeight(int idx, int maxCh) {
  int st[8], ln[8];
  int n = chatWrap(chatMsgs[idx].text, maxCh, st, ln, 8);
  if (n < 1) n = 1;
  return 9 + n * 9 + 4;
}

static void chatPrintCentered(LGFX_Sprite& s, int x, int w, int y, const char* t) {
  s.setCursor(x + (w - s.textWidth(t)) / 2, y);
  s.print(t);
}

static const char* chatCfgLabel(int i) {
  static const char* L[4] = { "Broker (host MQTT, port TLS 8883)", "Username MQTT", "Password MQTT", "Nama kamu" };
  return L[i];
}

static String* chatCfgField(int i) {
  return i == 0 ? &chatHost : (i == 1 ? &chatUser : (i == 2 ? &chatPass : &chatName));
}

static void chatDrawCfg(LGFX_Sprite& s) {
  s.setTextColor(T().accent); s.setCursor(8, 26); s.print("Chat - Pengaturan");
  if (chatCfgEdit >= 0) {                       // tampilan mengetik: satu field, di atas keyboard
    s.setTextColor(T().subtext); s.setCursor(10, 50); s.print(chatCfgLabel(chatCfgEdit));
    iosRR(s, 6, 62, SCR_W - 12, 22, 5, T().surface);
    String v = *chatCfgField(chatCfgEdit);
    int maxC = (SCR_W - 28) / 6;
    if ((int)v.length() > maxC - 1) v = String("..") + v.substring(v.length() - (maxC - 3));
    v += "_";
    s.setTextColor(T().text); s.setCursor(12, 69); s.print(v.c_str());
    s.setTextColor(T().subtext); s.setCursor(10, 92); s.print("Ketuk area di atas keyboard = selesai");
    return;
  }
  for (int i = 0; i < 4; i++) {
    int ly = 44 + i * 28;
    s.setTextColor(T().subtext); s.setCursor(10, ly); s.print(chatCfgLabel(i));
    iosRR(s, 6, ly + 9, SCR_W - 12, 17, 5, T().surface);
    String v;
    if (i == 2) { for (size_t k = 0; k < chatPass.length(); k++) v += '*'; }
    else v = *chatCfgField(i);
    int maxC = (SCR_W - 28) / 6;
    if ((int)v.length() > maxC) v = String("..") + v.substring(v.length() - (maxC - 2));
    s.setTextColor(v.length() ? T().text : T().subtext);
    s.setCursor(12, ly + 14); s.print(v.length() ? v.c_str() : "ketuk untuk mengisi...");
  }
  int by = 44 + 4 * 28 + 2;
  bool sdCfg = sdReady && SD_MMC.exists("/chat.cfg");
  int saveW = sdCfg ? (SCR_W - 12) * 6 / 10 : (SCR_W - 12);
  iosRR(s, 6, by, saveW, 22, 7, T().accent);
  s.setTextColor(T().bg); chatPrintCentered(s, 6, saveW, by + 7, "Simpan & sambung");
  if (sdCfg) {
    int bx = 6 + saveW + 4, bw = SCR_W - 6 - bx;
    iosRR(s, bx, by, bw, 22, 7, T().surface2);
    s.setTextColor(T().accent); chatPrintCentered(s, bx, bw, by + 7, "Muat SD");
  }
}

void drawChat(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false); s.setTextSize(1);
  if (chatToastFlag) { chatToastFlag = false; showToast(chatToastBuf, 2200); }

  if (!chatMsgs) {
    s.setTextColor(T().danger); s.setCursor(10, 60); s.print("RAM tidak cukup untuk Chat.");
    drawBack(s); drawToast(s);
    return;
  }
  if (chatCfgMode) {
    chatDrawCfg(s);
    if (kbVisible) drawKb(s); else drawBack(s);
    drawToast(s);
    return;
  }

  // ---- header ----
  s.setTextColor(T().accent); s.setCursor(8, 26); s.print("Chat");
  uint16_t dot = (chatConn == 1) ? (uint16_t)T().good : ((chatConn == 2) ? (uint16_t)T().danger : (uint16_t)T().subtext);
  s.fillCircle(46, 30, 3, dot);
  String stt = (chatConn == 1) ? String("terhubung") : ((chatConn == 2) ? String(chatErrBuf) : String("menyambung..."));
  int maxSt = (SCR_W - 54 - 58) / 6;
  if ((int)stt.length() > maxSt) stt = stt.substring(0, maxSt > 2 ? maxSt - 2 : maxSt) + "..";
  s.setTextColor(T().subtext); s.setCursor(54, 26); s.print(stt.c_str());
  iosRR(s, SCR_W - 52, 24, 48, 18, 5, T().surface2);
  s.setTextColor(T().accent); chatPrintCentered(s, SCR_W - 52, 48, 29, "Atur");

  // ---- area pesan ----
  int inputY = (kbVisible ? kbY() : backY()) - 26;
  int top = CHAT_TOP, bot = inputY - 4, areaH = bot - top;
  int maxCh = (SCR_W - 16) / 6;
  if (areaH > 10 && chatMx && xSemaphoreTake(chatMx, pdMS_TO_TICKS(30)) == pdTRUE) {
    int total = 0;
    for (int i = 0; i < chatCount; i++) total += chatMsgHeight(i, maxCh);
    int maxScroll = total > areaH ? total - areaH : 0;
    if (chatScroll > maxScroll) chatScroll = maxScroll;
    if (chatScroll < 0) chatScroll = 0;

    s.setClipRect(0, top, SCR_W, areaH);
    int y = bot + chatScroll;
    for (int i = chatCount - 1; i >= 0; i--) {
      int h = chatMsgHeight(i, maxCh);
      y -= h;
      if (y + h <= top) break;
      if (y >= bot) continue;
      bool mine = (chatName == String(chatMsgs[i].user));
      s.setTextColor(mine ? (uint16_t)T().accent : (uint16_t)T().good);
      s.setCursor(8, y); s.print(chatMsgs[i].user);
      char hm[8];
      if (chatMsgs[i].ts > 0) {
        time_t tt = (time_t)(chatMsgs[i].ts + CHAT_TZ_SEC);
        struct tm tmv; gmtime_r(&tt, &tmv);
        snprintf(hm, sizeof(hm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      } else snprintf(hm, sizeof(hm), "--:--");
      s.setTextColor(T().subtext);
      s.setCursor(8 + s.textWidth(chatMsgs[i].user) + 8, y); s.print(hm);
      int st[8], ln[8];
      int n = chatWrap(chatMsgs[i].text, maxCh, st, ln, 8);
      s.setTextColor(T().text);
      for (int k = 0; k < n; k++) {
        char line[64];
        int L = ln[k] < (int)sizeof(line) - 1 ? ln[k] : (int)sizeof(line) - 1;
        memcpy(line, chatMsgs[i].text + st[k], L); line[L] = 0;
        s.setCursor(8, y + 9 + k * 9); s.print(line);
      }
    }
    if (chatCount == 0) {
      s.setTextColor(T().subtext);
      s.setCursor(10, top + 6); s.print(chatConn == 1 ? "Belum ada pesan. Tulis yang pertama!" : "Menunggu koneksi...");
    }
    s.clearClipRect();
    xSemaphoreGive(chatMx);
  }

  // ---- baris input ----
  int boxW = SCR_W - 4 - 62;
  iosRR(s, 4, inputY, boxW, 22, 5, T().surface);
  bool typing = kbVisible && kbTarget == &chatInput;
  String shown = chatInput;
  int maxIn = (boxW - 12) / 6;
  if ((int)shown.length() > maxIn) shown = String("..") + shown.substring(shown.length() - (maxIn - 2));
  if (typing) shown += "_";
  s.setTextColor(chatInput.length() || typing ? T().text : T().subtext);
  s.setCursor(10, inputY + 7); s.print(shown.length() ? shown.c_str() : "Ketik pesan...");
  bool canSend = chatInput.length() > 0 && !chatSendPending && chatConn == 1;
  iosRR(s, SCR_W - 56, inputY, 52, 22, 5, canSend ? (uint16_t)T().accent : (uint16_t)T().surface2);
  s.setTextColor(canSend ? (uint16_t)T().bg : (uint16_t)T().subtext);
  chatPrintCentered(s, SCR_W - 56, 52, inputY + 7, chatSendPending ? "..." : "Kirim");

  if (kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuhan
// ---------------------------------------------------------------------
static void chatStartTyping(String* target) {
  kbTarget = target;
  kbVisible = true;
  kbMode = KB_LOWER;
  needRedraw = true;
}

static void chatDoSend() {
  String t = chatInput;
  t.trim();
  if (!t.length()) return;
  if (chatConn != 1) { showToast("Belum tersambung ke broker", 1800); return; }
  if (chatSendPending) { showToast("Tunggu pesan sebelumnya...", 1400); return; }
  if (t.length() > 200) t = t.substring(0, 200);
  t.replace("|", "/");                       // '|' dipakai sbg pemisah payload
  xSemaphoreTake(chatMx, portMAX_DELAY);
  chatSendQ = chatName + "\n" + t;
  xSemaphoreGive(chatMx);
  chatSendPending = true;
  chatInput = "";
  chatScroll = 0;
  needRedraw = true;
}

static void chatCfgFinish() {
  chatCfgSave();
  chatCfgMode = !chatConfigured();
  chatActive = !chatCfgMode;
  chatConn = 0;
  needRedraw = true;
}

void chatTouch(int x, int y, bool held, bool isNew) {
  static int dragY = 0;
  static bool dragging = false;
  if (!chatMsgs) { if (isNew && isBack(x, y)) navBack(); return; }

  if (kbVisible) {
    if (!isNew) return;
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; chatCfgEdit = -1; }
    else kbTouch(x, y);
    needRedraw = true;
    return;
  }

  if (chatCfgMode) {
    if (!isNew) return;
    if (isBack(x, y)) { if (chatConfigured()) chatCfgSave(); navBack(); return; }
    for (int i = 0; i < 4; i++) {
      int ly = 44 + i * 28;
      if (y >= ly + 7 && y <= ly + 27) {
        chatActive = false;                       // hentikan sesi selagi kredensial diubah
        chatCfgEdit = i;
        chatStartTyping(chatCfgField(i));
        return;
      }
    }
    int by = 44 + 4 * 28 + 2;
    if (y >= by && y <= by + 22) {
      bool sdCfg = sdReady && SD_MMC.exists("/chat.cfg");
      int saveW = sdCfg ? (SCR_W - 12) * 6 / 10 : (SCR_W - 12);
      if (x >= 6 && x <= 6 + saveW) {
        if (!chatConfigured()) { showToast("Isi broker, username & password dulu", 2200); return; }
        chatCfgFinish();
      } else if (sdCfg && x > 6 + saveW) {
        if (chatCfgFromSd()) { chatCfgSave(); showToast("Dimuat dari /chat.cfg", 1800); }
        else showToast("/chat.cfg tidak valid (butuh host=, user=, pass=)", 2800);
        needRedraw = true;
      }
    }
    return;
  }

  int inputY = backY() - 26;
  int top = CHAT_TOP, bot = inputY - 4;
  if (isNew) {
    dragging = false;
    if (isBack(x, y)) { navBack(); return; }
    if (x >= SCR_W - 52 && x <= SCR_W - 4 && y >= 24 && y <= 42) {
      chatActive = false;
      chatCfgMode = true;
      chatCfgEdit = -1;
      needRedraw = true;
      return;
    }
    if (y >= inputY && y <= inputY + 22) {
      if (x >= SCR_W - 56) chatDoSend();
      else chatStartTyping(&chatInput);
      return;
    }
    if (y >= top && y <= bot) { dragging = true; dragY = y; }
  } else if (held && dragging) {
    int dy = y - dragY;
    if (dy != 0) { chatScroll += dy; if (chatScroll < 0) chatScroll = 0; dragY = y; needRedraw = true; }
  }
}
