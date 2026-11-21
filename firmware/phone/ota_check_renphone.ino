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
#include "mbedtls/pk.h"
#include "sec_core.h"
#include "ota_pubkey.h"

#ifndef RP_BUILD_NUM
#define RP_BUILD_NUM 0          // build lokal / non-CI
#endif
// nama branch disuntik workflow lewat -DRP_BRANCH=<nama-bersih>; lokal = "local"
#define RP_STR2(x) #x
#define RP_STR(x)  RP_STR2(x)
#ifndef RP_BRANCH
#define RP_BRANCH local
#endif
// label versi utk manusia (mis. 1.0.0), disuntik workflow dari file VERSION lewat -DRP_VERSION=...
// Beda dgn RP_BUILD_NUM: nomor build yg dipakai OTA utk membandingkan, label ini hanya tampilan.
#ifndef RP_VERSION
#define RP_VERSION dev
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

// OTA_SIG_V1 -- info.txt bertanda tangan (ECDSA P-256 + SHA-256) & SHA-256 firmware diperiksa selagi unduh.
// Kunci publik di ota_pubkey.h. Kosong = mati (perilaku lama: ukuran + MD5). Terisi = WAJIB: info.txt tanpa/salah
// tanda tangan ditolak, jalur rilis lama & URL manual ditolak. Pembuatan kunci: tools/ota_keygen.sh.
#define OTA_SIG_REQUIRED (sizeof(OTA_PUBKEY_PEM) > 1)
static bool    otaSigOk      = false;    // info.txt utk otaExpUrl lolos verifikasi tanda tangan
static uint8_t otaExpSha[32];            // SHA-256 firmware yg diharapkan (dari info.txt)
static bool    otaExpShaSet  = false;
static BkSha   otaShaCtx;                // hash berjalan selama unduhan
static bool    otaShaOn      = false;

static bool otaVerifySig(const uint8_t* hash32, const uint8_t* sig, size_t sl) {
  mbedtls_pk_context pk; mbedtls_pk_init(&pk);
  bool ok = false;
  if (mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBKEY_PEM, sizeof(OTA_PUBKEY_PEM)) == 0)   // PEM wajib diakhiri NUL (ikut dihitung)
    ok = (mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash32, 32, sig, sl) == 0);
  mbedtls_pk_free(&pk);
  return ok;
}
// true = tanda tangan sah. signedOut = bagian badan yg ditandatangani (hanya ini yg boleh dipercaya & di-parse).
static bool otaVerifyInfo(const String& body, String& signedOut, const char*& why) {
  size_t sl; const char* sx; size_t xl;
  if (!secInfoSplit(body.c_str(), body.length(), &sl, &sx, &xl)) { why = "Info channel tidak bertanda tangan. Dibatalkan"; return false; }
  uint8_t sig[80];
  int n = secUnhex(sx, xl, sig, sizeof(sig));
  if (n < 8) { why = "Tanda tangan info channel rusak. Dibatalkan"; return false; }
  uint8_t h[32]; bkSha256((const uint8_t*)body.c_str(), sl, h);
  if (!otaVerifySig(h, sig, (size_t)n)) { why = "Tanda tangan info channel TIDAK valid. Dibatalkan"; return false; }
  signedOut = body.substring(0, sl);
  return true;
}

// ---- changelog (English) untuk update yg ditemukan "Cek update" ----
// Dibaca dari  releases/download/channel-<branch>/changelog.txt  (dibuat workflow dari CHANGELOG.md).
// Opsional: kalau file tidak ada / gagal diunduh, update tetap jalan seperti biasa tanpa changelog.
static String otaNotes     = "";     // isi changelog (ASCII, baris dipisah \n)
static String otaNotesHead = "";     // judul, mis. "v1.0.1 b57 [chat]"
static bool   otaNotesShow = false;  // true = layar changelog sedang terbuka di app Update

// =====================================================================
// CHANNEL (branch) -- daftar, pilihan, tambah
// =====================================================================
static const char* const OTA_BUILTIN[] = { "remote-control", "chat" };
#define OTA_BUILTIN_N 2

static String otaSel = "";             // channel terpilih (sudah bersih)
static String otaCustom = "";          // daftar branch tambahan, dipisah koma
static bool   otaChanLoaded = false;
static String otaNewBranch = "";
static bool   otaCrossDisp = false;    // true = ambil firmware untuk LAYAR LAIN (hanya RAM: hilang saat reboot, sengaja)
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
  otaSigOk = false; otaExpShaSet = false; otaShaOn = false;
  otaNotes = ""; otaNotesHead = ""; otaNotesShow = false;
}

String otaBranchName()  { return String(RP_STR(RP_BRANCH)); }   // branch firmware yg SEDANG jalan
String otaChannelName() { otaChanLoad(); return otaSel; }       // branch yg dipilih utk diunduh

// ---- OPSI: pasang firmware untuk LAYAR LAIN (mis. HP ILI9341 -> firmware ST7796) ----
// Dipakai saat panel mau diganti: pasang dulu firmware layar baru selagi layar lama masih bisa
// dipakai, baru tukar panelnya. Hanya RAM (tidak disimpan) -> tiap reboot kembali ke "layar ini".
static const char* otaOtherDispId()  { return strcmp(RP_DISP_ID, "st7796") == 0 ? "ili9341" : "st7796"; }
static const char* otaTargetDispId() { return otaCrossDisp ? otaOtherDispId() : RP_DISP_ID; }
bool otaCrossDispOn() { return otaCrossDisp; }
void otaDispLabel(char* out, int n) {
  snprintf(out, n, "%s", strcmp(otaTargetDispId(), "st7796") == 0 ? "7796" : "9341");
}
void otaCrossDispToggle() {
  otaCrossDisp = !otaCrossDisp;
  otaResetCheck();
  if (otaCrossDisp) {
    char mb[112];
    snprintf(mb, sizeof(mb), RPTR("Target firmware layar %s. Layar ini bisa gelap setelah pasang!"),
             strcmp(otaTargetDispId(), "st7796") == 0 ? "ST7796" : "ILI9341");
    showToast(mb, 3500);
  } else {
    showToast(RPTR("Target: firmware layar ini"), 1800);
  }
  needRedraw = true;
}

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
    if (otaCustomCount() >= OTA_CUSTOM_MAX) { showToast(RPTR("Daftar branch penuh (maks 6 tambahan)"), 2200); return; }
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
  int code = -1;
  for (int attempt = 0; attempt < 2; attempt++) {          // OTA-DIAG: -1 (koneksi gagal) sering sementara -> coba sekali lagi
    if (attempt) { Serial.println(F("[OTA] ulangi sekali...")); delay(1200); }
    nasaWaitForHeap();                 // guard heap sebelum TLS, sama spt jalur update lain
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    HTTPClient http;
    code = -1;
    const unsigned long t0 = millis();
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
      if (code < 0) {                  // OTA-DIAG: catat penyebab sebenarnya di serial
        char eb[100] = {0}; int le = client.lastError(eb, sizeof(eb));
        IPAddress ip; int dns = WiFi.hostByName("github.com", ip);
        Serial.printf("[OTA] GET %s -> %d (%s) %lu ms | TLS err=%d '%s' | WiFi=%d RSSI=%d DNS=%d | RAM internal: blok %u B, bebas %u B\n",
                      url.c_str(), code, HTTPClient::errorToString(code).c_str(), (unsigned long)(millis() - t0), le, eb,
                      (int)WiFi.status(), (int)WiFi.RSSI(), dns,
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      }
      http.end();
    } else {
      Serial.printf("[OTA] http.begin gagal untuk %s\n", url.c_str());
    }
    if (code != -1) break;             // sukses / kode HTTP asli / -2: tidak perlu ulang
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

// ---- changelog: aksesor untuk UI app Update (phone.ino) ----
const String& otaNotesText() { return otaNotes; }
const String& otaNotesTitle() { return otaNotesHead; }
bool otaNotesAvailable() { return otaNotes.length() > 0; }
bool otaNotesIsOpen()    { return otaNotesShow && otaNotes.length() > 0; }
void otaNotesOpen()      { if (otaNotes.length()) { otaNotesShow = true; needRedraw = true; } }
void otaNotesClose()     { otaNotesShow = false; needRedraw = true; }

// Unduh changelog.txt di folder channel yg sama dgn info.txt. TIDAK PERNAH menggagalkan update:
// 404 / error / terlalu besar -> changelog kosong, selesai.
static void otaFetchNotes(const String& base, const String& head) {
  otaNotes = ""; otaNotesHead = ""; otaNotesShow = false;
  String nb = "";
  updBusy = true;
  updDrawBusy(RPTR("Reading changelog..."));
  int c = otaHttpGetSmall(base + "changelog.txt", nb, 1536);
  updBusy = false;
  needRedraw = true;
  if (c != HTTP_CODE_OK) return;
  // bersihkan: hanya ASCII tercetak + \n (font layar tidak punya glyph lain), buang \r
  String clean = "";
  for (size_t i = 0; i < nb.length() && clean.length() < 1500; i++) {
    char ch = nb[i];
    if (ch == '\n' || (ch >= 32 && ch < 127)) clean += ch;
    else if (ch == '\t') clean += ' ';
  }
  clean.trim();
  if (!clean.length()) return;
  otaNotes = clean;
  otaNotesHead = head;
  otaNotesShow = true;                 // buka otomatis begitu ada update
}

// =====================================================================
// Cek channel (jalur baru) + jalur lama (GitHub API releases/latest)
// =====================================================================
String otaWifiLabel() {
  char b[48];
  if (otaCheckedOk) snprintf(b, sizeof(b), RPTR("WiFi - terbaru b%d (kini b%d):"), otaLatestNum, (int)RP_BUILD_NUM);
  else              snprintf(b, sizeof(b), RPTR("WiFi - unduh dari URL (kini b%d):"), (int)RP_BUILD_NUM);
  return String(b);
}

// jalur lama: dipakai HANYA utk channel bawaan "remote-control" selama channel-nya belum
// pernah dipublikasikan (rilis "build-N" biasa = non-prerelease terakhir).
static void otaCheckLegacy() {
  if (OTA_SIG_REQUIRED) {                       // jalur lama tidak punya info.txt -> tidak bisa diverifikasi
    showToast(RPTR("Jalur rilis lama tidak bertanda tangan. Dibatalkan"), 3500);
    otaResetCheck(); return;
  }
  updBusy = true;
  updDrawBusy(RPTR("Memeriksa rilis GitHub..."));

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
    if (code == 404) showToast(RPTR("Belum ada rilis (atau repo private)"));
    else showToast((String(RPTR("Gagal cek rilis, kode ")) + String(code)).c_str());
    return;
  }
  if (num <= 0) { showToast(RPTR("Tag rilis tak dikenali")); return; }

  otaLatestNum = num;
  otaCheckedOk = true;
  otaExpUrl = ""; otaExpMd5 = ""; otaExpSize = 0;   // jalur lama: tanpa info md5/size
  otaSigOk = false; otaExpShaSet = false;
  if (num > (int)RP_BUILD_NUM) {
    otaStateVal = 2;
    updUrl = String("https://github.com/") + OTA_REPO + "/releases/download/" + tag + "/firmware.bin";
    showToast(RPTR("Ada update! Ketuk Unduh & Pasang"), 2600);
  } else {
    otaStateVal = 1;
    showToast(RPTR("Sudah versi terbaru"), 2000);
  }
}

// dipanggil dari updTouch saat tombol "Cek update" diketuk
void otaCheckAndFill() {
  if (!wifiConnected) { showToast(RPTR("Sambungkan WiFi dulu di Setting")); return; }
  otaChanLoad();
  String ch = otaSel;
  // suffix channel mengikuti LAYAR TARGET (bukan layar build ini): st7796 -> channel-<branch>-st7796, ili9341 -> tanpa suffix
  const char* tgtDisp = otaTargetDispId();
  const char* tgtSuffix = (strcmp(tgtDisp, "st7796") == 0) ? "-st7796" : "";
  if (!ch.endsWith(tgtSuffix)) ch += tgtSuffix;

  updBusy = true;
  updDrawBusy(RPTR("Memeriksa channel..."));
  String base = String("https://github.com/") + OTA_REPO + "/releases/download/channel-" + ch + "/";
  String body = "";
  int code = otaHttpGetSmall(base + "info.txt", body, 1024);
  updBusy = false;
  needRedraw = true;

  if (code == 404 && ch == OTA_DEFAULT_CHANNEL) { otaCheckLegacy(); return; }
  if (code == 404) { showToast(RPTR("Channel belum dipublikasikan (branch belum di-build)"), 3000); return; }
  if (code != HTTP_CODE_OK) {
    showToast((String(RPTR("Gagal cek channel, kode ")) + String(code) + nasaHttpErrHint(code)).c_str(), 2500);   // OTA-DIAG
    return;
  }

  if (OTA_SIG_REQUIRED) {                       // OTA_SIG_V1: hanya isi yg DITANDATANGANI yg dipercaya
    String sb; const char* why = nullptr;
    if (!otaVerifyInfo(body, sb, why)) { showToast(RPTR(why), 3800); otaResetCheck(); return; }
    body = sb;
  }
  String bi  = otaKv(body, "branch");
  int build  = otaKv(body, "build").toInt();
  int baseRv = otaKv(body, "base").toInt();
  int size   = otaKv(body, "size").toInt();
  String md5 = otaKv(body, "md5");
  md5.toLowerCase();
  if (build <= 0 || size < 100000 || md5.length() != 32 || bi != ch) {
    showToast(RPTR("Info channel rusak / tidak valid"), 2500);
    return;
  }
  uint8_t shaBin[32];                           // OTA_SIG_V1: SHA-256 firmware (opsional; wajib kalau tanda tangan wajib)
  String sh256 = otaKv(body, "sha256"); sh256.toLowerCase();
  bool shaOk = (sh256.length() == 64 && secUnhex(sh256.c_str(), 64, shaBin, sizeof(shaBin)) == 32);
  if (OTA_SIG_REQUIRED && !shaOk) {
    showToast(RPTR("Info channel tanpa sha256. Dibatalkan"), 3000);
    otaResetCheck(); return;
  }
  // Firmware beda LAYAR (ILI9341 vs ST7796) tidak boleh saling timpa -> layar bisa gelap total,
  // KECUALI user sengaja menyalakan opsi "layar lain" (otaCrossDisp): lalu yang dicocokkan adalah layar TARGET.
  // info.txt lama tanpa "display=" dianggap ili9341.
  {
    String disp = otaKv(body, "display");
    if (disp.length() == 0) disp = "ili9341";
    if (disp != tgtDisp) {
      showToast(RPTR("Firmware untuk layar lain. Dibatalkan"), 3500);
      otaResetCheck();
      return;
    }
  }
  if (baseRv != RP_BASE_REV) {
    char b[72];
    snprintf(b, sizeof(b), RPTR("Tidak kompatibel (base %d, HP base %d). Dibatalkan"), baseRv, (int)RP_BASE_REV);
    showToast(b, 3800);
    otaResetCheck();
    return;
  }

  otaLatestNum = build;
  otaCheckedOk = true;
  bool switching = (ch != otaBranchName()) || otaCrossDisp;   // opsi layar lain: selalu boleh dipasang (walau build tidak lebih baru)
  if (switching || build > (int)RP_BUILD_NUM) {
    otaStateVal = 2;
    updUrl = base + "firmware.bin";
    otaExpUrl = updUrl; otaExpMd5 = md5; otaExpSize = size;
    otaExpShaSet = shaOk; if (shaOk) memcpy(otaExpSha, shaBin, sizeof(otaExpSha));
    otaSigOk = OTA_SIG_REQUIRED;                // lolos verifikasi di atas
    {   // changelog (opsional) -- judul: versi + build + channel
      String ver = otaKv(body, "version"); ver.trim();
      String head = (ver.length() ? String("v") + ver + " " : String("")) + "b" + String(build) + " [" + ch + "]";
      otaFetchNotes(base, head);
    }
    if (switching) {
      String m = String(otaCrossDisp ? RPTR("LAYAR LAIN: '") : RPTR("Pindah ke '")) + ch + "' b" + String(build) + RPTR(". Ketuk Unduh & Pasang");
      showToast(m.c_str(), 3200);
    } else {
      showToast(RPTR("Ada update! Ketuk Unduh & Pasang"), 2600);
    }
  } else {
    otaStateVal = 1;
    showToast(RPTR("Sudah versi terbaru"), 2000);
  }
}

// ---- dipakai UI app Update (phone.ino). Fungsi, bukan makro, karena RP_BUILD_NUM
//      default-nya didefinisikan di file ini yg urutannya SETELAH phone.ino ----
int otaBuildNum()    { return (int)RP_BUILD_NUM; }
void otaVersionLabel(char* out, int n) {   // "v1.0.0", atau "dev" utk build lokal
  const char* v = RP_STR(RP_VERSION);
  if (strcmp(v, "dev") == 0) snprintf(out, n, "dev");
  else snprintf(out, n, "v%s", v);
}
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
  if (OTA_SIG_REQUIRED && !(otaSigOk && otaExpUrl.length() && url == otaExpUrl)) {
    why = String(RPTR("URL tidak terverifikasi. Kunci tanda tangan OTA aktif: pakai 'Cek update' (channel bertanda tangan)."));
    return false;
  }
  if (otaExpUrl.length() && url == otaExpUrl && otaExpSize > 0 && contentLen != otaExpSize) {
    char wb[144];
    snprintf(wb, sizeof(wb), RPTR("Ukuran file (%d) tidak cocok dgn info channel (%d). Dibatalkan, firmware lama aman."), contentLen, otaExpSize);
    why = String(wb);
    return false;
  }
  return true;
}

// setelah Update.begin berhasil: pasang MD5 yg diharapkan -> Update.end() akan menolak kalau beda
void otaApplyExpect(const String& url) {
  if (otaExpUrl.length() && url == otaExpUrl && otaExpMd5.length() == 32) {
    Update.setMD5(otaExpMd5.c_str());
  }
  otaShaOn = false;                             // OTA_SIG_V1: mulai hitung SHA-256 selama unduhan
  if (otaExpUrl.length() && url == otaExpUrl && otaExpShaSet) { bkShaInit(otaShaCtx); otaShaOn = true; }
}
void otaShaFeed(const uint8_t* b, size_t n) { if (otaShaOn) bkShaUpdate(otaShaCtx, b, n); }
// dipanggil SEBELUM Update.end(): hash seluruh unduhan harus sama dgn info.txt. false = batalkan (firmware lama aman).
bool otaShaCheck(String& why) {
  if (!otaShaOn) {
    if (OTA_SIG_REQUIRED) { why = String(RPTR("Verifikasi SHA-256 tidak aktif. Dibatalkan, firmware lama aman.")); return false; }
    return true;
  }
  otaShaOn = false;
  uint8_t h[32]; bkShaFinal(otaShaCtx, h);
  if (!bkCtEq(h, otaExpSha, 32)) {
    why = String(RPTR("SHA-256 firmware tidak cocok dgn info channel. Dibatalkan, firmware lama aman."));
    return false;
  }
  return true;
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

// Ukuran slot OTA tujuan update (KB), atau -1 kalau tabel partisi perangkat TIDAK punya slot OTA.
// Dipakai layar GAGAL app Update supaya penyebab "Partition Could Not be Found" / "Not Enough Space" jelas.
int otaSlotSizeKB() {
  const esp_partition_t* o = esp_ota_get_next_update_partition(NULL);   // sama dgn yg dipakai Update.begin()
  if (!o) return -1;
  return (int)(o->size / 1024UL);
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
    const char* t1 = RPTR("Kembali ke versi sebelumnya...");
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
        showToast(RPTR("Update dibatalkan otomatis: firmware baru gagal"), 4500);
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
