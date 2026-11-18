#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_via_fix_patch.py -- balasan 180/200 OK utk panggilan masuk bisa terpotong -> Android tak pernah tahu
# panggilan diangkat (statusnya tetap "outgoing call"). Plus diagnosa pengiriman.
#
# Dugaan penyebab (BELUM terbukti): server SIP menaruh beberapa header Via (satu per server yg dilewati) dan
# Record-Route yg panjang di INVITE. Kode lama cuma menyimpan 300 byte Via (header ke-2 dst dibuang diam-diam
# kalau tdk muat, tiap header dipotong di 160 byte) dan 260 byte Record-Route. Balasan dgn rantai Via yg
# kurang/terpotong tidak bisa dirutekan server kembali ke penelepon.
#
# Perubahan:
#  - Via ditampung 640 byte (maks 8 header, tiap header 300 byte), Record-Route 420 byte (entri 160 byte).
#  - Kalau masih ada yg terpotong, baris diagnosa menulis "Via dipotong" / "Via terlalu panjang" / "RR dipotong".
#  - Diagnosa baru: "tx INVITE <n>B TCP" saat menelepon, "tx 200 OK <n>B TCP" saat mengangkat.
#
#     python3 apply_call_via_fix_patch.py
import sys, os
F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'Via terlalu panjang' in s:
    print("Patch Via sudah terpasang, dilewati"); sys.exit(0)

edits = []
edits.append(("struct", 
"  char via[300]; char from[170]; char to[170]; char callId[72];\n  char cseq[40]; char rr[260]; char contact[200];",
"  char via[640]; char from[170]; char to[170]; char callId[72];\n  char cseq[40]; char rr[420]; char contact[200];"))
edits.append(("Via split",
"""  for (int k = 0; k < 6; k++) {
    char v[160];
    if (!sipHdr(msg, "Via", "v", k, v, sizeof(v))) break;
    int l = (int)strlen(v);
    if (n + l + 8 >= (int)sizeof(rq.via)) break;""",
"""  for (int k = 0; k < 8; k++) {
    char v[300];
    if (!sipHdr(msg, "Via", "v", k, v, sizeof(v))) break;
    int l = (int)strlen(v);
    if (l >= (int)sizeof(v) - 1) callDbgPush("Via dipotong");
    if (n + l + 8 >= (int)sizeof(rq.via)) { callDbgPush("Via terlalu panjang"); break; }"""))
edits.append(("RR ukuran",
"""  char ent[6][100]; int ne = 0;
  char line[320];""",
"""  char ent[6][160]; int ne = 0;
  char line[480];"""))
edits.append(("RR potong",
"""      int n = (int)(gt - lt + 1);
      if (n > 99) n = 99;""",
"""      int n = (int)(gt - lt + 1);
      if (n > 159) { callDbgPush("RR dipotong"); n = 159; }"""))
edits.append(("tx 200 OK",
"""  int n = sipBuildReply(1, 200, "OK", ex, sdp, sipTx, SIP_BUF);
  sipSendKeep(dlg.peerIp, dlg.peerPort, n);""",
"""  int n = sipBuildReply(1, 200, "OK", ex, sdp, sipTx, SIP_BUF);
  sipSendKeep(dlg.peerIp, dlg.peerPort, n);
  { char dg[40]; snprintf(dg, sizeof(dg), "tx 200 OK %dB %s", n, sipRtxTcp ? "TCP" : "UDP"); callDbgPush(dg); }"""))
edits.append(("tx INVITE",
"""    (unsigned long)dlg.inviteCseq, fu, cont, authLine ? authLine : "", (int)strlen(sdp), sdp);
  sipSendKeep(dlg.destIp, dlg.destPort, n);""",
"""    (unsigned long)dlg.inviteCseq, fu, cont, authLine ? authLine : "", (int)strlen(sdp), sdp);
  sipSendKeep(dlg.destIp, dlg.destPort, n);
  { char dg[40]; snprintf(dg, sizeof(dg), "tx INVITE %dB %s%s", n, sipRtxTcp ? "TCP" : "UDP", (authLine && authLine[0]) ? " auth" : ""); callDbgPush(dg); }"""))

for name, old, new in edits:
    c = s.count(old)
    if c != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (name, c)); sys.exit(1)
for name, old, new in edits:
    s = s.replace(old, new)
open(F, 'w', encoding='utf-8').write(s)
print("OK: Via/Record-Route diperbesar + diagnosa tx (%d perubahan)" % len(edits))
