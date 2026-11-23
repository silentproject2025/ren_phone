#!/usr/bin/env python3
"""BROKERCFG_V1: pasang pengambilan kredensial broker dari Supabase ke app Chat & Catur.
Jalankan dari root repo:  python3 tools/apply_broker_cloud_patch.py
Aman dijalankan ulang (lewati yang sudah terpasang). Gagal keras jika teks sumber tidak cocok."""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PH = ROOT / "firmware/phone"
WF = ROOT / ".github/workflows/build-firmware.yml"

def patch(path, edits):
    s = path.read_text(encoding="utf-8")
    for tag, old, new in edits:
        if new in s:
            print(f"  = {path.name}: {tag} sudah ada"); continue
        if s.count(old) != 1:
            sys.exit(f"GAGAL {path.name}: '{tag}' cocok {s.count(old)}x (harus 1x). Kode sumber berubah?")
        s = s.replace(old, new, 1)
        print(f"  + {path.name}: {tag}")
    path.write_text(s, encoding="utf-8")

CHAT_POLL = '''  if (chatBcWait) {                         // BROKERCFG_V1: tunggu hasil ambil broker dari cloud
    const uint8_t st = bcState(BC_SVC_CHAT);
    if (st == BC_OK || st == BC_FAIL) {
      chatBcWait = false;
      String h, u, w;
      if (st == BC_OK && bcTake(BC_SVC_CHAT, h, u, w)) {
        if (bcMayOverwrite(BC_SVC_CHAT, chatHost, chatUser, chatPass)) {
          chatHost = h; chatUser = u; chatPass = w;
          chatCfgSave();
          bcMarkApplied(BC_SVC_CHAT, chatHost, chatUser, chatPass);
        }
      } else {
        bcAck(BC_SVC_CHAT);
        if (!chatConfigured()) showToast(RPTR("Gagal ambil broker, isi manual"), 2400);
      }
      chatCfgMode = !chatConfigured();
      chatActive = !chatCfgMode;
      chatConn = 0;
      needRedraw = true;
    }
  }
'''

patch(PH / "chat_renphone.ino", [
  ("BROKERCFG_V1 decl",
   "static bool chatCfgFromSd() {",
   "static bool chatBcWait = false;               // BROKERCFG_V1: sedang menunggu kredensial dari cloud\nstatic bool chatCfgFromSd() {"),
  ("BROKERCFG_V1 enter",
   "  chatCfgMode = !chatConfigured();\n  chatConn = 0;\n  chatErrBuf[0] = 0;\n  chatActive = !chatCfgMode;\n",
   "  chatBcWait = bcShouldFetch(BC_SVC_CHAT, chatHost, chatUser, chatPass) && bcRequest(BC_SVC_CHAT);   // BROKERCFG_V1\n"
   "  chatCfgMode = !chatConfigured() && !chatBcWait;\n  chatConn = 0;\n  chatErrBuf[0] = 0;\n  chatActive = !chatCfgMode && !chatBcWait;\n"),
  ("BROKERCFG_V1 exit",
   "void chatExit() {\n  chatActive = false;",
   "void chatExit() {\n  chatBcWait = false;   // BROKERCFG_V1\n  chatActive = false;"),
  ("BROKERCFG_V1 poll",
   "void chatLoopPoll() {\n  if (chatDirty) {\n    chatDirty = false;\n    if (curScreen() == SCR_CHAT) needRedraw = true;\n  }\n",
   "void chatLoopPoll() {\n  if (chatDirty) {\n    chatDirty = false;\n    if (curScreen() == SCR_CHAT) needRedraw = true;\n  }\n" + CHAT_POLL),
])

CHESS_POLL = '''  if (chsBcWait) {                          // BROKERCFG_V1: tunggu hasil ambil broker dari cloud
    const uint8_t st = bcState(BC_SVC_CHESS);
    if (st == BC_OK || st == BC_FAIL) {
      chsBcWait = false;
      String h, u, w;
      if (st == BC_OK && bcTake(BC_SVC_CHESS, h, u, w)) {
        if (chsBEdit < 0 && bcMayOverwrite(BC_SVC_CHESS, chsBHost, chsBUser, chsBPass)) {
          chsBHost = h; chsBUser = u; chsBPass = w;
          chsBrokerSave();
          bcMarkApplied(BC_SVC_CHESS, chsBHost, chsBUser, chsBPass);
        }
      } else {
        bcAck(BC_SVC_CHESS);
        if (!chsBrokerOk()) showToast(RPTR("Gagal ambil broker Catur"), 2400);
      }
      needRedraw = true;
    }
  }
'''

patch(PH / "chess_renphone.ino", [
  ("BROKERCFG_V1 decl",
   "static int8_t chsBEdit = -1;",
   "static bool   chsBcWait = false;                // BROKERCFG_V1: sedang menunggu kredensial dari cloud\nstatic int8_t chsBEdit = -1;"),
  ("BROKERCFG_V1 enter",
   '  if (!chsBrokerOk() && chsBrokerFromSd()) { chsBrokerSave(); showToast(RPTR("Broker Catur dimuat dari /chess.cfg"), 2200); }\n',
   '  if (!chsBrokerOk() && chsBrokerFromSd()) { chsBrokerSave(); showToast(RPTR("Broker Catur dimuat dari /chess.cfg"), 2200); }\n'
   '  chsBcWait = bcShouldFetch(BC_SVC_CHESS, chsBHost, chsBUser, chsBPass) && bcRequest(BC_SVC_CHESS);   // BROKERCFG_V1\n'),
  ("BROKERCFG_V1 exit",
   "void chessExit() {\n  chsActive = false;",
   "void chessExit() {\n  chsBcWait = false;        // BROKERCFG_V1\n  chsActive = false;"),
  ("BROKERCFG_V1 poll",
   "void chessLoopPoll() {\n",
   "void chessLoopPoll() {\n" + CHESS_POLL),
  ("BROKERCFG_V1 start",
   '  if (!chsBrokerOk()) { showToast(RPTR("Atur broker Catur dulu"), 2400); return; }\n',
   '  if (!chsBrokerOk() && chsBcWait) { showToast(RPTR("Mengambil broker Catur..."), 1800); return; }   // BROKERCFG_V1\n'
   '  if (!chsBrokerOk()) { showToast(RPTR("Atur broker Catur dulu"), 2400); return; }\n'),
])

patch(WF, [
  ("brokercfg_renphone.ino",
   "          cp firmware/phone/devreg_renphone.ino build_sketch/\n",
   "          cp firmware/phone/devreg_renphone.ino build_sketch/\n          cp firmware/phone/brokercfg_renphone.ino build_sketch/   # BROKERCFG_V1: kredensial broker Chat/Catur dari Supabase\n"),
])
print("selesai.")
