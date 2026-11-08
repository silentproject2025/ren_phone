// glue_test.cpp -- kompilasi + JALANKAN kamus_renphone.ino terhadap mock Arduino.
// Jalankan (dari folder repo): g++ -std=c++11 -Wall -Wextra -I. -Itests_kamus -o /tmp/glue_test tests_kamus/glue_test.cpp && /tmp/glue_test
#include "mock_arduino.h"

// ---- yang biasanya disediakan phone.ino / chat_renphone.ino
int SCR_W = 320, SCR_H = 240; bool needRedraw = false, sdReady = false, kbVisible = false;
String* kbTarget = nullptr; String noteText, aiPrompt, otherField; static String chatInput;
FSMock SD_MMC;
int kbKeyW() { return (SCR_W - 8 - 18) / 10; }
int kbLeft() { return (SCR_W - (10 * kbKeyW() + 18)) / 2; }
int kbBarH();

#include "../kamus_renphone.ino"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if(!(c)){ fails++; printf("GAGAL  baris %d  ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

// meniru kbTouch: huruf / spasi / hapus
static void typeStr(String& t, const char* s) {
  for (; *s; s++) { kbSugOnChar(t, *s); t += *s; }
}
static void backspace(String& t) { if (!kbSugBackspace(t)) { if (t.length()) t.remove(t.length() - 1); } }
static void reboot() { kmsInited = false; kmsUserN = 0; kmsAc.valid = false; kmsSigLast = 0; memset(kmsSeen, 0, sizeof(kmsSeen)); }
static int chipX() { return kbLeft() + 5; }
static int slotX(int i) { int L = kbLeft(), R = SCR_W - L; int sx = L + KMS_CHIP_W + 4, w = (R - sx) / KMS_SUG; return sx + i * w + w / 2; }

int main() {
  // ---- tinggi baris saran per field
  kbVisible = true;
  kbTarget = &otherField;  CHECK(kbBarH() == 0, "field lain: tanpa baris saran");
  kbTarget = &noteText;    CHECK(kbBarH() == KMS_BAR_H, "catatan: ada baris saran");
  kbTarget = &aiPrompt;    CHECK(kbBarH() == 0, "AI prompt: belum didukung (layar tidak menampilkan ketikan)");
  kbTarget = &chatInput;   CHECK(kbBarH() == KMS_BAR_H, "chat");
  kbVisible = false;       CHECK(kbBarH() == 0, "keyboard tertutup: 0");
  kbVisible = true; kbTarget = &noteText;

  // ---- autokoreksi + undo + belajar + persist
  noteText = "";
  typeStr(noteText, "saya mau makan nasii ");
  CHECK(noteText == "saya mau makan nasi ", "autokoreksi: '%s'", noteText.c_str());
  backspace(noteText);
  CHECK(noteText == "saya mau makan nasii", "undo mengembalikan ketikan asli: '%s'", noteText.c_str());
  CHECK(kmsUserN == 1 && strcmp(kmsUserBuf[0], "nasii") == 0, "kata dipelajari");
  typeStr(noteText, " ");
  CHECK(noteText == "saya mau makan nasii ", "tidak dikoreksi lagi: '%s'", noteText.c_str());
  backspace(noteText);                               // hapus biasa (bukan undo)
  CHECK(noteText == "saya mau makan nasii", "hapus biasa: '%s'", noteText.c_str());
  reboot();
  noteText = ""; typeStr(noteText, "nasii ");
  CHECK(noteText == "nasii ", "kata pelajaran bertahan setelah reboot: '%s'", noteText.c_str());

  // ---- undo hanya berlaku TEPAT setelah koreksi
  noteText = ""; typeStr(noteText, "kamuu ");
  CHECK(noteText == "kamu ", "koreksi kamuu: '%s'", noteText.c_str());
  typeStr(noteText, "x");                            // ketik huruf lain -> undo hangus
  backspace(noteText);
  CHECK(noteText == "kamu ", "undo hangus setelah ketik lain: '%s'", noteText.c_str());

  // ---- tanda baca sebagai pemisah
  noteText = ""; typeStr(noteText, "apa kabarr?");
  CHECK(noteText == "apa kabar?", "koreksi sebelum '?': '%s'", noteText.c_str());

  // ---- ketuk saran
  noteText = ""; typeStr(noteText, "mak");
  LGFX_Sprite sp; kbBarDraw(sp, 100);
  CHECK(kmsCur.n == KMS_SUG && sp.draws >= 3, "saran tergambar (n=%d draws=%d)", kmsCur.n, sp.draws);
  std::string first = kmsCur.w[0];
  kbBarTouch(slotX(0), 105);
  CHECK(noteText == (first + " ").c_str(), "ketuk saran 1 -> '%s' (harusnya '%s ')", noteText.c_str(), first.c_str());

  // ---- sorotan: kata yang akan dikoreksi tampil sebagai saran terpilih
  noteText = ""; typeStr(noteText, "terimaa"); kbBarDraw(sp, 100);
  CHECK(kmsFixIdx >= 0 && strcmp(kmsCur.w[kmsFixIdx], "terima") == 0, "slot koreksi = terima (idx %d)", kmsFixIdx);

  // ---- ganti mode lewat tombol "ab": 2 -> 1 -> 0 -> 2
  CHECK(kmsMode == 2, "mode awal 2");
  kbBarTouch(chipX(), 105); CHECK(kmsMode == 1 && gToast == "Kamus: saran saja", "mode 1 (%s)", gToast.c_str());
  noteText = ""; typeStr(noteText, "nasiii ");
  CHECK(noteText == "nasiii ", "mode saran-saja: tidak ada autokoreksi");
  noteText = ""; typeStr(noteText, "ter"); kbBarDraw(sp, 100);
  CHECK(kmsCur.n > 0, "mode saran-saja: saran tetap muncul");
  kbBarTouch(chipX(), 105); CHECK(kmsMode == 0 && gToast == "Kamus: mati", "mode 0");
  kbBarDraw(sp, 100); CHECK(kmsCur.n == 0, "mode mati: tanpa saran");
  kbBarTouch(slotX(0), 105); CHECK(noteText == "ter", "mode mati: ketuk slot tidak mengubah teks");
  kbBarTouch(chipX(), 105); CHECK(kmsMode == 2 && gToast == "Kamus: saran + koreksi", "kembali ke mode 2");
  reboot(); kbVisible = true; kbTarget = &noteText; kmsInit();
  CHECK(kmsMode == 2, "mode tersimpan di NVS");

  // ---- field yang tidak didukung tidak disentuh sama sekali
  kbTarget = &otherField; otherField = ""; typeStr(otherField, "passwordd ");
  CHECK(otherField == "passwordd ", "field lain tidak dikoreksi");
  kbTarget = &noteText;

  // ---- kata kapital / nama tidak dikoreksi
  noteText = ""; typeStr(noteText, "ketemu Budi dan Sitii ");
  CHECK(noteText == "ketemu Budi dan Sitii ", "nama tidak disentuh: '%s'", noteText.c_str());

  // ---- bahasa: ketik Inggris terus -> saran Inggris didahulukan
  noteText = "";
  for (int i = 0; i < 8; i++) { typeStr(noteText, "the house because "); }
  CHECK(kmsC.preferEn == 1, "preferEn aktif setelah mengetik Inggris");
  noteText = ""; typeStr(noteText, "th"); kbBarDraw(sp, 100);
  CHECK(kmsCur.n > 0 && strcmp(kmsCur.w[0], "the") == 0, "saran 'th' -> the (dapat %s)", kmsCur.n ? kmsCur.w[0] : "-");

  // ---- kamus SD
  system("mkdir -p /tmp/kms_sd && printf 'zebra\\nKUDA 12345\\nxyz!\\n\\nbuku,1\\r\\nkecoak\\n' > /tmp/kms_sd/kamus.txt");
  sdReady = true; reboot(); kbVisible = true; kbTarget = &noteText; kmsInit();
  CHECK(kmsSdN == 4, "SD: 4 kata valid dimuat (dapat %d)", kmsSdN);
  noteText = ""; typeStr(noteText, "zeb"); kbBarDraw(sp, 100);
  CHECK(kmsCur.n > 0 && strcmp(kmsCur.w[0], "zebra") == 0, "saran dari SD: %s", kmsCur.n ? kmsCur.w[0] : "-");
  noteText = ""; typeStr(noteText, "zebra ");
  CHECK(noteText == "zebra ", "kata SD tidak dikoreksi");

  // ---- batas: kata sangat panjang tidak crash
  noteText = ""; typeStr(noteText, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "); kbBarDraw(sp, 100);
  CHECK(true, "kata sangat panjang aman");

  printf("%d pengecekan, %d gagal\n", checks, fails);
  return fails ? 1 : 0;
}
