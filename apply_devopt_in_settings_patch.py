#!/usr/bin/env python3
# apply_devopt_in_settings_patch.py -- DEVOPT_IN_SETTINGS_V1
# Jalankan dari root repo:  python3 apply_devopt_in_settings_patch.py
#
# Opsi Developer dipindah ke DALAM app Setting (saran dev yang mencoba project: "kan termasuk settingan juga,
# daripada aplikasi baru"):
#  - Tidak ada lagi ikon Opsi Developer di Home (selalu disembunyikan dari grid Home).
#  - Setelah dibuka (ketuk baris "Versi firmware" di About Phone 7x), muncul baris "Opsi Developer" di
#    Setting > Sistem (di bawah Backup & Restore), lengkap dengan chip ikon "</>" dan bisa dicari lewat kolom cari.
#  - Status "terbuka" disimpan di NVS "ui" key "devopt". Kalau sebelumnya ikon sudah ditampilkan di Home,
#    otomatis dianggap sudah terbuka (tidak perlu ketuk 7x lagi).
#  - Tombol "Sembunyikan Opsi Developer" di dalam app kini menghapus baris dari Setting.
#  - "Atur Home": baris Opsi Developer tidak bisa lagi ditampilkan di Home (muncul toast penjelasan).
#  - Notifikasi "Pin baru dicoba" memastikan baris Setting tersedia, jadi pin percobaan selalu bisa dikonfirmasi.
# Berkas: phone.ino, devopt_renphone.ino, homeedit_renphone.ino, i18n_dict.h, tools/i18n_pairs.txt,
#         tests/devpins/mock_devopt.cpp, docs/features/system-tools.md, README.md, CHANGELOG.md.
# Aman dijalankan ulang; jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

FW = os.path.join("firmware", "phone")
PH = os.path.join(FW, "phone.ino")
DV = os.path.join(FW, "devopt_renphone.ino")
HE = os.path.join(FW, "homeedit_renphone.ino")
I1 = os.path.join(FW, "i18n_dict.h")
I2 = os.path.join("tools", "i18n_pairs.txt")
MK = os.path.join("tests", "devpins", "mock_devopt.cpp")
DOC = os.path.join("docs", "features", "system-tools.md")
RD = "README.md"
CL = "CHANGELOG.md"
MARK = "DEVOPT_IN_SETTINGS_V1"


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def die(msg):
    sys.exit("GAGAL: " + msg + " Tidak ada berkas yang diubah.")


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        die("pola '%s' ditemukan %dx (harus tepat 1x)." % (label, n))
    return s.replace(old, new)


for p in (PH, DV, HE, I1, I2, CL):
    if not os.path.exists(p):
        die("%s tidak ada (jalankan dari root repo)." % p)

ph, dv, he = rd(PH), rd(DV), rd(HE)
if MARK in ph:
    print("Sudah terpasang (%s). Tidak ada yang diubah." % MARK)
    sys.exit(0)

# ---------------------------------------------------------------- phone.ino
# 1) prototipe
ph = sub(ph,
         "void devoptLoopPoll(); void devoptUnlock(); void pwrReleaseHold();",
         "void devoptLoopPoll(); void devoptUnlock(); void pwrReleaseHold();\n"
         "bool devoptIsOn(); void devoptSetOn(bool);   // " + MARK, "prototipe devopt")

# 2) Home: ikon Opsi Developer tidak pernah tampil di grid; kalau dulu sudah ditampilkan -> pindahkan jadi 'terbuka'
ph = sub(ph,
         "  for(int k=0;k<APP_COUNT;k++){ int i=homeOrder[k]; if(!homeHide[i]) homeVis[homeVisN++]=i; }",
         "  for(int k=0;k<APP_COUNT;k++){\n"
         "    int i=homeOrder[k];\n"
         "    if(apps[i].screen==SCR_DEVOPT){ if(!homeHide[i]){ devoptSetOn(true); homeHide[i]=true; } continue; }   // " + MARK + ": Opsi Developer hidup di Setting > Sistem, bukan di Home\n"
         "    if(!homeHide[i]) homeVis[homeVisN++]=i;\n"
         "  }", "RebuildVis")

# 3) status terbuka (NVS 'ui' key 'devopt')
ph = sub(ph,
         "  return 0; // fallback: kalau NVS somehow kesimpen angka yg gak ada di daftar, jangan crash -- balik ke paling aman\n}\n",
         "  return 0; // fallback: kalau NVS somehow kesimpen angka yg gak ada di daftar, jangan crash -- balik ke paling aman\n}\n"
         "\n"
         "// " + MARK + ": Opsi Developer jadi baris di Setting > Sistem. Baris baru muncul setelah dibuka\n"
         "// (ketuk \"Versi firmware\" di About Phone 7x). Status disimpan di NVS \"ui\" key \"devopt\".\n"
         "// Key belum ada + ikon app sudah pernah ditampilkan di Home (versi lama) -> dianggap sudah terbuka.\n"
         "static int devOptOnState=-1;   // -1 = belum dimuat\n"
         "bool devoptIsOn(){\n"
         "  if(devOptOnState<0){\n"
         "    Preferences p; p.begin(\"ui\",true);\n"
         "    int v=p.getInt(\"devopt\",-1);\n"
         "    p.end();\n"
         "    if(v<0){ homeLayoutEnsure(); int i=appIndexForScreen(SCR_DEVOPT); v=(i>=0 && !homeHide[i])?1:0; }\n"
         "    devOptOnState=v?1:0;\n"
         "  }\n"
         "  return devOptOnState==1;\n"
         "}\n"
         "void devoptSetOn(bool on){\n"
         "  devOptOnState=on?1:0;\n"
         "  Preferences p;\n"
         "  if(!p.begin(\"ui\",false)) return;\n"
         "  p.putInt(\"devopt\",on?1:0);\n"
         "  p.end();\n"
         "}\n", "fungsi devoptIsOn")

# 4) enum baris Setting
ph = sub(ph,
         "SR_SEARCH, SR_QUICK, SR_CAT, SR_NOHIT, SR_GAP /* UI-REVAMP */ };",
         "SR_SEARCH, SR_QUICK, SR_CAT, SR_NOHIT, SR_GAP /* UI-REVAMP */, SR_DEVOPT /* " + MARK + " */ };", "enum SR")

# 5) kata kunci pencarian
ph = sub(ph,
         '    case SR_FACTORY:  return "reset pabrik factory hapus";',
         '    case SR_FACTORY:  return "reset pabrik factory hapus";\n'
         '    case SR_DEVOPT:   return "developer opsi pengembang pin gpio adc ina219 baterai sensor";   // ' + MARK, "settKeys")

# 6) tata letak halaman Sistem
ph = sub(ph,
         "      SADD(SR_ABOUT,34); SADD(SR_BACKUP,34);\n      SGRP();",
         "      SADD(SR_ABOUT,34); SADD(SR_BACKUP,34);\n"
         "      if(devoptIsOn()) SADD(SR_DEVOPT,34);   // " + MARK + "\n"
         "      SGRP();", "layout Sistem")

# 7) disorot saat ditekan + punya chip
ph = sub(ph,
         "    case SR_OWNER: case SR_ABOUT: case SR_POWER: case SR_BACKUP: case SR_FACTORY: case SR_CAT:\n      return true;",
         "    case SR_OWNER: case SR_ABOUT: case SR_POWER: case SR_BACKUP: case SR_FACTORY: case SR_CAT: case SR_DEVOPT:\n      return true;", "settPressable")
ph = sub(ph,
         "case SR_HEAPLOG: case SR_HWS: case SR_FACTORY: case SR_KBTAP: case SR_CAT:\n      return true;",
         "case SR_HEAPLOG: case SR_HWS: case SR_FACTORY: case SR_KBTAP: case SR_CAT: case SR_DEVOPT:\n      return true;", "settHasChip")

# 8) warna + glyph chip
ph = sub(ph,
         "    case SR_FACTORY:  col=CHIP_RED;    break;   // FACTORY_RESET_V1",
         "    case SR_FACTORY:  col=CHIP_RED;    break;   // FACTORY_RESET_V1\n"
         "    case SR_DEVOPT:   col=CHIP_GREEN;  break;   // " + MARK, "warna chip")
ph = sub(ph,
         "    case SR_POWER:     // tombol daya\n",
         "    case SR_DEVOPT:    // " + MARK + ": \"</>\"\n"
         "      s.setTextSize(1); s.setTextColor(W); s.setCursor(cx-8,cy-4); s.print(\"</>\");\n"
         "      break;\n"
         "    case SR_POWER:     // tombol daya\n", "glyph chip")

# 9) gambar baris
ph = sub(ph,
         "      case SR_POWER:\n        settPrintFit(s,xl,ty,RPTR(\"Daya (Matikan / Mulai Ulang)\")",
         "      case SR_DEVOPT:   // " + MARK + "\n"
         "        settPrintFit(s,xl,ty,RPTR(\"Opsi Developer\"),xr-xl-(rpPinBootState==1?30:16),T().text);\n"
         "        if(rpPinBootState==1) nyxAaCircle(s,xr-20,ty+4,3,T().accent);   // pin percobaan menunggu konfirmasi\n"
         "        settChevron(s,xr-4,ty+4,T().subtext);\n"
         "        break;\n"
         "      case SR_POWER:\n        settPrintFit(s,xl,ty,RPTR(\"Daya (Matikan / Mulai Ulang)\")", "gambar baris")

# 10) ketukan
ph = sub(ph,
         "    case SR_BACKUP:\n      settKeepPage=true; settKeepScroll=settScroll; navPush(SCR_BACKUP); break;",
         "    case SR_BACKUP:\n      settKeepPage=true; settKeepScroll=settScroll; navPush(SCR_BACKUP); break;\n"
         "    case SR_DEVOPT:   // " + MARK + "\n"
         "      settKeepPage=true; settKeepScroll=settScroll; navPush(SCR_DEVOPT); break;", "settTap")

# --------------------------------------------------------- devopt_renphone.ino
dv = sub(dv,
         "// membuka app dari About (ketuk judul 7x): tampilkan ikonnya di Home lalu buka\n"
         "void devoptUnlock() {\n"
         "  homeLayoutEnsure();\n"
         "  int i = appIndexForScreen(SCR_DEVOPT);\n"
         "  if (i >= 0 && homeHide[i]) {\n"
         "    homeHide[i] = false; homeLayoutRebuildVis(); homeLayoutSave();\n"
         "    showToast(RPTR(\"Opsi Developer aktif\"));\n"
         "  } else showToast(RPTR(\"Opsi Developer sudah aktif\"));\n"
         "  navPush(SCR_DEVOPT);\n"
         "}",
         "// membuka dari About (ketuk 7x): munculkan baris \"Opsi Developer\" di Setting > Sistem lalu buka  // " + MARK + "\n"
         "void devoptUnlock() {\n"
         "  if (!devoptIsOn()) { devoptSetOn(true); showToast(RPTR(\"Opsi Developer aktif\")); }\n"
         "  else showToast(RPTR(\"Opsi Developer sudah aktif\"));\n"
         "  navPush(SCR_DEVOPT);\n"
         "}", "devoptUnlock")

dv = sub(dv,
         "    homeLayoutEnsure();\n"
         "    int i = appIndexForScreen(SCR_DEVOPT);\n"
         "    if (i >= 0 && homeHide[i]) { homeHide[i] = false; homeLayoutRebuildVis(); homeLayoutSave(); }   // pastikan app bisa dibuka\n",
         "    if (!devoptIsOn()) devoptSetOn(true);   // " + MARK + ": pastikan baris Opsi Developer ada di Setting > Sistem\n",
         "notice boot")

dv = sub(dv,
         "      int i = appIndexForScreen(SCR_DEVOPT);\n"
         "      if (i >= 0) { homeHide[i] = true; homeLayoutRebuildVis(); homeLayoutSave(); }\n"
         "      showToast(RPTR(\"Opsi Developer disembunyikan\"));",
         "      devoptSetOn(false);   // " + MARK + ": baris di Setting > Sistem hilang\n"
         "      showToast(RPTR(\"Opsi Developer disembunyikan\"));", "DA_LOCK")

# ------------------------------------------------------------ homeedit_renphone.ino
he = sub(he,
         "  } else if (x >= heVisX() - 2 && x <= heVisX() + heVisW() + 2) { // tampil / sembunyi\n"
         "    homeHide[i] = !homeHide[i];",
         "  } else if (x >= heVisX() - 2 && x <= heVisX() + heVisW() + 2) { // tampil / sembunyi\n"
         "    if (apps[i].screen == SCR_DEVOPT) { showToast(RPTR(\"Ada di Setting > Sistem\")); return; }   // " + MARK + "\n"
         "    homeHide[i] = !homeHide[i];", "homeedit toggle")

# ----------------------------------------------------------------- i18n
pair_id, pair_en = "Ada di Setting > Sistem", "Find it in Settings > System"
i1 = rd(I1)
i1 = sub(i1,
         '  { "Sembunyikan Opsi Developer", "Hide developer options" },\n',
         '  { "Sembunyikan Opsi Developer", "Hide developer options" },\n'
         '  { "%s", "%s" },\n' % (pair_id, pair_en), "i18n dict")
i2 = rd(I2)
i2 = sub(i2,
         "Sembunyikan Opsi Developer ::: Hide developer options\n",
         "Sembunyikan Opsi Developer ::: Hide developer options\n%s ::: %s\n" % (pair_id, pair_en), "i18n pairs")

# ---------------------------------------------------------------- tes PC
mk = None
if os.path.exists(MK):
    mk = rd(MK)
    mk = sub(mk,
             "static int appIndexForScreen(Screen s) { return s == SCR_DEVOPT ? 1 : -1; }",
             "static int appIndexForScreen(Screen s) { return s == SCR_DEVOPT ? 1 : -1; }\n"
             "static bool g_devOn = false; static bool devoptIsOn() { return g_devOn; } static void devoptSetOn(bool v) { g_devOn = v; }   // " + MARK,
             "mock stub")
    mk = sub(mk,
             "  homeHide[1] = false; devoptEnter(); h = scrollToHit(DA_LOCK, 0); CHECK(h >= 0); tapHit(h); CHECK(homeHide[1] && g_navBack == 1);",
             "  g_devOn = true; devoptEnter(); h = scrollToHit(DA_LOCK, 0); CHECK(h >= 0); tapHit(h); CHECK(!g_devOn && g_navBack == 1);",
             "mock DA_LOCK")
    mk = sub(mk,
             "  homeHide[1] = true; devoptUnlock(); CHECK(!homeHide[1] && g_navPush == 1);",
             "  g_devOn = false; devoptUnlock(); CHECK(g_devOn && g_navPush == 1);",
             "mock unlock")

# ------------------------------------------------------------------ dokumen
doc = rd(DOC) if os.path.exists(DOC) else None
if doc is not None:
    old = ("The app's icon then appears on Home (category *System*) and the app opens. *Hide Developer options* at the bottom of the Pin tab hides it again. It can also be shown or hidden in *Home Layout*.")
    if doc.count(old) == 1:
        doc = doc.replace(old,
            "A *Developer options* row then appears in *Settings > System* (under *Backup & Restore*; it is searchable too) and the app opens. "
            "The app has no icon on Home. *Hide Developer options* at the bottom of the Pin tab removes the row again.")
    else:
        doc = None   # teks dokumen sudah beda: lewati saja, bukan kegagalan
    if doc is not None and "An Android-style **Developer options** app for changing pins and reading the battery sensor, hidden until you unlock it." in doc:
        doc = doc.replace("An Android-style **Developer options** app for changing pins and reading the battery sensor, hidden until you unlock it.",
                          "An Android-style **Developer options** page (inside Settings) for changing pins and reading the battery sensor, hidden until you unlock it.")

rdm = rd(RD) if os.path.exists(RD) else None
if rdm is not None:
    old = "| | [Developer options](./docs/features/system-tools.md#developer-options-beta): change pins without rebuilding (tried first, then confirmed), live ADC calculation or INA219 readings; unlock by tapping the *Firmware version* row in *About Phone* 7 times |"
    if rdm.count(old) == 1:
        rdm = rdm.replace(old, old.replace("live ADC calculation or INA219 readings;", "live ADC calculation or INA219 readings, found in *Settings > System*;"))
    else:
        rdm = None

cl = rd(CL)
cl = sub(cl, "## Unreleased\n",
         "## Unreleased\n"
         "- Changed: Developer options moved into the Settings app (" + MARK + "), as suggested by a tester (\"it is a setting too, not a new app\"). "
         "It no longer has an icon on Home. After the unlock (tap the *Firmware version* row in *About Phone* 7 times) a *Developer options* row appears in *Settings > System*, "
         "under *Backup & Restore*, with a `</>` chip, a dot while new pins wait for confirmation, and it can be found with the Settings search box. "
         "The unlocked state is kept in NVS `ui` / `devopt`; if the icon had already been shown on Home before this update, the row is unlocked automatically. "
         "*Hide Developer options* (bottom of the Pin tab) removes the row again. In *Home Layout* the Developer options entry can no longer be shown on Home (a toast explains where it is). "
         "The \"new pins on trial\" notification makes sure the row exists, so a trial can always be confirmed. Only touch / drawing glue changed; the pins, the trial logic and the sensor tab are untouched. "
         "Not yet compiled or tested on a device. Script: `apply_devopt_in_settings_patch.py`\n", "changelog")

# ------------------------------------------------------------------- tulis
wr(PH, ph); wr(DV, dv); wr(HE, he); wr(I1, i1); wr(I2, i2); wr(CL, cl)
if mk is not None: wr(MK, mk)
if doc is not None: wr(DOC, doc)
if rdm is not None: wr(RD, rdm)
print("OK -- %s terpasang. Build ulang firmware lalu flash." % MARK)
