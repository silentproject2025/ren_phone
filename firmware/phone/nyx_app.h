#ifndef NYX_APP_H
#define NYX_APP_H
/* =====================================================================
 * nyx_app.h -- SDK app NATIVE NyxOS  (C dan C++; dipakai app DAN firmware)
 * =====================================================================
 * App native = file ELF (Xtensa) yang dimuat dari SD saat dijalankan.
 * Ia TIDAK memanggil firmware langsung, hanya lewat tabel fungsi NyxApi
 * yang diberikan host. Kontrak:
 *
 *   int main(int argc, char** argv)       <- titik masuk ELF
 *       api = nyx_api_from_args(argc, argv);
 *       api->set_ops(&ops);               <- daftarkan enter/frame/leave
 *       return 0;                         <- kembali SEGERA
 *   (makro NYX_APP_GLOBALS + NYX_APP_MAIN di bawah membuatkan main() ini)
 *
 *   ops.enter()  sekali saat app dibuka (return 0 = ok)
 *   ops.frame()  tiap frame selagi di layar; HARUS cepat (target < 30 ms,
 *                batas keras ~250 ms -> app di-kill watchdog)
 *   ops.leave()  sekali saat ditutup (bebaskan apa pun yang kamu punya)
 *
 * Memori: pakai api->mem_alloc/free (arena app dengan quota). Alokasi lewat
 * libc/malloc langsung TIDAK dihitung dan tidak dibebaskan otomatis.
 * Koordinat gambar relatif ke viewport app (0,0 = kiri-atas area app).
 *
 * MODEL KEPERCAYAAN (jujur): app native berjalan di ruang alamat yang sama
 * dengan firmware dan memakai pointer asli. Kuota memori, sandbox file,
 * izin, dan watchdog melindungi dari app yang SALAH, bukan dari app yang
 * JAHAT. Pasang hanya app yang kamu percaya.
 * ===================================================================== */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NYX_API_MAGIC    0x3150414Eu   /* 'N' 'A' 'P' '1' */
#define NYX_API_VERSION  1
#define NYX_ENTRY_ARGC   0x4E59

/* izin (manifest: perms=fs,net,tilt,notify) */
#define NYX_PERM_FS      0x0001
#define NYX_PERM_NET     0x0002
#define NYX_PERM_TILT    0x0004
#define NYX_PERM_NOTIFY  0x0008

/* galat (nilai balik negatif) */
#define NYX_E_PERM   (-1)
#define NYX_E_INVAL  (-2)
#define NYX_E_NOENT  (-3)
#define NYX_E_NOMEM  (-4)
#define NYX_E_IO     (-5)
#define NYX_E_BUSY   (-6)

/* sentuh: bit state dari api->touch() */
#define NYX_TOUCH_DOWN      1
#define NYX_TOUCH_PRESSED   2
#define NYX_TOUCH_RELEASED  4

/* api->fopen mode */
#define NYX_FOPEN_READ    0
#define NYX_FOPEN_WRITE   1   /* potong/buat baru */
#define NYX_FOPEN_APPEND  2

/* api->http_state */
#define NYX_HTTP_IDLE     0
#define NYX_HTTP_RUNNING  1
#define NYX_HTTP_DONE     2
#define NYX_HTTP_FAILED   3

typedef struct NyxAppOps {
  int  (*enter)(void);
  void (*frame)(void);
  void (*leave)(void);
} NyxAppOps;

typedef struct NyxApi {
  uint32_t magic;            /* NYX_API_MAGIC */
  uint32_t version;          /* NYX_API_VERSION host */
  uint32_t size;             /* sizeof(NyxApi) host (untuk cek kompatibilitas) */
  uint32_t perms;            /* izin yang diberikan manifest */

  void (*set_ops)(const NyxAppOps* ops);
  void (*quit)(void);                        /* minta host menutup app ini */

  /* ---- memori (arena app, quota keras) ---- */
  void*  (*mem_alloc)(size_t n);             /* NULL jika quota habis */
  void*  (*mem_calloc)(size_t n);
  void*  (*mem_realloc)(void* p, size_t n);
  void   (*mem_free)(void* p);
  size_t (*mem_used)(void);
  size_t (*mem_quota)(void);
  size_t (*mem_largest)(void);               /* alokasi terbesar yang masih mungkin */

  /* ---- grafis (RGB565, koordinat viewport) ---- */
  int      (*screen_w)(void);
  int      (*screen_h)(void);
  uint16_t (*rgb)(int r, int g, int b);
  void (*clear)(uint16_t c);
  void (*fill_rect)(int x, int y, int w, int h, uint16_t c);
  void (*rect)(int x, int y, int w, int h, uint16_t c);
  void (*round_rect)(int x, int y, int w, int h, int r, uint16_t c);
  void (*pixel)(int x, int y, uint16_t c);
  void (*line)(int x0, int y0, int x1, int y1, uint16_t c);
  void (*circle)(int x, int y, int r, uint16_t c);        /* terisi */
  void (*ring)(int x, int y, int r, uint16_t c);          /* garis */
  void (*text)(int x, int y, const char* s, uint16_t c, int size);
  int  (*text_w)(const char* s, int size);
  void (*blit)(int x, int y, int w, int h, const uint16_t* px);

  /* ---- input ---- */
  int  (*touch)(int* x, int* y);             /* bit NYX_TOUCH_*; x,y boleh NULL */
  int  (*tilt)(int* x, int* y, int* z);      /* 1/1000 g; 0 = ok (butuh izin tilt) */

  /* ---- sistem ---- */
  uint32_t (*millis)(void);
  uint32_t (*rand)(void);
  void (*log)(const char* s);                /* ke Serial: [app] ... */
  void (*toast)(const char* s);              /* butuh izin notify */

  /* ---- file: HANYA folder data app (butuh izin fs). path relatif. ---- */
  int  (*fopen)(const char* path, int mode); /* handle 1..4 atau galat */
  int  (*fread)(int h, void* buf, int n);
  int  (*fwrite)(int h, const void* buf, int n);
  int  (*fclose)(int h);
  int  (*fseek)(int h, int pos);
  int  (*fsize)(int h);
  int  (*fexists)(const char* path);
  int  (*fremove)(const char* path);

  /* ---- jaringan: satu permintaan GET async dalam satu waktu (butuh izin net) ---- */
  int  (*http_get)(const char* url, void* buf, size_t cap);        /* id>0 atau galat */
  int  (*http_state)(int id, int* http_code, size_t* nbytes);      /* NYX_HTTP_* */
  void (*http_close)(int id);

  void* reserved[8];
} NyxApi;

/* ---------- pembantu sisi app ---------- */
static inline const NyxApi* nyx_api_from_args(int argc, char** argv) {
  const NyxApi* a;
  if (argc != NYX_ENTRY_ARGC || !argv || !argv[0]) return 0;
  a = (const NyxApi*)(void*)argv[0];
  if (a->magic != NYX_API_MAGIC || a->version < 1) return 0;
  return a;
}

/* Pola pakai (satu app per ELF):
 *     NYX_APP_GLOBALS                       <- di atas, sebelum fungsi-fungsimu
 *     static int  app_enter(void) {...}     <- pakai g_nyx->...
 *     static void app_frame(void) {...}
 *     static void app_leave(void) {...}
 *     NYX_APP_MAIN(app_enter, app_frame, app_leave)   <- paling bawah
 */
#define NYX_APP_GLOBALS static const NyxApi* g_nyx;
#define NYX_APP_MAIN(enter_fn, frame_fn, leave_fn)                              \
  int main(int argc, char** argv) {                                             \
    static NyxAppOps ops;                                                       \
    g_nyx = nyx_api_from_args(argc, argv);                                      \
    if (!g_nyx) return -1;                                                      \
    ops.enter = enter_fn; ops.frame = frame_fn; ops.leave = leave_fn;           \
    g_nyx->set_ops(&ops);                                                       \
    return 0;                                                                   \
  }

#ifdef __cplusplus
}
#endif
#endif /* NYX_APP_H */
