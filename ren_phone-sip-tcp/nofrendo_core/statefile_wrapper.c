/*
** statefile_wrapper.c
**
** Implementasi tipis (passthrough) utk fungsi statefile_fopen/fread/
** fwrite/fseek/fclose yang dideklarasikan di statefile_wrapper.h dan
** dipakai libsnss.c (save/load state NES) lewat SNSS_FILE->fp.
**
** Di ESP32 Arduino, SD_MMC/FFat/SPIFFS didaftarkan ke VFS sehingga
** fopen()/fread()/dst. dari stdio standar sudah otomatis diarahkan ke
** filesystem yang bersangkutan selama path-nya pakai mount point yang
** benar (mis. "/sdcard/...", "/ffat/..."). Jadi wrapper ini cukup
** meneruskan langsung ke fungsi stdio bawaan -- tidak perlu re-route
** manual ke SD_MMC.open()/dst.
*/

#include "statefile_wrapper.h"

FILE *statefile_fopen(const char *pathname, const char *mode)
{
   return fopen(pathname, mode);
}

int statefile_fclose(FILE *stream)
{
   return fclose(stream);
}

size_t statefile_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
   return fread(ptr, size, nmemb, stream);
}

size_t statefile_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
   return fwrite(ptr, size, nmemb, stream);
}

int statefile_fseek(FILE *stream, long offset, int whence)
{
   return fseek(stream, offset, whence);
}
