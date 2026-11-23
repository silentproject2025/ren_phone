# SysMem — manajer memori terpusat untuk firmware utama ("MMU software")

ESP32-S3 tidak punya MMU/MPU yang bisa memisahkan fitur firmware, jadi SysMem **tidak** memberi
proteksi alamat. Yang disediakan adalah *manajemen*: siapa memakai berapa, batas per fitur, lantai
cadangan RAM internal, dan satu jalur reclaim untuk semua fitur. (Untuk Nyx Apps ada `nyxmem.h`, terpisah.)

| Fungsi | Cara kerja |
|---|---|
| Pemilik | tiap `smAlloc(owner, n, flags)` dicatat per owner dan region (RAM internal / PSRAM), termasuk puncak |
| Anggaran | `setOwner(id, nama, budgetInt, budgetPsram, priv)`; lewat batas → `nullptr` (0 = tanpa batas) |
| Cadangan | owner biasa tidak boleh menurunkan free internal < 24 KB atau blok kontigu < 20 KB; owner `priv` (system/net/tls) boleh |
| Reclaim | callback berprioritas; dipanggil **satu per satu sampai berhasil**; callback milik peminta dilewati; batas 64 langkah |
| Gate | `smWaitContig(need, timeoutMs, skipOwner)` → tunggu/reclaim blok kontigu (TLS, stack task, DMA) |
| Deteksi | header + canary; double free / header rusak diabaikan (bocor, bukan merusak heap) |
| Laporan | `smDump()` ke Serial; bagian **untracked** = kode yang masih memakai `malloc`/`new` langsung |

## Batas yang jujur
* Hanya alokasi lewat `smAlloc` yang terlacak. DOOM, nofrendo, mbedTLS dan `malloc`/`new` biasa muncul sebagai *untracked*.
* Overhead 16 B header + 4 B canary per blok → untuk buffer ≥ 256 B. Payload sejajar 4 B; buffer DMA/SIMD tetap `heap_caps_aligned_alloc`.
* `smFree` hanya untuk pointer dari `smAlloc` (tanpa tabel pointer, demi hemat RAM).
* Reclaim bawaan: korban LRU app background (`onLowMem`), sama dengan App Memory Guard. App Memory Guard lama tetap ada; SysMem menambah pemicu berbasis **blok kontigu**.
* RAM internal: `Mgr` ≈ 1,5 KB BSS.

## Status verifikasi
| Bagian | Status |
|---|---|
| `sysmem.h` (logika inti) | **Teruji di PC**: ±77 ribu cek, fuzz 5 seed, ASan + UBSan, 7 tes mutasi |
| `sysmem_renphone.ino` (glue) | **Cek sintaks saja** (stub). Belum dijalankan di HP |
| `tools/patches/apply_sysmem_patch.py` | Anchor cocok & idempoten pada `phone.ino` repo ini |

Tes: `./tests/sysmem/run.sh` (butuh `g++`).

## Memasang
```bash
python3 tools/patches/apply_sysmem_patch.py --check   # cek anchor
python3 tools/patches/apply_sysmem_patch.py           # pasang (cadangan: phone.ino.bak_sysmem)
```
CI (`build-firmware.yml`) sudah menyalin `sysmem.h`, `sysmem_fw.h`, `sysmem_renphone.ino` ke sketch.

## Memigrasi satu fitur (contoh)
```cpp
// sebelum
if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 30000) { ... gagal ... }
// sesudah: reclaim dulu, baru menyerah
if (!smWaitContig(30000, 1500, SM_O_GAME)) { ... gagal ... }

uint8_t* buf = (uint8_t*)smAlloc(SM_O_BOOKS, 64 * 1024, sysmem::F_PSRAM);
...
smFree(buf);
```

## Cek di HP (urutan disarankan)
1. Build + flash → boot normal → buka beberapa app → `smDump()` tampil di Serial tanpa reboot.
2. Pantau `untracked` vs `tracked`: sebelum migrasi hampir semua *untracked* — itu wajar.
3. Migrasikan satu fitur (mis. Gutenberg: loop tunggu TLS di `gutenberg_renphone.ino` ±baris 486) → `[SysMem] reclaim:` muncul saat RAM mepet.
4. Setelah ada beberapa owner, isi `budget` per owner dari puncak yang terlihat di `smDump()`.

## Cadangan kontigu (anti-fragmentasi)
Heap ESP32 tidak bisa di-defrag, jadi `sysmem` memegang **satu blok RAM internal** (default 32 KB, `-DRP_CONTIG_HOLD_KB=0` mematikannya; margin ambil-ulang 4 KB, jeda 1 dtk) selagi heap masih rapi, lalu **melepasnya otomatis** saat ada yang butuh blok kontigu.
- Diambil di `setup()` tepat setelah `connectWifi()` (`smHoldTake()`), dan oleh `smTick()` kalau free internal ≥ 64 KB dan blok terbesar ≥ hold + 12 KB. Setelah dilepas ada jeda 1 dtk sebelum bisa diambil lagi (FRAG_V2).
- Dilepas **sebelum** reclaim cache app (murah, tidak terlihat user): `smWaitContig()`/`contigNow()`, `smEnsureContig(need)` (cek blok telepon, catur, backup cloud, devreg), tunggu TLS Gutenberg, dan sebelum radio WiFi/AP dinyalakan (`connectWifi()`, `remote_renphone.ino`).
- `pressure()` dan App Memory Guard menghitung cadangan sebagai ruang yang tersedia, jadi cadangan sendiri tidak memicu status LOW/CRITICAL.
- `smDump()` menampilkan baris `hold=24K on=1 take=N rel=N`.
- `rpAllocPs(n)` (di `sysmem_fw.h`): alokasi PSRAM dulu, jatuh ke heap biasa. Jangan dipakai untuk buffer yang diisi DMA dari SD (SD SPI di Aether).
