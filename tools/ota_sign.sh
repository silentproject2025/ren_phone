#!/usr/bin/env bash
# Menambahkan baris "sig=<hex DER ECDSA>" ke info.txt. Yang ditandatangani = seluruh isi info.txt sebelum baris sig=.
# Kunci privat diambil dari variabel lingkungan OTA_SIGNING_KEY (isi PEM) atau file OTA_SIGNING_KEY_FILE.
# Pemakaian: tools/ota_sign.sh info.txt
set -euo pipefail
F="${1:?pakai: ota_sign.sh info.txt}"
KEY="$(mktemp)"; trap 'rm -f "$KEY"' EXIT
if [ -n "${OTA_SIGNING_KEY_FILE:-}" ]; then cp "$OTA_SIGNING_KEY_FILE" "$KEY"
elif [ -n "${OTA_SIGNING_KEY:-}" ]; then printf '%s\n' "$OTA_SIGNING_KEY" > "$KEY"
else echo "OTA_SIGNING_KEY / OTA_SIGNING_KEY_FILE kosong" >&2; exit 1; fi
grep -q '^sig=' "$F" && { echo "info.txt sudah punya baris sig=" >&2; exit 1; }
[ -n "$(tail -c1 "$F")" ] && echo >> "$F"                       # pastikan diakhiri newline
SIG="$(openssl dgst -sha256 -sign "$KEY" "$F" | od -An -v -tx1 | tr -d ' \n')"
# verifikasi balik sebelum ditulis
openssl ec -in "$KEY" -pubout -out "$KEY.pub" 2>/dev/null
printf '%s' "$SIG" | xxd -r -p > "$KEY.sig"
openssl dgst -sha256 -verify "$KEY.pub" -signature "$KEY.sig" "$F" >/dev/null || { echo "verifikasi balik GAGAL" >&2; rm -f "$KEY.pub" "$KEY.sig"; exit 1; }
rm -f "$KEY.pub" "$KEY.sig"
echo "sig=${SIG}" >> "$F"
echo "info.txt ditandatangani (${#SIG} hex)"
