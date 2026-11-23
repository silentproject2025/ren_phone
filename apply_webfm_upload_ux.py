#!/usr/bin/env python3
"""WEBFM_UPLOAD_UX_V1 -- Upload di Web File Manager jadi jauh lebih mudah:
  * tarik & lepas (drag & drop) file ATAU folder utuh ke kotak upload (struktur subfolder ikut dibuat otomatis)
  * tombol "Pilih file" (bisa banyak sekaligus) dan "Pilih folder"
  * antrean upload: progress bar per file, kecepatan KB/s, status Selesai / Gagal + alasannya
  * daftar file otomatis dimuat ulang setelah semua upload sukses
  * peringatan kalau tab ditutup saat upload masih berjalan
  * server membalas OK/gagal (penyimpanan penuh, tidak bisa menulis) lewat ?ajax=1, bukan redirect diam-diam
Kode JS/CSS dilayani dari PROGMEM lewat /up.js (tidak membebani RAM String halaman).
Cocok dengan atau tanpa apply_webfm_folders.py (WEBFM_DIR_V1). Idempoten.
Jalankan dari ROOT repo:  python3 apply_webfm_upload_ux.py"""
import sys, os, shutil

P = "firmware/phone/phone.ino"
if not os.path.exists(P):
    sys.exit("Jalankan dari root repo (folder berisi firmware/phone/phone.ino)")
s = open(P, encoding="utf-8").read()
if "WEBFM_UPLOAD_UX_V1" in s:
    print("SKIP", P, "(sudah terpasang)"); sys.exit(0)
HAS_DIR = "WEBFM_DIR_V1" in s

def rep(old, new, n=1):
    global s
    c = s.count(old)
    if c != n:
        sys.exit("GAGAL: pola ditemukan %d kali (harus %d):\n  %s" % (c, n, old[:120]))
    s = s.replace(old, new)

JS = r'''(function(){
var d=document,$=function(i){return d.getElementById(i)};
var st=d.createElement('style');
st.textContent="#dz{border:2px dashed #555;border-radius:12px;padding:22px 10px;text-align:center;color:#bbb;cursor:pointer;margin:6px 0 10px;transition:.15s}"
+"#dz.hv{border-color:#00e5ff;background:#0b2a30;color:#fff}#dz b{color:#00e5ff}"
+".ubt{background:#2a2a2a;color:#fff;border:1px solid #555;border-radius:6px;padding:8px 12px;margin:0 6px 8px 0;cursor:pointer}"
+".ur{background:#262626;border-radius:8px;padding:8px 10px;margin:6px 0;font-size:13px}"
+".un{word-break:break-all}.ub{height:6px;background:#3a3a3a;border-radius:3px;margin:6px 0 4px;overflow:hidden}"
+".ub i{display:block;height:100%;width:0;background:#00e5ff;transition:width .1s}"
+".us{color:#aaa;font-size:12px}.ur.ok .ub i{background:#4caf50}.ur.ok .us{color:#4caf50}"
+".ur.er .ub i{background:#ff5252}.ur.er .us{color:#ff5252}#sm{font-weight:bold;margin:8px 0 0}";
d.head.appendChild(st);
var dz=$('dz'),fi=$('fi'),fd=$('fd'),ul=$('ul'),sm=$('sm'),fsT=$('fsT'),dirT=$('dirT');
var q=[],busy=false,fails=0,done=0;
function fmt(n){return n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(2)+' MB'}
function base(){var x=(dirT.value||'').trim().replace(/\\/g,'/');if(x&&x[0]!='/')x='/'+x;while(x.length>1&&x.slice(-1)=='/')x=x.slice(0,-1);return x=='/'?'':x}
function add(f,rel){
 var r=d.createElement('div');r.className='ur';
 r.innerHTML='<div class="un"></div><div class="ub"><i></i></div><div class="us">Menunggu</div>';
 r.firstChild.textContent=(rel?rel+'/':'')+f.name+' ('+fmt(f.size)+')';
 ul.appendChild(r);q.push({f:f,rel:rel,row:r});
}
function bad(it,msg){it.row.className='ur er';it.row.querySelector('.us').textContent=msg;fails++}
function go(){if(!busy){if(!q.length)return;fails=0;done=0;sm.textContent='';next()}}
function fin(){
 sm.textContent=done+' file terunggah'+(fails?', '+fails+' gagal':'');
 sm.style.color=fails?'#ff5252':'#4caf50';
 if(!fails&&done)setTimeout(function(){location.reload()},1200);
}
function next(){
 if(!q.length){busy=false;fin();return}
 busy=true;
 var it=q.shift(),bar=it.row.querySelector('i'),sx=it.row.querySelector('.us');
 var dir=base()+(it.rel?'/'+it.rel:'');
 if(dir.length>119||dir.indexOf('..')>=0){bad(it,'Jalur folder terlalu panjang / tidak valid');next();return}
 var x=new XMLHttpRequest(),t0=Date.now();
 x.open('POST','/upload?ajax=1&fs='+encodeURIComponent(fsT.value)+'&dir='+encodeURIComponent(dir));
 x.upload.onprogress=function(e){
  if(!e.lengthComputable)return;
  var p=e.loaded/e.total*100;bar.style.width=p+'%';
  sx.textContent=e.loaded>=e.total?'Menyimpan ke penyimpanan...':Math.round(p)+'% \u00b7 '+Math.round(e.loaded/1024/Math.max(.1,(Date.now()-t0)/1000))+' KB/s';
 };
 x.onload=function(){
  if(x.status==200){it.row.className='ur ok';bar.style.width='100%';sx.textContent='Selesai ('+((Date.now()-t0)/1000).toFixed(1)+' dtk)';done++}
  else bad(it,(x.responseText||'Gagal')+' ['+x.status+']');
  next();
 };
 x.onerror=function(){bad(it,'Koneksi terputus');next()};
 var fm=new FormData();fm.append('upload',it.f,it.f.name);
 x.send(fm);
}
function readAll(r){return new Promise(function(ok){var a=[];(function g(){r.readEntries(function(b){if(b.length){a=a.concat([].slice.call(b));g()}else ok(a)},function(){ok(a)})})()})}
function walk(en,path){
 if(en.isFile)return new Promise(function(ok){en.file(function(f){add(f,path);ok()},ok)});
 if(en.isDirectory){var p=path?path+'/'+en.name:en.name;
  return readAll(en.createReader()).then(function(l){return l.reduce(function(pr,e){return pr.then(function(){return walk(e,p)})},Promise.resolve())})}
 return Promise.resolve();
}
['dragenter','dragover'].forEach(function(n){dz.addEventListener(n,function(e){e.preventDefault();dz.classList.add('hv')})});
['dragleave','drop'].forEach(function(n){dz.addEventListener(n,function(){dz.classList.remove('hv')})});
dz.addEventListener('drop',function(e){
 e.preventDefault();
 var it=e.dataTransfer.items,es=[],i;
 if(it&&it.length&&it[0].webkitGetAsEntry){
  for(i=0;i<it.length;i++){var en=it[i].webkitGetAsEntry();if(en)es.push(en)}
  es.reduce(function(pr,en){return pr.then(function(){return walk(en,'')})},Promise.resolve()).then(go);
 }else{[].forEach.call(e.dataTransfer.files,function(f){add(f,'')});go()}
});
dz.onclick=function(){fi.click()};
$('bF').onclick=function(){fi.click()};
$('bD').onclick=function(){fd.click()};
fi.onchange=function(){[].forEach.call(fi.files,function(f){add(f,'')});fi.value='';go()};
fd.onchange=function(){[].forEach.call(fd.files,function(f){
 var rp=f.webkitRelativePath||f.name,k=rp.lastIndexOf('/');add(f,k>0?rp.slice(0,k):'')});fd.value='';go()};
window.onbeforeunload=function(){if(busy)return 'Upload masih berjalan'};
})();'''
assert ')JS"' not in JS

# 1) JS di PROGMEM (+ rute /up.js didaftarkan di langkah 5)
rep("static bool   ffatUpToFfat = false;   // FFAT_UPLOAD_V1\n",
    "// WEBFM_UPLOAD_UX_V1 -- JS/CSS halaman upload (dilayani dari flash lewat /up.js)\n"
    "static const char WEBUP_JS[] PROGMEM = R\"JS(" + JS + ")JS\";\n"
    "static bool   webUpOk      = false;   // WEBFM_UPLOAD_UX_V1: hasil upload terakhir (utk balasan ?ajax=1)\n"
    "static bool   ffatUpToFfat = false;   // FFAT_UPLOAD_V1\n")

# 2) hasil upload: reset di awal, gagal bila file tak bisa dibuka / tulis tak lengkap
rep("void handleUploadStream() {\n  HTTPUpload& upload = webServer.upload();\n",
    "void handleUploadStream() {\n  HTTPUpload& upload = webServer.upload();\n"
    "  if (upload.status == UPLOAD_FILE_START) webUpOk = false;   // WEBFM_UPLOAD_UX_V1\n")
rep("      uploadFile = SD_MMC.open(filename, FILE_WRITE);\n    }\n  } else if (upload.status == UPLOAD_FILE_WRITE) {",
    "      uploadFile = SD_MMC.open(filename, FILE_WRITE);\n    }\n"
    "    webUpOk = (bool)uploadFile;   // WEBFM_UPLOAD_UX_V1\n"
    "  } else if (upload.status == UPLOAD_FILE_WRITE) {")
rep("      if (ffatUpToFfat && w != upload.currentSize) ffatUpFail = true;   // FFat penuh -> file dibuang di akhir\n",
    "      if (ffatUpToFfat && w != upload.currentSize) ffatUpFail = true;   // FFat penuh -> file dibuang di akhir\n"
    "      if (w != upload.currentSize) webUpOk = false;   // WEBFM_UPLOAD_UX_V1 (SD/FFat penuh)\n")
rep("    if (ffatUpToFfat && ffatUpFail) { FFat.remove(ffatUpName); ffatUpFail = false; needRedrawNow(); return; }",
    "    if (ffatUpToFfat && ffatUpFail) { FFat.remove(ffatUpName); ffatUpFail = false; webUpOk = false; needRedrawNow(); return; }")

# 3) batas panjang folder (hanya kalau patch folder belum menaikkannya)
if "upDir.length() < 48" in s:
    rep("upDir.length() < 48", "upDir.length() < 120")

# 4) balasan ?ajax=1 di rute /upload
rep('  webServer.on("/upload", HTTP_POST, [](){\n    if (!webGuard(true)) return;   // WEBAUTH_V1\n',
    '  webServer.on("/upload", HTTP_POST, [](){\n    if (!webGuard(true)) return;   // WEBAUTH_V1\n'
    '    if (webServer.arg("ajax") == "1") {   // WEBFM_UPLOAD_UX_V1\n'
    '      if (webUpOk) webServer.send(200, "text/plain", "OK");\n'
    '      else webServer.send(507, "text/plain", "Gagal menyimpan (penyimpanan penuh / tidak bisa menulis)");\n'
    '      return;\n'
    '    }\n')

# 5) rute /up.js
rep('  webServer.on("/delete", HTTP_GET, handleWebDelete);\n',
    '  webServer.on("/up.js", HTTP_GET, [](){   // WEBFM_UPLOAD_UX_V1\n'
    '    if (!webGuard(false)) return;\n'
    '    webServer.sendHeader("Cache-Control", "no-store");\n'
    '    webServer.send_P(200, "application/javascript", WEBUP_JS);\n'
    '  });\n'
    '  webServer.on("/delete", HTTP_GET, handleWebDelete);\n')

# 6) kartu upload di halaman utama: ganti seluruh form lama
a = s.index("  html += \"<div class='card'><h3>Upload File ke ESP32</h3>\";")
b = s.index("  html += \"<div class='card'><h3>Gemini API Key</h3>\";")
if b < a: sys.exit("GAGAL: urutan penanda kartu upload salah")
if HAS_DIR:
    selSd, selFf = "(!fmFfat ? \" selected\" : \"\")", "(fmFfat ? \" selected\" : \"\")"
    dirVal = "webAttr(fmDir == \"/\" ? String(\"\") : fmDir)"
else:
    selSd, selFf = "\" selected\"", "(!sdReady ? \" selected\" : \"\")"
    dirVal = "String(\"\")"
card = '''  html += "<div class='card'><h3>Upload File ke ESP32</h3>";   // FFAT_UPLOAD_V1 / WEBFM_UPLOAD_UX_V1
  html += "<select id='fsT' style='margin-bottom:10px;padding:8px;background:#2a2a2a;color:#fff;border:1px solid #444;border-radius:6px;'>";
  if (sdReady) html += String("<option value='sd'") + (''' + selSd + ''') + ">SD Card</option>";
  if (ffatReady) html += String("<option value='ffat'") + (''' + selFf + ''') + ">Flash internal (FFat) &mdash; sisa " + String((unsigned long)(FFat.totalBytes()-FFat.usedBytes())/1024) + " KB</option>";
  html += "</select><br>";
  html += "<input id='dirT' type='text' value='" + ''' + dirVal + ''' + "' placeholder='Folder tujuan (opsional), mis. /apps/hello' style='margin-bottom:10px;padding:8px;width:90%;background:#2a2a2a;color:#fff;border:1px solid #444;border-radius:6px;'>";   // NYXFS_DUAL_V1
  html += "<div id='dz'><b>Tarik &amp; lepas</b> file atau folder ke sini<br><span style='font-size:12px'>atau ketuk area ini untuk memilih file</span></div>";
  html += "<button type='button' id='bF' class='ubt'>Pilih file</button><button type='button' id='bD' class='ubt'>Pilih folder</button>";
  html += "<input type='file' id='fi' multiple style='display:none'><input type='file' id='fd' webkitdirectory style='display:none'>";
  html += "<div id='ul'></div><div id='sm'></div><script src='/up.js'></script></div>\\n";
'''
s = s[:a] + card.replace("\\n\";\n", "\";\n") + "\n" + s[b:]

shutil.copy(P, P + ".bak_webupux")
open(P, "w", encoding="utf-8").write(s)
print("OK: phone.ino (WEBFM_UPLOAD_UX_V1%s). Cadangan: %s.bak_webupux" % (", dengan folder" if HAS_DIR else "", P))

# ---- dokumentasi ----
def doc(path, old, new):
    try: t = open(path, encoding="utf-8").read()
    except FileNotFoundError: print("lewati (tidak ada):", path); return
    if new in t: print("lewati (sudah ada):", path); return
    if t.count(old) != 1: print("PERINGATAN: pola dokumen tidak cocok, dilewati:", path); return
    shutil.copy(path, path + ".bak_webupux")
    open(path, "w", encoding="utf-8").write(t.replace(old, new)); print("OK:", path)

doc("CHANGELOG.md", "## Unreleased\n",
    "## Unreleased\n- Improved: uploading in the Web File Manager is much easier. Drag and drop files or whole folders onto the upload box (subfolders are created automatically), or use the Choose files / Choose folder buttons. Uploads run as a queue with a progress bar, speed and a clear success or failure message per file (for example when the storage is full), and the file list refreshes by itself when everything is done\n")
