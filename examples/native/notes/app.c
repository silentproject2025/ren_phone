// notes -- file di folder data app: penghitung buka-ke-N tersimpan, uji sandbox path.
#include "nyx_app.h"
NYX_APP_GLOBALS

static int count, escOk;

static int app_enter(void) {
  const NyxApi* a = g_nyx; char b[16] = {0}; int h;
  count = 0;
  h = a->fopen("count.txt", NYX_FOPEN_READ);
  if (h > 0) { int n = a->fread(h, b, 15); a->fclose(h); for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) count = count * 10 + (b[i] - '0'); }
  count++;
  h = a->fopen("count.txt", NYX_FOPEN_WRITE);
  if (h > 0) {
    char o[16]; int l = 0, t = count; char r[12]; int n = 0;
    if (t == 0) r[n++] = '0';
    while (t) { r[n++] = (char)('0' + t % 10); t /= 10; }
    while (n) o[l++] = r[--n];
    a->fwrite(h, o, l); a->fclose(h);
  }
  escOk = 1;                                                       // semua percobaan keluar sandbox HARUS ditolak
  if (a->fopen("../escape.txt", NYX_FOPEN_WRITE) > 0) escOk = 0;
  if (a->fopen("/etc/passwd", NYX_FOPEN_READ) > 0) escOk = 0;
  if (a->fopen("sub/../../x", NYX_FOPEN_WRITE) > 0) escOk = 0;
  if (a->fopen("a:b", NYX_FOPEN_WRITE) > 0) escOk = 0;
  if (a->fopen("sub/ok.txt", NYX_FOPEN_WRITE) <= 0) { /* sub/ boleh bila backend mendukung */ }
  g_nyx->log(escOk ? "notes: sandbox ok" : "notes: SANDBOX BOCOR");
  return 0;
}

static void app_frame(void) {
  const NyxApi* a = g_nyx; char b[16]; int l = 0, t = count; char r[12]; int n = 0;
  a->clear(a->rgb(20, 20, 20));
  a->text(8, 8, "Dibuka ke-", a->rgb(255, 255, 255), 2);
  if (t == 0) r[n++] = '0';
  while (t) { r[n++] = (char)('0' + t % 10); t /= 10; }
  while (n) b[l++] = r[--n];
  b[l] = 0;
  a->text(8, 36, b, a->rgb(255, 210, 90), 3);
}

static void app_leave(void) {}

NYX_APP_MAIN(app_enter, app_frame, app_leave)
