#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_chat_ram_patch.py -- CHAT_RAM_FIX_V1
Chat gagal "RAM padat, tutup app lain". Penyebab di kode: task chat menolak
konek kalau blok kontigu RAM internal < 30000 byte (fitur NASA/Trivia cukup
20000) dan langsung menyerah tanpa menunggu RAM dilepas.
Patch: ambang 22000, tunggu s/d 4 dtk, pesan error menampilkan ANGKA aslinya
(+ log Serial) supaya kelihatan seberapa padat RAM-nya.
Pakai (Termux, folder repo):  python apply_chat_ram_patch.py
"""
import os, shutil, sys
MARK = "CHAT_RAM_FIX_V1"
FN = "chat_renphone.ino"
REPS = [
 ("#define CHAT_PORT      8883\n",
  "#define CHAT_PORT      8883\n#define CHAT_MIN_BLOCK 22000          // CHAT_RAM_FIX_V1: blok kontigu RAM internal minimum utk TLS (dulu 30000)\n"),
 ("""    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 30000) {
      chatConn = 2; chatSetErr("RAM padat, tutup app lain"); chatDirty = true;
      vTaskDelay(pdMS_TO_TICKS(2000)); continue;
    }
""",
  """    {   // CHAT_RAM_FIX_V1: tunggu RAM dilepas dulu (lwIP butuh waktu), baru menyerah
      size_t lb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
      for (int w = 0; lb < CHAT_MIN_BLOCK && w < 40 && chatActive; w++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        lb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
      }
      if (!chatActive) continue;
      if (lb < CHAT_MIN_BLOCK) {
        char eb[44];
        snprintf(eb, sizeof(eb), "RAM padat: blok %uK, perlu %uK", (unsigned)(lb / 1024), (unsigned)(CHAT_MIN_BLOCK / 1024));
        chatConn = 2; chatSetErr(eb); chatDirty = true;
        Serial.printf("[chat] RAM padat: largestBlock=%u freeInternal=%u perlu=%u\\n",
                      (unsigned)lb, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)CHAT_MIN_BLOCK);
        vTaskDelay(pdMS_TO_TICKS(2000)); continue;
      }
    }
"""),
]
def main():
    if not os.path.isfile(FN):
        print("GAGAL: %s tidak ada (jalankan dari folder repo)" % FN); sys.exit(1)
    txt = open(FN, encoding="utf-8", newline="").read()
    if MARK in txt:
        print("sudah dipatch, dilewati."); return
    crlf = "\r\n" in txt
    for old, new in REPS:
        if crlf: old = old.replace("\n", "\r\n"); new = new.replace("\n", "\r\n")
        n = txt.count(old)
        if n != 1:
            print("GAGAL: potongan kode ditemukan %d kali (harus 1): %s" % (n, old.strip().splitlines()[0][:80]))
            print("Tidak ada file yang diubah."); sys.exit(1)
        txt = txt.replace(old, new, 1)
    shutil.copyfile(FN, FN + ".bak-chatram")
    open(FN, "w", encoding="utf-8", newline="").write(txt)
    print("OK: %s dipatch (backup: %s.bak-chatram)" % (FN, FN))
if __name__ == "__main__":
    main()
