// =================================================================
// ren_phone v4 — "OS" rewrite (FIXED GEMINI AI HTTP ERROR -1)
// ESP32-S3, ILI9341, XPT2046 touch, SD via SDIO
//
// PERBAIKAN BUG HTTP ERROR -1:
//  1. Menambah Stack Size task FreeRTOS Gemini dari 8KB ke 16KB (16384 byte)
//     karena MbedTLS SSL Handshake di ESP32-S3 butuh alokasi memori stack cukup besar.
//  2. Menambahkan timeout socket & TLS connection handshake (15000 ms).
//  3. Menambahkan helper http.errorToString() untuk diagnosa koneksi detail.
// =================================================================
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "FS.h"
#include "SD_MMC.h"

// =============================================
// TYPE DEFINITIONS
// =============================================
enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SENSOR,
              SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS, SCR_AICHAT, SCR_FILEEXPLORER };
enum Orientation { ORIENT_LANDSCAPE = 0, ORIENT_PORTRAIT = 1 };

struct Theme {
  const char* name;
  uint16_t bg, surface, surface2, accent, accent2,
           text, subtext, divider, good, danger;
};

Theme themes[] = {
  { "Dark",   0x1084,0x2104,0x31A6,0xFD40,0x04FF,0xFFFF,0x8C51,0x2965,0x07E0,0xF800 },
  { "AMOLED", 0x0000,0x1082,0x2104,0xFD40,0x04FF,0xFFFF,0x8C51,0x2104,0x07E0,0xF800 },
  { "Light",  0xEF5D,0xFFFF,0xDEFB,0xFD00,0x001F,0x0000,0x6B4D,0xC618,0x03E0,0xD000 },
  { "Pastel", 0xFF1F,0xFFFF,0xFF9F,0xFB16,0x5D9F,0x39C7,0x9C92,0xFEB7,0x2FE7,0xE8B4 },
};
#define THEME_COUNT 4
int themeIdx = 0;
Theme& T() { return themes[themeIdx]; }

void saveTheme(){ Preferences p; p.begin("ui",false); p.putInt("theme",themeIdx); p.end(); }
void loadTheme(){
  Preferences p; p.begin("ui",true);
  themeIdx = p.getInt("theme",0); p.end();
  if(themeIdx<0||themeIdx>=THEME_COUNT) themeIdx=0;
}

enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };

// =============================================
// LGFX CONFIG
// =============================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;
public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host=SPI2_HOST; cfg.spi_mode=0;
      cfg.freq_write=40000000; cfg.freq_read=16000000;
      cfg.spi_3wire=false; cfg.use_lock=true;
      cfg.dma_channel=SPI_DMA_CH_AUTO;
      cfg.pin_sclk=12; cfg.pin_mosi=11;
      cfg.pin_miso=13; cfg.pin_dc=2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs=10; cfg.pin_rst=14; cfg.pin_busy=-1;
      cfg.memory_width=240; cfg.memory_height=320;
      cfg.panel_width=240;  cfg.panel_height=320;
      cfg.offset_x=0; cfg.offset_y=0; cfg.offset_rotation=0;
      cfg.dummy_read_pixel=8; cfg.dummy_read_bits=1;
      cfg.readable=true; cfg.invert=false;
      cfg.rgb_order=false; cfg.dlen_16bit=false;
      cfg.bus_shared=false;
      _panel_instance.config(cfg); }
    { auto cfg = _light_instance.config();
      cfg.pin_bl=21; cfg.invert=false;
      cfg.freq=44100; cfg.pwm_channel=7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance); }
    { auto cfg = _touch_instance.config();
      cfg.pin_int=-1; cfg.bus_shared=false;
      cfg.offset_rotation=0; cfg.spi_host=SPI3_HOST;
      cfg.freq=2000000;
      cfg.pin_sclk=6; cfg.pin_mosi=5;
      cfg.pin_miso=4; cfg.pin_cs=9;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance); }
    setPanel(&_panel_instance);
  }
};
LGFX display;
LGFX_Sprite canvas(&display);      // frame buffer utama (full screen)
LGFX_Sprite canvasApp(&display);   // buffer khusus app Canvas (drawing)

// =============================================
// ORIENTASI
// =============================================
Orientation currentOrient = ORIENT_LANDSCAPE;
int SCR_W = 320, SCR_H = 240;
#define STATUS_H 22   // tinggi status bar

void saveOrientPref() {
  Preferences p; p.begin("ui", false);
  p.putInt("orient", (int)currentOrient);
  p.end();
}
Orientation loadOrientPref() {
  Preferences p; p.begin("ui", true);
  int v = p.getInt("orient", (int)ORIENT_LANDSCAPE);
  p.end();
  return (v == (int)ORIENT_PORTRAIT) ? ORIENT_PORTRAIT : ORIENT_LANDSCAPE;
}

void loadCanvas();
void saveCanvas();
int  canvasAppHeight();
void needRedrawNow();

void applyOrientation(Orientation o, bool doSave) {
  currentOrient = o;
  display.setRotation(o == ORIENT_LANDSCAPE ? 1 : 0);
  SCR_W = display.width();
  SCR_H = display.height();
  canvas.deleteSprite();
  canvas.setPsram(true);
  canvas.createSprite(SCR_W, SCR_H);
  canvasApp.deleteSprite();
  canvasApp.setPsram(true);
  canvasApp.createSprite(SCR_W, canvasAppHeight());
  canvasApp.fillSprite(T().bg);
  loadCanvas();
  if (doSave) saveOrientPref();
  needRedrawNow();
}

// =============================================
// SD CARD & GEMINI API KEY STORAGE
// =============================================
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40
bool sdReady=false;
const char* NOTE_FILE="/notepad.txt";
const char* GEMINI_KEY_FILE="/gemini_key.txt";

String geminiApiKey = "";

void initSD(){
  SD_MMC.setPins(SD_PIN_CLK,SD_PIN_CMD,SD_PIN_D0);
  sdReady=SD_MMC.begin("/sdcard",true);
}
String noteText="";
void loadNote(){
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_READ);
  if(f){noteText=f.readString();f.close();}
}
void saveNote(){
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_WRITE);
  if(f){f.print(noteText);f.close();}
}

void loadGeminiKey(){
  if(!sdReady) return;
  if(!SD_MMC.exists(GEMINI_KEY_FILE)){
    File f = SD_MMC.open(GEMINI_KEY_FILE, FILE_WRITE);
    if(f){
      f.println("# =============================================");
      f.println("# GEMINI API KEY CONFIGURATION (ESP32-S3)");
      f.println("# Dapatkan API key gratis dari Google AI Studio:");
      f.println("# https://aistudio.google.com/app/apikey");
      f.println("#");
      f.println("# Tempel API Key kamu di baris tanpa tanda '#':");
      f.println("# =============================================");
      f.println("YOUR_GEMINI_API_KEY_HERE");
      f.close();
    }
  }
  File f = SD_MMC.open(GEMINI_KEY_FILE, FILE_READ);
  if(f){
    geminiApiKey = "";
    while(f.available()){
      String line = f.readStringUntil('\n');
      line.trim();
      if(line.length() > 0 && !line.startsWith("#")){
        geminiApiKey = line;
        break;
      }
    }
    f.close();
  }
}
void saveGeminiKey(){
  if(!sdReady) return;
  File f = SD_MMC.open(GEMINI_KEY_FILE, FILE_WRITE);
  if(f){
    f.println("# =============================================");
    f.println("# GEMINI API KEY CONFIGURATION (ESP32-S3)");
    f.println("# Dapatkan API key gratis dari Google AI Studio:");
    f.println("# https://aistudio.google.com/app/apikey");
    f.println("# =============================================");
    f.println(geminiApiKey);
    f.close();
  }
}

int canvasAppHeight(){
  return SCR_H - STATUS_H - 44;
}
const char* canvasFileFor(Orientation o){
  return o==ORIENT_LANDSCAPE ? "/canvas_land.bin" : "/canvas_port.bin";
}
void loadCanvas(){
  if(!sdReady) return;
  uint32_t need = (uint32_t)SCR_W * canvasAppHeight() * 2;
  File f=SD_MMC.open(canvasFileFor(currentOrient),FILE_READ);
  if(f && f.size()==need) f.read((uint8_t*)canvasApp.getBuffer(), need);
  if(f) f.close();
}
void saveCanvas(){
  if(!sdReady) return;
  uint32_t need = (uint32_t)SCR_W * canvasAppHeight() * 2;
  File f=SD_MMC.open(canvasFileFor(currentOrient),FILE_WRITE);
  if(f){ f.write((uint8_t*)canvasApp.getBuffer(), need); f.close(); }
}

// =============================================
// WEB SERVER (FILE MANAGER & INLINE TEXT EDITOR)
// =============================================
WebServer webServer(80);
bool webServerRunning = false;
File uploadFile;

void handleWebRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 File Manager</title>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#fff;padding:15px;max-width:650px;margin:auto;}";
  html += ".card{background:#1e1e1e;padding:15px;border-radius:10px;margin-bottom:15px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}";
  html += "a{color:#00e5ff;text-decoration:none;margin-right:5px;} .btn{background:#e91e63;color:#fff;padding:8px 14px;border:none;border-radius:6px;cursor:pointer;font-weight:bold;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:10px;} td,th{padding:10px;text-align:left;border-bottom:1px solid #333;}</style>";
  html += "</head><body><h2>ESP32 Web File Manager</h2>";
  
  // Upload Form
  html += "<div class='card'><h3>Upload File ke ESP32 SD Card</h3>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='upload' required style='margin-bottom:10px;'><br>";
  html += "<input type='submit' class='btn' value='Upload File'>";
  html += "</form></div>";

  // File List
  html += "<div class='card'><h3>Daftar File SD Card</h3><table><tr><th>Nama File</th><th>Ukuran</th><th>Aksi</th></tr>";
  if (sdReady) {
    File root = SD_MMC.open("/");
    File f = root.openNextFile();
    int count = 0;
    while (f) {
      if (!f.isDirectory()) {
        count++;
        String fname = String(f.name());
        if (!fname.startsWith("/")) fname = "/" + fname;
        html += "<tr><td>" + fname + "</td><td>" + String(f.size()) + " B</td><td>";
        
        if (fname.endsWith(".txt") || fname.endsWith(".bin") == false) {
          html += "<a href='/edit?file=" + fname + "' style='color:#ffca28;'>Edit</a> | ";
        }
        
        html += "<a href='/download?file=" + fname + "'>Download</a> | ";
        html += "<a href='/delete?file=" + fname + "' style='color:#ff5252' onclick=\"return confirm('Hapus file " + fname + "?')\">Hapus</a></td></tr>";
      }
      f = root.openNextFile();
    }
    if(count==0) html += "<tr><td colspan='3'>SD Card Kosong</td></tr>";
  } else {
    html += "<tr><td colspan='3'>SD Card Tidak Terdeteksi</td></tr>";
  }
  html += "</table></div></body></html>";

  webServer.send(200, "text/html", html);
}

void handleWebEditGet() {
  if (!webServer.hasArg("file")) {
    webServer.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String fname = webServer.arg("file");
  if (!fname.startsWith("/")) fname = "/" + fname;
  String content = "";
  if (sdReady && SD_MMC.exists(fname)) {
    File f = SD_MMC.open(fname, FILE_READ);
    if (f) { content = f.readString(); f.close(); }
  }

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Edit " + fname + "</title>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#fff;padding:15px;max-width:600px;margin:auto;}";
  html += "textarea{width:100%;height:260px;background:#1e1e1e;color:#00e5ff;border:1px solid #444;border-radius:8px;padding:12px;font-size:14px;box-sizing:border-box;}";
  html += ".btn{background:#4caf50;color:#fff;padding:10px 18px;border:none;border-radius:6px;cursor:pointer;font-weight:bold;font-size:14px;margin-top:10px;}";
  html += ".btn-back{background:#555;color:#fff;padding:10px 18px;border-radius:6px;text-decoration:none;display:inline-block;margin-right:10px;font-size:14px;}</style>";
  html += "</head><body><h2>Edit File: " + fname + "</h2>";
  html += "<form method='POST' action='/save'>";
  html += "<input type='hidden' name='file' value='" + fname + "'>";
  html += "<textarea name='content'>" + content + "</textarea><br>";
  html += "<a href='/' class='btn-back'>&lt; Batal</a>";
  html += "<input type='submit' class='btn' value='Simpan File'>";
  html += "</form></body></html>";

  webServer.send(200, "text/html", html);
}

void handleWebEditPost() {
  if (webServer.hasArg("file") && webServer.hasArg("content")) {
    String fname = webServer.arg("file");
    String content = webServer.arg("content");
    if (!fname.startsWith("/")) fname = "/" + fname;
    if (sdReady) {
      File f = SD_MMC.open(fname, FILE_WRITE);
      if (f) {
        f.print(content);
        f.close();
        if (fname == NOTE_FILE) loadNote();
        if (fname == GEMINI_KEY_FILE) loadGeminiKey();
        needRedrawNow();
      }
    }
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleUploadStream() {
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    uploadFile = SD_MMC.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    needRedrawNow();
  }
}

void handleWebDelete() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (SD_MMC.exists(path)) {
      SD_MMC.remove(path);
      needRedrawNow();
    }
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleWebDownload() {
  if (webServer.hasArg("file")) {
    String path = webServer.arg("file");
    if (SD_MMC.exists(path)) {
      File f = SD_MMC.open(path, FILE_READ);
      webServer.streamFile(f, "application/octet-stream");
      f.close();
      return;
    }
  }
  webServer.send(404, "text/plain", "File Not Found");
}

void startWebServer(){
  if(webServerRunning) return;
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/edit", HTTP_GET, handleWebEditGet);
  webServer.on("/save", HTTP_POST, handleWebEditPost);
  webServer.on("/upload", HTTP_POST, [](){
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  }, handleUploadStream);
  webServer.on("/delete", HTTP_GET, handleWebDelete);
  webServer.on("/download", HTTP_GET, handleWebDownload);
  webServer.begin();
  webServerRunning = true;
}

// =============================================
// WIFI & NTP & AUTO SCAN
// =============================================
char WIFI_SSID[64]="", WIFI_PASSWORD[64]="";
const char* NTP_SERVER="pool.ntp.org";
const long  GMT_OFFSET=7*3600;
const int   DST_OFFSET=0;
bool wifiConnected=false, ntpSynced=false;
bool airplaneMode=false;

bool wifiScanning = false;
struct ScannedWifi {
  String ssid;
  int rssi;
  bool isEncrypted;
};
#define MAX_SCANNED_WIFI 8
ScannedWifi scannedWifis[MAX_SCANNED_WIFI];
int scannedWifiNum = 0;

void startWifiScan() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  wifiScanning = true;
  scannedWifiNum = 0;
  needRedrawNow();
}

void checkWifiScanComplete() {
  if (!wifiScanning) return;
  int n = WiFi.scanComplete();
  if (n >= 0) {
    wifiScanning = false;
    scannedWifiNum = min(n, MAX_SCANNED_WIFI);
    for (int i = 0; i < scannedWifiNum; i++) {
      scannedWifis[i].ssid = WiFi.SSID(i);
      scannedWifis[i].rssi = WiFi.RSSI(i);
      scannedWifis[i].isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    needRedrawNow();
  }
}

void loadWifiCreds(){
  Preferences p; p.begin("wifi",true);
  p.getString("ssid","").toCharArray(WIFI_SSID,64);
  p.getString("pass","").toCharArray(WIFI_PASSWORD,64);
  p.end();
}
void saveWifiCreds(){
  Preferences p; p.begin("wifi",false);
  p.putString("ssid",WIFI_SSID);
  p.putString("pass",WIFI_PASSWORD);
  p.end();
}
void connectWifi(){
  if(airplaneMode || !strlen(WIFI_SSID)) return;
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int t=0;
  while(WiFi.status()!=WL_CONNECTED&&t<20){delay(300);t++;}
  wifiConnected=(WiFi.status()==WL_CONNECTED);
  if(wifiConnected){
    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);
    struct tm tm; if(getLocalTime(&tm,5000)) ntpSynced=true;
    startWebServer();
  }
}
void disconnectWifi(){
  WiFi.disconnect(true);
  wifiConnected=false;
}
void toggleAirplaneMode(){
  airplaneMode=!airplaneMode;
  if(airplaneMode) disconnectWifi();
  else connectWifi();
}

// =============================================
// GEMINI AI HTTP CLIENT & FREERTOS TASK (FIXED STACK & TIMEOUT)
// =============================================
String aiPrompt = "";
String aiResponse = "";
bool aiLoading = false;

void sendGeminiRequest(String promptText) {
  if (!wifiConnected) {
    aiResponse = "Error: WiFi belum terhubung!";
    aiLoading = false;
    needRedrawNow();
    return;
  }
  
  geminiApiKey.trim();
  if (geminiApiKey.length() == 0 || geminiApiKey == "YOUR_GEMINI_API_KEY_HERE") {
    aiResponse = "Error: Isi API Key di /gemini_key.txt pada SD Card!";
    aiLoading = false;
    needRedrawNow();
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15); // Timeout socket TLS 15 detik

  HTTPClient http;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + geminiApiKey;

  http.setTimeout(15000);
  http.setConnectTimeout(15000);

  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");

    // Format & Escape JSON Payload dengan benar
    String escapedPrompt = promptText;
    escapedPrompt.replace("\\", "\\\\");
    escapedPrompt.replace("\"", "\\\"");
    escapedPrompt.replace("\r", "");
    escapedPrompt.replace("\n", "\\n");

    String jsonPayload = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapedPrompt + "\"}]}]}";

    int httpCode = http.POST(jsonPayload);
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == 200) {
        String respStr = http.getString();
        int textIdx = respStr.indexOf("\"text\": \"");
        if (textIdx >= 0) {
          int start = textIdx + 9;
          int end = respStr.indexOf("\"", start);
          if (end > start) {
            String rawText = respStr.substring(start, end);
            rawText.replace("\\n", "\n");
            rawText.replace("\\\"", "\"");
            aiResponse = rawText;
          } else {
            aiResponse = respStr;
          }
        } else {
          aiResponse = "Response: " + respStr.substring(0, 150);
        }
      } else {
        String errBody = http.getString();
        aiResponse = "HTTP Error " + String(httpCode) + ": " + errBody.substring(0, 100);
      }
    } else {
      aiResponse = "HTTP Connection Error: " + http.errorToString(httpCode) + " (" + String(httpCode) + ")";
    }
    http.end();
  } else {
    aiResponse = "Gagal inisialisasi koneksi HTTP.";
  }

  aiLoading = false;
  needRedrawNow();
}

void geminiTaskFunc(void* parameter) {
  sendGeminiRequest(aiPrompt);
  vTaskDelete(NULL);
}

void triggerGeminiAI() {
  if (aiPrompt.length() == 0 || aiLoading) return;
  aiLoading = true;
  aiResponse = "Menghubungi Gemini AI...";
  needRedrawNow();
  // Gunakan stack size 16KB (16384 byte) agar MbedTLS SSL Handshake tidak stack overflow
  xTaskCreate(geminiTaskFunc, "geminiTask", 16384, NULL, 1, NULL);
}

// =============================================
// TOUCH CALIBRATION
// =============================================
void loadOrRunCalibration(){
  Preferences p; p.begin("touch_cal",false);
  bool done=p.getBool("done",false);
  if(done){
    uint16_t d[8]; p.getBytes("data",d,sizeof(d));
    display.setTouchCalibrate(d);
  } else {
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE); display.setTextSize(2);
    display.setCursor(20,100); display.println("Kalibrasi Touch");
    display.setTextSize(1);
    display.setCursor(20,130); display.println("Sentuh tanda di setiap sudut");
    uint16_t d[8];
    display.calibrateTouch(d,TFT_WHITE,TFT_BLACK,15);
    p.putBytes("data",d,sizeof(d));
    p.putBool("done",true);
    display.fillScreen(TFT_BLACK);
    delay(200);
  }
  p.end();
}

// =============================================
// STATE GLOBAL / LIFECYCLE
// =============================================
int brightness=200;
bool needRedraw=true;
void needRedrawNow(){ needRedraw=true; }
bool locked = true;
bool controlCenterOpen = false;
bool dndMode = false;
bool kbVisible=false;
String* kbTarget=nullptr;
KbMode kbMode=KB_LOWER;

// -------- Navigation stack --------
#define NAV_MAX 8
Screen navStack[NAV_MAX];
int navDepth=0;
Screen curScreen(){ return navDepth>0 ? navStack[navDepth-1] : SCR_HOME; }

void appOnEnter(Screen s);
void appOnExit(Screen s);

void navPush(Screen s){
  appOnExit(curScreen());
  if(navDepth<NAV_MAX) navStack[navDepth++]=s;
  appOnEnter(s);
  kbVisible=false; kbTarget=nullptr;
  needRedraw=true;
}
void navGoHome(){
  appOnExit(curScreen());
  navDepth=0;
  kbVisible=false; kbTarget=nullptr;
  needRedraw=true;
}
void navBack(){
  appOnExit(curScreen());
  if(navDepth>0) navDepth--;
  if(navDepth>0) appOnEnter(navStack[navDepth-1]);
  kbVisible=false; kbTarget=nullptr;
  needRedraw=true;
}

// =============================================
// VIRTUAL KEYBOARD
// =============================================
const char* kbL[3][10]={
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l",";"},
  {"z","x","c","v","b","n","m",",",".","?"}};
const char* kbU[3][10]={
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L",":"},
  {"Z","X","C","V","B","N","M","!","@","#"}};
const char* kbN[3][10]={
  {"1","2","3","4","5","6","7","8","9","0"},
  {"-","=","[","]","/","'","\"","<",">","\\"},
  {"~","!","@","#","$","%","^","&","*","("}};
const char* (*kbMaps[3])[10]={kbL,kbU,kbN};

int kbKeyW(){ return (SCR_W-8)/10 - 2; }
int kbKeyH(){ return 22; }
int kbY(){ return SCR_H - 4*(kbKeyH()+2) - 4; }
int kbOffCol(int row){
  int base = 4;
  if(row==1) base += (kbKeyW()+2)/2;
  if(row==2) base += (kbKeyW()+2);
  return base;
}

void drawKb(LGFX_Sprite& s){
  int y0=kbY();
  s.fillRect(0,y0-2,SCR_W,SCR_H-y0+2,T().surface);
  s.drawFastHLine(0,y0-2,SCR_W,T().divider);
  auto* lay=kbMaps[(int)kbMode];
  int kw=kbKeyW(), kh=kbKeyH();
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2), ox=kbOffCol(r);
    for(int c=0;c<10;c++){
      int kx=ox+c*(kw+2);
      s.fillRoundRect(kx,ry,kw,kh,3,T().surface2);
      s.setTextColor(T().text); s.setTextSize(1);
      s.setCursor(kx+kw/2-3,ry+kh/2-4);
      s.print(lay[r][c]);
    }
  }
  int cy=y0+3*(kh+2);
  int spaceW = SCR_W - 96 - 88 - 8;
  s.fillRoundRect(4,cy,40,kh,3,kbMode==KB_UPPER?T().accent:T().surface2);
  s.setTextColor(T().text);s.setTextSize(1);s.setCursor(10,cy+kh/2-4);s.print("SHF");
  s.fillRoundRect(48,cy,44,kh,3,kbMode==KB_NUM?T().accent:T().surface2);
  s.setCursor(54,cy+kh/2-4);s.print(kbMode==KB_NUM?"ABC":"123");
  s.fillRoundRect(96,cy,max(40,spaceW),kh,3,T().surface2);
  s.setCursor(96+max(40,spaceW)/2-24,cy+kh/2-4);s.print("SPACE");
  s.fillRoundRect(SCR_W-88,cy,84,kh,3,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-70,cy+kh/2-4);s.print("<--");
}

void kbTouch(int x,int y){
  if(!kbVisible||!kbTarget) return;
  int y0=kbY();
  if(y<y0-2) return;
  int kw=kbKeyW(), kh=kbKeyH();
  int cy=y0+3*(kh+2);
  int spaceW = max(40, SCR_W-96-88-8);
  if(y>=cy&&y<=cy+kh){
    if(x>=4&&x<=44)            kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER);
    else if(x>=48&&x<=92)      kbMode=(kbMode==KB_NUM?KB_LOWER:KB_NUM);
    else if(x>=96&&x<=96+spaceW) *kbTarget+=" ";
    else if(x>=SCR_W-88&&kbTarget->length()>0)
      *kbTarget=kbTarget->substring(0,kbTarget->length()-1);
    needRedraw=true;
    return;
  }
  auto* lay=kbMaps[(int)kbMode];
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2);
    if(y>=ry&&y<=ry+kh){
      int ox=kbOffCol(r);
      for(int c=0;c<10;c++){
        int kx=ox+c*(kw+2);
        if(x>=kx&&x<=kx+kw){
          *kbTarget+=lay[r][c];
          if(kbMode==KB_UPPER)kbMode=KB_LOWER;
          needRedraw=true;
          return;
        }
      }
    }
  }
}

// =============================================
// TOAST
// =============================================
String toastMsg="";
unsigned long toastUntil=0;
void showToast(const char* msg,int ms=1400){
  if(dndMode) return;
  toastMsg=msg; toastUntil=millis()+ms;
}
void drawToast(LGFX_Sprite& s){
  if(!toastMsg.length()||millis()>toastUntil) return;
  int tw=toastMsg.length()*6+16;
  int tx=SCR_W/2-tw/2;
  int ty=SCR_H-36;
  s.fillRoundRect(tx,ty,tw,22,6,T().accent2);
  s.setTextColor(T().bg);s.setTextSize(1);
  s.setCursor(tx+8,ty+7);s.print(toastMsg.c_str());
}

// =============================================
// STATUS BAR
// =============================================
void drawStatusBar(LGFX_Sprite& s){
  s.fillRect(0,0,SCR_W,STATUS_H,T().surface);
  s.drawFastHLine(0,STATUS_H,SCR_W,T().divider);
  struct tm t;
  bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(ok?T().text:T().subtext);s.setTextSize(1);
  if(ok){char b[6];sprintf(b,"%02d:%02d",t.tm_hour,t.tm_min);s.setCursor(8,7);s.print(b);}
  else{s.setCursor(8,7);s.print("--:--");}
  int rx = SCR_W-14;
  if(sdReady){s.setTextColor(T().good);s.setCursor(rx-46,7);s.print("SD");}
  if(airplaneMode){
    s.setTextColor(T().subtext); s.setCursor(rx-4,7); s.print("A");
  } else if(wifiConnected){
    s.fillCircle(rx,16,2,T().good);
    s.drawArc(rx,18,5,4,210,330,T().good);
    s.drawArc(rx,18,9,8,210,330,T().good);
  } else {s.setTextColor(T().danger);s.setCursor(rx-6,7);s.print("X");}
  if(dndMode){ s.setTextColor(T().accent); s.setCursor(rx-66,7); s.print("DND"); }
  if(curScreen()==SCR_HOME && !controlCenterOpen){
    s.fillRoundRect(SCR_W/2-12,STATUS_H+2,24,3,2,T().divider);
  }
}

// =============================================
// BACK BUTTON
// =============================================
#define BACK_W 62
#define BACK_H 24
int backX(){ return 4; }
int backY(){ return SCR_H-BACK_H-3; }

void drawBack(LGFX_Sprite& s){
  if(kbVisible)return;
  s.fillRoundRect(backX(),backY(),BACK_W,BACK_H,6,T().surface2);
  s.setTextColor(T().accent);s.setTextSize(1);
  s.setCursor(backX()+10,backY()+8);s.print("< Back");
}

bool isBack(int x,int y){
  return !kbVisible && x>=backX() && x<=backX()+BACK_W && y>=backY() && y<=backY()+BACK_H;
}

// =============================================
// LOCK SCREEN
// =============================================
float lockDragY=0; bool lockDragging=false;
void drawLockScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  s.setTextColor(T().text); s.setTextSize(4);
  int tw=strlen(tb)*24;
  s.setCursor(SCR_W/2-tw/2, SCR_H/2-60-(int)lockDragY);
  s.print(tb);
  if(ok){
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28]; sprintf(db,"%s, %02d %s %04d",days[t.tm_wday],t.tm_mday,mons[t.tm_mon],t.tm_year+1900);
    s.setTextColor(T().subtext); s.setTextSize(1);
    int dw=strlen(db)*6;
    s.setCursor(SCR_W/2-dw/2, SCR_H/2-10-(int)lockDragY);
    s.print(db);
  }
  s.setTextColor(T().subtext); s.setTextSize(1);
  const char* hint="^ geser ke atas utk buka ^";
  s.setCursor(SCR_W/2-strlen(hint)*3, SCR_H-24);
  s.print(hint);
}

void lockScreenInput(bool touched,bool newT,int tx,int ty){
  static int startY=0;
  if(newT){ startY=ty; lockDragging=true; lockDragY=0; needRedraw=true; }
  else if(touched && lockDragging){
    int dy = startY-ty;
    lockDragY = constrain((float)dy, 0.0f, 80.0f);
    needRedraw=true;
  } else if(!touched && lockDragging){
    lockDragging=false;
    if(lockDragY>50){ locked=false; navGoHome(); }
    lockDragY=0; needRedraw=true;
  }
}

// =============================================
// CONTROL CENTER
// =============================================
float ccOffset = 0;
bool  ccAnimatingOpen=false, ccAnimatingClose=false;
#define CC_PANEL_H_FRAC 0.62f
void openControlCenter(){ controlCenterOpen=true; ccAnimatingOpen=true; needRedraw=true; }
void closeControlCenter(){ ccAnimatingOpen=false; ccAnimatingClose=true; needRedraw=true; }
int ccPanelH(){ return (int)(SCR_H*CC_PANEL_H_FRAC); }

void ccActWifi(){ if(wifiConnected) disconnectWifi(); else connectWifi(); }
void ccActAirplane(){ toggleAirplaneMode(); }
void ccActDnd(){ dndMode=!dndMode; }
void ccActTheme(){ themeIdx=(themeIdx+1)%THEME_COUNT; saveTheme(); }
void ccActOrient(){
  applyOrientation(currentOrient==ORIENT_LANDSCAPE?ORIENT_PORTRAIT:ORIENT_LANDSCAPE, true);
}

void drawControlCenter(LGFX_Sprite& s){
  int ph = (int)ccOffset;
  if(ph<=0) return;
  int panelY = 0;
  s.fillRoundRect(0,panelY,SCR_W,ph,0,T().surface);
  s.fillRoundRect(SCR_W/2-16,ph-10,32,4,2,T().divider);
  int gap=8, cols=3;
  int cw=(SCR_W-gap*(cols+1))/cols;
  int ch=54;
  const char* labels[6]={
    wifiConnected?"WiFi: ON":"WiFi: OFF",
    airplaneMode?"Airplane: ON":"Airplane: OFF",
    dndMode?"DND: ON":"DND: OFF",
    "Tema",
    currentOrient==ORIENT_LANDSCAPE?"Orient: Land":"Orient: Port",
    "Kunci Layar"
  };
  bool activeState[6]={wifiConnected,airplaneMode,dndMode,false,false,false};
  for(int i=0;i<6 && (STATUS_H+8+(i/cols+1)*(ch+gap)) < ph-8; i++){
    int col=i%cols, row=i/cols;
    int x=gap+col*(cw+gap);
    int y=STATUS_H+8+row*(ch+gap);
    uint16_t bg = activeState[i]? T().accent : T().surface2;
    uint16_t fg = activeState[i]? T().bg : T().text;
    s.fillRoundRect(x,y,cw,ch,8,bg);
    s.setTextColor(fg); s.setTextSize(1);
    int lw=strlen(labels[i])*6;
    s.setCursor(x+cw/2-lw/2, y+ch/2-4);
    s.print(labels[i]);
  }
  int sliderY = STATUS_H+8+2*(ch+gap)+6;
  if(sliderY+30 < ph-8){
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(gap, sliderY); s.print("Brightness");
    int sx=gap, sw=SCR_W-gap*2, sy=sliderY+12;
    s.fillRoundRect(sx,sy,sw,10,5,T().divider);
    s.fillRoundRect(sx,sy,map(brightness,0,255,0,sw),10,5,T().accent);
  }
  drawToast(s);
}

void ccTouch(int x,int y){
  int ph=(int)ccOffset;
  if(y> ph-10 && y<=ph+4){ closeControlCenter(); return; }
  if(y>ph) { closeControlCenter(); return; }
  int gap=8, cols=3;
  int cw=(SCR_W-gap*(cols+1))/cols;
  int ch=54;
  void(*actions[6])() = { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, nullptr };
  for(int i=0;i<6;i++){
    int col=i%cols, row=i/cols;
    int bx=gap+col*(cw+gap);
    int by=STATUS_H+8+row*(ch+gap);
    if(x>=bx&&x<=bx+cw&&y>=by&&y<=by+ch){
      if(i==5){ locked=true; closeControlCenter(); return; }
      if(actions[i]) actions[i]();
      needRedraw=true;
      return;
    }
  }
  int sliderY = STATUS_H+8+2*(ch+gap)+6;
  int sy=sliderY+12;
  if(y>=sy-6 && y<=sy+16){
    int sx=gap, sw=SCR_W-gap*2;
    brightness=constrain(map(x-sx,0,sw,0,255),10,255);
    display.setBrightness(brightness);
    needRedraw=true;
  }
}

// =============================================
// HOME SCREEN & APP INTERFACE
// =============================================
struct AppDef {
  const char* name; char sym; uint16_t color;
  void(*onEnter)(); void(*onExit)();
  void(*draw)(LGFX_Sprite&);
  void(*touch)(int,int,bool,bool);
  Screen screen;
};

void clockEnter(); void clockExit();
void drawClock(LGFX_Sprite&); void clockTouch(int,int,bool,bool);
void calcEnter(); void calcExit();
void drawCalc(LGFX_Sprite&); void calcTouch(int,int,bool,bool);
void sensorEnter(); void sensorExit();
void drawSensor(LGFX_Sprite&); void sensorTouch(int,int,bool,bool);
void settingsEnter(); void settingsExit();
void drawSettings(LGFX_Sprite&); void settingsTouch(int,int,bool,bool);
void notepadEnter(); void notepadExit();
void drawNotepad(LGFX_Sprite&); void notepadTouch(int,int,bool,bool);
void canvasEnter(); void canvasExit();
void drawCanvasScreen(LGFX_Sprite&); void canvasTouch(int,int,bool,bool);
void aiEnter(); void aiExit();
void drawAiChat(LGFX_Sprite&); void aiTouch(int,int,bool,bool);
void fileExpEnter(); void fileExpExit();
void drawFileExplorer(LGFX_Sprite&); void fileExpTouch(int,int,bool,bool);

AppDef apps[8] = {
  { "Jam",        'J', 0, clockEnter,    clockExit,    drawClock,        clockTouch,    SCR_CLOCK },
  { "Kalkulator", '+', 0, calcEnter,     calcExit,     drawCalc,         calcTouch,     SCR_CALC },
  { "Sensor",     '~', 0, sensorEnter,   sensorExit,   drawSensor,       sensorTouch,   SCR_SENSOR },
  { "Setting",    '@', 0, settingsEnter, settingsExit, drawSettings,     settingsTouch, SCR_SETTINGS },
  { "Notepad",    'N', 0, notepadEnter,  notepadExit,  drawNotepad,      notepadTouch,  SCR_NOTEPAD },
  { "Canvas",     'C', 0, canvasEnter,   canvasExit,   drawCanvasScreen, canvasTouch,   SCR_CANVAS },
  { "AI Chat",    'A', 0, aiEnter,       aiExit,       drawAiChat,       aiTouch,       SCR_AICHAT },
  { "Files",      'F', 0, fileExpEnter,  fileExpExit,  drawFileExplorer, fileExpTouch,  SCR_FILEEXPLORER },
};

void initAppColors(){
  apps[0].color=T().accent;   apps[1].color=T().accent2;
  apps[2].color=0x07FF;       apps[3].color=0xF81F;
  apps[4].color=0xFFE0;       apps[5].color=T().good;
  apps[6].color=0xFD40;       apps[7].color=0x3ADF;
}

int appIndexForScreen(Screen s){
  for(int i=0;i<8;i++) if(apps[i].screen==s) return i;
  return -1;
}
void appOnEnter(Screen s){ int i=appIndexForScreen(s); if(i>=0 && apps[i].onEnter) apps[i].onEnter(); }
void appOnExit(Screen s){  int i=appIndexForScreen(s); if(i>=0 && apps[i].onExit)  apps[i].onExit(); }

float homeScrollY=0, homeScrollVel=0;
int homeCols(){ return currentOrient==ORIENT_LANDSCAPE ? 3 : 2; }
int homeCardW(){ int cols=homeCols(); return (SCR_W - (cols+1)*6)/cols; }
int homeCardH(){ return 60; }
int homeDockY(){ return SCR_H-38-6; }

void drawHome(LGFX_Sprite& s,float sc){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(T().text);s.setTextSize(3);
  char tb[6]; if(ok)sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min);else strcpy(tb,"--:--");
  int tw=strlen(tb)*18;
  s.setCursor(SCR_W/2-tw/2,26);s.print(tb);
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=6, gridTop=72;
  for(int i=0;i<8;i++){
    int col=i%cols, row=i/cols;
    int x=gap+col*(cw+gap), y=gridTop+row*(ch+gap)-(int)sc;
    if(y+ch<STATUS_H+2||y>homeDockY()-4)continue;
    s.fillRoundRect(x,y,cw,ch,10,T().surface);
    s.fillCircle(x+cw/2,y+18,14,apps[i].color);
    s.setTextColor(T().bg);s.setTextSize(2);
    char sym[2]={apps[i].sym,0};
    s.setCursor(x+cw/2-6,y+11);s.print(sym);
    s.setTextColor(T().text);s.setTextSize(1);
    int nl=strlen(apps[i].name)*6;
    s.setCursor(x+cw/2-nl/2,y+ch-12);
    s.print(apps[i].name);
  }
  int dockY=homeDockY();
  s.fillRoundRect(6,dockY,SCR_W-12,38,12,T().surface2);
  int dockIdx[4]={0,4,6,7};
  int dw=(SCR_W-12)/4;
  for(int i=0;i<4;i++){
    int di=dockIdx[i];
    int cx=6+i*dw+dw/2;
    s.fillCircle(cx,dockY+19,15,apps[di].color);
    s.setTextColor(T().bg);s.setTextSize(2);
    char sym[2]={apps[di].sym,0};
    s.setCursor(cx-6,dockY+12);s.print(sym);
  }
  drawToast(s);
}

Screen homeCheck(int x,int y,float sc){
  int dockY=homeDockY();
  if(y>=dockY&&y<=dockY+38){
    int dockIdx[4]={0,4,6,7};
    int dw=(SCR_W-12)/4;
    for(int i=0;i<4;i++){
      int cx=6+i*dw+dw/2;
      if(abs(x-cx)<dw/2) return apps[dockIdx[i]].screen;
    }
  }
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=6, gridTop=72;
  for(int i=0;i<8;i++){
    int col=i%cols,row=i/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-(int)sc;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return apps[i].screen;
  }
  return SCR_HOME;
}

int homeMaxScroll(){
  int cols=homeCols();
  int rows=(8+cols-1)/cols;
  int ch=homeCardH(), gap=6, gridTop=72;
  int needed = gridTop + rows*(ch+gap) - homeDockY();
  return max(0, needed);
}

// =============================================
// APP: JAM
// =============================================
void clockEnter(){} void clockExit(){}
void drawClock(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,28);s.print("Jam");
  struct tm t;bool ok=ntpSynced&&getLocalTime(&t);
  if(ok){
    char tb[9];sprintf(tb,"%02d:%02d:%02d",t.tm_hour,t.tm_min,t.tm_sec);
    s.setTextColor(T().text);s.setTextSize(3);s.setCursor(20,55);s.print(tb);
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28];sprintf(db,"%s, %02d %s %04d",days[t.tm_wday],t.tm_mday,mons[t.tm_mon],t.tm_year+1900);
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(20,100);s.print(db);
    s.drawFastHLine(20,114,SCR_W-40,T().accent);
    s.setTextColor(T().good);s.setCursor(20,120);s.print("NTP Sync OK");
  } else {
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);s.print("Tidak sync");
  }
  drawBack(s);drawToast(s);
}
void clockTouch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)) navBack();
}

// =============================================
// APP: KALKULATOR
// =============================================
String calcInput="0";float calcA=0;char calcOp=0;bool calcNew=true;
const char* calcLabels[5][4] = {
  {"C","+/-","%","/"},
  {"7","8","9","x"},
  {"4","5","6","-"},
  {"1","2","3","+"},
  {"0",".","=","="}
};
void calcEnter(){} void calcExit(){}

void calcBtnRect(int r,int c,int& x,int& y,int& w,int& h){
  int top = STATUS_H + 34;
  int bottom = backY() - 6;
  int gap = 4;
  int gridW = SCR_W - 8;
  int gridH = bottom - top;
  int rows=5, cols=4;
  int cw = (gridW-(cols-1)*gap)/cols;
  int ch = (gridH-(rows-1)*gap)/rows;
  y = top + r*(ch+gap);
  h = ch;
  if(r==4 && c==0){ x=4; w=cw*2+gap; return; }
  if(r==4 && c==2){ x=4+2*(cw+gap); w=cw*2+gap; return; }
  if(r==4 && (c==1||c==3)) { w=0; return; }
  x = 4 + c*(cw+gap);
  w = cw;
}

void drawCalc(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,25);s.print("Kalkulator");
  s.fillRoundRect(4,STATUS_H+8,SCR_W-8,22,4,T().surface);
  s.setTextColor(T().text);s.setTextSize(2);
  int tw2=calcInput.length()*12;
  s.setCursor(max(8,SCR_W-12-tw2),STATUS_H+11);s.print(calcInput);
  for(int r=0;r<5;r++){
    for(int c=0;c<4;c++){
      int x,y,w,h; calcBtnRect(r,c,x,y,w,h);
      if(w==0) continue;
      const char* l=calcLabels[r][c];
      bool isOp = (!strcmp(l,"/")||!strcmp(l,"x")||!strcmp(l,"-")||!strcmp(l,"+"));
      bool isEq = (r==4&&c==2);
      uint16_t bg = isEq?T().accent2:(isOp?T().accent:T().surface);
      uint16_t fg = (isOp||isEq)?T().bg:T().text;
      s.fillRoundRect(x,y,w,h,6,bg);
      s.setTextColor(fg);s.setTextSize(2);
      int lw=strlen(l)*12;s.setCursor(x+w/2-lw/2,y+h/2-8);s.print(l);
    }
  }
  drawBack(s);
  drawToast(s);
}

void calcApplyLabel(const char* l){
  if(!strcmp(l,"C")){calcInput="0";calcA=0;calcOp=0;calcNew=true;}
  else if(!strcmp(l,"+/-"))calcInput=String(calcInput.toFloat()*-1);
  else if(!strcmp(l,"%"))calcInput=String(calcInput.toFloat()/100);
  else if(!strcmp(l,"=")){
    float b2=calcInput.toFloat(),res=0;
    if(calcOp=='+')res=calcA+b2;else if(calcOp=='-')res=calcA-b2;
    else if(calcOp=='x')res=calcA*b2;
    else if(calcOp=='/')res=b2?calcA/b2:0;
    calcInput=(res==(int)res)?String((int)res):String(res,4);
    calcOp=0;calcNew=true;
  }
  else if(!strcmp(l,"+")||!strcmp(l,"-")||!strcmp(l,"x")||!strcmp(l,"/")){
    calcA=calcInput.toFloat();calcOp=l[0];calcNew=true;
  } else {
    if(calcNew){calcInput="";calcNew=false;}
    if(!strcmp(l,".")&&calcInput.indexOf('.')>=0)return;
    if(calcInput=="0"&&strcmp(l,".")!=0)calcInput="";
    calcInput+=l;
  }
  if(calcInput.length()>12)calcInput=calcInput.substring(0,12);
}

void calcTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  for(int r=0;r<5;r++){
    for(int c=0;c<4;c++){
      int bx,by,bw,bh; calcBtnRect(r,c,bx,by,bw,bh);
      if(bw==0) continue;
      if(x>=bx&&x<=bx+bw&&y>=by&&y<=by+bh){ 
        calcApplyLabel(calcLabels[r][c]); 
        needRedraw=true; 
        return; 
      }
    }
  }
}

// =============================================
// APP: SENSOR
// =============================================
void sensorEnter(){} void sensorExit(){}
void drawSensor(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent2);s.setTextSize(1);s.setCursor(8,28);s.print("Sensor");
  float temp=temperatureRead();
  int bx=20, by=42, bw=SCR_W-40, bh=100;
  s.fillRoundRect(bx,by,bw,bh,14,T().surface);
  s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(bx+15,by+14);s.print("Suhu Internal Chip");
  char buf[8];sprintf(buf,"%.1f",temp);
  s.setTextColor(T().accent2);s.setTextSize(4);s.setCursor(bx+20,by+28);s.print(buf);
  s.setTextSize(2);s.print(" C");
  int gy=by+bh+6;
  s.fillRoundRect(bx,gy,bw,18,6,T().divider);
  int barMax=bw-4;
  int bwv = constrain(map((int)temp,20,90,0,barMax),0,barMax);
  uint16_t bc=(temp<50)?T().good:(temp<70)?T().accent:T().danger;
  s.fillRoundRect(bx+2,gy+2,bwv,14,4,bc);
  s.setTextColor(T().subtext);s.setTextSize(1);
  s.setCursor(bx,gy+22);s.print("20C");
  s.setCursor(bx+bw/2-12,gy+22);s.print("55C");
  s.setCursor(bx+bw-24,gy+22);s.print("90C");
  drawBack(s);drawToast(s);
}
void sensorTouch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)) navBack();
}

// =============================================
// APP: SETTINGS
// =============================================
String settSSID="",settPass="";
bool settShowPass=false;int settFocus=-1;

void settingsEnter(){ 
  settSSID=String(WIFI_SSID); 
  settPass=String(WIFI_PASSWORD); 
  startWifiScan();
}
void settingsExit(){}

void drawSettings(LGFX_Sprite& s){
  checkWifiScanComplete();

  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,26);s.print("Pengaturan");
  
  s.fillRoundRect(SCR_W-74,24,66,18,4,wifiScanning?T().surface2:T().accent);
  s.setTextColor(wifiScanning?T().subtext:T().bg);
  s.setCursor(SCR_W-68,29);
  s.print(wifiScanning?"Memindai":"Pindai");

  int rowY = STATUS_H+12;
  int rowW = SCR_W-16;
  
  s.fillRoundRect(8,rowY,rowW,26,6,T().surface);
  s.setTextColor(T().text);s.setCursor(14,rowY+8);s.print("Cerah:");
  s.fillRoundRect(58,rowY+9,rowW-90,7,3,T().divider);
  s.fillRoundRect(59,rowY+10,map(brightness,0,255,0,rowW-92),5,2,T().accent);
  char bb[6];sprintf(bb,"%d%%",brightness*100/255);
  s.setTextColor(T().subtext);s.setCursor(rowW-28,rowY+8);s.print(bb);
  
  rowY+=28;
  s.fillRoundRect(8,rowY,rowW,26,6,T().surface);
  s.setTextColor(T().text);s.setCursor(14,rowY+8);s.print("Tema:");
  int tbtnW=(rowW-50)/THEME_COUNT;
  for(int i=0;i<THEME_COUNT;i++){
    uint16_t bg=(i==themeIdx)?T().accent:T().surface2;
    uint16_t fg=(i==themeIdx)?T().bg:T().text;
    int tx=52+i*tbtnW;
    s.fillRoundRect(tx,rowY+2,tbtnW-3,22,4,bg);
    s.setTextColor(fg);s.setTextSize(1);
    int nl=strlen(themes[i].name)*6;
    s.setCursor(tx+(tbtnW-3)/2-nl/2,rowY+7);s.print(themes[i].name);
  }
  
  rowY+=28;
  s.fillRoundRect(8,rowY,rowW,36,6,T().surface);
  s.setTextColor(T().subtext);s.setCursor(14,rowY+4);
  s.print("Daftar WiFi (ketuk utk pilih):");

  if(wifiScanning){
    s.setTextColor(T().accent);s.setCursor(14,rowY+18);
    s.print("Memindai jaringan sekitar...");
  } else if(scannedWifiNum==0){
    s.setTextColor(T().danger);s.setCursor(14,rowY+18);
    s.print("Tidak ada WiFi. Ketuk [Pindai]");
  } else {
    int wifiPillW=(rowW-12)/3;
    for(int i=0;i<3 && i<scannedWifiNum;i++){
      int wx=12+i*(wifiPillW+4);
      bool isSelected = (settSSID == scannedWifis[i].ssid);
      uint16_t pBg = isSelected ? T().accent : T().surface2;
      uint16_t pFg = isSelected ? T().bg : T().text;
      s.fillRoundRect(wx,rowY+16,wifiPillW-2,16,4,pBg);
      s.setTextColor(pFg);s.setCursor(wx+4,rowY+20);
      String dispSSID = scannedWifis[i].ssid;
      if(dispSSID.length()>8) dispSSID = dispSSID.substring(0,7)+".";
      s.print(dispSSID.c_str());
    }
  }

  rowY+=38;
  s.fillRoundRect(8,rowY,rowW,22,4,settFocus==0?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(14,rowY+6);s.print("SSID:");
  s.setTextColor(T().text);s.setCursor(54,rowY+6);
  String sd2=settSSID.length()?settSSID:"(ketuk / pilih di atas)";
  if(sd2.length()>22)sd2=sd2.substring(0,22)+"..";
  s.print(sd2.c_str());
  
  rowY+=24;
  s.fillRoundRect(8,rowY,rowW-28,22,4,settFocus==1?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(14,rowY+6);s.print("Pass:");
  s.setTextColor(T().text);s.setCursor(54,rowY+6);
  if(settPass.length()){
    if(settShowPass)s.print(settPass.substring(0,18).c_str());
    else for(int i=0;i<(int)settPass.length()&&i<18;i++)s.print("*");
  } else s.print("(ketuk)");
  s.fillRoundRect(8+rowW-24,rowY,24,22,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(8+rowW-18,rowY+6);s.print(settShowPass?"H":"S");
  
  rowY+=26;
  int btnW=(rowW-8)/2;
  s.fillRoundRect(8,rowY,btnW,24,6,T().accent);
  s.setTextColor(T().bg);s.setCursor(16,rowY+7);s.print("Sambungkan");
  s.fillRoundRect(SCR_W/2+4,rowY,btnW,24,6,T().surface2);
  s.setTextColor(T().text);s.setCursor(SCR_W/2+10,rowY+7);s.print("Kalibrasi Ulang");

  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}

void settingsTouch(int x,int y,bool held,bool isNew){
  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){kbVisible=false;kbTarget=nullptr;settFocus=-1;}
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(isBack(x,y)){ navBack(); return; }

  if(x>=SCR_W-74 && x<=SCR_W-8 && y>=24 && y<=42){
    if(!wifiScanning){ startWifiScan(); showToast("Memindai..."); }
    return;
  }

  int rowW = SCR_W-16;
  int rowY = STATUS_H+12;

  if(x>=58&&x<=58+rowW-90&&y>=rowY&&y<=rowY+26){
    brightness=constrain(map(x-58,0,rowW-90,10,255),10,255);
    display.setBrightness(brightness);
    needRedraw=true;
    return;
  }

  rowY+=28;
  int tbtnW=(rowW-50)/THEME_COUNT;
  if(y>=rowY&&y<=rowY+26){
    for(int i=0;i<THEME_COUNT;i++){
      int tx=52+i*tbtnW;
      if(x>=tx&&x<=tx+tbtnW-3){
        themeIdx=i;saveTheme();initAppColors();
        showToast("Tema diganti");needRedraw=true;return;
      }
    }
  }

  rowY+=28;
  if(y>=rowY+14 && y<=rowY+34 && !wifiScanning && scannedWifiNum>0){
    int wifiPillW=(rowW-12)/3;
    for(int i=0;i<3 && i<scannedWifiNum;i++){
      int wx=12+i*(wifiPillW+4);
      if(x>=wx && x<=wx+wifiPillW-2){
        settSSID = scannedWifis[i].ssid;
        settPass = "";
        settFocus = 1; kbTarget = &settPass; kbVisible = true; kbMode = KB_LOWER;
        showToast(settSSID.c_str());
        needRedraw=true;
        return;
      }
    }
  }

  rowY+=38;
  if(y>=rowY&&y<=rowY+22){ 
    settFocus=0;kbTarget=&settSSID;kbVisible=true;kbMode=KB_LOWER;
    needRedraw=true;return; 
  }

  rowY+=24;
  if(y>=rowY&&y<=rowY+22){
    if(x>=8+rowW-24){ settShowPass=!settShowPass; needRedraw=true; return; }
    settFocus=1;kbTarget=&settPass;kbVisible=true;kbMode=KB_LOWER;needRedraw=true;return;
  }

  rowY+=26;
  int btnW=(rowW-8)/2;
  if(y>=rowY&&y<=rowY+24){
    if(x>=8&&x<=8+btnW){
      settSSID.toCharArray(WIFI_SSID,64);
      settPass.toCharArray(WIFI_PASSWORD,64);
      saveWifiCreds();connectWifi();
      showToast(wifiConnected?"WiFi Terhubung!":"Gagal Connect");
      needRedraw=true;
      return;
    }
    if(x>=SCR_W/2+4 && x<=SCR_W/2+4+btnW){
      Preferences p;p.begin("touch_cal",false);p.putBool("done",false);p.end();
      ESP.restart();
      return;
    }
  }
}

// =============================================
// APP: NOTEPAD
// =============================================
void notepadEnter(){} void notepadExit(){ saveNote(); }

void drawNotepad(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,26);s.print("Notepad");
  
  s.fillRoundRect(SCR_W-70,24,62,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-56,29);s.print("Hapus");
  
  int bot = kbVisible ? kbY()-4 : backY()-6;
  s.fillRoundRect(4,44,SCR_W-8,bot-44,6,T().surface);
  s.setTextColor(T().text);s.setTextSize(1);s.setTextWrap(true);
  s.setCursor(10,50);s.print((noteText+"|").c_str());
  
  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}

void notepadTouch(int x,int y,bool held,bool isNew){
  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){kbVisible=false;kbTarget=nullptr;saveNote();}
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(x>=SCR_W-70&&x<=SCR_W-4&&y>=24&&y<=42){
    noteText="";saveNote();showToast("Dihapus");
    needRedraw=true;
    return;
  }
  
  if(isBack(x,y)){ navBack(); return; }
  
  int textBot = backY()-6;
  if(y>=44&&y<=textBot){
    kbTarget=&noteText;
    kbVisible=true;
    kbMode=KB_LOWER;
    needRedraw=true;
  }
}

// =============================================
// APP: CANVAS
// =============================================
uint16_t palette[]={0x0000,0xF800,0x07E0,0x001F,0xFFE0,0x07FF,0xF81F,0xFD40,0xFFFF};
#define PAL_N 9
uint16_t drawColor=0xFFFF;
int brushSize=3;
int lastDX=-1,lastDY=-1;

int canvasCapY(){ return STATUS_H+2; }
int canvasToolY(){ return SCR_H-44; }

void canvasEnter(){ lastDX=-1; }
void canvasExit(){ saveCanvas(); }

void drawCanvasScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,14);s.print("Canvas");
  
  canvasApp.pushSprite(&s,0,canvasCapY());
  
  int ty=canvasToolY();
  s.fillRect(0,ty,SCR_W,SCR_H-ty,T().surface);
  s.drawFastHLine(0,ty,SCR_W,T().divider);
  
  int palGap = (SCR_W-8)/PAL_N;
  for(int i=0;i<PAL_N;i++){
    int px=4+i*palGap;
    s.fillCircle(px+palGap/2,ty+10,7,palette[i]);
    if(palette[i]==drawColor) s.drawCircle(px+palGap/2,ty+10,9,T().text);
  }
  
  int r2Y = ty + 22;
  s.fillRoundRect(4,r2Y,56,18,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(10,r2Y+5);s.print("< Back");
  
  s.fillRoundRect(66,r2Y,60,18,4,T().surface2);
  s.setTextColor(T().text);char bs[8];sprintf(bs,"B:%d",brushSize);
  s.setCursor(76,r2Y+5);s.print(bs);
  
  s.fillRoundRect(SCR_W-64,r2Y,60,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-52,r2Y+5);s.print("CLR");
  
  drawToast(s);
}

void canvasTouch(int x,int y,bool held,bool isNew){
  int ty=canvasToolY();
  
  if(y>=ty){
    if(isNew){
      int r2Y = ty + 22;
      if(x>=4 && x<=60 && y>=r2Y){ navBack(); return; }
      
      if(y>=ty && y<r2Y){
        int palGap=(SCR_W-8)/PAL_N;
        for(int i=0;i<PAL_N;i++){
          int px=4+i*palGap;
          if(x>=px && x<px+palGap){ drawColor=palette[i]; lastDX=-1; needRedraw=true; return; }
        }
      }
      
      if(y>=r2Y){
        if(x>=66 && x<=126){ brushSize=(brushSize%8)+1; lastDX=-1; needRedraw=true; return; }
        if(x>=SCR_W-64 && x<=SCR_W-4){ canvasApp.fillSprite(T().bg); lastDX=-1; saveCanvas(); showToast("Dibersihkan"); needRedraw=true; return; }
      }
    }
    return;
  }
  
  int capY=canvasCapY();
  if(y>=capY && y<ty){
    int cy=y-capY;
    if(held&&lastDX>=0){
      canvasApp.drawLine(lastDX,lastDY,x,cy,drawColor);
      for(int t2=1;t2<brushSize;t2++){
        canvasApp.drawLine(lastDX,lastDY+t2,x,cy+t2,drawColor);
        canvasApp.drawLine(lastDX+t2,lastDY,x+t2,cy,drawColor);
      }
    } else canvasApp.fillCircle(x,cy,brushSize/2,drawColor);
    lastDX=x;lastDY=cy;
    needRedraw=true;
  }
}

// =============================================
// APP: AI CHAT
// =============================================
void aiEnter(){} void aiExit(){}

void drawAiChat(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,26);s.print("AI Chat (Gemini)");
  
  bool hasKey = (geminiApiKey.length() > 0 && geminiApiKey != "YOUR_GEMINI_API_KEY_HERE");
  s.fillRoundRect(SCR_W-72,24,68,18,4,hasKey?T().good:T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-66,29);
  s.print(hasKey?"Key: OK":"Key: Edit");

  int contentBot = kbVisible ? kbY()-4 : backY()-32;
  
  s.fillRoundRect(4,44,SCR_W-8,30,6,T().surface);
  s.setTextColor(T().accent);s.setCursor(10,48);s.print("Tanya: ");
  s.setTextColor(T().text);
  String pText = aiPrompt.length() ? aiPrompt : "(ketuk area bawah utk ketik)";
  if(pText.length()>32) pText = pText.substring(0,32) + "..";
  s.print(pText.c_str());

  int respH = contentBot - 80;
  if(respH > 20){
    s.fillRoundRect(4,78,SCR_W-8,respH,6,T().surface2);
    s.setTextColor(T().accent2);s.setCursor(10,84);s.print("Gemini: ");
    s.setTextColor(T().text);s.setTextWrap(true);
    if(aiLoading){
      s.setTextColor(T().good);
      s.print("Sedang berpikir & menghubungi Gemini API...");
    } else if(aiResponse.length()){
      String rDisp = aiResponse;
      if(rDisp.length() > 180) rDisp = rDisp.substring(0,180) + "...";
      s.print(rDisp.c_str());
    } else {
      s.setTextColor(T().subtext);
      s.print("Ketik pertanyaan lalu tekan [Kirim].");
    }
  }

  if(!kbVisible){
    int inputY = backY() - 26;
    s.fillRoundRect(4,inputY,SCR_W-64,22,4,T().surface);
    s.setTextColor(T().subtext);s.setCursor(10,inputY+6);
    String ipDisp = aiPrompt.length() ? aiPrompt : "Ketik pertanyaan...";
    if(ipDisp.length()>24) ipDisp = ipDisp.substring(0,24)+"..";
    s.print(ipDisp.c_str());
    
    s.fillRoundRect(SCR_W-56,inputY,52,22,4,aiLoading?T().surface2:T().accent);
    s.setTextColor(aiLoading?T().subtext:T().bg);s.setCursor(SCR_W-48,inputY+6);
    s.print("Kirim");
  }

  if(kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

void aiTouch(int x,int y,bool held,bool isNew){
  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){ kbVisible=false; kbTarget=nullptr; }
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(isBack(x,y)){ navBack(); return; }

  if(x>=SCR_W-72 && x<=SCR_W-4 && y>=24 && y<=42){
    kbTarget = &geminiApiKey;
    kbVisible = true;
    kbMode = KB_LOWER;
    showToast("Edit API Key");
    needRedraw = true;
    return;
  }

  int inputY = backY() - 26;
  
  if((y>=44 && y<=74) || (y>=inputY && y<=inputY+22 && x<=SCR_W-60)){
    kbTarget = &aiPrompt;
    kbVisible = true;
    kbMode = KB_LOWER;
    needRedraw = true;
    return;
  }

  if(x>=SCR_W-56 && x<=SCR_W-4 && y>=inputY && y<=inputY+22){
    saveGeminiKey();
    triggerGeminiAI();
    return;
  }
}

// =============================================
// APP: FILE EXPLORER (DENGAN WEB UPLOADER)
// =============================================
#define MAX_EXP_FILES 20
String expFileNames[MAX_EXP_FILES];
uint32_t expFileSizes[MAX_EXP_FILES];
int expFileCount = 0;
int expScrollPage = 0;
String expViewContent = "";
String expViewFileName = "";

void scanSdFiles() {
  expFileCount = 0;
  if (!sdReady) return;
  File root = SD_MMC.open("/");
  File f = root.openNextFile();
  while (f && expFileCount < MAX_EXP_FILES) {
    if (!f.isDirectory()) {
      String fname = String(f.name());
      if (!fname.startsWith("/")) fname = "/" + fname;
      expFileNames[expFileCount] = fname;
      expFileSizes[expFileCount] = f.size();
      expFileCount++;
    }
    f = root.openNextFile();
  }
}

void fileExpEnter(){
  expViewContent = "";
  expViewFileName = "";
  expScrollPage = 0;
  scanSdFiles();
}
void fileExpExit(){}

void drawFileExplorer(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(0x3ADF);s.setTextSize(1);s.setCursor(8,26);s.print("File Explorer");

  if(wifiConnected && webServerRunning){
    s.setTextColor(T().good);s.setCursor(110,26);
    String ipStr = "Web: " + WiFi.localIP().toString();
    s.print(ipStr.c_str());
  } else {
    s.setTextColor(T().subtext);s.setCursor(120,26);
    s.print("Web: Off (No WiFi)");
  }

  if(expViewFileName.length() > 0){
    s.fillRoundRect(SCR_W-64,24,58,18,4,T().surface2);
    s.setTextColor(T().accent);s.setCursor(SCR_W-54,29);s.print("Tutup");

    s.fillRoundRect(4,44,SCR_W-8,backY()-50,6,T().surface);
    s.setTextColor(T().accent2);s.setCursor(10,50);s.print("File: ");
    s.setTextColor(T().text);s.print(expViewFileName.c_str());
    s.drawFastHLine(10,64,SCR_W-20,T().divider);

    s.setTextColor(T().text);s.setTextWrap(true);s.setCursor(10,70);
    String vDisp = expViewContent;
    if(vDisp.length() > 220) vDisp = vDisp.substring(0,220) + "...";
    s.print(vDisp.c_str());

    drawBack(s);drawToast(s);
    return;
  }

  int listY = 44;
  int itemH = 26;
  int itemsPerPage = 5;
  int startIdx = expScrollPage * itemsPerPage;

  if(!sdReady){
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);
    s.print("SD Card Tdk Siap");
    drawBack(s);drawToast(s);
    return;
  }

  if(expFileCount == 0){
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(20,80);
    s.print("SD Card Kosong / Belum ada file.");
  } else {
    for(int i = 0; i < itemsPerPage && (startIdx + i) < expFileCount; i++){
      int idx = startIdx + i;
      int itemY = listY + i * (itemH + 4);
      s.fillRoundRect(4,itemY,SCR_W-8,itemH,6,T().surface);
      
      s.setTextColor(T().accent2);s.setCursor(10,itemY+8);s.print("[F]");
      s.setTextColor(T().text);s.setCursor(32,itemY+8);
      String fn = expFileNames[idx];
      if(fn.length() > 16) fn = fn.substring(0,14) + "..";
      s.print(fn.c_str());

      s.fillRoundRect(SCR_W-84,itemY+3,36,20,4,T().surface2);
      s.setTextColor(T().good);s.setCursor(SCR_W-76,itemY+8);s.print("Buka");

      s.fillRoundRect(SCR_W-44,itemY+3,36,20,4,T().danger);
      s.setTextColor(0xFFFF);s.setCursor(SCR_W-38,itemY+8);s.print("Hps");
    }
  }

  int pageY = backY() - 2;
  if(expScrollPage > 0){
    s.fillRoundRect(SCR_W-120,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-110,pageY+6);s.print("< Prev");
  }
  if((expScrollPage + 1) * itemsPerPage < expFileCount){
    s.fillRoundRect(SCR_W-60,pageY,54,22,4,T().surface2);
    s.setTextColor(T().text);s.setCursor(SCR_W-52,pageY+6);s.print("Next >");
  }

  drawBack(s);drawToast(s);
}

void fileExpTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;

  if(isBack(x,y)){ navBack(); return; }

  if(expViewFileName.length() > 0){
    if(x>=SCR_W-64 && x<=SCR_W-4 && y>=24 && y<=42){
      expViewFileName = "";
      expViewContent = "";
      needRedraw = true;
    }
    return;
  }

  if(!sdReady) return;

  int listY = 44;
  int itemH = 26;
  int itemsPerPage = 5;
  int startIdx = expScrollPage * itemsPerPage;

  for(int i = 0; i < itemsPerPage && (startIdx + i) < expFileCount; i++){
    int idx = startIdx + i;
    int itemY = listY + i * (itemH + 4);
    if(y >= itemY && y <= itemY + itemH){
      if(x >= SCR_W-84 && x <= SCR_W-48){
        expViewFileName = expFileNames[idx];
        File f = SD_MMC.open(expViewFileName, FILE_READ);
        if(f){ expViewContent = f.readString(); f.close(); }
        else expViewContent = "Gagal membaca isi file.";
        needRedraw = true;
        return;
      }
      if(x >= SCR_W-44 && x <= SCR_W-8){
        SD_MMC.remove(expFileNames[idx]);
        showToast("File Dihapus");
        scanSdFiles();
        needRedraw = true;
        return;
      }
    }
  }

  int pageY = backY() - 2;
  if(expScrollPage > 0 && x>=SCR_W-120 && x<=SCR_W-66 && y>=pageY){
    expScrollPage--; needRedraw = true; return;
  }
  if((expScrollPage + 1) * itemsPerPage < expFileCount && x>=SCR_W-60 && x<=SCR_W-6 && y>=pageY){
    expScrollPage++; needRedraw = true; return;
  }
}

// =============================================
// PUSH FRAME
// =============================================
void push(){ canvas.pushSprite(0,0); }
void renderCurrentFrame(){
  if(locked){
    drawLockScreen(canvas);
  } else if(curScreen()==SCR_HOME){
    drawHome(canvas,homeScrollY);
  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0) apps[idx].draw(canvas);
  }
  if(!locked && controlCenterOpen) drawControlCenter(canvas);
}

// =============================================
// SETUP
// =============================================
bool wasTouched=false;
int  touchStartX=0,touchStartY=0,touchLastY=0;
bool isSwiping=false;
unsigned long swipeStartTime=0;
int gStartX=0,gStartY=0; bool gGestureDone=false;

void setup(){
  Serial.begin(115200);
  display.init();
  display.setRotation(1);
  display.setBrightness(brightness);
  canvas.setPsram(true);
  canvas.createSprite(320,240);
  canvas.fillSprite(0x1084);
  canvas.setTextColor(0xFD40);canvas.setTextSize(3);
  canvas.setCursor(50,80);canvas.print("ESP Phone");
  canvas.setTextColor(0x8C51);canvas.setTextSize(1);
  canvas.setCursor(60,130);canvas.print("Powered by ESP32-S3 + LGFX");
  push();delay(1200);
  if(display.touch())loadOrRunCalibration();
  loadTheme();
  initAppColors();
  drawColor=T().text;
  initSD();
  loadWifiCreds();
  connectWifi();
  loadNote();
  loadGeminiKey();
  canvasApp.setPsram(true);
  canvasApp.createSprite(320,240-STATUS_H-44);
  canvasApp.fillSprite(T().bg);
  Orientation savedOrient = loadOrientPref();
  applyOrientation(savedOrient, false);
  locked = true;
  needRedraw = true;
}

// =============================================
// LOOP
// =============================================
unsigned long lastClk=0,lastSen=0;
void loop(){
  if(wifiConnected && webServerRunning){
    webServer.handleClient();
  }

  lgfx::touch_point_t tp;
  bool touched=display.getTouch(&tp);
  int tx=touched?(int)tp.x:0,ty=touched?(int)tp.y:0;
  bool newT=touched&&!wasTouched;

  if(ccAnimatingOpen){
    ccOffset += (float)ccPanelH()/6.0f;
    if(ccOffset>=ccPanelH()){ ccOffset=ccPanelH(); ccAnimatingOpen=false; }
    needRedraw=true;
  } else if(ccAnimatingClose){
    ccOffset -= (float)ccPanelH()/6.0f;
    if(ccOffset<=0){ ccOffset=0; ccAnimatingClose=false; controlCenterOpen=false; }
    needRedraw=true;
  }

  if(locked){
    lockScreenInput(touched,newT,tx,ty);
    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }
    wasTouched=touched; delay(8); return;
  }

  if(controlCenterOpen){
    if(newT) ccTouch(tx,ty);
    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }
    wasTouched=touched; delay(8); return;
  }

  if(newT){ gStartX=tx; gStartY=ty; gGestureDone=false; }
  if(touched && !gGestureDone && !kbVisible){
    int dy=ty-gStartY, dx=tx-gStartX;
    if(gStartY<STATUS_H+6 && dy>28 && abs(dy)>abs(dx)){
      openControlCenter(); gGestureDone=true;
      wasTouched=touched; delay(8); return;
    }
    if(gStartY>SCR_H-16 && dy<-28 && abs(dy)>abs(dx) && curScreen()!=SCR_HOME){
      navGoHome(); gGestureDone=true;
      wasTouched=touched; delay(8); return;
    }
  }

  if(curScreen()==SCR_HOME){
    if(touched){
      if(!wasTouched){
        touchStartX=tx;touchStartY=ty;touchLastY=ty;
        isSwiping=false;swipeStartTime=millis();
      } else {
        if(abs(ty-touchStartY)>12)isSwiping=true;
        if(isSwiping){
          homeScrollVel=(touchLastY-ty)*0.8f;
          homeScrollY+=touchLastY-ty;
          homeScrollY=constrain(homeScrollY,0.0f,(float)homeMaxScroll());
          needRedraw=true;
        }
        touchLastY=ty;
      }
    } else if(wasTouched){
      if(!isSwiping&&millis()-swipeStartTime<400){
        Screen nx=homeCheck(touchStartX,touchStartY,homeScrollY);
        if(nx!=SCR_HOME) navPush(nx);
      }
    } else {
      if(fabsf(homeScrollVel)>0.5f){
        homeScrollY+=homeScrollVel;homeScrollVel*=0.85f;
        homeScrollY=constrain(homeScrollY,0.0f,(float)homeMaxScroll());
        needRedraw=true;
      }
    }
    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}

  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0){
      bool held = touched&&wasTouched;
      if(curScreen()==SCR_CANVAS){
        if(touched){ apps[idx].touch(tx,ty,held,newT); }
      } else {
        if(newT) apps[idx].touch(tx,ty,false,true);
        else if(touched && held) apps[idx].touch(tx,ty,true,false);
      }
    }
    if(curScreen()==SCR_CLOCK && millis()-lastClk>500){ needRedraw=true; lastClk=millis(); }
    if(curScreen()==SCR_SENSOR && millis()-lastSen>1000){ needRedraw=true; lastSen=millis(); }
    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}
  }
  wasTouched=touched;
  delay(8);
}
