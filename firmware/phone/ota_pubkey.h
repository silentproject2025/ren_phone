// =====================================================================
// ota_pubkey.h -- kunci PUBLIK untuk memverifikasi tanda tangan info.txt OTA (ECDSA P-256 / SHA-256).
//
// KOSONG ("") = verifikasi tanda tangan MATI -> OTA hanya memeriksa ukuran + MD5 (+ SHA-256 kalau ada di info.txt),
//               perilaku lama. Cocok selama kunci belum dibuat.
// TERISI      = HP menolak SEMUA update yang info.txt-nya tidak bertanda tangan / tanda tangannya salah, menolak
//               jalur rilis lama (tanpa info.txt) dan menolak pasang dari URL yang diketik manual.
//
// Cara mengaktifkan (sekali saja):
//   1. ./tools/ota_keygen.sh            -> membuat ota_private.pem (RAHASIA, jangan di-commit) dan menulis ulang file ini
//   2. GitHub repo -> Settings -> Secrets and variables -> Actions -> New secret
//        nama: OTA_SIGNING_KEY   isi: seluruh isi ota_private.pem
//   3. Commit file ini, build & pasang firmware itu SECARA MANUAL (kabel/SD) sekali; build berikutnya bisa lewat OTA.
// Kunci publik aman dipublikasikan. Kalau kunci privat bocor: buat pasangan baru & pasang ulang secara manual.
// =====================================================================
#pragma once
#define OTA_PUBKEY_PEM ""
