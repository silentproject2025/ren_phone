#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""apply_ffat_upload.py (FFAT_UPLOAD_V1)
 - Web File Manager: pilih target upload SD atau flash internal (FFat); daftar file FFat + Download/Hapus.
 - Doom: kalau WAD tidak ada di SD (atau SD tidak terpasang), cari di FFat (kandidat sama).
Patch phone.ino + doom_renphone.ino. Jalankan dari root repo. Idempotent."""
import sys, os

def load(p):
    if not os.path.exists(p): sys.exit("ERROR: %s tidak ketemu" % p)
    t = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in t
    return t.replace("\r\n", "\n"), crlf
def save(p, t, crlf):
    if crlf: t = t.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(t)

class Doc:
    def __init__(s, p): s.p = p; s.t, s.crlf = load(p)
    def rep(s, old, new, label):
        n = s.t.count(old)
        if n != 1: sys.exit("ERROR: '%s' di %s ketemu %d kali (harus 1). Dibatalkan, tidak ada file yg diubah." % (label, s.p, n))
        s.t = s.t.replace(old, new)

ph = Doc("phone.ino"); dm = Doc("doom_renphone.ino")
if "FFAT_UPLOAD_V1" in ph.t and "FFAT_UPLOAD_V1" in dm.t:
    print("[skip] sudah dipatch"); sys.exit(0)

# ---------------- phone.ino ----------------
# 1. form upload: pilih target
ph.rep('''  html += "<div class='card'><h3>Upload File ke ESP32 SD Card</h3>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
''', '''  html += "<div class='card'><h3>Upload File ke ESP32</h3>";   // FFAT_UPLOAD_V1
  html += "<form method='POST' action='/upload' enctype='multipart/form-data' onsubmit=\\"var t=document.getElementById('fsT');this.action='/upload?fs='+(t?t.value:'sd');\\">";
  html += "<select id='fsT' style='margin-bottom:10px;padding:8px;background:#2a2a2a;color:#fff;border:1px solid #444;border-radius:6px;'>";
  if (sdReady) html += String("<option value='sd'") + (sdReady ? " selected" : "") + ">SD Card</option>";
  if (ffatReady) html += String("<option value='ffat'") + (!sdReady ? " selected" : "") + ">Flash internal (FFat) &mdash; sisa " + String((unsigned long)(FFat.totalBytes()-FFat.usedBytes())/1024) + " KB</option>";
  html += "</select><br>";
''', "form upload")

# 2. stream upload
ph.rep('''    if (!sdReady && filename == WALLPAPER_FILE && ffatReady) {
      uploadFile = FFat.open(filename, FILE_WRITE);
    } else {
      uploadFile = SD_MMC.open(filename, FILE_WRITE);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
''', '''    ffatUpFail = false;
    ffatUpName = filename;
    ffatUpToFfat = ffatReady && (webServer.arg("fs") == "ffat" || (!sdReady && filename == WALLPAPER_FILE));   // FFAT_UPLOAD_V1
    if (ffatUpToFfat) {
      uploadFile = FFat.open(filename, FILE_WRITE);
    } else {
      uploadFile = SD_MMC.open(filename, FILE_WRITE);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && !ffatUpFail) {
      size_t w = uploadFile.write(upload.buf, upload.currentSize);
      if (ffatUpToFfat && w != upload.currentSize) ffatUpFail = true;   // FFat penuh -> file dibuang di akhir
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    if (ffatUpToFfat && ffatUpFail) { FFat.remove(ffatUpName); ffatUpFail = false; needRedrawNow(); return; }
''', "stream upload")
ph.rep("void handleUploadStream() {\n", '''static bool   ffatUpToFfat = false;   // FFAT_UPLOAD_V1
static bool   ffatUpFail   = false;
static String ffatUpName;
void handleUploadStream() {
''', "deklarasi stream")

# 3. daftar file FFat
ph.rep('''  html += "</table></div></body></html>";

  webServer.send(200, "text/html", html);''', '''  html += "</table></div>";

  // FFAT_UPLOAD_V1: daftar file di flash internal (FFat)
  if (ffatReady) {
    html += "<div class='card'><h3>Daftar File Flash (FFat)</h3>";
    html += "<p style='color:#aaa;font-size:13px;'>Terpakai " + String((unsigned long)FFat.usedBytes()/1024) + " KB dari " + String((unsigned long)FFat.totalBytes()/1024) + " KB</p>";
    html += "<table><tr><th>Nama File</th><th>Ukuran</th><th>Aksi</th></tr>";
    File rootF = FFat.open("/");
    File ff = rootF.openNextFile();
    int cntF = 0;
    while (ff) {
      if (!ff.isDirectory()) {
        cntF++;
        String fn = String(ff.name());
        if (!fn.startsWith("/")) fn = "/" + fn;
        String fnS = escapeHtml(fn);
        html += "<tr><td>" + fnS + "</td><td>" + String((unsigned long)ff.size()) + " B</td><td>";
        html += "<a href='/download?fs=ffat&file=" + fnS + "'>Download</a> | ";
        html += "<a href='/delete?fs=ffat&file=" + fnS + "' style='color:#ff5252' onclick=\\"return confirm('Hapus file " + fnS + "?')\\">Hapus</a></td></tr>";
      }
      ff = rootF.openNextFile();
    }
    if (cntF == 0) html += "<tr><td colspan='3'>FFat kosong</td></tr>";
    html += "</table></div>";
  }
  html += "</body></html>";

  webServer.send(200, "text/html", html);''', "daftar ffat")

# 4. hapus
ph.rep('''void handleWebDelete() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (SD_MMC.exists(path)) {''', '''void handleWebDelete() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (webServer.arg("fs") == "ffat" && ffatReady && FFat.exists(path)) {   // FFAT_UPLOAD_V1
      FFat.remove(path);
      if (path == WALLPAPER_FILE) loadWallpaper();
      needRedrawNow();
      webServer.sendHeader("Location", "/");
      webServer.send(303);
      return;
    }
    if (SD_MMC.exists(path)) {''', "hapus")

# 5. download
ph.rep('''void handleWebDownload() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (SD_MMC.exists(path)) {''', '''void handleWebDownload() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (webServer.arg("fs") == "ffat" && ffatReady && FFat.exists(path)) {   // FFAT_UPLOAD_V1
      File ffd = FFat.open(path, FILE_READ);
      webServer.streamFile(ffd, "application/octet-stream");
      ffd.close();
      return;
    }
    if (SD_MMC.exists(path)) {''', "download")

# ---------------- doom_renphone.ino ----------------
dm.rep('''static const char* doomFindWad(){
  for(size_t i=0;i<sizeof(DOOM_WAD_CANDIDATES)/sizeof(DOOM_WAD_CANDIDATES[0]);i++)
    if(SD_MMC.exists(DOOM_WAD_CANDIDATES[i])) return DOOM_WAD_CANDIDATES[i];
  return nullptr;
}''', '''static fs::FS* doomWadFs = nullptr;   // FFAT_UPLOAD_V1: filesystem tempat WAD ditemukan
static const char* doomFindWad(){
  const size_t n = sizeof(DOOM_WAD_CANDIDATES)/sizeof(DOOM_WAD_CANDIDATES[0]);
  if(sdReady)   for(size_t i=0;i<n;i++) if(SD_MMC.exists(DOOM_WAD_CANDIDATES[i])){ doomWadFs=&SD_MMC; return DOOM_WAD_CANDIDATES[i]; }
  if(ffatReady) for(size_t i=0;i<n;i++) if(FFat.exists(DOOM_WAD_CANDIDATES[i])){  doomWadFs=&FFat;   return DOOM_WAD_CANDIDATES[i]; }
  return nullptr;
}''', "doom find")
dm.rep("  DG_FS = &SD_MMC;\n", "  DG_FS = doomWadFs ? doomWadFs : &SD_MMC;   // FFAT_UPLOAD_V1\n", "dg_fs")
dm.rep('''  if(!sdReady){
    doomErrorMsg = "Kartu SD tidak terpasang.\\nDOOM butuh WAD di SD.";
    return;
  }
  doomWadSel = doomFindWad();''', '''  if(!sdReady && !ffatReady){   // FFAT_UPLOAD_V1
    doomErrorMsg = "Kartu SD tidak terpasang\\ndan FFat tidak tersedia.\\nDOOM butuh WAD.";
    return;
  }
  doomWadSel = doomFindWad();''', "doom sd check")
dm.rep('doomErrorMsg = String("WAD tidak ditemukan di SD.\\nTaruh di root SD: doom2.wad,\\ndoom1.wad, freedoom2.wad, plutonia.wad\\natau tnt.wad (nama persis).");',
       'doomErrorMsg = String("WAD tidak ditemukan di SD/FFat.\\nTaruh di root: doom2.wad, doom1.wad,\\nfreedoom2.wad, plutonia.wad atau tnt.wad\\n(nama persis; FFat via web upload).");', "doom msg")

save("phone.ino", ph.t, ph.crlf); save("doom_renphone.ino", dm.t, dm.crlf)
print("OK: FFAT_UPLOAD_V1 diterapkan")
