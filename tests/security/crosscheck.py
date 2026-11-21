#!/usr/bin/env python3
"""Silang-cek vektor dari C++ dengan pustaka standar: PBKDF2, HMAC, AES-256-CTR (+ format enc1:)."""
import sys, hmac, hashlib
vec = dict(l.split(" ", 1) for l in open(sys.argv[1]).read().splitlines() if " " in l)
ok = True
def check(name, cond):
    global ok
    print(("ok   " if cond else "GAGAL"), name)
    ok &= bool(cond)

salt = bytes((i * 7 + 1) & 255 for i in range(16))
check("PBKDF2-HMAC-SHA256 (2000 iter)", hashlib.pbkdf2_hmac("sha256", b"rahasia123", salt, 2000, 32).hex() == vec["PBKDF2"])

ikm = bytes((i * 5 + 3) & 255 for i in range(38))
ek = hmac.new(ikm, b"nyx-keyenc-v1", hashlib.sha256).digest()
mk = hmac.new(ikm, b"nyx-keymac-v1", hashlib.sha256).digest()
check("turunan kunci enkripsi", ek.hex() == vec["EK"])
check("turunan kunci MAC", mk.hex() == vec["MK"])

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    txt = vec["TXT"]
    assert txt.startswith("enc1:")
    raw = bytes.fromhex(txt[5:])
    iv, ct, mac = raw[:16], raw[16:-16], raw[-16:]
    check("MAC enc1 (HMAC-SHA256 dipotong 16)", hmac.new(mk, iv + ct, hashlib.sha256).digest()[:16] == mac)
    d = Cipher(algorithms.AES(ek), modes.CTR(iv)).decryptor()
    check("AES-256-CTR enc1 -> plaintext", d.update(ct) + d.finalize() == b"AIzaSyD-EXAMPLE_key_1234567890abcdefghij")
except ImportError:
    print("(lewati AES: paket cryptography tidak ada)")
sys.exit(0 if ok else 1)
