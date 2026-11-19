#!/usr/bin/env python3
"""Cocokkan backup_cloud.h (base64 + turunan kunci) dengan Python. Jalankan dari folder repo:
   python3 tests/backup/cloud_check.py"""
import base64, hashlib, hmac, os, subprocess, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
os.makedirs('/tmp/bkc', exist_ok=True)
r = subprocess.run(['g++', '-std=c++11', '-O2', '-D_GNU_SOURCE', '-I' + os.path.join(root, 'firmware', 'phone'),
                    '-o', '/tmp/bkc/cloud', os.path.join(root, 'tests', 'backup', 'test_cloud.cpp')], capture_output=True, text=True)
if r.returncode: print(r.stderr); sys.exit(1)
run = lambda *a: subprocess.run(['/tmp/bkc/cloud', *a], capture_output=True, text=True).stdout.split()
fails = 0
def check(n, got, want):
    global fails
    if got != want: fails += 1; print('GAGAL', n, got, want)
for n in (1, 2, 3, 4, 5, 100, 1000, 4097):
    d = os.urandom(n); check('b64-%d' % n, run('b64', d.hex())[0], base64.b64encode(d).decode())
check('magic', base64.b64encode(b'NYXE1\0').decode()[:8], 'TllYRTEA')       # yang diperiksa server
for _ in range(5):
    chip, dev = os.urandom(32), os.urandom(8).hex()
    s = hmac.new(chip, b'ren_phone.cloud.v1|' + dev.encode(), 'sha256').digest()
    token = hmac.new(s, b'auth', 'sha256').hexdigest(); enc = hmac.new(s, b'enc', 'sha256').hexdigest()
    check('derive', run('derive', chip.hex(), dev), [token, enc])
print('SEMUA COCOK dgn Python' if not fails else '%d GAGAL' % fails); sys.exit(1 if fails else 0)
