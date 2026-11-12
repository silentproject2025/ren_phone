# Pasang diagnosa RAM internal boot (heaplog). Jalankan di folder proyek:
#   python3 apply_heaplog_patch.py
import sys, os
if not os.path.exists('heaplog_renphone.ino'):
    print("ERROR: heaplog_renphone.ino tidak ada di folder ini"); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'rpHLFlush' in s:
    print("Heaplog sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

s=rep(s,"void ownerSetAvatarUi(const char*);\n",
        "void ownerSetAvatarUi(const char*);\n\nvoid rpHL(const char*); void rpHLFlush(); void rpHLLoopPoll();\n","forward decl")
s=rep(s,"  Serial.begin(115200);\n","  Serial.begin(115200);\n  rpHL(\"awal_setup\");\n","Serial.begin")
s=rep(s,"  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);\n",
        "  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);\n  rpHL(\"setelah_BT_release\");\n","BT release")
s=rep(s,"  mjpegEnsureBuffer();\n","  mjpegEnsureBuffer();\n  rpHL(\"setelah_mjpeg_buf\");\n","mjpeg")
s=rep(s,"  display.init();\n","  display.init();\n  rpHL(\"setelah_display_init\");\n","display.init")
s=rep(s,"  canvas.createSprite(display.width(),display.height());\n",
        "  canvas.createSprite(display.width(),display.height());\n  rpHL(\"setelah_canvas\");\n","canvas")
s=rep(s,"  runBootSequence();","  rpHL(\"sebelum_boot_anim\");\n  runBootSequence();","runBootSequence")
s=rep(s,"  initSD();\n","  rpHL(\"sebelum_initSD\");\n  initSD();\n  rpHL(\"setelah_initSD\");\n","initSD")
s=rep(s,"  canvasApp.createSprite(320,240-STATUS_H-44);\n",
        "  rpHL(\"sebelum_canvasApp\");\n  canvasApp.createSprite(320,240-STATUS_H-44);\n  rpHL(\"setelah_canvasApp\");\n","canvasApp")
s=rep(s,"  loadWifiCreds();\n  connectWifi();\n  wakeBegin();",
        "  rpHL(\"sebelum_wifi\");\n  loadWifiCreds();\n  connectWifi();\n  rpHL(\"setelah_connectWifi\");\n  wakeBegin();","wifi/wake")
s=rep(s,"kalau user sempat ngaktifin)\n}\n",
        "kalau user sempat ngaktifin)\n  rpHL(\"akhir_setup\");\n  rpHLFlush();\n}\n","akhir setup")
s=rep(s,"void loop(){\n","void loop(){\n  rpHLLoopPoll();\n","loop")

wf='.github/workflows/build-firmware.yml'
w=None
if os.path.exists(wf):
    w=open(wf,encoding='utf-8').read()
    a="          cp gutdl_core.h build_sketch/\n"
    if 'heaplog_renphone.ino' not in w:
        if w.count(a)==1: w=w.replace(a,a+"          cp heaplog_renphone.ino build_sketch/\n")
        else: print("PERINGATAN: tambahkan 'cp heaplog_renphone.ino build_sketch/' ke workflow sendiri"); w=None
    else: w=None
open('phone.ino','w',encoding='utf-8').write(s)
if w: open(wf,'w',encoding='utf-8').write(w)
print("OK: heaplog terpasang. Flash, boot, tunggu ~60 dtk, lalu baca /heaplog.txt di SD.")
