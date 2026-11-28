#!/usr/bin/env python3
# apply_nav_round_patch.py -- NAV_ROUND_V1
# Jalankan dari root repo:  python3 apply_nav_round_patch.py
#
# Animasi buka/tutup aplikasi dibuat lebih "HP modern":
#  - Jendela yang membesar/menyusut sekarang SELALU bersudut membulat. Dulu sudut membulat hanya selama fase
#    balok warna; begitu isi app muncul, jendelanya persegi tajam. Sekarang radius sudut berubah mulus:
#    radius ikon -> ~18 px di tengah gerakan -> 0 saat layar penuh (dan kebalikannya saat ditutup).
#  - Berlaku untuk BUKA (ikon -> layar penuh), TUTUP (layar penuh -> ikon) dan TUTUP DENGAN GESER dari tepi bawah
#    (jendela mengikuti jari), jadi ketiganya satu bahasa gerak.
#  - Tetap hemat SPI: jendela hanya ditulis di strip yang baru terbuka + potongan kecil di sudut
#    (sekitar 2x36 kotak mungil per frame), tidak ada alpha blend per piksel.
#  - Buka layar kunci (unlockZoomHome) tetap memakai bentuk lama (transShot dipakai cache layar kunci).
# Berkas: firmware/phone/phone.ino, CHANGELOG.md, docs/features/system-ui.md.
# Aman dijalankan ulang; jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

PH = os.path.join("firmware", "phone", "phone.ino")
CL = "CHANGELOG.md"
DOC = os.path.join("docs", "features", "system-ui.md")
MARK = "NAV_ROUND_V1"


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


for p in (PH, CL):
    if not os.path.exists(p):
        die("%s tidak ada (jalankan dari root repo)." % p)

ph = rd(PH)
if MARK in ph:
    print("Sudah terpasang (%s). Tidak ada yang diubah." % MARK)
    sys.exit(0)

# ------------------------------------------------------------------ helper
HELPER = r'''// NAV_ROUND_V1 BEGIN -------------------------------------------------------------------------------
// Sudut membulat utk jendela zoom buka/tutup app. Panel TIDAK bisa di-mask per piksel, jadi sudut dibuat dengan
// mengembalikan piksel di luar lengkung dari sprite sumber (potongan 1-2 baris per kotak kecil, 4 sudut).
bool navZoomPlain=false;   // true = bentuk lama (tanpa sudut bulat di fase jendela); dipakai unlockZoomHome()
int navZoomRadius(float ez,int orad){   // ez: 0 = sebesar ikon, 1 = layar penuh
  if(navZoomPlain){
    if(ez>=0.5f) return 0;
    float cr=ez*1.6f; if(cr>1.0f) cr=1.0f;
    return (int)lroundf(orad*(1.0f-cr));
  }
  const float RM=18.0f;                                  // radius di tengah gerakan
  if(ez<0.5f) return (int)lroundf((float)orad+(RM-(float)orad)*(ez*2.0f));
  return (int)lroundf(RM*(1.0f-ez)*2.0f);
}
// tempel potongan sprite sumber (fromShot ? transShot : canvas) ke panel, dibatasi kotak (bx0,by0)-(bx1,by1)
void navZoomPiece(bool fromShot,int x,int y,int w,int h,int bx0,int by0,int bx1,int by1){
  int x1=x+w, y1=y+h;
  if(x<bx0) x=bx0; if(y<by0) y=by0; if(x1>bx1) x1=bx1; if(y1>by1) y1=by1;
  if(x1<=x || y1<=y) return;
  display.setClipRect(x,y,x1-x,y1-y);
  if(fromShot) transShot.pushSprite(0,0); else canvas.pushSprite(0,0);
  display.clearClipRect();
}
// isi piksel DI LUAR lengkung di 4 sudut jendela (x0,y0,w,h) berradius rr dari sprite sumber
void navZoomCorners(bool fromShot,int x0,int y0,int w,int h,int rr,int bx0,int by0,int bx1,int by1){
  if(rr>w/2) rr=w/2;
  if(rr>h/2) rr=h/2;
  if(rr<2) return;
  for(int i=0;i<rr;){
    int hh=(i<rr/2)?1:2; if(i+hh>rr) hh=rr-i;           // dekat tepi lengkung curam -> baris tunggal
    float dy=(float)rr-((float)i+(float)hh*0.5f);
    int dx=rr-(int)(sqrtf((float)rr*(float)rr-dy*dy)+0.5f);
    if(dx>0){
      navZoomPiece(fromShot,x0,y0+i,dx,hh,bx0,by0,bx1,by1);
      navZoomPiece(fromShot,x0+w-dx,y0+i,dx,hh,bx0,by0,bx1,by1);
      navZoomPiece(fromShot,x0,y0+h-i-hh,dx,hh,bx0,by0,bx1,by1);
      navZoomPiece(fromShot,x0+w-dx,y0+h-i-hh,dx,hh,bx0,by0,bx1,by1);
    }
    i+=hh;
  }
}
// NAV_ROUND_V1 END ---------------------------------------------------------------------------------
'''

ph = sub(ph,
         "void navZoomPushHome(int x,int y,int w,int h);   // SMOOTH-ZOOM: definisi di bawah (tempel potongan canvas ke panel, di-clip)\n",
         "void navZoomPushHome(int x,int y,int w,int h);   // SMOOTH-ZOOM: definisi di bawah (tempel potongan canvas ke panel, di-clip)\n" + HELPER,
         "helper")

# --------------------------------------------------------------- buka (open)
ph = sub(ph,
         "void navPlayZoomOpen(){\n  renderCurrentFrame();   // canvas = layar BARU; panel masih menampilkan Home (frame terakhir)\n",
         "void navPlayZoomOpen(){\n"
         "  const bool rnd=!navZoomPlain;   // " + MARK + "\n"
         "  if(rnd) canvas.pushSprite(&transShot,0,0);   // " + MARK + ": salinan Home (canvas msh berisi layar lama) utk mengisi sudut membulat\n"
         "  renderCurrentFrame();   // canvas = layar BARU; panel masih menampilkan Home (frame terakhir)\n",
         "open start")

ph = sub(ph,
         "  bool inApp=false;       // SMOOTH-ZOOM: true = rect terakhir di panel sudah berisi isi app (fase 2)\n",
         "  bool inApp=false;       // SMOOTH-ZOOM: true = rect terakhir di panel sudah berisi isi app (fase 2)\n"
         "  int prr=0;              // " + MARK + ": radius sudut jendela frame sebelumnya (0 = tidak ada sudut yg perlu dipulihkan)\n",
         "open vars")

OPEN_OLD = (
    "      if(ez<0.5f){\n"
    "        float cr=ez*1.6f; if(cr>1.0f) cr=1.0f;\n"
    "        int rr=(int)lroundf(orad*(1.0f-cr));\n"
    "        if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;\n"
    "        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));\n"
    "      } else if(!inApp){\n"
    "        navZoomPushHome(x0,y0,w,h);                     // SMOOTH-ZOOM: pindah balok -> isi app: tempel SELURUH jendela sekali\n"
    "        inApp=true;\n"
    "      } else {\n"
    "        // SMOOTH-ZOOM: jendela sebelumnya sudah berisi isi app yg sama -> kirim HANYA 4 strip yg baru terbuka\n"
    "        // (dulu seluruh jendela tiap frame, ~300 KB di akhir animasi -> ~16 fps @40 MHz).\n"
    "        navZoomPushHome(x0,y0,w,py0-y0);                // strip atas\n"
    "        navZoomPushHome(x0,py1,w,y1-py1);               // strip bawah\n"
    "        navZoomPushHome(x0,py0,px0-x0,py1-py0);         // strip kiri\n"
    "        navZoomPushHome(px1,py0,x1-px1,py1-py0);        // strip kanan\n"
    "      }\n")
OPEN_NEW = (
    "      const int rrc=navZoomRadius(ez,orad);             // " + MARK + ": radius sudut mulus ikon -> 18 -> 0\n"
    "      if(ez<0.5f){\n"
    "        int rr=rrc; if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;\n"
    "        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));\n"
    "        prr=0;\n"
    "      } else {\n"
    "        if(!inApp){\n"
    "          navZoomPushHome(x0,y0,w,h);                   // SMOOTH-ZOOM: pindah balok -> isi app: tempel SELURUH jendela sekali\n"
    "          inApp=true;\n"
    "        } else {\n"
    "          // SMOOTH-ZOOM: jendela sebelumnya sudah berisi isi app yg sama -> kirim HANYA 4 strip yg baru terbuka\n"
    "          // (dulu seluruh jendela tiap frame, ~300 KB di akhir animasi -> ~16 fps @40 MHz).\n"
    "          if(prr>=2) navZoomCorners(false,px0,py0,px1-px0,py1-py0,prr,0,0,SCR_W,SCR_H);   // sudut lama (tadinya Home) -> isi app lagi\n"
    "          navZoomPushHome(x0,y0,w,py0-y0);              // strip atas\n"
    "          navZoomPushHome(x0,py1,w,y1-py1);             // strip bawah\n"
    "          navZoomPushHome(x0,py0,px0-x0,py1-py0);       // strip kiri\n"
    "          navZoomPushHome(px1,py0,x1-px1,py1-py0);      // strip kanan\n"
    "        }\n"
    "        if(rnd) navZoomCorners(true,x0,y0,w,h,rrc,0,0,SCR_W,SCR_H);   // sudut baru: piksel di luar lengkung = Home (transShot)\n"
    "        prr=rnd ? min(rrc,min(w/2,h/2)) : 0;\n"
    "      }\n")
ph = sub(ph, OPEN_OLD, OPEN_NEW, "open loop")

# -------------------------------------------------------------- tutup (close)
ph = sub(ph,
         "void navPlayZoomClose(){\n  renderCurrentFrame();   // canvas = Home; panel masih menampilkan layar app (frame terakhir)\n",
         "void navPlayZoomClose(){\n"
         "  const bool rnd=!navZoomPlain;   // " + MARK + "\n"
         "  if(rnd) canvas.pushSprite(&transShot,0,0);   // " + MARK + ": salinan layar app (canvas msh berisi app) utk memulihkan sudut yg tadinya Home\n"
         "  renderCurrentFrame();   // canvas = Home; panel masih menampilkan layar app (frame terakhir)\n",
         "close start")

ph = sub(ph,
         "  int px0=0,py0=0,px1=SCR_W,py1=SCR_H;            // rect frame sebelumnya (selalu mengandung rect baru)\n",
         "  int px0=0,py0=0,px1=SCR_W,py1=SCR_H;            // rect frame sebelumnya (selalu mengandung rect baru)\n"
         "  int prr=0;                                      // " + MARK + ": radius sudut frame sebelumnya\n",
         "close vars")

ph = sub(ph,
         "        float cr=ez*1.6f; if(cr>1.0f) cr=1.0f;\n"
         "        int rr=(int)lroundf(orad*(1.0f-cr));\n"
         "        if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;\n"
         "        if(rr>0){                                  // pojok yg terpotong lengkungan balok -> kembalikan isi Home\n",
         "        int rr=navZoomRadius(ez,orad);             // " + MARK + "\n"
         "        if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;\n"
         "        if(rr>0){                                  // pojok yg terpotong lengkungan balok -> kembalikan isi Home\n",
         "close radius")

ph = sub(ph,
         "        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));\n"
         "      }\n"
         "      px0=x0; py0=y0; px1=x1; py1=y1;",
         "        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));\n"
         "        prr=0;\n"
         "      } else if(rnd){                              // " + MARK + ": jendela berisi app tetap bersudut bulat\n"
         "        if(prr>=2) navZoomCorners(true,px0,py0,px1-px0,py1-py0,prr,x0,y0,x1,y1);   // sudut lama yg kini di dalam jendela -> isi app lagi\n"
         "        const int rrc=navZoomRadius(ez,orad);\n"
         "        navZoomCorners(false,x0,y0,w,h,rrc,0,0,SCR_W,SCR_H);                       // sudut baru: piksel di luar lengkung = Home (canvas)\n"
         "        prr=min(rrc,min(w/2,h/2));\n"
         "      }\n"
         "      px0=x0; py0=y0; px1=x1; py1=y1;",
         "close corners")

# ------------------------------------------------------ tutup dgn geser (drag)
ph = sub(ph,
         "  if(ez>=0.5f){\n"
         "    display.setClipRect(x0,y0,w,h); transShot.pushSprite(0,0); display.clearClipRect();\n"
         "  } else {\n"
         "    float cr=ez*1.6f; if(cr>1.0f) cr=1.0f;\n"
         "    int rr=(int)lroundf(dragOR*(1.0f-cr)); if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;\n",
         "  if(ez>=0.5f){\n"
         "    display.setClipRect(x0,y0,w,h); transShot.pushSprite(0,0); display.clearClipRect();\n"
         "    navZoomCorners(false,x0,y0,w,h,navZoomRadius(ez,dragOR),0,0,SCR_W,SCR_H);   // " + MARK + ": jendela yg mengikuti jari bersudut bulat (Home = canvas)\n"
         "  } else {\n"
         "    int rr=navZoomRadius(ez,dragOR); if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;\n",
         "drag close")

# ----------------------------------------------------------------- unlock
ph = sub(ph,
         "  navPlayZoomOpen();                                   // renderCurrentFrame() sudah Home (locked=false)\n",
         "  navZoomPlain=true;                                   // " + MARK + ": transShot dipakai cache layar kunci -> bentuk lama\n"
         "  navPlayZoomOpen();                                   // renderCurrentFrame() sudah Home (locked=false)\n"
         "  navZoomPlain=false;\n",
         "unlock")

# ------------------------------------------------------------- changelog/doc
cl = rd(CL)
cl = sub(cl, "## Unreleased\n",
         "## Unreleased\n"
         "- Improved: app open / close animation with rounded corners all the way (" + MARK + "). Before, the growing window was rounded only while it was still a coloured block; "
         "as soon as the app appeared it became a sharp rectangle. Now the corner radius changes smoothly: icon radius, about 18 px halfway, 0 at full screen, and the reverse when closing. "
         "It applies to opening an app, closing it, and the swipe-up-to-close gesture (the window follows the finger), so the three feel the same. "
         "The panel cannot mask pixels, so the corners are made by restoring the pixels outside the arc from a copy of the other screen (about 2 x 36 tiny pieces per frame, no per-pixel blending); "
         "the corner pieces of the previous frame are repainted first, so nothing is left behind. The copy goes to `transShot` (one extra 300 KB copy before the animation). "
         "Unlocking still uses the old shape because `transShot` holds the lock-screen cache at that moment. Only drawing code changed. Not yet compiled or tested on a device. Script: `apply_nav_round_patch.py`\n",
         "changelog")

doc = rd(DOC) if os.path.exists(DOC) else None
if doc is not None:
    old = "a Home icon grows into the app screen when opened and shrinks back to its icon when closed."
    if doc.count(old) == 1:
        doc = doc.replace(old, "a Home icon grows into the app screen when opened and shrinks back to its icon when closed; the window keeps rounded corners the whole way (radius icon -> 18 px -> 0), also when you close an app by swiping up and the window follows your finger.")
    else:
        doc = None

wr(PH, ph)
wr(CL, cl)
if doc is not None:
    wr(DOC, doc)
print("OK -- %s terpasang. Build ulang firmware lalu flash." % MARK)
