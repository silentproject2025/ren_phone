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
