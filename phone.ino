// =================================================================
// ren_phone v9 — FIX bug lock screen tidak bisa diusap ke atas
// (kalibrasi touch & rotasi layar sekarang SELALU sinkron)
// ESP32-S3, ILI9341, XPT2046 touch, SD via SDIO
//
// PERBAIKAN v9 (berdasarkan laporan user: "abis kalibrasi, lock screen
// gak bisa diusap ke atas"):
//  1. AKAR MASALAH DITEMUKAN: di v8 (dan versi2 sebelumnya), urutan di
//     setup() itu display.setRotation(0) (paksa PORTRAIT) -> KALIBRASI
//     TOUCH dijalankan di rotasi portrait itu -> baru BELAKANGAN
//     loadOrientPref()+applyOrientation() menerapkan orientasi yang
//     SEBENARNYA tersimpan di NVS (yang bisa saja LANDSCAPE, misalnya
//     peninggalan dari firmware versi lama yang defaultnya landscape,
//     atau dari sesi sebelumnya waktu user pernah pindah ke landscape
//     lewat Control Center). Kalau itu terjadi: kalibrasi terekam utk
//     rotasi portrait, tapi device jalan di rotasi landscape. XPT2046
//     di LovyanGFX itu pemetaan sumbu X/Y-nya terikat ke rotasi yang
//     aktif SAAT kalibrasi dilakukan -> begitu rotasi runtime beda dari
//     rotasi kalibrasi, sumbu X/Y touch jadi tertukar. Akibatnya usapan
//     FISIK ke atas di lock screen malah kebaca sebagai gerakan
//     menyamping oleh kode (krn lockScreenInput() cuma mengecek
//     dy = startY-ty), jadi swipe-to-unlock tidak pernah ter-trigger.
//     Inilah kenapa di kode lama (yang SELALU setRotation(1)/landscape
//     DULU baru kalibrasi, dengan default orientasi landscape juga)
//     bug ini tidak pernah muncul: rotasi kalibrasi & rotasi runtime
//     selalu sama persis, di orientasi apapun user berada.
//  2. FIX: setup() sekarang membaca loadOrientPref() DI PALING AWAL,
//     lalu langsung display.setRotation() sesuai orientasi tersimpan
//     itu SEBELUM boot sequence & SEBELUM kalibrasi touch dijalankan.
//     Dengan ini, kalibrasi SELALU terjadi tepat di rotasi yang bakal
//     dipakai saat runtime -- baik portrait maupun landscape -- jadi
//     tidak akan pernah mismatch lagi. Ini juga sekalian membenahi efek
//     samping kecil: dulu animasi boot SELALU main di portrait dulu baru
//     "dibanting" ke landscape sesaat setelahnya kalau orientasi
//     tersimpannya landscape; sekarang boot sequence langsung main di
//     orientasi yang benar dari awal. applyOrientation() tetap dipanggil
//     belakangan seperti biasa, sekarang cuma tugasnya merapikan ukuran
//     canvas/canvasApp & memuat isi Canvas app -- bukan lagi penentu
//     rotasi yang dipakai saat kalibrasi.
//
// =================================================================
// v10 — UPDATE (permintaan user):
//  1. Game Mode sekarang animasi "game booster" ala HP gaming (ring
//     energi memancar, sapuan cahaya, kilat, progress bar bertahap)
//     ketimbang cuma teks statis diam.
//  2. Semua ikon aplikasi (termasuk game yg dulu cuma huruf polos)
//     sekarang punya logo vektor sendiri lewat drawAppIcon().
//  3. Tambah kalibrasi MPU6050 (offset accel & gyro) yang bisa
//     dijalankan dari halaman Setting, hasilnya disimpan permanen.
// =================================================================
// v11 — UPDATE (permintaan user):
//  1. Animasi "Game Booster" diganti total: yg lama kesannya ramai/norak
//     (ring memancar, sapuan cahaya, kilat, teks besar "GAME BOOSTER").
//     Sekarang jadi loading ring simpel & bersih dgn easing halus
//     (pakai fungsi easing yg sama dgn transisi antar layar).
//  2. AKAR MASALAH "logo game gak kelihatan" DITEMUKAN: initAppColors()
//     ternyata TIDAK PERNAH mengisi warna utk index 11-15 (Snake,
//     Flappy, 2048, TicTacToe, Breakout) -- warnanya default 0 alias
//     HITAM. Karena lingkaran latar ikon jadi hitam total, garis vektor
//     ikon (yg warnanya T().bg, ikut gelap) jadi nyaris tak kelihatan di
//     atasnya. Sekarang semua game dapat warna cerah tersendiri.
//  3. Control Center dirombak: tiap tombol sekarang pakai IKON vektor
//     (drawCCIcon) + label singkat di bawahnya -- bukan kotak polos
//     isi tulisan status doang. Jarak antar kartu (ccGap/ccCardH)
//     diperbesar biar ikonnya lebih lega, gak sempit.
//  4. Animasi buka/tutup Control Center diganti dari formula ad-hoc
//     per-frame jadi animasi berbasis WAKTU (millis) + easing yg sama
//     dgn transisi navigasi -> gerakannya mulus & konsisten kecepatannya
//     berapa pun framerate loop() saat itu (dulu bisa keliatan patah2
//     kalau loop lagi sibuk mis. pas WiFi/AI jalan).
//  5. Scroll Home dibikin lebih smooth: kecepatan gesture di-low-pass
//     (bukan diganti mentah tiap frame), decay momentum dibuat lbh
//     landai, + sedikit efek elastis di ujung atas/bawah list.
//  6. Canvas: goresan gambar dulu dibikin dgn drawLine() yg digeser-
//     geser per pixel ketebalan -> keliatan bertangga/kotak-kotak,
//     apalagi brush besar atau gerakan diagonal cepat. Sekarang goresan
//     "distempel" pakai fillCircle yg diinterpolasi sepanjang jalur
//     gerakan jari (canvasStampStroke), jadi mulus & tanpa celah.
// =================================================================
// v12 — UPDATE (permintaan user):
//  1. Control Center dibuat lebih RINGKAS & "mirip HP": panel sekarang
//     kartu mengambang dgn inset margin kiri/kanan + sudut membulat
//     (dulu nempel penuh ke tepi layar, sudut lancip), dan tiap tombol
//     dibuat lebih kecil/padat (ccCardH dulu 48/64px, sekarang 38/52px).
//  2. Scroll teks dibuat lebih smooth: AI Chat & viewer isi file di File
//     Explorer sekarang punya momentum (low-pass velocity + decay stlh
//     jari dilepas) senada dgn gaya scroll Home, bukan cuma geser 1:1
//     ikut jari lalu berhenti mendadak.
//  3. File Explorer: BARU - isi file (.txt dsb) sekarang bisa di-scroll
//     dgn drag jari (dulu teks panjang meluber keluar kotak & gak bisa
//     dibaca semua). Dibungkus+di-wrap+di-clip+ada scrollbar tipis,
//     reuse pola & fungsi yg sama persis dgn AI Chat (AiLine+aiWrapAppend).
//  4. Tema boot sequence diperbarui: latar jadi gradient halus (dulu
//     hitam polos), animasi logo/hexagon pakai easing yg sama dgn
//     transisi navigasi (dulu progres linear), ada halo lembut di
//     belakang logo, judul muncul dgn fade halus (dulu nongol mendadak),
//     & loader titik di stage 2 berdenyut ukurannya (dulu cuma kedip
//     nyala/mati). Sengaja dibuat tetap bersih/elegan (bukan "ramai"),
//     senada dgn keputusan v11 yg menyederhanakan Game Booster.
// =================================================================
// v13 — UPDATE (permintaan user: "sistem beneran smooth" + Control Center
// bulat + boot sequence minim warna):
//  1. Transisi navigasi antar layar (playNavTransition) dulu jalan dgn
//     JUMLAH FRAME TETAP (12 frame + delay 6ms/frame) -- durasi totalnya
//     bisa molor kalau render sedang berat (layar kompleks, atau SPI lg
//     dipakai bareng WiFi/AI). Sekarang dibuat BERBASIS WAKTU (millis),
//     pola yg sama dgn fix ccOffset Control Center di v11 -- durasi total
//     transisi SELALU konsisten ~170ms di kondisi hardware apapun.
//  2. Control Center dirombak dari kartu kotak (fillRoundRect) jadi TOMBOL
//     BULAT (fillCircle) dgn label di LUAR/bawah lingkaran -- persis pola
//     quick-toggle di HP asli. Diameter lingkaran (ccCircleD) dihitung dari
//     lebar kolom panel (otomatis menyesuaikan lebar layar/orientasi) tapi
//     dipagari batas atas yg sengaja dikecilkan sedikit drpd ukuran kartu
//     v12 (38/52px -> skrg diameter dasar 30/40px). Area sentuh (ccTouch)
//     tetap 1 sel penuh (lingkaran+label) biar tetap gampang di-tap.
//     Slider Brightness jg dpt handle bulat di ujungnya, senada gaya bulat
//     yg baru.
//  3. Boot sequence: dulu stage "Ren Phone" pakai aksen oranye + gradient
//     latar biru, stage "SanzX OS" pakai aksen cyan + gradient latar navy
//     (2 hue berbeda + latar berwarna = kesan "banyak warna"). Sekarang
//     KEDUA stage berbagi SATU palet monokrom gelap yg sama (BOOT_ACCENT
//     putih, BOOT_GLOW abu-kebiruan redup, latar nyaris hitam polos) --
//     boot sequence terasa satu tema dark yg konsisten, bukan dua tema
//     warna yg bertukar.
// =================================================================
// v14 — UPDATE (permintaan user: scroll lebih smooth + fitur Dynamic Island
// + game Trivia Quiz pakai API):
//  1. Momentum scroll (Home / AI Chat / File Explorer) dulu decay-nya
//     (*=0.92 tiap iterasi loop()) TIDAK dinormalisasi waktu -- sama persis
//     dgn akar masalah transisi navigasi sebelum v13: kalau loop() lg berat
//     (render kompleks/WiFi & HTTP jalan bareng), jumlah iterasi per detik
//     turun, jadi decay-nya ikut kerasa lebih "lambat berhenti" krn faktor
//     0.92 diterapkan lebih jarang. Sekarang decay-nya dinormalisasi
//     terhadap dt (waktu nyata antar frame, bukan jumlah iterasi), pola yg
//     SAMA PERSIS dgn fix playNavTransition() di v13 -- jadi kecepatan
//     berhenti momentum scroll konsisten di kondisi hardware apapun.
//  2. FITUR BARU: Dynamic Island -- pil hitam mengambang di tengah atas
//     layar (persis konsep "Dynamic Island" HP modern) yg BERMORFING antara
//     status IDLE (tersembunyi) / COMPACT (pil kecil + indikator/skor) /
//     EXPANDED (melebar dgn ikon+teks) pakai animasi berbasis waktu DENGAN
//     easing "overshoot" (dikit "mantul" di ujung gerakan, beda dr navEase
//     yg datar) supaya morph-nya kerasa kenyal/hidup, bukan geser kaku.
//     Terintegrasi ke AI Chat (status "sedang berpikir" -> "jawaban siap"),
//     Mode Game (notifikasi boost performa), & Trivia Quiz (skor live +
//     feedback benar/salah). Ketuk pil-nya bisa memekarkan lagi isinya,
//     dan utk AI Chat bisa langsung lompat ke app-nya.
//  3. FITUR BARU: App Trivia Quiz -- soal diambil real-time dari Open Trivia
//     Database (opentdb.com) lewat HTTPClient async (task terpisah spt pola
//     Gemini AI, biar UI gak nge-freeze nunggu respons server). 12 tema
//     soal (Umum, Sains, Komputer, Olahraga, Geografi, Sejarah, Film,
//     Musik, Video Game, Hewan, Anime, Mitologi) x 4 tingkat kesulitan x
//     3 pilihan jumlah soal (5/10/15) = banyak kombinasi permainan. Body
//     JSON di-decode manual (base64 -> HTML entity) tanpa perlu library
//     ArduinoJson, senada gaya parsing manual yg sudah dipakai di
//     doGeminiHttpRequest(). Skor & feedback tiap jawaban nongol di
//     Dynamic Island.
// =================================================================
// v15 — FIX BUILD (root cause & perbaikan):
//  1. AKAR MASALAH build gagal ("'TriviaLayout' does not name a type" lalu
//     diikuti "'diNotify' was not declared in this scope"): struct
//     TriviaLayout (dipakai sbg TIPE RETURN fungsi triviaCalcLayout())
//     didefinisikan di TENGAH file, dekat kode Trivia -- PERSIS pola bug
//     yang sudah pernah dialami & didokumentasikan sebelumnya utk AiLine,
//     Vec3f, dan NavAnim (lihat komentar di dekat definisi struct2 itu di
//     atas). arduino-cli men-generate prototype utk SEMUA fungsi di
//     puncak file SEBELUM badan kode lain terbaca. Karena prototype
//     "TriviaLayout triviaCalcLayout();" ikut digenerate di puncak file
//     padahal struct TriviaLayout-nya sendiri baru didefinisikan jauh di
//     bawah, baris prototype itu gagal dikompilasi ("does not name a
//     type"). Kegagalan pada satu baris di blok prototype yang digenerate
//     itu membuat parser gagal memproses lanjutan blok prototype dengan
//     benar (efek domino/cascading), sehingga prototype fungsi lain yang
//     seharusnya valid -- termasuk diNotify() -- ikut tidak ter-generate,
//     makanya sendGeminiRequest()/triggerGeminiAI() yang memanggil
//     diNotify() sebelum definisi aslinya jadi error "was not declared".
//  2. FIX: struct TriviaLayout dipindahkan ke PUNCAK FILE, tepat setelah
//     definisi struct Vec3f (bergabung dgn AiLine/Vec3f yang memang sudah
//     ditaruh di sana justru karena alasan yang sama). Definisi struct
//     yang lama di bagian APP: TRIVIA QUIZ dihapus (tidak didefinisikan
//     dua kali), fungsi triviaCalcLayout() di lokasi aslinya tetap sama,
//     cuma tinggal memakai struct yang sudah dikenal dari puncak file.
//     Dengan ini seluruh blok prototype otomatis kembali valid dari awal
//     sampai akhir, dan error diNotify() ikut hilang tanpa perlu diubah
//     sama sekali (diNotify tidak pernah salah dari awal).
// =================================================================
// v16 — FIX BUILD (root cause & perbaikan, KALI INI diNotify() SUNGGUHAN
// yang salah -- v15 salah diagnosa soal ini):
//  1. AKAR MASALAH: build masih gagal dgn error yang SAMA PERSIS spt
//     sebelum v15 ("'diNotify' was not declared in this scope" di
//     sendGeminiRequest() & triggerGeminiAI()), padahal struct
//     TriviaLayout sudah dipindah ke puncak file di v15. Ini membuktikan
//     penyebab errornya BUKAN cascading dari TriviaLayout (itu memang
//     valid utk dibenahi, tapi bukan akar masalah diNotify).
//     PENYEBAB SEBENARNYA: fungsi diNotify() adalah SATU-SATUNYA fungsi
//     di seluruh file ini yang parameter terakhirnya punya DEFAULT VALUE
//     (const String& badge=""). arduino-cli memakai ctags utk men-scan &
//     meng-generate prototype semua fungsi secara otomatis di puncak
//     file SEBELUM badan definisi aslinya terbaca -- tapi ctags versi yg
//     dipakai gagal mem-parse signature fungsi yang punya default
//     parameter bertipe referensi dgn literal string (=""), sehingga
//     prototype "void diNotify(...)" itu SATU-SATUNYA yang gagal
//     digenerate, sementara fungsi lain (diLink, diHide, dst - yang
//     TIDAK punya default parameter) tetap berhasil ter-generate dgn
//     benar. Makanya errornya SELALU spesifik ke diNotify() saja, tidak
//     pernah ke fungsi Dynamic Island lainnya, di versi manapun dari v14
//     sampai v15.
//  2. FIX: tambahkan forward declaration MANUAL utk diNotify() di puncak
//     file (persis di bawah definisi struct2 AiLine/Vec3f/TriviaLayout),
//     lengkap dengan default value-nya. Dengan deklarasi manual ini,
//     compiler sudah mengenal signature diNotify() (termasuk default
//     value badge) jauh sebelum sendGeminiRequest()/triggerGeminiAI()
//     memanggilnya -- tidak lagi bergantung pada prototype otomatis
//     ctags yang terbukti gagal utk kasus ini. Default value ""
//     dihapus dari definisi ASLI diNotify() di bagian DYNAMIC ISLAND
//     (tetap di deklarasi saja), karena C++ melarang default argument
//     yang sama dideklarasikan dua kali (di forward declaration DAN di
//     definisi) -- aturan standar C++, bukan hal spesifik proyek ini.
// =================================================================
// v17 — UPDATE (permintaan user: transisi antar aplikasi lebih smooth/gak
// patah-patah + Dynamic Island jangan sampai ada teks yang keluar dari pil):
//  1. AKAR MASALAH transisi "patah-patah": playNavTransition() menggambar
//     tiap frame animasi dengan MEMANGGIL DUA pushSprite() terpisah ke
//     `display` (transShot lalu canvas). Tanpa startWrite()/endWrite()
//     eksplisit, LovyanGFX membuka & menutup TRANSAKSI SPI SENDIRI-SENDIRI
//     utk tiap pushSprite() (assert chip-select, kirim command jendela
//     alamat, lock bus, lalu lepas lagi) -- overhead buka-tutup ini terjadi
//     2x per frame, di atas biaya transfer piksel yang sebenarnya. Karena
//     durasi transisi sengaja dibuat pendek (170ms, lihat NAV_TRANS_MS),
//     overhead ini jadi porsi yang signifikan dari waktu tiap frame,
//     sehingga jumlah frame yang sempat tergambar sedikit & timing-nya gak
//     merata -> terlihat/terasa tersendat, apalagi kalau SPI lagi dipakai
//     bareng proses lain (mis. AI Chat/WiFi jalan di background).
//     FIX: seluruh loop animasi transisi sekarang dibungkus SATU pasang
//     display.startWrite()/display.endWrite(). Transaksi SPI dibuka SEKALI
//     di awal & tetap terbuka sepanjang animasi -- tiap pushSprite() di
//     dalam loop tinggal menumpang transaksi yang sudah terbuka (tanpa
//     lock/assert-CS/kirim-command berulang), sehingga tiap frame jauh
//     lebih ringan & gerakannya terasa mulus, tidak tersendat lagi.
//  2. AKAR MASALAH teks Dynamic Island "keluar" dari pil: ADA DUA penyebab.
//     Pertama, pil dulu punya DUA baris teks (title + subtitle), tapi lebar
//     pil (diTargetW) CUMA dihitung dari panjang title -- giliran subtitle-
//     nya lebih panjang dari title (mis. title "Mode Game" tapi subtitle
//     "Performa dimaksimalkan"), subtitle itu meleber keluar krn pilnya
//     sendiri dibuat sempit mengikuti title yang lebih pendek. Kedua, bahkan
//     utk SATU baris title pun: kalau title dibuat dari gabungan variabel
//     (mis. nama kategori trivia + jumlah soal) dan jadi lebih panjang dari
//     muat maksimum pil (SCR_W-28), diTargetW() meng-CLAMP lebar pilnya ke
//     batas itu, TAPI teks yang digambar TIDAK ikut dipotong/disesuaikan ke
//     lebar yang sudah di-clamp -> tetap digambar penuh sepanjang aslinya,
//     otomatis meleber keluar begitu klem lebar itu aktif.
//     FIX (3 lapis): (a) subtitle DIHAPUS TOTAL dari Dynamic Island -- skrg
//     cuma SATU baris teks singkat saja (persis spt diminta: mis. "Mode
//     Game Aktif", tanpa kalimat penjelasan di baris bawahnya), semua
//     pemanggil diNotify() diubah supaya pesannya singkat & padat dalam
//     SATU frasa. (b) Fungsi baru diFitTitle() memotong (truncate + "..")
//     title SEBELUM disimpan kalau ternyata lebih panjang dari muat
//     maksimum pil, pakai rumus lebar yang SAMA PERSIS dengan diTargetW()
//     supaya lebar pil & panjang teks yang akhirnya digambar SELALU
//     konsisten satu sama lain. (c) Sebagai jaring pengaman terakhir,
//     drawDynamicIsland() sekarang membungkus SEMUA isi pil (ikon, teks,
//     badge, progress bar) dengan setClipRect() persis di batas pil --
//     walau ada kasus tak terduga lain nanti, potongan piksel teks TIDAK
//     PERNAH bisa tergambar keluar dari bentuk pil lagi.
// =================================================================
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "FS.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include <Update.h>   // OTA: flashing firmware baru (dari SD lokal atau WiFi)
#include <math.h>     // dipakai boot sequence (cosf/sinf utk animasi logo) + sensor 3D
#include <Wire.h>     // I2C utk MPU6050 (accel+gyro)

// ---- MJPEG PLAYER: dependensi tambahan ----
#include <JPEGDEC.h>
#include "MjpegClass.h"

// =============================================
// TYPE DEFINITIONS
// =============================================
enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SENSOR,
              SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS, SCR_AICHAT, SCR_FILEEXPLORER,
              SCR_MJPEG, SCR_UPDATE, SCR_BATTERY, SCR_SNAKE, SCR_FLAPPY,
              SCR_2048, SCR_TTT, SCR_BREAKOUT, SCR_TRIVIA };
enum Orientation { ORIENT_LANDSCAPE = 0, ORIENT_PORTRAIT = 1 };

// Dipindah ke atas — alasan sama dengan AiLine & Vec3f: arduino-cli men-generate
// prototype untuk playNavTransition(NavAnim anim) di puncak file; kalau NavAnim
// baru dideklarasikan di tengah file (dekat fungsinya), prototype itu digenerate
// SEBELUM definisi enum ini terbaca → error "NavAnim was not declared in this scope".
enum NavAnim { NAV_ANIM_PUSH, NAV_ANIM_BACK, NAV_ANIM_HOME };

// Dipindah ke atas (sebelum semua definisi fungsi) supaya prototype otomatis
// yang di-generate arduino-cli di puncak file sudah kenal tipe ini duluan.
// Kalau struct ini didefinisikan di tengah file, build GAGAL dengan error
// "AiLine was not declared in this scope" karena prototype fungsi yang
// memakainya di-generate SEBELUM baris struct ini kalau ditaruh di bawah.
struct AiLine { String text; uint16_t color; };

// Sama seperti AiLine di atas: dipakai sbg parameter fungsi rot3f() di app
// "Orientasi 3D", jadi HARUS didefinisikan di sini (bukan di dekat fungsinya)
// supaya prototype otomatis arduino-cli tidak gagal.
struct Vec3f { float x,y,z; };

// v15 FIX: sama persis dgn AiLine/Vec3f di atas -- struct TriviaLayout
// dipakai sbg TIPE RETURN fungsi triviaCalcLayout() (app Trivia Quiz).
// Sebelumnya struct ini didefinisikan di tengah file (dekat kodenya di
// bagian APP: TRIVIA QUIZ), sehingga prototype otomatis
// "TriviaLayout triviaCalcLayout();" yang digenerate arduino-cli di
// PUNCAK file gagal dikompilasi ("TriviaLayout does not name a type"),
// dan itu bikin sisa blok prototype ikut gagal ter-generate dgn benar
// (termasuk prototype diNotify() yang jadi keliatan "not declared" di
// sendGeminiRequest()/triggerGeminiAI() walau diNotify sendiri benar).
// Pindahkan ke sini -> masalah selesai dari akarnya.
struct TriviaLayout{ int diffY, amtY, startY, catBottom; };

// v16 FIX: forward declaration MANUAL untuk diNotify() -- lihat catatan
// lengkap "v16" di paling atas file. Singkatnya: diNotify() adalah
// SATU-SATUNYA fungsi di file ini dengan default parameter
// (const String& badge=""), dan itu bikin ctags (dipakai arduino-cli utk
// auto-generate prototype fungsi di puncak file) gagal mem-parse &
// men-generate prototype KHUSUS untuk fungsi ini -- fungsi lain yang
// tidak punya default parameter (diLink, diHide, dst) tetap berhasil
// ter-generate otomatis seperti biasa. Karena prototype-nya tidak pernah
// ada, sendGeminiRequest()/triggerGeminiAI() (dipanggil jauh lebih awal
// di file, sebelum definisi asli diNotify() di bagian DYNAMIC ISLAND)
// gagal mengenali fungsi ini -> error "was not declared in this scope".
// Dengan deklarasi manual di sini (lengkap dgn default value badge=""),
// compiler sudah kenal signature diNotify() dari awal, jadi tidak lagi
// bergantung pada prototype otomatis yang terbukti gagal utk kasus ini.
// v17: parameter 'subtitle' DIHAPUS dari signature -- Dynamic Island
// sekarang cuma menampilkan SATU baris teks (lihat catatan "v17" di
// paling atas file & di dekat definisi asli diNotify() utk alasannya).
void diNotify(char icon, const String& title, uint16_t color,
              unsigned long expandMs, bool keepAliveCompact, const String& badge = "");

struct Theme {
  const char* name;
  uint16_t bg, surface, surface2, accent, accent2,
           text, subtext, divider, good, danger;
};

Theme themes[] = {
  { "Dark",   0x1084,0x2104,0x31A6,0xFD40,0x04FF,0xFFFF,0x8C51,0x2965,0x07E0,0xF800 },
  { "AMOLED", 0x0000,0x1082,0x2104,0xFD40,0x04FF,0xFFFF,0x8C51,0x2104,0x07E0,0xF800 },
  { "Light",  0xEF5D,0xFFFF,0xDEFB,0xFD00,0x001F,0x0000,0x6B4D,0xC618,0x03E0,0xD000 },
  { "Pastel", 0xFF1F,0xFFFF,0xFF9F,0xFB16,0x5D9F,0x39C7,0x9C92,0xFEB7,0x2FE7,0xE8B4 },
};
#define THEME_COUNT 4
int themeIdx = 0;
Theme& T() { return themes[themeIdx]; }

void saveTheme(){ Preferences p; p.begin("ui",false); p.putInt("theme",themeIdx); p.end(); }
void loadTheme(){
  Preferences p; p.begin("ui",true);
  themeIdx = p.getInt("theme",0); p.end();
  if(themeIdx<0||themeIdx>=THEME_COUNT) themeIdx=0;
}

enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };

// =============================================
// LGFX CONFIG
// =============================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;
public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host=SPI2_HOST; cfg.spi_mode=0;
      cfg.freq_write=40000000; cfg.freq_read=16000000;
      cfg.spi_3wire=false; cfg.use_lock=true;
      cfg.dma_channel=SPI_DMA_CH_AUTO;
      cfg.pin_sclk=12; cfg.pin_mosi=11;
      cfg.pin_miso=13; cfg.pin_dc=2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs=10; cfg.pin_rst=14; cfg.pin_busy=-1;
      cfg.memory_width=240; cfg.memory_height=320;
      cfg.panel_width=240;  cfg.panel_height=320;
      cfg.offset_x=0; cfg.offset_y=0; cfg.offset_rotation=0;
      cfg.dummy_read_pixel=8; cfg.dummy_read_bits=1;
      cfg.readable=true; cfg.invert=false;
      cfg.rgb_order=false; cfg.dlen_16bit=false;
      cfg.bus_shared=false;
      _panel_instance.config(cfg); }
    { auto cfg = _light_instance.config();
      cfg.pin_bl=21; cfg.invert=false;
      cfg.freq=44100; cfg.pwm_channel=7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance); }
    { auto cfg = _touch_instance.config();
      cfg.pin_int=-1; cfg.bus_shared=false;
      cfg.offset_rotation=0; cfg.spi_host=SPI3_HOST;
      cfg.freq=2000000;
      cfg.pin_sclk=6; cfg.pin_mosi=5;
      cfg.pin_miso=4; cfg.pin_cs=9;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance); }
    setPanel(&_panel_instance);
  }
};
LGFX display;
LGFX_Sprite canvas(&display);      // frame buffer utama (full screen)
LGFX_Sprite canvasApp(&display);   // buffer khusus app Canvas (drawing)
LGFX_Sprite transShot(&display);   // snapshot layar lama, dipakai animasi transisi antar-layar

// =============================================
// ORIENTASI
// =============================================
Orientation currentOrient = ORIENT_LANDSCAPE;
int SCR_W = 320, SCR_H = 240;
#define STATUS_H 22   // tinggi status bar

// Didefinisikan penuh di bagian SENSOR (dekat setup()), di-extern di sini
// supaya bisa dipakai oleh kode yang letaknya lebih awal di file
// (drawStatusBar, ccActOrient, app Orientasi 3D, dll).
// (Fungsi seperti showToast()/saveAutoRotatePref() tidak perlu di-extern -
// arduino-cli otomatis membuat prototype utk semua fungsi di puncak file.)
extern bool  autoRotateEnabled;
extern bool  shakeEnabled;
extern bool  mpuReady;
extern float mpuAx, mpuAy, mpuAz;
extern float mpuGx, mpuGy, mpuGz;
extern float mpuTempC;
extern float smoothRoll, smoothPitch;
extern float battVoltage;
extern int   battPercent;

void saveOrientPref() {
  Preferences p; p.begin("ui", false);
  p.putInt("orient", (int)currentOrient);
  p.end();
}
Orientation loadOrientPref() {
  Preferences p; p.begin("ui", true);
  // Default orientasi = LANDSCAPE lagi (dibalikin sesuai permintaan user -
  // ini orientasi yang sudah terbukti touch-nya jalan lancar). User tetap
  // bisa ganti ke portrait kapan saja lewat toggle Orientasi di Control
  // Center; pilihan itu tersimpan permanen (lihat ccActOrient->saveOrientPref).
  int v = p.getInt("orient", (int)ORIENT_LANDSCAPE);
  p.end();
  return (v == (int)ORIENT_PORTRAIT) ? ORIENT_PORTRAIT : ORIENT_LANDSCAPE;
}

void loadCanvas();
void saveCanvas();
int  canvasAppHeight();
void needRedrawNow();

void applyOrientation(Orientation o, bool doSave) {
  currentOrient = o;
  display.setRotation(o == ORIENT_LANDSCAPE ? 1 : 0);
  SCR_W = display.width();
  SCR_H = display.height();
  canvas.deleteSprite();
  canvas.setPsram(true);
  canvas.createSprite(SCR_W, SCR_H);
  transShot.deleteSprite();
  transShot.setPsram(true);
  transShot.createSprite(SCR_W, SCR_H);
  canvasApp.deleteSprite();
  canvasApp.setPsram(true);
  canvasApp.createSprite(SCR_W, canvasAppHeight());
  canvasApp.fillSprite(T().bg);
  loadCanvas();
  if (doSave) saveOrientPref();
  needRedrawNow();
}

// =============================================
// SD CARD & GEMINI API KEY STORAGE
// =============================================
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40
bool sdReady=false;
const char* NOTE_FILE="/notepad.txt";
const char* GEMINI_KEY_FILE="/gemini_key.txt";

String geminiApiKey = "";

void initSD(){
  SD_MMC.setPins(SD_PIN_CLK,SD_PIN_CMD,SD_PIN_D0);
  sdReady=SD_MMC.begin("/sdcard",true);
}
String noteText="";
void loadNote(){
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_READ);
  if(f){noteText=f.readString();f.close();}
}
void saveNote(){
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_WRITE);
  if(f){f.print(noteText);f.close();}
}

String sanitizeKeyLine(String line) {
  if (line.length() >= 3 &&
      (uint8_t)line[0]==0xEF && (uint8_t)line[1]==0xBB && (uint8_t)line[2]==0xBF) {
    line = line.substring(3);
  }
  line.trim();
  if (line.length()>=2 &&
      ((line.startsWith("\"") && line.endsWith("\"")) ||
       (line.startsWith("'")  && line.endsWith("'")))) {
    line = line.substring(1, line.length()-1);
    line.trim();
  }
  return line;
}

void loadGeminiKey(){
  geminiApiKey = "";
  if(!sdReady) return;

  if(!SD_MMC.exists(GEMINI_KEY_FILE)){
    File f = SD_MMC.open(GEMINI_KEY_FILE, FILE_WRITE);
    if(f){
      f.print("# =============================================\r\n");
      f.print("# GEMINI API KEY CONFIGURATION (ESP32-S3)\r\n");
      f.print("# Dapatkan API key gratis dari Google AI Studio:\r\n");
      f.print("# https://aistudio.google.com/app/apikey\r\n");
      f.print("#\r\n");
      f.print("# Tempel API Key kamu di baris tanpa tanda '#':\r\n");
      f.print("# =============================================\r\n");
      f.print("YOUR_GEMINI_API_KEY_HERE\r\n");
      f.close();
    }
  }

  File f = SD_MMC.open(GEMINI_KEY_FILE, FILE_READ);
  if(!f){
    Serial.println("[Gemini] Gagal membuka file key!");
    return;
  }

  String raw;
  raw.reserve(f.size()+1);
  while(f.available()) raw += (char)f.read();
  f.close();

  raw.replace("\r\n", "\n");
  raw.replace("\r", "\n");

  int start = 0;
  while (start < (int)raw.length()) {
    int nl = raw.indexOf('\n', start);
    String line = (nl == -1) ? raw.substring(start) : raw.substring(start, nl);
    line = sanitizeKeyLine(line);
    if (line.length() > 0 && line.charAt(0) != '#') {
      geminiApiKey = line;
      break;
    }
    if (nl == -1) break;
    start = nl + 1;
  }

  Serial.printf("[Gemini] Key loaded, length=%d\n", geminiApiKey.length());
  if (geminiApiKey.length() > 8) {
    Serial.printf("[Gemini] Preview: %s...%s\n",
                  geminiApiKey.substring(0,4).c_str(),
                  geminiApiKey.substring(geminiApiKey.length()-4).c_str());
  }
}

void saveGeminiKey(){
  if(!sdReady) return;
  geminiApiKey = sanitizeKeyLine(geminiApiKey);
  File f = SD_MMC.open(GEMINI_KEY_FILE, FILE_WRITE);
  if(f){
    f.print("# =============================================\r\n");
    f.print("# GEMINI API KEY CONFIGURATION (ESP32-S3)\r\n");
    f.print("# Dapatkan API key gratis dari Google AI Studio:\r\n");
    f.print("# https://aistudio.google.com/app/apikey\r\n");
    f.print("# =============================================\r\n");
    f.print(geminiApiKey);
    f.print("\r\n");
    f.close();
  }
}

int canvasAppHeight(){
  return SCR_H - STATUS_H - 44;
}
const char* canvasFileFor(Orientation o){
  return o==ORIENT_LANDSCAPE ? "/canvas_land.bin" : "/canvas_port.bin";
}
void loadCanvas(){
  if(!sdReady) return;
  uint32_t need = (uint32_t)SCR_W * canvasAppHeight() * 2;
  File f=SD_MMC.open(canvasFileFor(currentOrient),FILE_READ);
  if(f && f.size()==need) f.read((uint8_t*)canvasApp.getBuffer(), need);
  if(f) f.close();
}
void saveCanvas(){
  if(!sdReady) return;
  uint32_t need = (uint32_t)SCR_W * canvasAppHeight() * 2;
  File f=SD_MMC.open(canvasFileFor(currentOrient),FILE_WRITE);
  if(f){ f.write((uint8_t*)canvasApp.getBuffer(), need); f.close(); }
}

// =============================================
// WEB SERVER (FILE MANAGER & INLINE TEXT EDITOR)
// =============================================
WebServer webServer(80);
bool webServerRunning = false;
File uploadFile;

void handleWebRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 File Manager</title>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#fff;padding:15px;max-width:650px;margin:auto;}";
  html += ".card{background:#1e1e1e;padding:15px;border-radius:10px;margin-bottom:15px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}";
  html += "a{color:#00e5ff;text-decoration:none;margin-right:5px;} .btn{background:#e91e63;color:#fff;padding:8px 14px;border:none;border-radius:6px;cursor:pointer;font-weight:bold;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:10px;} td,th{padding:10px;text-align:left;border-bottom:1px solid #333;}</style>";
  html += "</head><body><h2>ESP32 Web File Manager</h2>";
  
  html += "<div class='card'><h3>Upload File ke ESP32 SD Card</h3>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='upload' required style='margin-bottom:10px;'><br>";
  html += "<input type='submit' class='btn' value='Upload File'>";
  html += "</form></div>";

  html += "<div class='card'><h3>Daftar File SD Card</h3><table><tr><th>Nama File</th><th>Ukuran</th><th>Aksi</th></tr>";
  if (sdReady) {
    File root = SD_MMC.open("/");
    File f = root.openNextFile();
    int count = 0;
    while (f) {
      if (!f.isDirectory()) {
        count++;
        String fname = String(f.name());
        if (!fname.startsWith("/")) fname = "/" + fname;
        html += "<tr><td>" + fname + "</td><td>" + String(f.size()) + " B</td><td>";
        
        if (fname.endsWith(".txt") || fname.endsWith(".bin") == false) {
          html += "<a href='/edit?file=" + fname + "' style='color:#ffca28;'>Edit</a> | ";
        }
        
        html += "<a href='/download?file=" + fname + "'>Download</a> | ";
        html += "<a href='/delete?file=" + fname + "' style='color:#ff5252' onclick=\"return confirm('Hapus file " + fname + "?')\">Hapus</a></td></tr>";
      }
      f = root.openNextFile();
    }
    if(count==0) html += "<tr><td colspan='3'>SD Card Kosong</td></tr>";
  } else {
    html += "<tr><td colspan='3'>SD Card Tidak Terdeteksi</td></tr>";
  }
  html += "</table></div></body></html>";

  webServer.send(200, "text/html", html);
}

void handleWebEditGet() {
  if (!webServer.hasArg("file")) {
    webServer.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String fname = webServer.arg("file");
  if (!fname.startsWith("/")) fname = "/" + fname;
  String content = "";
  if (sdReady && SD_MMC.exists(fname)) {
    File f = SD_MMC.open(fname, FILE_READ);
    if (f) { content = f.readString(); f.close(); }
  }

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Edit " + fname + "</title>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#fff;padding:15px;max-width:600px;margin:auto;}";
  html += "textarea{width:100%;height:260px;background:#1e1e1e;color:#00e5ff;border:1px solid #444;border-radius:8px;padding:12px;font-size:14px;box-sizing:border-box;}";
  html += ".btn{background:#4caf50;color:#fff;padding:10px 18px;border:none;border-radius:6px;cursor:pointer;font-weight:bold;font-size:14px;margin-top:10px;}";
  html += ".btn-back{background:#555;color:#fff;padding:10px 18px;border-radius:6px;text-decoration:none;display:inline-block;margin-right:10px;font-size:14px;}</style>";
  html += "</head><body><h2>Edit File: " + fname + "</h2>";
  html += "<form method='POST' action='/save'>";
  html += "<input type='hidden' name='file' value='" + fname + "'>";
  html += "<textarea name='content'>" + content + "</textarea><br>";
  html += "<a href='/' class='btn-back'>&lt; Batal</a>";
  html += "<input type='submit' class='btn' value='Simpan File'>";
  html += "</form></body></html>";

  webServer.send(200, "text/html", html);
}

void handleWebEditPost() {
  if (webServer.hasArg("file") && webServer.hasArg("content")) {
    String fname = webServer.arg("file");
    String content = webServer.arg("content");
    if (!fname.startsWith("/")) fname = "/" + fname;
    if (sdReady) {
      File f = SD_MMC.open(fname, FILE_WRITE);
      if (f) {
        f.print(content);
        f.close();
        if (fname == NOTE_FILE) loadNote();
        if (fname == GEMINI_KEY_FILE) loadGeminiKey();
        needRedrawNow();
      }
    }
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleUploadStream() {
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    uploadFile = SD_MMC.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    needRedrawNow();
  }
}

void handleWebDelete() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (SD_MMC.exists(path)) {
      SD_MMC.remove(path);
      needRedrawNow();
    }
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleWebDownload() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (SD_MMC.exists(path)) {
      File f = SD_MMC.open(path, FILE_READ);
      webServer.streamFile(f, "application/octet-stream");
      f.close();
      return;
    }
  }
  webServer.send(404, "text/plain", "File Not Found");
}

void startWebServer(){
  if(webServerRunning) return;
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/edit", HTTP_GET, handleWebEditGet);
  webServer.on("/save", HTTP_POST, handleWebEditPost);
  webServer.on("/upload", HTTP_POST, [](){
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  }, handleUploadStream);
  webServer.on("/delete", HTTP_GET, handleWebDelete);
  webServer.on("/download", HTTP_GET, handleWebDownload);
  webServer.begin();
  webServerRunning = true;
}

// =============================================
// WIFI & NTP & AUTO SCAN
// =============================================
char WIFI_SSID[64]="", WIFI_PASSWORD[64]="";
const char* NTP_SERVER="pool.ntp.org";
const long  GMT_OFFSET=7*3600;
const int   DST_OFFSET=0;
bool wifiConnected=false, ntpSynced=false;
bool airplaneMode=false;

bool wifiScanning = false;
struct ScannedWifi {
  String ssid;
  int rssi;
  bool isEncrypted;
};
#define MAX_SCANNED_WIFI 8
ScannedWifi scannedWifis[MAX_SCANNED_WIFI];
int scannedWifiNum = 0;

void startWifiScan() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  wifiScanning = true;
  scannedWifiNum = 0;
  needRedrawNow();
}

void checkWifiScanComplete() {
  if (!wifiScanning) return;
  int n = WiFi.scanComplete();
  if (n >= 0) {
    wifiScanning = false;
    scannedWifiNum = min(n, MAX_SCANNED_WIFI);
    for (int i = 0; i < scannedWifiNum; i++) {
      scannedWifis[i].ssid = WiFi.SSID(i);
      scannedWifis[i].rssi = WiFi.RSSI(i);
      scannedWifis[i].isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    needRedrawNow();
  }
}

void loadWifiCreds(){
  Preferences p; p.begin("wifi",true);
  p.getString("ssid","").toCharArray(WIFI_SSID,64);
  p.getString("pass","").toCharArray(WIFI_PASSWORD,64);
  p.end();
}
void saveWifiCreds(){
  Preferences p; p.begin("wifi",false);
  p.putString("ssid",WIFI_SSID);
  p.putString("pass",WIFI_PASSWORD);
  p.end();
}
void connectWifi(){
  if(airplaneMode || !strlen(WIFI_SSID)) return;
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  WiFi.setSleep(false);
  int t=0;
  while(WiFi.status()!=WL_CONNECTED&&t<20){delay(300);t++;}
  wifiConnected=(WiFi.status()==WL_CONNECTED);
  if(wifiConnected){
    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);
    struct tm tm; if(getLocalTime(&tm,5000)) ntpSynced=true;
    startWebServer();
  }
}
void disconnectWifi(){
  WiFi.disconnect(true);
  wifiConnected=false;
}
void toggleAirplaneMode(){
  airplaneMode=!airplaneMode;
  if(airplaneMode) disconnectWifi();
  else connectWifi();
}

// =============================================
// GEMINI AI HTTP CLIENT & FREERTOS TASK
// =============================================
String aiPrompt = "";
String aiResponse = "";
volatile bool aiLoading = false;
float aiRespScrollY = 0;   // posisi scroll layar AI Chat (dipakai jg oleh triggerGeminiAI utk reset)
int   aiRespMaxScroll = 0;
float aiRespScrollVel = 0; // v12: kecepatan scroll (low-pass) -> momentum halus stlh jari dilepas

// =================================================================
// AI MEMORY: riwayat percakapan disimpan permanen di SD Card supaya
// Gemini "ingat" obrolan sebelumnya walau HP di-restart. Setiap tanya-
// jawab yg BERHASIL ditambahkan ke file ini, lalu isinya disuntikkan
// otomatis (via prompt hardcode) sebelum pertanyaan baru user dikirim.
// =================================================================
#define AI_MEMORY_PATH "/ai_memory.txt"
#define AI_MEMORY_MAX_CHARS   2000  // batas panjang riwayat yg disertakan per request (jaga RAM & kuota)
#define AI_MEMORY_MAX_ENTRIES 12    // batas jumlah tanya-jawab yg disimpan di file (sisanya dibuang otomatis)

const char* AI_MEMORY_INSTRUCTION =
  "Kamu adalah asisten AI pribadi yang berjalan di sebuah HP DIY buatan sendiri (ESP32). "
  "Di bawah ini ada MEMORY / riwayat percakapan sebelumnya dengan user. Baca dan "
  "pertimbangkan riwayat ini untuk menjawab pertanyaan baru user, supaya kamu tetap "
  "ingat konteks (misalnya nama user, preferensi, atau topik yang pernah dibahas). "
  "Kalau riwayatnya tidak relevan dengan pertanyaan baru, abaikan saja dan jawab "
  "pertanyaan baru itu seperti biasa. Jangan menyebutkan kata 'memory' atau 'riwayat' "
  "secara eksplisit di jawabanmu kecuali user memang bertanya soal itu.\n\n";

String loadAiMemory(){
  if(!sdReady) return "";
  File f = SD_MMC.open(AI_MEMORY_PATH, FILE_READ);
  if(!f || f.isDirectory()){ if(f) f.close(); return ""; }
  String content = f.readString();
  f.close();
  if((int)content.length() > AI_MEMORY_MAX_CHARS){
    content = content.substring(content.length()-AI_MEMORY_MAX_CHARS);
    int firstBreak = content.indexOf("\n---\n");   // buang potongan entri yg kepotong di awal
    if(firstBreak >= 0) content = content.substring(firstBreak+5);
  }
  return content;
}

void trimAiMemoryIfNeeded(){
  if(!sdReady) return;
  File f = SD_MMC.open(AI_MEMORY_PATH, FILE_READ);
  if(!f) return;
  String content = f.readString();
  f.close();
  const String sep = "\n---\n";
  int entryStarts[64]; int n=0;
  entryStarts[n++] = 0;
  int idx=0;
  while(n < 64){
    int p = content.indexOf(sep, idx);
    if(p<0) break;
    idx = p + sep.length();
    entryStarts[n++] = idx;
  }
  if(n <= AI_MEMORY_MAX_ENTRIES) return; // belum perlu dipangkas
  int keepFrom = entryStarts[n - AI_MEMORY_MAX_ENTRIES];
  String trimmed = content.substring(keepFrom);
  File wf = SD_MMC.open(AI_MEMORY_PATH, FILE_WRITE); // FILE_WRITE menimpa dari awal
  if(wf){ wf.print(trimmed); wf.close(); }
}

void appendAiMemory(const String& q, const String& a){
  if(!sdReady) return;
  // Pakai pola baca-lalu-tulis-ulang (bukan FILE_APPEND) supaya konsisten
  // dgn cara file lain di project ini dibaca/ditulis (mis. Notepad) dan
  // gak bergantung pada mode file yg belum pernah dites di kode ini.
  String existing = "";
  File rf = SD_MMC.open(AI_MEMORY_PATH, FILE_READ);
  if(rf){ existing = rf.readString(); rf.close(); }
  existing += "User: " + q + "\n";
  existing += "AI: "   + a + "\n---\n";
  File wf = SD_MMC.open(AI_MEMORY_PATH, FILE_WRITE);
  if(wf){ wf.print(existing); wf.close(); }
  trimAiMemoryIfNeeded();
}

void clearAiMemory(){
  if(!sdReady) return;
  SD_MMC.remove(AI_MEMORY_PATH);
}

TaskHandle_t geminiTaskHandle = NULL;
WiFiClientSecure* aiClientPtr = nullptr;
volatile unsigned long aiRequestStartMillis = 0;
const unsigned long AI_SOFT_TIMEOUT_MS = 15000; // 15s -> paksa putus socket
const unsigned long AI_HARD_TIMEOUT_MS = 30000; // 30s -> paksa hapus task
volatile bool aiSoftStopTriggered = false;

bool doGeminiHttpRequest(const String& promptText, String& outResponse) {
  bool ok = false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  aiClientPtr = &client;

  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(15000);

  // FIX v7: model diganti dari gemini-2.5-flash-lite -> gemini-3.5-flash-lite
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent?key=" + geminiApiKey;

  Serial.printf("[Gemini] Free DRAM internal: %u bytes (blok terbesar: %u)\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  if (!http.begin(client, url)) {
    outResponse = "Gagal inisialisasi HTTPClient (begin() gagal).";
    aiClientPtr = nullptr;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  String escapedPrompt = promptText;
  escapedPrompt.replace("\\", "\\\\");
  escapedPrompt.replace("\"", "\\\"");
  escapedPrompt.replace("\r", "");
  escapedPrompt.replace("\n", "\\n");

  String jsonPayload = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapedPrompt + "\"}]}]}";

  int httpCode = http.POST(jsonPayload);
  Serial.printf("[Gemini] httpCode=%d err=%s\n", httpCode, http.errorToString(httpCode).c_str());

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String respStr = http.getString();
      int textIdx = respStr.indexOf("\"text\":");
      if (textIdx >= 0) {
        int start = respStr.indexOf("\"", textIdx + 7);
        if (start >= 0) {
          start += 1;
          int end = start;
          while (end < (int)respStr.length()) {
            end = respStr.indexOf("\"", end);
            if (end < 0) break;
            if (respStr.charAt(end - 1) != '\\') break;
            end++;
          }
          if (end > start) {
            String rawText = respStr.substring(start, end);
            rawText.replace("\\n", "\n");
            rawText.replace("\\\"", "\"");
            rawText.replace("\\\\", "\\");
            outResponse = rawText;
          } else {
            outResponse = respStr;
          }
        } else {
          outResponse = respStr;
        }
      } else {
        outResponse = "Response: " + respStr.substring(0, 150);
      }
      ok = true;
    } else {
      String errBody = http.getString();
      outResponse = "HTTP Error " + String(httpCode) + ": " + errBody.substring(0, 150);
    }
  } else if (httpCode == -1) {
    outResponse = "HTTP Error -1: TLS/koneksi gagal (biasanya RAM/DNS sesaat). Dicoba ulang otomatis...";
  } else {
    outResponse = "HTTP Connection Error: " + http.errorToString(httpCode) + " (" + String(httpCode) + ")";
  }

  http.end();
  aiClientPtr = nullptr;
  return ok;
}

void sendGeminiRequest(String promptText) {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    aiResponse = "Error: WiFi belum terhubung!";
    aiLoading = false;
    aiRequestStartMillis = 0;
    needRedrawNow();
    return;
  }

  geminiApiKey.trim();
  if (geminiApiKey.length() == 0 || geminiApiKey == "YOUR_GEMINI_API_KEY_HERE") {
    aiResponse = "Error: Isi API Key di /gemini_key.txt pada SD Card!";
    aiLoading = false;
    aiRequestStartMillis = 0;
    needRedrawNow();
    return;
  }

  IPAddress testIp;
  if (!WiFi.hostByName("generativelanguage.googleapis.com", testIp)) {
    aiResponse = "Error: DNS gagal resolve googleapis.com. Cek jaringan WiFi/DNS.";
    aiLoading = false;
    aiRequestStartMillis = 0;
    needRedrawNow();
    return;
  }

  String resp;
  bool ok = false;
  const int MAX_ATTEMPTS = 3;

  // Suntikkan prompt hardcode + memory riwayat SEBELUM pertanyaan asli user.
  // promptText asli TETAP dipakai apa adanya utk disimpan ke memory & tampil
  // di UI -- yg diubah cuma teks yg dikirim ke API (fullPrompt).
  String memory = loadAiMemory();
  String fullPrompt;
  if(memory.length() > 0){
    fullPrompt = String(AI_MEMORY_INSTRUCTION) +
                 "=== RIWAYAT PERCAKAPAN SEBELUMNYA ===\n" + memory +
                 "=== PERTANYAAN BARU ===\n" + promptText;
  } else {
    fullPrompt = promptText;
  }

  for (int attempt = 1; attempt <= MAX_ATTEMPTS && !aiSoftStopTriggered; attempt++) {
    if (attempt > 1) {
      Serial.printf("[Gemini] Percobaan %d gagal, retry ke-%d...\n", attempt - 1, attempt);
      delay(400 * attempt);
      if (WiFi.status() != WL_CONNECTED) break;
    }
    ok = doGeminiHttpRequest(fullPrompt, resp);
    if (ok) break;
  }

  aiResponse = resp;
  if(ok) appendAiMemory(promptText, resp); // cuma simpan tanya-jawab yg BERHASIL
  aiLoading = false;
  aiRequestStartMillis = 0;
  aiSoftStopTriggered = false;
  // v14: Dynamic Island kabari hasilnya (mekar sebentar lalu hilang total,
  // krn activity-nya sudah selesai -> keepAliveCompact=false)
  diNotify('A', ok?"Jawaban siap!":"Gagal merespons", ok?T().good:T().danger, 1700, false);
  diLink(SCR_AICHAT);
  needRedrawNow();
}

void geminiTaskFunc(void* parameter) {
  sendGeminiRequest(aiPrompt);
  geminiTaskHandle = NULL;
  vTaskDelete(NULL);
}

void triggerGeminiAI() {
  if (aiPrompt.length() == 0 || aiLoading) return;

  size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t largestBlock  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  Serial.printf("[Gemini] Sebelum bikin task -> freeInternal=%u largestBlock=%u freePsram=%u\n",
                freeInternal, largestBlock, ESP.getFreePsram());

  if (largestBlock < 46000) {
    aiResponse = "Error: RAM internal terlalu terfragmentasi/rendah utk TLS. Tutup app lain / restart perangkat.";
    needRedrawNow();
    return;
  }

  aiLoading = true;
  aiSoftStopTriggered = false;
  aiResponse = "Menghubungi Gemini AI...";
  aiRequestStartMillis = millis();
  aiRespScrollY = 0; // pertanyaan baru -> mulai baca dari atas lagi
  // v14: Dynamic Island nunjukin activity latar -> mekar sebentar lalu
  // mengecil jadi pil compact yg berdenyut selama masih loading
  diNotify('A', "Sedang berpikir...", T().accent2, 1400, true);
  diLink(SCR_AICHAT);
  needRedrawNow();

  BaseType_t res = xTaskCreatePinnedToCore(
      geminiTaskFunc, "geminiTask", 16384, NULL, 1, &geminiTaskHandle, 1);

  if (res != pdPASS) {
    Serial.println("[Gemini] xTaskCreatePinnedToCore GAGAL!");
    aiLoading = false;
    aiRequestStartMillis = 0;
    geminiTaskHandle = NULL;
    aiResponse = "Error: Gagal membuat proses AI. Coba restart perangkat.";
    needRedrawNow();
  }
}

void checkAiWatchdog() {
  if (!aiLoading || aiRequestStartMillis == 0) return;

  unsigned long elapsed = millis() - aiRequestStartMillis;

  if (!aiSoftStopTriggered && elapsed > AI_SOFT_TIMEOUT_MS) {
    Serial.println("[Gemini][Watchdog] Soft-timeout tercapai, memutus koneksi paksa...");
    aiSoftStopTriggered = true;
    if (aiClientPtr) {
      aiClientPtr->stop();
    }
  }

  if (elapsed > AI_HARD_TIMEOUT_MS) {
    Serial.println("[Gemini][Watchdog] Hard-timeout tercapai, memaksa hapus task!");
    if (geminiTaskHandle != NULL) {
      vTaskDelete(geminiTaskHandle);
      geminiTaskHandle = NULL;
    }
    aiClientPtr = nullptr;
    aiLoading = false;
    aiRequestStartMillis = 0;
    aiSoftStopTriggered = false;
    aiResponse = "Error: Request timeout total. Coba lagi atau restart perangkat.";
    needRedrawNow();
  }
}

// =============================================
// TOUCH CALIBRATION
// =============================================
void loadOrRunCalibration(){
  Preferences p; p.begin("touch_cal",false);
  bool done=p.getBool("done",false);
  if(done){
    uint16_t d[8]; p.getBytes("data",d,sizeof(d));
    display.setTouchCalibrate(d);
  } else {
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE); display.setTextSize(2);
    display.setCursor(20,100); display.println("Kalibrasi Touch");
    display.setTextSize(1);
    display.setCursor(20,130); display.println("Sentuh tanda di setiap sudut");
    uint16_t d[8];
    display.calibrateTouch(d,TFT_WHITE,TFT_BLACK,15);
    p.putBytes("data",d,sizeof(d));
    p.putBool("done",true);
    display.fillScreen(TFT_BLACK);
    delay(200);
  }
  p.end();
}

// =============================================
// STATE GLOBAL / LIFECYCLE
// =============================================
int brightness=200;
bool needRedraw=true;
void needRedrawNow(){ needRedraw=true; }
bool locked = true;
bool controlCenterOpen = false;
bool dndMode = false;
bool kbVisible=false;
String* kbTarget=nullptr;
KbMode kbMode=KB_LOWER;

// -------- Navigation stack --------
#define NAV_MAX 8
Screen navStack[NAV_MAX];
int navDepth=0;
Screen curScreen(){ return navDepth>0 ? navStack[navDepth-1] : SCR_HOME; }

void appOnEnter(Screen s);
void appOnExit(Screen s);

// -------- Notepad: cek perubahan blm tersimpan (dipakai navigasi) --------
bool notepadNeedsConfirm();          // true = ada perubahan blm disimpan, tahan navigasi
void notepadRequestConfirm(int act); // act: 1=Back, 2=Home -> tampilkan dialog Simpan/Buang

// =============================================
// TRANSISI ANTAR LAYAR (slide halus dgn easing, dipakai navPush/Back/Home)
// Push  = layar baru masuk dari KANAN (kesan "buka halaman baru")
// Back  = layar sblmnya masuk dari KIRI (kebalikan dari Push)
// Home  = geser TURUN (kesan "menutup/pulang", searah swipe unlock)
// =============================================
void renderCurrentFrame(); // forward decl - dipakai utk render frame BARU sblm animasi

// ease-in-out cubic: mulai halus, cepat di tengah, berhenti halus - lbh
// "premium" drpd ease-out biasa krn simetris di awal & akhir gerakan.
// FIX v11: fungsi easing ini sekarang dipakai ULANG oleh animasi buka/tutup
// Control Center (lihat CONTROL CENTER) & animasi Game Booster, jadi semua
// animasi di HP ini punya "rasa" gerakan yg konsisten satu sama lain.
float navEase(float p){
  return p<0.5f ? 4.0f*p*p*p : 1.0f-powf(-2.0f*p+2.0f,3.0f)/2.0f;
}

// v13: dulu transisi ini jalan dgn JUMLAH FRAME TETAP (STEPS=12) + delay(6)
// per frame -- artinya durasi TOTAL animasi ikut molor kalau proses push
// sprite lg lambat (mis. layar penuh/kompleks, atau SPI lg dipakai bareng
// WiFi/AI), krn tiap frame maunya nunggu render+delay dulu baru lanjut.
// Sekarang (senada dgn fix ccOffset di v11) dibuat BERBASIS WAKTU (millis):
// selama total waktu blm mencapai NAV_TRANS_MS, terus gambar frame baru
// dgn progres = waktu-berjalan/NAV_TRANS_MS. Kalau hardware cepat -> lebih
// banyak frame yg sempat digambar (makin mulus). Kalau hardware lg sibuk
// -> frame yg digambar lebih sedikit, TAPI durasi totalnya tetap sama
// persis -> transisi TIDAK PERNAH kerasa "molor"/lambat, cuma kerasa
// sedikit kurang mulus di kondisi terburuk. Ini yg dimaksud "smooth" yg
// konsisten di kondisi apapun, bukan cuma smooth pas kondisi ideal.
const float NAV_TRANS_MS = 170.0f;
void playNavTransition(NavAnim anim){
  canvas.pushSprite(&transShot,0,0); // simpan frame LAMA (canvas msh berisi layar sblm pindah)
  renderCurrentFrame();              // gambar frame BARU ke canvas (state navigasi sudah berubah)

  unsigned long startMs = millis();
  float t;
  // v17 FIX "transisi patah-patah": AKAR MASALAH -- tiap iterasi loop di
  // bawah ini memanggil DUA pushSprite() terpisah ke `display` (transShot
  // lalu canvas). Tanpa startWrite()/endWrite() eksplisit, LovyanGFX
  // MEMBUKA & MENUTUP TRANSAKSI SPI SENDIRI-SENDIRI utk tiap pushSprite()
  // (assert chip-select, kirim command jendela alamat, lock bus, lalu
  // lepas semuanya lagi) -- overhead buka-tutup ini terjadi 2x per frame,
  // di atas biaya transfer piksel yang sebenarnya. Karena durasi transisi
  // sengaja dibuat pendek (170ms, lihat NAV_TRANS_MS), overhead ini jadi
  // porsi yang cukup besar dari waktu tiap frame, sehingga jumlah frame
  // yang sempat digambar jadi sedikit & timing-nya gak merata -> terasa
  // "patah-patah"/tersendat, apalagi kalau SPI lagi dipakai bareng proses
  // lain (mis. AI Chat/WiFi jalan di background).
  // FIX: bungkus SELURUH loop animasi dgn SATU pasang startWrite()/
  // endWrite(). Transaksi SPI dibuka SEKALI di awal & tetap terbuka
  // sepanjang animasi berjalan -- tiap pushSprite() di dalam loop tinggal
  // menumpang transaksi yang sudah terbuka itu (tanpa lock/assert-CS/
  // kirim-command jendela alamat berulang), jadi jauh lebih ringan per
  // frame & gerakannya terasa jauh lebih mulus / tidak tersendat lagi.
  display.startWrite();
  do {
    t = (millis()-startMs)/NAV_TRANS_MS;
    if(t>1.0f) t=1.0f;
    float p = navEase(t);
    if(anim==NAV_ANIM_PUSH){
      int offset=(int)(SCR_W*p);
      transShot.pushSprite(-offset,0);
      canvas.pushSprite(SCR_W-offset,0);
    } else if(anim==NAV_ANIM_BACK){
      int offset=(int)(SCR_W*p);
      transShot.pushSprite(offset,0);
      canvas.pushSprite(-SCR_W+offset,0);
    } else { // NAV_ANIM_HOME
      int offset=(int)(SCR_H*p);
      transShot.pushSprite(0,offset);
      canvas.pushSprite(0,-SCR_H+offset);
    }
  } while(t<1.0f);
  display.endWrite();
  push(); // pastikan frame akhir persis sama dgn 'canvas' (jaga2 sisa pembulatan offset)
}

void navPush(Screen s){
  appOnExit(curScreen());
  if(navDepth<NAV_MAX) navStack[navDepth++]=s;
  appOnEnter(s);
  kbVisible=false; kbTarget=nullptr;
  playNavTransition(NAV_ANIM_PUSH);
  needRedraw=false; // frame akhir sudah ke-push oleh playNavTransition
}
void navGoHome(){
  if(notepadNeedsConfirm()){ notepadRequestConfirm(2); return; }
  appOnExit(curScreen());
  navDepth=0;
  kbVisible=false; kbTarget=nullptr;
  playNavTransition(NAV_ANIM_HOME);
  needRedraw=false;
}
void navBack(){
  if(notepadNeedsConfirm()){ notepadRequestConfirm(1); return; }
  appOnExit(curScreen());
  if(navDepth>0) navDepth--;
  if(navDepth>0) appOnEnter(navStack[navDepth-1]);
  kbVisible=false; kbTarget=nullptr;
  playNavTransition(NAV_ANIM_BACK);
  needRedraw=false;
}

// =============================================
// VIRTUAL KEYBOARD
// =============================================
const char* kbL[3][10]={
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l",";"},
  {"z","x","c","v","b","n","m",",",".","?"}};
const char* kbU[3][10]={
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L",":"},
  {"Z","X","C","V","B","N","M","!","@","#"}};
const char* kbN[3][10]={
  {"1","2","3","4","5","6","7","8","9","0"},
  {"-","=","[","]","/","'","\"","<",">","\\"},
  {"~","!","@","#","$","%","^","&","*","("}};
const char* (*kbMaps[3])[10]={kbL,kbU,kbN};

int kbKeyW(){ return (SCR_W-8)/10 - 2; }
int kbKeyH(){ return 22; }
int kbY(){ return SCR_H - 4*(kbKeyH()+2) - 4; }
int kbOffCol(int row){
  int base = 4;
  if(row==1) base += (kbKeyW()+2)/2;
  if(row==2) base += (kbKeyW()+2);
  return base;
}

void drawKb(LGFX_Sprite& s){
  int y0=kbY();
  s.fillRect(0,y0-2,SCR_W,SCR_H-y0+2,T().surface);
  s.drawFastHLine(0,y0-2,SCR_W,T().divider);
  auto* lay=kbMaps[(int)kbMode];
  int kw=kbKeyW(), kh=kbKeyH();
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2), ox=kbOffCol(r);
    for(int c=0;c<10;c++){
      int kx=ox+c*(kw+2);
      s.fillRoundRect(kx,ry,kw,kh,3,T().surface2);
      s.setTextColor(T().text); s.setTextSize(1);
      s.setCursor(kx+kw/2-3,ry+kh/2-4);
      s.print(lay[r][c]);
    }
  }
  int cy=y0+3*(kh+2);
  int spaceW = SCR_W - 96 - 88 - 8;
  s.fillRoundRect(4,cy,40,kh,3,kbMode==KB_UPPER?T().accent:T().surface2);
  s.setTextColor(T().text);s.setTextSize(1);s.setCursor(10,cy+kh/2-4);s.print("SHF");
  s.fillRoundRect(48,cy,44,kh,3,kbMode==KB_NUM?T().accent:T().surface2);
  s.setCursor(54,cy+kh/2-4);s.print(kbMode==KB_NUM?"ABC":"123");
  s.fillRoundRect(96,cy,max(40,spaceW),kh,3,T().surface2);
  s.setCursor(96+max(40,spaceW)/2-24,cy+kh/2-4);s.print("SPACE");
  s.fillRoundRect(SCR_W-88,cy,84,kh,3,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-70,cy+kh/2-4);s.print("<--");
}

void kbTouch(int x,int y){
  if(!kbVisible||!kbTarget) return;
  int y0=kbY();
  if(y<y0-2) return;
  int kw=kbKeyW(), kh=kbKeyH();
  int cy=y0+3*(kh+2);
  int spaceW = max(40, SCR_W-96-88-8);
  if(y>=cy&&y<=cy+kh){
    if(x>=4&&x<=44)            kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER);
    else if(x>=48&&x<=92)      kbMode=(kbMode==KB_NUM?KB_LOWER:KB_NUM);
    else if(x>=96&&x<=96+spaceW) *kbTarget+=" ";
    else if(x>=SCR_W-88&&kbTarget->length()>0)
      *kbTarget=kbTarget->substring(0,kbTarget->length()-1);
    needRedraw=true;
    return;
  }
  auto* lay=kbMaps[(int)kbMode];
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2);
    if(y>=ry&&y<=ry+kh){
      int ox=kbOffCol(r);
      for(int c=0;c<10;c++){
        int kx=ox+c*(kw+2);
        if(x>=kx&&x<=kx+kw){
          *kbTarget+=lay[r][c];
          if(kbMode==KB_UPPER)kbMode=KB_LOWER;
          needRedraw=true;
          return;
        }
      }
    }
  }
}

// =============================================
// TOAST
// =============================================
String toastMsg="";
unsigned long toastUntil=0;
void showToast(const char* msg,int ms=1400){
  if(dndMode) return;
  toastMsg=msg; toastUntil=millis()+ms;
}
void drawToast(LGFX_Sprite& s){
  if(!toastMsg.length()||millis()>toastUntil) return;
  int tw=toastMsg.length()*6+16;
  int tx=SCR_W/2-tw/2;
  int ty=SCR_H-36;
  s.fillRoundRect(tx,ty,tw,22,6,T().accent2);
  s.setTextColor(T().bg);s.setTextSize(1);
  s.setCursor(tx+8,ty+7);s.print(toastMsg.c_str());
}

// =============================================
// STATUS BAR
// =============================================
void drawStatusBar(LGFX_Sprite& s){
  s.fillRect(0,0,SCR_W,STATUS_H,T().surface);
  s.drawFastHLine(0,STATUS_H,SCR_W,T().divider);
  struct tm t;
  bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(ok?T().text:T().subtext);s.setTextSize(1);
  if(ok){char b[6];sprintf(b,"%02d:%02d",t.tm_hour,t.tm_min);s.setCursor(8,7);s.print(b);}
  else{s.setCursor(8,7);s.print("--:--");}

  int rx = SCR_W-6; // kursor kanan, bergerak ke kiri tiap elemen ditambah

  // --- Baterai: ikon kotak terisi + teks persen (paling kanan) ---
  int bw=16,bh=9, bx=rx-bw, by=7;
  uint16_t bc = (battPercent<=15)?T().danger:(battPercent<=35)?T().accent:T().good;
  s.drawRect(bx,by,bw-2,bh,T().subtext);
  s.fillRect(bx+bw-2,by+2,2,bh-4,T().subtext); // kutub kecil baterai
  int innerW = bw-6;
  int fillW = constrain((innerW*battPercent)/100,0,innerW);
  if(fillW>0) s.fillRect(bx+2,by+2,fillW,bh-4,bc);
  rx = bx-3;
  char pctBuf[6]; sprintf(pctBuf,"%d%%",battPercent);
  int pctW = strlen(pctBuf)*6;
  rx -= pctW;
  s.setTextColor(bc); s.setCursor(rx,7); s.print(pctBuf);
  rx -= 6;

  // --- Wifi / pesawat / tanpa sinyal ---
  if(airplaneMode){
    s.setTextColor(T().subtext); rx-=6; s.setCursor(rx,7); s.print("A");
    rx -= 8;
  } else if(wifiConnected){
    rx -= 9;
    s.fillCircle(rx,16,2,T().good);
    s.drawArc(rx,18,5,4,210,330,T().good);
    s.drawArc(rx,18,9,8,210,330,T().good);
    rx -= 12;
  } else {
    s.setTextColor(T().danger); rx-=6; s.setCursor(rx,7); s.print("X");
    rx -= 8;
  }

  // --- SD ---
  if(sdReady){
    s.setTextColor(T().good);
    rx -= 12; s.setCursor(rx,7); s.print("SD");
    rx -= 6;
  }
  // --- DND ---
  if(dndMode){
    s.setTextColor(T().accent);
    rx -= 18; s.setCursor(rx,7); s.print("DND");
  }

  if(curScreen()==SCR_HOME && !controlCenterOpen){
    s.fillRoundRect(SCR_W/2-12,STATUS_H+2,24,3,2,T().divider);
  }
}

// =============================================
// DYNAMIC ISLAND
// Pil hitam mengambang di tengah-atas layar (meniru "Dynamic Island" HP
// modern), dipakai utk notifikasi live singkat: status AI Chat, Mode Game,
// & skor/feedback Trivia Quiz. Bermorfing antara 3 keadaan (IDLE tersembunyi
// total / COMPACT pil kecil+indikator / EXPANDED melebar ikon+teks) via
// animasi BERBASIS WAKTU (pola sama dgn Control Center & transisi navigasi
// v11/v13) supaya durasinya konsisten di kondisi hardware apapun -- TAPI
// pakai easing "overshoot" (dikit "mantul" di ujung gerakan) bukan navEase
// yg datar, biar morph-nya kerasa kenyal/hidup kayak Dynamic Island asli,
// bukan geser kaku antar 2 ukuran.
//
// v17 FIX (akar masalah "kata keluar dari pil"): dulu pil ini punya DUA
// baris teks (title + subtitle), tapi lebar pil (diTargetW) CUMA dihitung
// dari panjang title -- subtitle yang seringkali lebih panjang (mis. title
// "Mode Game" tapi subtitle "Performa dimaksimalkan") jadi meleber keluar
// pil krn lebar pil ketinggalan sempit dari teksnya sendiri. Bahkan utk
// SATU baris title pun, kalau title dibuat dari gabungan variabel (mis.
// nama kategori trivia) & jadi lebih panjang dari muat maksimum pil
// (SCR_W-28), diTargetW() meng-CLAMP lebar pil tapi teksnya TIDAK ikut
// dipotong -> tetap meleber.
// FIX: (a) subtitle dihapus total -- sekarang SATU baris teks singkat saja
// (persis spt diminta: "Mode Game" tanpa kalimat penjelasan di bawahnya),
// (b) diFitTitle() memotong+kasih ".." title SEBELUM disimpan kalau lebih
// panjang dari muat maksimum pil, pakai rumus lebar yg SAMA PERSIS dgn
// diTargetW() supaya keduanya selalu konsisten, dan (c) sbg jaring
// pengaman terakhir, drawDynamicIsland() sekarang membungkus SEMUA isi pil
// (ikon+teks+badge+progress bar) dgn setClipRect() persis di batas pil --
// walau ada kasus tak terduga lain nanti, piksel teks TIDAK PERNAH bisa
// tergambar keluar dari bentuk pil lagi.
// =============================================
enum DIState { DI_IDLE, DI_COMPACT, DI_EXPANDED };
DIState diState = DI_IDLE, diTargetState = DI_IDLE;
unsigned long diAnimStartMs = 0;
float diAnimFromW = 0, diAnimFromH = 0;
float diCurW = 0, diCurH = 0;
const float DI_ANIM_MS = 380.0f; // lbh lama dr transisi nav (170ms) -- morph perlu "napas" biar kenyal

char     diIcon = 0;
String   diTitle = "", diBadge = ""; // v17: diSubtitle dihapus -- cuma 1 baris teks skrg
uint16_t diColor = 0;
float    diProgress = -1;   // -1 = tanpa progress bar
bool     diPulse = false;   // true = ada live activity latar (tetap COMPACT & berdenyut)
unsigned long diExpandedUntil = 0;
Screen   diLinkedScreen = SCR_HOME;
bool     diHasLink = false;
int      diCurX = 0, diCurY = 2; // posisi terakhir dirender, dipakai loop() utk hit-test tap

// ease "back": dikit overshoot lalu balik pas ke target -- beda dr navEase
// yg datar simetris; overshoot inilah yg bikin morph kerasa kenyal/hidup.
float diSpringEase(float p){
  const float c1=1.70158f, c3=c1+1.0f;
  float x=p-1.0f;
  return 1.0f + c3*x*x*x + c1*x*x;
}

int diTargetW(){
  if(diTargetState==DI_IDLE) return 0;
  if(diTargetState==DI_COMPACT){
    int w = diBadge.length()? (30+(int)diBadge.length()*7) : 34;
    return constrain(w, 24, SCR_W-28); // v17: diklem jg spy badge panjang gak pernah minta lebar > layar
  }
  int tw = (int)diTitle.length()*6;
  int w = 20 + tw + 16; // ikon + teks + padding
  return constrain(w, 96, SCR_W-28);
}
// v17: tinggi expanded dikecilkan (30->26) krn skrg cuma 1 baris teks,
// gak perlu ruang ekstra lagi utk baris subtitle ke-2.
int diTargetH(){ return diTargetState==DI_IDLE ? 0 : (diTargetState==DI_EXPANDED ? 26 : 16); }

void diBeginAnim(){ diAnimFromW=diCurW; diAnimFromH=diCurH; diAnimStartMs=millis(); }

// v17: potong (truncate + "..") title SEBELUM disimpan kalau lebih panjang
// dari muat maksimum pil -- rumus lebar di sini SENGAJA disamakan persis
// dgn diTargetW() (20 padding-kiri+ikon, 16 padding-kanan, font 6px/char)
// supaya lebar pil & panjang teks yg akhirnya digambar SELALU konsisten
// satu sama lain (gak ada lagi kasus pil di-clamp sempit tapi teksnya
// tetap digambar sepanjang aslinya).
void diFitTitle(const String& title){
  int maxTextPx = (SCR_W-28) - 36; // 36 = padding kiri(20) + padding kanan(16), sama dgn diTargetW()
  int maxChars = maxTextPx/6;
  if(maxChars<3) maxChars=3;
  if((int)title.length() > maxChars) diTitle = title.substring(0, maxChars-2) + "..";
  else diTitle = title;
}

// Tampilkan notifikasi baru. keepAliveCompact=true -> setelah expandMs, pil
// MENGECIL ke COMPACT & terus berdenyut (dipakai selama activity latar msh
// jalan, mis. AI msh mikir / kuis msh berlangsung); false -> pil MENGHILANG
// TOTAL setelah expandMs (notifikasi sekali-tembak spt benar/salah/hasil).
// v16 FIX: default value "" utk parameter 'badge' SUDAH dideklarasikan di
// forward declaration diNotify() di puncak file -- TIDAK boleh ditulis lagi
// di sini (definisi asli), karena C++ melarang default argument yang sama
// dideklarasikan dua kali. Signature di bawah ini sengaja TANPA "=\"\"".
// v17: parameter 'subtitle' DIHAPUS TOTAL -- pesan sekarang cuma SATU
// frasa singkat (title saja), lihat catatan "v17" di atas struct DIState.
void diNotify(char icon, const String& title, uint16_t color,
              unsigned long expandMs, bool keepAliveCompact, const String& badge){
  diIcon=icon; diFitTitle(title); diColor=color;
  diPulse=keepAliveCompact; diBadge=badge; diProgress=-1; diHasLink=false;
  diExpandedUntil = millis()+expandMs;
  diBeginAnim();
  diTargetState = DI_EXPANDED;
  needRedrawNow();
}
void diSetProgress(float p){ diProgress=constrain(p,0.0f,1.0f); needRedrawNow(); }
void diLink(Screen s){ diLinkedScreen=s; diHasLink=true; }
void diHide(){
  diPulse=false; diHasLink=false;
  if(diTargetState!=DI_IDLE){ diBeginAnim(); diTargetState=DI_IDLE; }
}

bool diHitTest(int x,int y){
  if(diState==DI_IDLE && diTargetState==DI_IDLE) return false;
  return x>=diCurX && x<=diCurX+(int)diCurW && y>=diCurY && y<=diCurY+(int)diCurH;
}
void diHandleTap(){
  if(diHasLink && curScreen()!=diLinkedScreen){ navPush(diLinkedScreen); return; }
  if(diTargetState==DI_COMPACT){ // ketuk pil compact -> mekar lagi sebentar
    diExpandedUntil = millis()+1600;
    diBeginAnim(); diTargetState=DI_EXPANDED;
    needRedrawNow();
  }
}

// Dipanggil TIAP loop() (murah): urus auto-collapse expanded->compact/idle
// & pastikan needRedraw menyala selama masih ada animasi/denyut berjalan.
void diUpdate(){
  if(diTargetState==DI_EXPANDED && millis()>diExpandedUntil){
    diBeginAnim();
    diTargetState = diPulse? DI_COMPACT : DI_IDLE;
  }
  bool midAnim = (diState!=diTargetState) || (millis()-diAnimStartMs < (unsigned long)DI_ANIM_MS);
  if(midAnim || (diState==DI_COMPACT && diPulse) || diTargetState==DI_EXPANDED) needRedraw=true;
}

void drawDynamicIsland(LGFX_Sprite& s){
  float t=(millis()-diAnimStartMs)/DI_ANIM_MS; if(t>1.0f)t=1.0f;
  float e=diSpringEase(t);
  float tw=diAnimFromW+(diTargetW()-diAnimFromW)*e;
  float th=diAnimFromH+(diTargetH()-diAnimFromH)*e;
  if(t>=1.0f){ diState=diTargetState; tw=(float)diTargetW(); th=(float)diTargetH(); }
  diCurW=tw; diCurH=th;
  if(tw<1||th<1){ diCurX=SCR_W/2; diCurY=2; return; }

  int w=(int)tw,h=(int)th;
  int x=SCR_W/2-w/2, y=2;
  diCurX=x; diCurY=y;

  s.fillRoundRect(x,y,w,h,h/2,0x0000); // pil hitam pekat, senada notch HP asli

  // v17: jaring pengaman terakhir -- SEMUA isi pil (ikon/teks/badge/
  // progress bar) digambar di dalam clip rect PERSIS seukuran pil, jadi
  // walau ada kasus tak terduga lain di masa depan, potongan piksel teks
  // TIDAK PERNAH bisa tergambar keluar dari bentuk pil lagi.
  s.setClipRect(x,y,w,h);

  bool showingCompactLook = (diState==DI_COMPACT) || (diTargetState==DI_COMPACT && t<1.0f);
  if(showingCompactLook && h<22){
    if(diBadge.length()){
      s.setTextColor(0xFFFF); s.setTextSize(1);
      int tw2=diBadge.length()*6;
      s.setCursor(x+w/2-tw2/2, y+h/2-4); s.print(diBadge);
    } else {
      float pulse = diPulse ? (0.6f+0.4f*sinf(millis()/260.0f)) : 1.0f;
      s.fillCircle(x+w/2, y+h/2, max(2,(int)(3*pulse)), diColor);
    }
  } else if(h>=22){
    int iconR = max(6,(h-6)/2);
    int iconCx=x+8+iconR, iconCy=y+h/2;
    drawAppIcon(s, diIcon, iconCx, iconCy, iconR, diColor);
    s.setTextColor(0xFFFF); s.setTextSize(1);
    int textX = iconCx+iconR+6;
    // v17: cuma SATU baris title -- subtitle dihapus total (lihat catatan
    // v17 di atas struct DIState utk alasan lengkapnya).
    s.setCursor(textX, y+h/2-4); s.print(diTitle);
    if(diProgress>=0){
      int pbY=y+h-5, pbX=textX, pbW=(x+w-6)-textX;
      if(pbW>10){
        s.fillRoundRect(pbX,pbY,pbW,2,1,0x39C7);
        s.fillRoundRect(pbX,pbY,(int)(pbW*diProgress),2,1,diColor);
      }
    }
  }

  s.clearClipRect();
}

// =============================================
// BACK BUTTON
// =============================================
#define BACK_W 62
#define BACK_H 24
int backX(){ return 4; }
int backY(){ return SCR_H-BACK_H-3; }

void drawBack(LGFX_Sprite& s){
  if(kbVisible)return;
  s.fillRoundRect(backX(),backY(),BACK_W,BACK_H,6,T().surface2);
  s.setTextColor(T().accent);s.setTextSize(1);
  s.setCursor(backX()+10,backY()+8);s.print("< Back");
}

bool isBack(int x,int y){
  return !kbVisible && x>=backX() && x<=backX()+BACK_W && y>=backY() && y<=backY()+BACK_H;
}

// =============================================
// LOCK SCREEN
// =============================================
float lockDragY=0; bool lockDragging=false;

void drawLockScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  s.setTextColor(T().text); s.setTextSize(4);
  int tw=strlen(tb)*24;
  s.setCursor(SCR_W/2-tw/2, SCR_H/2-60-(int)lockDragY);
  s.print(tb);
  if(ok){
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28]; sprintf(db,"%s, %02d %s %04d",days[t.tm_wday],t.tm_mday,mons[t.tm_mon],t.tm_year+1900);
    s.setTextColor(T().subtext); s.setTextSize(1);
    int dw=strlen(db)*6;
    s.setCursor(SCR_W/2-dw/2, SCR_H/2-10-(int)lockDragY);
    s.print(db);
  }
  s.setTextColor(T().subtext); s.setTextSize(1);
  const char* hint="^ geser ke atas utk buka ^";
  s.setCursor(SCR_W/2-strlen(hint)*3, SCR_H-24);
  s.print(hint);
}

void lockScreenInput(bool touched,bool newT,int tx,int ty){
  static int startY=0;
  if(newT){ startY=ty; lockDragging=true; lockDragY=0; needRedraw=true; }
  else if(touched && lockDragging){
    int dy = startY-ty;
    lockDragY = constrain((float)dy, 0.0f, 80.0f);
    needRedraw=true;
  } else if(!touched && lockDragging){
    lockDragging=false;
    if(lockDragY>50){ locked=false; navGoHome(); }
    lockDragY=0; needRedraw=true;
  }
}

// =============================================
// CONTROL CENTER
// =============================================
float ccOffset = 0;
// FIX v11: dulu animasi buka/tutup Control Center pakai formula ad-hoc
// per-frame (ccOffset += (target-ccOffset)*0.4f + 1.5f) yg kecepatannya
// ikut naik-turun sesuai framerate loop() saat itu (kadang keliatan
// patah-patah kalau loop lagi sibuk, mis. pas WiFi/AI jalan).
// Sekarang dibuat berbasis WAKTU (millis) + fungsi easing yg sama dgn
// transisi antar-layar (navEase), jadi kecepatan animasinya SELALU mulus
// & konsisten independen dari framerate.
bool  ccAnimating=false;
float ccAnimFromH=0, ccAnimToH=0;
unsigned long ccAnimStartMs=0;
const float CC_ANIM_MS = 200.0f;
#define CC_COLS 3
#define CC_ROWS 3
#define CC_ITEMS 7
int ccPanelH(); // forward decl - didefinisikan di bawah, dipakai openControlCenter()
void openControlCenter(){
  controlCenterOpen=true;
  ccAnimFromH=ccOffset; ccAnimToH=(float)ccPanelH();
  ccAnimStartMs=millis(); ccAnimating=true;
  needRedraw=true;
}
void closeControlCenter(){
  ccAnimFromH=ccOffset; ccAnimToH=0.0f;
  ccAnimStartMs=millis(); ccAnimating=true;
  needRedraw=true;
}

// ---- Layout dihitung dinamis (BUKAN persentase tetap) supaya 6 kartu +
// slider brightness SELALU muat & terlihat penuh, di landscape maupun
// portrait. drawControlCenter() dan ccTouch() memakai fungsi yg SAMA
// persis di bawah ini, jadi area yg digambar dan area yg bisa disentuh
// tidak akan pernah meleset lagi (dulu inilah sebab "area kosong Tema"
// ke-tap dan mengubah tema, karena baris ke-2 kartu tidak digambar tapi
// koordinat sentuhnya tetap aktif).
// v13: Control Center dirombak jadi TOMBOL BULAT (bukan kartu kotak lagi)
// senada dgn quick-toggle di HP sungguhan (lingkaran ikon + label kecil di
// BAWAHnya, terpisah dari lingkaran -- bukan tulisan ditumpuk di dlm kotak).
// Diameter lingkaran (ccCircleD) dihitung dari lebar kolom yg tersedia
// (ccCellW, yg mengikuti SCR_W panel) supaya otomatis menyesuaikan layar,
// TAPI dipagari batas atas (base) yg sengaja dikecilkan sedikit drpd ukuran
// kartu v12 (38/52) biar kesannya lbih padat/mirip toggle asli, bukan
// membesar penuh memenuhi kolom.
int ccMarginX(){ return 8; }  // v12: inset kiri/kanan panel -> kesan mengambang
int ccPanelW(){ return SCR_W - ccMarginX()*2; }
int ccGap(){ return 7; } // v12: dipadatkan (dulu 10)
int ccCardW(){ return (ccPanelW() - ccGap()*(CC_COLS+1))/CC_COLS; } // lebar 1 kolom/sel
int ccCircleD(){
  int cellW = ccCardW();
  int base = currentOrient==ORIENT_LANDSCAPE ? 30 : 40; // v13: agak dikecilkan drpd ccCardH v12 (38/52)
  int d = min(cellW-8, base); // menyesuaikan layar: mengecil otomatis kalau kolom sempit
  if(d<24) d=24;
  return d;
}
int ccCardH(){ return ccCircleD()+14; } // tinggi 1 sel = lingkaran + jarak tipis + label
int ccGridX(){ return ccMarginX()+ccGap(); }
int ccGridTop(){ return STATUS_H+6; }
int ccGridH(){ return CC_ROWS*ccCardH() + (CC_ROWS-1)*ccGap(); }
int ccSliderLabelY(){ return ccGridTop()+ccGridH()+8; }
int ccSliderTrackY(){ return ccSliderLabelY()+12; }
int ccContentBottom(){ return ccSliderTrackY()+16; }
int ccPanelH(){
  int needed = ccContentBottom()+12;   // + ruang utk drag-handle di bawah
  int maxAvail = SCR_H-8;              // jgn sampai nutup 1 layar penuh
  return needed<maxAvail? needed : maxAvail;
}

void ccActWifi(){ if(wifiConnected) disconnectWifi(); else connectWifi(); }
void ccActAirplane(){ toggleAirplaneMode(); }
void ccActDnd(){ dndMode=!dndMode; }
void ccActTheme(){ themeIdx=(themeIdx+1)%THEME_COUNT; saveTheme(); }
void ccActOrient(){
  applyOrientation(currentOrient==ORIENT_LANDSCAPE?ORIENT_PORTRAIT:ORIENT_LANDSCAPE, true);
  if(autoRotateEnabled){
    autoRotateEnabled = false;
    saveAutoRotatePref();
    showToast("Auto-rotate dimatikan (manual)");
  }
}
void ccActShake(){
  shakeEnabled = !shakeEnabled;
  saveShakePref();
  showToast(shakeEnabled?"Shake to Home aktif":"Shake to Home mati");
}

// FIX v11: Control Center dulu isinya kotak polos + tulisan status
// ("WiFi: ON" dst). Sekarang tiap tombol punya IKON vektor sendiri
// (mirip drawAppIcon di Home) + label singkat kecil di bawahnya, dan
// kartunya dikasih ruang lebih lega (lihat ccGap/ccCardH di atas).
void drawCCIcon(LGFX_Sprite& s, int idx, int cx, int cy, int r, bool active){
  uint16_t ic = active ? T().bg : T().text; // kontras dgn warna latar tombol
  switch(idx){
    case 0: { // WiFi: titik + 2 lengkung sinyal, dicoret kalau OFF
      s.fillCircle(cx,cy+r-8,2,ic);
      s.drawArc(cx,cy+r-8,7,6,210,330,ic);
      s.drawArc(cx,cy+r-8,12,11,210,330,ic);
      if(!wifiConnected){
        s.drawLine(cx-r+4,cy-r+4,cx+r-4,cy+r-4,ic);
      }
      break;
    }
    case 1: { // Airplane: pesawat kertas sederhana
      s.fillTriangle(cx,cy-r+6, cx-4,cy+7, cx+4,cy+7, ic);
      s.fillTriangle(cx-r+4,cy+9, cx+r-4,cy+9, cx,cy-2, ic);
      break;
    }
    case 2: { // DND: bulan sabit
      s.fillCircle(cx,cy,r-6,ic);
      s.fillCircle(cx+5,cy-3,r-7, active?T().accent:T().surface2);
      break;
    }
    case 3: { // Tema: palet warna (lingkaran + titik2 warna)
      s.drawCircle(cx,cy,r-6,ic);
      s.fillCircle(cx-5,cy-3,2, active?T().bg:T().accent);
      s.fillCircle(cx+4,cy-5,2, active?T().bg:T().good);
      s.fillCircle(cx+6,cy+4,2, active?T().bg:T().danger);
      s.fillCircle(cx-3,cy+6,2, active?T().bg:T().accent2);
      break;
    }
    case 4: { // Orientasi: siluet HP (ikut orientasi skrg) + panah rotasi
      if(currentOrient==ORIENT_LANDSCAPE){
        s.drawRoundRect(cx-10,cy-6,20,12,2,ic);
      } else {
        s.drawRoundRect(cx-6,cy-10,12,20,2,ic);
      }
      s.drawArc(cx,cy,r-1,r-2,20,300,ic);
      s.fillTriangle(cx+r-6,cy-r+4, cx+r-1,cy-r+7, cx+r-8,cy-r+9, ic);
      break;
    }
    case 5: { // Shake: garis zig-zag (getaran)
      s.drawLine(cx-9,cy+2,cx-4,cy-6,ic);
      s.drawLine(cx-4,cy-6,cx+1,cy+4,ic);
      s.drawLine(cx+1,cy+4,cx+6,cy-6,ic);
      s.drawLine(cx+6,cy-6,cx+10,cy,ic);
      break;
    }
    case 6: { // Kunci layar: gembok
      s.drawRoundRect(cx-7,cy-1,14,11,2,ic);
      s.drawArc(cx,cy-3,6,5,180,360,ic);
      break;
    }
  }
}

void drawControlCenter(LGFX_Sprite& s){
  int ph = (int)ccOffset;
  if(ph<=0) return;
  // v12: panel mengambang -> inset margin kiri/kanan + sudut membulat.
  // Radius di-clamp ke ph/2 supaya gak "meledak" pas ph masih kecil di
  // awal animasi buka/tutup (fillRoundRect dgn radius > tinggi rect bisa
  // bikin bentuknya aneh).
  int mx = ccMarginX(), pw = ccPanelW();
  int rad = min(16, ph/2); if(rad<0) rad=0;
  s.fillRoundRect(mx,0,pw,ph,rad,T().surface);
  s.fillRoundRect(SCR_W/2-14,ph-9,28,4,2,T().divider);

  const char* labels[CC_ITEMS]={
    "WiFi", "Airplane", "DND", "Tema", "Orientasi", "Shake", "Kunci"
  };
  bool activeState[CC_ITEMS]={wifiConnected,airplaneMode,dndMode,false,false,shakeEnabled,false};
  // v13: tombol skrg BULAT (fillCircle) drpd kotak, label ditulis di LUAR
  // lingkaran (di bawahnya) -- persis pola toggle quick-settings di HP asli.
  int cw=ccCardW(), ch=ccCardH(), gap=ccGap(), top=ccGridTop(), gx=ccGridX();
  int d=ccCircleD(), crad=d/2; // v13: nama 'crad' (bukan 'rad') biar tdk bentrok dgn 'rad' radius sudut panel di atas
  int iconR = crad-5; if(iconR<9) iconR=9; if(iconR>16) iconR=16;
  for(int i=0;i<CC_ITEMS;i++){
    int col=i%CC_COLS, row=i/CC_COLS;
    int x=gx+col*(cw+gap);
    int y=top+row*(ch+gap);
    int ccx=x+cw/2, ccy=y+crad; // pusat lingkaran, rata atas dlm sel

    uint16_t bg = activeState[i]? T().accent : T().surface2;
    s.fillCircle(ccx,ccy,crad,bg);
    drawCCIcon(s, i, ccx, ccy, iconR, activeState[i]);

    // label di luar lingkaran -> warnanya dari palet teks umum (bukan lawan
    // warna latar tombol lagi), aksen dipakai saat status ON biar tetap
    // kebaca statusnya walau labelnya sudah tidak menumpuk di dlm tombol.
    uint16_t fg = activeState[i]? T().accent : T().subtext;
    s.setTextColor(fg); s.setTextSize(1);
    int lw=(int)strlen(labels[i])*6;
    s.setCursor(ccx-lw/2, y+d+3);
    s.print(labels[i]);
  }

  s.setTextColor(T().subtext); s.setTextSize(1);
  s.setCursor(gx, ccSliderLabelY()); s.print("Brightness");
  int sx=gx, sw=pw-gap*2, sy=ccSliderTrackY();
  s.fillRoundRect(sx,sy,sw,9,4,T().divider);
  int fillW = map(brightness,0,255,0,sw);
  s.fillRoundRect(sx,sy,fillW,9,4,T().accent);
  // v13: handle bulat di ujung slider, senada dgn gaya "bulat" Control Center
  int thumbCx=sx+fillW, thumbCy=sy+4;
  s.fillCircle(thumbCx,thumbCy,7,T().bg);
  s.fillCircle(thumbCx,thumbCy,5,T().accent);

  drawToast(s);
}

void ccTouch(int x,int y){
  int ph=(int)ccOffset;
  if(y> ph-9 && y<=ph+4){ closeControlCenter(); return; }
  if(y>ph) { closeControlCenter(); return; }

  int cw=ccCardW(), ch=ccCardH(), gap=ccGap(), top=ccGridTop(), gx=ccGridX();
  void(*actions[CC_ITEMS])() = { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, ccActShake, nullptr };
  for(int i=0;i<CC_ITEMS;i++){
    int col=i%CC_COLS, row=i/CC_COLS;
    int bx=gx+col*(cw+gap);
    int by=top+row*(ch+gap);
    if(x>=bx&&x<=bx+cw&&y>=by&&y<=by+ch){
      if(i==6){ locked=true; closeControlCenter(); return; }
      if(actions[i]) actions[i]();
      needRedraw=true;
      return;
    }
  }
  int sy=ccSliderTrackY();
  if(y>=sy-8 && y<=sy+18){
    int sx=gx, sw=ccPanelW()-gap*2;
    brightness=constrain(map(x-sx,0,sw,0,255),10,255);
    display.setBrightness(brightness);
    needRedraw=true;
  }
}

// =============================================
// APP ICONS (vektor sederhana - bukan huruf tunggal)
// =============================================
// Setiap ikon digambar dgn bentuk yg berhubungan dgn fungsinya, dgn warna
// kontras (bg = warna teks/garis, fg tetap dipakai utk latar lingkaran).
void drawAppIcon(LGFX_Sprite& s, char sym, int cx, int cy, int r, uint16_t bgCircle){
  s.fillCircle(cx,cy,r,bgCircle);
  uint16_t ic = T().bg; // warna vektor ikon (kontras dgn lingkaran berwarna)
  switch(sym){
    case 'J': { // Jam: wajah jam + jarum
      s.drawCircle(cx,cy,r-3,ic);
      s.drawLine(cx,cy,cx,cy-r+6,ic);
      s.drawLine(cx,cy,cx+r-8,cy+2,ic);
      s.fillCircle(cx,cy,1,ic);
      break;
    }
    case '+': { // Kalkulator: kotak + grid tombol
      s.fillRoundRect(cx-r+4,cy-r+4,(r-4)*2,(r-4)*2,3,ic);
      s.fillRect(cx-r+7,cy-r+7,(r-4)*2-6,5,bgCircle);
      for(int i=0;i<2;i++) for(int j=0;j<3;j++)
        s.fillRect(cx-r+8+j*7,cy-2+i*7,4,4,bgCircle);
      break;
    }
    case '3': { // Orientasi 3D: kubus wireframe mini
      int w=r-7;
      s.drawRect(cx-w,cy-w+4,w*2-4,w*2-4,ic);
      s.drawLine(cx-w,cy-w+4,cx-w+6,cy-w-2,ic);
      s.drawLine(cx+w-4,cy-w+4,cx+w+2,cy-w-2,ic);
      s.drawLine(cx-w+6,cy-w-2,cx+w+2,cy-w-2,ic);
      s.drawLine(cx+w-4,cy-w+4,cx+w-4,cy+w-4,ic);
      break;
    }
    case '@': { // Setting: gear
      s.drawCircle(cx,cy,r-6,ic);
      for(int i=0;i<8;i++){
        float a=i*PI/4.0f;
        int x1=cx+(int)(cosf(a)*(r-6)), y1=cy+(int)(sinf(a)*(r-6));
        int x2=cx+(int)(cosf(a)*(r-1)), y2=cy+(int)(sinf(a)*(r-1));
        s.drawLine(x1,y1,x2,y2,ic);
      }
      s.fillCircle(cx,cy,4,ic);
      s.fillCircle(cx,cy,2,bgCircle);
      break;
    }
    case 'N': { // Notepad: kertas bergaris
      s.fillRoundRect(cx-r+6,cy-r+4,(r-6)*2,(r-4)*2,2,ic);
      for(int i=0;i<3;i++) s.drawFastHLine(cx-r+10,cy-r+10+i*6,(r-6)*2-8,bgCircle);
      break;
    }
    case 'C': { // Canvas: kuas lukis
      s.fillCircle(cx-4,cy-4,4,ic);
      s.drawLine(cx-1,cy-1,cx+7,cy+7,ic);
      s.drawLine(cx+2,cy-2,cx+8,cy+4,ic);
      s.fillCircle(cx+8,cy+7,2,ic);
      break;
    }
    case 'A': { // AI Chat: gelembung percakapan
      s.fillRoundRect(cx-r+5,cy-r+7,(r-5)*2,(r-7)*2-2,4,ic);
      s.fillTriangle(cx-4,cy+r-9,cx+2,cy+r-9,cx-6,cy+r-2,ic);
      s.fillCircle(cx-5,cy-2,1,bgCircle); s.fillCircle(cx,cy-2,1,bgCircle); s.fillCircle(cx+5,cy-2,1,bgCircle);
      break;
    }
    case 'F': { // Files: folder
      s.fillRoundRect(cx-r+5,cy-r+6,14,6,2,ic);
      s.fillRoundRect(cx-r+5,cy-r+10,(r-5)*2,(r-10)*2,2,ic);
      break;
    }
    case 'M': { // MJPEG: tombol play
      s.fillTriangle(cx-6,cy-8,cx-6,cy+8,cx+9,cy,ic);
      break;
    }
    case 'U': { // Update: panah unduh
      s.fillRect(cx-3,cy-9,6,10,ic);
      s.fillTriangle(cx-8,cy,cx+8,cy,cx,cy+10,ic);
      break;
    }
    case 'B': { // Baterai
      s.drawRoundRect(cx-r+6,cy-8,(r-6)*2-4,16,3,ic);
      s.fillRect(cx+r-8,cy-4,3,8,ic);
      s.fillRect(cx-r+9,cy-5,(r-6)*2-10,10,ic);
      break;
    }
    // ---------------- GAME ----------------
    case 'S': { // Snake
      s.fillCircle(cx-8,cy+6,4,ic);
      s.fillCircle(cx-2,cy+2,4,ic);
      s.fillCircle(cx+3,cy-4,4,ic);
      s.fillCircle(cx+8,cy-8,5,ic);      // kepala
      s.fillCircle(cx+9,cy-9,1,bgCircle); // mata
      break;
    }
    case 'V': { // Flappy Block: burung
      s.fillCircle(cx-2,cy,7,ic);
      s.fillTriangle(cx+5,cy-2,cx+5,cy+2,cx+12,cy,ic);   // paruh
      s.fillTriangle(cx-9,cy-2,cx-2,cy-6,cx-2,cy+2,ic);  // sayap
      s.fillCircle(cx+2,cy-3,1,bgCircle); // mata
      break;
    }
    case '2': { // 2048: grid 2x2
      int gw=r-4;
      s.fillRoundRect(cx-gw,cy-gw,gw-2,gw-2,2,ic);
      s.fillRoundRect(cx+2,cy-gw,gw-2,gw-2,2,ic);
      s.fillRoundRect(cx-gw,cy+2,gw-2,gw-2,2,ic);
      s.fillRoundRect(cx+2,cy+2,gw-2,gw-2,2,ic);
      break;
    }
    case 'X': { // Tic-Tac-Toe: grid + X/O
      s.drawFastVLine(cx-4,cy-9,18,ic);
      s.drawFastVLine(cx+4,cy-9,18,ic);
      s.drawFastHLine(cx-9,cy-4,18,ic);
      s.drawFastHLine(cx-9,cy+4,18,ic);
      s.drawLine(cx-8,cy-8,cx-2,cy-2,ic);
      s.drawLine(cx-8,cy-2,cx-2,cy-8,ic);
      s.drawCircle(cx+5,cy+5,3,ic);
      break;
    }
    case 'K': { // Breakout: bricks + paddle + bola
      s.fillRect(cx-8,cy-9,6,4,ic);
      s.fillRect(cx-1,cy-9,6,4,ic);
      s.fillRect(cx+6,cy-9,4,4,ic);
      s.fillCircle(cx-2,cy+1,2,ic);
      s.fillRoundRect(cx-9,cy+7,18,4,2,ic);
      break;
    }
    // -------- Trivia Quiz & Dynamic Island (v14) --------
    // Skala pakai fraksi r (bukan offset absolut spt di atas) krn dipakai
    // dobel: ukuran normal di grid Home (r~14) DAN ukuran mini di badge
    // Dynamic Island (r bisa sekecil ~6).
    case 'Q': { // Trivia: tanda tanya
      int rr = max(3, r*3/4);
      s.drawArc(cx, cy-rr/3, rr/2, max(1,rr/2-2), -150, 60, ic);
      s.fillCircle(cx, cy+rr/2, max(1,rr/6), ic);
      break;
    }
    case 'Y': { // Feedback: jawaban benar (centang)
      int rr=max(4,r*3/4);
      s.drawLine(cx-rr/2, cy,   cx-rr/6, cy+rr/2,   ic);
      s.drawLine(cx-rr/6, cy+rr/2, cx+rr/2, cy-rr/3, ic);
      s.drawLine(cx-rr/2, cy+1, cx-rr/6, cy+rr/2+1, ic);
      s.drawLine(cx-rr/6, cy+rr/2+1, cx+rr/2, cy-rr/3+1, ic);
      break;
    }
    case 'W': { // Feedback: jawaban salah (silang)
      int rr=max(4,r*3/4);
      s.drawLine(cx-rr/2,cy-rr/2,cx+rr/2,cy+rr/2,ic);
      s.drawLine(cx-rr/2,cy+rr/2,cx+rr/2,cy-rr/2,ic);
      s.drawLine(cx-rr/2,cy-rr/2+1,cx+rr/2,cy+rr/2+1,ic);
      s.drawLine(cx-rr/2,cy+rr/2+1,cx+rr/2,cy-rr/2+1,ic);
      break;
    }
    case 'L': { // Mode Game aktif: petir/boost
      int rr=max(4,r*3/4);
      s.fillTriangle(cx+rr/4,cy-rr, cx-rr/3,cy+rr/6, cx+rr/8,cy+rr/6, ic);
      s.fillTriangle(cx-rr/8,cy-rr/6, cx+rr/3,cy-rr/6, cx-rr/4,cy+rr, ic);
      break;
    }
    default: {
      s.setTextColor(ic); s.setTextSize(2);
      char b[2]={sym,0};
      s.setCursor(cx-6,cy-8); s.print(b);
    }
  }
}

// =============================================
// HOME SCREEN & APP INTERFACE
// =============================================
struct AppDef {
  const char* name; char sym; uint16_t color;
  void(*onEnter)(); void(*onExit)();
  void(*draw)(LGFX_Sprite&);
  void(*touch)(int,int,bool,bool);
  Screen screen;
};

void clockEnter(); void clockExit();
void drawClock(LGFX_Sprite&); void clockTouch(int,int,bool,bool);
void calcEnter(); void calcExit();
void drawCalc(LGFX_Sprite&); void calcTouch(int,int,bool,bool);
void sensorEnter(); void sensorExit();
void drawSensor(LGFX_Sprite&); void sensorTouch(int,int,bool,bool);
void settingsEnter(); void settingsExit();
void drawSettings(LGFX_Sprite&); void settingsTouch(int,int,bool,bool);
void notepadEnter(); void notepadExit();
void drawNotepad(LGFX_Sprite&); void notepadTouch(int,int,bool,bool);
void canvasEnter(); void canvasExit();
void drawCanvasScreen(LGFX_Sprite&); void canvasTouch(int,int,bool,bool);
void aiEnter(); void aiExit();
void drawAiChat(LGFX_Sprite&); void aiTouch(int,int,bool,bool);
void fileExpEnter(); void fileExpExit();
void drawFileExplorer(LGFX_Sprite&); void fileExpTouch(int,int,bool,bool);
// ---- MJPEG PLAYER: forward declarations ----
void mjpegEnter(); void mjpegExit();
void drawMjpegPlayer(LGFX_Sprite&); void mjpegTouch(int,int,bool,bool);
// ---- UPDATE (OTA): forward declarations ----
void updEnter(); void updExit();
void drawUpdate(LGFX_Sprite&); void updTouch(int,int,bool,bool);
// ---- BATERAI: forward declarations (BARU di v8) ----
void battEnter(); void battExit();
void drawBatteryApp(LGFX_Sprite&); void battTouch(int,int,bool,bool);
// ---- GAME: forward declarations ----
void snakeEnter(); void snakeExit();
void drawSnake(LGFX_Sprite&); void snakeTouch(int,int,bool,bool);
void flapEnter(); void flapExit();
void drawFlap(LGFX_Sprite&); void flapTouch(int,int,bool,bool);
void g2048Enter(); void g2048Exit();
void draw2048(LGFX_Sprite&); void g2048Touch(int,int,bool,bool);
void tttEnter(); void tttExit();
void drawTtt(LGFX_Sprite&); void tttTouch(int,int,bool,bool);
void brkEnter(); void brkExit();
void drawBrk(LGFX_Sprite&); void brkTouch(int,int,bool,bool);
// ---- TRIVIA QUIZ: forward declarations (BARU di v14) ----
void triviaEnter(); void triviaExit();
void drawTrivia(LGFX_Sprite&); void triviaTouch(int,int,bool,bool);

AppDef apps[17] = {
  { "Jam",        'J', 0, clockEnter,    clockExit,    drawClock,        clockTouch,    SCR_CLOCK },
  { "Kalkulator", '+', 0, calcEnter,     calcExit,     drawCalc,         calcTouch,     SCR_CALC },
  { "Orientasi3D", '3', 0, sensorEnter,   sensorExit,   drawSensor,       sensorTouch,   SCR_SENSOR },
  { "Setting",    '@', 0, settingsEnter, settingsExit, drawSettings,     settingsTouch, SCR_SETTINGS },
  { "Notepad",    'N', 0, notepadEnter,  notepadExit,  drawNotepad,      notepadTouch,  SCR_NOTEPAD },
  { "Canvas",     'C', 0, canvasEnter,   canvasExit,   drawCanvasScreen, canvasTouch,   SCR_CANVAS },
  { "AI Chat",    'A', 0, aiEnter,       aiExit,       drawAiChat,       aiTouch,       SCR_AICHAT },
  { "Files",      'F', 0, fileExpEnter,  fileExpExit,  drawFileExplorer, fileExpTouch,  SCR_FILEEXPLORER },
  { "MJPEG",      'M', 0, mjpegEnter,    mjpegExit,    drawMjpegPlayer,  mjpegTouch,    SCR_MJPEG },
  { "Update",     'U', 0, updEnter,      updExit,      drawUpdate,       updTouch,      SCR_UPDATE },
  { "Baterai",    'B', 0, battEnter,     battExit,     drawBatteryApp,   battTouch,     SCR_BATTERY },
  { "Snake",      'S', 0, snakeEnter,    snakeExit,    drawSnake,        snakeTouch,    SCR_SNAKE },
  { "Flappy",     'V', 0, flapEnter,     flapExit,     drawFlap,         flapTouch,     SCR_FLAPPY },
  { "2048",       '2', 0, g2048Enter,    g2048Exit,    draw2048,         g2048Touch,    SCR_2048 },
  { "TicTacToe",  'X', 0, tttEnter,      tttExit,      drawTtt,          tttTouch,      SCR_TTT },
  { "Breakout",   'K', 0, brkEnter,      brkExit,      drawBrk,          brkTouch,      SCR_BREAKOUT },
  { "Trivia",     'Q', 0, triviaEnter,   triviaExit,   drawTrivia,       triviaTouch,   SCR_TRIVIA },
};
#define APP_COUNT 17

void initAppColors(){
  apps[0].color=T().accent;   apps[1].color=T().accent2;
  apps[2].color=0x07FF;       apps[3].color=0xF81F;
  apps[4].color=0xFFE0;       apps[5].color=T().good;
  apps[6].color=0xFD40;       apps[7].color=0x3ADF;
  apps[8].color=0xFBE0; // MJPEG player - warna oranye
  apps[9].color=0xF800; // Update FW - warna merah (menonjol/perlu perhatian)
  apps[10].color=0x07E0; // Baterai - warna hijau
  // FIX v11 - AKAR MASALAH "logo game gak kelihatan semua, kayaknya
  // item hitam": index 11-15 (semua app GAME) dulu TIDAK PERNAH diisi
  // di sini, jadi apps[i].color tetap default 0 alias HITAM. Efeknya,
  // lingkaran latar ikon di Home (drawAppIcon bgCircle) jadi hitam
  // total, dan garis vektor ikon (warnanya T().bg, ikut gelap) jadi
  // nyaris tak kelihatan di atas lingkaran hitam itu. Sekarang tiap
  // game dapat warna cerah tersendiri spy ikonnya kontras & jelas.
  apps[11].color=0x1FF9; // Snake - hijau toska
  apps[12].color=0xFC9F; // Flappy Block - pink
  apps[13].color=0xFFD2; // 2048 - kuning keemasan
  apps[14].color=0x861F; // TicTacToe - ungu
  apps[15].color=0xFB40; // Breakout - oranye kemerahan
  apps[16].color=0xFDA0; // Trivia Quiz - kuning-oranye cerah (BARU v14)
}

int appIndexForScreen(Screen s){
  for(int i=0;i<APP_COUNT;i++) if(apps[i].screen==s) return i;
  return -1;
}
void appOnEnter(Screen s){ int i=appIndexForScreen(s); if(i>=0 && apps[i].onEnter) apps[i].onEnter(); }
void appOnExit(Screen s){  int i=appIndexForScreen(s); if(i>=0 && apps[i].onExit)  apps[i].onExit(); }

float homeScrollY=0, homeScrollVel=0;
int homeCols(){ return currentOrient==ORIENT_LANDSCAPE ? 3 : 2; }
int homeCardW(){ int cols=homeCols(); return (SCR_W - (cols+1)*6)/cols; }
int homeCardH(){ return 60; }
int homeDockY(){ return SCR_H-38-6; }

void drawHome(LGFX_Sprite& s,float sc){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(T().text);s.setTextSize(3);
  char tb[6]; if(ok)sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min);else strcpy(tb,"--:--");
  int tw=strlen(tb)*18;
  s.setCursor(SCR_W/2-tw/2,26);s.print(tb);
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=6, gridTop=72;
  for(int i=0;i<APP_COUNT;i++){
    int col=i%cols, row=i/cols;
    int x=gap+col*(cw+gap), y=gridTop+row*(ch+gap)-(int)sc;
    if(y+ch<STATUS_H+2||y>homeDockY()-4)continue;
    s.fillRoundRect(x,y,cw,ch,10,T().surface);
    drawAppIcon(s, apps[i].sym, x+cw/2, y+18, 14, apps[i].color);
    s.setTextColor(T().text);s.setTextSize(1);
    int nl=strlen(apps[i].name)*6;
    s.setCursor(x+cw/2-nl/2,y+ch-12);
    s.print(apps[i].name);
  }
  int dockY=homeDockY();
  s.fillRoundRect(6,dockY,SCR_W-12,38,12,T().surface2);
  int dockIdx[4]={0,4,6,7};
  int dw=(SCR_W-12)/4;
  for(int i=0;i<4;i++){
    int di=dockIdx[i];
    int cx=6+i*dw+dw/2;
    drawAppIcon(s, apps[di].sym, cx, dockY+19, 15, apps[di].color);
  }
  drawToast(s);
}

Screen homeCheck(int x,int y,float sc){
  int dockY=homeDockY();
  if(y>=dockY&&y<=dockY+38){
    int dockIdx[4]={0,4,6,7};
    int dw=(SCR_W-12)/4;
    for(int i=0;i<4;i++){
      int cx=6+i*dw+dw/2;
      if(abs(x-cx)<dw/2) return apps[dockIdx[i]].screen;
    }
  }
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=6, gridTop=72;
  for(int i=0;i<APP_COUNT;i++){
    int col=i%cols,row=i/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-(int)sc;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return apps[i].screen;
  }
  return SCR_HOME;
}

int homeMaxScroll(){
  int cols=homeCols();
  int rows=(APP_COUNT+cols-1)/cols;
  int ch=homeCardH(), gap=6, gridTop=72;
  int needed = gridTop + rows*(ch+gap) - homeDockY();
  return max(0, needed);
}

// =============================================
// GAME MODE: dipanggil tiap masuk ke game apapun. Menjalankan animasi
// loading singkat & bersih (ring progress muter + ikon kotak scale-in)
// + boost clock CPU ESP32-S3 ke 240MHz (maksimal) + jeda polling sensor
// latar belakang (MPU/baterai) selama main, biar loop game mulus tanpa
// gangguan. Balik ke clock normal (hemat daya) & polling aktif lagi
// pas keluar game.
// =============================================
#define CPU_MHZ_NORMAL 160
#define CPU_MHZ_GAME   240
bool gameModeActive = false;

// FIX v11: animasi lama ("game booster" ala HP gaming dgn ring memancar,
// sapuan cahaya, kilat, teks besar "GAME BOOSTER") kesannya terlalu
// ramai/norak. Diganti dgn loading indicator simpel & bersih: ring
// progress yg terisi halus (pakai easing yg sama dgn transisi navigasi)
// + kotak ikon kecil yg scale-in di tengahnya + 1 baris label singkat.
void gameBoosterAnim(){
  int cx=SCR_W/2, cy=SCR_H/2-6;
  const int STEPS = 18;
  int ringR = min(SCR_W,SCR_H)/7;
  if(ringR<24) ringR=24;
  if(ringR>40) ringR=40;

  for(int f=0; f<=STEPS; f++){
    float p = navEase((float)f/STEPS); // easing halus, sama dgn transisi navigasi
    canvas.fillSprite(T().bg);

    // Ring progress tipis, terisi searah jarum jam (2 lapis biar ada depth)
    canvas.drawArc(cx,cy,ringR,ringR-5,-90,-90+(int)(360*p),T().divider);
    canvas.drawArc(cx,cy,ringR,ringR-5,-90,-90+(int)(360*p*p),T().accent2);

    // Kotak ikon kecil, scale-in halus di tengah ring
    int boxSize=(int)(26*constrain(p*1.5f,0.0f,1.0f));
    if(boxSize>2){
      canvas.fillRoundRect(cx-boxSize/2,cy-boxSize/2,boxSize,boxSize,boxSize/4,T().accent);
    }

    if(p>0.45f){
      canvas.setTextColor(T().subtext); canvas.setTextSize(1);
      const char* label="Menyiapkan mode game...";
      int tw=(int)strlen(label)*6;
      canvas.setCursor(cx-tw/2, cy+ringR+16); canvas.print(label);
    }
    push();
    delay(10);
  }
  delay(120);
}

void enterGameMode(){
  if(!gameModeActive){
    gameModeActive = true;
    setCpuFrequencyMhz(CPU_MHZ_GAME);
  }
  gameBoosterAnim();
  diNotify('L', "Mode Game Aktif", T().accent, 1300, false); // v14 (v17: subtitle dihapus, jadi 1 baris singkat)
}
void exitGameMode(){
  if(!gameModeActive) return;
  gameModeActive = false;
  setCpuFrequencyMhz(CPU_MHZ_NORMAL);
}

// =============================================
// GAME: SNAKE (ketuk layar ke arah tujuan - kepala ular akan belok ke situ)
// =============================================
#define SNAKE_CELL 12
#define SNAKE_MAXLEN 500
struct SnakeSeg{ int x,y; };
SnakeSeg snakeBody[SNAKE_MAXLEN];
int  snakeCols, snakeRows, snakeLen;
int  snakeDirX, snakeDirY;
int  snakeFoodX, snakeFoodY;
int  snakeScore, snakeTickMs;
bool snakeOver;
unsigned long snakeLastTick;

void snakeSpawnFood(){
  bool onBody;
  do{
    onBody=false;
    snakeFoodX = random(0,snakeCols);
    snakeFoodY = random(0,snakeRows);
    for(int i=0;i<snakeLen;i++) if(snakeBody[i].x==snakeFoodX && snakeBody[i].y==snakeFoodY){ onBody=true; break; }
  } while(onBody);
}
void snakeReset(){
  snakeCols = SCR_W/SNAKE_CELL;
  snakeRows = (SCR_H-STATUS_H)/SNAKE_CELL;
  snakeLen = 3;
  for(int i=0;i<snakeLen;i++){ snakeBody[i].x = snakeCols/2 - i; snakeBody[i].y = snakeRows/2; }
  snakeDirX=1; snakeDirY=0;
  snakeScore=0; snakeOver=false; snakeTickMs=150;
  snakeSpawnFood();
  snakeLastTick=millis();
}
void snakeEnter(){ enterGameMode(); snakeReset(); }
void snakeExit(){ exitGameMode(); }

void snakeTick(){
  if(snakeOver) return;
  SnakeSeg newHead = { snakeBody[0].x+snakeDirX, snakeBody[0].y+snakeDirY };
  if(newHead.x<0||newHead.x>=snakeCols||newHead.y<0||newHead.y>=snakeRows){ snakeOver=true; return; }
  for(int i=0;i<snakeLen;i++) if(snakeBody[i].x==newHead.x && snakeBody[i].y==newHead.y){ snakeOver=true; return; }
  bool grow = (newHead.x==snakeFoodX && newHead.y==snakeFoodY);
  int newLen = grow ? min(snakeLen+1,SNAKE_MAXLEN) : snakeLen;
  for(int i=newLen-1;i>0;i--) snakeBody[i]=snakeBody[i-1];
  snakeBody[0]=newHead;
  snakeLen=newLen;
  if(grow){
    snakeScore+=10;
    if(snakeTickMs>70) snakeTickMs-=3;
    snakeSpawnFood();
  }
}

void drawSnake(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  if(!snakeOver && millis()-snakeLastTick>=(unsigned long)snakeTickMs){ snakeTick(); snakeLastTick=millis(); }

  int top=STATUS_H;
  for(int i=0;i<snakeLen;i++){
    uint16_t c = (i==0)?T().accent:T().good;
    s.fillRoundRect(snakeBody[i].x*SNAKE_CELL, top+snakeBody[i].y*SNAKE_CELL, SNAKE_CELL-1, SNAKE_CELL-1, 2, c);
  }
  s.fillRoundRect(snakeFoodX*SNAKE_CELL, top+snakeFoodY*SNAKE_CELL, SNAKE_CELL-1, SNAKE_CELL-1, 4, T().danger);

  char buf[24]; sprintf(buf,"Skor: %d",snakeScore);
  s.setTextColor(T().subtext); s.setTextSize(1); s.setCursor(6,STATUS_H+2); s.print(buf);

  if(snakeOver){
    int bx=SCR_W/2-72, by=SCR_H/2-32, bw=144, bh=64;
    s.fillRoundRect(bx,by,bw,bh,10,T().surface);
    s.drawRoundRect(bx,by,bw,bh,10,T().danger);
    s.setTextColor(T().danger); s.setTextSize(1); s.setCursor(bx+30,by+14); s.print("GAME OVER");
    s.setTextColor(T().subtext); s.setCursor(bx+10,by+34); s.print("Ketuk layar utk ulang");
  }
  drawBack(s);
}

void snakeTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  if(snakeOver){ snakeReset(); return; }
  SnakeSeg head=snakeBody[0];
  int hx = head.x*SNAKE_CELL+SNAKE_CELL/2;
  int hy = STATUS_H+head.y*SNAKE_CELL+SNAKE_CELL/2;
  int dx=x-hx, dy=y-hy;
  int nx=snakeDirX, ny=snakeDirY;
  if(abs(dx)>abs(dy)){ nx = dx>0?1:-1; ny=0; } else { ny = dy>0?1:-1; nx=0; }
  if(!(nx==-snakeDirX && ny==-snakeDirY)){ snakeDirX=nx; snakeDirY=ny; } // gak boleh langsung balik 180 derajat
}

// =============================================
// GAME: FLAPPY BLOCK (ketuk layar utk terbang, hindari pipa)
// =============================================
#define FLAP_GRAVITY 0.35f
#define FLAP_IMPULSE -5.4f
#define FLAP_GAP     72
#define FLAP_PIPE_W  26
#define FLAP_BIRD_X  50
#define FLAP_BIRD_R  8
#define FLAP_PIPE_COUNT 3

struct FlapPipe{ int x, gapY; bool passed; };
FlapPipe flapPipes[FLAP_PIPE_COUNT];
float flapY, flapVel;
int   flapScore;
bool  flapOver, flapStarted;

void flapReset(){
  flapY = SCR_H/2; flapVel=0;
  flapScore=0; flapOver=false; flapStarted=false;
  for(int i=0;i<FLAP_PIPE_COUNT;i++){
    flapPipes[i].x = SCR_W + 60 + i*140;
    flapPipes[i].gapY = random(STATUS_H+60, SCR_H-60);
    flapPipes[i].passed=false;
  }
}
void flapEnter(){ enterGameMode(); flapReset(); }
void flapExit(){ exitGameMode(); }

void flapTick(){
  if(!flapStarted || flapOver) return;
  flapVel += FLAP_GRAVITY;
  flapY += flapVel;
  if(flapY > SCR_H-6 || flapY < STATUS_H+6){ flapOver=true; return; }
  for(int i=0;i<FLAP_PIPE_COUNT;i++){
    flapPipes[i].x -= 3;
    if(flapPipes[i].x < -FLAP_PIPE_W){
      flapPipes[i].x = SCR_W+10;
      flapPipes[i].gapY = random(STATUS_H+60, SCR_H-60);
      flapPipes[i].passed=false;
    }
    bool xOverlap = (FLAP_BIRD_X+FLAP_BIRD_R > flapPipes[i].x) && (FLAP_BIRD_X-FLAP_BIRD_R < flapPipes[i].x+FLAP_PIPE_W);
    if(xOverlap){
      if(flapY-FLAP_BIRD_R < flapPipes[i].gapY-FLAP_GAP/2 || flapY+FLAP_BIRD_R > flapPipes[i].gapY+FLAP_GAP/2){
        flapOver=true;
      }
    }
    if(!flapPipes[i].passed && flapPipes[i].x+FLAP_PIPE_W < FLAP_BIRD_X){
      flapPipes[i].passed=true; flapScore++;
    }
  }
}

void drawFlap(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  flapTick();
  for(int i=0;i<FLAP_PIPE_COUNT;i++){
    int gy=flapPipes[i].gapY;
    s.fillRoundRect(flapPipes[i].x, STATUS_H, FLAP_PIPE_W, max(0,gy-FLAP_GAP/2-STATUS_H), 4, T().good);
    s.fillRoundRect(flapPipes[i].x, gy+FLAP_GAP/2, FLAP_PIPE_W, max(0,SCR_H-(gy+FLAP_GAP/2)), 4, T().good);
  }
  s.fillCircle(FLAP_BIRD_X, (int)flapY, FLAP_BIRD_R, T().accent);

  char buf[8]; sprintf(buf,"%d",flapScore);
  s.setTextColor(T().text); s.setTextSize(2); s.setCursor(SCR_W/2-6,STATUS_H+6); s.print(buf);

  if(!flapStarted){
    const char* h="Ketuk layar utk mulai";
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(SCR_W/2-(int)strlen(h)*3,SCR_H/2+40); s.print(h);
  }
  if(flapOver){
    int bx=SCR_W/2-72, by=SCR_H/2-32, bw=144, bh=64;
    s.fillRoundRect(bx,by,bw,bh,10,T().surface);
    s.drawRoundRect(bx,by,bw,bh,10,T().danger);
    s.setTextColor(T().danger); s.setTextSize(1); s.setCursor(bx+30,by+14); s.print("GAME OVER");
    s.setTextColor(T().subtext); s.setCursor(bx+10,by+34); s.print("Ketuk layar utk ulang");
  }
  drawBack(s);
}

void flapTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  if(flapOver){ flapReset(); return; }
  flapStarted=true;
  flapVel = FLAP_IMPULSE;
}

// =============================================
// GAME: 2048 (swipe layar ke 4 arah)
// =============================================
#define G2048_CELL 52
#define G2048_GAP  6
int  g2048Board[4][4];
int  g2048Score;
bool g2048Over, g2048Win;
int  g2048DragStartX, g2048DragStartY;
bool g2048Dragging;

void g2048AddRandom(){
  int emptyR[16], emptyC[16], n=0;
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) if(g2048Board[r][c]==0){ emptyR[n]=r; emptyC[n]=c; n++; }
  if(n==0) return;
  int pick=random(0,n);
  g2048Board[emptyR[pick]][emptyC[pick]] = (random(0,10)<9)?2:4;
}
void g2048Reset(){
  memset(g2048Board,0,sizeof(g2048Board));
  g2048Score=0; g2048Over=false; g2048Win=false; g2048Dragging=false;
  g2048AddRandom(); g2048AddRandom();
}
void g2048Enter(){ enterGameMode(); g2048Reset(); }
void g2048Exit(){ exitGameMode(); }

bool g2048CompressMergeLeft(){
  bool moved=false;
  for(int r=0;r<4;r++){
    int line[4], n=0;
    for(int c=0;c<4;c++) if(g2048Board[r][c]!=0) line[n++]=g2048Board[r][c];
    for(int i=0;i<n-1;i++){
      if(line[i]==line[i+1]){
        line[i]*=2; g2048Score+=line[i];
        if(line[i]==2048) g2048Win=true;
        for(int k=i+1;k<n-1;k++) line[k]=line[k+1];
        n--;
      }
    }
    for(int c=0;c<4;c++){
      int v = c<n?line[c]:0;
      if(g2048Board[r][c]!=v) moved=true;
      g2048Board[r][c]=v;
    }
  }
  return moved;
}
void g2048RotateCW(){
  int tmp[4][4];
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) tmp[c][3-r]=g2048Board[r][c];
  memcpy(g2048Board,tmp,sizeof(tmp));
}
bool g2048BoardFull(){
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) if(g2048Board[r][c]==0) return false;
  return true;
}
bool g2048HasMove(){
  if(!g2048BoardFull()) return true;
  for(int r=0;r<4;r++) for(int c=0;c<4;c++){
    int v=g2048Board[r][c];
    if(c<3 && g2048Board[r][c+1]==v) return true;
    if(r<3 && g2048Board[r+1][c]==v) return true;
  }
  return false;
}
// dir: 0=kiri, 1=atas, 2=kanan, 3=bawah
void g2048Move(int dir){
  if(g2048Over) return;
  for(int i=0;i<dir;i++) g2048RotateCW();
  bool moved = g2048CompressMergeLeft();
  for(int i=0;i<(4-dir)%4;i++) g2048RotateCW();
  if(moved){
    g2048AddRandom();
    if(!g2048HasMove()) g2048Over=true;
  }
}

void draw2048(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  char buf[24]; sprintf(buf,"Skor: %d",g2048Score);
  s.setTextColor(T().subtext); s.setTextSize(1); s.setCursor(6,STATUS_H+2); s.print(buf);

  int boardW = 4*G2048_CELL+5*G2048_GAP;
  int gx = SCR_W/2-boardW/2, gy = STATUS_H+22;
  s.fillRoundRect(gx,gy,boardW,boardW,8,T().surface);
  for(int r=0;r<4;r++){
    for(int c=0;c<4;c++){
      int v=g2048Board[r][c];
      int cx=gx+G2048_GAP+c*(G2048_CELL+G2048_GAP);
      int cy=gy+G2048_GAP+r*(G2048_CELL+G2048_GAP);
      uint16_t cc = (v==0)?T().surface2 : (v<=4?T().good : v<=32?T().accent : v<=256?T().accent2 : T().danger);
      s.fillRoundRect(cx,cy,G2048_CELL,G2048_CELL,6,cc);
      if(v>0){
        char vb[6]; sprintf(vb,"%d",v);
        int vw=strlen(vb)*6*(v<100?2:1);
        s.setTextColor(v<=4?T().bg:0xFFFF); s.setTextSize(v<100?2:1);
        s.setCursor(cx+G2048_CELL/2-vw/2, cy+G2048_CELL/2-8);
        s.print(vb);
      }
    }
  }

  if(g2048Over || g2048Win){
    int bx=SCR_W/2-72, by=SCR_H/2-32, bw=144, bh=64;
    s.fillRoundRect(bx,by,bw,bh,10,T().surface);
    s.drawRoundRect(bx,by,bw,bh,10,g2048Win?T().good:T().danger);
    s.setTextColor(g2048Win?T().good:T().danger); s.setTextSize(1);
    s.setCursor(bx+30,by+14); s.print(g2048Win?"KAMU MENANG!":"GAME OVER");
    s.setTextColor(T().subtext); s.setCursor(bx+10,by+34); s.print("Ketuk layar utk ulang");
  }
  drawBack(s);
}

void g2048Touch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)){ navBack(); return; }
  if(g2048Over || g2048Win){ if(isNew) g2048Reset(); return; }
  if(isNew){ g2048DragStartX=x; g2048DragStartY=y; g2048Dragging=true; return; }
  if(held && g2048Dragging){
    int dx=x-g2048DragStartX, dy=y-g2048DragStartY;
    int adx=abs(dx), ady=abs(dy);
    if(max(adx,ady) > 34){ // ambang batas swipe
      if(adx>ady) g2048Move(dx>0?2:0); else g2048Move(dy>0?3:1);
      g2048Dragging=false; // 1 gesture = 1 langkah, cegah geser panjang jadi banyak langkah
      needRedraw=true;
    }
  }
}

// =============================================
// GAME: TIC-TAC-TOE (vs komputer - AI sederhana: menang>blok>tengah>acak)
// =============================================
char tttBoard[9]; // ' '=kosong, 'X'=user, 'O'=komputer
bool tttOver; char tttWinner; // 'X','O','D'(seri)

void tttReset(){
  for(int i=0;i<9;i++) tttBoard[i]=' ';
  tttOver=false; tttWinner=' ';
}
void tttEnter(){ tttReset(); } // game santai, gak perlu Game Mode (gak butuh performa tinggi)
void tttExit(){}

char tttCheckWin(){
  const int L[8][3]={{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
  for(int i=0;i<8;i++){
    char a=tttBoard[L[i][0]],b=tttBoard[L[i][1]],c=tttBoard[L[i][2]];
    if(a!=' ' && a==b && b==c) return a;
  }
  bool full=true; for(int i=0;i<9;i++) if(tttBoard[i]==' ') full=false;
  if(full) return 'D';
  return ' ';
}
void tttAiMove(){
  // 1) kalau AI bisa menang langsung, ambil
  // 2) kalau user bisa menang giliran depan, blok
  // 3) ambil tengah kalau kosong
  // 4) acak dari sisa kotak kosong
  const int L[8][3]={{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
  for(int pass=0; pass<2; pass++){
    char me = pass==0? 'O':'X';
    for(int i=0;i<8;i++){
      int a=L[i][0],b=L[i][1],c=L[i][2];
      char va=tttBoard[a],vb=tttBoard[b],vc=tttBoard[c];
      if(va==me&&vb==me&&vc==' '){ tttBoard[c]='O'; return; }
      if(va==me&&vc==me&&vb==' '){ tttBoard[b]='O'; return; }
      if(vb==me&&vc==me&&va==' '){ tttBoard[a]='O'; return; }
    }
  }
  if(tttBoard[4]==' '){ tttBoard[4]='O'; return; }
  int empty[9],n=0;
  for(int i=0;i<9;i++) if(tttBoard[i]==' ') empty[n++]=i;
  if(n>0) tttBoard[empty[random(0,n)]]='O';
}

void drawTtt(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  s.setTextColor(T().accent); s.setTextSize(1); s.setCursor(8,STATUS_H+4); s.print("Tic-Tac-Toe vs CPU");

  int cellW = min(SCR_W-40, SCR_H-STATUS_H-100)/3;
  int gx=SCR_W/2-cellW*3/2, gy=STATUS_H+30;
  for(int i=0;i<9;i++){
    int r=i/3, c=i%3;
    int cx=gx+c*cellW, cy=gy+r*cellW;
    s.fillRoundRect(cx+2,cy+2,cellW-4,cellW-4,6,T().surface);
    if(tttBoard[i]=='X'){
      s.setTextColor(T().accent); s.setTextSize(3);
      s.setCursor(cx+cellW/2-9,cy+cellW/2-12); s.print("X");
    } else if(tttBoard[i]=='O'){
      s.setTextColor(T().danger); s.setTextSize(3);
      s.setCursor(cx+cellW/2-9,cy+cellW/2-12); s.print("O");
    }
  }

  if(tttOver){
    const char* msg = tttWinner=='X' ? "Kamu Menang!" : tttWinner=='O' ? "CPU Menang!" : "Seri!";
    int bx=SCR_W/2-72, by=gy+cellW*3+14, bw=144, bh=44;
    s.fillRoundRect(bx,by,bw,bh,10,T().surface);
    s.drawRoundRect(bx,by,bw,bh,10,T().accent);
    s.setTextColor(T().text); s.setTextSize(1);
    s.setCursor(SCR_W/2-(int)strlen(msg)*3,by+10); s.print(msg);
    s.setTextColor(T().subtext); s.setCursor(SCR_W/2-48,by+26); s.print("Ketuk utk main lagi");
  }
  drawBack(s);
}

void tttTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  if(tttOver){ tttReset(); needRedraw=true; return; }

  int cellW = min(SCR_W-40, SCR_H-STATUS_H-100)/3;
  int gx=SCR_W/2-cellW*3/2, gy=STATUS_H+30;
  if(x<gx || x>=gx+cellW*3 || y<gy || y>=gy+cellW*3) return;
  int c=(x-gx)/cellW, r=(y-gy)/cellW;
  int idx=r*3+c;
  if(tttBoard[idx]!=' ') return;

  tttBoard[idx]='X';
  char w = tttCheckWin();
  if(w!=' '){ tttOver=true; tttWinner=w; needRedraw=true; return; }
  tttAiMove();
  w = tttCheckWin();
  if(w!=' '){ tttOver=true; tttWinner=w; }
  needRedraw=true;
}

// =============================================
// GAME: BREAKOUT (paddle dikontrol KEMIRINGAN HP via MPU6050, fallback drag jari)
// =============================================
#define BRK_COLS 8
#define BRK_ROWS 4
#define BRK_PADW 44
#define BRK_PADH 8
#define BRK_BALLR 4
bool  brkBricks[BRK_ROWS][BRK_COLS];
float brkPadX;
float brkBallX, brkBallY, brkVelX, brkVelY;
int   brkScore; bool brkOver, brkWin, brkStarted;
int   brkBrickW, brkBrickH, brkTop;

void brkReset(){
  for(int r=0;r<BRK_ROWS;r++) for(int c=0;c<BRK_COLS;c++) brkBricks[r][c]=true;
  brkBrickW = SCR_W/BRK_COLS;
  brkBrickH = 14;
  brkTop = STATUS_H+16;
  brkPadX = SCR_W/2;
  brkBallX = SCR_W/2; brkBallY = SCR_H-40;
  brkVelX = 0; brkVelY = 0;
  brkScore=0; brkOver=false; brkWin=false; brkStarted=false;
}
void brkEnter(){ enterGameMode(); brkReset(); }
void brkExit(){ exitGameMode(); }

void brkTick(){
  if(!brkStarted || brkOver || brkWin) return;

  // Kontrol paddle: kalau MPU6050 kedeteksi, ikutin kemiringan kiri/kanan (smoothRoll).
  // Kalau sensor gak ada, paddle ngikutin posisi X jari terakhir yg nyentuh layar (fallback).
  if(mpuReady){
    float target = SCR_W/2 + constrain(smoothRoll,-35.0f,35.0f)*3.2f;
    brkPadX += (target-brkPadX)*0.25f;
  }
  brkPadX = constrain(brkPadX, BRK_PADW/2.0f, SCR_W-BRK_PADW/2.0f);

  brkBallX += brkVelX; brkBallY += brkVelY;
  if(brkBallX<BRK_BALLR || brkBallX>SCR_W-BRK_BALLR) brkVelX=-brkVelX;
  if(brkBallY<brkTop+BRK_BALLR) brkVelY=-brkVelY;
  if(brkBallY > SCR_H+20){ brkOver=true; return; }

  int padY = SCR_H-24;
  if(brkBallY+BRK_BALLR >= padY && brkBallY < padY+BRK_PADH &&
     brkBallX > brkPadX-BRK_PADW/2 && brkBallX < brkPadX+BRK_PADW/2 && brkVelY>0){
    brkVelY=-fabsf(brkVelY);
    float hit = (brkBallX-brkPadX)/(BRK_PADW/2.0f); // -1..1, posisi kena di paddle
    brkVelX = hit*3.2f;
  }

  for(int r=0;r<BRK_ROWS;r++){
    for(int c=0;c<BRK_COLS;c++){
      if(!brkBricks[r][c]) continue;
      int bx=c*brkBrickW, by=brkTop+r*brkBrickH;
      if(brkBallX>bx && brkBallX<bx+brkBrickW && brkBallY-BRK_BALLR<by+brkBrickH && brkBallY+BRK_BALLR>by){
        brkBricks[r][c]=false;
        brkVelY=-brkVelY;
        brkScore+=10;
        goto brickHit;
      }
    }
  }
  brickHit:;

  bool anyLeft=false;
  for(int r=0;r<BRK_ROWS;r++) for(int c=0;c<BRK_COLS;c++) if(brkBricks[r][c]) anyLeft=true;
  if(!anyLeft) brkWin=true;
}

void drawBrk(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  brkTick();

  for(int r=0;r<BRK_ROWS;r++){
    for(int c=0;c<BRK_COLS;c++){
      if(!brkBricks[r][c]) continue;
      uint16_t cc = (r==0)?T().danger:(r==1)?T().accent:(r==2)?T().accent2:T().good;
      s.fillRect(c*brkBrickW+1, brkTop+r*brkBrickH+1, brkBrickW-2, brkBrickH-2, cc);
    }
  }
  int padY=SCR_H-24;
  s.fillRoundRect((int)(brkPadX-BRK_PADW/2), padY, BRK_PADW, BRK_PADH, 3, T().accent);
  s.fillCircle((int)brkBallX,(int)brkBallY,BRK_BALLR,T().text);

  char buf[24]; sprintf(buf,"Skor: %d",brkScore);
  s.setTextColor(T().subtext); s.setTextSize(1); s.setCursor(6,STATUS_H+2); s.print(buf);

  if(!mpuReady){
    s.setTextColor(T().subtext);
    s.setCursor(SCR_W-98,STATUS_H+2); s.print("(drag = geser)");
  }

  if(!brkStarted){
    const char* h="Ketuk layar utk mulai";
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(SCR_W/2-(int)strlen(h)*3,SCR_H/2+40); s.print(h);
  }
  if(brkOver || brkWin){
    int bx=SCR_W/2-72, by=SCR_H/2-32, bw=144, bh=64;
    s.fillRoundRect(bx,by,bw,bh,10,T().surface);
    s.drawRoundRect(bx,by,bw,bh,10,brkWin?T().good:T().danger);
    s.setTextColor(brkWin?T().good:T().danger); s.setTextSize(1);
    s.setCursor(bx+30,by+14); s.print(brkWin?"MENANG!":"GAME OVER");
    s.setTextColor(T().subtext); s.setCursor(bx+10,by+34); s.print("Ketuk layar utk ulang");
  }
  drawBack(s);
}

void brkTouch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)){ navBack(); return; }
  if(brkOver || brkWin){ if(isNew) brkReset(); return; }
  if(!brkStarted){
    if(isNew){ brkStarted=true; brkVelX=1.6f; brkVelY=-3.2f; }
    return;
  }
  // Fallback drag-kontrol kalau MPU6050 gak kedeteksi
  if(!mpuReady && (held || isNew)) brkPadX = x;
}

// =============================================
// APP: JAM
// =============================================
void clockEnter(){} void clockExit(){}
void drawClock(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,28);s.print("Jam");
  struct tm t;bool ok=ntpSynced&&getLocalTime(&t);
  if(ok){
    char tb[9];sprintf(tb,"%02d:%02d:%02d",t.tm_hour,t.tm_min,t.tm_sec);
    s.setTextColor(T().text);s.setTextSize(3);s.setCursor(20,55);s.print(tb);
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28];sprintf(db,"%s, %02d %s %04d",days[t.tm_wday],t.tm_mday,mons[t.tm_mon],t.tm_year+1900);
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(20,100);s.print(db);
    s.drawFastHLine(20,114,SCR_W-40,T().accent);
    s.setTextColor(T().good);s.setCursor(20,120);s.print("NTP Sync OK");
  } else {
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);s.print("Tidak sync");
  }
  drawBack(s);drawToast(s);
}
void clockTouch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)) navBack();
}

// =============================================
// APP: KALKULATOR
// =============================================
String calcInput="0";float calcA=0;char calcOp=0;bool calcNew=true;
const char* calcLabels[5][4] = {
  {"C","+/-","%","/"},
  {"7","8","9","x"},
  {"4","5","6","-"},
  {"1","2","3","+"},
  {"0",".","=","="}
};
void calcEnter(){} void calcExit(){}

void calcBtnRect(int r,int c,int& x,int& y,int& w,int& h){
  int top = STATUS_H + 34;
  int bottom = backY() - 6;
  int gap = 4;
  int gridW = SCR_W - 8;
  int gridH = bottom - top;
  int rows=5, cols=4;
  int cw = (gridW-(cols-1)*gap)/cols;
  int ch = (gridH-(rows-1)*gap)/rows;
  y = top + r*(ch+gap);
  h = ch;
  if(r==4 && c==0){ x=4; w=cw*2+gap; return; }
  if(r==4 && c==2){ x=4+2*(cw+gap); w=cw*2+gap; return; }
  if(r==4 && (c==1||c==3)) { w=0; return; }
  x = 4 + c*(cw+gap);
  w = cw;
}

void drawCalc(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,25);s.print("Kalkulator");
  s.fillRoundRect(4,STATUS_H+8,SCR_W-8,22,4,T().surface);
  s.setTextColor(T().text);s.setTextSize(2);
  int tw2=calcInput.length()*12;
  s.setCursor(max(8,SCR_W-12-tw2),STATUS_H+11);s.print(calcInput);
  for(int r=0;r<5;r++){
    for(int c=0;c<4;c++){
      int x,y,w,h; calcBtnRect(r,c,x,y,w,h);
      if(w==0) continue;
      const char* l=calcLabels[r][c];
      bool isOp = (!strcmp(l,"/")||!strcmp(l,"x")||!strcmp(l,"-")||!strcmp(l,"+"));
      bool isEq = (r==4&&c==2);
      uint16_t bg = isEq?T().accent2:(isOp?T().accent:T().surface);
      uint16_t fg = (isOp||isEq)?T().bg:T().text;
      s.fillRoundRect(x,y,w,h,6,bg);
      s.setTextColor(fg);s.setTextSize(2);
      int lw=strlen(l)*12;s.setCursor(x+w/2-lw/2,y+h/2-8);s.print(l);
    }
  }
  drawBack(s);
  drawToast(s);
}

void calcApplyLabel(const char* l){
  if(!strcmp(l,"C")){calcInput="0";calcA=0;calcOp=0;calcNew=true;}
  else if(!strcmp(l,"+/-"))calcInput=String(calcInput.toFloat()*-1);
  else if(!strcmp(l,"%"))calcInput=String(calcInput.toFloat()/100);
  else if(!strcmp(l,"=")){
    float b2=calcInput.toFloat(),res=0;
    if(calcOp=='+')res=calcA+b2;else if(calcOp=='-')res=calcA-b2;
    else if(calcOp=='x')res=calcA*b2;
    else if(calcOp=='/')res=b2?calcA/b2:0;
    calcInput=(res==(int)res)?String((int)res):String(res,4);
    calcOp=0;calcNew=true;
  }
  else if(!strcmp(l,"+")||!strcmp(l,"-")||!strcmp(l,"x")||!strcmp(l,"/")){
    calcA=calcInput.toFloat();calcOp=l[0];calcNew=true;
  } else {
    if(calcNew){calcInput="";calcNew=false;}
    if(!strcmp(l,".")&&calcInput.indexOf('.')>=0)return;
    if(calcInput=="0"&&strcmp(l,".")!=0)calcInput="";
    calcInput+=l;
  }
  if(calcInput.length()>12)calcInput=calcInput.substring(0,12);
}

void calcTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  for(int r=0;r<5;r++){
    for(int c=0;c<4;c++){
      int bx,by,bw,bh; calcBtnRect(r,c,bx,by,bw,bh);
      if(bw==0) continue;
      if(x>=bx&&x<=bx+bw&&y>=by&&y<=by+bh){ 
        calcApplyLabel(calcLabels[r][c]); 
        needRedraw=true; 
        return; 
      }
    }
  }
}

// =============================================
// APP: ORIENTASI 3D (visualisasi real-time dari MPU6050)
// =============================================
Vec3f rot3f(Vec3f v, float rxRad, float ryRad){
  // rotasi sumbu X (pitch: tunduk/dongak) lalu sumbu Y (roll: miring kanan/kiri)
  float y1 = v.y*cosf(rxRad) - v.z*sinf(rxRad);
  float z1 = v.y*sinf(rxRad) + v.z*cosf(rxRad);
  float x1 = v.x;
  float x2 =  x1*cosf(ryRad) + z1*sinf(ryRad);
  float z2 = -x1*sinf(ryRad) + z1*cosf(ryRad);
  return { x2, y1, z2 };
}

// Gambar kotak wireframe 3D yg merepresentasikan bodi HP, berputar sesuai
// rollDeg (miring kanan/kiri) & pitchDeg (nunduk/dongak). scale=1.0 -> ukuran dasar.
void draw3DPhoneBox(LGFX_Sprite& s,int cx,int cy,float rollDeg,float pitchDeg,float scale){
  float rx = pitchDeg * (PI/180.0f);
  float ry = rollDeg  * (PI/180.0f);
  float w=32*scale, h=56*scale, d=6*scale;
  Vec3f local[8] = {
    {-w,-h,-d},{w,-h,-d},{w,h,-d},{-w,h,-d}, // sisi belakang (0-3)
    {-w,-h, d},{w,-h, d},{w,h, d},{-w,h, d}  // sisi depan / "layar" (4-7)
  };
  float px[8], py[8];
  for(int i=0;i<8;i++){
    Vec3f r = rot3f(local[i], rx, ry);
    float persp = 240.0f/(240.0f+r.z);
    px[i] = cx + r.x*persp;
    py[i] = cy - r.y*persp;
  }
  // isi wajah "layar" (depan) dulu sblm gambar rangka, biar berasa solid
  s.fillTriangle((int)px[4],(int)py[4],(int)px[5],(int)py[5],(int)px[6],(int)py[6], 0x10A2);
  s.fillTriangle((int)px[4],(int)py[4],(int)px[6],(int)py[6],(int)px[7],(int)py[7], 0x10A2);

  static const int edges[12][2] = {
    {0,1},{1,2},{2,3},{3,0},   // rusuk belakang
    {4,5},{5,6},{6,7},{7,4},   // rusuk depan (layar)
    {0,4},{1,5},{2,6},{3,7}    // rusuk penghubung
  };
  for(int i=0;i<12;i++){
    s.drawLine((int)px[edges[i][0]],(int)py[edges[i][0]],(int)px[edges[i][1]],(int)py[edges[i][1]], T().accent);
  }
  for(int i=0;i<8;i++) s.fillCircle((int)px[i],(int)py[i],2, T().accent2);
}

void sensorEnter(){} void sensorExit(){}

void drawSensor(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent2);s.setTextSize(1);s.setCursor(8,26);s.print("Orientasi 3D");

  if(!mpuReady){
    int bx=16, by=52, bw=SCR_W-32, bh=SCR_H-52-40;
    s.fillRoundRect(bx,by,bw,bh,10,T().surface);
    s.setTextColor(T().danger);s.setTextSize(1);
    s.setCursor(bx+10,by+12);s.print("MPU6050 tidak terdeteksi");
    s.setTextColor(T().subtext);
    s.setCursor(bx+10,by+32);s.print("Cek wiring:");
    s.setCursor(bx+10,by+46);s.print("SDA -> GPIO 15");
    s.setCursor(bx+10,by+58);s.print("SCL -> GPIO 7");
    s.setCursor(bx+10,by+70);s.print("VCC -> 3V3, GND -> GND");
    s.fillRoundRect(bx+10,by+bh-30,bw-20,22,6,T().accent);
    s.setTextColor(T().bg);s.setCursor(bx+bw/2-30,by+bh-24);s.print("Coba Lagi");
    drawBack(s); drawToast(s);
    return;
  }

  // Layout adaptif: bagian atas utk box 3D, bagian bawah utk angka2 sensor,
  // dihitung dari tinggi layar yg tersedia biar muat di portrait & landscape.
  int top = STATUS_H+4;
  int bottom = backY()-4;
  int avail = bottom-top;
  int boxAreaH = (int)(avail*0.60f);
  int cx = SCR_W/2;
  int cy = top + boxAreaH/2 + 6;
  float boxScale = constrain(boxAreaH/170.0f, 0.5f, 1.15f);

  draw3DPhoneBox(s, cx, cy, smoothRoll, smoothPitch, boxScale);

  int ty = top + boxAreaH + 8;
  char buf[48];
  s.setTextColor(T().text); s.setTextSize(1);
  sprintf(buf,"Roll : %+.1f deg", smoothRoll);
  s.setCursor(14, ty); s.print(buf);
  sprintf(buf,"Pitch: %+.1f deg", smoothPitch);
  s.setCursor(14, ty+12); s.print(buf);

  s.setTextColor(T().subtext);
  sprintf(buf,"Accel(g): X%.2f Y%.2f Z%.2f", mpuAx, mpuAy, mpuAz);
  s.setCursor(14, ty+27); s.print(buf);
  sprintf(buf,"Gyro(dps): X%.0f Y%.0f Z%.0f", mpuGx, mpuGy, mpuGz);
  s.setCursor(14, ty+39); s.print(buf);
  sprintf(buf,"Suhu MPU: %.1fC  Chip: %.1fC", mpuTempC, temperatureRead());
  s.setCursor(14, ty+51); s.print(buf);

  drawBack(s); drawToast(s);
}

void sensorTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  if(!mpuReady){
    int bx=16, by=52, bw=SCR_W-32, bh=SCR_H-52-40;
    if(x>=bx+10 && x<=bx+10+(bw-20) && y>=by+bh-30 && y<=by+bh-8){
      mpuInit();
      showToast(mpuReady?"MPU6050 terhubung!":"Masih gagal, cek wiring");
      needRedraw=true;
    }
  }
}

// =============================================
// APP: SETTINGS
// =============================================
String settSSID="",settPass="";
bool settShowPass=false;int settFocus=-1;

void settingsEnter(){ 
  settSSID=String(WIFI_SSID); 
  settPass=String(WIFI_PASSWORD); 
  startWifiScan();
}
void settingsExit(){}

void drawSettings(LGFX_Sprite& s){
  checkWifiScanComplete();

  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,26);s.print("Pengaturan");

  // Toggle Auto-Rotate (di sebelah kiri tombol Pindai)
  int arW=62;
  int arX = SCR_W-74-arW-6;
  s.fillRoundRect(arX,24,arW,18,4, autoRotateEnabled?T().good:T().surface2);
  s.setTextColor(autoRotateEnabled?T().bg:T().subtext);
  s.setCursor(arX+4,29);
  s.print(autoRotateEnabled?"Rot:ON":"Rot:OFF");

  s.fillRoundRect(SCR_W-74,24,66,18,4,wifiScanning?T().surface2:T().accent);
  s.setTextColor(wifiScanning?T().subtext:T().bg);
  s.setCursor(SCR_W-68,29);
  s.print(wifiScanning?"Memindai":"Pindai");

  int rowY = STATUS_H+12;
  int rowW = SCR_W-16;
  
  s.fillRoundRect(8,rowY,rowW,26,6,T().surface);
  s.setTextColor(T().text);s.setCursor(14,rowY+8);s.print("Cerah:");
  s.fillRoundRect(58,rowY+9,rowW-90,7,3,T().divider);
  s.fillRoundRect(59,rowY+10,map(brightness,0,255,0,rowW-92),5,2,T().accent);
  char bb[6];sprintf(bb,"%d%%",brightness*100/255);
  s.setTextColor(T().subtext);s.setCursor(rowW-28,rowY+8);s.print(bb);
  
  rowY+=28;
  s.fillRoundRect(8,rowY,rowW,26,6,T().surface);
  s.setTextColor(T().text);s.setCursor(14,rowY+8);s.print("Tema:");
  int tbtnW=(rowW-50)/THEME_COUNT;
  for(int i=0;i<THEME_COUNT;i++){
    uint16_t bg=(i==themeIdx)?T().accent:T().surface2;
    uint16_t fg=(i==themeIdx)?T().bg:T().text;
    int tx=52+i*tbtnW;
    s.fillRoundRect(tx,rowY+2,tbtnW-3,22,4,bg);
    s.setTextColor(fg);s.setTextSize(1);
    int nl=strlen(themes[i].name)*6;
    s.setCursor(tx+(tbtnW-3)/2-nl/2,rowY+7);s.print(themes[i].name);
  }
  
  rowY+=28;
  s.fillRoundRect(8,rowY,rowW,36,6,T().surface);
  s.setTextColor(T().subtext);s.setCursor(14,rowY+4);
  s.print("Daftar WiFi (ketuk utk pilih):");

  if(wifiScanning){
    s.setTextColor(T().accent);s.setCursor(14,rowY+18);
    s.print("Memindai jaringan sekitar...");
  } else if(scannedWifiNum==0){
    s.setTextColor(T().danger);s.setCursor(14,rowY+18);
    s.print("Tidak ada WiFi. Ketuk [Pindai]");
  } else {
    int wifiPillW=(rowW-12)/3;
    for(int i=0;i<3 && i<scannedWifiNum;i++){
      int wx=12+i*(wifiPillW+4);
      bool isSelected = (settSSID == scannedWifis[i].ssid);
      uint16_t pBg = isSelected ? T().accent : T().surface2;
      uint16_t pFg = isSelected ? T().bg : T().text;
      s.fillRoundRect(wx,rowY+16,wifiPillW-2,16,4,pBg);
      s.setTextColor(pFg);s.setCursor(wx+4,rowY+20);
      String dispSSID = scannedWifis[i].ssid;
      if(dispSSID.length()>8) dispSSID = dispSSID.substring(0,7)+".";
      s.print(dispSSID.c_str());
    }
  }

  rowY+=38;
  s.fillRoundRect(8,rowY,rowW,22,4,settFocus==0?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(14,rowY+6);s.print("SSID:");
  s.setTextColor(T().text);s.setCursor(54,rowY+6);
  String sd2=settSSID.length()?settSSID:"(ketuk / pilih di atas)";
  if(sd2.length()>22)sd2=sd2.substring(0,22)+"..";
  s.print(sd2.c_str());
  
  rowY+=24;
  s.fillRoundRect(8,rowY,rowW-28,22,4,settFocus==1?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(14,rowY+6);s.print("Pass:");
  s.setTextColor(T().text);s.setCursor(54,rowY+6);
  if(settPass.length()){
    if(settShowPass)s.print(settPass.substring(0,18).c_str());
    else for(int i=0;i<(int)settPass.length()&&i<18;i++)s.print("*");
  } else s.print("(ketuk)");
  s.fillRoundRect(8+rowW-24,rowY,24,22,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(8+rowW-18,rowY+6);s.print(settShowPass?"H":"S");
  
  rowY+=26;
  int btnW=(rowW-8)/2;
  s.fillRoundRect(8,rowY,btnW,24,6,T().accent);
  s.setTextColor(T().bg);s.setCursor(16,rowY+7);s.print("Sambungkan");
  s.fillRoundRect(SCR_W/2+4,rowY,btnW,24,6,T().surface2);
  s.setTextColor(T().text);s.setCursor(SCR_W/2+10,rowY+7);s.print("Kalibrasi Ulang");

  // ---- BARU: Kalibrasi sensor gerak MPU6050 ----
  rowY+=28;
  s.fillRoundRect(8,rowY,rowW,24,6, mpuReady?T().accent2:T().surface2);
  s.setTextColor(mpuReady?T().bg:T().subtext);s.setTextSize(1);
  s.setCursor(14,rowY+8);
  s.print(mpuReady?"Kalibrasi Sensor Gerak (MPU6050)":"MPU6050 tidak terdeteksi");

  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}

void settingsTouch(int x,int y,bool held,bool isNew){
  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){kbVisible=false;kbTarget=nullptr;settFocus=-1;}
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(isBack(x,y)){ navBack(); return; }

  int arW=62;
  int arX = SCR_W-74-arW-6;
  if(x>=arX && x<=arX+arW && y>=24 && y<=42){
    autoRotateEnabled = !autoRotateEnabled;
    saveAutoRotatePref();
    showToast(autoRotateEnabled?"Auto-rotate aktif":"Auto-rotate mati");
    needRedraw=true;
    return;
  }

  if(x>=SCR_W-74 && x<=SCR_W-8 && y>=24 && y<=42){
    if(!wifiScanning){ startWifiScan(); showToast("Memindai..."); }
    return;
  }

  int rowW = SCR_W-16;
  int rowY = STATUS_H+12;

  if(x>=58&&x<=58+rowW-90&&y>=rowY&&y<=rowY+26){
    brightness=constrain(map(x-58,0,rowW-90,10,255),10,255);
    display.setBrightness(brightness);
    needRedraw=true;
    return;
  }

  rowY+=28;
  int tbtnW=(rowW-50)/THEME_COUNT;
  if(y>=rowY&&y<=rowY+26){
    for(int i=0;i<THEME_COUNT;i++){
      int tx=52+i*tbtnW;
      if(x>=tx&&x<=tx+tbtnW-3){
        themeIdx=i;saveTheme();initAppColors();
        showToast("Tema diganti");needRedraw=true;return;
      }
    }
  }

  rowY+=28;
  if(y>=rowY+14 && y<=rowY+34 && !wifiScanning && scannedWifiNum>0){
    int wifiPillW=(rowW-12)/3;
    for(int i=0;i<3 && i<scannedWifiNum;i++){
      int wx=12+i*(wifiPillW+4);
      if(x>=wx && x<=wx+wifiPillW-2){
        settSSID = scannedWifis[i].ssid;
        settPass = "";
        settFocus = 1; kbTarget = &settPass; kbVisible = true; kbMode = KB_LOWER;
        showToast(settSSID.c_str());
        needRedraw=true;
        return;
      }
    }
  }

  rowY+=38;
  if(y>=rowY&&y<=rowY+22){ 
    settFocus=0;kbTarget=&settSSID;kbVisible=true;kbMode=KB_LOWER;
    needRedraw=true;return; 
  }

  rowY+=24;
  if(y>=rowY&&y<=rowY+22){
    if(x>=8+rowW-24){ settShowPass=!settShowPass; needRedraw=true; return; }
    settFocus=1;kbTarget=&settPass;kbVisible=true;kbMode=KB_LOWER;needRedraw=true;return;
  }

  rowY+=26;
  int btnW=(rowW-8)/2;
  if(y>=rowY&&y<=rowY+24){
    if(x>=8&&x<=8+btnW){
      settSSID.toCharArray(WIFI_SSID,64);
      settPass.toCharArray(WIFI_PASSWORD,64);
      saveWifiCreds();connectWifi();
      showToast(wifiConnected?"WiFi Terhubung!":"Gagal Connect");
      needRedraw=true;
      return;
    }
    if(x>=SCR_W/2+4 && x<=SCR_W/2+4+btnW){
      Preferences p;p.begin("touch_cal",false);p.putBool("done",false);p.end();
      ESP.restart();
      return;
    }
  }

  // ---- BARU: tombol Kalibrasi Sensor Gerak (MPU6050) ----
  rowY+=28;
  if(y>=rowY&&y<=rowY+24){
    if(mpuReady){
      calibrateMPU();
      needRedraw=true;
    } else {
      showToast("Sensor MPU6050 tidak terdeteksi");
    }
    return;
  }
}

// =============================================
// APP: NOTEPAD
// =============================================
bool   notepadConfirmActive = false;
int    notepadConfirmAction = 0;   // 1 = mau Back, 2 = mau Home
String notepadSavedText = "";      // snapshot isi terakhir yg tersimpan di SD

void notepadEnter(){
  notepadSavedText = noteText;
  notepadConfirmActive = false;
  notepadConfirmAction = 0;
}
void notepadExit(){ saveNote(); }

// Dipanggil dari navBack()/navGoHome(): true kalau ada perubahan yg
// belum disimpan, supaya navigasi ditahan & dialog Simpan/Buang muncul.
bool notepadNeedsConfirm(){
  return curScreen()==SCR_NOTEPAD && !notepadConfirmActive && noteText != notepadSavedText;
}
void notepadRequestConfirm(int act){
  notepadConfirmAction = act;
  notepadConfirmActive = true;
  kbVisible = false; kbTarget = nullptr;
  needRedraw = true;
}

void drawNotepad(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,26);s.print("Notepad");
  
  s.fillRoundRect(SCR_W-70,24,62,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-56,29);s.print("Hapus");
  
  int bot = kbVisible ? kbY()-4 : backY()-6;
  s.fillRoundRect(4,44,SCR_W-8,bot-44,6,T().surface);
  s.setTextColor(T().text);s.setTextSize(1);s.setTextWrap(true);
  s.setCursor(10,50);s.print((noteText+"|").c_str());
  
  if(kbVisible)drawKb(s);else drawBack(s);

  // ---- Kotak peringatan Simpan/Buang saat keluar dg perubahan blm tersimpan ----
  if(notepadConfirmActive){
    s.fillRect(0,0,SCR_W,SCR_H,0x0000); // dim overlay
    int dw = min(SCR_W-40, 220), dh=104;
    int dx=(SCR_W-dw)/2, dy=(SCR_H-dh)/2;
    s.fillRoundRect(dx,dy,dw,dh,8,T().surface);
    s.drawRoundRect(dx,dy,dw,dh,8,T().divider);
    s.setTextColor(T().text); s.setTextSize(1);
    s.setCursor(dx+12,dy+14); s.print("Simpan perubahan");
    s.setCursor(dx+12,dy+28); s.print("catatan ini?");
    int bw=(dw-24)/2, bh=30, by=dy+dh-40;
    s.fillRoundRect(dx+8,by,bw,bh,6,T().good);
    s.setTextColor(0xFFFF); s.setCursor(dx+8+bw/2-24,by+bh/2-4); s.print("Simpan");
    s.fillRoundRect(dx+16+bw,by,bw,bh,6,T().danger);
    s.setCursor(dx+16+bw+bw/2-20,by+bh/2-4); s.print("Buang");
  }

  drawToast(s);
}

void notepadTouch(int x,int y,bool held,bool isNew){
  if(notepadConfirmActive){
    if(!isNew) return;
    int dw = min(SCR_W-40, 220), dh=104;
    int dx=(SCR_W-dw)/2, dy=(SCR_H-dh)/2;
    int bw=(dw-24)/2, bh=30, by=dy+dh-40;
    if(y>=by && y<=by+bh){
      int act = notepadConfirmAction;
      if(x>=dx+8 && x<=dx+8+bw){                 // Simpan
        saveNote(); notepadSavedText = noteText;
      } else if(x>=dx+16+bw && x<=dx+16+bw+bw){   // Buang
        noteText = notepadSavedText;
      } else return;
      notepadConfirmActive = false;
      notepadConfirmAction = 0;
      if(act==2) navGoHome(); else navBack();
    }
    return; // tahan semua sentuhan lain selagi dialog terbuka
  }

  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){kbVisible=false;kbTarget=nullptr;}
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(x>=SCR_W-70&&x<=SCR_W-4&&y>=24&&y<=42){
    noteText="";saveNote();notepadSavedText="";showToast("Dihapus");
    needRedraw=true;
    return;
  }
  
  if(isBack(x,y)){ navBack(); return; }
  
  int textBot = backY()-6;
  if(y>=44&&y<=textBot){
    kbTarget=&noteText;
    kbVisible=true;
    kbMode=KB_LOWER;
    needRedraw=true;
  }
}

// =============================================
// APP: CANVAS
// =============================================
uint16_t palette[]={0x0000,0xF800,0x07E0,0x001F,0xFFE0,0x07FF,0xF81F,0xFD40,0xFFFF};
#define PAL_N 9
uint16_t drawColor=0xFFFF;
int brushSize=3;
int lastDX=-1,lastDY=-1;

int canvasCapY(){ return STATUS_H+2; }
int canvasToolY(){ return SCR_H-44; }

void canvasEnter(){ lastDX=-1; }
void canvasExit(){ saveCanvas(); }

// FIX v11: dulu goresan tebal digambar dgn drawLine() digeser-geser per
// pixel ketebalan (garis utama + garis offset) - hasilnya jelas kelihatan
// bertangga/kotak-kotak, apalagi kalau brush besar atau jari digerakkan
// diagonal/cepat. Sekarang goresan "distempel" pakai fillCircle yg
// diinterpolasi sepanjang jalur gerakan jari (jarak antar stempel
// disesuaikan ukuran brush), jadi goresannya mulus & tanpa celah walau
// jari digerakkan cepat.
void canvasStampStroke(int x0,int y0,int x1,int y1,int size,uint16_t color){
  int rad = max(1, size/2);
  float dx=(float)(x1-x0), dy=(float)(y1-y0);
  float dist=sqrtf(dx*dx+dy*dy);
  int step = max(1, rad/2);            // jarak antar stempel - makin kecil brush, makin rapat
  int steps = max(1, (int)(dist/step));
  for(int i=0;i<=steps;i++){
    float t=(float)i/steps;
    int px=x0+(int)(dx*t+0.5f), py=y0+(int)(dy*t+0.5f);
    canvasApp.fillCircle(px,py,rad,color);
  }
}

void drawCanvasScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,14);s.print("Canvas");
  
  canvasApp.pushSprite(&s,0,canvasCapY());
  
  int ty=canvasToolY();
  s.fillRect(0,ty,SCR_W,SCR_H-ty,T().surface);
  s.drawFastHLine(0,ty,SCR_W,T().divider);
  
  int palGap = (SCR_W-8)/PAL_N;
  for(int i=0;i<PAL_N;i++){
    int px=4+i*palGap;
    s.fillCircle(px+palGap/2,ty+10,7,palette[i]);
    if(palette[i]==drawColor) s.drawCircle(px+palGap/2,ty+10,9,T().text);
  }
  
  int r2Y = ty + 22;
  s.fillRoundRect(4,r2Y,56,18,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(10,r2Y+5);s.print("< Back");
  
  s.fillRoundRect(66,r2Y,60,18,4,T().surface2);
  s.setTextColor(T().text);char bs[8];sprintf(bs,"B:%d",brushSize);
  s.setCursor(76,r2Y+5);s.print(bs);
  
  s.fillRoundRect(SCR_W-64,r2Y,60,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-52,r2Y+5);s.print("CLR");
  
  drawToast(s);
}

void canvasTouch(int x,int y,bool held,bool isNew){
  int ty=canvasToolY();
  
  if(y>=ty){
    if(isNew){
      int r2Y = ty + 22;
      if(x>=4 && x<=60 && y>=r2Y){ navBack(); return; }
      
      if(y>=ty && y<r2Y){
        int palGap=(SCR_W-8)/PAL_N;
        for(int i=0;i<PAL_N;i++){
          int px=4+i*palGap;
          if(x>=px && x<px+palGap){ drawColor=palette[i]; lastDX=-1; needRedraw=true; return; }
        }
      }
      
      if(y>=r2Y){
        if(x>=66 && x<=126){ brushSize=(brushSize%8)+1; lastDX=-1; needRedraw=true; return; }
        if(x>=SCR_W-64 && x<=SCR_W-4){ canvasApp.fillSprite(T().bg); lastDX=-1; saveCanvas(); showToast("Dibersihkan"); needRedraw=true; return; }
      }
    }
    return;
  }
  
  int capY=canvasCapY();
  if(y>=capY && y<ty){
    int cy=y-capY;
    if(held&&lastDX>=0){
      canvasStampStroke(lastDX,lastDY,x,cy,brushSize,drawColor);
    } else {
      canvasApp.fillCircle(x,cy,max(1,brushSize/2),drawColor);
    }
    lastDX=x;lastDY=cy;
    needRedraw=true;
  }
}

// =============================================
// APP: AI CHAT
// =============================================
#define AI_CHAT_TOP 46
#define AI_MAX_LINES 400
AiLine aiLinesBuf[AI_MAX_LINES];

void aiEnter(){ aiRespScrollY = 0; }
void aiExit(){}

// Word-wrap manual (font default = ~6px/karakter) supaya kita tahu persis
// tinggi total konten -> bisa dibatasi scroll-nya & di-clip dgn benar.
int aiWrapAppend(AiLine* buf,int cap,int count,const String& text,uint16_t color,int maxChars){
  if(maxChars<4) maxChars=4;
  int start=0, len=text.length();
  if(len==0){ if(count<cap){ buf[count].text=""; buf[count].color=color; count++; } return count; }
  while(start<len && count<cap){
    int nl = text.indexOf('\n', start);
    int segEnd = (nl==-1)? len : nl;
    String seg = text.substring(start, segEnd);
    if(seg.length()==0){
      buf[count].text=""; buf[count].color=color; count++;
    } else {
      while((int)seg.length() > maxChars && count<cap){
        int cut = maxChars, sp = -1;
        for(int k=maxChars;k>0;k--){ if(seg[k]==' '){ sp=k; break; } }
        if(sp>0) cut=sp;
        buf[count].text = seg.substring(0,cut); buf[count].color=color; count++;
        seg = seg.substring(cut);
        while(seg.length() && seg[0]==' ') seg = seg.substring(1);
      }
      if(count<cap){ buf[count].text=seg; buf[count].color=color; count++; }
    }
    start = segEnd+1;
  }
  return count;
}

void drawAiChat(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextWrap(false);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,26);s.print("AI Chat (Gemini)");
  
  bool hasKey = (geminiApiKey.length() > 0 && geminiApiKey != "YOUR_GEMINI_API_KEY_HERE");
  s.fillRoundRect(SCR_W-72,24,68,18,4,hasKey?T().good:T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-66,29);
  s.print(hasKey?"Key: OK":"Key: Edit");

  s.fillRoundRect(SCR_W-118,24,40,18,4,T().surface2);
  s.setTextColor(T().text);s.setCursor(SCR_W-113,29);
  s.print("Mem");

  int inputY  = backY() - 26;
  int chatTop = AI_CHAT_TOP;
  int chatBot = kbVisible ? kbY()-4 : inputY-6;
  int chatH   = chatBot - chatTop;

  // ---- Satu area chat gabungan (Kamu + Gemini), dibungkus & discroll ----
  s.fillRoundRect(4,chatTop,SCR_W-8,chatH,6,T().surface);

  int lineH = 10;
  int innerX = 10, innerW = (SCR_W-8) - 12;
  int maxChars = innerW/6; if(maxChars<6) maxChars=6;

  int n=0;
  n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,"Kamu:",T().accent,maxChars);
  String qText = aiPrompt.length() ? aiPrompt : "(ketuk kotak input di bawah utk mengetik)";
  n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,qText,T().text,maxChars);
  n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,"",T().text,maxChars);
  n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,"Gemini:",T().accent2,maxChars);
  if(aiLoading){
    unsigned long elapsed = aiRequestStartMillis ? (millis()-aiRequestStartMillis) : 0;
    long remain = (long)(AI_HARD_TIMEOUT_MS - elapsed) / 1000;
    if(remain<0) remain=0;
    char buf[80];
    snprintf(buf,sizeof(buf),"Sedang berpikir & menghubungi Gemini API... (timeout %lds)",remain);
    n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,buf,T().good,maxChars);
  } else if(aiResponse.length()){
    n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,aiResponse,T().text,maxChars);
  } else {
    n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,"Ketik pertanyaan lalu tekan [Kirim].",T().subtext,maxChars);
  }

  int contentH = n*lineH;
  int viewH = chatH-8;
  aiRespMaxScroll = contentH-viewH; if(aiRespMaxScroll<0) aiRespMaxScroll=0;
  if(aiRespScrollY>aiRespMaxScroll) aiRespScrollY=aiRespMaxScroll;
  if(aiRespScrollY<0) aiRespScrollY=0;

  // FIX: pakai clip rect -> teks TIDAK PERNAH lagi meluber keluar kotak
  // (dulu ini yg bikin tulisan menutupi kotak pertanyaan/Kirim/Back).
  s.setClipRect(4,chatTop,SCR_W-8,chatH);
  s.setTextWrap(false);
  for(int i=0;i<n;i++){
    int ly = chatTop+4+i*lineH-(int)aiRespScrollY;
    if(ly+lineH < chatTop || ly > chatBot) continue;
    s.setTextColor(aiLinesBuf[i].color);
    s.setCursor(innerX,ly);
    s.print(aiLinesBuf[i].text.c_str());
  }
  s.clearClipRect();

  // Indikator scrollbar tipis di kanan, muncul kalau kontennya lebih panjang dari kotak
  if(aiRespMaxScroll>0){
    int trackX=SCR_W-8, trackY=chatTop+4, trackH=chatH-8;
    int thumbH = max(14, (int)((float)viewH/contentH*trackH));
    int thumbY = trackY + (int)((float)aiRespScrollY/aiRespMaxScroll*(trackH-thumbH));
    s.fillRoundRect(trackX,trackY,3,trackH,1,T().divider);
    s.fillRoundRect(trackX,thumbY,3,thumbH,1,T().accent);
  }

  if(!kbVisible){
    s.fillRoundRect(4,inputY,SCR_W-64,22,4,T().surface);
    s.setTextColor(T().subtext);s.setCursor(10,inputY+6);
    String ipDisp = aiPrompt.length() ? aiPrompt : "Ketik pertanyaan...";
    if((int)ipDisp.length()>maxChars) ipDisp = ipDisp.substring(0, maxChars>3?maxChars-3:maxChars) + "..";
    s.print(ipDisp.c_str());
    
    s.fillRoundRect(SCR_W-56,inputY,52,22,4,aiLoading?T().surface2:T().accent);
    s.setTextColor(aiLoading?T().subtext:T().bg);s.setCursor(SCR_W-48,inputY+6);
    s.print("Kirim");
  }

  if(kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

void aiTouch(int x,int y,bool held,bool isNew){
  static int dragLastY=0;

  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){ kbVisible=false; kbTarget=nullptr; }
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }

  int inputY  = backY() - 26;
  int chatTop = AI_CHAT_TOP;
  int chatBot = inputY-6;

  if(isNew){
    if(isBack(x,y)){ navBack(); return; }

    if(x>=SCR_W-72 && x<=SCR_W-4 && y>=24 && y<=42){
      kbTarget = &geminiApiKey;
      kbVisible = true;
      kbMode = KB_LOWER;
      showToast("Edit API Key");
      needRedraw = true;
      return;
    }

    if(x>=SCR_W-118 && x<=SCR_W-78 && y>=24 && y<=42){
      clearAiMemory();
      showToast("Memory percakapan dihapus");
      needRedraw = true;
      return;
    }

    // FIX: menyentuh area chat SEKARANG dipakai utk geser/scroll isi
    // (dulu tidak bisa discroll sama sekali).
    if(y>=chatTop && y<=chatBot){
      dragLastY = y;
      aiRespScrollVel = 0; // v12: reset sisa momentum di awal sentuhan baru (spt Home)
      return;
    }

    if(y>=inputY && y<=inputY+22 && x<=SCR_W-60){
      kbTarget = &aiPrompt;
      kbVisible = true;
      kbMode = KB_LOWER;
      needRedraw = true;
      return;
    }

    if(x>=SCR_W-56 && x<=SCR_W-4 && y>=inputY && y<=inputY+22){
      saveGeminiKey();
      triggerGeminiAI();
      return;
    }
    return;
  }

  // Frame lanjutan selagi jari masih ditahan -> geser scroll
  if(held && y>=chatTop-20 && y<=chatBot+20){
    int dy = dragLastY - y;
    if(dy!=0){
      // v12: kecepatan di-low-pass (bukan delta mentah) supaya sisa
      // momentumnya bisa dipakai utk scroll lanjutan yg mulus setelah jari
      // dilepas (lihat momentum di loop()) -- gaya yg sama dgn Home.
      aiRespScrollVel = aiRespScrollVel*0.5f + dy*0.5f;
      aiRespScrollY = constrain(aiRespScrollY + dy, 0.0f, (float)aiRespMaxScroll);
      dragLastY = y;
      needRedraw = true;
    }
  }
}

// =============================================
// APP: FILE EXPLORER (DENGAN WEB UPLOADER)
// =============================================
#define MAX_EXP_FILES 20
String expFileNames[MAX_EXP_FILES];
uint32_t expFileSizes[MAX_EXP_FILES];
int expFileCount = 0;
int expScrollPage = 0;
String expViewContent = "";
String expViewFileName = "";

// v12: BARU - isi file skrg bisa di-scroll (dulu teks panjang meluber
// keluar kotak & tak terbaca sama sekali). Dibungkus+di-wrap+di-clip persis
// spt pola scroll teks di AI Chat (reuse AiLine + aiWrapAppend), lengkap dgn
// momentum halus stlh jari dilepas.
#define EXP_MAX_LINES 400
AiLine expLinesBuf[EXP_MAX_LINES];
float  expViewScrollY = 0;
int    expViewMaxScroll = 0;
float  expViewScrollVel = 0;

void scanSdFiles() {
  expFileCount = 0;
  if (!sdReady) return;
  File root = SD_MMC.open("/");
  File f = root.openNextFile();
  while (f && expFileCount < MAX_EXP_FILES) {
    if (!f.isDirectory()) {
      String fname = String(f.name());
      if (!fname.startsWith("/")) fname = "/" + fname;
      expFileNames[expFileCount] = fname;
      expFileSizes[expFileCount] = f.size();
      expFileCount++;
    }
    f = root.openNextFile();
  }
}

void fileExpEnter(){
  expViewContent = "";
  expViewFileName = "";
  expScrollPage = 0;
  expViewScrollY = 0;    // v12
  expViewScrollVel = 0;  // v12
  scanSdFiles();
}
void fileExpExit(){}

void drawFileExplorer(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(0x3ADF);s.setTextSize(1);s.setCursor(8,26);s.print("File Explorer");

  if(wifiConnected && webServerRunning){
    s.setTextColor(T().good);s.setCursor(110,26);
    String ipStr = "Web: " + WiFi.localIP().toString();
    s.print(ipStr.c_str());
  } else {
    s.setTextColor(T().subtext);s.setCursor(120,26);
    s.print("Web: Off (No WiFi)");
  }

  if(expViewFileName.length() > 0){
    s.fillRoundRect(SCR_W-64,24,58,18,4,T().surface2);
    s.setTextColor(T().accent);s.setCursor(SCR_W-54,29);s.print("Tutup");

    int viewTop=44, viewBot=backY()-6, viewH=viewBot-viewTop;
    s.fillRoundRect(4,viewTop,SCR_W-8,viewH,6,T().surface);
    s.setTextColor(T().accent2);s.setCursor(10,viewTop+6);s.print("File: ");
    s.setTextColor(T().text);s.print(expViewFileName.c_str());
    s.drawFastHLine(10,viewTop+20,SCR_W-20,T().divider);

    // v12: isi file di-wrap ke expLinesBuf (persis pola AI Chat) supaya
    // tingginya pasti & bisa discroll+diclip -> teks panjang skrg bisa
    // dibaca semua dgn geser (drag), bukan meluber keluar kotak lagi.
    int contentTop=viewTop+26, contentBot=viewBot-6;
    int lineH=10;
    int innerX=10, innerW=(SCR_W-8)-12;
    int maxChars=innerW/6; if(maxChars<6) maxChars=6;
    int n = aiWrapAppend(expLinesBuf,EXP_MAX_LINES,0,expViewContent,T().text,maxChars);

    int contentH = n*lineH;
    int viewAreaH = contentBot-contentTop;
    expViewMaxScroll = contentH-viewAreaH; if(expViewMaxScroll<0) expViewMaxScroll=0;
    if(expViewScrollY>expViewMaxScroll) expViewScrollY=(float)expViewMaxScroll;
    if(expViewScrollY<0) expViewScrollY=0;

    s.setClipRect(4,contentTop,SCR_W-8,viewAreaH);
    s.setTextWrap(false);
    for(int i=0;i<n;i++){
      int ly = contentTop+i*lineH-(int)expViewScrollY;
      if(ly+lineH < contentTop || ly > contentBot) continue;
      s.setTextColor(expLinesBuf[i].color);
      s.setCursor(innerX,ly);
      s.print(expLinesBuf[i].text.c_str());
    }
    s.clearClipRect();

    // Scrollbar tipis di kanan, sama gaya spt di AI Chat
    if(expViewMaxScroll>0){
      int trackX=SCR_W-8, trackY=contentTop, trackH=viewAreaH;
      int thumbH = max(14, (int)((float)viewAreaH/contentH*trackH));
      int thumbY = trackY + (int)((float)expViewScrollY/expViewMaxScroll*(trackH-thumbH));
      s.fillRoundRect(trackX,trackY,3,trackH,1,T().divider);
      s.fillRoundRect(trackX,thumbY,3,thumbH,1,T().accent);
    }

    drawBack(s);drawToast(s);
    return;
  }

  int listY = 44;
  int itemH = 26;
  int itemsPerPage = 5;
  int startIdx = expScrollPage * itemsPerPage;

  if(!sdReady){
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);
    s.print("SD Card Tdk Siap");
    drawBack(s);drawToast(s);
    return;
  }

  if(expFileCount == 0){
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(20,80);
    s.print("SD Card Kosong / Belum ada file.");
  } else {
    for(int i = 0; i < itemsPerPage && (startIdx + i) < expFileCount; i++){
      int idx = startIdx + i;
      int itemY = listY + i * (itemH + 4);
      s.fillRoundRect(4,itemY,SCR_W-8,itemH,6,T().surface);
      
      s.setTextColor(T().accent2);s.setCursor(10,itemY+8);s.print("[F]");
      s.setTextColor(T().text);s.setCursor(32,itemY+8);
      String fn = expFileNames[idx];
      if(fn.length() > 16) fn = fn.substring(0,14) + "..";
      s.print(fn.c_str());

      s.fillRoundRect(SCR_W-84,itemY+3,36,20,4,T().surface2);
      s.setTextColor(T().good);s.setCursor(SCR_W-76,itemY+8);s.print("Buka");

      s.fillRoundRect(SCR_W-44,itemY+3,36,20,4,T().danger);
      s.setTextColor(0xFFFF);s.setCursor(SCR_W-38,itemY+8);s.print("Hps");
    }
  }

  int pageY = backY() - 2;
  if(expScrollPage > 0){
    s.fillRoundRect(SCR_W-120,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-110,pageY+6);s.print("< Prev");
  }
  if((expScrollPage + 1) * itemsPerPage < expFileCount){
    s.fillRoundRect(SCR_W-60,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-52,pageY+6);s.print("Next >");
  }

  drawBack(s);drawToast(s);
}

void fileExpTouch(int x,int y,bool held,bool isNew){
  static int dragLastY=0;
  int viewTop=44, viewBot=backY()-6; // v12: sama dgn area kotak isi file di drawFileExplorer

  if(isNew){
    if(isBack(x,y)){ navBack(); return; }

    if(expViewFileName.length() > 0){
      if(x>=SCR_W-64 && x<=SCR_W-4 && y>=24 && y<=42){
        expViewFileName = "";
        expViewContent = "";
        expViewScrollY = 0; expViewScrollVel = 0; // v12
        needRedraw = true;
        return;
      }
      // v12: sentuh area isi file -> mulai drag utk scroll (spt AI Chat)
      if(y>=viewTop && y<=viewBot){
        dragLastY = y;
        expViewScrollVel = 0; // reset sisa momentum di awal sentuhan baru
      }
      return;
    }

    if(!sdReady) return;

    int listY = 44;
    int itemH = 26;
    int itemsPerPage = 5;
    int startIdx = expScrollPage * itemsPerPage;

    for(int i = 0; i < itemsPerPage && (startIdx + i) < expFileCount; i++){
      int idx = startIdx + i;
      int itemY = listY + i * (itemH + 4);
      if(y >= itemY && y <= itemY + itemH){
        if(x >= SCR_W-84 && x <= SCR_W-48){
          expViewFileName = expFileNames[idx];
          File f = SD_MMC.open(expViewFileName, FILE_READ);
          if(f){ expViewContent = f.readString(); f.close(); }
          else expViewContent = "Gagal membaca isi file.";
          expViewScrollY = 0; expViewScrollVel = 0; // v12: mulai dari atas tiap buka file baru
          needRedraw = true;
          return;
        }
        if(x >= SCR_W-44 && x <= SCR_W-8){
          SD_MMC.remove(expFileNames[idx]);
          showToast("File Dihapus");
          scanSdFiles();
          needRedraw = true;
          return;
        }
      }
    }

    int pageY = backY() - 2;
    if(expScrollPage > 0 && x>=SCR_W-120 && x<=SCR_W-66 && y>=pageY){
      expScrollPage--; needRedraw = true; return;
    }
    if((expScrollPage + 1) * itemsPerPage < expFileCount && x>=SCR_W-60 && x<=SCR_W-6 && y>=pageY){
      expScrollPage++; needRedraw = true; return;
    }
    return;
  }

  // v12: Frame lanjutan selagi jari ditahan -> geser scroll isi file
  // (dulu file explorer tidak menangani frame held sama sekali).
  if(held && expViewFileName.length() > 0 && y>=viewTop-20 && y<=viewBot+20){
    int dy = dragLastY - y;
    if(dy!=0){
      expViewScrollVel = expViewScrollVel*0.5f + dy*0.5f; // low-pass, sama gaya dgn Home/AI Chat
      expViewScrollY = constrain(expViewScrollY + dy, 0.0f, (float)expViewMaxScroll);
      dragLastY = y;
      needRedraw = true;
    }
  }
}

// =============================================
// APP: MJPEG PLAYER (FIXED v7)
// =============================================
const char* MJPEG_FOLDER = "/mjpeg";
#define MJPEG_MAX_FILES 30
String  mjpegFileList[MJPEG_MAX_FILES];
uint32_t mjpegFileSizes[MJPEG_MAX_FILES];
int mjpegFileCount   = 0;
int mjpegScrollPage  = 0;
int mjpegSelectedIdx = -1;
volatile bool mjpegPlaying       = false;
volatile bool mjpegStopRequested = false;

MjpegClass mjpeg;
uint8_t* mjpegBuf = nullptr;
size_t   mjpegBufSize = 0;
unsigned long mjpegTotalFrames = 0;
unsigned long mjpegStartMs = 0;

// Scan folder /mjpeg di SD Card, ambil semua file *.mjpeg
void mjpegScanFiles(){
  mjpegFileCount = 0;
  if(!sdReady) return;

  if(!SD_MMC.exists(MJPEG_FOLDER)){
    SD_MMC.mkdir(MJPEG_FOLDER);
    return;
  }

  File dir = SD_MMC.open(MJPEG_FOLDER);
  if(!dir || !dir.isDirectory()){ if(dir) dir.close(); return; }

  File f = dir.openNextFile();
  while(f && mjpegFileCount < MJPEG_MAX_FILES){
    if(!f.isDirectory()){
      String name = String(f.name());
      int slashIdx = name.lastIndexOf('/');
      if(slashIdx >= 0) name = name.substring(slashIdx+1);
      String lower = name; lower.toLowerCase();
      if(lower.endsWith(".mjpeg")){
        mjpegFileList[mjpegFileCount]  = name;
        mjpegFileSizes[mjpegFileCount] = f.size();
        mjpegFileCount++;
      }
    }
    f = dir.openNextFile();
  }
  dir.close();
  Serial.printf("[MJPEG] %d file ditemukan di %s\n", mjpegFileCount, MJPEG_FOLDER);
}

// Callback dipanggil JPEGDEC setiap kali 1 baris/blok JPEG selesai didecode.
// Digambar LANGSUNG ke hardware `display` (bukan canvas sprite) demi speed.
int mjpegDrawCallback(JPEGDRAW *pDraw){
  display.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight,
                     (lgfx::swap565_t*)pDraw->pPixels);
  return 1;
}

// FIX v7: Alokasi buffer decode MJPEG DIPERBESAR (dari ~30KB jadi ~150KB).
// Ini adalah penyebab utama video "dipotong" sebelum selesai: kalau ada 1
// frame JPEG saja yang ukurannya lebih besar dari buffer, readMjpegBuf()
// gagal membaca frame itu dan mengembalikan false — padahal file MJPEG
// belum habis — sehingga loop pemutaran berhenti dan dikira "sudah selesai".
// Buffer besar ditaruh di PSRAM (bukan DRAM internal) supaya tidak
// membebani RAM internal yang juga dipakai TLS/HTTPClient utk AI Chat.
bool mjpegEnsureBuffer(){
  if(mjpegBuf) return true;

  // Ukuran buffer yang jauh lebih longgar per frame JPEG 320x240.
  // Dicoba dari yang paling besar dulu, turun bertahap kalau alokasi gagal,
  // supaya tetap dapat buffer sebesar mungkin sesuai PSRAM yang tersedia.
  const size_t candidateSizes[] = {
    (size_t)320 * 240 * 2,        // ~153.6 KB (default baru, 5x lebih besar dari v6)
    (size_t)320 * 240,            // ~76.8 KB
    (size_t)320 * 240 * 2 / 3,    // ~51.2 KB
  };

  for (size_t sz : candidateSizes) {
    mjpegBuf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mjpegBuf) { mjpegBufSize = sz; break; }
  }
  // Fallback terakhir: DRAM internal, ukuran kecil spy tidak bikin heap penuh
  if (!mjpegBuf) {
    mjpegBufSize = (size_t)320 * 240 * 2 / 5;
    mjpegBuf = (uint8_t*)heap_caps_malloc(mjpegBufSize, MALLOC_CAP_8BIT);
  }
  if (!mjpegBuf) {
    Serial.println("[MJPEG] Gagal alokasi buffer decode!");
    return false;
  }
  Serial.printf("[MJPEG] Buffer decode dialokasikan: %u bytes\n", (unsigned)mjpegBufSize);
  return true;
}

// Putar 1 file mjpeg secara blocking. Ketuk layar kapan saja utk berhenti.
void mjpegPlayFile(const String& filename){
  if(!mjpegEnsureBuffer()){
    showToast("RAM tidak cukup utk MJPEG");
    return;
  }

  String fullPath = String(MJPEG_FOLDER) + "/" + filename;
  File mjFile = SD_MMC.open(fullPath, FILE_READ);
  if(!mjFile || mjFile.isDirectory()){
    if(mjFile) mjFile.close();
    showToast("Gagal membuka file MJPEG");
    return;
  }

  // Layar hint sebelum mulai
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10,10);
  display.print("Memutar: "); display.println(filename);
  display.setCursor(10,26);
  display.print("Ketuk layar utk berhenti...");
  delay(600);
  display.fillScreen(TFT_BLACK);

  mjpegPlaying       = true;
  mjpegStopRequested = false;
  mjpegTotalFrames   = 0;
  mjpegStartMs       = millis();

  mjpeg.setup(&mjFile, mjpegBuf, mjpegDrawCallback, true /* useBigEndian */,
              0 /* x */, 0 /* y */, display.width(), display.height());

  int frameCheckCounter = 0;

  // FIX v7: kembali menggabungkan mjFile.available() DAN mjpeg.readMjpegBuf()
  // sebagai syarat loop — persis pola yang sudah terbukti memutar video
  // sampai benar-benar tuntas di kode referensi milik user (logic player-nya
  // yang diambil di sini). Dikombinasikan dengan buffer decode yang jauh
  // lebih besar (lihat mjpegEnsureBuffer di atas), frame besar tidak lagi
  // gagal terbaca sehingga tidak ada lagi frame/akhir video yang "kepotong".
  while(!mjpegStopRequested && mjFile.available() && mjpeg.readMjpegBuf()){
    mjpeg.drawJpg();
    mjpegTotalFrames++;

    // Cek sentuhan setiap beberapa frame supaya tidak terlalu membebani SPI
    if(++frameCheckCounter >= 2){
      frameCheckCounter = 0;
      lgfx::touch_point_t tp;
      if(display.getTouch(&tp)){
        mjpegStopRequested = true;
      }
    }

    // Tetap layani web server & jaga watchdog selama playback panjang
    if(wifiConnected && webServerRunning) webServer.handleClient();
    yield();
  }

  mjFile.close();
  mjpegPlaying       = false;
  mjpegStopRequested = false;

  unsigned long timeUsed = millis() - mjpegStartMs;
  float fps = timeUsed > 0 ? (1000.0f * mjpegTotalFrames / timeUsed) : 0;
  Serial.printf("[MJPEG] Selesai '%s' | frame=%lu | waktu=%lums | fps=%.1f\n",
                filename.c_str(), mjpegTotalFrames, timeUsed, fps);

  // Layar sudah kotor karena digambar langsung ke hardware,
  // paksa UI "OS" digambar ulang lewat canvas sprite.
  needRedrawNow();
}

void mjpegEnter(){
  mjpegScanFiles();
  mjpegScrollPage = 0;
}
void mjpegExit(){}

void drawMjpegPlayer(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(0xFBE0);s.setTextSize(1);s.setCursor(8,26);s.print("MJPEG Player");

  s.fillRoundRect(SCR_W-70,24,64,18,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(SCR_W-62,29);s.print("Scan");

  int listY = 44;
  int itemH = 26;
  int itemsPerPage = 5;
  int startIdx = mjpegScrollPage * itemsPerPage;

  if(!sdReady){
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);
    s.print("SD Card Tdk Siap");
  } else if(mjpegFileCount == 0){
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(14,80);
    s.print("Tidak ada file .mjpeg di /mjpeg");
    s.setCursor(14,96);
    s.print("Taruh file *.mjpeg di folder itu,");
    s.setCursor(14,108);
    s.print("lalu ketuk [Scan].");
  } else {
    for(int i=0; i<itemsPerPage && (startIdx+i)<mjpegFileCount; i++){
      int idx = startIdx+i;
      int itemY = listY + i*(itemH+4);
      s.fillRoundRect(4,itemY,SCR_W-8,itemH,6,T().surface);

      s.setTextColor(0xFBE0);s.setCursor(10,itemY+8);s.print("[V]");
      s.setTextColor(T().text);s.setCursor(32,itemY+8);
      String fn = mjpegFileList[idx];
      if(fn.length() > 15) fn = fn.substring(0,13) + "..";
      s.print(fn.c_str());

      s.fillRoundRect(SCR_W-64,itemY+3,56,20,4,T().good);
      s.setTextColor(T().bg);s.setCursor(SCR_W-56,itemY+8);s.print("Play");
    }
  }

  int pageY = backY() - 2;
  if(mjpegScrollPage > 0){
    s.fillRoundRect(SCR_W-120,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-110,pageY+6);s.print("< Prev");
  }
  if((mjpegScrollPage+1)*itemsPerPage < mjpegFileCount){
    s.fillRoundRect(SCR_W-60,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-52,pageY+6);s.print("Next >");
  }

  drawBack(s);
  drawToast(s);
}

void mjpegTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }

  if(x>=SCR_W-70 && x<=SCR_W-6 && y>=24 && y<=42){
    mjpegScanFiles();
    showToast("Scan selesai");
    needRedraw = true;
    return;
  }

  if(!sdReady) return;

  int listY = 44;
  int itemH = 26;
  int itemsPerPage = 5;
  int startIdx = mjpegScrollPage * itemsPerPage;

  for(int i=0;i<itemsPerPage && (startIdx+i)<mjpegFileCount;i++){
    int idx = startIdx+i;
    int itemY = listY + i*(itemH+4);
    if(y>=itemY && y<=itemY+itemH){
      if(x>=SCR_W-64 && x<=SCR_W-8){
        mjpegSelectedIdx = idx;
        mjpegPlayFile(mjpegFileList[idx]);   // blocking, berhenti saat disentuh
        return;
      }
    }
  }

  int pageY = backY() - 2;
  if(mjpegScrollPage>0 && x>=SCR_W-120 && x<=SCR_W-66 && y>=pageY){
    mjpegScrollPage--; needRedraw=true; return;
  }
  if((mjpegScrollPage+1)*itemsPerPage < mjpegFileCount && x>=SCR_W-60 && x<=SCR_W-6 && y>=pageY){
    mjpegScrollPage++; needRedraw=true; return;
  }
}

// =============================================
// APP: UPDATE FIRMWARE (OTA) — LOKAL (SD) & WIFI (URL)
// =============================================
const char* UPDATE_FOLDER = "/update";
#define UPDATE_MAX_FILES 15
String   updFileList[UPDATE_MAX_FILES];
uint32_t updFileSizes[UPDATE_MAX_FILES];
int updFileCount  = 0;
int updScrollPage = 0;

String updUrl = "";
volatile bool updBusy = false;

String updPendingConfirm = "";
unsigned long updPendingUntil = 0;
bool updCheckConfirm(const String& action){
  unsigned long now = millis();
  if(updPendingConfirm == action && now <= updPendingUntil){
    updPendingConfirm = "";
    return true;
  }
  updPendingConfirm = action;
  updPendingUntil   = now + 4000;
  showToast("Ketuk sekali lagi utk konfirmasi!");
  needRedraw = true;
  return false;
}

#define UPD_ROW0         (STATUS_H+12)
#define UPD_ITEM_Y        (UPD_ROW0+14)
#define UPD_ITEM_H        22
#define UPD_ITEM_GAP      4
#define UPD_ITEMS_PAGE    2
#define UPD_NAV_Y        (UPD_ITEM_Y + UPD_ITEMS_PAGE*(UPD_ITEM_H+UPD_ITEM_GAP))
#define UPD_NAV_H         18
#define UPD_DIV_Y        (UPD_NAV_Y + UPD_NAV_H + 8)
#define UPD_WIFI_LABEL_Y (UPD_DIV_Y + 8)
#define UPD_URL_Y        (UPD_WIFI_LABEL_Y + 14)
#define UPD_URL_H         22
#define UPD_BTN_Y        (UPD_URL_Y + 26)
#define UPD_BTN_H         24

void updScanFiles(){
  updFileCount = 0;
  if(!sdReady) return;
  if(!SD_MMC.exists(UPDATE_FOLDER)){ SD_MMC.mkdir(UPDATE_FOLDER); return; }
  File dir = SD_MMC.open(UPDATE_FOLDER);
  if(!dir || !dir.isDirectory()){ if(dir) dir.close(); return; }
  File f = dir.openNextFile();
  while(f && updFileCount < UPDATE_MAX_FILES){
    if(!f.isDirectory()){
      String name = String(f.name());
      int slashIdx = name.lastIndexOf('/');
      if(slashIdx >= 0) name = name.substring(slashIdx+1);
      String lower = name; lower.toLowerCase();
      if(lower.endsWith(".bin")){
        updFileList[updFileCount]  = name;
        updFileSizes[updFileCount] = f.size();
        updFileCount++;
      }
    }
    f = dir.openNextFile();
  }
  dir.close();
  Serial.printf("[Update] %d file .bin ditemukan di %s\n", updFileCount, UPDATE_FOLDER);
}

void updDrawProgress(const char* title, size_t written, size_t total){
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10,10);
  display.print(title);
  display.setCursor(10,26);
  display.setTextColor(TFT_RED);
  display.print("JANGAN MATIKAN PERANGKAT!");

  int barW=SCR_W-20, barH=18, barY=50;
  int pct = (total>0) ? (int)(((uint32_t)written*100)/total) : 0;
  display.setTextColor(TFT_WHITE);
  display.drawRect(10,barY,barW,barH,TFT_WHITE);
  int fillW = (barW-4)*pct/100;
  if(fillW>0) display.fillRect(12,barY+2,fillW,barH-4,TFT_GREEN);

  char buf[32];
  sprintf(buf,"%d%%  (%u/%u KB)", pct, (unsigned)(written/1024), (unsigned)(total/1024));
  display.setCursor(10,barY+barH+10);
  display.print(buf);
}

// reason: alasan gagal yang ditampilkan LANGSUNG di layar (bukan cuma di Serial),
// supaya kelihatan penyebab pastinya walau dipakai tanpa PC/serial monitor.
void updShowResult(bool success, const String& reason){
  updBusy = false;
  display.fillScreen(TFT_BLACK);
  display.setTextSize(2);
  display.setCursor(20,90);
  if(success){
    display.setTextColor(TFT_GREEN);
    display.print("Update Sukses!");
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE);
    display.setCursor(20,120);
    display.print("Perangkat restart otomatis...");
    delay(1800);
    ESP.restart();
  } else {
    display.setTextColor(TFT_RED);
    display.print("Update Gagal");
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE);
    display.setCursor(20,120);
    display.setTextWrap(true);
    display.print(reason.length() ? reason : "Cek file/koneksi, coba lagi.");
    display.setTextWrap(false);
    delay(3000);
    needRedrawNow();
  }
}

bool updInstallFromSD(const String& filename){
  String fullPath = String(UPDATE_FOLDER) + "/" + filename;
  File f = SD_MMC.open(fullPath, FILE_READ);
  if(!f || f.isDirectory()){
    if(f) f.close();
    showToast("Gagal membuka file .bin");
    return false;
  }
  size_t fileSize = f.size();
  if(fileSize == 0){
    f.close();
    showToast("File .bin kosong/rusak");
    return false;
  }

  updBusy = true;
  updDrawProgress("Update Lokal (SD Card)...", 0, fileSize);

  if(!Update.begin(fileSize, U_FLASH)){
    f.close();
    String why = Update.errorString();
    Serial.printf("[Update] begin() gagal: %s (fileSize=%u)\n", why.c_str(), (unsigned)fileSize);
    updShowResult(false, "begin(): " + why + " (file " + String((unsigned)(fileSize/1024)) + "KB, cek muat di partisi OTA)");
    return false;
  }

  uint8_t buf[4096];
  size_t written = 0;
  while(f.available()){
    size_t n = f.read(buf, sizeof(buf));
    if(n == 0) break;
    if(Update.write(buf, n) != n){
      Update.abort();
      f.close();
      String why = Update.errorString();
      Serial.printf("[Update] write() gagal: %s\n", why.c_str());
      updShowResult(false, "write(): " + why);
      return false;
    }
    written += n;
    updDrawProgress("Update Lokal (SD Card)...", written, fileSize);
    if(wifiConnected && webServerRunning) webServer.handleClient();
    yield();
  }
  f.close();

  if(!Update.end(true) || !Update.isFinished()){
    String why = Update.errorString();
    Serial.printf("[Update] end() gagal: %s\n", why.c_str());
    updShowResult(false, "end(): " + why + " - file .bin mungkin korup/tidak lengkap");
    return false;
  }
  updShowResult(true, "");
  return true;
}

bool updInstallFromURL(const String& url){
  if(WiFi.status() != WL_CONNECTED){
    showToast("WiFi belum terhubung!");
    return false;
  }
  if(url.length() < 8){
    showToast("URL tidak valid");
    return false;
  }

  updBusy = true;
  updDrawProgress("Menghubungi server...", 0, 0);

  bool isHttps = url.startsWith("https://");
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;

  bool beginOk;
  if(isHttps){
    secureClient.setInsecure();
    secureClient.setTimeout(15000);
    beginOk = http.begin(secureClient, url);
  } else {
    beginOk = http.begin(plainClient, url);
  }
  if(!beginOk){
    showToast("Gagal inisialisasi koneksi");
    updShowResult(false, "Gagal inisialisasi koneksi HTTP(S)");
    return false;
  }
  http.setTimeout(15000);
  http.setConnectTimeout(15000);

  int httpCode = http.GET();
  if(httpCode != HTTP_CODE_OK){
    Serial.printf("[Update] GET gagal, httpCode=%d\n", httpCode);
    http.end();
    showToast(("Gagal unduh, HTTP " + String(httpCode)).c_str());
    updShowResult(false, "HTTP GET gagal, kode: " + String(httpCode));
    return false;
  }

  int contentLen = http.getSize();
  if(contentLen <= 0){
    http.end();
    showToast("Ukuran file tidak diketahui server");
    updShowResult(false, "Server tidak mengirim Content-Length");
    return false;
  }

  if(!Update.begin((size_t)contentLen, U_FLASH)){
    http.end();
    String why = Update.errorString();
    Serial.printf("[Update] begin() gagal: %s\n", why.c_str());
    updShowResult(false, "begin(): " + why + " (file " + String(contentLen/1024) + "KB, cek muat di partisi OTA)");
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  unsigned long lastData = millis();

  while(http.connected() && written < (size_t)contentLen){
    size_t avail = stream->available();
    if(avail){
      size_t toRead = (avail < sizeof(buf)) ? avail : sizeof(buf);
      int n = stream->readBytes(buf, toRead);
      if(n > 0){
        if(Update.write(buf, n) != (size_t)n){
          Update.abort();
          http.end();
          String why = Update.errorString();
          Serial.printf("[Update] write() gagal: %s\n", why.c_str());
          updShowResult(false, "write(): " + why);
          return false;
        }
        written += n;
        lastData = millis();
        updDrawProgress("Update via WiFi...", written, contentLen);
      }
    } else {
      if(millis() - lastData > 15000){
        Update.abort();
        http.end();
        showToast("Timeout saat unduh firmware");
        updShowResult(false, "Timeout, tidak ada data > 15 detik");
        return false;
      }
      delay(2);
    }
    yield();
  }
  http.end();

  if(!Update.end(true) || !Update.isFinished()){
    String why = Update.errorString();
    Serial.printf("[Update] end() gagal: %s\n", why.c_str());
    updShowResult(false, "end(): " + why + " - file mungkin korup/tidak lengkap");
    return false;
  }
  updShowResult(true, "");
  return true;
}

void updEnter(){
  updScanFiles();
  updScrollPage = 0;
  updPendingConfirm = "";
}
void updExit(){}

void drawUpdate(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  s.setTextColor(0xF800); s.setTextSize(1); s.setCursor(8,26); s.print("Update Firmware");

  s.fillRoundRect(SCR_W-64,24,58,18,4,T().surface2);
  s.setTextColor(T().accent); s.setCursor(SCR_W-58,29); s.print("Scan");

  s.setTextColor(T().subtext); s.setCursor(8,UPD_ROW0); s.print("Lokal - file .bin di /update:");

  if(!sdReady){
    s.setTextColor(T().danger); s.setCursor(8,UPD_ITEM_Y+4); s.print("SD Card tidak siap");
  } else if(updFileCount == 0){
    s.setTextColor(T().subtext); s.setCursor(8,UPD_ITEM_Y+4); s.print("Tidak ada .bin. Taruh di /update");
  } else {
    int startIdx = updScrollPage*UPD_ITEMS_PAGE;
    for(int i=0;i<UPD_ITEMS_PAGE && (startIdx+i)<updFileCount;i++){
      int idx = startIdx+i;
      int itemY = UPD_ITEM_Y + i*(UPD_ITEM_H+UPD_ITEM_GAP);
      s.fillRoundRect(4,itemY,SCR_W-8,UPD_ITEM_H,5,T().surface);
      s.setTextColor(T().text); s.setCursor(8,itemY+6);
      String fn = updFileList[idx];
      String fnDisp = fn;
      if(fnDisp.length()>14) fnDisp = fnDisp.substring(0,12)+"..";
      s.print(fnDisp.c_str());

      bool pendingThis = (updPendingConfirm == ("sd:"+fn)) && millis()<=updPendingUntil;
      s.fillRoundRect(SCR_W-70,itemY+2,64,UPD_ITEM_H-4,4, pendingThis?T().danger:T().good);
      s.setTextColor(T().bg); s.setCursor(SCR_W-64,itemY+6);
      s.print(pendingThis?"Yakin?":"Pasang");
    }
    if(updFileCount > UPD_ITEMS_PAGE){
      if(updScrollPage>0){
        s.fillRoundRect(SCR_W-120,UPD_NAV_Y,54,UPD_NAV_H,4,T().surface2);
        s.setTextColor(T().text); s.setCursor(SCR_W-112,UPD_NAV_Y+4); s.print("< Prev");
      }
      if((updScrollPage+1)*UPD_ITEMS_PAGE < updFileCount){
        s.fillRoundRect(SCR_W-60,UPD_NAV_Y,54,UPD_NAV_H,4,T().surface2);
        s.setTextColor(T().text); s.setCursor(SCR_W-54,UPD_NAV_Y+4); s.print("Next >");
      }
    }
  }

  s.drawFastHLine(8,UPD_DIV_Y,SCR_W-16,T().divider);

  s.setTextColor(T().subtext); s.setCursor(8,UPD_WIFI_LABEL_Y); s.print("WiFi - unduh dari URL:");

  s.fillRoundRect(8,UPD_URL_Y,SCR_W-16,UPD_URL_H,4,T().surface);
  s.setTextColor(T().text); s.setCursor(12,UPD_URL_Y+6);
  String uDisp = updUrl.length() ? updUrl : "(ketuk utk isi URL firmware.bin)";
  if(uDisp.length()>34) uDisp = uDisp.substring(0,32)+"..";
  s.print(uDisp.c_str());

  bool pendingWifi = (updPendingConfirm == ("wifi:"+updUrl)) && millis()<=updPendingUntil;
  uint16_t wifiBtnColor = (!wifiConnected) ? T().surface2 : (pendingWifi ? T().danger : T().accent);
  s.fillRoundRect(8,UPD_BTN_Y,SCR_W-16,UPD_BTN_H,6, wifiBtnColor);
  s.setTextColor((!wifiConnected) ? T().subtext : T().bg); s.setCursor(14,UPD_BTN_Y+7);
  s.print(!wifiConnected ? "WiFi tidak terhubung" : (pendingWifi ? "Yakin? Ketuk lagi" : "Unduh & Pasang"));

  if(kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

void updTouch(int x,int y,bool held,bool isNew){
  if(updBusy) return;

  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){ kbVisible=false; kbTarget=nullptr; }
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;

  if(isBack(x,y)){ navBack(); return; }

  if(x>=SCR_W-64 && x<=SCR_W-6 && y>=24 && y<=42){
    updScanFiles(); showToast("Scan selesai"); needRedraw=true; return;
  }

  if(sdReady && updFileCount>0){
    int startIdx = updScrollPage*UPD_ITEMS_PAGE;
    for(int i=0;i<UPD_ITEMS_PAGE && (startIdx+i)<updFileCount;i++){
      int idx = startIdx+i;
      int itemY = UPD_ITEM_Y + i*(UPD_ITEM_H+UPD_ITEM_GAP);
      if(y>=itemY && y<=itemY+UPD_ITEM_H && x>=SCR_W-70 && x<=SCR_W-6){
        String fn = updFileList[idx];
        if(updCheckConfirm("sd:"+fn)){
          updInstallFromSD(fn);
        }
        return;
      }
    }
    if(updFileCount > UPD_ITEMS_PAGE){
      if(updScrollPage>0 && x>=SCR_W-120 && x<=SCR_W-66 && y>=UPD_NAV_Y && y<=UPD_NAV_Y+UPD_NAV_H){
        updScrollPage--; needRedraw=true; return;
      }
      if((updScrollPage+1)*UPD_ITEMS_PAGE<updFileCount && x>=SCR_W-60 && x<=SCR_W-6 && y>=UPD_NAV_Y && y<=UPD_NAV_Y+UPD_NAV_H){
        updScrollPage++; needRedraw=true; return;
      }
    }
  }

  if(y>=UPD_URL_Y && y<=UPD_URL_Y+UPD_URL_H){
    kbTarget=&updUrl; kbVisible=true; kbMode=KB_LOWER; needRedraw=true; return;
  }

  if(y>=UPD_BTN_Y && y<=UPD_BTN_Y+UPD_BTN_H){
    if(!wifiConnected){ showToast("Sambungkan WiFi dulu di Setting"); return; }
    if(updUrl.length()<8){ showToast("Isi URL firmware dulu"); return; }
    if(updCheckConfirm("wifi:"+updUrl)){
      updInstallFromURL(updUrl);
    }
    return;
  }
}

// =============================================
// PUSH FRAME
// =============================================
void push(){ canvas.pushSprite(0,0); }
void renderCurrentFrame(){
  if(locked){
    drawLockScreen(canvas);
  } else if(curScreen()==SCR_HOME){
    drawHome(canvas,homeScrollY);
  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0) apps[idx].draw(canvas);
  }
  if(!locked && controlCenterOpen) drawControlCenter(canvas);
  if(!locked && !controlCenterOpen) drawDynamicIsland(canvas); // v14: overlay di atas semua layar (kecuali lock/CC)
}

// =============================================
// BOOT SEQUENCE — "Ren Phone" lalu logo OS "SanzX OS", baru transisi ke UI
// (sekarang main persis di orientasi yang tersimpan/dipilih user, karena
// rotasi sudah di-set SEBELUM boot sequence dipanggil di setup() -- lihat
// FIX v9 di bagian setup())
// =============================================
void bootFade(int fromB,int toB,int steps,int stepMs){
  if(steps<1) steps=1;
  for(int i=0;i<=steps;i++){
    int b = fromB + (toB-fromB)*i/steps;
    if(b<0)b=0; if(b>255)b=255;
    display.setBrightness((uint8_t)b);
    delay(stepMs);
  }
}

void bootDrawParticleRing(int cx,int cy,int r,int count,uint16_t color,float phase){
  for(int i=0;i<count;i++){
    float ang = (float)i/count*2.0f*PI + phase;
    int px = cx + (int)(cosf(ang)*r);
    int py = cy + (int)(sinf(ang)*r);
    canvas.fillCircle(px,py,2,color);
  }
}

// ---- Efek "glitch" - dipakai di kedua tahap boot ----
void glitchNoiseBlocks(int w,int h,int count){
  for(int i=0;i<count;i++){
    int bw = random(4,26), bh = random(1,5);
    int bx = random(0,max(1,w-bw)), by = random(0,max(1,h-bh));
    int r=random(0,3);
    uint16_t c = (r==0)?0xFFFF:(r==1)?0xF81F:0x07FF; // putih / magenta / cyan
    canvas.fillRect(bx,by,bw,bh,c);
  }
}
void glitchScanTear(int w,int h,int count){
  for(int i=0;i<count;i++){
    int ty = random(0,h);
    int th = random(1,4);
    uint16_t c = (random(0,2)==0)?0xF81F:0x07FF; // garis interferensi magenta/cyan
    canvas.fillRect(0,ty,w,th,c);
    if(random(0,2)==0){
      int sx = random(0,max(1,w-24));
      canvas.fillRect(sx,ty,24,th,0x0000); // "potongan" hitam kayak sinyal putus
    }
  }
}
// teks dgn efek chromatic aberration (merah/cyan geser dikit dr putih) - ciri khas glitch
void glitchTextRGB(int cx,int y,const char* text,int size){
  int tw = (int)strlen(text)*6*size;
  int x = cx - tw/2;
  int off = random(1,3);
  canvas.setTextSize(size);
  canvas.setTextColor(0xF800); canvas.setCursor(x-off,y); canvas.print(text);
  canvas.setTextColor(0x07FF); canvas.setCursor(x+off,y); canvas.print(text);
  canvas.setTextColor(0xFFFF); canvas.setCursor(x,y);     canvas.print(text);
}

// =================================================================
// v12: TEMA BOOT SEQUENCE DIPERBARUI -- dulu latar cuma hitam polos +
// scale-in linear + teks muncul mendadak (hard cutoff) + loader titik
// cuma kedip nyala/mati. Sekarang:
//  1. Latar jadi gradient halus (lerpColor565+bootDrawGradientBg) drpd
//     hitam polos, bikin logo lebih "pop".
//  2. Logo & hexagon skrg pakai navEase (easing yg sama dgn transisi
//     navigasi & Control Center) drpd progres linear -> geraknya lbh
//     "premium", mulai halus & berhenti halus.
//  3. Ada halo lembut berlapis di belakang logo (pendekatan murah dari
//     efek glow, tanpa alpha blending sungguhan).
//  4. Judul muncul dgn fade halus (interpolasi warna) drpd nongol
//     mendadak di p>0.5.
//  5. Loader titik di stage 2 skrg berdenyut ukurannya (wave pulse)
//     drpd cuma kedip warna nyala/mati.
// Sengaja TIDAK dibuat "ramai" (no glitch/chromatic-aberration) --
// senada dgn keputusan v11 yg bikin Game Booster jadi simpel & bersih.
// =================================================================
uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t){
  if(t<0)t=0; if(t>1)t=1;
  int r1=(c1>>11)&0x1F, g1=(c1>>5)&0x3F, b1=c1&0x1F;
  int r2=(c2>>11)&0x1F, g2=(c2>>5)&0x3F, b2=c2&0x1F;
  int r=r1+(int)((r2-r1)*t), g=g1+(int)((g2-g1)*t), b=b1+(int)((b2-b1)*t);
  return (uint16_t)((r<<11)|(g<<5)|b);
}
// x,y,w,h = area yg mau digambar; totalH = tinggi total gradient (biasanya
// tinggi layar) -- dipisah dari h/y supaya bisa dipakai jg utk nge-refresh
// SEBAGIAN area saja (mis. area loader titik) tanpa bikin "kotak hitam"
// yg warnanya beda dari gradient di sekelilingnya.
void bootDrawGradientBg(int x,int y,int w,int h,int totalH,uint16_t top,uint16_t bottom){
  for(int row=0; row<h; row++){
    float t=(totalH>0)? (float)(y+row)/totalH : 0.0f;
    canvas.drawFastHLine(x,y+row,w,lerpColor565(top,bottom,t));
  }
}

// v13: PALET BOOT DISATUKAN & DIREDAM -- dulu stage 1 pakai oranye+gradient
// biru dan stage 2 pakai cyan+gradient navy (2 hue berbeda + gradient
// berwarna = kesannya "ramai"/banyak warna). Sekarang KEDUA stage berbagi
// satu palet monokrom gelap yg sama (BOOT_*): background nyaris hitam
// polos (bukan gradient biru/navy lagi), logo/hex/teks putih bersih, dan
// cuma SATU warna aksen abu-kebiruan redup dipakai bersama utk halo &
// particle ring. Baik sesuai preferensi tema dark (minim warna).
uint16_t BOOT_ACCENT = 0xFFFF; // putih -- dipakai kedua stage (dulu: oranye vs cyan)
uint16_t BOOT_GLOW   = 0x39C7; // satu abu-kebiruan redup utk halo & partikel
uint16_t BOOT_BG_TOP = 0x10A2; // abu sangat gelap (nyaris hitam)
uint16_t BOOT_BG_BOT = 0x0000; // hitam polos

void bootStageRenPhone(int w,int h){
  int cx=w/2, cy=h/2-24;
  uint16_t accent=BOOT_ACCENT;
  uint16_t glow=BOOT_GLOW;
  uint16_t bgTop=BOOT_BG_TOP, bgBot=BOOT_BG_BOT;

  // v12: logo membesar dgn EASING (navEase) + halo lembut berlapis +
  // background gradient -- kesannya lbh "premium" drpd scale-in linear +
  // partikel doang di atas hitam polos.
  const int STEPS=14;
  for(int f=0; f<=STEPS; f++){
    float lp=(float)f/STEPS;
    float p=navEase(lp);
    bootDrawGradientBg(0,0,w,h,h,bgTop,bgBot);

    int haloR=(int)(20+34*p);
    canvas.fillCircle(cx,cy,haloR+10,lerpColor565(bgTop,glow,0.35f));
    canvas.fillCircle(cx,cy,haloR+4, lerpColor565(bgTop,glow,0.6f));
    bootDrawParticleRing(cx,cy,46+(int)(16*p),12,BOOT_GLOW,p*2.4f);

    int boxSize=(int)(22+42*p);
    canvas.fillRoundRect(cx-boxSize/2,cy-boxSize/2,boxSize,boxSize,boxSize/4,accent);
    canvas.setTextColor(0x0000); canvas.setTextSize(3);
    canvas.setCursor(cx-9,cy-12); canvas.print("R");

    if(lp>0.45f){
      float tp = navEase((lp-0.45f)/0.55f); // fade-in judul jg mulus, bukan mendadak
      canvas.setTextColor(lerpColor565(bgBot,0xFFFF,tp)); canvas.setTextSize(2);
      const char* title="REN PHONE";
      int tw=strlen(title)*12;
      canvas.setCursor(cx-tw/2,cy+50); canvas.print(title);
    }
    push();
    delay(7);
  }
  canvas.setTextColor(0x8C51); canvas.setTextSize(1);
  const char* tag="Simplicity, Redefined.";
  int tgw=strlen(tag)*6;
  canvas.setCursor(cx-tgw/2,cy+76); canvas.print(tag);
  push();
  delay(280);
}

void bootStageSanzXOS(int w,int h){
  int cx=w/2, cy=h/2-10;
  // v13: dulu cyan+gradient navy (beda hue dr stage 1 = kesan banyak warna).
  // Sekarang pakai persis palet BOOT_* yg sama dgn stage 1 -> boot sequence
  // terasa satu kesatuan tema gelap, bukan dua tema warna yg beda2.
  uint16_t hexColor=BOOT_ACCENT;
  uint16_t glow=BOOT_GLOW;
  uint16_t bgTop=BOOT_BG_TOP, bgBot=BOOT_BG_BOT;

  // v12: hexagon muncul dgn EASING (bukan progres linear) + sedikit
  // rotasi selagi membesar (kesan "merakit diri", settle ke posisi
  // normal) + halo lembut + background gradient -- senada dgn stage 1.
  const int STEPS=14;
  for(int f=0; f<=STEPS; f++){
    float lp=(float)f/STEPS;
    float p=navEase(lp);
    bootDrawGradientBg(0,0,w,h,h,bgTop,bgBot);

    int haloR=(int)(16+30*p);
    canvas.fillCircle(cx,cy,haloR+10,lerpColor565(bgTop,glow,0.35f));
    canvas.fillCircle(cx,cy,haloR+4, lerpColor565(bgTop,glow,0.6f));

    float r=34*p;
    float rot=(1.0f-p)*0.6f; // mulai agak miring, settle ke posisi normal
    int hx[6],hy[6];
    for(int i=0;i<6;i++){
      float ang=PI/6+i*PI/3+rot;
      hx[i]=cx+(int)(cosf(ang)*r); hy[i]=cy+(int)(sinf(ang)*r);
    }
    for(int i=0;i<6;i++) canvas.drawLine(hx[i],hy[i],hx[(i+1)%6],hy[(i+1)%6],hexColor);

    canvas.setTextColor(hexColor); canvas.setTextSize(2);
    canvas.setCursor(cx-8,cy-8); canvas.print("S");

    if(lp>0.45f){
      float tp = navEase((lp-0.45f)/0.55f);
      canvas.setTextColor(lerpColor565(bgBot,0xFFFF,tp)); canvas.setTextSize(2);
      const char* title="SanzX OS";
      int tw=strlen(title)*12;
      canvas.setCursor(cx-tw/2,cy+48); canvas.print(title);
    }
    push();
    delay(7);
  }
  // v12: loader 3 titik skrg berdenyut ukurannya secara berjalan (wave
  // pulse) drpd cuma kedip nyala/mati -- terasa lbh hidup & mulus.
  int dotCx=cx, dotCy=cy+70;
  for(int i=0;i<8;i++){
    bootDrawGradientBg(dotCx-28,dotCy-8,56,18,h,bgTop,bgBot);
    for(int d=0; d<3; d++){
      float phase = (float)i - d*1.2f;
      float pulse = 0.5f+0.5f*sinf(phase*0.9f);
      int rad = 2+(int)(2*pulse);
      uint16_t dc = lerpColor565(BOOT_GLOW,0xFFFF,pulse);
      canvas.fillCircle(dotCx-16+d*16,dotCy,rad,dc);
    }
    push();
    delay(45);
  }
  delay(120);
}

void runBootSequence(){
  int w=display.width(), h=display.height();
  display.setBrightness(0);
  canvas.fillSprite(0x0000);
  push();

  bootFade(0,255,8,6);       // fade masuk gelap -> terang (cepat)
  bootStageRenPhone(w,h);    // 1) logo "Ren Phone"
  bootFade(255,0,7,5);       // fade keluar gelap
  delay(60);

  canvas.fillSprite(0x0000); push();
  bootFade(0,255,8,6);
  bootStageSanzXOS(w,h);     // 2) logo OS "SanzX OS"
  bootFade(255,0,7,5);
  delay(60);

  bootFade(0,brightness,10,6); // 3) transisi akhir, fade masuk ke UI utama
}

// =============================================
// SETUP
// =============================================
bool wasTouched=false;
int  touchStartX=0,touchStartY=0,touchLastY=0;
bool isSwiping=false;
unsigned long swipeStartTime=0;
int gStartX=0,gStartY=0; bool gGestureDone=false;

// =================================================================
// SENSOR: MPU6050 (accelerometer + gyroscope) via I2C
// Wiring: SDA=GPIO15, SCL=GPIO7, VCC=3V3, GND=GND, alamat I2C=0x68
// Dipakai untuk: auto-rotate layar, deteksi goyang (shake), dan
// app "Orientasi 3D" (visualisasi 3D real-time).
// =================================================================
#define MPU_SDA_PIN 15
#define MPU_SCL_PIN 7
#define MPU_ADDR    0x68

bool  mpuReady   = false;
float mpuAx=0, mpuAy=0, mpuAz=1.0f;      // percepatan, satuan g (sudah dikoreksi offset)
float mpuGx=0, mpuGy=0, mpuGz=0;         // kecepatan sudut, derajat/detik (sudah dikoreksi offset)
float mpuTempC   = 0;
float smoothRoll = 0, smoothPitch = 0;   // sudut kemiringan halus (derajat) - dipakai app Orientasi 3D

// ---- BARU: offset kalibrasi MPU6050, disimpan permanen di NVS ----
// Tanpa kalibrasi, accel/gyro mentah biasanya punya bias kecil bawaan
// pabrik (mis. gyro tidak persis 0 saat diam, accel Z tidak persis 1g
// saat rata). Offset ini dikurangkan dari pembacaan mentah di
// mpuReadRaw() supaya app Orientasi 3D, auto-rotate, shake-detect, dan
// kontrol Breakout jadi lebih akurat & stabil.
float mpuOffAx=0, mpuOffAy=0, mpuOffAz=0;
float mpuOffGx=0, mpuOffGy=0, mpuOffGz=0;

void saveMpuCal(){
  Preferences p; p.begin("mpucal",false);
  p.putFloat("ax",mpuOffAx); p.putFloat("ay",mpuOffAy); p.putFloat("az",mpuOffAz);
  p.putFloat("gx",mpuOffGx); p.putFloat("gy",mpuOffGy); p.putFloat("gz",mpuOffGz);
  p.putBool("done",true);
  p.end();
}
void loadMpuCal(){
  Preferences p; p.begin("mpucal",true);
  mpuOffAx=p.getFloat("ax",0); mpuOffAy=p.getFloat("ay",0); mpuOffAz=p.getFloat("az",0);
  mpuOffGx=p.getFloat("gx",0); mpuOffGy=p.getFloat("gy",0); mpuOffGz=p.getFloat("gz",0);
  p.end();
}

bool mpuWriteReg(uint8_t reg, uint8_t val){
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
bool mpuReadBytes(uint8_t reg, uint8_t* buf, uint8_t len){
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom((int)MPU_ADDR, (int)len);
  if(got != len) return false;
  for(uint8_t i=0;i<len;i++) buf[i] = Wire.read();
  return true;
}
bool mpuInit(){
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(400000);
  uint8_t who = 0;
  if(!mpuReadBytes(0x75, &who, 1) || who != 0x68){
    mpuReady = false;
    Serial.printf("[MPU6050] WHO_AM_I=0x%02X (harusnya 0x68) - cek wiring SDA=%d SCL=%d\n",
                  who, MPU_SDA_PIN, MPU_SCL_PIN);
    return false;
  }
  mpuWriteReg(0x6B, 0x00); // bangunkan dari sleep mode
  delay(10);
  mpuWriteReg(0x1C, 0x00); // rentang accel +-2g
  mpuWriteReg(0x1B, 0x00); // rentang gyro +-250 dps
  mpuReady = true;
  Serial.println("[MPU6050] OK, sensor siap.");
  return true;
}
bool mpuReadRaw(){
  uint8_t buf[14];
  if(!mpuReadBytes(0x3B, buf, 14)) return false;
  int16_t rax=(buf[0]<<8)|buf[1], ray=(buf[2]<<8)|buf[3], raz=(buf[4]<<8)|buf[5];
  int16_t rtemp=(buf[6]<<8)|buf[7];
  int16_t rgx=(buf[8]<<8)|buf[9], rgy=(buf[10]<<8)|buf[11], rgz=(buf[12]<<8)|buf[13];
  // FIX v10: offset kalibrasi (mpuOffAx..mpuOffGz) dikurangkan di sini,
  // jadi SEMUA konsumen sensor (auto-rotate, shake, Breakout, Orientasi 3D)
  // otomatis ikut lebih akurat begitu user selesai kalibrasi di Setting.
  mpuAx = rax/16384.0f - mpuOffAx; mpuAy = ray/16384.0f - mpuOffAy; mpuAz = raz/16384.0f - mpuOffAz; // +-2g -> 16384 LSB/g
  mpuGx = rgx/131.0f   - mpuOffGx; mpuGy = rgy/131.0f   - mpuOffGy; mpuGz = rgz/131.0f   - mpuOffGz;   // +-250dps -> 131 LSB/(deg/s)
  mpuTempC = rtemp/340.0f + 36.53f;
  return true;
}

// =================================================================
// KALIBRASI MPU6050 (BARU di v10)
// Dipanggil dari halaman Setting. HP HARUS diam & rata (layar menghadap
// ke atas) selama proses ini berjalan (~1 detik). Fungsi ini mengambil
// rata-rata sejumlah sampel mentah, lalu menghitung offset supaya:
//  - gyro (X/Y/Z) terbaca 0 dps saat diam (bias giro dihilangkan)
//  - accel X/Y terbaca 0g dan accel Z terbaca 1g saat rata (bias accel
//    & sedikit ketidaklurusan pemasangan chip dikompensasi)
// Hasilnya disimpan permanen ke NVS lewat saveMpuCal(), jadi tetap
// berlaku walau perangkat direstart.
// =================================================================
void calibrateMPU(){
  if(!mpuReady) return;

  showToast("Kalibrasi... taruh HP rata & diam!",2500);
  renderCurrentFrame();
  push();
  delay(400); // beri waktu user meletakkan HP sebelum sampling dimulai

  const int N = 200;
  double sax=0,say=0,saz=0,sgx=0,sgy=0,sgz=0;
  int got=0;
  for(int i=0;i<N;i++){
    if(mpuReadRaw()){ // note: mpuReadRaw() sudah mengurangi offset LAMA -
                       // gpp, kita hanya pakai ini utk cari offset BARU relatif thd nilai ini
      sax+=mpuAx; say+=mpuAy; saz+=mpuAz;
      sgx+=mpuGx; sgy+=mpuGy; sgz+=mpuGz;
      got++;
    }
    delay(5);
  }

  if(got<10){
    showToast("Kalibrasi gagal, coba lagi");
    return;
  }

  // Offset baru = offset lama + rata-rata error yg baru terukur, supaya
  // hasil akhir kumulatif membuat pembacaan pas di titik nol/1g yg benar.
  mpuOffAx += sax/got;
  mpuOffAy += say/got;
  mpuOffAz += (saz/got) - 1.0f; // asumsi HP rata, layar ke atas -> Z seharusnya 1g
  mpuOffGx += sgx/got;
  mpuOffGy += sgy/got;
  mpuOffGz += sgz/got;

  saveMpuCal();
  showToast("Kalibrasi MPU6050 selesai!");
  needRedrawNow();
}

// ---- Auto-rotate: layar ikut kemiringan HP ----
bool autoRotateEnabled = true;
void saveAutoRotatePref(){ Preferences p; p.begin("ui",false); p.putBool("autorot",autoRotateEnabled); p.end(); }
bool loadAutoRotatePref(){ Preferences p; p.begin("ui",true); bool v=p.getBool("autorot",true); p.end(); return v; }

Orientation  pendingOrient      = ORIENT_PORTRAIT;
unsigned long pendingOrientSince = 0;

// =====================================================================
// FIX v8 (BUG #1 - orientasi MPU6050 terbalik):
//   Sebelumnya "ax dominan -> LANDSCAPE, ay dominan -> PORTRAIT". Ini
//   terbalik dari cara MPU6050 terpasang secara fisik di board user,
//   sehingga HP yang dipegang tegak (portrait) malah dibaca landscape
//   dan sebaliknya. Sekarang ditukar: "ax dominan -> PORTRAIT,
//   ay dominan -> LANDSCAPE".
//
// FIX v8 (BUG #2 - lock screen tidak bisa diusap, kasus AUTO-ROTATE):
//   Ditambah "if(locked) return;" di baris pertama, supaya orientasi
//   layar TIDAK PERNAH berubah otomatis selagi masih di lock screen.
//   (Catatan: ada JUGA penyebab lain utk keluhan "lock screen tidak
//   bisa diusap" yang TIDAK ada hubungannya dgn MPU6050/auto-rotate
//   sama sekali -- yaitu mismatch antara rotasi saat kalibrasi touch
//   dilakukan vs rotasi runtime. Itu dibenahi terpisah di FIX v9,
//   lihat komentar di fungsi setup().)
// =====================================================================
void autoRotateUpdate(){
  if(locked) return;                          // <-- FIX v8: jangan auto-rotate saat lock screen
  if(!autoRotateEnabled || !mpuReady) return;
  float ax=mpuAx, ay=mpuAy;
  float mag = sqrtf(ax*ax+ay*ay);
  if(mag < 0.35f) return; // HP hampir rebah datar -> jangan ganti orientasi
  const float HYST = 0.15f;
  Orientation suggestion = currentOrient;
  if(fabsf(ax) > fabsf(ay) + HYST) suggestion = ORIENT_PORTRAIT;    // FIX v8: ditukar (dulu LANDSCAPE)
  else if(fabsf(ay) > fabsf(ax) + HYST) suggestion = ORIENT_LANDSCAPE; // FIX v8: ditukar (dulu PORTRAIT)
  else return; // di zona abu-abu (dekat 45 derajat), biarkan dulu

  if(suggestion != pendingOrient){
    pendingOrient = suggestion;
    pendingOrientSince = millis();
    return;
  }
  // butuh stabil ~450ms dulu sebelum benar2 rotasi, biar gak flip-flop
  if(suggestion != currentOrient && millis()-pendingOrientSince > 450){
    applyOrientation(suggestion, false);
  }
}

// ---- Deteksi goyang (shake) - toggle ON/OFF ada di Control Center ----
bool shakeEnabled = true;
void saveShakePref(){ Preferences p; p.begin("ui",false); p.putBool("shakeon",shakeEnabled); p.end(); }
bool loadShakePref(){ Preferences p; p.begin("ui",true); bool v=p.getBool("shakeon",true); p.end(); return v; }

float shakeLastMag = 1.0f;
unsigned long lastShakeMs = 0;
void shakeCheck(){
  if(!shakeEnabled || !mpuReady) return;
  float mag = sqrtf(mpuAx*mpuAx + mpuAy*mpuAy + mpuAz*mpuAz);
  float delta = fabsf(mag - shakeLastMag);
  shakeLastMag = mag;
  if(delta > 0.7f && millis()-lastShakeMs > 1200){
    lastShakeMs = millis();
    // Default aksi shake = pulang ke Home. Jangan trigger saat lagi ketik,
    // gambar di Canvas, atau lagi proses update firmware (biar gak ganggu).
    if(!locked && !kbVisible && curScreen()!=SCR_CANVAS && curScreen()!=SCR_UPDATE && !updBusy){
      showToast("Goyangan terdeteksi -> Home");
      navGoHome();
    }
  }
}

void mpuUpdate(){
  if(!mpuReady) return;
  if(!mpuReadRaw()) return;
  float rollRaw  = atan2f(mpuAy, mpuAz) * 180.0f/PI;
  float pitchRaw = atan2f(-mpuAx, sqrtf(mpuAy*mpuAy+mpuAz*mpuAz)) * 180.0f/PI;
  smoothRoll  += (rollRaw  - smoothRoll)  * 0.25f;
  smoothPitch += (pitchRaw - smoothPitch) * 0.25f;
  autoRotateUpdate();
  shakeCheck();
}

// =================================================================
// BATERAI: voltage divider 10k+10k (rasio 1:2), Li-ion 1 sel 3.0V-4.2V
// Kapasitas baterai yang dipakai: 2300 mAh
//
// PENTING - PERBAIKAN PIN (root cause bug "lock screen gak bisa diswipe"):
// GPIO4 TIDAK DIPAKAI LAGI di sini karena BENTROK dengan pin MISO chip
// touch XPT2046 (cfg.pin_miso=4 di konfigurasi LGFX touch). analogRead()
// tiap 5 detik ke GPIO4 mengubah mode pin itu dari SPI jadi ADC, merusak
// pembacaan sentuh XPT2046 -- inilah kenapa swipe lock screen jadi gagal
// setelah fitur baterai ditambahkan.
// Sekarang dipindah ke GPIO8 (ADC1_CH7 di ESP32-S3), pin yang benar2
// kosong / tidak dipakai fungsi lain di board ini, dan tetap di ADC1
// (bukan ADC2) supaya aman dipakai bareng WiFi.
// ==> WIRING: pindahkan kabel output voltage divider dari GPIO4 ke GPIO8.
// =================================================================
#define BATT_ADC_PIN 8
#define BATT_DIVIDER_RATIO 2.0f   // 10k+10k sama besar -> Vbat = Vadc * 2
#define BATTERY_CAPACITY_MAH 2300 // kapasitas baterai yang dipakai user
float battVoltage = 3.7f;
int   battPercent = 100;
bool  battWarnedLow = false, battWarnedCritical = false;

void battUpdate(){
  int raw = analogRead(BATT_ADC_PIN);
  float vAdc = (raw/4095.0f) * 3.3f;
  float vBat = vAdc * BATT_DIVIDER_RATIO;
  battVoltage = battVoltage*0.7f + vBat*0.3f; // smoothing biar angkanya gak lompat-lompat
  int pct = (int)((battVoltage - 3.0f) / (4.2f - 3.0f) * 100.0f);
  battPercent = constrain(pct, 0, 100);

  if(battPercent <= 5 && !battWarnedCritical){
    battWarnedCritical = true;
    showToast("Baterai kritis! Segera cas.");
  } else if(battPercent <= 15 && !battWarnedLow){
    battWarnedLow = true;
    showToast("Baterai lemah, segera cas.");
  } else if(battPercent > 25){
    battWarnedLow = false; battWarnedCritical = false; // reset biar bisa warning lagi kalau turun lagi
  }
}

// =============================================
// APP: BATERAI
// =============================================
void battEnter(){ battUpdate(); }
void battExit(){}

void drawBatteryApp(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setTextColor(0x07E0);s.setTextSize(1);s.setCursor(8,26);s.print("Baterai");

  s.fillRoundRect(SCR_W-70,24,64,18,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(SCR_W-62,29);s.print("Refresh");

  // --- Ikon baterai besar, terisi sesuai persen ---
  int iconW = 90, iconH = 44;
  int iconX = SCR_W/2 - iconW/2;
  int iconY = STATUS_H + 20;
  uint16_t battColor = (battPercent<=15)?T().danger:(battPercent<=35)?T().accent:T().good;

  s.drawRoundRect(iconX,iconY,iconW,iconH,6,T().text);
  s.drawRoundRect(iconX+1,iconY+1,iconW-2,iconH-2,5,T().text);
  s.fillRoundRect(iconX+iconW,iconY+iconH/2-7,7,14,3,T().text); // kutub positif baterai

  int innerPad=5;
  int innerW = iconW - innerPad*2;
  int innerH = iconH - innerPad*2;
  int fillW = constrain((innerW*battPercent)/100,0,innerW);
  if(fillW>0) s.fillRoundRect(iconX+innerPad,iconY+innerPad,fillW,innerH,3,battColor);

  // --- Persen besar ---
  char pctBuf[8]; sprintf(pctBuf,"%d%%",battPercent);
  s.setTextColor(battColor); s.setTextSize(3);
  int pw = strlen(pctBuf)*18;
  s.setCursor(SCR_W/2-pw/2, iconY+iconH+14);
  s.print(pctBuf);

  // --- Detail teknis ---
  int ty = iconY+iconH+52;
  int lh = 14;
  char buf[64];
  s.setTextSize(1);

  s.setTextColor(T().subtext); s.setCursor(14,ty); s.print("Tegangan saat ini:");
  s.setTextColor(T().text);
  sprintf(buf,"%.2f V", battVoltage);
  s.setCursor(SCR_W-14-(int)strlen(buf)*6, ty); s.print(buf);
  ty += lh;

  s.setTextColor(T().subtext); s.setCursor(14,ty); s.print("Rentang tegangan:");
  s.setTextColor(T().text); s.setCursor(SCR_W-14-11*6, ty); s.print("3.0 - 4.2 V");
  ty += lh;

  s.setTextColor(T().subtext); s.setCursor(14,ty); s.print("Kapasitas baterai:");
  s.setTextColor(T().text);
  sprintf(buf,"%d mAh", BATTERY_CAPACITY_MAH);
  s.setCursor(SCR_W-14-(int)strlen(buf)*6, ty); s.print(buf);
  ty += lh;

  s.setTextColor(T().subtext); s.setCursor(14,ty); s.print("Estimasi sisa daya:");
  s.setTextColor(T().text);
  int remainMah = (int)(BATTERY_CAPACITY_MAH * battPercent / 100.0f);
  sprintf(buf,"~%d mAh", remainMah);
  s.setCursor(SCR_W-14-(int)strlen(buf)*6, ty); s.print(buf);
  ty += lh;

  s.setTextColor(T().subtext); s.setCursor(14,ty); s.print("Status:");
  s.setTextColor(battColor);
  const char* statusTxt = (battPercent<=5)?"Kritis, segera cas!" :
                           (battPercent<=15)?"Lemah, segera cas" :
                           (battPercent<=35)?"Cukup" : "Baik";
  s.setCursor(14+7*6, ty); s.print(statusTxt);
  ty += lh+4;

  s.drawFastHLine(10,ty,SCR_W-20,T().divider);
  ty += 8;
  s.setTextColor(T().subtext); s.setTextWrap(true);
  s.setCursor(10,ty);
  s.print("Catatan: estimasi dihitung dari pembacaan tegangan (voltage divider), bukan fuel-gauge IC, jadi persen & sisa mAh di atas perkiraan kasar, bukan angka presisi.");
  s.setTextWrap(false);

  drawBack(s);
  drawToast(s);
}

void battTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  if(x>=SCR_W-70 && x<=SCR_W-6 && y>=24 && y<=42){
    battUpdate();
    showToast("Data baterai diperbarui");
    needRedraw=true;
    return;
  }
}

// =============================================
// APP: TRIVIA QUIZ (BARU v14)
// Soal diambil REAL-TIME dari Open Trivia Database (opentdb.com) lewat
// HTTPClient async (task terpisah spt pola doGeminiHttpRequest/geminiTaskFunc
// -> UI gak nge-freeze nunggu server). Parsing JSON manual (tanpa library
// ArduinoJson) senada gaya yg sudah dipakai di doGeminiHttpRequest():
// dipakai param API &encode=base64 supaya SEMUA field teks jadi string
// base64 polos (tanpa perlu unescape tanda kutip/karakter aneh dulu),
// lalu hasilnya di-decode base64 + entity HTML manual (spt &quot; &#039;).
// =============================================

// ---- Base64 decode sederhana (dipakai baca respons OpenTDB) ----
int triviaB64Val(char c){
  if(c>='A'&&c<='Z') return c-'A';
  if(c>='a'&&c<='z') return c-'a'+26;
  if(c>='0'&&c<='9') return c-'0'+52;
  if(c=='+') return 62;
  if(c=='/') return 63;
  return -1;
}
String triviaB64Decode(const String& in){
  String out; out.reserve(in.length()*3/4+4);
  int val=0, bits=-8;
  for(size_t i=0;i<in.length();i++){
    char c=in[i];
    if(c=='=') break;
    int d=triviaB64Val(c);
    if(d<0) continue;
    val=(val<<6)+d; bits+=6;
    if(bits>=0){ out += (char)((val>>bits)&0xFF); bits-=8; }
  }
  return out;
}

// ---- Decode entity HTML umum yg sering muncul di teks OpenTDB. Karakter
// aksen (é, ü, dst) sengaja ditranslit ke ASCII polos krn font bawaan
// LovyanGFX di proyek ini cuma dukung ASCII -- kalau dibiarkan unicode
// mentah, malah gak kebaca sama sekali di layar. ----
String triviaHtmlDecode(String s){
  struct Ent{ const char* pat; const char* rep; };
  static const Ent ents[] = {
    {"&quot;","\""}, {"&#039;","'"}, {"&apos;","'"}, {"&rsquo;","'"}, {"&lsquo;","'"},
    {"&rdquo;","\""}, {"&ldquo;","\""}, {"&ndash;","-"}, {"&mdash;","-"}, {"&hellip;","..."},
    {"&eacute;","e"}, {"&egrave;","e"}, {"&ecirc;","e"}, {"&uuml;","u"}, {"&ouml;","o"},
    {"&auml;","a"}, {"&ntilde;","n"}, {"&ccedil;","c"}, {"&aacute;","a"}, {"&iacute;","i"},
    {"&oacute;","o"}, {"&uacute;","u"}, {"&deg;","deg"}, {"&times;","x"}, {"&divide;","/"},
    {"&frac12;","1/2"}, {"&frac14;","1/4"}, {"&frac34;","3/4"}, {"&trade;","(TM)"},
    {"&lt;","<"}, {"&gt;",">"}, {"&nbsp;"," "}, {"&amp;","&"}, // &amp; PALING TERAKHIR
  };
  for(auto& e: ents) s.replace(e.pat, e.rep);
  return s;
}

// ---- Parser JSON manual (khusus bentuk respons OpenTDB, bukan parser umum) ----
int triviaJsonGetInt(const String& s, const char* key){
  String pat = String("\"")+key+"\":";
  int i = s.indexOf(pat);
  if(i<0) return -1;
  i += pat.length();
  int j=i;
  while(j<(int)s.length() && (isDigit(s[j])||s[j]=='-')) j++;
  if(j==i) return -1;
  return s.substring(i,j).toInt();
}
String triviaJsonGetString(const String& obj, const char* key){
  String pat = String("\"")+key+"\":\"";
  int i = obj.indexOf(pat);
  if(i<0) return "";
  i += pat.length();
  int j = obj.indexOf('"', i);
  if(j<0) return "";
  return obj.substring(i,j);
}
int triviaJsonGetStringArray(const String& obj, const char* key, String out[], int maxN){
  String pat = String("\"")+key+"\":[";
  int i = obj.indexOf(pat);
  if(i<0) return 0;
  i += pat.length();
  int end = obj.indexOf(']', i);
  if(end<0) return 0;
  String body = obj.substring(i,end);
  int n=0, p=0;
  while(n<maxN){
    int q1=body.indexOf('"',p); if(q1<0) break;
    int q2=body.indexOf('"',q1+1); if(q2<0) break;
    out[n++]=body.substring(q1+1,q2);
    p=q2+1;
  }
  return n;
}
// Cari isi array "results":[...] dgn menghitung kedalaman [ ] (bukan cuma
// indexOf '[' & ']' pertama) krn di dalamnya ada array bersarang lain
// (tiap objek soal punya "incorrect_answers":[...] sendiri).
String triviaExtractResultsArray(const String& full){
  int i = full.indexOf("\"results\":[");
  if(i<0) return "";
  i = full.indexOf('[', i);
  int depth=0;
  for(int j=i; j<(int)full.length(); j++){
    if(full[j]=='[') depth++;
    else if(full[j]==']'){ depth--; if(depth==0) return full.substring(i+1,j); }
  }
  return "";
}
// Pecah isi array objek {...},{...} jadi potongan per-objek dgn menghitung
// kedalaman { } (robust walau ada [ ] bersarang di dalam tiap objek).
int triviaSplitObjects(const String& arrBody, String outObjs[], int maxN){
  int n=0, depth=0, start=-1;
  for(int i=0;i<(int)arrBody.length() && n<maxN;i++){
    char c=arrBody[i];
    if(c=='{'){ if(depth==0) start=i; depth++; }
    else if(c=='}'){ depth--; if(depth==0 && start>=0){ outObjs[n++]=arrBody.substring(start,i+1); start=-1; } }
  }
  return n;
}

// ---- Data tema/kategori (id sesuai id kategori resmi OpenTDB) ----
struct TriviaCategory{ int id; const char* name; uint16_t color; };
TriviaCategory triviaCats[] = {
  {9,  "Umum",          0xFD40},
  {17, "Sains & Alam",  0x07E0},
  {18, "Komputer",      0x07FF},
  {21, "Olahraga",      0xF800},
  {22, "Geografi",      0x3ADF},
  {23, "Sejarah",       0xFC9F},
  {11, "Film",          0xFFE0},
  {12, "Musik",         0x861F},
  {15, "Video Game",    0x1FF9},
  {27, "Hewan",         0xFB40},
  {31, "Anime & Manga", 0xF81F},
  {20, "Mitologi",      0x04FF},
};
#define TRIVIA_CAT_COUNT 12
const char* triviaDiffNames[4] = {"Mudah","Sedang","Sulit","Acak"};
const int   triviaAmountOpts[3] = {5,10,15};

// ---- State ----
enum TriviaPage { TRV_SETUP, TRV_LOADING, TRV_QUESTION, TRV_RESULT, TRV_ERROR };
TriviaPage triviaPage = TRV_SETUP;

#define TRIVIA_MAX_Q 20
struct TriviaQuestion{ String question; String answers[4]; int correctIdx; };
TriviaQuestion triviaQs[TRIVIA_MAX_Q];
int triviaCount=0, triviaCur=0, triviaScore=0, triviaCorrectCount=0;
int triviaSelected=-1; bool triviaAnswered=false;
unsigned long triviaAnswerAtMs=0;
String triviaErrorMsg="";

int triviaSelCat=0, triviaSelDiff=0, triviaSelAmount=1; // default: Umum, Mudah, 10 soal
float triviaCatScrollY=0;
int triviaTouchStartX=0, triviaTouchStartY=0, triviaTouchLastY=0;
bool triviaIsSwiping=false; unsigned long triviaSwipeStartTime=0;

TaskHandle_t triviaTaskHandle=NULL;

// ---- Layout (dihitung dari BAWAH ke ATAS, satu sumber kebenaran dipakai
// bareng oleh fungsi gambar & fungsi hit-test sentuhan -> gak bisa ketuker) ----
// v15 FIX: struct TriviaLayout dipindahkan ke PUNCAK FILE (dekat AiLine/
// Vec3f) karena dipakai sbg TIPE RETURN fungsi di bawah ini -- lihat
// komentar "v15 — FIX BUILD" di paling atas file utk penjelasan lengkap
// akar masalahnya. Definisi struct tidak lagi ada di sini, cukup dipakai.
TriviaLayout triviaCalcLayout(){
  TriviaLayout L;
  L.startY = backY()-6-28;
  L.amtY   = L.startY-34;
  L.diffY  = L.amtY-34;
  L.catBottom = L.diffY-14;
  return L;
}
int triviaCatTop(){ return STATUS_H+26; }
int triviaCatCols(){ return currentOrient==ORIENT_LANDSCAPE?3:2; }
int triviaCatCardW(){ int cols=triviaCatCols(); return (SCR_W-(cols+1)*6)/cols; }
int triviaCatCardH(){ return 36; }
int triviaCatMaxScroll(){
  TriviaLayout L=triviaCalcLayout();
  int cols=triviaCatCols();
  int rows=(TRIVIA_CAT_COUNT+cols-1)/cols;
  int viewportH = L.catBottom - triviaCatTop();
  int needed = rows*(triviaCatCardH()+6) - viewportH;
  return max(0,needed);
}
int triviaQTop(){ return STATUS_H+24; }
int triviaQTextH(){ return 58; }
int triviaAnsTop(){ return triviaQTop()+triviaQTextH()+4; }
void triviaAnsRect(int idx,int& x,int& y,int& w,int& h){
  int top=triviaAnsTop(), bottom=backY()-6, gap=5;
  h=(bottom-top-3*gap)/4;
  y=top+idx*(h+gap);
  x=8; w=SCR_W-16;
}

// ---- Fetch (jalan di task terpisah, pola sama dgn doGeminiHttpRequest) ----
void triviaFetchQuestions(){
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(12000);
  HTTPClient http; http.setTimeout(12000); http.setConnectTimeout(12000);

  int catId = triviaCats[triviaSelCat].id;
  int amount = triviaAmountOpts[triviaSelAmount];
  String diffParam = "";
  if(triviaSelDiff==0) diffParam="&difficulty=easy";
  else if(triviaSelDiff==1) diffParam="&difficulty=medium";
  else if(triviaSelDiff==2) diffParam="&difficulty=hard";
  // triviaSelDiff==3 ("Acak") -> tanpa parameter difficulty = OpenTDB campur semua tingkat

  String url = "https://opentdb.com/api.php?amount="+String(amount)+"&category="+String(catId)+
               diffParam+"&type=multiple&encode=base64";

  if(!http.begin(client,url)){
    triviaErrorMsg="Gagal inisialisasi koneksi ke server soal.";
    triviaPage=TRV_ERROR; needRedrawNow(); return;
  }
  int httpCode = http.GET();
  if(httpCode!=HTTP_CODE_OK){
    triviaErrorMsg = "Gagal mengambil soal (HTTP "+String(httpCode)+"). Cek koneksi internet.";
    http.end(); triviaPage=TRV_ERROR; needRedrawNow(); return;
  }
  String body = http.getString();
  http.end();

  int rc = triviaJsonGetInt(body, "response_code");
  if(rc!=0){
    switch(rc){
      case 1: triviaErrorMsg="Soal gak cukup utk tema/tingkat ini. Coba tema/jumlah lain."; break;
      case 2: triviaErrorMsg="Parameter permintaan tidak valid."; break;
      default: triviaErrorMsg="Server soal sedang bermasalah. Coba lagi nanti."; break;
    }
    triviaPage=TRV_ERROR; needRedrawNow(); return;
  }

  String resultsArr = triviaExtractResultsArray(body);
  String objs[TRIVIA_MAX_Q];
  int n = triviaSplitObjects(resultsArr, objs, TRIVIA_MAX_Q);

  int built=0;
  for(int i=0;i<n && built<TRIVIA_MAX_Q;i++){
    String qB64 = triviaJsonGetString(objs[i], "question");
    String cB64 = triviaJsonGetString(objs[i], "correct_answer");
    String wrongB64[3];
    int wn = triviaJsonGetStringArray(objs[i], "incorrect_answers", wrongB64, 3);
    if(qB64.length()==0 || cB64.length()==0 || wn<1) continue;

    String qText = triviaHtmlDecode(triviaB64Decode(qB64));
    String correctText = triviaHtmlDecode(triviaB64Decode(cB64));
    String opts[4]; int optN=0;
    opts[optN++]=correctText;
    for(int k=0;k<wn && optN<4;k++) opts[optN++]=triviaHtmlDecode(triviaB64Decode(wrongB64[k]));
    while(optN<4) opts[optN++]="-";

    // acak urutan opsi (Fisher-Yates) biar jawaban benar gak selalu di A
    for(int k=optN-1;k>0;k--){ int r=random(0,k+1); String tmp=opts[k]; opts[k]=opts[r]; opts[r]=tmp; }
    int correctIdx=0;
    for(int k=0;k<optN;k++) if(opts[k]==correctText){ correctIdx=k; break; }

    triviaQs[built].question = qText;
    for(int k=0;k<4;k++) triviaQs[built].answers[k]=opts[k];
    triviaQs[built].correctIdx = correctIdx;
    built++;
  }

  if(built==0){
    triviaErrorMsg="Gagal membaca data soal dari server.";
    triviaPage=TRV_ERROR; needRedrawNow(); return;
  }

  triviaCount=built; triviaCur=0; triviaScore=0; triviaCorrectCount=0;
  triviaSelected=-1; triviaAnswered=false;
  triviaPage=TRV_QUESTION;
  diNotify('Q', "Kuis dimulai!", triviaCats[triviaSelCat].color, 1600, false); // v17: subtitle dihapus
  needRedrawNow();
}
void triviaTaskFunc(void* parameter){
  triviaFetchQuestions();
  triviaTaskHandle=NULL;
  vTaskDelete(NULL);
}
void triviaStartFetch(){
  if(WiFi.status()!=WL_CONNECTED){
    triviaErrorMsg="WiFi belum terhubung. Sambungkan WiFi dulu lewat Setting.";
    triviaPage=TRV_ERROR; needRedraw=true; return;
  }
  triviaPage=TRV_LOADING;
  needRedraw=true;
  BaseType_t res = xTaskCreatePinnedToCore(triviaTaskFunc,"triviaTask",16384,NULL,1,&triviaTaskHandle,1);
  if(res!=pdPASS){
    triviaErrorMsg="Gagal membuat proses pengambilan soal. Coba lagi.";
    triviaPage=TRV_ERROR; needRedraw=true;
  }
}

void triviaAdvance(){
  triviaCur++;
  if(triviaCur>=triviaCount){
    triviaPage=TRV_RESULT;
    diNotify('Y', "Selesai: "+String(triviaCorrectCount)+"/"+String(triviaCount),
              T().accent2, 2200, false); // v17: subtitle dihapus, digabung ke 1 baris singkat
  } else {
    triviaSelected=-1; triviaAnswered=false;
  }
  needRedraw=true;
}
// Dipanggil TIAP loop() (murah, cek curScreen dulu) - urus animasi spinner
// loading & auto-lanjut ke soal berikutnya stlh feedback benar/salah tampil.
void triviaPeriodicUpdate(){
  if(curScreen()!=SCR_TRIVIA) return;
  if(triviaPage==TRV_LOADING) needRedraw=true;
  if(triviaPage==TRV_QUESTION && triviaAnswered && millis()-triviaAnswerAtMs>1400) triviaAdvance();
}

void triviaEnter(){ triviaPage=TRV_SETUP; triviaCatScrollY=0; }
void triviaExit(){}

void drawTriviaSetup(LGFX_Sprite& s){
  s.setTextColor(T().accent); s.setTextSize(1);
  s.setCursor(8,25); s.print("Trivia Quiz - Pilih Tema");

  TriviaLayout L = triviaCalcLayout();
  int cols=triviaCatCols(), cw=triviaCatCardW(), ch=triviaCatCardH(), gap=6;
  int top=triviaCatTop(), bottom=L.catBottom;
  for(int i=0;i<TRIVIA_CAT_COUNT;i++){
    int col=i%cols, row=i/cols;
    int x=gap+col*(cw+gap), y=top+row*(ch+gap)-(int)triviaCatScrollY;
    if(y+ch<top || y>bottom) continue;
    bool sel=(i==triviaSelCat);
    uint16_t bg = sel? triviaCats[i].color : T().surface;
    s.fillRoundRect(x,y,cw,ch,8,bg);
    if(sel) s.drawRoundRect(x,y,cw,ch,8,T().text);
    s.setTextColor(sel?T().bg:T().text); s.setTextSize(1);
    int nl=strlen(triviaCats[i].name)*6;
    s.setCursor(x+cw/2-min(nl,cw-6)/2, y+ch/2-4);
    s.print(triviaCats[i].name);
  }

  s.setTextColor(T().subtext); s.setTextSize(1);
  s.setCursor(8, L.diffY-10); s.print("Tingkat:");
  int dw=(SCR_W-16)/4;
  for(int i=0;i<4;i++){
    int x=8+i*dw; bool sel=(i==triviaSelDiff);
    s.fillRoundRect(x,L.diffY,dw-4,22,5, sel?T().accent:T().surface2);
    s.setTextColor(sel?T().bg:T().text);
    int nl=strlen(triviaDiffNames[i])*6;
    s.setCursor(x+(dw-4)/2-nl/2, L.diffY+7);
    s.print(triviaDiffNames[i]);
  }

  s.setTextColor(T().subtext);
  s.setCursor(8, L.amtY-10); s.print("Jumlah Soal:");
  int aw=(SCR_W-16)/3;
  for(int i=0;i<3;i++){
    int x=8+i*aw; bool sel=(i==triviaSelAmount);
    s.fillRoundRect(x,L.amtY,aw-4,22,5, sel?T().accent2:T().surface2);
    char b[10]; sprintf(b,"%d Soal",triviaAmountOpts[i]);
    int nl=strlen(b)*6;
    s.setTextColor(sel?T().bg:T().text);
    s.setCursor(x+(aw-4)/2-nl/2, L.amtY+7);
    s.print(b);
  }

  s.fillRoundRect(8,L.startY,SCR_W-16,28,8,T().good);
  s.setTextColor(T().bg); s.setTextSize(2);
  const char* lbl="Mulai Kuis";
  int lw=strlen(lbl)*12;
  s.setCursor(SCR_W/2-lw/2, L.startY+6);
  s.print(lbl);
}

void drawTriviaLoading(LGFX_Sprite& s){
  int cx=SCR_W/2, cy=SCR_H/2-6;
  float ang = fmodf((float)millis(),1200.0f)/1200.0f*360.0f;
  s.drawArc(cx,cy,26,20,0,360,T().divider);       // cincin dasar redup
  s.drawArc(cx,cy,26,20,ang,ang+260,T().accent2); // arc terang yg muter di atasnya
  s.setTextColor(T().subtext); s.setTextSize(1);
  const char* lbl="Mengambil soal dari server...";
  int tw=strlen(lbl)*6;
  s.setCursor(cx-tw/2, cy+40); s.print(lbl);
}

void drawTriviaQuestion(LGFX_Sprite& s){
  if(triviaCur<0||triviaCur>=triviaCount) return;
  TriviaQuestion& q = triviaQs[triviaCur];

  s.fillRoundRect(6,STATUS_H+3,96,16,4,triviaCats[triviaSelCat].color);
  s.setTextColor(T().bg); s.setTextSize(1);
  s.setCursor(11,STATUS_H+7); s.print(triviaCats[triviaSelCat].name);

  char info[24]; sprintf(info,"Soal %d/%d",triviaCur+1,triviaCount);
  s.setTextColor(T().text); s.setCursor(SCR_W-(int)strlen(info)*6-64, STATUS_H+7); s.print(info);
  char scoreB[20]; sprintf(scoreB,"Skor:%d",triviaScore);
  s.setTextColor(T().accent2); s.setCursor(SCR_W-56, STATUS_H+7); s.print(scoreB);

  // wrap pertanyaan pakai fungsi & buffer yg sama dgn AI Chat (reuse pola
  // yg sudah dipakai File Explorer sejak v12 - lihat aiWrapAppend)
  int maxChars = (SCR_W-16)/6;
  int n = aiWrapAppend(aiLinesBuf, AI_MAX_LINES, 0, q.question, T().text, maxChars);
  int qy=triviaQTop();
  for(int i=0;i<n && i<5;i++){
    s.setTextColor(aiLinesBuf[i].color); s.setTextSize(1);
    s.setCursor(8, qy+i*11); s.print(aiLinesBuf[i].text.c_str());
  }

  const char* letters[4]={"A","B","C","D"};
  for(int i=0;i<4;i++){
    int x,y,w,h; triviaAnsRect(i,x,y,w,h);
    uint16_t bg=T().surface;
    if(triviaAnswered){
      if(i==q.correctIdx) bg=T().good;
      else if(i==triviaSelected) bg=T().danger;
    }
    s.fillRoundRect(x,y,w,h,6,bg);
    bool hi = triviaAnswered && (i==q.correctIdx || i==triviaSelected);
    s.setTextColor(hi?T().bg:T().text); s.setTextSize(1);
    String line = String(letters[i])+". "+q.answers[i];
    int maxC=(w-10)/6;
    if((int)line.length()>maxC) line=line.substring(0,maxC-1)+".";
    s.setCursor(x+6, y+h/2-4); s.print(line.c_str());
  }
}

void drawTriviaResult(LGFX_Sprite& s){
  int cx=SCR_W/2;
  s.setTextColor(T().accent); s.setTextSize(2);
  const char* title="Kuis Selesai!";
  s.setCursor(cx-(int)strlen(title)*6, STATUS_H+18); s.print(title);

  char scoreB[32]; sprintf(scoreB,"%d/%d benar",triviaCorrectCount,triviaCount);
  s.setTextColor(T().text); s.setTextSize(2);
  s.setCursor(cx-(int)strlen(scoreB)*6, STATUS_H+46); s.print(scoreB);

  int pct = triviaCount? (triviaCorrectCount*100/triviaCount) : 0;
  const char* msg = pct>=80?"Hebat, jagoan trivia!" : pct>=50?"Lumayan, terus asah!" : "Ayo coba lagi, pasti bisa!";
  s.setTextColor(T().subtext); s.setTextSize(1);
  s.setCursor(cx-(int)strlen(msg)*3, STATUS_H+74); s.print(msg);

  s.fillRoundRect(8, STATUS_H+96, SCR_W-16, 26, 7, T().good);
  s.setTextColor(T().bg); s.setTextSize(1);
  const char* l1="Main Lagi (tema sama)";
  s.setCursor(cx-(int)strlen(l1)*3, STATUS_H+106); s.print(l1);

  s.fillRoundRect(8, STATUS_H+128, SCR_W-16, 26, 7, T().surface2);
  s.setTextColor(T().text);
  const char* l2="Ganti Tema";
  s.setCursor(cx-(int)strlen(l2)*3, STATUS_H+138); s.print(l2);
}

void drawTriviaError(LGFX_Sprite& s){
  s.setTextColor(T().danger); s.setTextSize(1);
  s.setCursor(8, STATUS_H+20); s.print("Gagal memuat soal:");
  int maxChars=(SCR_W-16)/6;
  int n = aiWrapAppend(aiLinesBuf, AI_MAX_LINES, 0, triviaErrorMsg, T().text, maxChars);
  for(int i=0;i<n && i<6;i++){
    s.setTextColor(T().text); s.setCursor(8, STATUS_H+36+i*11); s.print(aiLinesBuf[i].text.c_str());
  }
  int by=STATUS_H+36+6*11+10;
  s.fillRoundRect(8,by,SCR_W-16,26,7,T().accent);
  s.setTextColor(T().bg); s.setTextSize(1);
  const char* l="Coba Lagi";
  s.setCursor(SCR_W/2-(int)strlen(l)*3, by+10); s.print(l);
}

void drawTrivia(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  switch(triviaPage){
    case TRV_SETUP:    drawTriviaSetup(s); break;
    case TRV_LOADING:  drawTriviaLoading(s); break;
    case TRV_QUESTION: drawTriviaQuestion(s); break;
    case TRV_RESULT:   drawTriviaResult(s); break;
    case TRV_ERROR:    drawTriviaError(s); break;
  }
  drawBack(s);
  drawToast(s);
}

// Ketuk sekali (tap) di halaman Setup - dipanggil dari loop() saat lepas
// jari & terdeteksi BUKAN gerakan drag (lihat blok khusus SCR_TRIVIA di loop()).
void triviaHandleSetupTap(int x,int y){
  if(isBack(x,y)){ navBack(); return; }
  TriviaLayout L = triviaCalcLayout();
  int top=triviaCatTop(), bottom=L.catBottom;
  if(y>=top && y<=bottom){
    int cols=triviaCatCols(), cw=triviaCatCardW(), ch=triviaCatCardH(), gap=6;
    for(int i=0;i<TRIVIA_CAT_COUNT;i++){
      int col=i%cols, row=i/cols;
      int ax=gap+col*(cw+gap), ay=top+row*(ch+gap)-(int)triviaCatScrollY;
      if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch){ triviaSelCat=i; needRedraw=true; return; }
    }
    return;
  }
  if(y>=L.diffY && y<=L.diffY+30){
    int dw=(SCR_W-16)/4;
    for(int i=0;i<4;i++){ int bx=8+i*dw; if(x>=bx&&x<=bx+dw-4){ triviaSelDiff=i; needRedraw=true; return; } }
    return;
  }
  if(y>=L.amtY && y<=L.amtY+30){
    int aw=(SCR_W-16)/3;
    for(int i=0;i<3;i++){ int bx=8+i*aw; if(x>=bx&&x<=bx+aw-4){ triviaSelAmount=i; needRedraw=true; return; } }
    return;
  }
  if(y>=L.startY && y<=L.startY+28){ triviaStartFetch(); return; }
}

void triviaQuestionTouch(int x,int y){
  if(triviaAnswered || triviaCur<0 || triviaCur>=triviaCount) return;
  for(int i=0;i<4;i++){
    int bx,by,bw,bh; triviaAnsRect(i,bx,by,bw,bh);
    if(x>=bx&&x<=bx+bw&&y>=by&&y<=by+bh){
      triviaSelected=i; triviaAnswered=true; triviaAnswerAtMs=millis();
      bool correct = (i==triviaQs[triviaCur].correctIdx);
      if(correct){ triviaScore+=10; triviaCorrectCount++; }
      String badge = String(triviaCorrectCount)+"/"+String(triviaCount);
      diNotify(correct?'Y':'W', correct?"Jawaban benar!":"Kurang tepat",
                correct?T().good:T().danger, 1300, true, badge); // v17: subtitle "Skor: X" dihapus
      needRedraw=true;
      return;
    }
  }
}
void triviaResultTouch(int x,int y){
  if(y>=STATUS_H+96 && y<=STATUS_H+122){ triviaStartFetch(); return; }
  if(y>=STATUS_H+128 && y<=STATUS_H+154){ triviaPage=TRV_SETUP; needRedraw=true; return; }
}
void triviaErrorTouch(int x,int y){
  int by=STATUS_H+36+6*11+10;
  if(y>=by && y<=by+26){ triviaPage=TRV_SETUP; needRedraw=true; return; }
}

// Dispatch generik (dipanggil loop() persis spt app lain: tap-on-press).
// Halaman TRV_SETUP TIDAK lewat sini -- ditangani terpisah di loop() krn
// butuh drag-scroll+tap (lihat blok khusus SCR_TRIVIA).
void triviaTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  switch(triviaPage){
    case TRV_QUESTION: triviaQuestionTouch(x,y); break;
    case TRV_RESULT:   triviaResultTouch(x,y); break;
    case TRV_ERROR:    triviaErrorTouch(x,y); break;
    default: break;
  }
}

void setup(){
  Serial.begin(115200);
  display.init();

  // =============================================================
  // FIX v9 (BUG: lock screen tidak bisa diusap ke atas setelah kalibrasi)
  // -----------------------------------------------------------------
  // AKAR MASALAH: sebelumnya display.setRotation() SELALU dipaksa ke
  // rotasi 0 (portrait) di titik ini, lalu KALIBRASI TOUCH dijalankan
  // dalam rotasi portrait tsb. Orientasi "final" yang benar-benar
  // dipakai perangkat (dibaca lewat loadOrientPref() dari NVS) baru
  // diterapkan BELAKANGAN lewat applyOrientation(). Kalau ternyata
  // orientasi tersimpan == LANDSCAPE (mis. peninggalan dari sesi atau
  // firmware sebelumnya yang defaultnya landscape), maka: kalibrasi
  // terjadi di rotasi 0, tapi tampilan (termasuk lock screen) berjalan
  // di rotasi 1. Kalibrasi XPT2046 di LovyanGFX terikat ke rotasi yang
  // aktif SAAT kalibrasi dilakukan, jadi mismatch ini bikin sumbu X/Y
  // touch tertukar -> usapan fisik ke ATAS di lock screen malah
  // terbaca sebagai gerakan ke SAMPING oleh lockScreenInput() (yang
  // cuma mengecek dy), sehingga unlock tidak pernah ter-trigger.
  // Persis seperti kode lama (v6) yang SELALU setRotation(1) dulu
  // SEBELUM kalibrasi, dan default orientasinya juga landscape ->
  // rotasi kalibrasi & rotasi runtime selalu sama-sama landscape,
  // jadi tidak pernah kena bug ini.
  //
  // FIX: baca dulu orientasi tersimpan (savedOrient), terapkan
  // rotasinya SEBELUM boot sequence & kalibrasi dijalankan. Dengan
  // begitu, apapun orientasi yang akan dipakai runtime (portrait
  // ATAU landscape), kalibrasi SELALU terjadi persis pada rotasi
  // yang sama -> tidak ada lagi mismatch, di orientasi apapun. Ini
  // aman karena ganti rotasi live (mis. lewat Control Center) sudah
  // terbukti tidak merusak hasil kalibrasi -- yang penting kalibrasi
  // AWAL-nya match dengan rotasi yang aktif saat itu.
  // =============================================================
  Orientation savedOrient = loadOrientPref();
  display.setRotation(savedOrient == ORIENT_LANDSCAPE ? 1 : 0);

  canvas.setPsram(true);
  canvas.createSprite(display.width(),display.height());

  runBootSequence(); // "Ren Phone" -> logo "SanzX OS" -> transisi ke UI (skrg ikut orientasi tersimpan)

  display.setBrightness(brightness);
  if(display.touch())loadOrRunCalibration(); // kalibrasi SEKARANG selalu match dgn rotasi runtime
  loadTheme();
  initAppColors();
  drawColor=T().text;
  initSD();
  loadWifiCreds();
  connectWifi();
  loadNote();
  loadGeminiKey();
  canvasApp.setPsram(true);
  canvasApp.createSprite(320,240-STATUS_H-44);
  canvasApp.fillSprite(T().bg);
  applyOrientation(savedOrient, false); // rapikan SCR_W/H, canvas & canvasApp (rotasi sudah benar dari awal)
  locked = true;
  needRedraw = true;

  mpuInit();                                // MPU6050 (SDA=15, SCL=7)
  loadMpuCal();                              // BARU v10: muat offset kalibrasi tersimpan (kalau ada)
  autoRotateEnabled = loadAutoRotatePref();
  shakeEnabled = loadShakePref();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);           // biar analogRead mentok di ~3.3V
  battUpdate();

  if(sdReady && !SD_MMC.exists(MJPEG_FOLDER)){
    SD_MMC.mkdir(MJPEG_FOLDER);
  }
  if(sdReady && !SD_MMC.exists(UPDATE_FOLDER)){
    SD_MMC.mkdir(UPDATE_FOLDER);
  }

  Serial.printf("[Boot] FreePsram=%u FreeInternal=%u LargestInternalBlock=%u\n",
                ESP.getFreePsram(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// =============================================
// LOOP
// =============================================
unsigned long lastClk=0,lastSen=0,lastMpu=0,lastBatt=0;
unsigned long lastLoopMs=0; // v14: dipakai normalisasi decay momentum scroll thdp waktu nyata (dt)
void loop(){
  unsigned long nowMs=millis();
  float dt = lastLoopMs? (float)(nowMs-lastLoopMs) : 16.0f;
  if(dt>100.0f) dt=100.0f; // jaga2 abis jeda blocking panjang (mis. persis setelah boot)
  lastLoopMs = nowMs;

  if(!gameModeActive){
    if(millis()-lastMpu>50){ lastMpu=millis(); mpuUpdate(); }
    if(millis()-lastBatt>5000){ lastBatt=millis(); battUpdate(); needRedraw=true; }
  }

  if(wifiConnected && webServerRunning){
    webServer.handleClient();
  }

  checkAiWatchdog();
  diUpdate();              // v14: urus animasi/auto-collapse Dynamic Island
  triviaPeriodicUpdate();  // v14: urus spinner loading & auto-lanjut soal Trivia

  lgfx::touch_point_t tp;
  bool touched=display.getTouch(&tp);
  int tx=touched?(int)tp.x:0,ty=touched?(int)tp.y:0;
  bool newT=touched&&!wasTouched;

  if(ccAnimating){
    // FIX v11: animasi berbasis waktu (bukan formula per-frame ad-hoc) +
    // easing navEase yg sama dipakai transisi antar layar -> gerakannya
    // mulus & konsisten kecepatannya berapa pun framerate loop() saat ini.
    float t = (millis()-ccAnimStartMs)/CC_ANIM_MS;
    if(t>=1.0f){
      t=1.0f;
      ccAnimating=false;
      if(ccAnimToH<=0.0f) controlCenterOpen=false;
    }
    ccOffset = ccAnimFromH + (ccAnimToH-ccAnimFromH)*navEase(t);
    needRedraw=true;
  }

  if(locked){
    lockScreenInput(touched,newT,tx,ty);
    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }
    wasTouched=touched; delay(8); return;
  }

  if(controlCenterOpen){
    if(newT) ccTouch(tx,ty);
    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }
    wasTouched=touched; delay(8); return;
  }

  if(newT){
    gStartX=tx; gStartY=ty; gGestureDone=false;
    if(diHitTest(tx,ty)){ diHandleTap(); gGestureDone=true; } // v14: ketuk Dynamic Island
  }
  if(touched && !gGestureDone && !kbVisible){
    int dy=ty-gStartY, dx=tx-gStartX;
    if(gStartY<STATUS_H+6 && dy>28 && abs(dy)>abs(dx)){
      openControlCenter(); gGestureDone=true;
      wasTouched=touched; delay(8); return;
    }
    if(gStartY>SCR_H-16 && dy<-28 && abs(dy)>abs(dx) && curScreen()!=SCR_HOME){
      navGoHome(); gGestureDone=true;
      wasTouched=touched; delay(8); return;
    }
  }

  if(curScreen()==SCR_HOME){
    if(touched){
      if(!wasTouched){
        touchStartX=tx;touchStartY=ty;touchLastY=ty;
        isSwiping=false;swipeStartTime=millis();
        homeScrollVel=0; // FIX v11: reset residual velocity di awal sentuhan baru
      } else {
        if(abs(ty-touchStartY)>12)isSwiping=true;
        if(isSwiping){
          float rawDelta=(float)(touchLastY-ty);
          // FIX v11: velocity di-low-pass (bukan diganti mentah tiap
          // frame) supaya gerakan drag terasa lebih mulus & tidak
          // "patah" saat jari sedikit goyang antar sample sentuhan.
          homeScrollVel = homeScrollVel*0.5f + rawDelta*0.5f;
          homeScrollY += rawDelta;
          // FIX v11: sedikit efek elastis di batas atas/bawah drpd
          // constrain keras, biar transisi ke ujung list terasa halus.
          if(homeScrollY<0) homeScrollY*=0.5f;
          float maxS=(float)homeMaxScroll();
          if(homeScrollY>maxS) homeScrollY = maxS + (homeScrollY-maxS)*0.5f;
          needRedraw=true;
        }
        touchLastY=ty;
      }
    } else if(wasTouched){
      if(!isSwiping&&millis()-swipeStartTime<400){
        Screen nx=homeCheck(touchStartX,touchStartY,homeScrollY);
        if(nx!=SCR_HOME) navPush(nx);
      }
      homeScrollY=constrain(homeScrollY,0.0f,(float)homeMaxScroll());
    } else {
      if(fabsf(homeScrollVel)>0.3f){
        homeScrollY+=homeScrollVel;
        // v14: decay dinormalisasi thdp dt (bukan konstanta per-iterasi) --
        // pola sama dgn fix playNavTransition() di v13, biar kecepatan
        // momentum berhenti KONSISTEN di kondisi hardware apapun (dulu loop
        // yg lg berat bikin decay kerasa lbh lambat krn iterasi/detik turun).
        homeScrollVel*=powf(0.92f, dt/16.0f);
        homeScrollY=constrain(homeScrollY,0.0f,(float)homeMaxScroll());
        needRedraw=true;
      } else {
        homeScrollVel=0;
      }
    }
    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}

  } else if(curScreen()==SCR_TRIVIA && triviaPage==TRV_SETUP){
    // v14: halaman pilih tema Trivia butuh drag-scroll+tap spt Home (bukan
    // sekadar tap-on-press spt app lain), jadi ditangani khusus di sini
    // persis pola Home -- dispatch generik apps[idx].touch() di bawah cuma
    // dikirim pas TOUCH-DOWN/HELD (tanpa event lepas), jadi gak bisa bedain
    // niat "tap pilih kartu" vs "mulai drag scroll".
    if(touched){
      if(!wasTouched){
        triviaTouchStartX=tx; triviaTouchStartY=ty; triviaTouchLastY=ty;
        triviaIsSwiping=false; triviaSwipeStartTime=millis();
      } else {
        if(abs(ty-triviaTouchStartY)>10) triviaIsSwiping=true;
        TriviaLayout TL = triviaCalcLayout();
        if(triviaIsSwiping && triviaTouchStartY>=triviaCatTop() && triviaTouchStartY<=TL.catBottom){
          float rawDelta=(float)(triviaTouchLastY-ty);
          triviaCatScrollY += rawDelta;
          if(triviaCatScrollY<0) triviaCatScrollY*=0.5f;
          float maxS=(float)triviaCatMaxScroll();
          if(triviaCatScrollY>maxS) triviaCatScrollY = maxS+(triviaCatScrollY-maxS)*0.5f;
          needRedraw=true;
        }
        triviaTouchLastY=ty;
      }
    } else if(wasTouched){
      if(!triviaIsSwiping && millis()-triviaSwipeStartTime<400){
        triviaHandleSetupTap(triviaTouchStartX,triviaTouchStartY);
      }
      triviaCatScrollY=constrain(triviaCatScrollY,0.0f,(float)triviaCatMaxScroll());
      needRedraw=true;
    }
    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}

  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0){
      bool held = touched&&wasTouched;
      if(curScreen()==SCR_CANVAS){
        if(touched){ apps[idx].touch(tx,ty,held,newT); }
      } else {
        if(newT) apps[idx].touch(tx,ty,false,true);
        else if(touched && held) apps[idx].touch(tx,ty,true,false);
      }
    }
    if(curScreen()==SCR_CLOCK && millis()-lastClk>500){ needRedraw=true; lastClk=millis(); }
    if(curScreen()==SCR_SENSOR && millis()-lastSen>50){ needRedraw=true; lastSen=millis(); }
    bool inActionGame = curScreen()==SCR_SNAKE || curScreen()==SCR_FLAPPY || curScreen()==SCR_BREAKOUT;
    if(inActionGame && millis()-lastSen>40){ needRedraw=true; lastSen=millis(); }
    if(curScreen()==SCR_AICHAT && aiLoading){ needRedraw=true; }
    if(curScreen()==SCR_TRIVIA && triviaPage==TRV_LOADING){ needRedraw=true; } // v14: animasi spinner

    // v12: momentum scroll halus utk teks (AI Chat & isi file di File
    // Explorer) selagi jari SUDAH dilepas -> gerakannya jalan terus lalu
    // melambat pelan-pelan, senada dgn gaya momentum scroll Home.
    if(!touched){
      if(curScreen()==SCR_AICHAT && !kbVisible){
        if(fabsf(aiRespScrollVel)>0.3f){
          aiRespScrollY = constrain(aiRespScrollY + aiRespScrollVel, 0.0f, (float)aiRespMaxScroll);
          aiRespScrollVel *= powf(0.92f, dt/16.0f); // v14: decay dinormalisasi dt (lihat catatan di blok Home)
          needRedraw = true;
        } else {
          aiRespScrollVel = 0;
        }
      }
      if(curScreen()==SCR_FILEEXPLORER && expViewFileName.length()>0){
        if(fabsf(expViewScrollVel)>0.3f){
          expViewScrollY = constrain(expViewScrollY + expViewScrollVel, 0.0f, (float)expViewMaxScroll);
          expViewScrollVel *= powf(0.92f, dt/16.0f); // v14: decay dinormalisasi dt
          needRedraw = true;
        } else {
          expViewScrollVel = 0;
        }
      }
    }

    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}
  }
  wasTouched=touched;
  delay(8);
}
