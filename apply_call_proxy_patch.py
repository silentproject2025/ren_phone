#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_proxy_patch.py -- dukungan "outbound proxy" untuk app Telepon (mis. SIP2SIP.info).
#
# Masalah: firmware memakai SATU nama untuk dua hal: domain akun (alamat SIP) dan server yang dihubungi
# (dicari lewat DNS record A). SIP2SIP memisahkan keduanya: domain = sip2sip.info, tapi server yang harus
# dihubungi = proxy.sipthor.net. Tanpa pemisahan, REGISTER dikirim ke alamat yang salah.
#
# Cara pakai setelah patch: di Telepon > Atur, isi Server dengan  DOMAIN/PROXY , contoh:
#       sip2sip.info/proxy.sipthor.net
# (tanpa garis miring = perilaku lama, mis. sip.linphone.org). Port proxy opsional: DOMAIN/PROXY:5060
#
#     python3 apply_call_proxy_patch.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'tProxyName' in s:
    print("Patch proxy sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

s = rep(s, "static int  tPort = CALL_SIP_PORT;",
           "static int  tPort = CALL_SIP_PORT;\nstatic char tProxyName[64] = \"\";                 // opsional: server yang dihubungi (outbound proxy); kosong = pakai domain", "decl")

s = rep(s, '  { char* s = strstr(tHostName, "sip:"); if (s == tHostName) memmove(tHostName, tHostName + 4, strlen(tHostName + 4) + 1); }\n  srvOk = false;',
           '  { char* s = strstr(tHostName, "sip:"); if (s == tHostName) memmove(tHostName, tHostName + 4, strlen(tHostName + 4) + 1); }\n'
           '  tProxyName[0] = 0;                                // format "domain/proxy": pisahkan domain akun & server yang dihubungi\n'
           '  { char* sl = strchr(tHostName, \'/\'); if (sl) { *sl = 0; callCopy(tProxyName, sizeof(tProxyName), sl + 1); } }\n'
           '  srvOk = false;', "snapshot")

s = rep(s, "  if (!callResolve(tHostName, ip)) { srvOk = false; return false; }",
           "  if (!callResolve(tProxyName[0] ? tProxyName : tHostName, ip)) { srvOk = false; return false; }", "resolve")

open(F, 'w', encoding='utf-8').write(s)
print("OK: Server boleh diisi 'domain/proxy' (contoh: sip2sip.info/proxy.sipthor.net)")
