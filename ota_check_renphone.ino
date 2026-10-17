// =====================================================================
// ota_check_renphone.ino -- "Terbaru": cek rilis firmware di GitHub Release
//
// Alur: ketuk pill "Terbaru" di app Update -> HP tanya GitHub rilis terakhir
// (tag "build-N", dibuat otomatis oleh workflow tiap build) -> kalau N lebih
// besar dari build yg terpasang (RP_BUILD_NUM, disuntik workflow lewat
// -DRP_BUILD_NUM), kolom URL otomatis terisi link firmware.bin rilis itu.
// Tinggal ketuk "Unduh & Pasang" (alur konfirmasi & flash yg sudah ada).
//
// Catatan: repo harus PUBLIC supaya HP bisa unduh tanpa login.
// =====================================================================
#ifndef RP_BUILD_NUM
#define RP_BUILD_NUM 0          // build lokal / non-CI
#endif
#define OTA_REPO "silentproject2025/ren_phone"

static int    otaLatestNum = -1;
static bool   otaCheckedOk = false;

// label di atas kolom URL WiFi (dipanggil dari drawUpdate)
String otaWifiLabel() {
  char b[48];
  if (otaCheckedOk) snprintf(b, sizeof(b), "WiFi - terbaru b%d (kini b%d):", otaLatestNum, (int)RP_BUILD_NUM);
  else              snprintf(b, sizeof(b), "WiFi - unduh dari URL (kini b%d):", (int)RP_BUILD_NUM);
  return String(b);
}

// dipanggil dari updTouch saat pill "Terbaru" diketuk
void otaCheckAndFill() {
  if (!wifiConnected) { showToast("Sambungkan WiFi dulu di Setting"); return; }

  updBusy = true;
  updDrawBusy("Memeriksa rilis GitHub...");

  nasaWaitForHeap();                 // guard heap sebelum TLS, sama spt jalur update lain
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
  if (num > (int)RP_BUILD_NUM) {
    updUrl = String("https://github.com/") + OTA_REPO + "/releases/download/" + tag + "/firmware.bin";
    showToast("Ada update! Ketuk Unduh & Pasang", 2600);
  } else {
    showToast("Sudah versi terbaru", 2000);
  }
}

// ---- dipakai UI app Update (phone.ino). Fungsi, bukan makro, karena RP_BUILD_NUM
//      default-nya didefinisikan di file ini yg urutannya SETELAH phone.ino ----
int otaBuildNum()    { return (int)RP_BUILD_NUM; }
int otaLatestBuild() { return otaLatestNum; }
int otaState() {      // 0 belum dicek, 1 sudah terbaru, 2 ada update
  if (!otaCheckedOk) return 0;
  return (otaLatestNum > (int)RP_BUILD_NUM) ? 2 : 1;
}
