#pragma once
// =====================================================================
// nyx_abi.h -- kontrak biner NyxVM (ISA, peta memori, syscall, kode galat)
// =====================================================================
// SUMBER TUNGGAL: dibaca oleh nyxvm_core.h / nyxsys.h (C++) DAN oleh
// tools/nyxc.py (compiler + assembler). Baris "#define NYX_SYS_xxx N /* args=K */"
// di-parse langsung oleh nyxc.py, jadi JANGAN ubah format komentar itu.
// File ini murni makro (tanpa tipe), aman di-include dari mana saja.
// =====================================================================

// ---- halaman & ruang alamat virtual (per proses) ----
#define NYX_PAGE_SHIFT     12
#define NYX_PAGE_SIZE      4096
#define NYX_VA_BITS        24                       // 16 MB per proses
#define NYX_VA_SIZE        (1u << NYX_VA_BITS)
#define NYX_NPAGES         (1u << (NYX_VA_BITS - NYX_PAGE_SHIFT))   // 4096 halaman

// ---- peta alamat konvensi ----
#define NYX_NULL_GUARD_END 0x001000u   // halaman 0 tidak dipetakan: NULL deref => fault
#define NYX_CODE_BASE      0x001000u
#define NYX_HEAP_LIMIT     0x800000u   // heap tidak boleh melewati ini
#define NYX_STACK_TOP      0xF00000u   // stack tumbuh turun dari sini

// ---- register ----
#define NYX_REG_FP   12
#define NYX_REG_SP   13
#define NYX_REG_LR   14
#define NYX_REG_ZERO 15
// konvensi panggilan NyxC: argumen r0..r5, hasil r0 (+r1,r2 utk syscall multi-hasil),
// r0..r5 & r6..r11 caller-saved, FP/SP/LR dikelola prolog/epilog.

// ---- file image .nyx ----
#define NYX_IMG_MAGIC    0x3158594Eu   // 'N' 'Y' 'X' '1' (little-endian)
#define NYX_IMG_VERSION  1
#define NYX_SNAP_MAGIC   0x5358594Eu   // 'N' 'Y' 'X' 'S'
#define NYX_SNAP_VERSION 1

// flags header image
#define NYX_F_HIBERNATE  0x0001        // app minta di-hibernate saat ditinggalkan

// izin (perms) di header image -- dicek di lapisan syscall
#define NYX_PERM_FS      0x0001        // folder data /apps/<id>/data
#define NYX_PERM_NET     0x0002        // HTTP(S) GET async
#define NYX_PERM_TILT    0x0004        // accelerometer MPU6050
#define NYX_PERM_NOTIFY  0x0008        // toast/notifikasi
#define NYX_PERM_AUDIO   0x0010        // dicadangkan (belum ada syscall)
#define NYX_PERM_CAMERA  0x0020        // dicadangkan (belum ada syscall)

// ---- kode galat syscall (nilai balik negatif) ----
#define NYX_E_PERM   (-1)
#define NYX_E_INVAL  (-2)
#define NYX_E_NOENT  (-3)
#define NYX_E_NOMEM  (-4)
#define NYX_E_IO     (-5)
#define NYX_E_BUSY   (-6)
#define NYX_E_FAULT  (-7)              // pointer argumen tidak valid
#define NYX_E_NOSYS  (-38)

// ---- kode fault proses ----
#define NYX_FAULT_NONE      0
#define NYX_FAULT_BADADDR   1          // alamat tak terpetakan / di luar ruang alamat
#define NYX_FAULT_PROT      2          // izin R/W/X dilanggar
#define NYX_FAULT_ALIGN     3
#define NYX_FAULT_ILLEGAL   4          // opcode tidak dikenal
#define NYX_FAULT_DIVZERO   5
#define NYX_FAULT_BREAK     6          // instruksi BRK
#define NYX_FAULT_OOM       7          // tidak ada frame / swap penuh
#define NYX_FAULT_IO        8          // gagal baca image/swap/snapshot
#define NYX_FAULT_STACK     9          // stack overflow (kena halaman penjaga)
#define NYX_FAULT_BADIMG    10
#define NYX_FAULT_WATCHDOG  11         // diset oleh host
#define NYX_FAULT_BADSYS    12

// ---- tabel syscall (NyxSys) ----
// FORMAT BARIS DIPARSE nyxc.py: #define NYX_SYS_<NAMA> <nomor> /* args=<n> */
#define NYX_SYS_EXIT        0   /* args=1 */   // exit(code)
#define NYX_SYS_YIELD       1   /* args=0 */   // akhiri frame ini
#define NYX_SYS_TIME_MS     2   /* args=0 */   // r0 = millis()
#define NYX_SYS_RAND        3   /* args=0 */   // r0 = acak 32-bit
#define NYX_SYS_LOG         4   /* args=2 */   // log(ptr,len)
#define NYX_SYS_SBRK        5   /* args=1 */   // r0 = brk lama / -4
#define NYX_SYS_SCREEN      6   /* args=0 */   // r0=lebar r1=tinggi viewport
#define NYX_SYS_SEED        7   /* args=1 */
#define NYX_SYS_STATS       8   /* args=1 */   // 0 page-in,1 swap-in,2 swap-out,3 rss(hal),4 tlb-miss,5 frame total

#define NYX_SYS_CLEAR       16  /* args=1 */
#define NYX_SYS_FILL_RECT   17  /* args=5 */
#define NYX_SYS_RECT        18  /* args=5 */
#define NYX_SYS_PIXEL       19  /* args=3 */
#define NYX_SYS_LINE        20  /* args=5 */
#define NYX_SYS_CIRCLE      21  /* args=4 */
#define NYX_SYS_RING        22  /* args=4 */
#define NYX_SYS_TEXT        23  /* args=5 */   // text(x,y,str,color,size)
#define NYX_SYS_BLIT        24  /* args=5 */   // blit(x,y,w,h,pixels565)
#define NYX_SYS_RGB         25  /* args=3 */
#define NYX_SYS_ROUND_RECT  26  /* args=6 */
#define NYX_SYS_TEXT_W      27  /* args=2 */   // lebar teks (px)

#define NYX_SYS_TOUCH       32  /* args=0 */   // r0=bit(1 down,2 baru,4 lepas) r1=x r2=y
#define NYX_SYS_TILT        33  /* args=0 */   // r0=status(0 ok / galat<0), r1,r2,r3 = ax,ay,az * 1000 (g)

#define NYX_SYS_FOPEN       40  /* args=2 */   // fopen(path, mode 0 baca,1 tulis,2 tambah)
#define NYX_SYS_FREAD       41  /* args=3 */
#define NYX_SYS_FWRITE      42  /* args=3 */
#define NYX_SYS_FCLOSE      43  /* args=1 */
#define NYX_SYS_FSEEK       44  /* args=2 */
#define NYX_SYS_FSIZE       45  /* args=1 */
#define NYX_SYS_FEXISTS     46  /* args=1 */
#define NYX_SYS_FREMOVE     47  /* args=1 */

#define NYX_SYS_TOAST       56  /* args=1 */

#define NYX_SYS_HTTP_GET    64  /* args=3 */   // http_get(url, buf, cap) -> id
#define NYX_SYS_HTTP_STATE  65  /* args=1 */   // r0=state(0 kosong,1 jalan,2 selesai,3 gagal) r1=kode HTTP r2=byte
#define NYX_SYS_HTTP_CLOSE  66  /* args=1 */

// kode balik handler syscall ke interpreter
#define NYX_SYSRC_CONT   0
#define NYX_SYSRC_YIELD  1
#define NYX_SYSRC_EXIT   2
