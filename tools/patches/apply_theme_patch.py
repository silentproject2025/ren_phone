#!/usr/bin/env python3
"""
apply_theme_patch.py -- dukungan app native "Theme" (wallpaper dari Picsum).

Pakai (dari root repo):
    python3 tools/patches/apply_theme_patch.py            # pasang
    python3 tools/patches/apply_theme_patch.py --check    # hanya cek anchor

Tiga perubahan kecil, idempoten & atomik (satu anchor gagal -> tidak ada yang diubah):
  1. nyxapp_renphone.ino : worker HTTP mengikuti redirect (picsum.photos membalas 302).
  2. nyxapp_renphone.ino : saat app ditutup, bila ada data/applied, muat ulang wallpaper.
  3. phone.ino           : wallpaperResolve() memeriksa /apps/theme/data/wallpaper{1,2}.jpg lebih dulu
                           (app native hanya boleh menulis ke data/-nya sendiri, tidak ke root).
"""
import sys, os

MARK = "THEME_V1"
NYX = os.path.join("firmware", "phone", "nyxapp_renphone.ino")
PHONE = os.path.join("firmware", "phone", "phone.ino")

EDITS = {
  NYX: [
    ("redirect HTTP",
     "        http.setTimeout(12000);\n        code = http.GET();",
     "        http.setTimeout(12000);\n        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // THEME_V1: picsum.photos -> 302\n        code = http.GET();"),
    ("hook muat ulang",
     "// hentikan app (aman dipanggil berulang). Tidak mengubah nyxState kecuali ke LIST bila tidak ada galat.\nstatic void nyxStop() {",
     "// THEME_V1: app Theme menulis data/applied setelah mengganti/menghapus wallpaper -> muat ulang di sini.\nbool loadWallpaper();\nstatic void nyxThemeReload() {\n"
     "  if (!nyxAppFs || !nyxAppFs->exists(\"/apps/theme/data/applied\")) return;\n"
     "  nyxAppFs->remove(\"/apps/theme/data/applied\");\n  loadWallpaper();\n}\n\n"
     "// hentikan app (aman dipanggil berulang). Tidak mengubah nyxState kecuali ke LIST bila tidak ada galat.\nstatic void nyxStop() {"),
    ("panggil muat ulang",
     "  nyxState = NYX_ST_RUN;\n#endif\n}",
     "  nyxState = NYX_ST_RUN;\n#endif\n  nyxThemeReload();   // THEME_V1\n}"),
  ],
  PHONE: [
    ("resolve wallpaper",
     "  const char* pref = (SCR_W>=SCR_H) ? WALLPAPER_FILE_L : WALLPAPER_FILE_P;\n  if(wallpaperFileExists(pref)) return pref;",
     "  // THEME_V1: wallpaper dari app Theme (folder data app) menang atas file di root\n"
     "  const char* thm = (SCR_W>=SCR_H) ? \"/apps/theme/data/wallpaper2.jpg\" : \"/apps/theme/data/wallpaper1.jpg\";\n"
     "  if(wallpaperFileExists(thm)) return thm;\n"
     "  const char* pref = (SCR_W>=SCR_H) ? WALLPAPER_FILE_L : WALLPAPER_FILE_P;\n  if(wallpaperFileExists(pref)) return pref;"),
  ],
}

def main():
    check = "--check" in sys.argv
    new = {}
    for path, edits in EDITS.items():
        if not os.path.exists(path): sys.exit("tidak ada: " + path + " (jalankan dari root repo)")
        s = open(path, encoding="utf-8").read()
        for name, old, rep in edits:
            if rep in s: print("  sudah ada:", name); continue
            n = s.count(old)
            if n != 1: sys.exit("GAGAL: anchor '%s' ditemukan %d kali di %s (harus 1). Tidak ada yang diubah." % (name, n, path))
            s = s.replace(old, rep); print("  ok:", name)
        new[path] = s
    if check: print("--check: semua anchor cocok."); return
    for path, s in new.items():
        if os.path.exists(path + ".bak_theme") is False: open(path + ".bak_theme", "w", encoding="utf-8").write(open(path, encoding="utf-8").read())
        open(path, "w", encoding="utf-8").write(s)
    print("Selesai. Build firmware, lalu pasang app (lihat examples/native/theme).")

if __name__ == "__main__":
    main()
