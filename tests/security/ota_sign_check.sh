#!/usr/bin/env bash
# keygen -> tandatangani info.txt -> verifikasi persis cara firmware (PEM dari header C, DER, badan sebelum "sig=").
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/tools" "$T/firmware/phone"
cp "$ROOT/tools/ota_keygen.sh" "$ROOT/tools/ota_sign.sh" "$T/tools/"
cp "$ROOT/firmware/phone/ota_pubkey.h" "$T/firmware/phone/"
( cd "$T" && ./tools/ota_keygen.sh >/dev/null
  printf 'branch=main\nbuild=7\nversion=1.0.3\nbase=1\nsize=1500000\nmd5=%032d\nsha256=%064d\nsha=abc\ndisplay=ili9341\n' 0 0 > info.txt
  OTA_SIGNING_KEY="$(cat ota_private.pem)" ./tools/ota_sign.sh info.txt >/dev/null
  python3 - <<'PY'
import re
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
hdr = open('firmware/phone/ota_pubkey.h').read()
pem = ''.join(re.findall(r'"([^"]*)"', hdr.split('OTA_PUBKEY_PEM', 1)[1])).replace('\\n', '\n').encode()
pub = serialization.load_pem_public_key(pem)
body = open('info.txt', 'rb').read()
i = body.index(b'\nsig=') + 1
pub.verify(bytes.fromhex(body[i+4:].strip().decode()), body[:i], ec.ECDSA(hashes.SHA256()))
try:
    pub.verify(bytes.fromhex(body[i+4:].strip().decode()), body[:i].replace(b'build=7', b'build=8'), ec.ECDSA(hashes.SHA256()))
    raise SystemExit("GAGAL: badan diubah tapi lolos")
except SystemExit: raise
except Exception: pass
print("OTA sign check: OK")
PY
)
