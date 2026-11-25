#!/usr/bin/env python3
# apply_factory_reset.py -- FACTORY_RESET_V1
# Jalankan dari root repo:  python3 tools/patches/apply_factory_reset.py
# Menambah baris "Reset Pabrik" di Settings > TENTANG (bawah Daya). Aman dari salah ketuk: harus diketuk 2x dalam 4 detik.
# Logika hapus ada di firmware/phone/factory_renphone.ino (berkas itu sudah ikut di repo). Berkas diubah: phone.ino, CHANGELOG.md, docs.
import os, sys
M = "FACTORY_RESET_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"; D = os.path.join("docs", "features", "system-ui.md")
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
if not os.path.exists(os.path.join("firmware", "phone", "factory_renphone.ino")): sys.exit("GAGAL: factory_renphone.ino tidak ada")
ph = rd(PH)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
ph = sub(ph, "       SR_WSTAT, SR_FOOT /* SETT_UX_V1 */ };", "       SR_WSTAT, SR_FOOT /* SETT_UX_V1 */, SR_FACTORY /* %s */ };" % M, "enum")
ph = sub(ph, "#define SETT_MAXROWS 40", "#define SETT_MAXROWS 44", "maxrows")
ph = sub(ph, "unsigned long settTouchT0=0, settTcalArmUntil=0;", "unsigned long settTouchT0=0, settTcalArmUntil=0, settFactArmUntil=0;   // %s" % M, "arm")
ph = sub(ph, "  settAddRow(SR_POWER,g,34,0,nullptr,y); g++;\n", "  settAddRow(SR_POWER,g,34,0,nullptr,y);\n  settAddRow(SR_FACTORY,g,34,0,nullptr,y); g++;   // %s\n" % M, "row")
ph = sub(ph, "    case SR_OWNER: case SR_ABOUT: case SR_POWER: case SR_BACKUP:\n      return true;", "    case SR_OWNER: case SR_ABOUT: case SR_POWER: case SR_BACKUP: case SR_FACTORY:\n      return true;", "pressable")
ph = sub(ph, "case SR_TCAL: case SR_ABOUT: case SR_POWER: case SR_BACKUP: case SR_HEAPLOG: case SR_HWS:\n      return true;", "case SR_TCAL: case SR_ABOUT: case SR_POWER: case SR_BACKUP: case SR_HEAPLOG: case SR_HWS: case SR_FACTORY:\n      return true;", "haschip")
ph = sub(ph, "    case SR_POWER:    col=CHIP_RED;    break;\n", "    case SR_POWER:    col=CHIP_RED;    break;\n    case SR_FACTORY:  col=CHIP_RED;    break;   // %s\n" % M, "chipcol")
ph = sub(ph, "    case SR_POWER:     // tombol daya\n", "    case SR_FACTORY:   // tempat sampah\n      s.fillRect(cx-5,cy-5,11,2,W); s.fillRect(cx-2,cy-7,5,2,W); s.fillRect(cx-4,cy-2,9,9,W); s.fillRect(cx-2,cy,1,5,col); s.fillRect(cx+1,cy,1,5,col);\n      break;\n    case SR_POWER:     // tombol daya\n", "glyph")
ph = sub(ph, "      case SR_TCAL:\n        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(RPTR(\"Kalibrasi Layar Sentuh\"));",
 "      case SR_FACTORY:\n        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(RPTR(\"Reset Pabrik\"));\n        if(millis()<settFactArmUntil){ settTextR(s,xr,ty,RPTR(\"Ketuk lagi: HAPUS SEMUA\"),T().danger); needRedraw=true; }\n        else settChevron(s,xr-4,ty+4,T().subtext);\n        break;\n      case SR_TCAL:\n        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(RPTR(\"Kalibrasi Layar Sentuh\"));", "draw")
ph = sub(ph, "    case SR_POWER:\n      powerMenuShow(); needRedraw=true; break;\n",
 "    case SR_POWER:\n      powerMenuShow(); needRedraw=true; break;\n    case SR_FACTORY:   // FACTORY_RESET_V1: ketuk 2x dalam 4 detik\n      if(millis()<settFactArmUntil){ showToast(RPTR(\"Mereset ke pengaturan pabrik...\"),3000); needRedrawNow(); delay(400); factoryResetNow(); }\n      else { settFactArmUntil=millis()+4000; showToast(RPTR(\"Ketuk lagi: semua pengaturan dihapus, HP restart\"),3200); needRedraw=true; }\n      break;\n", "touch")
cl = rd(CL)
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- New: *Reset Pabrik* (Factory reset) in Settings > About, under Power (FACTORY_RESET_V1). Tap twice within 4 seconds. It erases the whole NVS (settings, touch calibration, saved WiFi, Chat/Chess broker login, API key in NVS, battery statistics) and on the SD card `/prefs`, `/gemini_key.txt`, `/chat.cfg`, `/chess.cfg` and `/ai_memory.txt`, then restarts like a new phone (screen calibration is asked again). Photos, music, books, memos and other files on the SD card, and the firmware, are not touched. Not yet compiled or tested on a device. Script: `tools/patches/apply_factory_reset.py`\n", "changelog")
d = rd(D)
if "Factory reset" not in d:
    d = d.rstrip("\n") + "\n\n## Factory reset `BETA`\n\n*Settings > About > Factory reset* (tap twice within 4 seconds) erases all settings stored in NVS and the settings copies, API key, broker logins and AI memory on the SD card, then restarts. Your own files on the SD card (photos, music, books, memos) and the firmware stay. Afterwards the phone starts like a new one and asks for screen calibration.\n"
wr(PH, ph); wr(CL, cl); wr(D, d); print("terpasang (%s)" % M)
