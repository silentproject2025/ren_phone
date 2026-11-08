// glue_test.cpp -- kompilasi + JALANKAN backup_renphone.ino terhadap mock NVS/SD/UI (tests_backup/mock_env.h).
// Jalankan (dari folder repo):
//   g++ -std=c++11 -Wall -Wextra -Wno-unused-function -I. -Itests_backup -Itests_backup/stub -o /tmp/bk_glue tests_backup/glue_test.cpp && /tmp/bk_glue
#include "mock_env.h"

// ---- yang biasanya disediakan phone.ino
int SCR_W = 320, SCR_H = 480; bool needRedraw = false, sdReady = true, ntpSynced = true;
FSMock SD_MMC;
#define BACK_H 24
int backY() { return SCR_H - BACK_H - 3; }
bool backHidden() { return true; }          // app Backup menyembunyikan pil Kembali

#include "../backup_renphone.ino"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if(!(c)){ fails++; printf("GAGAL  baris %d  ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static void sh(const char* c) { int r = system(c); (void)r; }
static int cx(int r) { return bkR[r][0] + bkR[r][2] / 2; }
static int cy(int r) { return bkR[r][1] + bkR[r][3] / 2; }
static void frame() { LGFX_Sprite s; drawBackup(s); CHECK(s.clip == 0, "clip rect seimbang"); }
static void tap(int x, int y) { backupTouch(x, y, true, true); backupTouch(x, y, true, false); backupTouch(x, y, false, false); }
static void tapR(int r) { frame(); tap(cx(r), cy(r)); }
static void nvsSet(const char* ns, const char* k, int ty, std::vector<uint8_t> d) { nvsStore()[nk(ns, k)] = { ty, d }; }
static void nvsStr(const char* ns, const char* k, const char* v) { nvsSet(ns, k, NVS_TYPE_STR, std::vector<uint8_t>(v, v + strlen(v) + 1)); }
static bool nvsHas(const char* ns, const char* k) { return nvsStore().count(nk(ns, k)); }
static std::string nvsGetStr(const char* ns, const char* k) { auto it = nvsStore().find(nk(ns, k)); return it == nvsStore().end() ? "<none>" : std::string((char*)it->second.d.data()); }
static int nvsGetU8(const char* ns, const char* k) { auto it = nvsStore().find(nk(ns, k)); return it == nvsStore().end() ? -1 : it->second.d[0]; }
static bool fileHas(const char* path, const char* needle) {
  FILE* f = fopen(path, "rb"); if (!f) return false; std::string b; char c[4096]; size_t n; while ((n = fread(c, 1, sizeof c, f)) > 0) b.append(c, n); fclose(f);
  return b.find(needle) != std::string::npos; }
static bool fileExists(const char* p) { return access(p, F_OK) == 0; }

static void seed() {
  nvsStore().clear();
  nvsSet("ui", "theme", NVS_TYPE_U8, { 3 }); nvsSet("ui", "bright", NVS_TYPE_U32, { 200, 0, 0, 0 }); nvsStr("ui", "font", "Inter");
  nvsSet("homelay", "ord", NVS_TYPE_BLOB, { 1, 2, 3, 0, 255, 9 });
  nvsStr("wifi", "pass", "rahasia-wifi"); nvsStr("wifi", "ssid", "RumahKu"); nvsStr("sip", "pass", "rahasia-sip"); nvsStr("gemini", "apikey", "AIza-rahasia");
  nvsStr("rcfg", "pass3", "rahasia-ap"); nvsSet("rcfg", "fps", NVS_TYPE_U8, { 12 });
  nvsSet("otaguard", "boot", NVS_TYPE_U8, { 1 }); nvsSet("nvs.net80211", "x", NVS_TYPE_U8, { 7 }); nvsSet("phy", "cal", NVS_TYPE_U8, { 5 });
}

int main() {
  sh("rm -rf " SDROOT " && mkdir -p " SDROOT);
  seed();

  // ---- masuk app: belum ada folder /backup
  backupEnter();
  CHECK(bkListN == 0 && !bkSecrets, "awal: daftar kosong, sandi mati");
  frame();

  // ---- buat backup #1 (tanpa sandi)
  tapR(BKR_CREATE);
  CHECK(fileExists(SDROOT "/backup/nyx_001.nbk"), "nyx_001.nbk dibuat; toast='%s'", gToast.c_str());
  CHECK(bkListN == 1 && bkValid[0] && bkNum[0] == 1 && bkSel == 0, "daftar: 1 valid, terpilih");
  CHECK(bkCnt[0] == 5, "5 nilai (ui x3, homelay, rcfg fps) dapat %d", bkCnt[0]);
  CHECK(!fileHas(SDROOT "/backup/nyx_001.nbk", "rahasia"), "TIDAK ada rahasia di file #1");
  CHECK(fileHas(SDROOT "/backup/nyx_001.nbk", "Inter"), "isi pengaturan ada");
  CHECK(!fileHas(SDROOT "/backup/nyx_001.nbk", "otaguard") && !fileHas(SDROOT "/backup/nyx_001.nbk", "net80211"), "namespace sistem tidak ikut");
  CHECK(gToast.find("#001") != std::string::npos, "toast menyebut #001: '%s'", gToast.c_str());

  // ---- sakelar sandi, backup #2 (dengan sandi)
  tapR(BKR_TOGGLE);
  CHECK(bkSecrets, "sakelar nyala");
  { Preferences p; p.begin("nyxbak"); CHECK(p.getBool("sec", false), "sakelar tersimpan"); }
  tapR(BKR_CREATE);
  CHECK(bkListN == 2 && bkNum[0] == 2 && (bkFlg[0] & BK_FLAG_SECRETS) && !(bkFlg[1] & BK_FLAG_SECRETS), "urutan terbaru dulu, flag sandi benar");
  CHECK(fileHas(SDROOT "/backup/nyx_002.nbk", "rahasia-wifi") && fileHas(SDROOT "/backup/nyx_002.nbk", "AIza-rahasia"), "file #2 membawa sandi");
  CHECK(!fileHas(SDROOT "/backup/nyx_002.nbk", "otaguard"), "otaguard tetap tidak ikut");

  // ---- ubah pengaturan, lalu restore #2
  nvsSet("ui", "theme", NVS_TYPE_U8, { 9 }); nvsStore().erase(nk("wifi", "pass")); nvsStr("ui", "barukey", "dibuat-setelah-backup");
  nvsSet("otaguard", "boot", NVS_TYPE_U8, { 77 });
  bkSel = 0; ESP.restarts = 0;
  tapR(BKR_RESTORE);
  CHECK(bkArmKind == 1 && nvsGetU8("ui", "theme") == 9, "ketukan pertama hanya mengaktifkan konfirmasi (NVS belum berubah)");
  tapR(BKR_RESTORE);
  CHECK(nvsGetU8("ui", "theme") == 3, "theme dipulihkan jadi 3");
  CHECK(nvsGetStr("wifi", "pass") == "rahasia-wifi", "sandi wifi dipulihkan");
  CHECK(nvsGetStr("ui", "barukey") == "dibuat-setelah-backup", "key baru (tak ada di backup) dibiarkan");
  CHECK(nvsGetU8("otaguard", "boot") == 77, "otaguard TIDAK disentuh");
  CHECK(bkRestartAt != 0 && ESP.restarts == 0, "restart dijadwalkan, belum jalan");
  tap(cx(BKR_CREATE), cy(BKR_CREATE));
  CHECK(bkListN == 2, "sentuhan saat menunggu restart diabaikan");
  gMillis += 2300; frame();
  CHECK(ESP.restarts == 1, "HP restart setelah jeda");
  bkRestartAt = 0;

  // ---- restore #1 (tanpa sandi): sandi yang ada TIDAK dihapus
  nvsStr("wifi", "pass", "sandi-sekarang"); bkSel = 1;
  tapR(BKR_RESTORE); tapR(BKR_RESTORE);
  CHECK(nvsGetStr("wifi", "pass") == "sandi-sekarang", "restore backup tanpa sandi tidak menyentuh sandi yang ada");
  bkRestartAt = 0;

  // ---- konfirmasi kedaluwarsa
  bkSel = 0; tapR(BKR_RESTORE); CHECK(bkArmKind == 1, "armed");
  gMillis += 4100; frame(); CHECK(bkArmKind == 0, "konfirmasi kedaluwarsa setelah 4 dtk");
  // tombol lain membatalkan konfirmasi
  tapR(BKR_RESTORE); CHECK(bkArmKind == 1, "armed lagi"); tapR(BKR_DELETE); CHECK(bkArmKind == 2, "pindah ke konfirmasi hapus");
  frame(); { int x = cx(BKR_LIST), y = bkR[BKR_LIST][1] + 5; tap(x, y); } CHECK(bkArmKind == 0, "memilih baris membatalkan konfirmasi");

  // ---- tanpa pilihan
  bkSel = -1; bkArmKind = 0; tapR(BKR_RESTORE); CHECK(bkArmKind == 0 && gToast.find("Pilih") != std::string::npos, "restore tanpa pilihan ditolak");

  // ---- file rusak: 1 byte dibalik
  { FILE* f = fopen(SDROOT "/backup/nyx_001.nbk", "r+b"); fseek(f, 30, SEEK_SET); int c = fgetc(f); fseek(f, 30, SEEK_SET); fputc(c ^ 0x55, f); fclose(f); }
  // CRC rusak tetap lolos header -> terdaftar "valid" di daftar, tapi RESTORE harus menolak
  bkScan(false);
  nvsSet("ui", "theme", NVS_TYPE_U8, { 42 });
  int idx = -1; for (int i = 0; i < bkListN; i++) if (bkNum[i] == 1) idx = i;
  bkSel = idx; tapR(BKR_RESTORE); tapR(BKR_RESTORE);
  CHECK(nvsGetU8("ui", "theme") == 42, "file korup ditolak, NVS TIDAK berubah");
  CHECK(gToast.find("rusak") != std::string::npos && bkRestartAt == 0, "pesan 'rusak', tanpa restart: '%s'", gToast.c_str());
  // file sampah (bukan backup) -> ditandai rusak di daftar
  { FILE* f = fopen(SDROOT "/backup/nyx_005.nbk", "wb"); fwrite("sampah sampah sampah", 1, 20, f); fclose(f); }
  bkScan(false); CHECK(bkNum[0] == 5 && !bkValid[0], "sampah terdaftar sebagai rusak");
  bkSel = 0; tapR(BKR_RESTORE); CHECK(bkArmKind == 0 && gToast.find("rusak") != std::string::npos, "sampah tidak bisa di-restore");
  // file lain di folder diabaikan
  { FILE* f = fopen(SDROOT "/backup/catatan.txt", "wb"); fputs("x", f); fclose(f); }
  bkScan(false); CHECK(bkListN == 3, "file bukan nyx_NNN.nbk diabaikan (%d)", bkListN);
  sh("rm -f " SDROOT "/backup/nyx_005.nbk " SDROOT "/backup/catatan.txt"); bkScan(false);

  // ---- hapus
  bkSel = 0; int before = bkListN; int num0 = bkNum[0]; char p0[80]; snprintf(p0, sizeof p0, SDROOT "/backup/nyx_%03d.nbk", num0);
  tapR(BKR_DELETE); CHECK(fileExists(p0), "ketukan pertama belum menghapus");
  tapR(BKR_DELETE); CHECK(!fileExists(p0) && bkListN == before - 1 && bkSel == -1, "ketukan kedua menghapus");

  // ---- prune: buat 8 backup -> sisa 6, yang terbaru
  for (int i = 0; i < 8; i++) tapR(BKR_CREATE);
  int files = 0; { DIR* d = opendir(SDROOT "/backup"); dirent* e; while ((e = readdir(d))) if (bkParseName(e->d_name) >= 0) files++; closedir(d); }
  CHECK(files == BK_KEEP && bkListN == BK_KEEP, "prune: %d file di SD, %d di daftar", files, bkListN);
  for (int i = 1; i < bkListN; i++) CHECK(bkNum[i - 1] > bkNum[i], "terurut menurun");

  // ---- kegagalan sebagian saat menulis NVS: dilaporkan, tidak crash
  bkSel = 0; gSetFailEvery = 3; gSetCount = 0; bkRestartAt = 0; tapR(BKR_RESTORE); tapR(BKR_RESTORE); gSetFailEvery = 0;
  CHECK(gToast.find("gagal") != std::string::npos, "kegagalan sebagian dilaporkan: '%s'", gToast.c_str()); bkRestartAt = 0;

  // ================= ENKRIPSI SANDI =================
  {
    seed(); bool secKeep = bkSecrets; if (!bkSecrets) tapR(BKR_TOGGLE);                       // sandi & API key ikut -> paling berisiko bocor
    nvsStr("chess", "bpass", "rahasia-broker");
    auto newest = [&]() { char p[40]; bkMakeName(p, sizeof(p), bkNum[0]); return std::string(SDROOT) + p; };
    auto okBtn = [&]() { frame(); tap(cx(BKR_POK), cy(BKR_POK)); };
    auto typeIn = [&](const char* pw) { CHECK(kbTarget != nullptr, "keyboard terhubung ke kolom sandi"); if (kbTarget) *kbTarget = pw; };
    int before = bkListN; int numBefore = bkListN ? bkNum[0] : 0;

    tapR(BKR_ENC);
    CHECK(bkEnc, "sakelar enkripsi nyala"); { Preferences p; p.begin("nyxbak"); CHECK(p.getBool("enc", false), "sakelar enkripsi tersimpan"); }
    tapR(BKR_CREATE);
    CHECK(bkPrompt == 1 && kbVisible && kbTarget == &bkPassA, "Buat Backup membuka kotak sandi + keyboard");
    CHECK(bkListN == before && (bkListN == 0 || bkNum[0] == numBefore), "belum ada file dibuat sebelum sandi diisi");
    typeIn("abc"); okBtn();
    CHECK(bkPrompt == 1 && !strcmp(bkPromptErr, "Minimal 6 karakter"), "sandi < 6 karakter ditolak");
    typeIn("sandi-rahasia-9"); okBtn();
    CHECK(bkPrompt == 2 && kbTarget == &bkPassB, "lanjut ke 'ulangi sandi'");
    typeIn("sandi-lain-12345"); okBtn();
    CHECK(bkPrompt == 1 && !strcmp(bkPromptErr, "Tidak sama, ulangi") && bkPassA.length() == 0 && bkPassB.length() == 0, "sandi beda -> ulang dari awal, isian dihapus");
    typeIn("sandi-rahasia-9"); okBtn(); typeIn("sandi-rahasia-9"); okBtn();
    CHECK(bkPrompt == 0 && !kbVisible && kbTarget == nullptr, "kotak sandi & keyboard menutup");
    CHECK(bkPassA.length() == 0 && bkPassB.length() == 0, "sandi dihapus dari memori setelah dipakai");
    CHECK(bkListN >= 1 && bkValid[0] && bkEncF[0], "backup terenkripsi muncul di daftar (terkunci)");
    std::string P = newest();
    CHECK(fileHas(P.c_str(), "NYXE1"), "file memakai magic NYXE1");
    CHECK(!fileHas(P.c_str(), "rahasia-wifi") && !fileHas(P.c_str(), "AIza-rahasia") && !fileHas(P.c_str(), "rahasia-broker") && !fileHas(P.c_str(), "Inter") && !fileHas(P.c_str(), "RumahKu"), "TIDAK ada teks apa pun dari pengaturan di file terenkripsi");
    CHECK(bkCnt[0] == 11 && (bkFlg[0] & BK_FLAG_SECRETS), "header (jumlah nilai & flag) tetap terbaca tanpa sandi (%d)", bkCnt[0]);

    // batal
    int nB = bkListN; tapR(BKR_CREATE); CHECK(bkPrompt == 1, "prompt terbuka lagi"); typeIn("setengah-diketik"); frame(); tap(cx(BKR_PCANCEL), cy(BKR_PCANCEL));
    CHECK(bkPrompt == 0 && !kbVisible && bkPassA.length() == 0 && bkListN == nB, "Batal: tutup, isian dihapus, tidak ada file baru");

    // ubah pengaturan -> pulihkan dgn sandi SALAH, lalu BENAR
    nvsSet("ui", "theme", NVS_TYPE_U8, { 9 }); nvsStore().erase(nk("wifi", "pass")); ESP.restarts = 0;
    bkSel = 0; tapR(BKR_RESTORE);
    CHECK(bkArmKind == 1 && bkPrompt == 0, "ketukan pertama Pulihkan: konfirmasi dulu");
    tapR(BKR_RESTORE);
    CHECK(bkPrompt == 3 && kbVisible, "file terkunci -> minta sandi");
    typeIn("salah-sandi"); okBtn();
    CHECK(bkPrompt == 3 && !strcmp(bkPromptErr, "Sandi salah") && nvsGetU8("ui", "theme") == 9 && bkRestartAt == 0, "sandi salah: NVS tidak berubah, tidak restart");
    typeIn("sandi-rahasia-9"); okBtn();
    CHECK(bkPrompt == 0 && nvsGetU8("ui", "theme") == 3 && nvsGetStr("wifi", "pass") == "rahasia-wifi" && nvsGetStr("chess", "bpass") == "rahasia-broker", "sandi benar: pengaturan lengkap dipulihkan");
    CHECK(bkRestartAt != 0 && bkPassA.length() == 0, "restart dijadwalkan, sandi dihapus");
    bkRestartAt = 0;

    // file DIUBAH satu byte -> ditolak walau sandi benar (HMAC)
    { FILE* f = fopen(P.c_str(), "r+b"); fseek(f, 120, SEEK_SET); int c = fgetc(f); fseek(f, 120, SEEK_SET); fputc(c ^ 1, f); fclose(f); }
    nvsSet("ui", "theme", NVS_TYPE_U8, { 9 }); bkSel = 0; bkArmKind = 1; bkArmUntil = gMillis + 4000; tapR(BKR_RESTORE);
    CHECK(bkPrompt == 3, "prompt utk file yg diubah"); typeIn("sandi-rahasia-9"); okBtn();
    CHECK(bkPrompt == 3 && !strcmp(bkPromptErr, "Sandi salah") && nvsGetU8("ui", "theme") == 9, "file diubah: ditolak, NVS aman");
    frame(); tap(cx(BKR_PCANCEL), cy(BKR_PCANCEL));
    CHECK(bkPrompt == 0, "batal dari prompt pulihkan");

    // sakelar mati lagi -> backup biasa tanpa prompt; file biasa tetap bisa dipulihkan tanpa sandi
    nvsSet("ui", "theme", NVS_TYPE_U8, { 3 });
    tapR(BKR_ENC); CHECK(!bkEnc, "sakelar enkripsi mati");
    tapR(BKR_CREATE); CHECK(bkPrompt == 0 && !bkEncF[0] && bkValid[0], "tanpa enkripsi: langsung jadi, file biasa");
    nvsSet("ui", "theme", NVS_TYPE_U8, { 9 }); bkSel = 0; tapR(BKR_RESTORE); tapR(BKR_RESTORE);
    CHECK(bkPrompt == 0 && nvsGetU8("ui", "theme") == 3, "file biasa dipulihkan tanpa sandi"); bkRestartAt = 0;
    if (bkSecrets != secKeep) tapR(BKR_TOGGLE);
    seed();
  }

  // ---- SD tidak ada
  sdReady = false; bkScan(false); CHECK(bkListN == 0, "tanpa SD daftar kosong");
  tapR(BKR_CREATE); CHECK(gToast.find("SD") != std::string::npos, "pesan SD tidak terdeteksi: '%s'", gToast.c_str());
  frame(); sdReady = true; bkScan(false);

  // ---- NVS kosong tetap menghasilkan backup sah
  nvsStore().clear(); tapR(BKR_CREATE); CHECK(bkValid[0] && bkCnt[0] == 0, "backup dari NVS kosong sah (0 nilai)");

  // ---- tata letak: potret kecil, potret besar, lanskap -> semua rect di dalam layar & tidak tumpang tindih tombol
  struct { int w, h; } dims[] = { { 240, 320 }, { 320, 480 }, { 480, 320 }, { 320, 240 } };
  seed(); for (int i = 0; i < 6; i++) tapR(BKR_CREATE);
  for (auto& d : dims) {
    SCR_W = d.w; SCR_H = d.h; bkLayout(); frame();
    bool inside = true; for (int r = 0; r < BKR_N; r++) inside = inside && bkR[r][0] >= 0 && bkR[r][1] >= 0 && bkR[r][0] + bkR[r][2] <= SCR_W && bkR[r][1] + bkR[r][3] <= SCR_H - 8 && bkR[r][3] > 0;
    CHECK(inside, "%dx%d: semua rect di dalam layar", d.w, d.h);
    CHECK(bkR[BKR_LIST][3] >= BK_ROW_H, "%dx%d: daftar muat >= 1 baris (h=%d)", d.w, d.h, bkR[BKR_LIST][3]);
    bool ov = false; for (int a = 0; a < BKR_PBOX; a++) for (int b = a + 1; b < BKR_PBOX; b++) {
      bool sx = bkR[a][0] < bkR[b][0] + bkR[b][2] && bkR[b][0] < bkR[a][0] + bkR[a][2], sy = bkR[a][1] < bkR[b][1] + bkR[b][3] && bkR[b][1] < bkR[a][1] + bkR[a][3]; if (sx && sy) ov = true; }
    CHECK(!ov, "%dx%d: tidak ada rect tumpang tindih", d.w, d.h);
    CHECK(bkR[BKR_POK][1] + bkR[BKR_POK][3] <= kbY() && bkR[BKR_PBOX][1] + bkR[BKR_PBOX][3] <= kbY(), "%dx%d: kotak sandi tidak tertutup keyboard (kbY=%d)", d.w, d.h, kbY());
    // scroll: geser jari -> posisi berubah, dalam batas, dan TIDAK memicu ketukan
    int maxS = bkListN * BK_ROW_H - bkR[BKR_LIST][3];
    bkScroll = 0; bkSel = -1;
    int x = cx(BKR_LIST), y0 = bkR[BKR_LIST][1] + bkR[BKR_LIST][3] - 6;
    backupTouch(x, y0, true, true); backupTouch(x, y0 - 30, true, false); backupTouch(x, y0 - 90, true, false); backupTouch(x, y0 - 90, false, false);
    if (maxS > 0) CHECK(bkScroll > 0 && bkScroll <= maxS, "%dx%d: drag menggulir (%.0f/%d)", d.w, d.h, bkScroll, maxS);
    CHECK(bkSel == -1, "%dx%d: geser tidak memilih baris", d.w, d.h);
    backupTouch(x, y0, true, true); backupTouch(x, y0 + 200, true, false); backupTouch(x, y0 + 200, false, false);
    CHECK(bkScroll >= 0, "%dx%d: scroll tidak negatif", d.w, d.h);
  }
  SCR_W = 320; SCR_H = 480; bkLayout();

  // ---- tombol Kembali
  gWentBack = false; backupTouch(160, 9500, true, true); CHECK(gWentBack, "isBack -> navBack");

  printf("%d pemeriksaan, %d gagal\n", checks, fails);
  return fails ? 1 : 0;
}
