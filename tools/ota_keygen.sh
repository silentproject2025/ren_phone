#!/usr/bin/env bash
# Membuat pasangan kunci ECDSA P-256 untuk menandatangani info.txt OTA.
#   ota_private.pem  -> RAHASIA. Simpan sebagai GitHub secret OTA_SIGNING_KEY, JANGAN di-commit (sudah di .gitignore).
#   firmware/phone/ota_pubkey.h -> ditulis ulang berisi kunci publik (aman di-commit).
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -e ota_private.pem ] && [ "${1:-}" != "--force" ]; then
  echo "ota_private.pem sudah ada. Pakai --force untuk menimpa (firmware lama TIDAK akan menerima update baru!)." >&2; exit 1
fi
openssl ecparam -name prime256v1 -genkey -noout -out ota_private.pem
chmod 600 ota_private.pem
PUB="$(openssl ec -in ota_private.pem -pubout 2>/dev/null)"
{
  echo '// ota_pubkey.h -- dibuat oleh tools/ota_keygen.sh (kunci PUBLIK; aman di-commit). Lihat penjelasan di git history / docs/security.md.'
  echo '#pragma once'
  echo '#define OTA_PUBKEY_PEM \'
  printf '%s\n' "$PUB" | awk '{ printf "  \"%s\\n\"%s\n", $0, (NR==total? "" : " \\") }' total="$(printf '%s\n' "$PUB" | wc -l)"
} > firmware/phone/ota_pubkey.h
echo "OK. Kunci publik ditulis ke firmware/phone/ota_pubkey.h"
echo "Langkah berikut: simpan isi ota_private.pem sebagai GitHub secret OTA_SIGNING_KEY (lalu simpan cadangannya di tempat aman)."
