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
//   renphone/chat/t          indikator "sedang mengetik": payload = "<nama>", TIDAK retained, QoS 0.
//                            Dikirim paling sering tiap 2,5 dtk selagi user mengetik; penerima menampilkan
//                            titik-titik 4,5 dtk sejak paket terakhir (HP lama yg tak langganan topik ini tetap normal).
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
#define CHAT_TOPIC_TYPING "renphone/chat/t"
#define CHAT_TYPE_SHOW_MS  4500UL     // indikator mengetik bertahan segini sejak paket terakhir
#define CHAT_TYPE_SEND_GAP 2500UL     // jeda minimum antar paket "mengetik" yg kita kirim
#define CHAT_ANIM_MS       280UL      // animasi pesan live baru masuk
// ukuran bubble
#define CB_PADX     8
#define CB_PADT     5
#define CB_LINE     10
#define CB_TIMEH    9
#define CB_PADB     3
#define CB_NAMEH    11
#define CB_GAP_SAME 3
#define CB_GAP_NEW  8
#define CB_QUOTE_H  20                // tinggi kotak kutipan balasan di dalam bubble
#define CB_TYPE_H   26                // tinggi slot indikator mengetik (bubble 20 + celah)

struct ChatMsg { uint32_t seq; uint32_t ts; char user[CHAT_USER_LEN]; char text[CHAT_TEXT_LEN]; uint32_t born; };   // born = millis() saat pesan LIVE masuk (0 = riwayat, tanpa animasi)

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
// indikator "sedang mengetik"
// status pesan yg KITA kirim: bubble sementara "Mengirim..." -> hilang digantikan bubble asli (+tanda terkirim)
// begitu pesannya kembali dari broker; kalau gagal/10 dtk tak kembali -> "Gagal" + ketuk untuk kirim ulang.
static volatile uint8_t  chatOutState = 0;                   // 0 tidak ada, 1 mengirim, 2 gagal
static char              chatOutText[CHAT_TEXT_LEN] = "";
static char              chatOutName[CHAT_USER_LEN] = "";
static uint32_t          chatOutMs = 0;
static int               chatOutX = 0, chatOutY = 0, chatOutW = 0, chatOutH = 0;   // area bubble (utk ketuk kirim ulang)
// BALAS: tahan bubble -> bar "Membalas ..." di atas kotak input. Format di kabel: "{r:Nama:kutipan}isi pesan"
// (HP berfirmware lama menampilkannya apa adanya; yg baru menggambar kutipan di dalam bubble).
static bool              chatReplyOn = false;
static char              chatReplyName[CHAT_USER_LEN] = "";
static char              chatReplyQuote[30] = "";
static int               chatHitN = 0, chatHitR[24][4], chatHitIdx[24];   // area bubble yg tampil (utk tahan-balas)
static char              chatPeerName[CHAT_USER_LEN] = "";   // diisi callback MQTT (di bawah chatMx), dibaca UI
static volatile uint32_t chatPeerTypeUntil = 0;              // millis() sampai kapan indikator tampil (0 = mati)
static char              chatTypeNameBuf[CHAT_USER_LEN] = "";// nama yg dikirim task jaringan
static volatile bool     chatTypeSendReq = false;
static uint32_t          chatTypeSentMs = 0;
static float             chatTypeAnim = 0.0f;                // 0..1: tinggi slot indikator (muncul/hilang halus)

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
      m.born = (chatConn == 1) ? millis() : 0;          // riwayat (sebelum "siap") tidak dianimasikan
      if (chatConn == 1 && m.born == 0) m.born = 1;
      if (chatOutState != 0 && strcmp(chatOutName, m.user) == 0 && strcmp(chatOutText, m.text) == 0) chatOutState = 0;   // pesan kita sudah kembali dari broker
      if (strcmp(chatPeerName, m.user) == 0) chatPeerTypeUntil = 0;   // pengirimnya sudah kirim pesan -> titik-titik hilang
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

static void chatOnTyping(const char* payload, unsigned int len) {
  if (!chatMx || len == 0 || len > 60) return;
  char nm[CHAT_USER_LEN];
  chatCopyAscii(nm, sizeof(nm), payload, (int)len);
  xSemaphoreTake(chatMx, portMAX_DELAY);
  strncpy(chatPeerName, nm, sizeof(chatPeerName) - 1);
  chatPeerName[sizeof(chatPeerName) - 1] = 0;
  chatPeerTypeUntil = millis() + CHAT_TYPE_SHOW_MS;
  if (chatPeerTypeUntil == 0) chatPeerTypeUntil = 1;
  xSemaphoreGive(chatMx);
  chatDirty = true;
}

static void chatMqttCb(char* topic, uint8_t* payload, unsigned int len) {
  if (topic && strcmp(topic, CHAT_TOPIC_TYPING) == 0) { chatOnTyping((const char*)payload, len); return; }
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
        mqtt.subscribe(CHAT_TOPIC_TYPING, 0);
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
              chatOutState = 2;
              chatToast("Pesan gagal terkirim");
            }
          }
          if (ready && chatTypeSendReq) {
            chatTypeSendReq = false;
            mqtt.publish(CHAT_TOPIC_TYPING, chatTypeNameBuf);      // QoS 0, tidak retained
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
  chatOutState = 0; chatReplyOn = false;
  chatPeerTypeUntil = 0; chatPeerName[0] = 0; chatTypeAnim = 0.0f; chatTypeSentMs = 0; chatTypeSendReq = false;
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
static float chatEaseOut(float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float q = 1.0f - t;
  return 1.0f - q * q * q;
}

// ada orang LAIN yg sedang mengetik? (nama sama dgn kita = gema paket kita sendiri -> diabaikan)
static bool chatPeerTyping() {
  uint32_t until = chatPeerTypeUntil;
  if (until == 0 || (int32_t)(until - millis()) <= 0) return false;
  return strcmp(chatPeerName, chatName.c_str()) != 0;
}

// pesan live terbaru masih dalam animasi masuk?
static bool chatNewestAnimating() {
  if (!chatMx || !chatMsgs) return false;
  bool r = false;
  if (xSemaphoreTake(chatMx, 0) == pdTRUE) {
    if (chatCount > 0) {
      uint32_t b = chatMsgs[chatCount - 1].born;
      r = (b != 0 && (uint32_t)(millis() - b) < CHAT_ANIM_MS + 40UL);
    }
    xSemaphoreGive(chatMx);
  }
  return r;
}

void chatLoopPoll() {
  if (chatDirty) {
    chatDirty = false;
    if (curScreen() == SCR_CHAT) needRedraw = true;
  }
  if (curScreen() != SCR_CHAT || chatCfgMode || !chatMsgs) { chatTypeAnim = 0.0f; return; }
  if (chatOutState == 1 && millis() - chatOutMs > 10000UL) { chatOutState = 2; needRedraw = true; }   // tak kembali dari broker
  // animasi: indikator mengetik (muncul/hilang + titik memantul) & pesan baru masuk, ~30 fps
  static unsigned long lastMs = 0;
  unsigned long now = millis();
  if (now - lastMs < 30) return;
  float dt = (float)(now - lastMs);
  if (dt > 100.0f) dt = 100.0f;
  lastMs = now;
  bool want = chatPeerTyping();
  float tgt = want ? 1.0f : 0.0f;
  bool moving = false;
  if (chatTypeAnim != tgt) {
    float step = dt / 180.0f;
    if (chatTypeAnim < tgt) { chatTypeAnim += step; if (chatTypeAnim > tgt) chatTypeAnim = tgt; }
    else                    { chatTypeAnim -= step; if (chatTypeAnim < tgt) chatTypeAnim = tgt; }
    moving = true;
  }
  if (want || moving || chatNewestAnimating()) needRedraw = true;
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

// "{r:Nama:kutipan}isi" -> nama, kutipan, penunjuk ke isi. false (body = text) kalau bukan balasan.
static bool chatParseReply(const char* text, char* qn, int qnCap, char* qt, int qtCap, const char** body) {
  *body = text;
  if (strncmp(text, "{r:", 3) != 0) return false;
  const char* e = strchr(text + 3, '}');
  if (!e) return false;
  const char* col = (const char*)memchr(text + 3, ':', (size_t)(e - (text + 3)));
  if (!col) return false;
  int nl = (int)(col - (text + 3)); if (nl > qnCap - 1) nl = qnCap - 1;
  memcpy(qn, text + 3, (size_t)nl); qn[nl] = 0;
  int tl = (int)(e - (col + 1)); if (tl > qtCap - 1) tl = qtCap - 1;
  memcpy(qt, col + 1, (size_t)tl); qt[tl] = 0;
  const char* b = e + 1;
  while (*b == ' ') b++;
  *body = b;
  return true;
}

// Tata letak satu pesan sbg bubble. Mengembalikan tinggi slot (celah + nama + bubble).
// Out: jumlah baris, panjang baris terpanjang (karakter), celah di atas, tampilkan nama?
static int chatMsgLayout(int idx, int maxCh, int* nLines, int* maxLen, int* gap, bool* showName) {
  int st[8], ln[8];
  char qn[20], qt[32]; const char* body;
  bool hq = chatParseReply(chatMsgs[idx].text, qn, sizeof(qn), qt, sizeof(qt), &body);
  int n = chatWrap(body, maxCh, st, ln, 8);
  int ml = 0;
  if (n < 1) n = 1;
  else for (int k = 0; k < n; k++) if (ln[k] > ml) ml = ln[k];
  if (hq) { int ql = (int)strlen(qn) + 2 + (int)strlen(qt); if (ql > maxCh) ql = maxCh; if (ql > ml) ml = ql; }
  bool mine = strcmp(chatName.c_str(), chatMsgs[idx].user) == 0;
  bool newGroup = (idx == 0) || strcmp(chatMsgs[idx].user, chatMsgs[idx - 1].user) != 0;
  bool sn = !mine && newGroup;
  int g = newGroup ? CB_GAP_NEW : CB_GAP_SAME;
  if (nLines)   *nLines = n;
  if (maxLen)   *maxLen = ml;
  if (gap)      *gap = g;
  if (showName) *showName = sn;
  return g + (sn ? CB_NAMEH : 0) + (hq ? CB_QUOTE_H : 0) + CB_PADT + n * CB_LINE + CB_TIMEH + CB_PADB;
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

  // ---- area pesan (bubble) ----
  int inputY = (kbVisible ? kbY() : backY()) - 26;
  int top = CHAT_TOP, bot = inputY - 4 - (chatReplyOn ? 18 : 0), areaH = bot - top;
  chatHitN = 0;
  int maxBW = SCR_W * 3 / 4;
  int maxCh = (maxBW - 2 * CB_PADX) / 6;
  if (areaH > 10 && chatMx && xSemaphoreTake(chatMx, pdMS_TO_TICKS(30)) == pdTRUE) {
    int typeSlot = (int)lroundf((float)CB_TYPE_H * chatEaseOut(chatTypeAnim));
    // bubble "sedang dikirim / gagal" milik kita: slot tambahan paling bawah di atas indikator mengetik
    int outN = 0, outMl = 0, outH = 0, outGap = CB_GAP_NEW;
    if (chatOutState != 0) {
      int ost[8], oln[8];
      char oqn[20], oqt[32]; const char* obody;
      chatParseReply(chatOutText, oqn, sizeof(oqn), oqt, sizeof(oqt), &obody);
      outN = chatWrap(obody, maxCh, ost, oln, 8);
      if (outN < 1) outN = 1;
      else for (int k = 0; k < outN; k++) if (oln[k] > outMl) outMl = oln[k];
      if (chatCount > 0 && strcmp(chatMsgs[chatCount - 1].user, chatName.c_str()) == 0) outGap = CB_GAP_SAME;
      outH = outGap + CB_PADT + outN * CB_LINE + CB_TIMEH + CB_PADB;
    }
    int total = typeSlot + outH;
    for (int i = 0; i < chatCount; i++) total += chatMsgLayout(i, maxCh, 0, 0, 0, 0);
    int maxScroll = total > areaH ? total - areaH : 0;
    if (chatScroll > maxScroll) chatScroll = maxScroll;
    if (chatScroll < 0) chatScroll = 0;

    // pesan live baru: seluruh daftar "didorong naik" halus (digeser turun sebesar tinggi pesan lalu
    // mengendap ke 0), bubble barunya meluncur masuk dari tepi kanan/kiri. Hanya kalau user di dasar.
    int pushOff = 0, newX = 0;
    if (chatCount > 0 && chatScroll == 0) {
      uint32_t b0 = chatMsgs[chatCount - 1].born;
      uint32_t age = (b0 != 0) ? (uint32_t)(millis() - b0) : 0xFFFFFFFFUL;
      if (age < CHAT_ANIM_MS) {
        float e = chatEaseOut((float)age / (float)CHAT_ANIM_MS);
        int hNew = chatMsgLayout(chatCount - 1, maxCh, 0, 0, 0, 0);
        pushOff = (int)lroundf((float)hNew * (1.0f - e));
        newX = (int)lroundf(28.0f * (1.0f - e));
      }
    }

    s.setClipRect(0, top, SCR_W, areaH);
    int y = bot + chatScroll + pushOff;

    // indikator "sedang mengetik": bubble kecil dgn 3 titik memantul, meluncur naik dari bawah
    if (typeSlot > 4) {
      int bx = 6, by = y - CB_TYPE_H + 3 + (CB_TYPE_H - typeSlot);
      s.fillRoundRect(bx, by, 46, 20, 9, T().surface2);
      s.fillRect(bx, by + 11, 9, 9, T().surface2);
      float ph = (float)millis() / 170.0f;
      for (int k = 0; k < 3; k++) {
        float bn = sinf(ph - (float)k * 0.9f);
        if (bn < 0.0f) bn = 0.0f;
        int dy = (int)lroundf(3.0f * bn);
        uint16_t dc = blend565(T().subtext, T().text, (uint8_t)(255.0f * bn));
        s.fillCircle(bx + 14 + k * 10, by + 10 - dy, 3, dc);
      }
      char who[CHAT_USER_LEN + 12];
      snprintf(who, sizeof(who), "%s mengetik", chatPeerName[0] ? chatPeerName : "Seseorang");
      s.setTextColor(T().subtext);
      s.setCursor(bx + 46 + 6, by + 6); s.print(who);
    }
    y -= typeSlot;

    if (outH > 0) {
      y -= outH;
      uint8_t os = chatOutState;
      int minW = (os == 2) ? 26 * 6 : 11 * 6;
      int bw = (outMl * 6 > minW ? outMl * 6 : minW) + 2 * CB_PADX;
      int bh = CB_PADT + outN * CB_LINE + CB_TIMEH + CB_PADB;
      int bx = SCR_W - 6 - bw, by = y + outGap;
      uint16_t bc = (os == 2) ? blend565(T().surface2, T().danger, 150) : blend565(T().accent, T().bg, 90);   // pucat = belum terkirim
      s.fillRoundRect(bx, by, bw, bh, 9, bc);
      s.fillRect(bx + bw - 9, by + bh - 9, 9, 9, bc);
      if (os == 2) s.drawRoundRect(bx, by, bw, bh, 9, T().danger);
      int ost[8], oln[8];
      char oqn2[20], oqt2[32]; const char* obody2;
      chatParseReply(chatOutText, oqn2, sizeof(oqn2), oqt2, sizeof(oqt2), &obody2);
      int nn = chatWrap(obody2, maxCh, ost, oln, 8);
      s.setTextColor(T().bg);
      for (int k = 0; k < nn; k++) {
        char line[64];
        int L = oln[k] < (int)sizeof(line) - 1 ? oln[k] : (int)sizeof(line) - 1;
        memcpy(line, obody2 + ost[k], L); line[L] = 0;
        s.setCursor(bx + CB_PADX, by + CB_PADT + k * CB_LINE); s.print(line);
      }
      const char* st = (os == 2) ? "Gagal - ketuk utk kirim ulang" : "Mengirim...";
      s.setTextColor(os == 2 ? (uint16_t)T().danger : blend565(T().bg, T().accent, 90));
      s.setCursor(bx + bw - CB_PADX - s.textWidth(st), by + CB_PADT + outN * CB_LINE); s.print(st);
      chatOutX = bx; chatOutY = by; chatOutW = bw; chatOutH = bh;
    } else { chatOutW = 0; chatOutH = 0; }

    for (int i = chatCount - 1; i >= 0; i--) {
      int n, ml, gap; bool showName;
      int h = chatMsgLayout(i, maxCh, &n, &ml, &gap, &showName);
      y -= h;
      if (y + h <= top) break;
      if (y >= bot) continue;
      bool mine = (strcmp(chatName.c_str(), chatMsgs[i].user) == 0);
      int minIn = mine ? 42 : 30;
      int bw = (ml * 6 > minIn ? ml * 6 : minIn) + 2 * CB_PADX;
      char qn[20], qt[32]; const char* body;
      bool hq = chatParseReply(chatMsgs[i].text, qn, sizeof(qn), qt, sizeof(qt), &body);
      int qo = hq ? CB_QUOTE_H : 0;
      int bh = qo + CB_PADT + n * CB_LINE + CB_TIMEH + CB_PADB;
      int bx = mine ? (SCR_W - 6 - bw) : 6;
      if (i == chatCount - 1) bx += mine ? newX : -newX;
      int by = y + gap + (showName ? CB_NAMEH : 0);
      uint16_t bc = mine ? (uint16_t)T().accent : (uint16_t)T().surface2;
      uint16_t tc = mine ? (uint16_t)T().bg : (uint16_t)T().text;
      if (showName) {
        s.setTextColor(T().good);
        s.setCursor(bx + 4, y + gap); s.print(chatMsgs[i].user);
      }
      s.fillRoundRect(bx, by, bw, bh, 9, bc);
      if (mine) s.fillRect(bx + bw - 9, by + bh - 9, 9, 9, bc);   // sudut "ekor" bubble sendiri
      else      s.fillRect(bx, by + bh - 9, 9, 9, bc);            // sudut "ekor" bubble lawan bicara
      if (hq) {                                          // kotak kutipan balasan
        uint16_t qbg = mine ? blend565(T().accent, T().bg, 70) : blend565(T().surface2, T().bg, 70);
        s.fillRoundRect(bx + 5, by + 4, bw - 10, CB_QUOTE_H - 3, 4, qbg);
        s.fillRect(bx + 5, by + 4, 2, CB_QUOTE_H - 3, mine ? (uint16_t)T().bg : (uint16_t)T().accent);
        char ql[60]; int qmax = (bw - 10 - 8) / 6;
        snprintf(ql, sizeof(ql), "%s: %s", qn, qt);
        if ((int)strlen(ql) > qmax && qmax > 3) { ql[qmax - 2] = 0; strcat(ql, ".."); }
        s.setTextColor(mine ? (uint16_t)T().bg : (uint16_t)T().subtext);
        s.setCursor(bx + 10, by + 8); s.print(ql);
      }
      if (chatHitN < 24) { chatHitR[chatHitN][0] = bx; chatHitR[chatHitN][1] = by; chatHitR[chatHitN][2] = bw; chatHitR[chatHitN][3] = bh; chatHitIdx[chatHitN] = i; chatHitN++; }
      int st[8], ln[8];
      int nn = chatWrap(body, maxCh, st, ln, 8);
      s.setTextColor(tc);
      for (int k = 0; k < nn; k++) {
        char line[64];
        int L = ln[k] < (int)sizeof(line) - 1 ? ln[k] : (int)sizeof(line) - 1;
        memcpy(line, body + st[k], L); line[L] = 0;
        s.setCursor(bx + CB_PADX, by + CB_PADT + qo + k * CB_LINE); s.print(line);
      }
      char hm[8];
      if (chatMsgs[i].ts > 0) {
        time_t tt = (time_t)(chatMsgs[i].ts + CHAT_TZ_SEC);
        struct tm tmv; gmtime_r(&tt, &tmv);
        snprintf(hm, sizeof(hm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      } else snprintf(hm, sizeof(hm), "--:--");
      s.setTextColor(mine ? blend565(T().bg, T().accent, 90) : (uint16_t)T().subtext);
      s.setCursor(bx + bw - CB_PADX - s.textWidth(hm), by + CB_PADT + qo + n * CB_LINE); s.print(hm);
      if (mine) {                                       // tanda terkirim: sudah diterima broker (pesan kembali dari langganan)
        int kx = bx + bw - CB_PADX - s.textWidth(hm) - 11, ky = by + CB_PADT + qo + n * CB_LINE + 3;
        uint16_t kc = blend565(T().bg, T().accent, 60);
        s.drawLine(kx, ky + 2, kx + 2, ky + 4, kc); s.drawLine(kx + 2, ky + 4, kx + 7, ky - 1, kc);
      }
    }
    if (chatCount == 0) {
      s.setTextColor(T().subtext);
      s.setCursor(10, top + 6); s.print(chatConn == 1 ? "Belum ada pesan. Tulis yang pertama!" : "Menunggu koneksi...");
    }
    s.clearClipRect();
    xSemaphoreGive(chatMx);
  }

  if (chatReplyOn) {                                      // bar "Membalas ..." di atas kotak input
    int ry = inputY - 18;
    s.fillRoundRect(4, ry, SCR_W - 8, 16, 5, T().surface2);
    s.fillRect(8, ry + 3, 2, 10, T().accent);
    char rl[72]; int rmax = (SCR_W - 52) / 6; if (rmax > 70) rmax = 70;
    snprintf(rl, sizeof(rl), "Membalas %s: %s", chatReplyName, chatReplyQuote);
    if ((int)strlen(rl) > rmax && rmax > 3) { rl[rmax - 2] = 0; strcat(rl, ".."); }
    s.setTextColor(T().subtext); s.setCursor(14, ry + 4); s.print(rl);
    s.setTextColor(T().danger); s.setCursor(SCR_W - 18, ry + 4); s.print("x");
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

// dipanggil tiap isi kotak input berubah (diketik/dihapus): minta task jaringan kirim paket "mengetik"
static void chatNoteTyping() {
  if (chatConn != 1 || chatName.length() == 0 || chatInput.length() == 0) return;
  uint32_t now = millis();
  if (chatTypeSentMs != 0 && now - chatTypeSentMs < CHAT_TYPE_SEND_GAP) return;
  chatTypeSentMs = now;
  strncpy(chatTypeNameBuf, chatName.c_str(), sizeof(chatTypeNameBuf) - 1);
  chatTypeNameBuf[sizeof(chatTypeNameBuf) - 1] = 0;
  chatTypeSendReq = true;
}

static void chatDoSend() {
  String t = chatInput;
  t.trim();
  if (!t.length()) return;
  if (chatConn != 1) { showToast("Belum tersambung ke broker", 1800); return; }
  if (chatSendPending) { showToast("Tunggu pesan sebelumnya...", 1400); return; }
  t.replace("|", "/");                       // '|' dipakai sbg pemisah payload
  if (chatReplyOn) {                         // balasan: awali dgn "{r:Nama:kutipan}"
    String pf = String("{r:") + chatReplyName + ":" + chatReplyQuote + "}";
    pf.replace("|", "/");
    int room = 200 - (int)pf.length();
    if (room < 10) { pf = ""; room = 200; }
    if ((int)t.length() > room) t = t.substring(0, room);
    t = pf + t;
    chatReplyOn = false;
  }
  if (t.length() > 200) t = t.substring(0, 200);
  xSemaphoreTake(chatMx, portMAX_DELAY);
  chatSendQ = chatName + "\n" + t;
  chatCopyAscii(chatOutText, CHAT_TEXT_LEN, t.c_str(), (int)t.length());     // salinan yg sudah disanitasi = persis yg nanti kembali
  chatCopyAscii(chatOutName, CHAT_USER_LEN, chatName.c_str(), (int)chatName.length());
  chatOutState = 1; chatOutMs = millis();
  xSemaphoreGive(chatMx);
  chatSendPending = true;
  chatInput = "";
  chatScroll = 0;
  needRedraw = true;
}

// ketuk bubble "Gagal" -> kirim ulang
static void chatResend() {
  if (chatOutState != 2) return;
  if (chatConn != 1) { showToast("Belum tersambung ke broker", 1800); return; }
  if (chatSendPending) { showToast("Tunggu pesan sebelumnya...", 1400); return; }
  xSemaphoreTake(chatMx, portMAX_DELAY);
  chatSendQ = String(chatOutName) + "\n" + String(chatOutText);
  xSemaphoreGive(chatMx);
  chatSendPending = true;
  chatOutState = 1; chatOutMs = millis();
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
    if (chatReplyOn && !chatCfgMode && x >= SCR_W - 30 && y >= kbY() - 26 - 20 && y < kbY() - 26 - 2) { chatReplyOn = false; needRedraw = true; return; }
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; chatCfgEdit = -1; }
    else {
      size_t before = chatInput.length();
      kbTouch(x, y);
      if (!chatCfgMode && chatInput.length() != before) chatNoteTyping();
    }
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
  int top = CHAT_TOP, bot = inputY - 4 - (chatReplyOn ? 18 : 0);
  static unsigned long lpStart = 0; static int lpX = 0, lpY = 0; static bool lpFired = false;
  if (isNew) {
    dragging = false;
    lpStart = millis(); lpX = x; lpY = y; lpFired = false;
    if (chatReplyOn && x >= SCR_W - 30 && y >= inputY - 20 && y < inputY - 2) { chatReplyOn = false; needRedraw = true; return; }
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
    if (chatOutState == 2 && x >= chatOutX && x <= chatOutX + chatOutW && y >= chatOutY && y <= chatOutY + chatOutH) { chatResend(); return; }
    if (y >= top && y <= bot) { dragging = true; dragY = y; }
  } else if (held && dragging) {
    if (!lpFired && abs(x - lpX) < 8 && abs(y - lpY) < 8 && millis() - lpStart > 450UL) {   // tahan bubble = balas
      lpFired = true;
      for (int k = 0; k < chatHitN; k++) {
        if (x >= chatHitR[k][0] && x <= chatHitR[k][0] + chatHitR[k][2] && y >= chatHitR[k][1] && y <= chatHitR[k][1] + chatHitR[k][3]) {
          int mi = chatHitIdx[k];
          if (xSemaphoreTake(chatMx, pdMS_TO_TICKS(30)) == pdTRUE) {
            if (mi >= 0 && mi < chatCount) {
              char qn[20], qt[32]; const char* body;
              const char* who = chatMsgs[mi].user;
              if (chatParseReply(chatMsgs[mi].text, qn, sizeof(qn), qt, sizeof(qt), &body)) { /* body = isi tanpa kutipan lama */ }
              strncpy(chatReplyName, who, sizeof(chatReplyName) - 1); chatReplyName[sizeof(chatReplyName) - 1] = 0;
              for (char* q = chatReplyName; *q; q++) if (*q == ':' || *q == '}' || *q == '{') *q = '_';
              strncpy(chatReplyQuote, body, sizeof(chatReplyQuote) - 1); chatReplyQuote[sizeof(chatReplyQuote) - 1] = 0;
              for (char* q = chatReplyQuote; *q; q++) if (*q == '}' || *q == '{' || *q == '|') *q = ' ';
              chatReplyOn = true;
            }
            xSemaphoreGive(chatMx);
            vibTap(); dragging = false; needRedraw = true;
          }
          break;
        }
      }
      if (!dragging) return;
    }
    int dy = y - dragY;
    if (dy != 0) { chatScroll += dy; if (chatScroll < 0) chatScroll = 0; dragY = y; needRedraw = true; }
  }
}
