#!/usr/bin/env bash
# Pasang / PINDAHKAN PREFS_SD_V1 (Preferences -> fallback SD saat NVS penuh) di phone.ino. Tanpa Python, tanpa nomor baris.
#  - belum terpasang                     -> dipasang sesudah struct Theme
#  - terpasang di blok yg ditolak CI hoist -> dipindah ke sesudah struct Theme
#  - sudah di tempat yg benar              -> tidak mengubah apa-apa
# Pakai (dari folder repo):  bash apply_prefs_sd_v2.sh
set -e
F=phone.ino
[ -f "$F" ] || { echo "GAGAL: $F tidak ada di folder ini (cd ke folder repo dulu)"; exit 1; }

theme_end() {
  local T
  T=$(grep -n '^struct Theme {' "$F" | head -n1 | cut -d: -f1)
  [ -n "$T" ] || return 1
  awk -v t="$T" 'NR>=t { x=$0; gsub(/^[ \t]+|[ \t\r]+$/,"",x); if (x=="};") { print NR; exit } }' "$F"
}
E=$(theme_end) || true
[ -n "$E" ] || { echo "GAGAL: struct Theme / akhirnya tidak ketemu"; exit 1; }
grep -q '^void initSD(){[[:space:]]*\r\?$' "$F" || { echo "GAGAL: 'void initSD(){' tidak ketemu (isi sekitar initSD berubah?)"; exit 1; }

B=$(grep -n 'PREFS_SD_V1 BEGIN' "$F" | head -n1 | cut -d: -f1 || true)
if [ -n "$B" ]; then
  if [ "$B" -gt "$E" ]; then echo "sudah terpasang di tempat yang benar (baris $B, sesudah struct Theme baris $E). Tidak ada yang diubah."; exit 0; fi
  EN=$(grep -n 'PREFS_SD_V1 END' "$F" | head -n1 | cut -d: -f1 || true)
  { [ -n "$EN" ] && [ "$EN" -ge "$B" ]; } || { echo "GAGAL: penanda PREFS_SD_V1 END tidak ketemu / urutannya aneh"; exit 1; }
  cp "$F" "$F.sebelum-pindah"
  sed -i "${B},${EN}d" "$F"
  echo "Blok lama (baris $B-$EN, di dalam blok hoist) dihapus; cadangan: $F.sebelum-pindah"
  E=$(theme_end) || true
  [ -n "$E" ] || { echo "GAGAL: struct Theme tidak ketemu sesudah menghapus blok lama"; exit 1; }
else
  cp "$F" "$F.sebelum-prefs-sd"
fi

TMP=$(mktemp)
cat > "$TMP" <<'SHIM_EOF'
// PREFS_SD_V1: sengaja DI SINI (sesudah struct Theme), bukan di blok yg dipindah ci_hoist_types.py -- blok itu
// tidak boleh berisi #define. Preferences pertama kali dipakai jauh sesudah baris ini.
// ===== PREFS_SD_V1 BEGIN =====
// NVS cuma ~20 KB dan gampang penuh (error 0x1105 ESP_ERR_NVS_NOT_ENOUGH_SPACE). Kelas SdPrefs menggantikan
// Preferences (lihat #define di bawah) dgn API yg sama: NVS tetap jadi tempat utama; kalau NVS menolak tulis
// (penuh), nilainya otomatis disimpan di SD sbg berkas /prefs/<namespace>/<key>. Saat baca, berkas SD (kalau ada)
// didahulukan; kalau nanti NVS lega & tulis berhasil, berkas SD kunci itu dihapus. Tanpa SD = sama persis spt dulu.
extern bool sdReady;
void initSD();
class SdPrefs {
  Preferences _p; char _ns[20]; bool _nvs=false, _ro=false, _open=false;
  static bool& tried(){ static bool t=false; return t; }
  static bool& maybe(){ static bool m=true; return m; }   // true = belum tahu ada /prefs di SD
  static bool sdOk(){
    if(!sdReady && !tried()){ tried()=true; initSD(); }   // baca paling awal di setup() (spimhz) terjadi sebelum initSD()
    if(!sdReady) return false;
    if(maybe()){ maybe()=SD_MMC.exists("/prefs"); }
    return true;
  }
  static bool sdOkW(){ if(!sdOk()) return false; maybe()=true; return true; }
  void path(char* o,size_t n,const char* k) const { snprintf(o,n,"/prefs/%s/%s",_ns,k); }
  bool ovHas(const char* k){
    if(!sdOk()||!maybe()) return false;
    char f[64]; path(f,sizeof f,k); return SD_MMC.exists(f);
  }
  int ovLen(const char* k){
    if(!ovHas(k)) return -1;
    char f[64]; path(f,sizeof f,k); File g=SD_MMC.open(f,FILE_READ); if(!g) return -1;
    int n=(int)g.size(); g.close(); return n;
  }
  int ovRead(const char* k,void* b,size_t mx){   // -1 = tidak ada, selain itu jumlah byte terbaca
    if(!ovHas(k)) return -1;
    char f[64]; path(f,sizeof f,k); File g=SD_MMC.open(f,FILE_READ); if(!g) return -1;
    size_t n=(size_t)g.size(); if(n>mx) n=mx;
    int r=(int)g.read((uint8_t*)b,n); g.close(); return r<0?-1:r;
  }
  size_t ovWrite(const char* k,const void* b,size_t n){
    if(_ro||!sdOkW()) return 0;
    char d[48],f[64],t[72]; snprintf(d,sizeof d,"/prefs/%s",_ns);
    SD_MMC.mkdir("/prefs"); SD_MMC.mkdir(d); path(f,sizeof f,k); snprintf(t,sizeof t,"%s.tmp",f);
    File g=SD_MMC.open(t,FILE_WRITE); if(!g) return 0;
    size_t w=n?g.write((const uint8_t*)b,n):0; g.close();
    if(w!=n){ SD_MMC.remove(t); return 0; }
    if(SD_MMC.exists(f)) SD_MMC.remove(f);
    if(!SD_MMC.rename(t,f)){ SD_MMC.remove(t); return 0; }
    Serial.printf("[prefs] NVS penuh -> %s/%s disimpan di SD (%u B)\n",_ns,k,(unsigned)n);
    return n?n:1;
  }
  bool ovDrop(const char* k){ if(!ovHas(k)) return false; char f[64]; path(f,sizeof f,k); return SD_MMC.remove(f); }
  template<class T> size_t putN(const char* k,T v,size_t r){ if(_ro) return 0; if(r){ ovDrop(k); return r; } return ovWrite(k,&v,sizeof v); }
public:
  bool begin(const char* ns,bool ro=false,const char* part=NULL){
    end(); strncpy(_ns,ns,sizeof(_ns)-1); _ns[sizeof(_ns)-1]=0; _ro=ro;
    _nvs=_p.begin(ns,ro,part);
    if(_nvs) _open=true;
    else if(ro){ char d[48]; snprintf(d,sizeof d,"/prefs/%s",_ns); _open=sdOk()&&maybe()&&SD_MMC.exists(d); }
    else _open=sdOk();
    return _open;
  }
  void end(){ if(_nvs) _p.end(); _nvs=false; _open=false; }
  size_t freeEntries(){ return _nvs?_p.freeEntries():0; }
  bool isKey(const char* k){ return ovHas(k)||(_nvs&&_p.isKey(k)); }
  bool remove(const char* k){ bool a=(_nvs&&!_ro)?_p.remove(k):false; bool b=_ro?false:ovDrop(k); return a||b; }
  bool clear(){
    bool a=(_nvs&&!_ro)?_p.clear():false;
    if(!_ro&&sdOk()&&maybe()){
      char d[48]; snprintf(d,sizeof d,"/prefs/%s",_ns); File r=SD_MMC.open(d);
      if(r){ String names[24]; int n=0; File e=r.openNextFile(); while(e&&n<24){ names[n++]=String(e.name()); e=r.openNextFile(); } r.close();
        for(int i=0;i<n;i++){ String f=String(d)+"/"+names[i]; SD_MMC.remove(f.c_str()); } a=true; }
    }
    return a;
  }
#define SDP_NUM(T,PUT,GET,DEF) \
  size_t PUT(const char* k,T v){ return putN<T>(k,v,(_nvs&&!_ro)?_p.PUT(k,v):0); } \
  T GET(const char* k,T d=DEF){ T v; if(ovRead(k,&v,sizeof v)==(int)sizeof v) return v; return _nvs?_p.GET(k,d):d; }
  SDP_NUM(int32_t,putInt,getInt,0)
  SDP_NUM(uint32_t,putUInt,getUInt,0)
  SDP_NUM(uint8_t,putUChar,getUChar,0)
  SDP_NUM(uint16_t,putUShort,getUShort,0)
  SDP_NUM(uint32_t,putULong,getULong,0)
  SDP_NUM(uint64_t,putULong64,getULong64,0)
  SDP_NUM(float,putFloat,getFloat,0)
  SDP_NUM(bool,putBool,getBool,false)
#undef SDP_NUM
  size_t putString(const char* k,const char* v){
    if(_ro) return 0; size_t L=strlen(v); size_t r=(_nvs)?_p.putString(k,v):0;
    bool ok=r>0; if(!ok&&L==0&&_nvs) ok=_p.isKey(k)&&_p.getString(k,String("x")).length()==0;
    if(ok){ ovDrop(k); return r; }
    return ovWrite(k,v,L);
  }
  size_t putString(const char* k,const String& v){ return putString(k,v.c_str()); }
  String getString(const char* k,const String& d=String()){
    int n=ovLen(k); if(n>=0){ String s; s.reserve(n); char* b=(char*)malloc(n+1);
      if(b){ int r=ovRead(k,b,n); if(r>=0){ b[r]=0; s=String(b); free(b); return s; } free(b); } }
    return _nvs?_p.getString(k,d):d;
  }
  size_t getString(const char* k,char* out,size_t mx){
    if(!mx) return 0; int r=ovRead(k,out,mx-1); if(r>=0){ out[r]=0; return (size_t)r; }
    return _nvs?_p.getString(k,out,mx):0;
  }
  size_t putBytes(const char* k,const void* v,size_t n){
    if(_ro) return 0; size_t r=(_nvs)?_p.putBytes(k,v,n):0;
    if(r||(!n&&_nvs)){ ovDrop(k); return r; }
    return ovWrite(k,v,n);
  }
  size_t getBytesLength(const char* k){ int n=ovLen(k); if(n>=0) return (size_t)n; return _nvs?_p.getBytesLength(k):0; }
  size_t getBytes(const char* k,void* b,size_t mx){
    int n=ovLen(k); if(n>=0){ if((size_t)n>mx) return 0; int r=ovRead(k,b,mx); return r<0?0:(size_t)r; }
    return _nvs?_p.getBytes(k,b,mx):0;
  }
};
#define Preferences SdPrefs
// ===== PREFS_SD_V1 END =====
SHIM_EOF
GUARD='  if(sdReady) return; // PREFS_SD_V1: bisa dipanggil lebih awal oleh SdPrefs; jangan mount dua kali'
if grep -q $'\r' "$F"; then sed -i 's/$/\r/' "$TMP"; GUARD="$GUARD"$'\r'; fi

if ! grep -q 'PREFS_SD_V1: bisa dipanggil' "$F"; then
  sed -i "/^void initSD(){[[:space:]]*\r\?$/a\\
$GUARD" "$F"
fi
E=$(theme_end)
sed -i "${E}r $TMP" "$F"
rm -f "$TMP"
echo "OK: PREFS_SD_V1 ada sesudah struct Theme (baris $E)."
echo "Cek: $(grep -c PREFS_SD_V1 "$F") penanda (harus 4)"
