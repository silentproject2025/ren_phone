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

#define PAD_MAXC       5
#define PAD_STALE_MS   700UL    // mask dianggap basi kalau klien diam selama ini

static WebSocketsServer padWs(81);
static bool     padStarted = false;
static uint8_t  padCm[PAD_MAXC]  = {0};   // mask terakhir per klien
static uint32_t padCt[PAD_MAXC]  = {0};   // waktu paket terakhir per klien
static uint8_t  padGame    = 255;         // 0 none, 1 NES main, 2 DOOM main, 3 di app tapi belum main
static uint32_t padGameMs  = 0;

static void padRecalc() {
  uint8_t m = 0;
  for (int i = 0; i < PAD_MAXC; i++) m |= padCm[i];
  rcPadMask = m;
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

static void padWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (num >= PAD_MAXC) return;
  switch (type) {
    case WStype_CONNECTED: {
      if (!padIpOk(padWs.remoteIP(num))) { padWs.disconnect(num); return; }
      padCm[num] = 0; padCt[num] = millis();
      char b[4]; snprintf(b, sizeof(b), "g%u", (unsigned)padGameNow());
      padWs.sendTXT(num, b);                     // kasih tau game aktif -> label tombol
      Serial.printf("[pad] klien %u tersambung\n", (unsigned)num);
      break;
    }
    case WStype_DISCONNECTED:
      padCm[num] = 0; padRecalc();
      Serial.printf("[pad] klien %u putus\n", (unsigned)num);
      break;
    case WStype_BIN:
      if (length >= 1) { padCm[num] = payload[0]; padCt[num] = millis(); padRecalc(); }
      break;
    default:
      break;
  }
}

static void padWsStart() {
  if (padStarted) padWs.close();                 // dipanggil ulang kalau web server restart
  padWs.onEvent(padWsEvent);
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
  for (int i = 0; i < PAD_MAXC; i++)
    if (padCm[i] && now - padCt[i] > PAD_STALE_MS) { padCm[i] = 0; ch = true; }
  if (ch) padRecalc();

  if (now - padGameMs > 400UL) {                 // game aktif berubah -> reset + kabari halaman
    padGameMs = now;
    uint8_t g = padGameNow();
    if (g != padGame) {
      padGame = g;
      for (int i = 0; i < PAD_MAXC; i++) padCm[i] = 0;
      padRecalc();
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
body.g1{--ac:#e63946}body.g2{--ac:#ff8a1f}
#bar{position:fixed;top:calc(env(safe-area-inset-top,0px) + 8px);left:0;right:0;display:flex;align-items:center;gap:8px;padding:0 max(16px,env(safe-area-inset-left,0px));font-size:13px;color:var(--dim);z-index:2}
#dot{width:9px;height:9px;border-radius:50%;background:#c0392b;flex:none}
#dot.ok{background:#2ecc71}#dot.wait{background:#f1c40f}
#st{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#fs{background:var(--key);color:var(--fg);border:1px solid var(--ln);border-radius:8px;padding:6px 10px;font:inherit;font-size:12px}
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
#bB{left:0;top:15vmin}#bA{right:0;top:0}
#mid{position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom,0px) + 6vmin);transform:translateX(-50%);display:flex;gap:4vmin}
.pill{width:18vmin;height:7.5vmin;border-radius:99px;background:var(--key);border:2px solid var(--ln);display:flex;align-items:center;justify-content:center;font-size:2.8vmin;letter-spacing:.08em;color:var(--dim)}
.pill.on{background:var(--ac);border-color:var(--ac);color:#fff}
#rot{display:none;position:fixed;inset:0;background:var(--bg);z-index:9;align-items:center;justify-content:center;flex-direction:column;gap:12px;text-align:center;font-size:18px}
@media (orientation:portrait){#rot{display:flex}}
</style></head><body>
<div id="bar"><span id="dot"></span><span id="st">menyambung…</span><button id="fs" hidden>⛶ Layar penuh</button></div>
<div id="pad">
  <div id="dp"><span class="ar" id="au">▲</span><span class="ar" id="ad">▼</span><span class="ar" id="al">◀</span><span class="ar" id="ar">▶</span><div id="nub"></div></div>
  <div id="mid"><div class="pill" data-b="SELECT" id="lS">SELECT</div><div class="pill" data-b="START" id="lT">START</div></div>
  <div id="ab"><div class="rnd" data-b="B" id="bB">B</div><div class="rnd" data-b="A" id="bA">A</div></div>
</div>
<div id="rot"><div style="font-size:52px">⟳</div>Putar HP ke mode landscape</div>
<script>
const $=s=>document.querySelector(s);
const BIT={UP:1,DOWN:2,LEFT:4,RIGHT:8,A:16,B:32,START:64,SELECT:128};
const L={0:['A','B','START','SELECT','Buka app NES atau DOOM di HP Accretion'],
         1:['A','B','START','SELECT','NES'],
         2:['FIRE','USE','ENTER','MENU','DOOM'],
         3:['A','B','START','SELECT','Pilih game dulu di layar HP']};
const dp=$('#dp'),nub=$('#nub'),btns=[...document.querySelectorAll('[data-b]')];
let ws=null,mask=0,last=-1,game=0,conn='wait',rt=0;
const ptr=new Map();

function paint(){
  const l=L[game]||L[0];
  $('#bA').textContent=l[0];$('#bB').textContent=l[1];$('#lT').textContent=l[2];$('#lS').textContent=l[3];
  document.body.className='g'+game;
  $('#dot').className=conn;
  $('#st').textContent=conn==='ok'?l[4]:conn==='wait'?'Menyambung…':'Terputus — mencoba lagi…';
}
function open(){
  clearTimeout(rt);conn='wait';paint();
  try{ws=new WebSocket('ws://'+location.hostname+':81/')}catch(e){rt=setTimeout(open,1000);return}
  ws.binaryType='arraybuffer';
  ws.onopen=()=>{conn='ok';last=-1;paint();send(true)};
  ws.onclose=()=>{conn='off';paint();clearTimeout(rt);rt=setTimeout(open,1000)};
  ws.onerror=()=>{try{ws.close()}catch(e){}};
  ws.onmessage=e=>{if(typeof e.data==='string'&&e.data[0]==='g'){game=+e.data[1]||0;paint()}};
}
function send(force){
  if(!ws||ws.readyState!==1)return;
  if(mask!==last||force){ws.send(Uint8Array.of(mask));last=mask}
}
function hit(b,x,y){
  const r=b.getBoundingClientRect();
  if(b.classList.contains('rnd')){
    const R=r.width/2,cx=r.left+R,cy=r.top+R,dx=x-cx,dy=y-cy,Rg=R*1.2;
    return dx*dx+dy*dy<=Rg*Rg;
  }
  const m=8;return x>=r.left-m&&x<=r.right+m&&y>=r.top-m&&y<=r.bottom+m;
}
function calc(){
  let m=0,nx=0,ny=0;const on=new Set();
  for(const p of ptr.values()){
    if(p.own){
      const r=dp.getBoundingClientRect(),R=r.width/2,dx=p.x-(r.left+R),dy=p.y-(r.top+R),d=Math.hypot(dx,dy);
      if(d>R*.18){const k=.38*d;if(dy<-k)m|=1;if(dy>k)m|=2;if(dx<-k)m|=4;if(dx>k)m|=8}
      const c=Math.min(d,R*.5)/(d||1);nx=dx*c;ny=dy*c;
    }else for(const b of btns)if(hit(b,p.x,p.y)){m|=BIT[b.dataset.b];on.add(b)}
  }
  nub.style.transform='translate('+nx+'px,'+ny+'px)';
  btns.forEach(b=>b.classList.toggle('on',on.has(b)));
  $('#au').classList.toggle('on',!!(m&1));$('#ad').classList.toggle('on',!!(m&2));
  $('#al').classList.toggle('on',!!(m&4));$('#ar').classList.toggle('on',!!(m&8));
  if(m!==mask){
    if((m&~mask)&&navigator.vibrate)navigator.vibrate(8);
    mask=m;send(false);
  }
}
const portrait=()=>matchMedia('(orientation:portrait)').matches;
function ownsDp(x,y){const r=dp.getBoundingClientRect(),R=r.width/2;return Math.hypot(x-(r.left+R),y-(r.top+R))<=R*1.3}
addEventListener('pointerdown',e=>{
  if(e.target.closest('#fs')||portrait())return;
  e.preventDefault();
  ptr.set(e.pointerId,{x:e.clientX,y:e.clientY,own:ownsDp(e.clientX,e.clientY)});calc();
},{passive:false});
addEventListener('pointermove',e=>{const p=ptr.get(e.pointerId);if(p){p.x=e.clientX;p.y=e.clientY;calc()}});
['pointerup','pointercancel'].forEach(t=>addEventListener(t,e=>{if(ptr.delete(e.pointerId))calc()}));
function release(){ptr.clear();calc();send(true)}
addEventListener('blur',release);
document.addEventListener('visibilitychange',()=>{if(document.hidden)release()});
addEventListener('orientationchange',release);
addEventListener('contextmenu',e=>e.preventDefault());
document.addEventListener('gesturestart',e=>e.preventDefault());
setInterval(()=>{if(mask)send(true)},100);
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
