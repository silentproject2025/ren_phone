#!/usr/bin/env python3
# apply_audius.py -- AUDIUS_V1
# Jalankan dari root repo:  python3 tools/patches/apply_audius.py   (tambah --check untuk cek anchor saja)
# Menambah Audius sebagai sumber lagu online kedua di app Musik (halaman Online), di samping Jamendo.
#   - jam_core.h   : parser Audius (audParse, audKey, audGenre) + perbaikan jamUnescape (huruf non-ASCII di ujung teks)
#   - musjam.h     : sumber aktif (jamSrc), pencarian/trending Audius, buffer respons 256 KB (PSRAM)
#   - musicbt_*.ino: chip sumber di sebelah kotak cari, pilihan disimpan di NVS ("music"/"jsrc")
#   - docs         : CHANGELOG.md, docs/features/media.md, README.md
# Audius tidak butuh API key. Kalau build tidak punya JAM_CLIENT_ID, Audius jadi sumber bawaan.
import os, sys

M = "AUDIUS_V1"
FW = os.path.join("firmware", "phone")
JC = os.path.join(FW, "jam_core.h")
MJ = os.path.join(FW, "musjam.h")
MB = os.path.join(FW, "musicbt_renphone.ino")
CL = "CHANGELOG.md"
MD = os.path.join("docs", "features", "media.md")
RM = "README.md"
CHECK = "--check" in sys.argv

def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    c = s.count(o)
    if c != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, c))
    return s.replace(o, n)

jc, mj, mb, cl, md, rm = (rd(p) for p in (JC, MJ, MB, CL, MD, RM))
if M in mj and M in mb and M in jc:
    print("sudah terpasang (%s)" % M); sys.exit(0)

# ---------------------------------------------------------------- jam_core.h
AUD_CORE = r"""// =====================================================================
//  AUDIUS_V1 -- parser respons Audius (/v1/tracks/search dan /v1/tracks/trending)
//  Bentuk: {"data":[{ "id":"D7KyD", "title":"..", "duration":180, "user":{"id":"..","name":".."}, ... }]}
//  Objek track berisi objek bersarang (artwork, user, access, ...) yang juga punya "id" dan "name",
//  jadi kunci DICARI HANYA di kedalaman 1 (audKey), bukan strstr seperti jamFindStr.
//  Lagu yang tidak bisa diputar (is_streamable:false, is_stream_gated:true, access.stream:false) dilewati.
// =====================================================================
#define AUD_BASE  "https://api.audius.co/v1"
#define AUD_APP   "ren_phone"

// pointer ke nilai (setelah ':' dan spasi) dari "key" di kedalaman 1 objek; nullptr kalau tidak ada.
// obj harus diawali '{' atau '['. Untuk objek bersarang: audKey(audKey(o,"user"),"name").
static const char* audKey(const char* obj, const char* key) {
  if (!obj) return nullptr;
  size_t kl = strlen(key); int depth = 0;
  for (const char* p = obj; *p; p++) {
    char c = *p;
    if (c == '"') {
      const char* s = p + 1; const char* q = s;
      while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
      if (!*q) return nullptr;
      if (depth == 1 && (size_t)(q - s) == kl && strncmp(s, key, kl) == 0) {
        const char* v = q + 1;
        while (*v == ' ') v++;
        if (*v == ':') { v++; while (*v == ' ') v++; return v; }
      }
      p = q; continue;
    }
    if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') depth--;
  }
  return nullptr;
}
static bool audStr(const char* obj, const char* key, char* out, size_t cap) {
  const char* v = audKey(obj, key);
  if (!v || *v != '"') return false;
  jamUnescape(v + 1, out, cap);
  return true;
}
static int audInt(const char* obj, const char* key) {            // -1 kalau tidak ada / bukan angka
  const char* v = audKey(obj, key);
  if (!v || *v < '0' || *v > '9') return -1;
  long n = 0;
  while (*v >= '0' && *v <= '9' && n < 100000) { n = n * 10 + (*v - '0'); v++; }
  return (int)n;
}
static bool audIsFalse(const char* obj, const char* key) { const char* v = audKey(obj, key); return v && strncmp(v, "false", 5) == 0; }
static bool audIsTrue (const char* obj, const char* key) { const char* v = audKey(obj, key); return v && strncmp(v, "true", 4) == 0; }

// id Audius = huruf/angka saja (mis. "D7KyD"); apa pun selain itu ditolak supaya URL aman
static bool audIdOk(const char* id) {
  size_t n = strlen(id);
  if (n < 2 || n > 24) return false;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  return true;
}

// Parse respons. json DIUBAH (NUL disisipkan). Isi out[] dengan JamTrack yang sama dengan Jamendo:
// url = https://api.audius.co/v1/tracks/<id>/stream?app_name=ren_phone (server membalas 302 ke file MP3).
static int audParse(char* json, JamTrack* out, int maxN) {
  char* p = strstr(json, "\"data\"");
  if (!p) return 0;
  p = strchr(p, '[');
  if (!p) return 0;
  p++;
  int n = 0;
  while (n < maxN) {
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') break;
    char* start = p;
    int depth = 0; bool inStr = false;
    for (; *p; p++) {
      char c = *p;
      if (inStr) { if (c == '\\' && p[1]) p++; else if (c == '"') inStr = false; continue; }
      if (c == '"') inStr = true;
      else if (c == '{') depth++;
      else if (c == '}') { depth--; if (depth == 0) break; }
    }
    if (*p != '}') break;                              // objek terpotong
    *p = 0;
    JamTrack& t = out[n];
    t.title[0] = t.artist[0] = t.url[0] = 0; t.durSec = 0;
    char id[28]; id[0] = 0;
    bool ok = audStr(start, "id", id, sizeof(id)) && audIdOk(id);
    if (ok && (audIsFalse(start, "is_streamable") || audIsTrue(start, "is_stream_gated"))) ok = false;
    if (ok) { const char* ac = audKey(start, "access"); if (ac && *ac == '{' && audIsFalse(ac, "stream")) ok = false; }
    if (ok) {
      audStr(start, "title", t.title, sizeof(t.title));
      const char* u = audKey(start, "user");
      if (u && *u == '{') audStr(u, "name", t.artist, sizeof(t.artist));
      int d = audInt(start, "duration"); t.durSec = (d > 0 && d < 65535) ? (uint16_t)d : 0;
      snprintf(t.url, sizeof(t.url), AUD_BASE "/tracks/%s/stream?app_name=" AUD_APP, id);
      if (!t.title[0]) strcpy(t.title, "(tanpa judul)");
      n++;
    }
    p++;
  }
  return n;
}

// chip genre aplikasi (tag Jamendo huruf kecil) -> nama genre Audius untuk trending; "" = semua genre
static const char* audGenre(const char* tag) {
  if (!tag || !tag[0]) return "";
  if (!strcmp(tag, "pop"))  return "Pop";
  if (!strcmp(tag, "rock")) return "Rock";
  if (!strcmp(tag, "lofi")) return "Lo-Fi";
  if (!strcmp(tag, "jazz")) return "Jazz";
  return "";
}
"""

old = """  // buang byte UTF-8 yang terpotong di ujung
  size_t k = o;
  while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
  if (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0xC0) o = k - 1;
  out[o] = 0;"""
new = """  // buang byte UTF-8 yang terpotong di ujung (AUDIUS_V1: hanya kalau urutannya memang belum lengkap;
  // dulu satu huruf non-ASCII yang utuh di akhir teks ikut terbuang, mis. nama artis berakhiran "Ü")
  size_t k = o;
  while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
  if (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0xC0) {
    unsigned char lead = (unsigned char)out[k - 1];
    size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
    if (o - (k - 1) < need) o = k - 1;
  }
  out[o] = 0;"""
jc = sub(jc, old, new, "jamUnescape ujung UTF-8")
jc = jc.rstrip("\n") + "\n" + AUD_CORE

# ---------------------------------------------------------------- musjam.h
mj = sub(mj, "#define JAM_RESP_MAX (128 * 1024)   // JAM_PARTIAL_V1: 40 lagu x ~2 KB JSON (di PSRAM)",
  "#define JAM_RESP_MAX (256 * 1024)   // JAM_PARTIAL_V1: 40 lagu x ~2 KB JSON (di PSRAM); AUDIUS_V1: respons Audius ~3-5 KB per lagu",
  "JAM_RESP_MAX")

AUD_NET = """// =====================================================================
//  AUDIUS_V1 -- sumber lagu kedua. 0 = Jamendo (butuh client_id), 1 = Audius (tanpa kunci).
//  Daftar hasil (jamTracks) sama untuk keduanya, jadi UI, pemutaran (SPK / BT) dan lagu berikutnya tidak berubah.
//  Pilihan disimpan di NVS "music"/"jsrc". Bawaan: Jamendo kalau build punya JAM_CLIENT_ID, kalau tidak Audius.
//  Cari   -> GET /v1/tracks/search?query=..          (cocok sebagian/juga artis; urutan lalu dirapikan jamRank)
//  Hits   -> GET /v1/tracks/trending?time=week       (+ &genre=Pop/Rock/Lo-Fi/Jazz untuk chip genre)
//  Putar  -> GET /v1/tracks/<id>/stream              (balas 302 ke file MP3; HTTPClient mengikuti redirect)
//  BELUM DICOMPILE / DITES DI BOARD.
// =====================================================================
#define JAM_SRC_JAMENDO 0
#define JAM_SRC_AUDIUS  1
static volatile uint8_t jamSrc    = JAM_SRC_JAMENDO;   // pilihan di UI
static uint8_t          jamSrcRun = JAM_SRC_JAMENDO;   // salinan yang dipakai worker selama satu pencarian
static const char* jamSrcName()   { return jamSrc == JAM_SRC_AUDIUS ? "Audius" : "Jamendo"; }
static uint8_t jamSrcDefault()    { return (strcmp(JAM_CLIENT_ID, "ISI_CLIENT_ID_KAMU") == 0) ? JAM_SRC_AUDIUS : JAM_SRC_JAMENDO; }
static void jamSetSource(uint8_t s) { jamSrc = (s == JAM_SRC_AUDIUS) ? JAM_SRC_AUDIUS : JAM_SRC_JAMENDO; jamN = 0; }

// Return jumlah lagu (>=0) atau -2 = HTTP/TLS gagal. body = buffer PSRAM milik jamSearch.
static int audSearchInto(const char* qq, const char* tag, char* body) {
  char url[360]; int u;
  if (qq[0]) {
    char q[96]; jamUrlEnc(qq, q, sizeof(q));
    u = snprintf(url, sizeof(url), AUD_BASE "/tracks/search?query=%s&limit=%d&app_name=" AUD_APP, q, JAM_MAX_TRACKS);
  } else {
    u = snprintf(url, sizeof(url), AUD_BASE "/tracks/trending?time=week&limit=%d&app_name=" AUD_APP, JAM_MAX_TRACKS);
    const char* g = audGenre(tag);
    if (g[0]) { char ge[48]; jamUrlEnc(g, ge, sizeof(ge)); u += snprintf(url + u, sizeof(url) - u, "&genre=%s", ge); }
  }
  int result = -2;
  WiFiClientSecure net; net.setInsecure();              // sama seperti Jamendo: tanpa verifikasi sertifikat
  HTTPClient http; http.setTimeout(15000); http.setConnectTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (http.begin(net, url)) {
    int code = http.GET();
    if (code == 200) {
      JamSink sink(body, JAM_RESP_MAX);
      http.writeToStream(&sink);
      body[sink.len] = 0;
      jamN = audParse(body, jamTracks, JAM_MAX_TRACKS);
      result = jamN;
    }
    http.end();
  }
  return result;
}

"""
mj = sub(mj, "// Return jumlah lagu (>=0), atau -1 = tidak ada WiFi, -2 = HTTP/TLS gagal, -3 = RAM habis.\n",
         AUD_NET + "// Return jumlah lagu (>=0), atau -1 = tidak ada WiFi, -2 = HTTP/TLS gagal, -3 = RAM habis.\n", "blok Audius")

mj = sub(mj, "  int result = -2;\n  for (int pass = 0; pass < 2; pass++) {",
"""  if (jamSrcRun == JAM_SRC_AUDIUS) {                   // AUDIUS_V1
    int ar = audSearchInto(qq, tag, body);
    heap_caps_free(body);
    if (ar > 0 && qq[0]) jamRank(jamTracks, jamN, qq);
    return ar;
  }

  int result = -2;
  for (int pass = 0; pass < 2; pass++) {""", "cabang Audius di jamSearch")

mj = sub(mj, "  jamBusyF = true; jamDoneF = false;\n  xTaskNotifyGive(jamTaskH);",
         "  jamSrcRun = jamSrc;                                // AUDIUS_V1: sumber dikunci selama satu pencarian\n  jamBusyF = true; jamDoneF = false;\n  xTaskNotifyGive(jamTaskH);", "jamSrcRun")

# ---------------------------------------------------------------- musicbt_renphone.ino
mb = sub(mb, '    musOutSpk = p.getBool("spk", false);\n    p.end();\n  }\n  spkSetVolume(musVol);',
         '    musOutSpk = p.getBool("spk", false);\n    jamSetSource((uint8_t)p.getInt("jsrc", jamSrcDefault()));   // AUDIUS_V1\n    p.end();\n  }\n  spkSetVolume(musVol);', "baca jsrc")
mb = sub(mb, '    p.putBool("spk", musOutSpk);\n    p.end();',
         '    p.putBool("spk", musOutSpk);\n    p.putInt("jsrc", (int)jamSrc);   // AUDIUS_V1\n    p.end();', "simpan jsrc")
mb = sub(mb, '    if (onScr && jr < 0) showToast(jr == -1 ? "WiFi belum terhubung" : (jr == -3 ? "RAM tidak cukup" : "Gagal ambil data Jamendo"));',
         '    if (onScr && jr < 0) {                       // AUDIUS_V1: nama sumber ikut pesan\n      static char em[40]; snprintf(em, sizeof(em), "Gagal ambil data %s", jamSrcName());\n      showToast(jr == -1 ? "WiFi belum terhubung" : (jr == -3 ? "RAM tidak cukup" : em));\n    }', "toast gagal")

mb = sub(mb, "#define MJ_GO_W 46\n", "#define MJ_GO_W 46\n#define MJ_SRC_W 54                                 // AUDIUS_V1: chip sumber (Jamendo / Audius)\nstatic int musJamSrcX() { return SCR_W - 12 - MJ_GO_W - 4 - MJ_SRC_W; }\n", "MJ_GO_W")

mb = sub(mb, "static void musDrawJam(LGFX_Sprite& s) {",
"""// AUDIUS_V1: ganti sumber Jamendo <-> Audius. Daftar lama dibuang; kalau ada ketikan, dicari lagi di sumber baru.
static void musJamSwitchSrc() {
  if (jamBusy()) { showToast(RPTR("Masih memuat...")); return; }
  if (musJamKbOn()) { kbVisible = false; kbTarget = nullptr; }
  jamSetSource(jamSrc == JAM_SRC_AUDIUS ? JAM_SRC_JAMENDO : JAM_SRC_AUDIUS);
  musPrefsDirty = true; musPrefsMs = millis();
  showToast(jamSrc == JAM_SRC_AUDIUS ? "Sumber: Audius" : "Sumber: Jamendo");
  musJamOpened = true;
  String q = musJamQuery; q.trim();
  if (q.length() > 0) { musJamTag = -1; musJamFetch(q.c_str(), ""); }
  else                { musJamTag = 0;  musJamFetch("", ""); }
}

static void musDrawJam(LGFX_Sprite& s) {""", "musDrawJam")

mb = sub(mb, "  int by = musJamBoxY(), bx = 12, bw = SCR_W - 12 - MJ_GO_W - 6 - bx;",
             "  int by = musJamBoxY(), bx = 12, bw = musJamSrcX() - 4 - bx;     // AUDIUS_V1: ruang untuk chip sumber", "lebar kotak cari")
mb = sub(mb, '  s.setCursor(bx + 10, by + 7); s.print(q[0] ? q : RPTR("cari lagu / artis (Jamendo)"));',
             '  s.setCursor(bx + 10, by + 7); s.print(q[0] ? q : RPTR("cari lagu / artis"));', "placeholder")
mb = sub(mb, '  nyxAaRR(s, SCR_W - 12 - MJ_GO_W, by, MJ_GO_W, 22, 11, T().accent);\n  musDrawLabel(s, busy ? "..." : RPTR("Cari")',
             '  nyxAaRR(s, musJamSrcX(), by, MJ_SRC_W, 22, 11, chip);            // AUDIUS_V1: chip sumber\n  musDrawLabel(s, jamSrcName(), musJamSrcX(), by, MJ_SRC_W, 22, T().accent2);\n  nyxAaRR(s, SCR_W - 12 - MJ_GO_W, by, MJ_GO_W, 22, 11, T().accent);\n  musDrawLabel(s, busy ? "..." : RPTR("Cari")', "chip sumber")

mb = sub(mb, '    } else if (strcmp(JAM_CLIENT_ID, "ISI_CLIENT_ID_KAMU") == 0) {',
             '    } else if (jamSrc == JAM_SRC_JAMENDO && strcmp(JAM_CLIENT_ID, "ISI_CLIENT_ID_KAMU") == 0) {   // AUDIUS_V1', "pesan client_id")
mb = sub(mb, '      s.setTextColor(T().subtext); s.setCursor(12, tY + 14); s.print("Daftar gratis: devportal.jamendo.com");',
             '      s.setTextColor(T().subtext); s.setCursor(12, tY + 14); s.print("Daftar gratis: devportal.jamendo.com");\n      s.setCursor(12, tY + 28);                              s.print(RPTR("Atau ketuk chip sumber -> Audius (tanpa kunci)."));', "hint Audius")
mb = sub(mb, 's.print(jamLastResult() == -3 ? "RAM tidak cukup." : "Gagal ambil data Jamendo.");',
             '{ char em[40]; snprintf(em, sizeof(em), "Gagal ambil data %s.", jamSrcName()); s.print(jamLastResult() == -3 ? "RAM tidak cukup." : em); }', "pesan gagal")

mb = sub(mb, "  if (rowBox && x >= 12 && x < SCR_W - 12 - MJ_GO_W - 4) {",
             "  if (rowBox && x >= musJamSrcX() - 2 && x < SCR_W - 12 - MJ_GO_W - 4) { vibTap(); musJamSwitchSrc(); needRedraw = true; return; }   // AUDIUS_V1\n  if (rowBox && x >= 12 && x < musJamSrcX() - 2) {", "zona ketuk")

# ---------------------------------------------------------------- docs
cl = sub(cl, "## Unreleased\n", "## Unreleased\n" +
"- New: Audius as a second source for online music (AUDIUS_V1). In the Music app, Online page, a chip next to the search box switches between *Jamendo* and *Audius*; the choice is saved (NVS `music`/`jsrc`). Audius needs no API key, so it is the default when the build has no `JAM_CLIENT_ID`. Search uses `/v1/tracks/search`, the genre chips use `/v1/tracks/trending` (week) with the genre Pop / Rock / Lo-Fi / Jazz, and playing a track fetches `/v1/tracks/<id>/stream`, which redirects to the MP3. Tracks that are not streamable or are stream-gated are skipped. Results use the same list, ranking and playback path as Jamendo (speaker and Bluetooth). Audius streams are usually 320 kbps, so with *Output: BT* the 128 KB ring holds about 3 seconds instead of 8 and a weak Wi-Fi may cause gaps. The response buffer in PSRAM grew from 128 KB to 256 KB. Also fixed: a non-ASCII letter at the very end of a title or artist name was dropped (`jamUnescape`), for both sources. The parser is tested on a PC; the network and UI parts are not yet compiled or tested on a device. Script: `tools/patches/apply_audius.py`\n",
"changelog")

md = sub(md, "In the Music app, the **Online** chip next to the RYNE chip opens a page where you can search by title or artist, or browse popular tracks by tag.",
 "In the Music app, the **Online** chip next to the RYNE chip opens a page where you can search by title or artist, or browse popular tracks by tag. A **source chip** (*Jamendo* / *Audius*) next to the search box switches the catalogue; the choice is saved.", "media.md intro")
md = sub(md, "- **Needs Wi-Fi** and a Jamendo `client_id`",
 "- **Audius** needs no key (public API, `app_name=ren_phone`). Search uses `/v1/tracks/search`; the genre chips show this week's trending tracks of Pop, Rock, Lo-Fi or Jazz (*Hits* = all genres). Tracks that are not streamable or are stream-gated are skipped. Streams are usually 320 kbps, so with *Output: BT* the ring holds about 3 seconds and a weak Wi-Fi may cause gaps. Audius is the default when the build has no Jamendo `client_id`.\n- **Jamendo** needs Wi-Fi and a Jamendo `client_id`", "media.md bullet")

rm = sub(rm, "forks without it get a placeholder and no results).",
 "forks without it get a placeholder and no results). The Audius source needs no key and is the default when the build has no Jamendo id.", "README beta")
rm = sub(rm, "[Online music (Jamendo)](./docs/features/media.md#online-music-jamendo): search or browse by tag",
 "[Online music (Jamendo, Audius)](./docs/features/media.md#online-music-jamendo): search or browse by tag", "README tabel")

# marker: pastikan terpasang di tiap file (komentar sudah memuat AUDIUS_V1 di musjam.h, musicbt, jam_core.h)
for name, txt in (("jam_core.h", jc), ("musjam.h", mj), ("musicbt", mb)):
    if M not in txt: sys.exit("GAGAL: marker %s tidak ada di %s" % (M, name))

if CHECK:
    print("OK (--check): semua anchor cocok, tidak ada file yang diubah"); sys.exit(0)

for p, s in ((JC, jc), (MJ, mj), (MB, mb), (CL, cl), (MD, md), (RM, rm)):
    open(p + ".bak_audius", "w", encoding="utf-8", newline="").write(rd(p)); wr(p, s)
print("OK: %s terpasang (backup: *.bak_audius)" % M)
