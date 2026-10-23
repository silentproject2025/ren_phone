#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_stale_resp_fix.py -- abaikan balasan SIP "basi" (retransmit 401) di app Telepon.
#
# Gejala: panggilan keluar -> toast "Autentikasi ditolak", padahal telepon sudah masuk ke lawan;
# lawan angkat tapi tanpa suara & Ren Phone tampak idle; kadang status jadi merah
# "VoIP belum terdaftar" sampai Atur > Simpan.
# Sebab: di jaringan lambat (hotspot 4G) INVITE/REGISTER dikirim ulang (retransmit) sebelum 401
# pertama tiba. Server membalas 401 untuk KEDUANYA. 401 pertama dibalas dengan Authorization (CSeq+1),
# lalu 401 kedua (untuk CSeq lama) dianggap "auth sudah dicoba tapi ditolak" -> panggilan dibunuh /
# registrasi ditandai gagal. Perbaikan: balasan yang CSeq-nya bukan CSeq permintaan terakhir diabaikan.
#
#     python3 apply_call_stale_resp_fix.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'respons INVITE lama' in s:
    print("Patch stale-resp sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# 1. INVITE
s = rep(s, '  if (strstr(cs, "CANCEL")) return;',
'''  if (strstr(cs, "CANCEL")) return;
  {                                                                    // balasan utk INVITE lama (retransmit 401 dst) -> abaikan
    unsigned long rcs = strtoul(cs, nullptr, 10);
    if (rcs && rcs != (unsigned long)dlg.inviteCseq) {
      Serial.printf("[CALL] abaikan respons INVITE lama: kode %d cseq %lu (sekarang %lu)\\n", code, rcs, (unsigned long)dlg.inviteCseq);
      return;
    }
  }''', "invite resp")

# 2. REGISTER
s = rep(s, "  if (strcmp(cid, regCallId) != 0) return;\n  if (code < 200) return;",
'''  if (strcmp(cid, regCallId) != 0) return;
  {                                                                    // balasan utk REGISTER lama -> abaikan
    char cs[40]; sipHdr(msg, "CSeq", "", 0, cs, sizeof(cs));
    unsigned long rcs = strtoul(cs, nullptr, 10);
    if (rcs && rcs != (unsigned long)regCseq) return;
  }
  if (code < 200) return;''', "reg resp")

open(F, 'w', encoding='utf-8').write(s)
print("OK: balasan SIP lama (CSeq basi) sekarang diabaikan untuk INVITE dan REGISTER")
