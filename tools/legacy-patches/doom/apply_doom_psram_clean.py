# DOOM_PSRAM_CLEAN_V1: "PSRAM kontigu tidak cukup utk DOOM" sering muncul krn PSRAM terfragmentasi
# (blok kontigu terbesar < 6 MB walau total bebas cukup). Patch ini membersihkan cache yg aman dibuang
# SEBELUM cek blok kontigu, bertahap, lalu cek ulang tiap langkah:
#   langkah 1 (ringan): cache Home + semua preview kartu App Switcher (PSRAM, kecil2 & tersebar -> biang fragmentasi)
#   langkah 2 (hanya kalau masih kurang): buffer gambar NASA di RAM (nasaRamImgBuf, bisa 1-5 MB)
#       -> app NASA nanti memuat ulang gambarnya dari SD/jaringan kalau dibuka lagi
# Pesan error juga menampilkan angka sebelum dan sesudah dibersihkan + total PSRAM bebas (dan dicatat di Serial),
# supaya kelihatan apakah masalahnya fragmentasi atau memang kehabisan PSRAM.
# Jalankan dari root repo:  python apply_doom_psram_clean.py
import sys, os
f = 'doom_renphone.ino'
if not os.path.exists(f):
    print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
s = open(f, encoding='utf-8').read()
if 'DOOM_PSRAM_CLEAN_V1' in s:
    print("Sudah terpasang, dilewati"); sys.exit(0)

def rep(s, old, new, name):
    if s.count(old) != 1:
        print("ERROR [%s]: anchor ketemu %d kali: %r" % (name, s.count(old), old[:70])); sys.exit(1)
    return s.replace(old, new)

# 1) fungsi pembersih, ditaruh tepat sebelum doomEnter()
s = rep(s, "\nvoid doomEnter(){\n", """
// DOOM_PSRAM_CLEAN_V1: lepas cache PSRAM yg aman dibuang supaya blok kontigu terbesar membesar lagi.
//   level 1 = cache Home + preview App Switcher (kecil2 tapi tersebar -> memecah blok besar)
//   level 2 = level 1 + buffer gambar NASA di RAM (app NASA memuat ulang kalau dibuka lagi)
static void doomPsramClean(int level){
  homeCacheFree();
#ifdef SW_THUMB_SLOTS
  for(int i=0;i<SW_THUMB_SLOTS;i++) swThumbFree(i);
#endif
  if(level >= 2) nasaFreeRamImgBuf();
}

void doomEnter(){
""", 'fn')

# 2) cek blok kontigu: bersihkan bertahap sebelum menyerah
s = rep(s, """  size_t freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if(freeBlock < (size_t)6*1024*1024){
    doomErrorMsg = "PSRAM kontigu tidak cukup utk DOOM\\n(butuh ~6MB, tersedia " +
                   String(freeBlock/1024) + "KB).\\nTutup app lain / restart device.";
    return;
  }
""", """  size_t freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  // DOOM_PSRAM_CLEAN_V1: bersihkan bertahap, cek ulang tiap langkah (langkah 2 hanya kalau langkah 1 belum cukup)
  const size_t doomNeedBlk = (size_t)6*1024*1024;
  const size_t doomBlk0 = freeBlock;
  const size_t doomNasaKB = nasaRamImgLen/1024;
  int doomCleanLv = 0;
  if(freeBlock < doomNeedBlk){ doomPsramClean(1); doomCleanLv = 1; freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
  if(freeBlock < doomNeedBlk){ doomPsramClean(2); doomCleanLv = 2; freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
  Serial.printf("[doom] PSRAM kontigu: %uKB -> %uKB (bersih lv%d) | total bebas %uKB | butuh %uKB | gambar NASA %uKB\\n",
                (unsigned)(doomBlk0/1024), (unsigned)(freeBlock/1024), doomCleanLv,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024), (unsigned)(doomNeedBlk/1024), (unsigned)doomNasaKB);
  if(freeBlock < doomNeedBlk){
    doomErrorMsg = "PSRAM kontigu tidak cukup utk DOOM\\n(butuh ~6MB, tersedia " + String(freeBlock/1024) +
                   "KB;\\nsebelum dibersihkan " + String(doomBlk0/1024) + "KB, total bebas " +
                   String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024) + "KB).\\nTutup app lain / restart device.";
    return;
  }
""", 'check')

open(f, 'w', encoding='utf-8').write(s)
print("OK: pembersihan PSRAM bertahap sebelum DOOM terpasang")
