#pragma once
// i18n.h -- sistem 2 bahasa UI: English (DEFAULT) + Indonesia.   (I18N_V1)
//
// Cara pakai di kode:   s.print(RPTR("Pengaturan"));   showToast(RPTR("Tersimpan"));
//   - RPTR("...") mengembalikan teks sesuai bahasa aktif (uiLang). Teks yg TIDAK ada di
//     kamus dikembalikan apa adanya, jadi aman dipasang di mana saja.
//   - Kamus ada di i18n_dict.h (dihasilkan dari tools/i18n_pairs.txt oleh tools/gen_i18n.py).
//     Teks sumber boleh bahasa Indonesia ATAU Inggris -- keduanya dicocokkan.
//   - RPTR() HANYA utk literal (alamat tetap, hasilnya di-cache per alamat). Utk buffer yg isinya
//     berubah-ubah pakai RPTRD(). Jangan pasang RPTR() di inisialisasi global/static (butuh konstanta).
//   - Pilihan bahasa disimpan di NVS namespace "ui", kunci "lang" (0 = English, 1 = Indonesia).
#include <Arduino.h>
#include <Preferences.h>
#include "i18n_dict.h"

enum { LANG_EN = 0, LANG_ID = 1 };
static int uiLang = LANG_EN;                      // DEFAULT: English

#define I18N_CACHE_N 512                          // harus pangkat 2
static const char* i18nKey[I18N_CACHE_N];         // alamat literal
static int16_t     i18nIdx[I18N_CACHE_N];         // indeks pasangan kamus, -1 = tak ada terjemahan

static int i18nFind(const char* s){
  for(int i=0;i<I18N_DICT_N;i++){
    if(strcmp(s,I18N_DICT[i].id)==0 || strcmp(s,I18N_DICT[i].en)==0) return i;
  }
  return -1;
}

static inline const char* i18nPick(int idx,const char* s){
  if(idx<0) return s;
  return (uiLang==LANG_ID) ? I18N_DICT[idx].id : I18N_DICT[idx].en;
}

// Terjemahan utk LITERAL (di-cache per alamat -> murah walau dipanggil tiap frame).
static const char* rpTr_(const char* s){
  if(!s || !*s) return s;
  uint32_t h=(uint32_t)(((uintptr_t)s)>>2)*2654435761u;
  uint32_t k=(h>>7)&(I18N_CACHE_N-1);
  for(int p=0;p<8;p++){
    uint32_t j=(k+p)&(I18N_CACHE_N-1);
    if(i18nKey[j]==s) return i18nPick(i18nIdx[j],s);
    if(!i18nKey[j]){
      int idx=i18nFind(s);
      i18nKey[j]=s; i18nIdx[j]=(int16_t)idx;
      return i18nPick(idx,s);
    }
  }
  return i18nPick(i18nFind(s),s);                 // cache penuh di area ini: cari langsung
}
// Utk teks dinamis (buffer): tanpa cache.
static const char* rpTrd_(const char* s){ return (s && *s) ? i18nPick(i18nFind(s),s) : s; }

#define RPTR(s)  rpTr_(s)
#define RPTRD(s) rpTrd_(s)

// ---- simpan / muat pilihan bahasa ----
static void saveLangPref(){ Preferences p; p.begin("ui",false); p.putInt("lang",uiLang); p.end(); }
static void loadLangPref(){
  Preferences p; p.begin("ui",true);
  uiLang=p.getInt("lang",LANG_EN); p.end();       // belum pernah diset -> English
  if(uiLang!=LANG_EN && uiLang!=LANG_ID) uiLang=LANG_EN;
}
static inline const char* langName(int l){ return l==LANG_ID ? "Indonesia" : "English"; }
