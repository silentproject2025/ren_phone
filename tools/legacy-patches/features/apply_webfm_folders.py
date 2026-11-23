#!/usr/bin/env python3
# ARSIP: sudah terpasang; anchor-nya sudah tidak ada di phone.ino saat ini, jadi skrip ini akan GAGAL kalau dijalankan lagi.
# WEBFM_DIR_V1 -- Web File Manager mendukung FOLDER:
#   * jelajah folder SD & FFat (folder dulu, breadcrumb, "naik satu level"), daftar dikirim bertahap (hemat RAM)
#   * buat folder baru, hapus folder beserta isinya (rekursif), hapus/unduh/edit berkas di dalam folder
#   * edit teks kini juga untuk FFat; unduhan memakai nama berkas asli; form upload otomatis mengisi folder aktif
#   * semua jalur divalidasi (tanpa "..", "//", "\\")
# Pakai (dari root repo):  python3 tools/legacy-patches/features/apply_webfm_folders.py
import sys, shutil
P = "firmware/phone/phone.ino"
s = open(P, encoding="utf-8").read()
if "WEBFM_DIR_V1" in s:
    print("Sudah terpasang (phone.ino)."); 
else:
    orig = s
    def rep(old, new, n=1):
        global s
        c = s.count(old)
        if c != n:
            sys.exit("GAGAL: pola ditemukan %d kali (harus %d):\n%s" % (c, n, old[:170]))
        s = s.replace(old, new)
    def cut(start, end, new):
        """ganti dari awal 'start' sampai tepat sebelum 'end'"""
        global s
        if s.count(start) != 1 or s.count(end) != 1:
            sys.exit("GAGAL: penanda tidak unik:\n%s\n%s" % (start[:100], end[:100]))
        a = s.index(start); b = s.index(end)
        if b < a: sys.exit("GAGAL: urutan penanda salah: " + start[:60])
        s = s[:a] + new + s[b:]

    HELPERS = r"""// ===== WEBFM_DIR_V1 BEGIN -- Web File Manager: folder (jelajah, buat, hapus rekursif) di SD & FFat =====
#define WEBFM_MAX_ROWS 150
static bool webFsOk(bool ffat) { return ffat ? ffatReady : sdReady; }
static fs::FS& webFsSel(bool ffat) { if (ffat) return FFat; return SD_MMC; }
static bool webPathOk(const String& p) {                          // jalur absolut, tanpa '..', '//' atau '\\'
  if (p.length() < 1 || p.length() > 120 || p[0] != '/') return false;
  if (p.indexOf("..") >= 0 || p.indexOf("//") >= 0 || p.indexOf('\\') >= 0) return false;
  for (size_t i = 0; i < p.length(); i++) { if ((uint8_t)p[i] < 32) return false; }
  return true;
}
static bool webNameOk(const String& n) {                          // nama satu folder baru
  if (n.length() < 1 || n.length() > 40 || n.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if ((uint8_t)c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
  }
  char last = n[n.length() - 1];
  return !(last == ' ' || last == '.');
}
static String webUrlEnc(const String& s) {
  static const char* hx = "0123456789ABCDEF";
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/' || c == '~') o += (char)c;
    else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
  }
  return o;
}
static String webAttr(const String& s) { String o = escapeHtml(s); o.replace("'", "&#39;"); return o; }   // aman utk atribut '...'
static String webParentDir(const String& p) { int i = p.lastIndexOf('/'); return i <= 0 ? String("/") : p.substring(0, i); }
static String webJoin(const String& dir, const String& name) { return dir == "/" ? String("/") + name : dir + "/" + name; }
static String webListUrl(bool ffat, const String& dir) { return String("/?fs=") + (ffat ? "ffat" : "sd") + "&dir=" + webUrlEnc(dir); }

// hapus folder beserta isinya (iterasi dimulai ulang setelah tiap perubahan isi folder)
static bool webRmTree(fs::FS& F, const String& path, int depth) {
  File d = F.open(path);
  if (!d) return false;
  if (!d.isDirectory()) { d.close(); return F.remove(path); }
  for (int guard = 0; guard < 400; guard++) {
    File e = d.openNextFile();
    if (!e) break;
    String nm = String(e.name()); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
    bool isDir = e.isDirectory(); e.close(); d.close();
    String child = webJoin(path, nm);
    if (isDir) { if (depth < 6) webRmTree(F, child, depth + 1); else F.rmdir(child); }
    else F.remove(child);
    yield();
    d = F.open(path); if (!d) return false;
  }
  d.close();
  return F.rmdir(path);
}

// daftar isi folder: folder dulu lalu berkas, dikirim bertahap (hemat RAM)
static void webSendListing(bool ffat, const String& dir) {
  const char* fsn = ffat ? "ffat" : "sd";
  String h; h.reserve(1500);
  h = "<div class='card'><h3>Penjelajah Berkas</h3><p>";
  if (sdReady)   h += String("<a href='/?fs=sd'") + (!ffat ? " style='font-weight:bold;text-decoration:underline;'" : "") + ">SD Card</a> &nbsp; ";
  if (ffatReady) h += String("<a href='/?fs=ffat'") + (ffat ? " style='font-weight:bold;text-decoration:underline;'" : "") + ">Flash (FFat)</a>";
  h += "</p>";
  if (ffat && ffatReady) h += "<p style='color:#aaa;font-size:13px;'>Terpakai " + String((unsigned long)FFat.usedBytes() / 1024) + " KB dari " + String((unsigned long)FFat.totalBytes() / 1024) + " KB</p>";
  h += "<p style='word-break:break-all;'><a href='" + webListUrl(ffat, "/") + "'>" + (ffat ? "FFat:" : "SD:") + "</a>";
  String acc;
  for (int i = 1; i < (int)dir.length(); ) {
    int j = dir.indexOf('/', i); if (j < 0) j = dir.length();
    String seg = dir.substring(i, j);
    acc += "/" + seg;
    h += " / <a href='" + webListUrl(ffat, acc) + "'>" + escapeHtml(seg) + "</a>";
    i = j + 1;
  }
  h += "</p>";
  if (dir.length() > 1) h += "<p><a href='" + webListUrl(ffat, webParentDir(dir)) + "'>&#8593; Naik satu level</a></p>";
  h += "<form method='POST' action='/mkdir' style='margin:8px 0;'><input type='hidden' name='fs' value='" + String(fsn) + "'><input type='hidden' name='dir' value='" + webAttr(dir) + "'>";
  h += "<input type='text' name='name' maxlength='40' placeholder='Nama folder baru' required style='padding:8px;background:#2a2a2a;color:#fff;border:1px solid #444;border-radius:6px;'> <input type='submit' class='btn' value='Buat Folder'></form>";
  h += "<table><tr><th>Nama</th><th>Ukuran</th><th>Aksi</th></tr>";
  webServer.sendContent(h);

  int nRows = 0; bool more = false;
  String chunk; chunk.reserve(1700);
  if (!webFsOk(ffat)) {
    chunk += "<tr><td colspan='3'>Penyimpanan tidak tersedia</td></tr>";
  } else {
    fs::FS& F = webFsSel(ffat);
    File probe = F.open(dir);
    if (!probe || !probe.isDirectory()) {
      chunk += "<tr><td colspan='3'>Folder tidak ditemukan</td></tr>";
      if (probe) probe.close();
    } else {
      probe.close();
      for (int pass = 0; pass < 2 && !more; pass++) {
        File d = F.open(dir);
        if (!d) break;
        File e = d.openNextFile();
        while (e) {
          bool isDir = e.isDirectory();
          if (isDir == (pass == 0)) {
            if (nRows >= WEBFM_MAX_ROWS) { more = true; e.close(); break; }
            nRows++;
            String nm = String(e.name()); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
            unsigned long sz = (unsigned long)e.size();
            String full = webJoin(dir, nm);
            String q = String("fs=") + fsn + "&file=" + webUrlEnc(full);
            String nmS = escapeHtml(nm);
            if (isDir) {
              String lu = webListUrl(ffat, full);
              chunk += "<tr><td>&#128193; <a href='" + lu + "'>" + nmS + "/</a></td><td>&mdash;</td><td><a href='" + lu + "'>Buka</a> | ";
              chunk += "<a href='/delete?" + q + "&back=" + webUrlEnc(dir) + "' style='color:#ff5252' onclick=\"return confirm('Hapus folder ini beserta SELURUH isinya?')\">Hapus</a></td></tr>";
            } else {
              chunk += "<tr><td>" + nmS + "</td><td>" + (sz < 1024 ? String(sz) + " B" : String(sz / 1024) + " KB") + "</td><td>";
              if (isEditableExt(nm)) chunk += "<a href='/edit?" + q + "' style='color:#ffca28;'>Edit</a> | ";
              chunk += "<a href='/download?" + q + "'>Download</a> | ";
              chunk += "<a href='/delete?" + q + "&back=" + webUrlEnc(dir) + "' style='color:#ff5252' onclick=\"return confirm('Hapus berkas ini?')\">Hapus</a></td></tr>";
            }
            if (chunk.length() > 1200) { webServer.sendContent(chunk); chunk = ""; }
          }
          e.close();
          e = d.openNextFile();
        }
        d.close();
      }
      if (nRows == 0) chunk += "<tr><td colspan='3'>Folder kosong</td></tr>";
      if (more) chunk += "<tr><td colspan='3' style='color:#ffca28;'>Daftar dipotong (maks. 150 entri). Buka subfolder untuk melihat sisanya.</td></tr>";
    }
  }
  chunk += "</table></div></body></html>";
  webServer.sendContent(chunk);
}

void handleWebMkdir() {
  if (!webGuard(true)) return;
  bool ffat = ffatReady && webServer.arg("fs") == "ffat";
  String dir = webServer.arg("dir");
  String nm = webServer.arg("name"); nm.trim();
  if (!webPathOk(dir)) dir = "/";
  if (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);
  if (webNameOk(nm) && webFsOk(ffat)) {
    String np = webJoin(dir, nm);
    if (np.length() <= 120) {
      fs::FS& F = webFsSel(ffat);
      if (!F.exists(np)) F.mkdir(np);
      needRedrawNow();
    }
  }
  webServer.sendHeader("Location", webListUrl(ffat, dir));
  webServer.send(303);
}
// ===== WEBFM_DIR_V1 END =====

"""

    # 1) helper + /mkdir, ditaruh sebelum bagian ganti sandi
    rep("// WEBPW_WEB_V1 -- ganti sandi / nama pengguna dari halaman web (wajib isi sandi lama)\n",
        HELPERS + "// WEBPW_WEB_V1 -- ganti sandi / nama pengguna dari halaman web (wajib isi sandi lama)\n")

    # 2) handleWebRoot: baca ?fs= & ?dir=
    rep("void handleWebRoot() {\n  if (!webGuard(false)) return;   // WEBAUTH_V1\n",
        "void handleWebRoot() {\n  if (!webGuard(false)) return;   // WEBAUTH_V1\n"
        "  // WEBFM_DIR_V1: ?fs=sd|ffat&dir=/jalur\n"
        "  String fmFsArg = webServer.arg(\"fs\");\n"
        "  bool fmFfat = ffatReady && (fmFsArg == \"ffat\" || (fmFsArg != \"sd\" && !sdReady));\n"
        "  String fmDir = webServer.arg(\"dir\");\n"
        "  if (!webPathOk(fmDir)) fmDir = \"/\";\n"
        "  if (fmDir.length() > 1 && fmDir.endsWith(\"/\")) fmDir.remove(fmDir.length() - 1);\n")
    # 3) form upload mengikuti folder aktif
    rep("(sdReady ? \" selected\" : \"\") + \">SD Card</option>\"", "(!fmFfat ? \" selected\" : \"\") + \">SD Card</option>\"")
    rep("String(\"<option value='ffat'\") + (!sdReady ? \" selected\" : \"\")", "String(\"<option value='ffat'\") + (fmFfat ? \" selected\" : \"\")")
    rep("html += \"<input id='dirT' type='text' placeholder='Folder tujuan",
        "html += \"<input id='dirT' type='text' value='\" + webAttr(fmDir == \"/\" ? String(\"\") : fmDir) + \"' placeholder='Folder tujuan")
    # 4) dua kartu daftar lama -> penjelajah folder
    cut("  html += \"<div class='card'><h3>Daftar File SD Card</h3><table>",
        "void handleWebEditGet() {",
        "  // WEBFM_DIR_V1: penjelajah folder (SD / FFat), dikirim bertahap supaya hemat RAM\n"
        "  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);\n"
        "  webServer.send(200, \"text/html\", html);\n"
        "  webSendListing(fmFfat, fmDir);\n"
        "  webServer.sendContent(\"\");\n"
        "}\n\n")
    # 5) Edit (GET)
    rep("""  String content = "";
  bool fileFound = false;
  if (sdReady && SD_MMC.exists(fname)) {
    File f = SD_MMC.open(fname, FILE_READ);
    if (f) { content = f.readString(); f.close(); fileFound = true; }
  }
  String fnameSafe = escapeHtml(fname);
""", """  if (!webPathOk(fname)) { webServer.send(400, "text/plain", "Path tidak valid"); return; }   // WEBFM_DIR_V1
  bool editFfat = ffatReady && (webServer.arg("fs") == "ffat" || (webServer.arg("fs") != "sd" && !sdReady));
  String backUrl = webListUrl(editFfat, webParentDir(fname));
  String fsQ = editFfat ? "&fs=ffat" : "";
  String content = "";
  bool fileFound = false;
  if (webFsOk(editFfat)) {
    fs::FS& EF = webFsSel(editFfat);
    if (EF.exists(fname)) {
      File f = EF.open(fname, FILE_READ);
      if (f && !f.isDirectory()) { content = f.readString(); fileFound = true; }
      if (f) f.close();
    }
  }
  String fnameSafe = escapeHtml(fname);
""")
    rep("File tidak ditemukan di SD Card.", "File tidak ditemukan.")
    rep("html += \"<a href='/' class='btn-back'>&lt; Kembali</a>\";", "html += \"<a href='\" + backUrl + \"' class='btn-back'>&lt; Kembali</a>\";", 2)
    rep("html += \"<a href='/' class='btn-back'>&lt; Batal</a>\";", "html += \"<a href='\" + backUrl + \"' class='btn-back'>&lt; Batal</a>\";")
    rep("html += \"<a href='/download?file=\" + fnameSafe + \"' class='btn-back'>Download File Asli</a>\";",
        "html += \"<a href='/download?file=\" + webUrlEnc(fname) + fsQ + \"' class='btn-back'>Download File Asli</a>\";")
    rep("    html += \"<input type='hidden' name='file' value='\" + fnameSafe + \"'>\";\n",
        "    html += \"<input type='hidden' name='file' value='\" + fnameSafe + \"'>\";\n"
        "    html += \"<input type='hidden' name='fs' value='\" + String(editFfat ? \"ffat\" : \"sd\") + \"'>\";\n", 2)
    # 6) Edit (POST)
    cut("void handleWebEditPost() {", "static bool   ffatUpToFfat = false;",
"""void handleWebEditPost() {
  if (!webGuard(true)) return;    // WEBAUTH_V1
  bool saveFfat = ffatReady && (webServer.arg("fs") == "ffat" || (webServer.arg("fs") != "sd" && !sdReady));   // WEBFM_DIR_V1
  String backTo = "/";
  if (webServer.hasArg("file") && webServer.hasArg("content")) {
    String fname = webServer.arg("file");
    String content = webServer.arg("content");
    if (!fname.startsWith("/")) fname = "/" + fname;
    if (webPathOk(fname) && webFsOk(saveFfat)) {
      backTo = webListUrl(saveFfat, webParentDir(fname));
      File f = webFsSel(saveFfat).open(fname, FILE_WRITE);
      if (f) {
        f.print(content);
        f.close();
        if (!saveFfat) {
          if (fname == NOTE_FILE) loadNote();
          if (fname == GEMINI_KEY_FILE) loadGeminiKey();
        }
        needRedrawNow();
      }
    }
  }
  webServer.sendHeader("Location", backTo);
  webServer.send(303);
}

""")
    # 7) Upload: batas panjang folder + kembali ke folder yang sama
    rep("upDir.length() < 48", "upDir.length() < 120")
    rep("""    if (!webGuard(true)) return;   // WEBAUTH_V1
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  }, handleUploadStream);""", """    if (!webGuard(true)) return;   // WEBAUTH_V1
    { String ud = webServer.arg("dir"); bool uf = ffatReady && webServer.arg("fs") == "ffat";   // WEBFM_DIR_V1
      webServer.sendHeader("Location", (webPathOk(ud) && ud.length() > 1) ? webListUrl(uf, ud) : String(uf ? "/?fs=ffat" : "/")); }
    webServer.send(303);
  }, handleUploadStream);""")
    # 8) Hapus (berkas ATAU folder)
    cut("void handleWebDelete() {", "void handleWebDownload() {",
"""void handleWebDelete() {
  if (!webGuard(true)) return;    // WEBAUTH_V1
  String path = webServer.arg("file");
  bool wantFfat = (webServer.arg("fs") == "ffat");
  String back = "/";
  if (webServer.hasArg("back")) { String b = webServer.arg("back"); if (webPathOk(b)) back = webListUrl(wantFfat, b); }   // WEBFM_DIR_V1
  if (webServer.hasArg("file") && webPathOk(path) && path != "/") {
    fs::FS* F = nullptr;                                          // fs= dulu, lalu cadangan lama (SD, lalu FFat)
    if (wantFfat && ffatReady && FFat.exists(path)) F = &FFat;    // FFAT_UPLOAD_V1
    else if (sdReady && SD_MMC.exists(path)) F = &SD_MMC;
    else if (ffatReady && FFat.exists(path)) F = &FFat;           // v47: wallpaper fallback di FFat
    if (F) {
      File t = F->open(path);
      bool isD = t && t.isDirectory();
      if (t) t.close();
      if (isD) webRmTree(*F, path, 0); else F->remove(path);
      if (path == WALLPAPER_FILE) loadWallpaper();                // v47: wallpaper dihapus -> balik ke warna solid tema, live
      needRedrawNow();
    }
  }
  webServer.sendHeader("Location", back);
  webServer.send(303);
}

""")
    # 9) Unduh
    cut("void handleWebDownload() {", "// v27: halaman input manual Gemini API Key",
"""void handleWebDownload() {
  if (!webGuard(false)) return;   // WEBAUTH_V1
  String path = webServer.arg("file");
  if (webServer.hasArg("file") && webPathOk(path)) {   // WEBFM_DIR_V1
    fs::FS* F = nullptr;
    if (webServer.arg("fs") == "ffat" && ffatReady && FFat.exists(path)) F = &FFat;   // FFAT_UPLOAD_V1
    else if (sdReady && SD_MMC.exists(path)) F = &SD_MMC;
    if (F) {
      File f = F->open(path, FILE_READ);
      if (f && !f.isDirectory()) {
        String bn = path.substring(path.lastIndexOf('/') + 1); bn.replace("\\"", "_");
        webServer.sendHeader("Content-Disposition", "attachment; filename=\\"" + bn + "\\"");
        webServer.streamFile(f, "application/octet-stream");
        f.close();
        return;
      }
      if (f) f.close();
    }
  }
  webServer.send(404, "text/plain", "File Not Found");
}

""")
    # 10) rute /mkdir
    rep("  webServer.on(\"/delete\", HTTP_GET, handleWebDelete);\n",
        "  webServer.on(\"/delete\", HTTP_GET, handleWebDelete);\n  webServer.on(\"/mkdir\", HTTP_POST, handleWebMkdir);   // WEBFM_DIR_V1\n")
    shutil.copy(P, P + ".bak_webfm")
    open(P, "w", encoding="utf-8").write(s)
    print("OK: phone.ino (WEBFM_DIR_V1). Cadangan: " + P + ".bak_webfm")

# ---- dokumentasi ----
def doc(path, old, new):
    try:
        t = open(path, encoding="utf-8").read()
    except FileNotFoundError:
        print("lewati (tidak ada):", path); return
    if new in t:
        print("lewati (sudah ada):", path); return
    if t.count(old) != 1:
        print("PERINGATAN: pola dokumen tidak cocok, dilewati:", path); return
    shutil.copy(path, path + ".bak_webfm")
    open(path, "w", encoding="utf-8").write(t.replace(old, new))
    print("OK:", path)

doc("docs/configuration.md",
    "While WiFi is on, open `http://<device-ip>/` to upload, edit, download and delete files, set the Gemini key and change the wallpaper.",
    "While WiFi is on, open `http://<device-ip>/` to browse the SD card and the internal flash (FFat) folder by folder, create folders, upload, edit, download and delete files and folders (deleting a folder also deletes everything inside it), set the Gemini key and change the wallpaper. The upload form fills in the folder you are currently viewing.")
doc("CHANGELOG.md", "## Unreleased\n",
    "## Unreleased\n- Added: the Web File Manager now supports folders. Browse the SD card and FFat folder by folder (folders first, breadcrumb, up one level), create new folders, delete a folder together with its contents, and download, edit or delete files inside folders. Text files in FFat can now be edited too, downloads keep the original file name, and the upload form fills in the folder you are viewing. The listing is sent in parts to save RAM and shows at most 150 entries per folder\n")
