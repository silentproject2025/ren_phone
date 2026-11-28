#!/usr/bin/env python3
"""NO_NASAAPPS_V1: nonaktifkan NEO Asteroid, Bumi EPIC, dan Galeri NASA lewat flag RP_NO_NASAAPPS (default 1 = DIBUANG
dari build). APOD (Astronomi) TIDAK disentuh. Kode asli tetap ada di sumber, hanya dibungkus #if, jadi bisa dihidupkan lagi
dgn mengubah "#define RP_NO_NASAAPPS 1" menjadi 0 (atau -DRP_NO_NASAAPPS=0).
Yang dibungkus: blok 3 app (+ struct/state/task-nya), deklarasi maju, 3 baris apps[], ikon 'O'/'E'/'G', warna apps[18..20]
(indeks setelahnya digeser RP_NASA_N), mkdir cache, hook loop(), ptr_renphone.ino, nyxeksras skeleton.
Enum SCR_NEOWS/SCR_EPIC/SCR_NASAIMG sengaja DIPERTAHANKAN (statistik Profil disimpan per nilai enum -> jangan digeser).
Efek samping: APP_COUNT 47 -> 44, jadi statistik energi per-app (nyxen) otomatis reset sekali (ukuran data berubah).
Pakai dari root repo: python3 tools/patches/apply_no_nasaapps_patch.py [--check]   (idempoten)"""
import re, sys, pathlib
CHECK = "--check" in sys.argv
MARK = "RP_NO_NASAAPPS"
P = pathlib.Path("firmware/phone")
bad = 0
def log(ok, msg):
    global bad
    if not ok: bad += 1
    print(("ok     " if ok else "GAGAL  ") + msg)

def load(fn): return (P / fn).read_text(encoding="utf-8")
def save(fn, t):
    if not CHECK: (P / fn).write_text(t, encoding="utf-8")

def wrap_span(t, start_re, end_re, label, inclusive_end=True):
    """Bungkus dari awal baris yg cocok start_re sampai akhir baris yg cocok end_re dgn #if !RP_NO_NASAAPPS ... #endif."""
    ms = list(re.finditer(start_re, t, re.M))
    if len(ms) != 1: log(False, f"{label}: awal ditemukan {len(ms)}x"); return t
    s = ms[0].start()
    me = re.compile(end_re, re.M).search(t, ms[0].end() if inclusive_end else ms[0].start())
    if not me: log(False, f"{label}: akhir tidak ketemu"); return t
    e = me.end() if inclusive_end else me.start()
    log(True, label)
    return t[:s] + "#if !RP_NO_NASAAPPS\n" + t[s:e] + ("" if t[s:e].endswith("\n") else "\n") + "#endif // RP_NO_NASAAPPS\n" + t[e:]

# ---------------- phone.ino ----------------
t = load("phone.ino")
if MARK in t:
    print("sudah   phone.ino")
else:
    # A. flag + RP_NASA_N sebelum enum Screen
    a = "enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SENSOR,"
    if t.count(a) != 1: log(False, "enum Screen tidak ditemukan 1x")
    else:
        flag = ("// NO_NASAAPPS_V1: 1 = NEO Asteroid, Bumi EPIC & Galeri NASA tidak ikut di-build (hemat flash+RAM). 0 = hidupkan lagi. APOD tidak terpengaruh.\n"
                "#ifndef RP_NO_NASAAPPS\n#define RP_NO_NASAAPPS 1\n#endif\n"
                "#if RP_NO_NASAAPPS\n#define RP_NASA_N 3   // jumlah entri apps[] yg dibuang\n#else\n#define RP_NASA_N 0\n#endif\n\n")
        t = t.replace(a, flag + a, 1); log(True, "flag RP_NO_NASAAPPS + RP_NASA_N")
    # B. deklarasi maju
    t = wrap_span(t, r"^void neoEnter\(\); void neoExit\(\);$", r"^void imgLibLowMem\(\);[^\n]*\n", "deklarasi maju")
    # C. baris apps[]
    t = wrap_span(t, r'^  \{ "NEO Asteroid",', r'^  \{ "Galeri NASA",[^\n]*\n', "baris apps[] NEO/EPIC/Galeri")
    # D. APP_COUNT
    d = "#define APP_COUNT 47\n"
    if t.count(d) != 1: log(False, "#define APP_COUNT 47 tidak ditemukan 1x")
    else: t = t.replace(d, "#define APP_COUNT (47 - RP_NASA_N)   // NO_NASAAPPS_V1\n", 1); log(True, "APP_COUNT")
    # E. warna apps[18..20] + geser indeks >= 21
    t = wrap_span(t, r"^  apps\[18\]\.color=", r"^  apps\[20\]\.color=[^\n]*\n", "warna apps[18..20]")
    m1 = re.search(r"^  apps\[21\]\.color=", t, re.M); m2 = re.search(r"^  apps\[46\]\.color=[^\n]*\n", t, re.M)
    if not (m1 and m2): log(False, "rentang apps[21..46].color tidak ketemu")
    else:
        seg = t[m1.start():m2.end()]
        seg2 = re.sub(r"apps\[(\d+)\]\.color", lambda m: f"apps[{m.group(1)} - RP_NASA_N].color", seg)
        t = t[:m1.start()] + seg2 + t[m2.end():]; log(True, f"geser indeks warna ({len(re.findall('RP_NASA_N', seg2))} baris)")
    # F. ikon 'O','E','G'
    t = wrap_span(t, r"^    // -------- v22: 4 app astronomi tambahan --------$", r"^    case 'G': \{[^\n]*\n(?:.*\n)*?    \}\n", "ikon O/E/G")
    # G. mkdir cache
    g = "  neoEnsureCacheDir(); epicEnsureCacheDir(); imgLibEnsureCacheDir();\n"
    if t.count(g) != 1: log(False, "mkdir cache tidak ditemukan 1x")
    else: t = t.replace(g, "#if !RP_NO_NASAAPPS\n" + g + "#endif // RP_NO_NASAAPPS\n", 1); log(True, "mkdir cache")
    # H. hook loop()
    t = wrap_span(t, r"^    if\(curScreen\(\)==SCR_NEOWS && neoPage==NEO_LOADING\)", r"^    if\(curScreen\(\)==SCR_NASAIMG && imgLibPhotoLoading\)[^\n]*\n", "hook loading di loop()")
    t = wrap_span(t, r"^    if\(curScreen\(\)==SCR_NEOWS && searchBarStep\(neoSearchBar", r"^    if\(curScreen\(\)==SCR_NASAIMG && searchBarStep\(imgLibSearchBar[^\n]*\n", "hook search bar di loop()")
    # I. blok besar 3 app
    s1 = "// =============================================\n// APP: NEO ASTEROID"
    s2 = "// =============================================\n// APP: NEOPIXEL"
    if t.count(s1) != 1 or t.count(s2) != 1: log(False, "anchor blok besar tidak ditemukan 1x")
    else:
        t = t.replace(s1, "#if !RP_NO_NASAAPPS   // NO_NASAAPPS_V1: awal blok NEO/EPIC/Galeri NASA\n" + s1, 1)
        t = t.replace(s2, "#endif // RP_NO_NASAAPPS: akhir blok NEO/EPIC/Galeri NASA\n" + s2, 1); log(True, "blok besar NEO+EPIC+Galeri")
    save("phone.ino", t)

# ---------------- ptr_renphone.ino ----------------
t = load("ptr_renphone.ino")
if MARK in t: print("sudah   ptr_renphone.ino")
else:
    for lab, pat in (("ptr: ptrScreenOk", r"^    case SCR_NEOWS:   return neoPage[^\n]*\n    case SCR_EPIC:    return[^\n]*\n    case SCR_NASAIMG: return[^\n]*\n"),
                     ("ptr: ptrFire",     r"^    case SCR_NEOWS:   neoRetry\(\);[^\n]*\n    case SCR_EPIC:    epicRetry\(\);[^\n]*\n    case SCR_NASAIMG: imgLibRetry\(\);[^\n]*\n")):
        ms = list(re.finditer(pat, t, re.M))
        if len(ms) != 1: log(False, f"{lab}: {len(ms)}x"); continue
        m = ms[0]; t = t[:m.start()] + "#if !RP_NO_NASAAPPS\n" + m.group(0) + "#endif // RP_NO_NASAAPPS\n" + t[m.end():]; log(True, lab)
    save("ptr_renphone.ino", t)

# ---------------- nyxextras_renphone.ino ----------------
t = load("nyxextras_renphone.ino")
if MARK in t: print("sudah   nyxextras_renphone.ino")
else:
    a = "  } else if (kind == 1) {"
    b = "    nyxSkelBox(s, 8, navY + 38, 64, 7, 3, bandX);                        // tanggal\n  }\n"
    if t.count(a) != 1 or t.count(b) != 1: log(False, f"nyxSkeleton: anchor a={t.count(a)} b={t.count(b)}")
    else:
        t = t.replace(a, "  }\n#if !RP_NO_NASAAPPS\n  else if (kind == 1) {", 1)
        t = t.replace(b, b + "#endif // RP_NO_NASAAPPS\n", 1); log(True, "nyxSkeleton kind 1..3")
    save("nyxextras_renphone.ino", t)

print("NO_NASAAPPS_V1:", "ADA YANG GAGAL (tidak ada berkas yg ditulis jika gagal di --check)" if bad else ("cek OK" if CHECK else "selesai"))
sys.exit(1 if bad else 0)
