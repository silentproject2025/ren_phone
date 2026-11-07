#!/usr/bin/env python3
"""Cocokkan backup_crypto.h dengan implementasi referensi Python (hashlib/hmac/cryptography).
Jalankan dari folder repo:  python3 tests_backup/crypto_check.py"""
import hashlib, hmac, os, subprocess, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

src = r'''
#include <stdio.h>
#include <stdlib.h>
#include "backup_crypto.h"
static int hx(const char* s, uint8_t* o) { int n = 0; for (; s[0] && s[1]; s += 2) { unsigned v; sscanf(s, "%2x", &v); o[n++] = (uint8_t)v; } return n; }
static void ph(const uint8_t* d, size_t n) { for (size_t i = 0; i < n; i++) printf("%02x", d[i]); printf("\n"); }
int main(int argc, char** argv) {
  static uint8_t a[70000], b[70000], o[70000]; int na = argc > 2 ? hx(argv[2], a) : 0, nb = argc > 3 ? hx(argv[3], b) : 0;
  const char* c = argv[1];
  if (!strcmp(c, "sha")) { bkSha256(a, na, o); ph(o, 32); }
  else if (!strcmp(c, "hmac")) { bkHmac(a, na, b, nb, o); ph(o, 32); }
  else if (!strcmp(c, "pbkdf2")) { bkPbkdf2(a, na, b, nb, (uint32_t)atoi(argv[4]), o, 64); ph(o, 64); }
  else if (!strcmp(c, "ctr")) { uint8_t iv[16]; hx(argv[4], iv); bkAesCtr(a, iv, b, o, nb); ph(o, nb); }
  return 0;
}
'''
os.makedirs('/tmp/bkc', exist_ok=True)
open('/tmp/bkc/t.cpp', 'w').write(src)
r = subprocess.run(['g++', '-std=c++11', '-O2', '-I.', '-o', '/tmp/bkc/t', '/tmp/bkc/t.cpp'], capture_output=True, text=True)
if r.returncode: print(r.stderr); sys.exit(1)
def run(*a): return subprocess.run(['/tmp/bkc/t', *a], capture_output=True, text=True).stdout.strip()
fails = 0
def check(name, got, want):
    global fails
    if got != want: fails += 1; print('GAGAL', name, got[:40], want[:40])
for n in (0, 1, 55, 56, 63, 64, 65, 119, 1000):
    d = os.urandom(n); check('sha%d' % n, run('sha', d.hex() or ''), hashlib.sha256(d).hexdigest())
for kl in (1, 20, 32, 64, 65, 100):
    k = os.urandom(kl); m = os.urandom(77)
    check('hmac%d' % kl, run('hmac', k.hex(), m.hex()), hmac.new(k, m, 'sha256').hexdigest())
for pw, salt, it in ((b'password', b'salt', 1), (b'password', b'salt', 2), (b'sandi rahasia', os.urandom(16), 3000)):
    check('pbkdf2', run('pbkdf2', pw.hex(), salt.hex(), str(it)), hashlib.pbkdf2_hmac('sha256', pw, salt, it, 64).hex())
for n in (1, 15, 16, 17, 31, 32, 1000, 4097):
    key, iv, data = os.urandom(32), os.urandom(16), os.urandom(n)
    enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor(); want = enc.update(data) + enc.finalize()
    check('ctr%d' % n, run('ctr', key.hex(), data.hex(), iv.hex()), want.hex())
# CTR dgn counter yg melimpah antar-byte (iv berakhiran ff..)
key, data = os.urandom(32), os.urandom(100); iv = bytes(15 * [0xAB]) + b'\xff'
enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor(); check('ctr-carry', run('ctr', key.hex(), data.hex(), iv.hex()), (enc.update(data) + enc.finalize()).hex())
iv = bytes(8 * [0x11]) + bytes(8 * [0xff]); enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor(); check('ctr-carry2', run('ctr', key.hex(), data.hex(), iv.hex()), (enc.update(data) + enc.finalize()).hex())
print('SEMUA COCOK dgn Python' if not fails else '%d GAGAL' % fails); sys.exit(1 if fails else 0)
