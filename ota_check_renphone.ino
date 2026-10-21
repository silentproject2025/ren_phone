// =====================================================================
// ota_check_renphone.ino -- Update firmware: CHANNEL (branch) + PENGAMAN
//
// ALUR
//   1. Di app Update, pill channel (mis. "chat") memilih BRANCH sumber firmware.
//      Tombol "+" menambah branch lain (ketik nama), tombol panah-balik
//      mengembalikan firmware ke slot OTA sebelumnya.
//   2. "Cek update" membaca  releases/download/channel-<branch>/info.txt
//      (file kecil, dibuat otomatis oleh workflow tiap build branch itu).
//   3. info.txt berisi: branch, build, base, size, md5, sha. HP menolak kalau
//      base tidak sama (lihat RP_BASE_REV) -> firmware dari "basis kode" lain
//      TIDAK PERNAH dipasang. Kalau lolos, kolom URL terisi link firmware.bin.
//   4. "Unduh & Pasang" (alur konfirmasi yg sudah ada) memeriksa lagi:
//        - ukuran file = size di info.txt
//        - header image = image ESP32-S3 yg valid (magic 0xE9, chip id 9,
//          magic app_desc 0xABCD5432)
//        - MD5 seluruh file = md5 di info.txt (Update.setMD5)
//      Salah satu gagal -> proses dibatalkan, firmware lama TIDAK tersentuh.
//
// PENGAMAN BOOT (otaGuard*)
//   Setelah update sukses, firmware baru ditandai "pending". Tiap boot sambil
//   pending menaikkan penghitung; kalau firmware baru belum "sehat"
//   (loop() hidup 30 detik) sampai penghitung > 3 -> otomatis kembali ke slot
//   OTA sebelumnya (firmware lama yg sudah terbukti jalan) dan restart.
//   Catatan: pengaman ini baru aktif untuk update YANG DILAKUKAN oleh firmware
//   yg sudah memuat kode ini.
//
// RP_BASE_REV -- NAIKKAN angka ini (di SEMUA branch yg masih mau saling tukar
//   firmware) hanya kalau ada perubahan yg bikin firmware lama/baru tidak
//   bisa saling gantikan (skema partisi, board lain, format NVS tak kompatibel).
//   Workflow menolak membuat channel kalau base-nya beda dgn branch
//   remote-control, dan HP menolak memasang channel dgn base beda.
//
// Catatan: repo harus PUBLIC supaya HP bisa unduh tanpa login.
// =====================================================================
#include <Preferences.h>
#include <esp_ota_ops.h>

#ifndef RP_BUILD_NUM
#define RP_BUILD_NUM 0          // build lokal / non-CI
#endif
// nama branch disuntik workflow lewat -DRP_BRANCH=<nama-bersih>; lokal = "local"
#define RP_STR2(x) #x
#define RP_STR(x)  RP_STR2(x)
#ifndef RP_BRANCH
#define RP_BRANCH local
#endif
// versi "basis kode" -- lihat penjelasan di atas. HARUS satu baris, hanya angka.
#define RP_BASE_REV 1

#define OTA_REPO "silentproject2025/ren_phone"
#define OTA_DEFAULT_CHANNEL "remote-control"
#define OTA_GUARD_MAX_TRIES 3          // boot ke-4 yg belum sehat -> rollback
#define OTA_GUARD_OK_MS     30000UL    // loop() hidup selama ini = firmware dianggap sehat
#define OTA_CUSTOM_MAX      6

static int    otaLatestNum = -1;
static bool   otaCheckedOk = false;
static int    otaStateVal  = 0;        // 1 sudah terbaru, 2 ada update/pindah branch

// ---- harapan untuk unduhan yg URL-nya diisi oleh "Cek update" ----
static String otaExpUrl = "";
static String otaExpMd5 = "";
static int    otaExpSize = 0;

// =====================================================================
// CHANNEL (branch) -- daftar, pilihan, tambah
// =====================================================================
static const char* const OTA_BUILTIN[] = { "remote-control", "chat" };
#define OTA_BUILTIN_N 2

static String otaSel = "";             // channel terpilih (sudah bersih)
static String otaCustom = "";          // daftar branch tambahan, dipisah koma
static bool   otaChanLoaded = false;
static String otaNewBranch = "";
static bool   otaNewBranchPending = false;

// samakan PERSIS dgn workflow:  tr -c 'A-Za-z0-9_-' '-'
static String otaSanitize(String s) {
  s.trim();
  String o = "";
  for (size_t i = 0; i < s.length() && o.length() < 40; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    o += ok ? c : '-';
  }
  return o;
}

static void otaChanLoad() {
  if (otaChanLoaded) return;
  Preferences p;
  if (p.begin("otachan", false)) {
    otaSel    = p.getString("sel", OTA_DEFAULT_CHANNEL);
    otaCustom = p.getString("cus", "");
    p.end();
  } else {
    otaSel = OTA_DEFAULT_CHANNEL;
  }
  otaSel = otaSanitize(otaSel);
  if (!otaSel.length()) otaSel = OTA_DEFAULT_CHANNEL;
  otaChanLoaded = true;
}

static void otaChanSave() {
  Preferences p;
  if (p.begin("otachan", false)) {
    p.putString("sel", otaSel);
    p.putString("cus", otaCustom);
    p.end();
  }
}

static int otaCustomCount() {
  if (!otaCustom.length()) return 0;
  int n = 1;
  for (size_t i = 0; i < otaCustom.length(); i++) if (otaCustom[i] == ',') n++;
  return n;
}

static String otaCustomAt(int idx) {
  int cur = 0; size_t start = 0;
  for (size_t i = 0; i <= otaCustom.length(); i++) {
    if (i == otaCustom.length() || otaCustom[i] == ',') {
      if (cur == idx) return otaCustom.substring(start, i);
      cur++; start = i + 1;
    }
  }
  return "";
}

static int otaChanTotal() { return OTA_BUILTIN_N + otaCustomCount(); }

static String otaChanAt(int i) {
  if (i < OTA_BUILTIN_N) return String(OTA_BUILTIN[i]);
  return otaCustomAt(i - OTA_BUILTIN_N);
}

static int otaChanIndexOf(const String& name) {
  int n = otaChanTotal();
  for (int i = 0; i < n; i++) if (otaChanAt(i) == name) return i;
  return -1;
}

static void otaResetCheck() {
  otaCheckedOk = false; otaStateVal = 0; otaLatestNum = -1;
  otaExpUrl = ""; otaExpMd5 = ""; otaExpSize = 0;
}

String otaBranchName()  { return String(RP_STR(RP_BRANCH)); }   // branch firmware yg SEDANG jalan
String otaChannelName() { otaChanLoad(); return otaSel; }       // branch yg dipilih utk diunduh

void otaChannelCycle() {
  otaChanLoad();
  int n = otaChanTotal();
  int i = otaChanIndexOf(otaSel);
  i = (i + 1) % n;
  otaSel = otaChanAt(i);
  otaChanSave();
  otaResetCheck();
  String m = String("Channel: ") + otaSel;
  showToast(m.c_str(), 1500);
  needRedraw = true;
}

// buka keyboard utk mengetik nama branch baru
void otaAddBranchBegin() {
  otaChanLoad();
  otaNewBranch = "";
  otaNewBranchPending = true;
  kbTarget = &otaNewBranch;
  kbVisible = true;
  kbMode = KB_LOWER;
  needRedraw = true;
}

// dipanggil tiap frame dari app Update: begitu keyboard tertutup, simpan branch barunya
void otaAddBranchPoll() {
  if (!otaNewBranchPending || kbVisible) return;
  otaNewBranchPending = false;
  String nb = otaSanitize(otaNewBranch);
  otaNewBranch = "";
  if (!nb.length()) return;
  int idx = otaChanIndexOf(nb);
  if (idx < 0) {
    if (otaCustomCount() >= OTA_CUSTOM_MAX) { showToast("Daftar branch penuh (maks 6 tambahan)", 2200); return; }
    otaCustom += (otaCustom.length() ? "," : "") + nb;
  }
  otaSel = nb;
  otaChanSave();
  otaResetCheck();
  String m = String("Channel: ") + nb;
  showToast(m.c_str(), 1800);
  needRedraw = true;
}

// =====================================================================
// HTTP kecil (info.txt) -- ikuti redirect GitHub -> CDN
// =====================================================================
static int otaHttpGetSmall(const String& url, String& out, size_t maxLen) {
  nasaWaitForHeap();                 // guard heap sebelum TLS, sama spt jalur update lain
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  HTTPClient http;
  int code = -1;
  if (http.begin(client, url)) {
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setUserAgent("RenPhone-OTA");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    code = http.GET();
    if (code == HTTP_CODE_OK) {
      int len = http.getSize();
      if (len > 0 && (size_t)len > maxLen) code = -2;
      else {
        out = http.getString();
        if (out.length() > maxLen) code = -2;
      }
    }
    http.end();
  }
  return code;
}

static String otaKv(const String& body, const char* key) {
  String k = String(key) + "=";
  int i = 0, n = (int)body.length();
  while (i < n) {
    int e = body.indexOf('\n', i);
    if (e < 0) e = n;
    String line = body.substring(i, e);
    line.trim();
    if (line.startsWith(k)) return line.substring(k.length());
    i = e + 1;
  }
  return "";
}

// =====================================================================
// Cek channel (jalur baru) + jalur lama (GitHub API releases/latest)
// =====================================================================
String otaWifiLabel() {
  char b[48];
  if (otaCheckedOk) snprintf(b, sizeof(b), "WiFi - terbaru b%d (kini b%d):", otaLatestNum, (int)RP_BUILD_NUM);
  else              snprintf(b, sizeof(b), "WiFi - unduh dari URL (kini b%d):", (int)RP_BUILD_NUM);
  return String(b);
}

// jalur lama: dipakai HANYA utk channel bawaan "remote-control" selama channel-nya belum
// pernah dipublikasikan (rilis "build-N" biasa = non-prerelease terakhir).
static void otaCheckLegacy() {
  updBusy = true;
  updDrawBusy("Memeriksa rilis GitHub...");

  nasaWaitForHeap();
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  HTTPClient http;
  String api = String("https://api.github.com/repos/") + OTA_REPO + "/releases/latest";

  int code = -1;
  String tag = "";
  if (http.begin(client, api)) {
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setUserAgent("RenPhone-OTA");
    http.addHeader("Accept", "application/vnd.github+json");
    code = http.GET();
    if (code == HTTP_CODE_OK) {
      String body = http.getString();
      int i = body.indexOf("\"tag_name\"");
      if (i >= 0) {
        int c  = body.indexOf(':', i);
        int q1 = (c >= 0) ? body.indexOf('"', c + 1) : -1;
        int q2 = (q1 >= 0) ? body.indexOf('"', q1 + 1) : -1;
        if (q1 >= 0 && q2 > q1) tag = body.substring(q1 + 1, q2);
      }
    }
    http.end();
  }

  int num = 0;
  int d = tag.lastIndexOf('-');
  if (d >= 0) num = tag.substring(d + 1).toInt();

  updBusy = false;
  needRedraw = true;

  if (code != HTTP_CODE_OK) {
    if (code == 404) showToast("Belum ada rilis (atau repo private)");
    else showToast((String("Gagal cek rilis, kode ") + String(code)).c_str());
    return;
  }
  if (num <= 0) { showToast("Tag rilis tak dikenali"); return; }

  otaLatestNum = num;
  otaCheckedOk = true;
  otaExpUrl = ""; otaExpMd5 = ""; otaExpSize = 0;   // jalur lama: tanpa info md5/size
  if (num > (int)RP_BUILD_NUM) {
    otaStateVal = 2;
    updUrl = String("https://github.com/") + OTA_REPO + "/releases/download/" + tag + "/firmware.bin";
    showToast("Ada update! Ketuk Unduh & Pasang", 2600);
  } else {
    otaStateVal = 1;
    showToast("Sudah versi terbaru", 2000);
  }
}

// dipanggil dari updTouch saat tombol "Cek update" diketuk
void otaCheckAndFill() {
  if (!wifiConnected) { showToast("Sambungkan WiFi dulu di Setting"); return; }
  otaChanLoad();
  String ch = otaSel;

  updBusy = true;
  updDrawBusy("Memeriksa channel...");
  String base = String("https://github.com/") + OTA_REPO + "/releases/download/channel-" + ch + "/";
  String body = "";
  int code = otaHttpGetSmall(base + "info.txt", body, 1024);
  updBusy = false;
  needRedraw = true;

  if (code == 404 && ch == OTA_DEFAULT_CHANNEL) { otaCheckLegacy(); return; }
  if (code == 404) { showToast("Channel belum dipublikasikan (branch belum di-build)", 3000); return; }
  if (code != HTTP_CODE_OK) {
    showToast((String("Gagal cek channel, kode ") + String(code)).c_str(), 2500);
    return;
  }

  String bi  = otaKv(body, "branch");
  int build  = otaKv(body, "build").toInt();
  int baseRv = otaKv(body, "base").toInt();
  int size   = otaKv(body, "size").toInt();
  String md5 = otaKv(body, "md5");
  md5.toLowerCase();
  if (build <= 0 || size < 100000 || md5.length() != 32 || bi != ch) {
    showToast("Info channel rusak / tidak valid", 2500);
    return;
  }
  if (baseRv != RP_BASE_REV) {
    char b[72];
    snprintf(b, sizeof(b), "Tidak kompatibel (base %d, HP base %d). Dibatalkan", baseRv, (int)RP_BASE_REV);
    showToast(b, 3800);
    otaResetCheck();
    return;
  }

  otaLatestNum = build;
  otaCheckedOk = true;
  bool switching = (ch != otaBranchName());
  if (switching || build > (int)RP_BUILD_NUM) {
    otaStateVal = 2;
    updUrl = base + "firmware.bin";
    otaExpUrl = updUrl; otaExpMd5 = md5; otaExpSize = size;
    if (switching) {
      String m = String("Pindah ke '") + ch + "' b" + String(build) + ". Ketuk Unduh & Pasang";
      showToast(m.c_str(), 3200);
    } else {
      showToast("Ada update! Ketuk Unduh & Pasang", 2600);
    }
  } else {
    otaStateVal = 1;
    showToast("Sudah versi terbaru", 2000);
  }
}

// ---- dipakai UI app Update (phone.ino). Fungsi, bukan makro, karena RP_BUILD_NUM
//      default-nya didefinisikan di file ini yg urutannya SETELAH phone.ino ----
int otaBuildNum()    { return (int)RP_BUILD_NUM; }
int otaLatestBuild() { return otaLatestNum; }
int otaState() {      // 0 belum dicek, 1 sudah terbaru, 2 ada update
  return otaCheckedOk ? otaStateVal : 0;
}

// =====================================================================
// PEMERIKSAAN SEBELUM/SELAMA FLASH (dipanggil dari updInstallFromURL / FromSD)
// =====================================================================
static uint8_t otaHdrBuf[36];
static size_t  otaHdrFill = 0;
static bool    otaHdrDone = false;

// h = 36 byte pertama sebuah image aplikasi ESP32-S3
static bool otaHdrCheck(const uint8_t* h, size_t n) {
  if (n < 36) return false;
  if (h[0] != 0xE9) return false;                         // magic header image ESP
  if (h[12] != 0x09 || h[13] != 0x00) return false;       // chip id 9 = ESP32-S3
  uint32_t m = (uint32_t)h[32] | ((uint32_t)h[33] << 8) | ((uint32_t)h[34] << 16) | ((uint32_t)h[35] << 24);
  return m == 0xABCD5432UL;                               // magic esp_app_desc_t
}

void otaHdrReset() { otaHdrFill = 0; otaHdrDone = false; }

// kumpulkan 36 byte pertama dari potongan2 data; false = header SUDAH lengkap & TIDAK valid
bool otaHdrFeed(const uint8_t* b, size_t n, size_t off) {
  if (otaHdrDone) return true;
  if (off != otaHdrFill) return true;                     // bukan aliran berurutan -> jangan menebak
  size_t take = n;
  if (otaHdrFill + take > sizeof(otaHdrBuf)) take = sizeof(otaHdrBuf) - otaHdrFill;
  memcpy(otaHdrBuf + otaHdrFill, b, take);
  otaHdrFill += take;
  if (otaHdrFill >= sizeof(otaHdrBuf)) {
    otaHdrDone = true;
    return otaHdrCheck(otaHdrBuf, otaHdrFill);
  }
  return true;
}

// sebelum Update.begin: ukuran harus cocok dgn info.txt (kalau URL berasal dari "Cek update")
bool otaPreInstall(const String& url, int contentLen, String& why) {
  if (otaExpUrl.length() && url == otaExpUrl && otaExpSize > 0 && contentLen != otaExpSize) {
    why = String("Ukuran file (") + String(contentLen) + ") tidak cocok dgn info channel (" + String(otaExpSize) +
          "). Dibatalkan, firmware lama aman.";
    return false;
  }
  return true;
}

// setelah Update.begin berhasil: pasang MD5 yg diharapkan -> Update.end() akan menolak kalau beda
void otaApplyExpect(const String& url) {
  if (otaExpUrl.length() && url == otaExpUrl && otaExpMd5.length() == 32) {
    Update.setMD5(otaExpMd5.c_str());
  }
}

// =====================================================================
// PENGAMAN BOOT + ROLLBACK KE SLOT OTA SEBELUMNYA
// =====================================================================
// Slot OTA "lain" (yg bukan sedang jalan) valid kalau header + app_desc-nya benar.
static bool otaOtherSlotValid(const esp_partition_t** outp) {
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* o   = esp_ota_get_next_update_partition(run);
  if (!run || !o || o == run) return false;
  uint8_t h[36];
  if (esp_partition_read(o, 0, h, sizeof(h)) != ESP_OK) return false;
  if (!otaHdrCheck(h, sizeof(h))) return false;
  esp_app_desc_t d;
  if (esp_ota_get_partition_description(o, &d) != ESP_OK) return false;
  if (outp) *outp = o;
  return true;
}

static bool otaRbReady = false;
void otaRollbackRefresh() { otaRbReady = otaOtherSlotValid(nullptr); }
bool otaRollbackReady()   { return otaRbReady; }

// userInitiated=true: dari tombol di app Update (layar sudah hidup).
// false: dari otaGuardBoot() di awal setup() (layar BELUM diinisialisasi -> jangan disentuh).
bool otaRollbackNow(bool userInitiated) {
  const esp_partition_t* o = nullptr;
  if (!otaOtherSlotValid(&o)) return false;
  if (esp_ota_set_boot_partition(o) != ESP_OK) return false;   // juga memverifikasi image-nya
  Preferences p;
  if (p.begin("otaguard", false)) {
    p.putUChar("pend", 0);
    p.putUChar("tries", 0);
    if (!userInitiated) p.putUChar("rbmsg", 1);
    p.end();
  }
  if (userInitiated) {
    display.fillScreen(TFT_BLACK);
    display.setTextSize(1);
    display.setTextColor(0x07FF);
    const char* t1 = "Kembali ke versi sebelumnya...";
    display.setCursor((SCR_W - display.textWidth(t1)) / 2, SCR_H / 2 - 4);
    display.print(t1);
    delay(1500);
  }
  ESP.restart();
  return true;
}

// dipanggil dari updShowResult(sukses) tepat sebelum restart: tandai firmware baru "pending"
void otaGuardArm() {
  Preferences p;
  if (p.begin("otaguard", false)) {
    p.putUChar("pend", 1);
    p.putUChar("tries", 0);
    p.end();
  }
}

// PALING AWAL di setup(): hitung boot firmware yg belum terbukti sehat; kalau kebanyakan -> rollback
void otaGuardBoot() {
  Preferences p;
  if (!p.begin("otaguard", false)) return;
  if (p.getUChar("pend", 0)) {
    uint8_t t = p.getUChar("tries", 0) + 1;
    if (t > OTA_GUARD_MAX_TRIES) {
      p.putUChar("pend", 0);
      p.putUChar("tries", 0);
      p.end();
      Serial.println("[OTA-GUARD] firmware baru tak kunjung sehat -> kembali ke slot sebelumnya");
      otaRollbackNow(false);       // sukses = restart & tidak kembali; gagal = lanjut boot biasa
      return;
    }
    p.putUChar("tries", t);
  }
  p.end();
}

// dipanggil tiap putaran loop(): setelah hidup OTA_GUARD_OK_MS -> firmware dianggap sehat
void otaGuardPoll() {
  static bool healthy = false;
  static bool msgChecked = false;
  uint32_t ms = millis();
  if (!msgChecked && ms > 4000) {            // beri tahu kalau barusan di-rollback otomatis
    msgChecked = true;
    Preferences p;
    if (p.begin("otaguard", false)) {
      if (p.getUChar("rbmsg", 0)) {
        p.putUChar("rbmsg", 0);
        showToast("Update dibatalkan otomatis: firmware baru gagal", 4500);
      }
      p.end();
    }
  }
  if (healthy || ms < OTA_GUARD_OK_MS) return;
  healthy = true;
  Preferences p;
  if (p.begin("otaguard", false)) {
    if (p.getUChar("pend", 0)) {
      p.putUChar("pend", 0);
      p.putUChar("tries", 0);
    }
    p.end();
  }
}
