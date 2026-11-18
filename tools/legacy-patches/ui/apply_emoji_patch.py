#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_emoji_patch.py -- dukungan EMOJI di semua teks NyxOS (lihat emoji_renphone.ino).
#
# Jalankan dari folder repo (ada phone.ino + chat_renphone.ino), dgn emoji_renphone.ino & emoji_data.h
# sudah ada di folder yg sama:
#     python3 apply_emoji_patch.py
# Idempoten & semua-atau-tidak-sama-sekali (setiap anchor harus ketemu TEPAT 1x), pola sama dgn apply_call_patch.py.
#
# Yang diubah:
#   phone.ino            : include emoji_data.h + prototipe emj*, KbMode +2 halaman emoji, keyboard (tombol smiley,
#                          hapus sadar UTF-8), SEMUA .setFont(x) -> .setFont(emjF(x)), Notepad baca/simpan,
#                          AI Chat (jawaban -> PUA, prompt -> emoji sungguhan), pemenggal baris AI Chat
#   chat_renphone.ino    : pesan masuk/keluar, pemenggal baris (chatWrap) & lebar bubble dlm "sel", pemotongan teks
#   .github/workflows/build-firmware.yml : cp emoji_renphone.ino & emoji_data.h ke build_sketch
import sys, os, re

for f in ('phone.ino', 'chat_renphone.ino', 'emoji_renphone.ino', 'emoji_data.h'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

def rd(p): return open(p, encoding='utf-8').read()
def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

out = {}

# ------------------------------------------------------------------ phone.ino
s = rd('phone.ino')
if 'emjF(' in s:
    print("phone.ino sudah memuat dukungan emoji, dilewati")
else:
    # 1. include data + prototipe + mode keyboard
    s = rep(s, "enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };\n",
        "// ---- EMOJI (emoji_renphone.ino; sprite di emoji_data.h) ----\n"
        "#include \"emoji_data.h\"\n"
        "const lgfx::IFont* emjF(const lgfx::IFont* base);       // bungkus font: print() di mana pun bisa menggambar emoji\n"
        "String emjNormalize(const String& s);                   // emoji sungguhan -> kode privat (PUA) firmware\n"
        "String emjToWire(const String& s);                      // PUA -> emoji sungguhan (utk file/jaringan)\n"
        "String emjAsciiOnly(const String& s);\n"
        "void   emjSanitizeBuf(char* dst, int dstSize, const char* src, int len);\n"
        "void   emjBackspace(String& s);                         // hapus 1 karakter UTF-8 terakhir\n"
        "String emjHead(const String& s, int maxBytes);\n"
        "String emjTail(const String& s, int maxBytes);\n"
        "void   emjCutZ(char* s, int maxBytes);\n"
        "int    emjCells(const char* p, int len);\n"
        "enum KbMode { KB_LOWER, KB_UPPER, KB_NUM, KB_EMO1, KB_EMO2 };\n", "enum KbMode")
    s = rep(s, "const char* (*kbMaps[3])[10]={kbL,kbU,kbN};",
               "const char* (*kbMaps[5])[10]={kbL,kbU,kbN,kbE1,kbE2};", "kbMaps")

    # 2. drawKb
    s = rep(s, "  auto* lay=kbMaps[(int)kbMode];\n  int kw=kbKeyW(), kh=kbKeyH(), L=kbLeft(), R=SCR_W-L;\n  const int kr=9;",
               "  auto* lay=kbMaps[(int)kbMode];\n  bool emo=(kbMode==KB_EMO1||kbMode==KB_EMO2);\n"
               "  if(emo) s.setFont(emjF(&lgfx::fonts::Font0));          // halaman emoji: Font0 + sprite (lihat emoji_renphone.ino)\n"
               "  int kw=kbKeyW(), kh=kbKeyH(), L=kbLeft(), R=SCR_W-L;\n  const int kr=9;", "drawKb lay")
    s = rep(s, "  int shX=L, shW=40, nmX=shX+shW+4, nmW=44, spX=nmX+nmW+4;\n  int dlW=84, dlX=R-dlW;",
               "  int shX=L, shW=40, nmX=shX+shW+4, nmW=44, emX=nmX+nmW+4, emW=34, spX=emX+emW+4;\n  int dlW=84, dlX=R-dlW;", "drawKb bottom row")
    s = rep(s, "    s.fillTriangle(acx,acy-6, acx-6,acy+2, acx+6,acy+2, ic);\n    s.fillRect(acx-3,acy+2,6,4,ic);\n  }",
               "    if(emo){                                            // halaman emoji: tombol ini = ganti halaman\n"
               "      const char* pg=(kbMode==KB_EMO1)?\"1/2\":\"2/2\";\n"
               "      s.setTextColor(T().text); s.setCursor(shX+(shW-s.textWidth(pg))/2,cy+kh/2-4+in); s.print(pg);\n"
               "    } else {\n"
               "      s.fillTriangle(acx,acy-6, acx-6,acy+2, acx+6,acy+2, ic);\n    s.fillRect(acx-3,acy+2,6,4,ic);\n    }\n  }", "shift icon")
    s = rep(s, "    const char* nl=(kbMode==KB_NUM)?\"ABC\":\"123\";",
               "    const char* nl=(kbMode==KB_NUM||emo)?\"ABC\":\"123\";", "123 label")
    s = rep(s, "  { // spasi (tuts huruf)\n",
               "  { // emoji (wajah kecil digambar manual, tanpa font)\n"
               "    bool pr=(kbPressKey==104); int in=pr?1:0;\n"
               "    nyxAaRR(s, emX+in,cy+in,emW-2*in,kh-2*in,kr, emo?(uint16_t)T().accent:(pr?fnP:fnC));\n"
               "    uint16_t fc = emo ? inkAcc : (uint16_t)T().text;\n"
               "    int fx=emX+emW/2, fy=cy+kh/2;\n"
               "    s.drawCircle(fx,fy,7,fc); s.fillCircle(fx-3,fy-2,1,fc); s.fillCircle(fx+3,fy-2,1,fc);\n"
               "    s.drawLine(fx-3,fy+2,fx-1,fy+4,fc); s.drawLine(fx-1,fy+4,fx+1,fy+4,fc); s.drawLine(fx+1,fy+4,fx+3,fy+2,fc);\n"
               "  }\n"
               "  { // spasi (tuts huruf)\n", "emoji key")

    # 3. kbTouch
    s = rep(s, "    int shX=L, shW=40, nmX=shX+shW+4, nmW=44, spX=nmX+nmW+4;\n    int dlX=R-84;\n"
               "    if(x<nmX-2){            kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER); kbPressKey=100; }\n"
               "    else if(x<spX-2){       kbMode=(kbMode==KB_NUM?KB_LOWER:KB_NUM);     kbPressKey=101; }\n"
               "    else if(x<dlX-2){       *kbTarget+=\" \";                              kbPressKey=102; }\n"
               "    else {\n"
               "      if(kbTarget->length()>0) *kbTarget=kbTarget->substring(0,kbTarget->length()-1);\n"
               "      kbPressKey=103;\n    }\n",
               "    int shX=L, shW=40, nmX=shX+shW+4, nmW=44, emX=nmX+nmW+4, emW=34, spX=emX+emW+4;\n    int dlX=R-84;\n"
               "    bool emo=(kbMode==KB_EMO1||kbMode==KB_EMO2);\n"
               "    if(x<nmX-2){\n"
               "      if(emo) kbMode=(kbMode==KB_EMO1?KB_EMO2:KB_EMO1);       // di halaman emoji: ganti halaman 1/2\n"
               "      else    kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER);\n"
               "      kbPressKey=100;\n"
               "    }\n"
               "    else if(x<emX-2){       kbMode=((kbMode==KB_NUM||emo)?KB_LOWER:KB_NUM); kbPressKey=101; }\n"
               "    else if(x<spX-2){       kbMode=(emo?KB_LOWER:KB_EMO1);               kbPressKey=104; }\n"
               "    else if(x<dlX-2){       *kbTarget+=\" \";                              kbPressKey=102; }\n"
               "    else {\n"
               "      emjBackspace(*kbTarget);                                 // hapus 1 karakter UTF-8 utuh (emoji = 3 byte)\n"
               "      kbPressKey=103;\n    }\n", "kbTouch")

    # 4. Notepad (file tetap UTF-8 standar)
    s = rep(s, "  if(f){noteText=f.readString();f.close();}",
               "  if(f){noteText=emjNormalize(f.readString());f.close();}", "loadNote")
    s = rep(s, "  if(f){f.print(noteText);f.close();}",
               "  if(f){f.print(emjToWire(noteText));f.close();}", "saveNote")

    # 5. AI Chat
    s = rep(s, "void sendGeminiRequest(String promptText) {\n",
               "void sendGeminiRequest(String promptText) {\n  promptText = emjToWire(promptText);   // emoji: kirim UTF-8 standar ke API\n", "sendGeminiRequest")
    s = rep(s, "  aiResponse = resp;\n  if(ok) appendAiMemory(promptText, resp);",
               "  aiResponse = emjNormalize(resp);       // emoji dari Gemini -> kode privat firmware (digambar sbg sprite)\n"
               "  if(ok) appendAiMemory(promptText, resp);", "aiResponse")
    s = rep(s, "        if(sp>0) cut=sp;\n        buf[count].text = seg.substring(0,cut);",
               "        if(sp>0) cut=sp;\n        while(cut>1 && (((unsigned char)seg[cut])&0xC0)==0x80) cut--;   // jangan belah emoji (UTF-8)\n"
               "        buf[count].text = seg.substring(0,cut);", "aiWrapAppend")

    # 6. SEMUA .setFont(x) -> .setFont(emjF(x))  (hanya kode, bukan komentar)
    cnt = [0]
    def wrap_line(line):
        if line.lstrip().startswith('//'): return line
        def sub(m):
            args = m.group(2)
            if args.startswith('emjF('): return m.group(0)
            cnt[0] += 1
            return "%s.setFont(emjF(%s));" % (m.group(1), args)
        return re.sub(r'\b(\w+)\.setFont\((?!\))([^;]*?)\);', sub, line)
    s = "\n".join(wrap_line(l) for l in s.split("\n"))
    if cnt[0] < 10:
        print("ERROR: setFont yg dibungkus cuma %d (harapan >=10). Tidak ada yang diubah." % cnt[0]); sys.exit(1)
    print("phone.ino: %d pemanggilan setFont dibungkus emjF()" % cnt[0])
    out['phone.ino'] = s

# ------------------------------------------------------------------ chat_renphone.ino
c = rd('chat_renphone.ino')
if 'emjSanitizeBuf' in c:
    print("chat_renphone.ino sudah memuat dukungan emoji, dilewati")
else:
    c = rep(c, "      chatCopyAscii(m.text, CHAT_TEXT_LEN, text, tlen);",
               "      emjSanitizeBuf(m.text, CHAT_TEXT_LEN, text, tlen);          // ASCII + emoji (PUA); selain itu '?'", "insert text")
    c = rep(c, "  chatCopyAscii(chatOutText, CHAT_TEXT_LEN, t.c_str(), (int)t.length());",
               "  emjSanitizeBuf(chatOutText, CHAT_TEXT_LEN, t.c_str(), (int)t.length());", "out text")
    c = rep(c, "  char buf[300];\n  if (len == 0 || len >= sizeof(buf)) return;",
               "  char buf[420];                                  // emoji di kabel 4 byte (di firmware 3 byte)\n  if (len == 0 || len >= sizeof(buf)) return;", "payload buf")
    c = rep(c, "            tx.replace(\"\\n\", \" \");\n",
               "            tx.replace(\"\\n\", \" \");\n            tx = emjToWire(tx);                          // emoji: kirim UTF-8 standar (HP lama/PC tetap bisa baca)\n", "wire")
    c = rep(c, "  chatHost.trim(); chatUser.trim(); chatPass.trim(); chatName.trim();\n",
               "  chatHost.trim(); chatUser.trim(); chatPass.trim(); chatName.trim();\n  chatName = emjAsciiOnly(chatName);            // nama pengguna: ASCII saja (emoji di nama tidak dikirim)\n", "cfg name")
    # chatWrap berbasis "sel": ASCII = 1 sel (6 px), emoji (PUA 3 byte) = 2 sel; tidak pernah membelah karakter
    old_wrap = c[c.index("static int chatWrap("):c.index("// \"{r:Nama:kutipan}isi\"")]
    new_wrap = '''static int chatWrap(const char* t, int maxCh, int* st, int* ln, int maxL) {
  // maxCh dlm "sel" 6 px: ASCII 1 sel, emoji (PUA, 3 byte) 2 sel. st/ln tetap dlm BYTE (dipakai utk memotong teks).
  int n = 0, i = 0, L = (int)strlen(t);
  while (i < L && n < maxL) {
    int j = i, cells = 0, lastSp = -1;
    while (j < L) {
      int bl = 1, cw = 1;
      if ((unsigned char)t[j] == 0xEE && j + 2 < L) { bl = 3; cw = 2; }
      if (cells + cw > maxCh) break;
      if (t[j] == ' ') lastSp = j;
      cells += cw; j += bl;
    }
    if (j >= L) { st[n] = i; ln[n] = L - i; n++; break; }
    int cut = j - i;
    if (lastSp > i && (lastSp - i) * 2 > cut) cut = lastSp - i;   // putus di spasi bila tidak terlalu jauh ke belakang
    if (cut < 1) cut = ((unsigned char)t[i] == 0xEE && i + 2 < L) ? 3 : 1;
    st[n] = i; ln[n] = cut; n++;
    i += cut;
    while (i < L && t[i] == ' ') i++;
  }
  return n;
}

'''
    c = c.replace(old_wrap, new_wrap)
    c = rep(c, "  else for (int k = 0; k < n; k++) if (ln[k] > ml) ml = ln[k];",
               "  else for (int k = 0; k < n; k++) { int cl = emjCells(body + st[k], ln[k]); if (cl > ml) ml = cl; }", "layout cells")
    c = rep(c, "      else for (int k = 0; k < outN; k++) if (oln[k] > outMl) outMl = oln[k];",
               "      else for (int k = 0; k < outN; k++) { int cl = emjCells(obody + ost[k], oln[k]); if (cl > outMl) outMl = cl; }", "out cells")
    c = rep(c, "        if ((int)strlen(ql) > qmax && qmax > 3) { ql[qmax - 2] = 0; strcat(ql, \"..\"); }",
               "        if ((int)strlen(ql) > qmax && qmax > 3) { emjCutZ(ql, qmax - 2); strcat(ql, \"..\"); }", "quote cut")
    c = rep(c, "    if ((int)strlen(rl) > rmax && rmax > 3) { rl[rmax - 2] = 0; strcat(rl, \"..\"); }",
               "    if ((int)strlen(rl) > rmax && rmax > 3) { emjCutZ(rl, rmax - 2); strcat(rl, \"..\"); }", "reply bar cut")
    c = rep(c, "  if ((int)shown.length() > maxIn) shown = String(\"..\") + shown.substring(shown.length() - (maxIn - 2));",
               "  if ((int)shown.length() > maxIn) shown = String(\"..\") + emjTail(shown, maxIn - 2);", "input tail")
    c = rep(c, "    if ((int)v.length() > maxC - 1) v = String(\"..\") + v.substring(v.length() - (maxC - 3));",
               "    if ((int)v.length() > maxC - 1) v = String(\"..\") + emjTail(v, maxC - 3);", "cfg tail 1")
    c = rep(c, "    if ((int)v.length() > maxC) v = String(\"..\") + v.substring(v.length() - (maxC - 2));",
               "    if ((int)v.length() > maxC) v = String(\"..\") + emjTail(v, maxC - 2);", "cfg tail 2")
    c = rep(c, "    if ((int)t.length() > room) t = t.substring(0, room);",
               "    if ((int)t.length() > room) t = emjHead(t, room);", "send room")
    c = rep(c, "  if (t.length() > 200) t = t.substring(0, 200);",
               "  if (t.length() > 200) t = emjHead(t, 200);", "send 200")
    c = rep(c, "chatReplyQuote[sizeof(chatReplyQuote) - 1] = 0;\n",
               "chatReplyQuote[sizeof(chatReplyQuote) - 1] = 0;\n              emjCutZ(chatReplyQuote, (int)sizeof(chatReplyQuote) - 1);   // jangan simpan emoji terpotong\n", "reply quote")
    out['chat_renphone.ino'] = c

# ------------------------------------------------------------------ workflow
wf = '.github/workflows/build-firmware.yml'
if os.path.exists(wf):
    w = rd(wf)
    if 'emoji_renphone.ino' in w: print("Workflow sudah memuat emoji_renphone.ino, dilewati")
    else:
        out[wf] = rep(w, "          cp chat_renphone.ino build_sketch/\n",
                         "          cp chat_renphone.ino build_sketch/\n          cp emoji_renphone.ino build_sketch/\n          cp emoji_data.h build_sketch/\n", "workflow cp")
else:
    print("PERINGATAN: workflow tidak ditemukan, lewati")

for p, txt in out.items():
    with open(p, 'w', encoding='utf-8', newline='') as f: f.write(txt)
    print("ditulis:", p)
if not out: print("Tidak ada perubahan (semua sudah terpasang).")
else: print("Selesai. Commit: phone.ino chat_renphone.ino emoji_renphone.ino emoji_data.h .github/workflows/build-firmware.yml")
