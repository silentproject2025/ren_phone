#!/usr/bin/env python3
"""
Patch AVRCP untuk Ren Phone / Accretion
=======================================
Tombol di TWS/earbuds (play/pause, next, prev, vol+, vol-) sekarang mengendalikan app Musik.

Alur:  earbuds --AVRCP--> ESP32-CAM --RP_KEY ('K') via UART--> ESP32-S3 --> pemutar musik

Perubahan:
  rplink.h (2 salinan)      : tambah RP_KEY 'K'  (CAM -> S3, payload [kode][arg])
  cam_bt_coprocessor.ino    : AVRCP Target (diambil dari proyek referensi), kirim RP_KEY
  musicbt_renphone.ino      : terima RP_KEY, jalankan play/pause/next/prev/volume
  README.md                 : satu butir fitur (kalau anchor-nya ketemu)

Jalankan di root repo:   python patch_avrcp.py
Idempotent (aman diulang).  WAJIB flash ULANG ESP32-CAM + S3.
"""
import os
import sys

ROOT = "."
cache = {}


def load(path):
    if path not in cache:
        full = os.path.join(ROOT, path)
        if not os.path.exists(full):
            sys.exit(f"[GAGAL] {path} tidak ketemu. Jalankan dari root repo.")
        with open(full, encoding="utf-8", newline="") as f:
            cache[path] = f.read()
    return cache[path]


def edit(name, path, old, new, marker, soft=False):
    s = load(path)
    if marker in s:
        print(f"[skip ] {name}")
        return
    n = s.count(old)
    if n != 1:
        msg = f"{name}: anchor ditemukan {n}x di {path} (harus 1x)."
        if soft:
            print(f"[lewat] {msg}")
            return
        sys.exit(f"[GAGAL] {msg} File kamu mungkin sudah beda dari versi yang saya baca.")
    cache[path] = s.replace(old, new)
    print(f"[ok   ] {name}")


CAM = "cam_bt_coprocessor/cam_bt_coprocessor.ino"
MU = "musicbt_renphone.ino"

# ------------------------------------------------------------------ rplink.h (2 salinan, harus identik)
for rp in ("rplink.h", "cam_bt_coprocessor/rplink.h"):
    edit(f"{rp}: RP_KEY", rp,
         "#define RP_HELLO    'R'   // CAM baru boot\n",
         "#define RP_HELLO    'R'   // CAM baru boot\n"
         "#define RP_KEY      'K'   // tombol earbuds (AVRCP). payload: [kode][arg]\n"
         "                          //   kode: 1 play, 2 pause/stop, 3 next, 4 prev, 5 vol+, 6 vol-, 7 volume absolut (arg 0..127)\n",
         "#define RP_KEY ")

# ------------------------------------------------------------------ CAM
edit("CAM: komentar header", CAM,
     " *  Batasan v1: MP3 Layer III 44.1 kHz stereo saja (WAV/EQ/crossfade/AVRCP\n"
     " *  sengaja belum ikut).\n",
     " *  Batasan v1: MP3 Layer III 44.1 kHz stereo saja (WAV/EQ/crossfade\n"
     " *  sengaja belum ikut).\n"
     " *  AVRCP Target: tombol earbuds (play/pause/next/prev/vol) diteruskan ke S3 lewat RP_KEY.\n",
     "AVRCP Target: tombol earbuds")

edit("CAM: include esp_avrc_api.h", CAM,
     "#include <esp_gap_bt_api.h>\n",
     "#include <esp_gap_bt_api.h>\n#include <esp_avrc_api.h>\n",
     "#include <esp_avrc_api.h>")

AVRCP_BLOCK = r'''// ----------------------------------------------------------
// AVRCP TARGET: tombol di earbuds -> antrean -> loop() kirim RP_KEY ke S3
// (callback jalan di task BT, jadi JANGAN kirim UART dari sini -- cukup masukkan ke antrean)
// ----------------------------------------------------------
#define KEY_PLAY    1
#define KEY_PAUSE   2
#define KEY_NEXT    3
#define KEY_PREV    4
#define KEY_VOLUP   5
#define KEY_VOLDOWN 6
#define KEY_ABSVOL  7

struct KeyEvt { uint8_t code; uint8_t arg; };
static QueueHandle_t       keyQ = nullptr;
static volatile bool       avrcpStackReady = false;
static volatile bool       avrcpNeedInit = false;
static uint32_t            avrcpInitAt = 0;

static void keyPush(uint8_t code, uint8_t arg) {
  if (!keyQ) return;
  KeyEvt e; e.code = code; e.arg = arg;
  xQueueSend(keyQ, &e, 0);
}

static void avrc_tg_callback(esp_avrc_tg_cb_event_t ev, esp_avrc_tg_cb_param_t* p) {
  if (ev == ESP_AVRC_TG_PASSTHROUGH_CMD_EVT) {
    if (p->psth_cmd.key_state != 0) return;          // 0 = ditekan; abaikan event "dilepas"
    switch (p->psth_cmd.key_code) {
      case ESP_AVRC_PT_CMD_PLAY:     keyPush(KEY_PLAY, 0);    break;
      case ESP_AVRC_PT_CMD_PAUSE:    keyPush(KEY_PAUSE, 0);   break;
      case ESP_AVRC_PT_CMD_STOP:     keyPush(KEY_PAUSE, 0);   break;
      case ESP_AVRC_PT_CMD_FORWARD:  keyPush(KEY_NEXT, 0);    break;
      case ESP_AVRC_PT_CMD_BACKWARD: keyPush(KEY_PREV, 0);    break;
      case ESP_AVRC_PT_CMD_VOL_UP:   keyPush(KEY_VOLUP, 0);   break;
      case ESP_AVRC_PT_CMD_VOL_DOWN: keyPush(KEY_VOLDOWN, 0); break;
      default: break;
    }
  } else if (ev == ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT) {
    keyPush(KEY_ABSVOL, (uint8_t)(p->set_abs_vol.volume & 0x7F));
  }
}

static void avrcpStackInit() {
  esp_err_t r = esp_avrc_tg_init();
  avrcpStackReady = (r == ESP_OK || r == ESP_ERR_INVALID_STATE);   // INVALID_STATE = sudah di-init library
  Serial.printf("[AVRCP] tg_init=%d ready=%d\n", (int)r, (int)avrcpStackReady);
}

// dipanggil ~0,5 dtk setelah TWS tersambung (urutan sama dengan proyek referensi)
static void avrcpSetupFilter() {
  if (!avrcpStackReady) return;
  if (esp_avrc_tg_register_callback(avrc_tg_callback) != ESP_OK) { Serial.println("[AVRCP] register_callback gagal"); return; }
  esp_avrc_psth_bit_mask_t cs; memset(&cs, 0, sizeof(cs));
  if (esp_avrc_tg_get_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_ALLOWED_CMD, &cs) != ESP_OK) {
    memset(&cs, 0, sizeof(cs));
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_PLAY);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_PAUSE);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_STOP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_FORWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_BACKWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_VOL_UP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_VOL_DOWN);
  }
  esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &cs);
  esp_avrc_rn_evt_cap_mask_t es; memset(&es, 0, sizeof(es));
  esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &es, ESP_AVRC_RN_VOLUME_CHANGE);
  esp_avrc_tg_set_rn_evt_cap(&es);
  Serial.println("[AVRCP] filter terpasang");
}

void onConnectionChanged(esp_a2d_connection_state_t state, void* obj) {
'''

edit("CAM: blok AVRCP Target", CAM,
     "void onConnectionChanged(esp_a2d_connection_state_t state, void* obj) {\n",
     AVRCP_BLOCK,
     "static void avrc_tg_callback(")

edit("CAM: jadwalkan setup AVRCP saat tersambung", CAM,
     "    btConnected = true; btDisconnectedAt = 0;\n"
     "    applyTxGain(txGain);\n"
     "    a2dp.set_volume(volumeNow);\n"
     "    Serial.println(\"[BT] Terhubung\");\n",
     "    btConnected = true; btDisconnectedAt = 0;\n"
     "    applyTxGain(txGain);\n"
     "    a2dp.set_volume(volumeNow);\n"
     "    avrcpInitAt = millis(); avrcpNeedInit = true;     // pasang filter AVRCP 0,5 dtk lagi (dari loop)\n"
     "    Serial.println(\"[BT] Terhubung\");\n",
     "avrcpInitAt = millis(); avrcpNeedInit = true;")

edit("CAM: batalkan jadwal AVRCP saat putus", CAM,
     "    btConnected = false;\n"
     "    btDisconnectedAt = millis();\n"
     "    Serial.println(\"[BT] Terputus\");\n",
     "    btConnected = false;\n"
     "    avrcpNeedInit = false;\n"
     "    btDisconnectedAt = millis();\n"
     "    Serial.println(\"[BT] Terputus\");\n",
     "btConnected = false;\n    avrcpNeedInit = false;")

edit("CAM: buat antrean tombol", CAM,
     "  cmdQ = xQueueCreate(8, sizeof(Cmd));\n",
     "  cmdQ = xQueueCreate(8, sizeof(Cmd));\n"
     "  keyQ = xQueueCreate(12, sizeof(KeyEvt));\n",
     "keyQ = xQueueCreate(")

edit("CAM: init AVRCP stack di setup", CAM,
     "  a2dp.start(savedBTName[0] ? savedBTName : BT_DEFAULT_NAME);\n"
     "  delay(300);\n"
     "  applyTxGain(txGain);\n",
     "  a2dp.start(savedBTName[0] ? savedBTName : BT_DEFAULT_NAME);\n"
     "  delay(300);\n"
     "  applyTxGain(txGain);\n"
     "  avrcpStackInit();\n",
     "  avrcpStackInit();\n")

edit("CAM: loop -> filter AVRCP + kirim RP_KEY", CAM,
     "  if (volDirty) { volDirty = false; volumeNow = pendingVol; a2dp.set_volume(volumeNow); }\n",
     "  // AVRCP: pasang filter setelah TWS tersambung (jeda 0,5 dtk, seperti proyek referensi)\n"
     "  if (avrcpNeedInit && btConnected && millis() - avrcpInitAt >= 500) {\n"
     "    avrcpNeedInit = false;\n"
     "    avrcpSetupFilter();\n"
     "  }\n"
     "  // tombol earbuds -> S3. Kirim UART HANYA dari sini (satu pengirim = frame tidak saling menyela)\n"
     "  {\n"
     "    static uint32_t lastKeyMs = 0; static uint8_t lastKey = 0;\n"
     "    KeyEvt ke;\n"
     "    while (xQueueReceive(keyQ, &ke, 0) == pdTRUE) {\n"
     "      uint32_t tk = millis();\n"
     "      if (ke.code <= KEY_PREV && ke.code == lastKey && tk - lastKeyMs < 250) continue;  // buang pantulan tombol\n"
     "      lastKey = ke.code; lastKeyMs = tk;\n"
     "      uint8_t kb[2] = { ke.code, ke.arg };\n"
     "      rpSend(Serial2, RP_KEY, kb, 2);\n"
     "      Serial.printf(\"[AVRCP] key=%u arg=%u\\n\", (unsigned)ke.code, (unsigned)ke.arg);\n"
     "    }\n"
     "  }\n\n"
     "  if (volDirty) { volDirty = false; volumeNow = pendingVol; a2dp.set_volume(volumeNow); }\n",
     "tombol earbuds -> S3. Kirim UART")

# ------------------------------------------------------------------ S3 (musicbt_renphone.ino)
edit("Musik: handler tombol earbuds", MU,
     "// =====================================================================\n"
     "//  TASK UTAMA LINK (core 0, stack PSRAM)\n"
     "// =====================================================================\n",
     "// ---- tombol earbuds (AVRCP) yang diteruskan CAM lewat RP_KEY ----\n"
     "//  kode: 1 play, 2 pause/stop, 3 next, 4 prev, 5 vol+, 6 vol-, 7 volume absolut (arg 0..127)\n"
     "static void musSetVol(int v);   // definisi di bagian UI di bawah\n"
     "static void musOnKey(uint8_t code, uint8_t arg) {\n"
     "  switch (code) {\n"
     "    case 1: if (!musLoaded || musPaused) musPost('u', 0, nullptr); break;   // PLAY: hanya kalau sedang berhenti/jeda\n"
     "    case 2: if (musLoaded && !musPaused) musPost('u', 0, nullptr); break;   // PAUSE/STOP: hanya kalau sedang main\n"
     "    case 3: musPost('n', 0, nullptr); break;\n"
     "    case 4: musPost('b', 0, nullptr); break;\n"
     "    case 5: musSetVol((int)musVol + 8); break;\n"
     "    case 6: musSetVol((int)musVol - 8); break;\n"
     "    case 7: musSetVol((int)arg); break;\n"
     "    default: break;\n"
     "  }\n"
     "}\n\n"
     "// =====================================================================\n"
     "//  TASK UTAMA LINK (core 0, stack PSRAM)\n"
     "// =====================================================================\n",
     "static void musOnKey(")

edit("Musik: terima RP_KEY dari CAM", MU,
     "        case RP_HELLO:    musHello = true; break;\n",
     "        case RP_HELLO:    musHello = true; break;\n"
     "        case RP_KEY:      if (P.len >= 1) musOnKey(P.payload[0], P.len >= 2 ? P.payload[1] : 0); break;\n",
     "case RP_KEY:")

# ------------------------------------------------------------------ README (opsional)
edit("README: butir AVRCP", "README.md",
     "- **Mode acak 3 pilihan**: mati, biasa, atau **AI (RYNE v2)**.\n",
     "- **Mode acak 3 pilihan**: mati, biasa, atau **AI (RYNE v2)**.\n"
     "- **AVRCP (kontrol dari earbuds)**: tombol play/pause, next, prev, dan volume di TWS langsung mengendalikan app Musik. "
     "ESP32-CAM menangkap perintah AVRCP lalu meneruskannya ke S3 lewat frame `RP_KEY`. "
     "Sebagian earbuds hanya mengirim sebagian tombol, tergantung merek.\n",
     "**AVRCP (kontrol dari earbuds)**", soft=True)

# ------------------------------------------------------------------ tulis semua
for path, content in cache.items():
    with open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as f:
        f.write(content)
print("\nSelesai. Flash ULANG ESP32-CAM (cam_bt_coprocessor) DAN compile ulang firmware S3 (phone.ino).")
