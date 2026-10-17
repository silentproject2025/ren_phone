// =====================================================================
// gamepad_renphone.ino -- HP LAIN JADI GAMEPAD (WebSocket) untuk NES & DOOM
//
// Cara pakai:
//   1. Nyalakan AP di app "Remote" (atau izinkan lewat WiFi rumah).
//   2. Di HP lain, sambung ke WiFi Accretion, buka  http://192.168.4.1/pad
//   3. Mulai game NES / DOOM di HP Accretion -> tombol di halaman itu
//      langsung jadi controller-nya.
//
// Arsitektur (sengaja kecil):
//   * Halaman web  : GET /pad  (atau /g)  -> dilayani WebServer yg sudah ada
//                    (dijaga rcAllowed() sama persis dgn halaman /remote).
//   * Transport    : WebSocket di port 81 (lib "WebSockets" by Markus
//                    Sattler). Browser kirim 1 BYTE bitmask tiap tombol
//                    berubah (+ diulang tiap 100 ms selagi ada tombol
//                    ditekan, sbg heartbeat).
//   * Bitmask      : sama persis dgn NES:  UP=1 DOWN=2 LEFT=4 RIGHT=8
//                    A=16 B=32 START=64 SELECT=128.
//                    DOOM: bit yg sama = DBTN_UP..DBTN_SELECT (urutan
//                    enum-nya identik), jadi A=FIRE, B=USE, START=ENTER,
//                    SELECT=ESC.
//   * Konsumen     : nes_input_renphone.ino (osd_getinput) & doom_renphone.ino
//                    (RenPhoneDoomInput::update) tinggal OR-kan rcPadMask
//                    dgn state sentuh fisik -> sentuhan layar & gamepad
//                    bisa dipakai bareng.
//   * DOOM LENGKAP : paket biner lain (panjang >=2, byte pertama = tipe):
//                      0x01 + 4 byte  = mask tombol EXTRA 31-bit (strafe, run, alt,
//                                       senjata 1-7, MAP, Y/N, BS, PAUSE, F1-F11, -, =,
//                                       senjata prev/next)
//                      0x02 + int16   = geser-lihat (delta X, ala mouse)
//                      0x03 + ASCII   = ketik teks (nama save, cheat, dst)
//                    Dibaca engine lewat 3 hook di DoomGlue (padExtKeys,
//                    padTakeMouseDX, padTakeTyped) -- lihat apply_doom_full_patch.py.
//   * Anti macet   : klien putus / diam >700 ms / ganti game -> mask
//                    direset ke 0 (tombol gak bakal "nyangkut").
//   * Multi klien  : maks 5 koneksi, maskanya di-OR (dua HP = satu pad).
//
// DIPASANG lewat apply_gamepad_patch.py (idempoten). Butuh library
// "WebSockets" (arduino-cli lib install "WebSockets").
// =====================================================================

#include <WebSocketsServer.h>

// ---- didefinisikan di file lain ----
bool rcAllowedPublic();     // remote_renphone.ino: klien HTTP ini boleh? (AP / WiFi rumah diizinkan)
bool rcLanAllowedPublic();  // remote_renphone.ino: opsi "Izinkan lewat WiFi rumah"
bool rcApIsUp();            // remote_renphone.ino
void padRegisterRoutes();
void padLoopPoll();

// Dibaca oleh task NES / DOOM (core lain) -> volatile; 1 byte = atomik.
volatile uint8_t rcPadMask = 0;
volatile uint32_t rcPadExt = 0;          // tombol EXTRA DOOM (OR semua klien)
static volatile int32_t padMouseDx = 0;  // akumulasi geser-lihat, dikonsumsi task DOOM
#define PAD_TQ 256
static volatile uint8_t padTq[PAD_TQ];   // ring teks ketikan (producer: loop(), consumer: task DOOM)
static volatile uint16_t padTh = 0, padTt = 0;

#define PAD_MAXC       5
#define PAD_STALE_MS   700UL    // mask dianggap basi kalau klien diam selama ini

static WebSocketsServer padWs(81);
static bool     padStarted = false;
static uint8_t  padCm[PAD_MAXC]  = {0};   // mask terakhir per klien
static uint32_t padCt[PAD_MAXC]  = {0};   // waktu paket terakhir per klien
static uint32_t padCe[PAD_MAXC]  = {0};   // mask EXTRA terakhir per klien
static uint32_t padCte[PAD_MAXC] = {0};   // waktu paket EXTRA terakhir per klien
static uint8_t  padGame    = 255;         // 0 none, 1 NES main, 2 DOOM main, 3 di app tapi belum main
static uint32_t padGameMs  = 0;

static void padRecalc() {
  uint8_t m = 0; uint32_t e = 0;
  for (int i = 0; i < PAD_MAXC; i++) { m |= padCm[i]; e |= padCe[i]; }
  rcPadMask = m;
  rcPadExt  = e;
}

// ---- 3 hook yg dipanggil engine DOOM (task core 1) lewat DoomGlue_SetHooks() ----
uint32_t padExtKeys() { return rcPadExt; }
int32_t padTakeMouseDX() {
  int32_t v = __atomic_exchange_n(&padMouseDx, 0, __ATOMIC_ACQ_REL);
  if (v >  600) v =  600;   // batasi putaran per tic (600*8 = ~26 derajat) biar gak 'nyentak'
  if (v < -600) v = -600;
  return v;
}
int padTakeTyped() {
  uint16_t h = padTh;
  if (h == padTt) return 0;
  int c = padTq[h];
  padTh = (h + 1) % PAD_TQ;
  return c;
}
static void padTypePush(uint8_t c) {
  uint16_t t = padTt, n = (t + 1) % PAD_TQ;
  if (n == padTh) return;   // penuh -> buang
  padTq[t] = c; padTt = n;
}
static void padMouseAdd(int32_t d) {
  __atomic_fetch_add(&padMouseDx, d, __ATOMIC_ACQ_REL);
}

static uint8_t padGameNow() {
  auto s = curScreen();
  if (s == SCR_NES)  return nesIsPlaying()  ? 1 : 3;
  if (s == SCR_DOOM) return doomIsPlaying() ? 2 : 3;
  return 0;
}

// Klien boleh? Subnet AP kita (mis. 192.168.4.x) selalu boleh selama AP
// hidup; selain itu cuma kalau "Izinkan lewat WiFi rumah" dinyalakan.
// (WebSocket di port 81 gak lewat rcAllowed(), jadi dicek lewat IP klien.)
static bool padIpOk(IPAddress ip) {
  if (rcApIsUp()) {
    IPAddress ap = WiFi.softAPIP();
    if (ip[0] == ap[0] && ip[1] == ap[1] && ip[2] == ap[2]) return true;
  }
  return rcLanAllowedPublic();
}

// CATATAN: handler event sengaja berupa LAMBDA di dalam padWsStart(), bukan fungsi
// bernama. Arduino membuat prototipe otomatis utk fungsi bernama dan menaruhnya
// di ATAS file (sebelum #include <WebSocketsServer.h>), sehingga tipe WStype_t
// belum dikenal -> "WStype_t has not been declared". Lambda tidak diprototipe.
static void padWsStart() {
  if (padStarted) padWs.close();                 // dipanggil ulang kalau web server restart
  padWs.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (num >= PAD_MAXC) return;
  switch (type) {
    case WStype_CONNECTED: {
      if (!padIpOk(padWs.remoteIP(num))) { padWs.disconnect(num); return; }
      padCm[num] = 0; padCt[num] = millis(); padCe[num] = 0; padCte[num] = millis();
      char b[4]; snprintf(b, sizeof(b), "g%u", (unsigned)padGameNow());
      padWs.sendTXT(num, b);                     // kasih tau game aktif -> label tombol
      Serial.printf("[pad] klien %u tersambung\n", (unsigned)num);
      break;
    }
    case WStype_DISCONNECTED:
      padCm[num] = 0; padCe[num] = 0; padRecalc();
      Serial.printf("[pad] klien %u putus\n", (unsigned)num);
      break;
    case WStype_BIN:
      if (length == 1) {                                   // paket lama: mask 8 tombol
        padCm[num] = payload[0]; padCt[num] = millis(); padRecalc();
      } else if (length >= 2) {                            // paket DOOM lengkap
        uint8_t t = payload[0];
        if (t == 1 && length >= 5) {                       // mask EXTRA 32-bit (little endian)
          padCe[num] = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                       ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
          padCte[num] = millis(); padRecalc();
        } else if (t == 2 && length >= 3) {                // geser-lihat (int16 LE)
          if (padGame == 2) padMouseAdd((int16_t)((uint16_t)payload[1] | ((uint16_t)payload[2] << 8)));
        } else if (t == 3) {                               // ketik teks
          if (padGame == 2)
            for (size_t i = 1; i < length; i++) {
              uint8_t c = payload[i];
              if ((c >= 32 && c < 127) || c == 13 || c == 127) padTypePush(c);
            }
        }
      }
      break;
    default:
      break;
  }
  });
  padWs.begin();
  padWs.enableHeartbeat(3000, 2000, 2);          // ping 3 dtk; 2x gagal -> putus -> mask 0
  padStarted = true;
  Serial.println("[pad] WebSocket gamepad siap di port 81");
}

// Dipanggil dari rcLoopPoll() tiap putaran loop().
void padLoopPoll() {
  if (!padStarted) return;
  padWs.loop();

  uint32_t now = millis();
  bool ch = false;
  for (int i = 0; i < PAD_MAXC; i++) {
    if (padCm[i] && now - padCt[i]  > PAD_STALE_MS) { padCm[i] = 0; ch = true; }
    if (padCe[i] && now - padCte[i] > PAD_STALE_MS) { padCe[i] = 0; ch = true; }
  }
  if (ch) padRecalc();

  if (now - padGameMs > 400UL) {                 // game aktif berubah -> reset + kabari halaman
    padGameMs = now;
    uint8_t g = padGameNow();
    if (g != padGame) {
      padGame = g;
      for (int i = 0; i < PAD_MAXC; i++) { padCm[i] = 0; padCe[i] = 0; }
      padRecalc();
      __atomic_store_n(&padMouseDx, 0, __ATOMIC_RELEASE);
      padTh = padTt;                                 // buang ketikan yg belum terkirim
      char b[4]; snprintf(b, sizeof(b), "g%u", (unsigned)g);
      padWs.broadcastTXT(b);
    }
  }
}

// =====================================================================
// HALAMAN WEB  (GET /pad atau /g)
// =====================================================================
static const char PAD_PAGE[] PROGMEM = R"PADPAGE(<!doctype html><html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="theme-color" content="#07080c"><title>Accretion Gamepad</title>
<style>
:root{--ac:#4aa8ff;--bg:#07080c;--fg:#cfd6e4;--dim:#5b6475;--key:#171b24;--ln:#2a3142}
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:var(--bg);color:var(--fg);font:600 14px system-ui,-apple-system,sans-serif;overflow:hidden;touch-action:none;overscroll-behavior:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;-webkit-tap-highlight-color:transparent}
input,select{-webkit-user-select:text;user-select:text}
body.g1{--ac:#e63946}body.g2{--ac:#ff8a1f}
#bar{position:fixed;top:calc(env(safe-area-inset-top,0px) + 8px);left:0;right:0;display:flex;align-items:center;gap:8px;padding:0 max(16px,env(safe-area-inset-left,0px));font-size:13px;color:var(--dim);z-index:4}
#dot{width:9px;height:9px;border-radius:50%;background:#c0392b;flex:none}
#dot.ok{background:#2ecc71}#dot.wait{background:#f1c40f}
#st{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#fs,#mn,#xcl,.btn{background:var(--key);color:var(--fg);border:1px solid var(--ln);border-radius:8px;padding:6px 10px;font:inherit;font-size:12px}
#mn{display:none}body.g2 #mn{display:block}
#pad{position:fixed;inset:0;display:flex;align-items:center;justify-content:space-between;padding:calc(env(safe-area-inset-top,0px) + 28px) max(5vw,env(safe-area-inset-right,0px)) 10px max(5vw,env(safe-area-inset-left,0px))}
#dp{position:relative;width:46vmin;height:46vmin;border-radius:50%;background:radial-gradient(circle,#1d2230,#10131b 70%);border:2px solid #232a3a;flex:none}
.ar{position:absolute;color:#3b4458;font-size:6vmin;line-height:1}
.ar.on{color:var(--ac)}
#au{top:7%;left:50%;transform:translateX(-50%)}#ad{bottom:7%;left:50%;transform:translateX(-50%)}
#al{left:7%;top:50%;transform:translateY(-50%)}#ar{right:7%;top:50%;transform:translateY(-50%)}
#nub{position:absolute;left:50%;top:50%;width:32%;height:32%;margin:-16%;border-radius:50%;background:#262d3e;border:2px solid #33405a}
#ab{position:relative;width:46vmin;height:36vmin;flex:none}
.rnd{position:absolute;width:21vmin;height:21vmin;border-radius:50%;background:var(--key);border:2px solid var(--ln);display:flex;align-items:center;justify-content:center;font-size:4vmin;font-weight:800}
.rnd.on{background:var(--ac);border-color:var(--ac);color:#fff;transform:scale(.94)}
body.g2 #bA{width:25vmin;height:25vmin;font-size:4.4vmin}
#bB{left:0;top:15vmin}#bA{right:0;top:0}
#mid{position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom,0px) + 6vmin);transform:translateX(-50%);display:flex;gap:4vmin}
.pill{width:18vmin;height:7.5vmin;border-radius:99px;background:var(--key);border:2px solid var(--ln);display:flex;align-items:center;justify-content:center;font-size:2.8vmin;letter-spacing:.08em;color:var(--dim)}
.pill.on{background:var(--ac);border-color:var(--ac);color:#fff}
.dm{display:none}body.g2 .dm{display:flex}
#bR{position:absolute;left:0;top:1vmin;width:21vmin;height:9.5vmin}
.k{display:flex;align-items:center;justify-content:center;background:var(--key);border:2px solid var(--ln);border-radius:10px;font-size:max(11px,2.6vmin);color:var(--fg);padding:1vmin 1.4vmin;min-width:9vmin;min-height:8vmin;text-align:center;line-height:1.1}
.k.on{background:var(--ac);border-color:var(--ac);color:#fff}
#wp{position:fixed;left:50%;transform:translateX(-50%);top:calc(env(safe-area-inset-top,0px) + 30px);gap:1.2vmin;z-index:3}
#wp .k{min-width:8.5vmin;min-height:8.5vmin;padding:0;font-weight:800}
#pn{position:fixed;left:4vw;right:4vw;top:calc(env(safe-area-inset-top,0px) + 30px);bottom:2vh;display:none;z-index:6;background:rgba(7,8,12,.95);border:1px solid var(--ln);border-radius:14px;padding:12px;overflow:auto;touch-action:pan-y;-webkit-overflow-scrolling:touch}
#pn.open{display:block}
#pn h4{margin:10px 0 6px;font-size:12px;color:var(--dim);font-weight:700;letter-spacing:.06em}
#pn h4:first-child{margin-top:0}
.gr{display:flex;flex-wrap:wrap;gap:6px}
.gr .k{min-width:12vmin;min-height:9vmin;padding:0 10px}
.row{display:flex;align-items:center;gap:10px;margin:6px 0;font-size:13px}
.row input[type=range]{flex:1}
.row input[type=text]{flex:1;background:var(--key);color:var(--fg);border:1px solid var(--ln);border-radius:8px;padding:8px;font:inherit;min-width:0}
#rot{display:none;position:fixed;inset:0;background:var(--bg);z-index:9;align-items:center;justify-content:center;flex-direction:column;gap:12px;text-align:center;font-size:18px}
@media (orientation:portrait){#rot{display:flex}}
</style></head><body>
<div id="bar"><span id="dot"></span><span id="st">menyambung…</span><button id="mn" class="ui">☰ Tombol</button><button id="fs" class="ui" hidden>⛶</button></div>
<div id="wp" class="dm"></div>
<div id="pad">
  <div id="dp"><span class="ar" id="au">▲</span><span class="ar" id="ad">▼</span><span class="ar" id="al">◀</span><span class="ar" id="ar">▶</span><div id="nub"></div></div>
  <div id="mid"><div class="pill" data-b="SELECT" id="lS">SELECT</div><div class="pill" data-b="START" id="lT">START</div></div>
  <div id="ab"><div class="rnd" data-b="B" id="bB">B</div><div class="rnd" data-b="A" id="bA">A</div><div class="pill tg dm" data-b="RUN" id="bR">RUN</div></div>
</div>
<div id="pn">
  <div class="row"><b style="flex:1">Kontrol DOOM</b><button id="xcl" class="ui">Tutup ✕</button></div>
  <h4>MENU &amp; NAVIGASI</h4><div class="gr" id="g1"></div>
  <h4>GERAK TAMBAHAN</h4><div class="gr" id="g2"></div>
  <h4>F-KEYS (BAWAAN DOOM)</h4><div class="gr" id="g3"></div>
  <h4>PENGATURAN LIHAT</h4>
  <div class="row ui"><span>Kecepatan geser</span><input type="range" id="sens" min="1" max="14" step="0.5" value="4"><span id="sv">4</span></div>
  <div class="row ui"><label><input type="checkbox" id="inv"> Balik arah geser</label><label><input type="checkbox" id="stk" checked> Stick kiri/kanan = strafe (mati = putar)</label></div>
  <h4>KETIK (NAMA SAVE, CHEAT, DLL)</h4>
  <div class="row ui"><input type="text" id="tx" maxlength="60" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="mis. iddqd"><button class="btn" id="tsend">Kirim</button><button class="btn" id="tent">Kirim + Enter</button></div>
</div>
<div id="rot"><div style="font-size:52px">⟳</div>Putar HP ke mode landscape</div>
<script>
const $=s=>document.querySelector(s);
const BIT={UP:1,DOWN:2,LEFT:4,RIGHT:8,A:16,B:32,START:64,SELECT:128};
const EXN=['SL','SR','RUN','ALT','W1','W2','W3','W4','W5','W6','W7','MAP','Y','N','BS','PAUSE','F1','F2','F3','F4','F5','F6','F7','F8','F9','F10','F11','MINUS','EQ','PREV','NEXT'];
const EX={};EXN.forEach((n,i)=>EX[n]=1<<i);
const L={0:['A','B','START','SELECT','Buka app NES atau DOOM di HP Accretion'],
         1:['A','B','START','SELECT','NES'],
         2:['FIRE','USE','ENTER','ESC','DOOM \u00b7 geser area kosong = lihat sekeliling'],
         3:['A','B','START','SELECT','Pilih game dulu di layar HP']};
const dp=$('#dp'),nub=$('#nub'),pn=$('#pn');
const btns=[...document.querySelectorAll('[data-b]:not(.tg)')],tgs=[...document.querySelectorAll('.tg')];
function mk(par,name,label){const d=document.createElement('div');d.className='k';d.dataset.b=name;d.textContent=label;par.appendChild(d);btns.push(d)}
for(let i=1;i<=7;i++)mk($('#wp'),'W'+i,i);
mk($('#wp'),'PREV','\u25c0');mk($('#wp'),'NEXT','\u25b6');
[['UP','\u25b2'],['DOWN','\u25bc'],['LEFT','\u25c0'],['RIGHT','\u25b6'],['START','ENTER'],['SELECT','ESC'],['MAP','MAP'],['PAUSE','PAUSE'],['Y','Y'],['N','N'],['BS','\u232b']].forEach(a=>mk($('#g1'),a[0],a[1]));
[['SL','\u25c1 Strafe'],['SR','Strafe \u25b7'],['ALT','ALT'],['MINUS','\u2212 (kecil)'],['EQ','+ (besar)']].forEach(a=>mk($('#g2'),a[0],a[1]));
[['F1','F1 Bantuan'],['F2','F2 Simpan'],['F3','F3 Muat'],['F4','F4 Suara'],['F5','F5 Detail'],['F6','F6 QSave'],['F7','F7 Akhiri'],['F8','F8 Pesan'],['F9','F9 QLoad'],['F10','F10 Keluar'],['F11','F11 Gamma']].forEach(a=>mk($('#g3'),a[0],a[1]));
let ws=null,mask=0,exm=0,last=-1,lastE=-1,game=0,conn='wait',rt=0,tg=0,lookAcc=0;
const ptr=new Map();
let sens=4,inv=false,stk=true;
try{const s=JSON.parse(localStorage.getItem('padcfg')||'{}');if(s.sens)sens=s.sens;inv=!!s.inv;if(s.stk===false)stk=false}catch(e){}
$('#sens').value=sens;$('#sv').textContent=sens;$('#inv').checked=inv;$('#stk').checked=stk;
function saveCfg(){try{localStorage.setItem('padcfg',JSON.stringify({sens,inv,stk}))}catch(e){}}
$('#sens').oninput=e=>{sens=+e.target.value;$('#sv').textContent=sens;saveCfg()};
$('#inv').onchange=e=>{inv=e.target.checked;saveCfg()};
$('#stk').onchange=e=>{stk=e.target.checked;saveCfg()};
function setPn(o){pn.classList.toggle('open',o);if(!o){try{document.activeElement.blur()}catch(e){}}release()}
$('#mn').onclick=()=>setPn(!pn.classList.contains('open'));
$('#xcl').onclick=()=>setPn(false);
const txq=[];let txt=0;
function typeSend(enter){
  const v=$('#tx').value;for(const ch of v){const c=ch.charCodeAt(0);if(c>=32&&c<127)txq.push(c)}
  if(enter)txq.push(13);
  if(v)$('#tx').value='';
  if(!txt)txt=setInterval(()=>{
    if(!txq.length||!ws||ws.readyState!==1){if(!txq.length){clearInterval(txt);txt=0}return}
    ws.send(Uint8Array.from([3].concat(txq.splice(0,6))));
  },90);
}
$('#tsend').onclick=()=>typeSend(false);$('#tent').onclick=()=>typeSend(true);
$('#tx').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();typeSend(true)}});
const vis=b=>b.getClientRects().length>0;
function paint(){
  const l=L[game]||L[0];
  $('#bA').textContent=l[0];$('#bB').textContent=l[1];$('#lT').textContent=l[2];$('#lS').textContent=l[3];
  document.body.className='g'+game;
  if(game!==2&&pn.classList.contains('open'))pn.classList.remove('open');
  $('#dot').className=conn;
  $('#st').textContent=conn==='ok'?l[4]:conn==='wait'?'Menyambung\u2026':'Terputus \u2014 mencoba lagi\u2026';
}
function open(){
  clearTimeout(rt);conn='wait';paint();
  try{ws=new WebSocket('ws://'+location.hostname+':81/')}catch(e){rt=setTimeout(open,1000);return}
  ws.binaryType='arraybuffer';
  ws.onopen=()=>{conn='ok';last=-1;lastE=-1;paint();send(true);sendEx(true)};
  ws.onclose=()=>{conn='off';paint();clearTimeout(rt);rt=setTimeout(open,1000)};
  ws.onerror=()=>{try{ws.close()}catch(e){}};
  ws.onmessage=e=>{if(typeof e.data==='string'&&e.data[0]==='g'){game=+e.data[1]||0;tg=0;tgs.forEach(t=>t.classList.remove('on'));paint()}};
}
function send(force){
  if(!ws||ws.readyState!==1)return;
  if(mask!==last||force){ws.send(Uint8Array.of(mask));last=mask}
}
function sendEx(force){
  if(!ws||ws.readyState!==1||game!==2&&!force)return;
  if(exm!==lastE||force){const e=exm>>>0;ws.send(Uint8Array.of(1,e&255,(e>>>8)&255,(e>>>16)&255,(e>>>24)&255));lastE=exm}
}
function hit(b,x,y){
  const r=b.getBoundingClientRect();
  if(b.classList.contains('rnd')){
    const R=r.width/2,cx=r.left+R,cy=r.top+R,dx=x-cx,dy=y-cy,Rg=R*1.2;
    return dx*dx+dy*dy<=Rg*Rg;
  }
  const m=b.classList.contains('k')?0:8;return x>=r.left-m&&x<=r.right+m&&y>=r.top-m&&y<=r.bottom+m;
}
function btnAt(x,y){for(const b of btns)if(vis(b)&&hit(b,x,y))return b;return null}
function calc(){
  let m=0,ex=tg,nx=0,ny=0;const on=new Set();
  for(const p of ptr.values()){
    if(p.look||p.tgl)continue;
    if(p.own){
      const r=dp.getBoundingClientRect(),R=r.width/2,dx=p.x-(r.left+R),dy=p.y-(r.top+R),d=Math.hypot(dx,dy);
      if(d>R*.18){const k=.38*d;
        if(dy<-k)m|=1;if(dy>k)m|=2;
        if(game===2&&stk){if(dx<-k)ex|=EX.SL;if(dx>k)ex|=EX.SR}else{if(dx<-k)m|=4;if(dx>k)m|=8}}
      const c=Math.min(d,R*.5)/(d||1);nx=dx*c;ny=dy*c;
    }else{
      const b=btnAt(p.x,p.y);
      if(b){const n=b.dataset.b;if(n in BIT)m|=BIT[n];else ex|=EX[n];on.add(b)}
    }
  }
  nub.style.transform='translate('+nx+'px,'+ny+'px)';
  btns.forEach(b=>b.classList.toggle('on',on.has(b)));
  $('#au').classList.toggle('on',!!(m&1));$('#ad').classList.toggle('on',!!(m&2));
  $('#al').classList.toggle('on',!!(m&4)||!!(ex&EX.SL));$('#ar').classList.toggle('on',!!(m&8)||!!(ex&EX.SR));
  if(m!==mask){
    if((m&~mask)&&navigator.vibrate)navigator.vibrate(8);
    mask=m;send(false);
  }
  if(ex!==exm){
    if((ex&~exm)&&navigator.vibrate)navigator.vibrate(8);
    exm=ex;sendEx(false);
  }
}
const portrait=()=>matchMedia('(orientation:portrait)').matches;
function ownsDp(x,y){const r=dp.getBoundingClientRect(),R=r.width/2;return Math.hypot(x-(r.left+R),y-(r.top+R))<=R*1.3}
addEventListener('pointerdown',e=>{
  if(portrait())return;
  const t=e.target;
  if(t.closest('#fs,#mn,#xcl,.ui'))return;
  if(t.closest('#pn')&&!t.closest('.k'))return;
  e.preventDefault();
  const x=e.clientX,y=e.clientY,p={x:x,y:y,own:false,look:false,tgl:false,lx:x};
  for(const b of tgs)if(vis(b)&&hit(b,x,y)){
    tg^=EX[b.dataset.b];b.classList.toggle('on',!!(tg&EX[b.dataset.b]));
    p.tgl=true;ptr.set(e.pointerId,p);calc();return;
  }
  if(vis(dp)&&ownsDp(x,y))p.own=true;
  else if(game===2&&!pn.classList.contains('open')&&!btnAt(x,y))p.look=true;
  ptr.set(e.pointerId,p);calc();
},{passive:false});
addEventListener('pointermove',e=>{
  const p=ptr.get(e.pointerId);if(!p)return;
  if(p.look){const dx=e.clientX-p.lx;p.lx=e.clientX;lookAcc+=dx*sens*(inv?-1:1);return}
  p.x=e.clientX;p.y=e.clientY;calc();
});
['pointerup','pointercancel'].forEach(t=>addEventListener(t,e=>{if(ptr.delete(e.pointerId))calc()}));
function release(){ptr.clear();tg=0;tgs.forEach(t=>t.classList.remove('on'));lookAcc=0;calc();send(true);sendEx(true)}
addEventListener('blur',release);
document.addEventListener('visibilitychange',()=>{if(document.hidden)release()});
addEventListener('orientationchange',release);
addEventListener('contextmenu',e=>e.preventDefault());
document.addEventListener('gesturestart',e=>e.preventDefault());
setInterval(()=>{if(mask)send(true);if(exm)sendEx(true)},100);
setInterval(()=>{
  if(game!==2||!ws||ws.readyState!==1)return;
  let v=Math.trunc(lookAcc);if(!v)return;
  lookAcc-=v;v=Math.max(-32000,Math.min(32000,v));
  ws.send(Uint8Array.of(2,v&255,(v>>8)&255));
},25);
const fs=$('#fs'),de=document.documentElement;
if(de.requestFullscreen){fs.hidden=false;fs.onclick=async()=>{
  try{await de.requestFullscreen();if(screen.orientation&&screen.orientation.lock)screen.orientation.lock('landscape').catch(()=>{})}catch(e){}}}
paint();open();
</script></body></html>)PADPAGE";

static void padHandlePage() {
  if (!rcAllowedPublic()) { webServer.send(403, "text/plain", "403 - buka lewat WiFi Accretion (AP), atau nyalakan 'Izinkan via WiFi rumah'"); return; }
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send_P(200, "text/html; charset=utf-8", PAD_PAGE);
}

// Dipanggil dari rcRegisterRoutes() (=> tiap startWebServer()).
void padRegisterRoutes() {
  webServer.on("/pad", HTTP_GET, padHandlePage);
  webServer.on("/g",   HTTP_GET, padHandlePage);
  padWsStart();
}
