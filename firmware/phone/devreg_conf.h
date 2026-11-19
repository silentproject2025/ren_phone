// devreg_conf.h -- konfigurasi pendaftaran perangkat (Supabase). Skema + keamanannya: supabase/setup.sql.
// Isi RP_REG_URL dengan "Project URL" dari Supabase (Settings > API). Kunci di bawah = publishable key proyekmu.
// Kunci anon MEMANG dirancang publik; tabelnya terkunci RLS dan hanya fungsi register_device yang bisa dipanggil.
// Kosongkan RP_REG_URL untuk mematikan fitur ini sepenuhnya (modul jadi no-op, tidak ada trafik).
#pragma once
#ifndef RP_REG_URL
#define RP_REG_URL "https://ywhyfkxkihyxhrehoqya.supabase.co"
#endif
#ifndef RP_REG_KEY
#define RP_REG_KEY "sb_publishable_qB00w154m7mc4f6YmtPnRw_Y1ZZ-oWs"   // publishable key (publik by design)
#endif

// OPSIONAL (disarankan utk Backup ke database): sertifikat root CA (PEM) server Supabase. Bila diisi, koneksi backup
// memverifikasi server; kosong = tanpa verifikasi seperti modul jaringan lain (data backup tetap terenkripsi, tapi
// token bisa disadap penyerang aktif di jaringan yg sama). Contoh:  #define RP_REG_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
