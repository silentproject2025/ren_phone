#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_doom_full_patch.py -- DOOM lewat Gamepad web: SEMUA tombol + geser-lihat (mouse-look)
#
# Jalankan dari folder repo (yang ada doom_renphone.ino + gamepad_renphone.ino + esp_doom_core/):
#     python3 apply_doom_full_patch.py
#
# PENTING: gamepad_renphone.ino versi BARU (yang dikirim bareng script ini) harus
# sudah menimpa yang lama SEBELUM build. Script ini hanya memasang sisi engine:
#
#   esp_doom_core/DoomGlue.cpp <- DoomGlue_SetHooks(), 31 tombol EXTRA (strafe, run, alt,
#                                 senjata 1-7, MAP, Y/N, BS, PAUSE, F1-F11, -, =, prev/next senjata),
#                                 antrean teks ketikan, DG_MouseTake(); senjata prev/next
#                                 dipaksa ke ',' dan '.' setelah doomgeneric_Create()
#   esp_doom_core/i_input.c    <- I_GetEvent(): kirim ev_mouse (geser-lihat) ke engine,
#                                 di-skip selagi menu DOOM terbuka
#   esp_doom_core/g_game.c     <- mousex/mousey dijumlah (+=) bukan ditimpa, biar beberapa
#                                 event dalam 1 tic gak saling menghapus
#   doom_renphone.ino          <- daftarkan 3 hook (padExtKeys/padTakeMouseDX/padTakeTyped)
#                                 sebelum DoomGlue_Begin()
#
# Idempoten. Kalau ada anchor yang tidak ketemu persis 1x, TIDAK ADA file yang diubah.
import sys, os

NEED = ['esp_doom_core/DoomGlue.cpp', 'esp_doom_core/i_input.c', 'esp_doom_core/g_game.c',
        'doom_renphone.ino', 'gamepad_renphone.ino']
for f in NEED:
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari folder repo)" % f); sys.exit(1)

if 'padTakeMouseDX' not in open('gamepad_renphone.ino', encoding='utf-8').read():
    print("ERROR: gamepad_renphone.ino masih versi LAMA (belum ada padTakeMouseDX).")
    print("       Timpa dulu dengan gamepad_renphone.ino versi baru, baru jalankan script ini.")
    sys.exit(1)

files, eols, changed = {}, {}, set()

def load(p):
    if p not in files:
        raw = open(p, encoding='utf-8', newline='').read()
        eols[p] = '\r\n' if '\r\n' in raw else '\n'
        files[p] = raw.replace('\r\n', '\n')
    return files[p]

def rep(path, old, new, what, marker):
    s = load(path)
    if marker in s:
        print("  (lewati) %s: %s sudah terpasang" % (path, what)); return
    n = s.count(old)
    if n != 1:
        print("ERROR: anchor '%s' di %s ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, path, n))
        sys.exit(1)
    files[path] = s.replace(old, new)
    changed.add(path)
    print("  ok: %s: %s" % (path, what))

# ------------------------------------------------------------------ DoomGlue.cpp
G = 'esp_doom_core/DoomGlue.cpp'

rep(G,
"IDoomInput*   DG_Input   = nullptr;\n",
"""IDoomInput*   DG_Input   = nullptr;

// ------------------------------------------------------------------
//  HOOK DARI SKETCH (gamepad web) -- tombol EXTRA, geser-lihat, teks
// ------------------------------------------------------------------
//  IDoomInput cuma punya 8 tombol. Gamepad web butuh jauh lebih banyak
//  (strafe, run, senjata, F-keys, ...), jadi sketch mendaftarkan 3 fungsi
//  lewat DoomGlue_SetHooks(). Kalau tidak didaftarkan, semuanya no-op.
//  Bit EXTMAP di bawah HARUS sama persis dgn urutan EXN[] di halaman
//  gamepad_renphone.ino.
typedef uint32_t (*DG_ExtKeysFn)(void);
typedef int32_t  (*DG_MouseFn)(void);
typedef int      (*DG_TypedFn)(void);
static DG_ExtKeysFn hookExt   = nullptr;
static DG_MouseFn   hookMouse = nullptr;
static DG_TypedFn   hookTyped = nullptr;

void DoomGlue_SetHooks(DG_ExtKeysFn ext, DG_MouseFn mouse, DG_TypedFn typed)
{
    hookExt = ext; hookMouse = mouse; hookTyped = typed;
}

// Dipanggil dari I_GetEvent() (i_input.c): delta geser-lihat sejak panggilan lalu.
extern "C" int DG_MouseTake(void)
{
    return hookMouse ? (int)hookMouse() : 0;
}

extern "C" { extern int key_prevweapon; extern int key_nextweapon; }
""",
"DoomGlue_SetHooks", "void DoomGlue_SetHooks(")

rep(G,
"""static constexpr uint32_t POLL_INTERVAL_MS = 5;
static uint32_t lastPoll = 0;
""",
"""static constexpr uint32_t POLL_INTERVAL_MS = 5;
static uint32_t lastPoll = 0;

static inline int QueueFree() { return (qHead - qTail - 1 + QUEUE_SIZE) % QUEUE_SIZE; }

// bit i dari mask EXTRA -> kode tombol DOOM (urutan = EXN[] di halaman web)
static const unsigned char EXTMAP[31] =
{
    KEY_STRAFE_L, KEY_STRAFE_R, KEY_RSHIFT /*run*/, KEY_RALT /*alt = strafe-hold*/,
    '1', '2', '3', '4', '5', '6', '7',
    KEY_TAB /*map*/, 'y', 'n', KEY_BACKSPACE, KEY_PAUSE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11,
    KEY_MINUS, KEY_EQUALS,
    ',' /*senjata sebelumnya*/, '.' /*senjata berikutnya*/
};
static uint32_t extPrev = 0;
""",
"EXTMAP", "static const unsigned char EXTMAP[31]")

rep(G,
"""        else if (DG_Input->released(m.button)) QueuePush(m.key, 0);
    }
}
""",
"""        else if (DG_Input->released(m.button)) QueuePush(m.key, 0);
    }

    // ---- tombol EXTRA dari gamepad web (edge: tekan/lepas) ----
    if (hookExt)
    {
        uint32_t ext = hookExt();
        uint32_t diff = ext ^ extPrev;
        for (int i = 0; i < 31 && diff; i++)
        {
            uint32_t bit = 1u << i;
            if (!(diff & bit)) continue;
            if (QueueFree() < 1) break;          // antrean penuh -> coba lagi di poll berikut
            QueuePush(EXTMAP[i], (ext & bit) ? 1 : 0);
            extPrev ^= bit;
            diff &= ~bit;
        }
    }

    // ---- teks ketikan (nama save, cheat): 1 karakter = tekan+lepas ----
    if (hookTyped && QueueFree() >= 2)
    {
        int c = hookTyped();
        if (c > 0)
        {
            unsigned char k = (c == 127) ? KEY_BACKSPACE : (unsigned char)c;
            QueuePush(k, 1);
            QueuePush(k, 0);
        }
    }
}
""",
"ext = hookExt()", "ext = hookExt();")

rep(G,
"    doomgeneric_Create(3, argv);\n",
"""    extPrev = 0;
    doomgeneric_Create(3, argv);

    // Dipaksa SETELAH doomgeneric_Create() (default.cfg di SD bisa menimpa nilainya):
    // senjata sebelumnya / berikutnya dipakai tombol ◀ ▶ di halaman gamepad.
    key_prevweapon = ',';
    key_nextweapon = '.';
""",
"key_nextweapon = '.';", "key_nextweapon = '.';")

# ------------------------------------------------------------------ i_input.c
I = 'esp_doom_core/i_input.c'

rep(I,
"""void I_GetEvent(void)
{
    event_t event;
    int pressed;
    unsigned char key;
""",
"""// Geser-lihat dari gamepad web (ala mouse). Diimplementasi di DoomGlue.cpp.
// Menu DOOM terbuka -> dilewati (kalau tidak, geser malah menggeser pilihan menu).
extern int DG_MouseTake(void);

static void PostMouseLook(void)
{
    event_t mev;
    int dx = DG_MouseTake();

    if (dx == 0 || menuactive)
    {
        return;
    }

    mev.type = ev_mouse;
    mev.data1 = 0;      // tidak ada tombol mouse
    mev.data2 = dx;     // putar (g_game.c: angleturn -= mousex*8)
    mev.data3 = 0;      // maju/mundur tetap lewat tombol/stick
    mev.data4 = 0;
    D_PostEvent(&mev);
}

void I_GetEvent(void)
{
    event_t event;
    int pressed;
    unsigned char key;

    PostMouseLook();
""",
"PostMouseLook", "static void PostMouseLook(void)")

rep(I,
'#include "doomgeneric.h"\n',
'#include "doomgeneric.h"\n#include "doomstat.h"   // menuactive (dipakai PostMouseLook)\n',
"include doomstat.h", '#include "doomstat.h"   // menuactive')

# ------------------------------------------------------------------ g_game.c
J = 'esp_doom_core/g_game.c'
rep(J,
"mousex = ev->data2*(mouseSensitivity+5)/10;",
"mousex += ev->data2*(mouseSensitivity+5)/10;",
"mousex dijumlah", "mousex += ev->data2")
rep(J,
"mousey = ev->data3*(mouseSensitivity+5)/10;",
"mousey += ev->data3*(mouseSensitivity+5)/10;",
"mousey dijumlah", "mousey += ev->data3")

# ------------------------------------------------------------------ doom_renphone.ino
D = 'doom_renphone.ino'
rep(D,
"extern volatile uint8_t rcPadMask; // gamepad_renphone.ino: bit i = DBTN_i (UP,DOWN,LEFT,RIGHT,FIRE,USE,START,SELECT)\n",
"""extern volatile uint8_t rcPadMask; // gamepad_renphone.ino: bit i = DBTN_i (UP,DOWN,LEFT,RIGHT,FIRE,USE,START,SELECT)
// Hook gamepad web lengkap (gamepad_renphone.ino) -> didaftarkan ke DoomGlue di doomTaskFunc().
uint32_t padExtKeys();      // 31 tombol EXTRA (strafe, run, senjata, F-keys, ...)
int32_t  padTakeMouseDX();  // geser-lihat (delta sejak panggilan lalu)
int      padTakeTyped();    // 1 karakter ketikan (0 = kosong)
void DoomGlue_SetHooks(uint32_t (*ext)(void), int32_t (*mouse)(void), int (*typed)(void)); // esp_doom_core/DoomGlue.cpp
""",
"padExtKeys", "uint32_t padExtKeys();")

rep(D,
"  DG_Input   = &doomInput;\n",
"  DG_Input   = &doomInput;\n  DoomGlue_SetHooks(padExtKeys, padTakeMouseDX, padTakeTyped);\n",
"DoomGlue_SetHooks dipanggil", "DoomGlue_SetHooks(padExtKeys")

# ------------------------------------------------------------------ tulis
if not changed:
    print("Tidak ada perubahan (semua sudah terpasang)."); sys.exit(0)
for p in changed:
    open(p, 'w', encoding='utf-8', newline='').write(files[p].replace('\n', eols[p]))
print("\nSelesai. File diubah: " + ", ".join(sorted(changed)))
print("Lanjut: git add -A && git commit -m 'DOOM gamepad: semua tombol + geser-lihat' && git push")
