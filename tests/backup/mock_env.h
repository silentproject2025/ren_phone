// mock_env.h -- NVS, SD, Preferences, LovyanGFX palsu minimum agar backup_renphone.ino bisa dikompilasi
// DAN dijalankan di komputer (NVS = std::map, SD = folder /tmp/nyx_sd). Bukan untuk firmware.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <map>
#include <vector>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
static unsigned long gMillis = 1000;
static inline void yield() {}
static inline uint32_t esp_random() { return (uint32_t)rand(); }
struct String {                                  // String Arduino minimum
  std::string s; String(const char* c = "") : s(c) {}
  String& operator=(const char* c) { s = c; return *this; }
  unsigned length() const { return (unsigned)s.size(); } const char* c_str() const { return s.c_str(); }
  bool operator==(const String& o) const { return s == o.s; }
};
enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };
static bool kbVisible = false; static String* kbTarget = nullptr; static KbMode kbMode = KB_LOWER;
extern int SCR_H;
static inline int kbY() { return SCR_H - 100; }        // sama dgn phone.ino: SCR_H - 4*(22+2) - 4 (tanpa baris saran)

static inline unsigned long millis() { return gMillis; }
#define FILE_READ "r"
#define FILE_WRITE "w"

// ---------------- NVS palsu (urutan penyisiran = urutan peta)
typedef uint32_t nvs_handle_t; typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define NVS_DEFAULT_PART_NAME "nvs"
#define ESP_IDF_VERSION_MAJOR 5
enum nvs_type_t { NVS_TYPE_U8 = 0x01, NVS_TYPE_I8 = 0x11, NVS_TYPE_U16 = 0x02, NVS_TYPE_I16 = 0x12, NVS_TYPE_U32 = 0x04, NVS_TYPE_I32 = 0x14,
                  NVS_TYPE_U64 = 0x08, NVS_TYPE_I64 = 0x18, NVS_TYPE_STR = 0x21, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff };
struct MVal { int type; std::vector<uint8_t> d; };
static std::map<std::string, MVal>& nvsStore() { static std::map<std::string, MVal> m; return m; }   // "ns\x01key"
static std::vector<std::string> gNsOf;   // handle -> ns
struct nvs_entry_info_t { char namespace_name[16]; char key[16]; nvs_type_t type; };
struct MIter { std::vector<std::string> keys; size_t i; };
typedef MIter* nvs_iterator_t;
static inline std::string nk(const char* ns, const char* k) { return std::string(ns) + "\x01" + k; }
static inline esp_err_t nvs_entry_find(const char*, const char*, nvs_type_t, nvs_iterator_t* out) {
  if (nvsStore().empty()) { *out = nullptr; return -1; }
  MIter* it = new MIter(); it->i = 0; for (auto& kv : nvsStore()) it->keys.push_back(kv.first); *out = it; return ESP_OK; }
static inline esp_err_t nvs_entry_next(nvs_iterator_t* it) { if (!*it) return -1; (*it)->i++; if ((*it)->i >= (*it)->keys.size()) { delete *it; *it = nullptr; return -1; } return ESP_OK; }
static inline esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* o) {
  const std::string& k = it->keys[it->i]; size_t p = k.find('\x01');
  strncpy(o->namespace_name, k.substr(0, p).c_str(), 15); o->namespace_name[15] = 0;
  strncpy(o->key, k.substr(p + 1).c_str(), 15); o->key[15] = 0; o->type = (nvs_type_t)nvsStore()[k].type; return ESP_OK; }
static inline void nvs_release_iterator(nvs_iterator_t it) { delete it; }
static inline esp_err_t nvs_open(const char* ns, int, nvs_handle_t* h) { gNsOf.push_back(ns); *h = (nvs_handle_t)gNsOf.size() - 1; return ESP_OK; }
static inline void nvs_close(nvs_handle_t) {}
static inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
static int gSetFailEvery = 0, gSetCount = 0;
template <class T> static esp_err_t mget(nvs_handle_t h, const char* k, T* v, int ty) {
  auto it = nvsStore().find(nk(gNsOf[h].c_str(), k)); if (it == nvsStore().end() || it->second.type != ty) return -1; memcpy(v, it->second.d.data(), sizeof(T)); return ESP_OK; }
template <class T> static esp_err_t mset(nvs_handle_t h, const char* k, T v, int ty) {
  if (gSetFailEvery && (++gSetCount % gSetFailEvery) == 0) return -1;
  MVal x; x.type = ty; x.d.assign((uint8_t*)&v, (uint8_t*)&v + sizeof(T)); nvsStore()[nk(gNsOf[h].c_str(), k)] = x; return ESP_OK; }
#define GETSET(N, T, TY) \
  static inline esp_err_t nvs_get_##N(nvs_handle_t h, const char* k, T* v) { return mget<T>(h, k, v, TY); } \
  static inline esp_err_t nvs_set_##N(nvs_handle_t h, const char* k, T v) { return mset<T>(h, k, v, TY); }
GETSET(u8, uint8_t, NVS_TYPE_U8) GETSET(i8, int8_t, NVS_TYPE_I8) GETSET(u16, uint16_t, NVS_TYPE_U16) GETSET(i16, int16_t, NVS_TYPE_I16)
GETSET(u32, uint32_t, NVS_TYPE_U32) GETSET(i32, int32_t, NVS_TYPE_I32) GETSET(u64, uint64_t, NVS_TYPE_U64) GETSET(i64, int64_t, NVS_TYPE_I64)
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char* k, char* out, size_t* len) {
  auto it = nvsStore().find(nk(gNsOf[h].c_str(), k)); if (it == nvsStore().end() || it->second.type != NVS_TYPE_STR) return -1;
  if (out) { memcpy(out, it->second.d.data(), it->second.d.size()); }
  *len = it->second.d.size(); return ESP_OK; }
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char* k, const char* v) {
  MVal x; x.type = NVS_TYPE_STR; x.d.assign(v, v + strlen(v) + 1); nvsStore()[nk(gNsOf[h].c_str(), k)] = x; return ESP_OK; }
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char* k, void* out, size_t* len) {
  auto it = nvsStore().find(nk(gNsOf[h].c_str(), k)); if (it == nvsStore().end() || it->second.type != NVS_TYPE_BLOB) return -1;
  if (out) { memcpy(out, it->second.d.data(), it->second.d.size()); }
  *len = it->second.d.size(); return ESP_OK; }
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char* k, const void* v, size_t n) {
  MVal x; x.type = NVS_TYPE_BLOB; x.d.assign((const uint8_t*)v, (const uint8_t*)v + n); nvsStore()[nk(gNsOf[h].c_str(), k)] = x; return ESP_OK; }

// ---------------- Preferences palsu (bool saja)
struct Preferences {
  static std::map<std::string, bool>& st() { static std::map<std::string, bool> m; return m; }
  std::string ns;
  bool begin(const char* n, bool = false) { ns = n; return true; }
  void end() {}
  bool getBool(const char* k, bool d) { auto it = st().find(ns + "/" + k); return it == st().end() ? d : it->second; }
  void putBool(const char* k, bool v) { st()[ns + "/" + k] = v; }
};

// ---------------- SD palsu berbasis folder /tmp/nyx_sd
#define SDROOT "/tmp/nyx_sd"
struct File {
  FILE* f = nullptr; DIR* d = nullptr; std::string path, nm; bool isdir = false;
  operator bool() const { return f != nullptr || d != nullptr; }
  size_t size() { long p = ftell(f); fseek(f, 0, SEEK_END); long e = ftell(f); fseek(f, p, SEEK_SET); return (size_t)e; }
  size_t read(uint8_t* b, size_t n) { return fread(b, 1, n, f); }
  size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, f); }
  void close() { if (f) fclose(f); if (d) closedir(d); f = nullptr; d = nullptr; }
  bool isDirectory() { return isdir; }
  const char* name() { return nm.c_str(); }
  File openNextFile() { File x; if (!d) return x; dirent* e; while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      std::string full = path + "/" + e->d_name; struct stat st; stat(full.c_str(), &st); x.nm = e->d_name; x.isdir = S_ISDIR(st.st_mode); x.f = x.isdir ? nullptr : fopen(full.c_str(), "rb"); if (x.isdir) x.d = opendir(full.c_str()); return x; } return x; }
};
struct FSMock {
  File open(const char* p, const char* mode = "r") { File x; std::string full = std::string(SDROOT) + p; struct stat st;
    if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { x.d = opendir(full.c_str()); x.isdir = true; x.path = full; return x; }
    x.f = fopen(full.c_str(), !strcmp(mode, "w") ? "wb" : "rb"); return x; }
  bool exists(const char* p) { struct stat st; return stat((std::string(SDROOT) + p).c_str(), &st) == 0; }
  bool mkdir(const char* p) { return ::mkdir((std::string(SDROOT) + p).c_str(), 0755) == 0; }
  bool remove(const char* p) { return ::remove((std::string(SDROOT) + p).c_str()) == 0; }
};

// ---------------- LovyanGFX / UI palsu
namespace lgfx { struct LGFXBase {}; }
struct LGFX_Sprite : lgfx::LGFXBase {
  std::vector<std::string> texts; int clip = 0;
  void fillRect(int,int,int,int,uint16_t) {} void drawRoundRect(int,int,int,int,int,uint16_t) {}
  void setTextSize(float) {} void setTextColor(uint16_t) {} void setCursor(int,int) {}
  void print(const char* t) { texts.push_back(t); }
  int textWidth(const char* t) { return (int)strlen(t) * 6; }
  void setClipRect(int,int,int,int) { clip++; } void clearClipRect() { clip--; }
};
struct Theme { uint16_t bg, surface, surface2, accent, accent2, text, subtext, divider, danger; };
static inline Theme& T() { static Theme t = {1,2,3,4,5,6,7,8,9}; return t; }
static inline uint16_t blend565(uint16_t a, uint16_t, uint8_t) { return a; }
static inline void nyxAaRR(lgfx::LGFXBase&, int,int,int,int,int,uint16_t) {}
static inline void nyxAaCircle(lgfx::LGFXBase&, int,int,int,uint16_t) {}
static std::string gToast;
static inline void showToast(const char* m, int = 1400) { gToast = m; }
static inline void drawGlassPanel(LGFX_Sprite&, int,int,int,int,int,uint16_t,uint8_t) {}
static inline void drawGlassCard(LGFX_Sprite&, int,int,int,int,int,uint16_t,uint8_t) {}
static inline void iosBackdrop(LGFX_Sprite&) {} static inline void drawStatusBar(LGFX_Sprite&) {}
static inline void iosTitle(LGFX_Sprite& s, int,int,const char* t) { s.print(t); }
static inline void drawBack(LGFX_Sprite&) {} static inline void drawToast(LGFX_Sprite&) {}
struct LGFX_Sprite;
static int gKbTouches = 0;
static bool gWentBack = false;
static inline bool isBack(int, int y) { return y > 9000; }
static inline void navBack() { gWentBack = true; }
struct EspMock { int restarts = 0; void restart() { restarts++; } };
static EspMock ESP;
static inline void drawKb(LGFX_Sprite&) {}
static inline void kbTouch(int, int) { gKbTouches++; }
