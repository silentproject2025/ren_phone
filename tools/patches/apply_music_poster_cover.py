#!/usr/bin/env python3
# apply_music_poster_cover.py -- MUS_POSTER_V1
# Jalankan dari root repo SETELAH apply_music_flat_ui.py:  python3 tools/patches/apply_music_poster_cover.py   (--check = cek anchor saja)
# Cover tanpa gambar ID3 kini berupa POSTER GEOMETRIS (gaya Bauhaus): komposisi bentuk datar (lingkaran, segitiga,
# kotak, seperempat lingkaran, garis) dengan 2-3 warna solid + kertas + tinta. Pola (6 tata letak) dan warna dipilih dari hash judul,
# jadi tiap lagu punya poster sendiri. Piringan hitam berputar diganti. Hanya kode gambar yang berubah.
import os, sys
M = "MUS_POSTER_V1"
MB = os.path.join("firmware", "phone", "musicbt_renphone.ino")
CL = "CHANGELOG.md"
MD = os.path.join("docs", "features", "media.md")
CHECK = "--check" in sys.argv
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    c = s.count(o)
    if c != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, c))
    return s.replace(o, n)
mb, cl, md = rd(MB), rd(CL), rd(MD)
if M in mb: print("sudah terpasang (%s)" % M); sys.exit(0)
if "MUS_FLAT_V1" not in mb: sys.exit("GAGAL: jalankan tools/patches/apply_music_flat_ui.py dulu.")

POSTER = r"""// MUS_POSTER_V1: poster geometris datar. Pola + warna dari hash judul; semua bentuk dipotong ke kotak cover.
static void musPoster(LGFX_Sprite& s, int x, int y, int S, uint32_t h) {
  int i0 = (int)(h % 8), i1 = (i0 + 2 + (int)((h / 8) % 5)) % 8;     // i1 selalu beda dari i0
  uint16_t c0 = MUS_CVPAL[i0], c1 = MUS_CVPAL[i1];
  uint16_t paper = MUS_RGB(240, 234, 220), ink = MUS_RGB(26, 26, 30);
  int L = (int)((h / 64) % 6), hs = S / 2;
  s.fillRect(x, y, S, S, c0);
  s.setClipRect(x, y, S, S);
  switch (L) {
    case 0:   // lingkaran besar + kotak ink + garis kertas
      nyxAaCircle(s, x + S * 60 / 100, y + S * 56 / 100, S * 34 / 100, c1);
      s.fillRect(x + S / 10, y + S / 10, S / 4, S / 4, ink);
      s.fillRect(x, y + S * 82 / 100, S, S / 10, paper);
      break;
    case 1:   // segitiga diagonal + lingkaran kertas
      s.fillTriangle(x, y, x + S, y, x, y + S, c1);
      nyxAaCircle(s, x + S * 62 / 100, y + S * 62 / 100, S / 4, paper);
      s.fillRect(x + S * 70 / 100, y + S / 10, S / 6, S / 6, ink);
      break;
    case 2:   // setengah lingkaran di atas alas ink bergaris
      nyxAaCircle(s, x + hs, y + S * 56 / 100, S * 36 / 100, c1);
      s.fillRect(x, y + S * 56 / 100, S, S - S * 56 / 100, ink);
      for (int k = 0; k < 3; k++) s.fillRect(x, y + S * 66 / 100 + k * (S / 10), S, 2, c0);
      break;
    case 3:   // grid 2x2 ala Bauhaus
      s.fillRect(x, y, hs, hs, paper);
      nyxAaCircle(s, x + hs / 2, y + hs / 2, hs * 35 / 100, ink);
      s.fillRect(x + hs, y, S - hs, hs, c1);
      s.fillTriangle(x + hs, y + hs, x + S, y + hs, x + S, y, ink);
      s.fillRect(x, y + hs, hs, S - hs, ink);
      s.setClipRect(x, y + hs, hs, S - hs);
      nyxAaCircle(s, x + hs, y + S, hs * 90 / 100, c1);             // seperempat lingkaran
      s.setClipRect(x, y, S, S);
      nyxAaCircle(s, x + hs + hs / 2, y + hs + hs / 2, hs / 4, paper);
      break;
    case 4:   // lingkaran bersarang di sudut
      nyxAaCircle(s, x + S * 30 / 100, y + S * 70 / 100, S * 48 / 100, c1);
      nyxAaCircle(s, x + S * 30 / 100, y + S * 70 / 100, S * 32 / 100, paper);
      nyxAaCircle(s, x + S * 30 / 100, y + S * 70 / 100, S * 14 / 100, ink);
      break;
    default: { // pita vertikal + lingkaran
      int bw = S / 6;
      s.fillRect(x + bw, y, bw, S, ink);
      s.fillRect(x + 3 * bw, y, bw, S, paper);
      s.fillRect(x + 5 * bw - bw / 2, y, bw / 2, S, c1);
      nyxAaCircle(s, x + S * 55 / 100, y + S * 40 / 100, S / 5, c1);
      break;
    }
  }
  s.clearClipRect();
}

"""
COVER_TAIL_OLD_START = "  uint16_t col = MUS_CVPAL[hsh % 8];                // satu warna solid per judul\n"
a = mb.find(COVER_TAIL_OLD_START)
if a < 0 or mb.find(COVER_TAIL_OLD_START, a + 1) >= 0: sys.exit("GAGAL: ekor musDrawCover tidak ketemu 1x.")
b = mb.find("\n}\n", a)
mb = mb[:a] + "  musPoster(s, x, y, S, hsh);                       // MUS_POSTER_V1: poster geometris per judul\n}\n" + mb[b + 3:]
mb = sub(mb, "static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {", POSTER + "static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {", "musDrawCover")
mb = sub(mb, "static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {   // MUS_FLAT_V1\n",
         "static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {   // MUS_FLAT_V1\n  (void)playing;\n", "void playing") if False else mb
ENTRY = "- Changed: the Music app cover placeholder is now a flat geometric poster instead of a spinning record (MUS_POSTER_V1). Songs without an ID3 cover get a Bauhaus-style composition of circles, triangles, squares, quarter circles and stripes in solid colours; the pattern (6 layouts) and colours come from the song title, so every song has its own poster. Only drawing code changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_music_poster_cover.py`\n"
cl = sub(cl, "## Unreleased\n", "## Unreleased\n" + ENTRY, "changelog")
md = sub(md, "The cover placeholder is one solid colour from the title with a spinning record on it.", "The cover placeholder is a flat geometric poster (Bauhaus-style shapes in solid colours) generated from the title.", "media.md")
if CHECK: print("anchor OK (%s)" % M); sys.exit(0)
bak = MB + ".bak_" + M
if not os.path.exists(bak): wr(bak, rd(MB))
wr(MB, mb); wr(CL, cl); wr(MD, md); print("terpasang:", M)
