import sys, os
f = 'doom_renphone.ino'
if not os.path.exists(f):
    print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
s = open(f, encoding='utf-8').read()
if 'DOOM_STACK_SZ = 32768' in s:
    print("Patch stack DOOM sudah terpasang, dilewati"); sys.exit(0)

def rep(s, old, new):
    if s.count(old) != 1:
        print("ERROR: anchor ketemu %d kali: %r" % (s.count(old), old)); sys.exit(1)
    return s.replace(old, new)

s = rep(s, "const uint32_t DOOM_STACK_SZ = 12288;",
"""// Stack di PSRAM (murah), jadi dibuat lega: D_DoomMain/W_AddFile/printf/I_Error
  // punya buffer lokal besar -> 12KB kena overflow & reset (RTC_SW_CPU_RST).
  const uint32_t DOOM_STACK_SZ = 32768;
  const uint32_t DOOM_STACK_SZ_INTERNAL = 12288; // fallback RAM internal: jangan rakus""")
s = rep(s, 'xTaskCreatePinnedToCore(doomTaskFunc, "doomTask", DOOM_STACK_SZ, NULL, 1, &doomTaskHandle, 1);',
           'xTaskCreatePinnedToCore(doomTaskFunc, "doomTask", DOOM_STACK_SZ_INTERNAL, NULL, 1, &doomTaskHandle, 1);')
open(f, 'w', encoding='utf-8').write(s)
print("OK: stack task DOOM -> 32KB (PSRAM)")
