// =================================================================
// nes_protos_renphone.ino
// FIX bug Arduino IDE: auto-generate prototype buat fungsi extern "C"
// kita salah linkage (dibuat versi C++ biasa, padahal harusnya C),
// bentrok sama prototype asli di header nofrendo. Kita kasih tau
// prototype yang BENAR di sini duluan, biar Arduino nggak bikin
// sendiri yang salah.
// =================================================================

extern "C" {
  struct rgb_s;     typedef struct rgb_s rgb_t;
  struct vidinfo_s; typedef struct vidinfo_s vidinfo_t;

  void vid_setpalette(rgb_t *pal);
  void osd_getvideoinfo(vidinfo_t *info);
  void osd_getinput(void);
  int  osd_init(void);
  void osd_shutdown(void);
  int  osd_main(int argc, char *argv[]);
  int  osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize);
  void osd_fullname(char *fullname, const char *shortname);
  char *osd_newextension(char *string, char *ext);
  void osd_getmouse(int *x, int *y, int *button);
  int  osd_makesnapname(char *filename, int len);
  int  osd_nofrendo_ticks(void);
  const char *osd_getromdata(const char *name);
  void osd_unloadromdata(void);
  void ppu_scanline_blit(unsigned char *bmp, int scanline, bool draw_flag);
}
