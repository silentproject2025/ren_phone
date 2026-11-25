// =====================================================================
// factory_renphone.ino -- FACTORY_RESET_V1: Reset Pabrik (Settings > Tentang)
//
// Yang DIHAPUS : seluruh NVS (pengaturan, kalibrasi sentuh, WiFi tersimpan, login broker Chat/Chess, kunci
//                API Gemini di NVS, statistik baterai, dst.) + di SD: /prefs (limpahan NVS), /gemini_key.txt,
//                /chat.cfg, /chess.cfg, /ai_memory.txt.
// Yang TIDAK disentuh: firmware, foto, musik, buku, memo, dan berkas pribadi lain di SD.
// Sesudah itu HP restart; boot berikutnya seperti HP baru (kalibrasi layar diminta lagi).
// =====================================================================
#include <nvs_flash.h>

// hapus pohon folder (kedalaman kecil; nama dikumpulkan dulu baru dihapus, spt SdPrefs::clear)
static void frRmTree(const String& path, int depth) {
  if (depth > 3) return;
  File r = SD_MMC.open(path.c_str());
  if (!r) return;
  if (!r.isDirectory()) { r.close(); SD_MMC.remove(path.c_str()); return; }
  String names[24]; bool isDir[24]; int n = 0;
  File e = r.openNextFile();
  while (e && n < 24) { names[n] = String(e.name()); isDir[n] = e.isDirectory(); n++; e = r.openNextFile(); }
  r.close();
  for (int i = 0; i < n; i++) {
    String nm = names[i]; int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
    String f = path + "/" + nm;
    if (isDir[i]) frRmTree(f, depth + 1); else SD_MMC.remove(f.c_str());
  }
  SD_MMC.rmdir(path.c_str());
}

void factoryResetNow() {
  if (sdReady) {
    frRmTree(String("/prefs"), 0);
    SD_MMC.remove("/gemini_key.txt");
    SD_MMC.remove("/chat.cfg");
    SD_MMC.remove("/chess.cfg");
    SD_MMC.remove("/ai_memory.txt");
  }
  nvs_flash_erase();
  Serial.println("[factory] NVS dihapus -> restart");
  delay(300);
  ESP.restart();
}
