# Longgarkan ambang RAM TLS untuk Gemini + tampilkan angka RAM di pesan error.
# Jalankan di folder proyek:  python3 apply_gemini_tls_patch.py
# Alasan: library kustom memakai CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y (buffer TLS di PSRAM),
# jadi syarat 1 blok internal >= 20 KB (ditulis utk mbedTLS di RAM internal) terlalu ketat.
import sys
s=open('phone.ino',encoding='utf-8').read()
if 'MIN_BLOCK_FOR_TLS = 12000' in s:
    print("Patch sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

s=rep(s,"  const size_t MIN_BLOCK_FOR_TLS = 20000;\n","  const size_t MIN_BLOCK_FOR_TLS = 12000; // diturunkan dr 20000: mbedTLS sudah di PSRAM (lib kustom)\n","MIN_BLOCK_FOR_TLS")
s=rep(s,"  const size_t MIN_BLOCK_TO_START = 20000;\n","  const size_t MIN_BLOCK_TO_START = 12000; // diturunkan dr 20000 (lihat catatan di atas)\n","MIN_BLOCK_TO_START")
s=rep(s,'    outResponse = "Error: RAM internal terlalu terfragmentasi/rendah utk TLS. Tutup app lain / restart perangkat.";\n',
'''    char eb[200];
    snprintf(eb, sizeof(eb), "Error: RAM internal terlalu terfragmentasi/rendah utk TLS (blok %u KB, bebas %u KB, perlu %u KB). Tutup app lain / restart perangkat.",
             (unsigned)(lb/1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024), (unsigned)(MIN_BLOCK_FOR_TLS/1024));
    outResponse = eb;
''',"error TLS")
s=rep(s,'    aiResponse = "Error: RAM internal kritis rendah. Tutup app lain / restart perangkat.";\n',
'''    {
      char eb[200];
      snprintf(eb, sizeof(eb), "Error: RAM internal kritis rendah (blok %u KB, bebas %u KB, perlu %u KB). Tutup app lain / restart perangkat.",
               (unsigned)(largestBlock/1024), (unsigned)(freeInternal/1024), (unsigned)(MIN_BLOCK_TO_START/1024));
      aiResponse = eb;
    }
''',"error kritis")
open('phone.ino','w',encoding='utf-8').write(s)
print("OK: ambang TLS Gemini 20 KB -> 12 KB, pesan error kini menampilkan angka RAM.")
