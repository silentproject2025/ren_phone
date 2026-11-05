// mock_arduino.h -- API Arduino/LovyanGFX palsu minimum agar kamus_renphone.ino bisa dikompilasi
// DAN dijalankan di komputer (String, Preferences, SD berbasis berkas /tmp/kms_sd). Bukan untuk firmware.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <vector>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
#define FILE_READ "r"

struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  unsigned int length() const { return (unsigned int)s.size(); }
  const char* c_str() const { return s.c_str(); }
  String& operator+=(const char* c) { s += c; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  char operator[](int i) const { return s[(size_t)i]; }
  void remove(unsigned int idx) { if (idx < s.size()) s.erase(idx); }
  int indexOf(char c, unsigned int from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  bool operator==(const char* o) const { return s == o; }
};

struct Preferences {
  static std::map<std::string, std::string>& store() { static std::map<std::string, std::string> m; return m; }
  std::string ns;
  bool begin(const char* n, bool = false) { ns = n; return true; }
  void end() {}
  int getInt(const char* k, int d) { auto it = store().find(ns + "/" + k); return it == store().end() ? d : atoi(it->second.c_str()); }
  void putInt(const char* k, int v) { store()[ns + "/" + k] = std::to_string(v); }
  String getString(const char* k, const char* d) { auto it = store().find(ns + "/" + k); return String(it == store().end() ? d : it->second.c_str()); }
  void putString(const char* k, const String& v) { store()[ns + "/" + k] = v.s; }
};

struct File {
  FILE* f = nullptr;
  operator bool() const { return f != nullptr; }
  size_t size() { long p = ftell(f); fseek(f, 0, SEEK_END); long e = ftell(f); fseek(f, p, SEEK_SET); return (size_t)e; }
  size_t read(uint8_t* b, size_t n) { return fread(b, 1, n, f); }
  void close() { if (f) fclose(f); f = nullptr; }
};
struct FSMock { File open(const char* path, const char*) { File x; x.f = fopen((std::string("/tmp/kms_sd") + path).c_str(), "rb"); return x; } };

namespace lgfx { struct IFont { int d; }; }
struct LGFX_Sprite {
  int draws = 0; std::vector<std::string> texts;
  void fillRect(int,int,int,int,uint16_t) {}   void drawFastHLine(int,int,int,uint16_t) {}   void drawFastVLine(int,int,int,uint16_t) {}
  void drawLine(int,int,int,int,uint16_t) {}   void setTextSize(float) {}   void setTextColor(uint16_t) {}   void setCursor(int,int) {}
  void print(const char* t) { draws++; texts.push_back(t); }
};
struct Theme { uint16_t bg, surface, surface2, accent, text, subtext, divider; };
static inline Theme& T() { static Theme t = {1,2,3,4,5,6,7}; return t; }
static inline uint16_t blend565(uint16_t a, uint16_t, uint8_t) { return a; }
static inline uint16_t appIconInk(uint16_t) { return 0; }
static inline void nyxAaRR(LGFX_Sprite&, int,int,int,int,int,uint16_t) {}
static std::string gToast;
static inline void showToast(const char* m, int) { gToast = m; }
