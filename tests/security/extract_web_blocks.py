#!/usr/bin/env python3
"""Ambil blok WEBAUTH_V2 dan halaman /passwd LANGSUNG dari phone.ino -> extracted_blocks.inc (untuk web_auth_harness.cpp)."""
import sys
src = open(sys.argv[1], encoding='utf-8').read()
def cut(a, b):
    i = src.index(a); j = src.index(b, i); return src[i:j]
C = cut('// KEYENC_V1 -- API key disimpan TERENKRIPSI', '// v27: fallback NVS')
A = cut('// WEBAUTH_V2 --', 'static bool webCsrfOk() {').replace('#include "mbedtls/base64.h"', '')
B = cut('// WEBPW_WEB_V1 -- ganti sandi / nama pengguna dari halaman web', 'void handleWebRoot() {')
open(sys.argv[2], 'w', encoding='utf-8').write(C + "\n" + A + "\n" + B)
