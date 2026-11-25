#!/usr/bin/env python3
# apply_chat_ui_v2.py -- CHAT_UI_V2
# Jalankan dari root repo:  python3 tools/patches/apply_chat_ui_v2.py   (--check = hanya cek jangkar)
#
# Desain ulang tampilan & cara pakai DUA app chat dengan bahasa visual yang sama:
#   * AI Chat (Gemini)   : phone.ino          (drawAiChat, aiTouch, riwayat tampilan, hook di triggerGeminiAI)
#   * Chat lewat MQTT    : chat_renphone.ino  (drawChat, chatMsgLayout, chatTouch)
# Tidak menyentuh jaringan, TLS, MQTT, Gemini request, memory SD, atau format payload.
# Aman dijalankan ulang (cek penanda). Berkas: phone.ino, chat_renphone.ino, CHANGELOG.md,
# docs/features/chat.md, docs/features/voice-and-ai.md, tools/i18n_pairs.txt (lalu jalankan tools/gen_i18n.py).
import os, sys

MARK = "CHAT_UI_V2"
PH = os.path.join("firmware", "phone", "phone.ino")
CH = os.path.join("firmware", "phone", "chat_renphone.ino")
CL = "CHANGELOG.md"
D1 = os.path.join("docs", "features", "chat.md")
D2 = os.path.join("docs", "features", "voice-and-ai.md")
PR = os.path.join("tools", "i18n_pairs.txt")
CHECK = "--check" in sys.argv


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus tepat 1x). Tidak ada berkas yang diubah." % (label, n))
    return s.replace(old, new)


def region(s, a, b, new, label):
    if s.count(a) != 1:
        sys.exit("GAGAL: awal '%s' ditemukan %dx (harus 1x)." % (label, s.count(a)))
    i = s.index(a)
    j = s.find(b, i)
    if j < 0 or s.count(b) != 1:
        sys.exit("GAGAL: akhir '%s' tidak ditemukan tepat 1x." % label)
    return s[:i] + new + s[j:]


ph = rd(PH)
ch = rd(CH)
if MARK in ph and MARK in ch:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)

# =====================================================================
# 1) phone.ino -- state riwayat tampilan AI Chat
# =====================================================================
ph = sub(ph,
         "float aiRespScrollVel = 0; // v12: kecepatan scroll (low-pass) -> momentum halus stlh jari dilepas\n",
         r'''float aiRespScrollVel = 0; // v12: kecepatan scroll (low-pass) -> momentum halus stlh jari dilepas

// CHAT_UI_V2: riwayat TAMPILAN AI Chat (RAM/PSRAM saja, hilang saat restart). Memory Gemini di SD
// (ai_memory.txt) TIDAK berubah. Yang disimpan di sini hanya tanya-jawab SEBELUM giliran yang sedang
// tampil; giliran terbaru selalu digambar dari aiLastPrompt / aiResponse (utuh, tidak dipotong).
#define AIH_MAX   6
#define AIH_Q_LEN 160
#define AIH_A_LEN 900
struct AiHist { char q[AIH_Q_LEN]; char a[AIH_A_LEN]; };
AiHist* aiHist = nullptr;    // PSRAM, dialokasikan saat pertama dipakai
int aiHistN = 0;
int aiFollow = 1;            // 1 = ikuti dasar chat, 2 = lompat ke awal jawaban terbaru (sekali), 0 = bebas (user menggeser)
void aiHistClear(){ aiHistN = 0; }
void aiHistPushPrev(){       // dipanggil triggerGeminiAI() SEBELUM pertanyaan baru menimpa aiLastPrompt / aiResponse
  if(!aiLastPrompt.length() || !aiResponse.length()) return;
  if(aiResponse.startsWith("Error:") || aiResponse.startsWith("Menghubungi Gemini")) return;
  if(!aiHist){
    aiHist = (AiHist*)heap_caps_calloc(AIH_MAX, sizeof(AiHist), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!aiHist) return;
  }
  if(aiHistN >= AIH_MAX){ memmove(aiHist, aiHist + 1, sizeof(AiHist) * (AIH_MAX - 1)); aiHistN = AIH_MAX - 1; }
  AiHist& h = aiHist[aiHistN++];
  strncpy(h.q, aiLastPrompt.c_str(), AIH_Q_LEN - 1); h.q[AIH_Q_LEN - 1] = 0; emjCutZ(h.q, AIH_Q_LEN - 1);
  bool cut = (int)aiResponse.length() > AIH_A_LEN - 1;
  strncpy(h.a, aiResponse.c_str(), AIH_A_LEN - 1); h.a[AIH_A_LEN - 1] = 0;
  if(cut){ emjCutZ(h.a, AIH_A_LEN - 4); strcat(h.a, ".."); } else emjCutZ(h.a, AIH_A_LEN - 1);
}
''', "state aiHist")

# hook: kirim pertanyaan baru -> simpan giliran sebelumnya, ikuti dasar chat
ph = sub(ph,
         "  aiLastPrompt = aiPrompt;\n  aiPrompt = \"\";\n",
         "  aiHistPushPrev();   // CHAT_UI_V2: giliran sebelumnya masuk riwayat tampilan\n  aiLastPrompt = aiPrompt;\n  aiPrompt = \"\";\n",
         "hook push")
ph = sub(ph,
         "  aiRespScrollY = 0; // pertanyaan baru -> mulai baca dari atas lagi\n",
         "  aiRespScrollY = 0; aiFollow = 1; // CHAT_UI_V2: pertanyaan baru -> layar ikut ke dasar (pertanyaan + titik mengetik)\n",
         "hook follow")
ph = sub(ph,
         "  aiResponse = emjNormalize(resp);       // emoji dari Gemini -> kode privat firmware (digambar sbg sprite)\n",
         "  aiResponse = emjNormalize(resp);       // emoji dari Gemini -> kode privat firmware (digambar sbg sprite)\n  aiFollow = 2;                          // CHAT_UI_V2: jawaban baru -> layar lompat ke AWAL jawaban\n",
         "hook jawaban")
ph = sub(ph,
         "void aiEnter(){ aiRespScrollY = 0; }",
         "void aiEnter(){ aiRespScrollY = 0; aiFollow = 2; }   // CHAT_UI_V2",
         "aiEnter")

# =====================================================================
# 2) phone.ino -- helper bersama + drawAiChat + aiTouch
# =====================================================================
AI_BLOCK = r'''// CHAT_UI_V2 ---------------------------------------------------------------
// Potongan UI yang dipakai bersama oleh AI Chat (di sini) dan Chat MQTT (chat_renphone.ino).
// Tombol Kirim bentuk pil: panah ke atas + teks; busy = tiga titik bergelombang.
void cuSendBtn(LGFX_Sprite& s,int x,int y,int w,int h,bool on,bool busy,const char* label){
  uint16_t bg = on ? (uint16_t)T().accent : (uint16_t)T().surface2;
  uint16_t fg = on ? (uint16_t)T().bg : (uint16_t)T().subtext;
  nyxAaRR(s,x,y,w,h,h/2,bg);
  int cy = y+h/2;
  if(busy){
    float t = millis()/1000.0f;
    for(int k=0;k<3;k++){
      float b = (sinf(t*6.0f-k*0.8f)+1.0f)*0.5f;
      nyxAaCircle(s,x+w/2-8+k*8,cy-(int)(b*2.0f),2,blend565(T().subtext,T().text,(uint8_t)(255.0f*b)));
    }
    return;
  }
  int tw = emjCells(label,(int)strlen(label))*6;
  int cx = x+(w-(7+4+tw))/2;
  s.fillTriangle(cx+3,cy-4,cx,cy+3,cx+6,cy+3,fg);
  s.setTextColor(fg); s.setCursor(cx+11,cy-4); s.print(label);
}
// Tombol bulat "ke pesan terbaru" (panah bawah) + lencana jumlah belum dibaca (0 = tanpa lencana).
void cuToBottomBtn(LGFX_Sprite& s,int cx,int cy,int badge){
  nyxAaCircle(s,cx,cy,12,T().divider);
  nyxAaCircle(s,cx,cy,11,T().surface2);
  s.drawLine(cx-4,cy-2,cx,cy+2,T().text); s.drawLine(cx,cy+2,cx+4,cy-2,T().text);
  s.drawLine(cx-4,cy-1,cx,cy+3,T().text); s.drawLine(cx,cy+3,cx+4,cy-1,T().text);
  if(badge>0){
    char b[4]; snprintf(b,sizeof(b),"%d",badge>9?9:badge);
    nyxAaCircle(s,cx+8,cy-9,6,T().accent);
    s.setTextColor(T().bg); s.setCursor(cx+8-3,cy-9-3); s.print(b);
  }
}
// Gelembung: sudut "ekor" persegi hanya kalau tail = true (pesan terakhir dalam satu kelompok).
void cuBubble(LGFX_Sprite& s,int x,int y,int w,int h,uint16_t c,bool right,bool tail){
  nyxAaRR(s,x,y,w,h,9,c);
  if(tail){
    if(right) s.fillRect(x+w-9,y+h-9,9,9,c);
    else      s.fillRect(x,y+h-9,9,9,c);
  }
}

#define AI_CHIP_N 3
const char* const AI_CHIPS[AI_CHIP_N] = { "Ceritakan fakta unik", "Buat timer 10 menit", "Ide menu makan malam" };
int aiChipY(int i){ return AI_CHAT_TOP+58+i*22; }
int aiInputRight(){ return aiPrompt.length() ? SCR_W-94 : SCR_W-60; }   // kolom input melebar saat tombol X tidak perlu
bool aiEmptyState(){ return aiHistN==0 && !aiLastPrompt.length() && !aiLoading && !aiResponse.length(); }

void drawAiChat(LGFX_Sprite& s){
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false); s.setTextSize(1);

  bool hasKey = (geminiApiKey.length() > 0 && geminiApiKey != "YOUR_GEMINI_API_KEY_HERE");
  bool online = (WiFi.status() == WL_CONNECTED);
  int micBtnX = aiMicBtnX(), micBtnW = AI_MIC_BTN_W;

  // ---- header: judul + titik status | Mic  Baru  API Key ----
  s.setTextColor(T().accent); s.setCursor(8,26); s.print("AI Chat");
  {
    uint16_t dc; const char* st;
    if(micRecording){ dc = T().danger; st = RPTR("merekam"); }
    else if(micTranscribing){ dc = T().accent2; st = RPTR("menyalin"); }
    else if(aiLoading){ dc = T().accent2; st = RPTR("berpikir"); }
    else if(!online){ dc = T().danger; st = RPTR("offline"); }
    else { dc = T().good; st = "Gemini"; }
    nyxAaCircle(s,58,30,3,dc);
    int room = (micBtnX-6-65)/6;
    if(room >= (int)strlen(st)){ s.setTextColor(T().subtext); s.setCursor(65,26); s.print(st); }
  }
  uint16_t micCol = micRecording ? T().danger : (micTranscribing ? T().surface2 : T().accent2);
  iosRR(s,micBtnX,24,micBtnW,18,9,micCol);
  const char* micTxt = micRecording ? "Stop" : (micTranscribing ? "..." : "Mic");
  s.setTextColor(0xFFFF); s.setCursor(micBtnX+(micBtnW-(int)strlen(micTxt)*6)/2,29); s.print(micTxt);

  iosRR(s,SCR_W-118,24,40,18,9,T().surface2);
  s.setTextColor(T().text); s.setCursor(SCR_W-118+(40-24)/2,29); s.print(RPTR("Baru"));

  iosRR(s,SCR_W-72,24,68,18,9,T().surface2);
  nyxAaCircle(s,SCR_W-72+9,33,3,hasKey ? T().good : T().danger);
  s.setTextColor(hasKey ? (uint16_t)T().text : (uint16_t)T().danger); s.setCursor(SCR_W-72+17,29); s.print("API Key");
  s.drawFastHLine(0,43,SCR_W,T().divider);

  int inputY  = backY() - 26;
  int chatTop = AI_CHAT_TOP;
  int chatBot = kbVisible ? kbY()-4 : inputY-6;
  int chatH   = chatBot - chatTop;

  if(aiEmptyState()){
    // ---- layar sambutan: ikon, ajakan, dan tiga saran satu-ketuk (hemat mengetik di keyboard layar) ----
    int cx = SCR_W/2, cy = chatTop+16;
    s.setClipRect(0,chatTop,SCR_W,chatH);
    nyxAaCircle(s,cx,cy,13,T().surface2);
    nyxAaCircle(s,cx,cy,7,T().accent2);
    nyxAaCircle(s,cx,cy,3,T().surface2);
    s.setTextColor(T().text);
    const char* hi = RPTR("Tanya apa saja");
    s.setCursor(cx-emjCells(hi,(int)strlen(hi))*3,cy+20); s.print(hi);
    if(!kbVisible){
      for(int i=0;i<AI_CHIP_N;i++){
        int cyy = aiChipY(i);
        iosRR(s,20,cyy,SCR_W-40,18,9,T().surface2);
        const char* ct = RPTR(AI_CHIPS[i]);
        s.setTextColor(T().accent); s.setCursor(SCR_W/2-emjCells(ct,(int)strlen(ct))*3,cyy+5); s.print(ct);
      }
    }
    s.clearClipRect();
    aiRespMaxScroll = 0; aiRespScrollY = 0;
  } else {
    // ---- daftar gelembung: riwayat + giliran terbaru ----
    const int PADX = 8, PADT = 5, PADB = 5, LH = 10, NAMEH = 11, GAPN = 8;
    int maxBWu = SCR_W*3/4, maxBWa = SCR_W*9/10;
    int chU = (maxBWu-2*PADX)/6, chA = (maxBWa-2*PADX)/6;
    const int MMAX = AIH_MAX*2+2;
    const char* mt[MMAX]; uint8_t mk[MMAX]; int mn = 0;     // mk: 0 kamu, 1 Gemini, 2 Gemini (error), 3 Gemini mengetik
    for(int i=0;i<aiHistN && mn<MMAX-2;i++){ mt[mn]=aiHist[i].q; mk[mn++]=0; mt[mn]=aiHist[i].a; mk[mn++]=1; }
    if(aiLastPrompt.length() && mn<MMAX-1){
      mt[mn]=aiLastPrompt.c_str(); mk[mn++]=0;
      if(aiLoading){ mt[mn]=""; mk[mn++]=3; }
      else if(aiResponse.length()){ mt[mn]=aiResponse.c_str(); mk[mn++]=aiResponse.startsWith("Error:") ? 2 : 1; }
    } else if(!aiLastPrompt.length() && aiResponse.length() && mn<MMAX){
      mt[mn]=aiResponse.c_str(); mk[mn++]=aiResponse.startsWith("Error:") ? 2 : 1;   // mis. error sebelum ada pertanyaan
    }
    // pesan paling lama dibuang bila buffer baris (AI_MAX_LINES) tidak cukup; yang terbaru selalu ikut
    int first = mn, used = 0, budget = AI_MAX_LINES-6;
    for(int i=mn-1;i>=0;i--){
      int L = (int)strlen(mt[i]), nl = 1;
      for(const char* p=mt[i];*p;p++) if(*p=='\n') nl++;
      int cm = (mk[i]==0) ? chU : chA; if(cm<8) cm=8;
      int est = (mk[i]==3) ? 1 : (L/(cm>12 ? cm-6 : cm) + nl + 1);
      if(used+est > budget && first < mn) break;
      used += est; first = i;
    }
    int ms[MMAX], mc[MMAX], mw[MMAX], mh[MMAX], mbh[MMAX], mg[MMAX], py[MMAX];
    int n = 0;
    for(int i=first;i<mn;i++){            // satu putaran: bungkus teks ke aiLinesBuf (salinan, jadi aman dari task Gemini)
      ms[i] = n;
      if(mk[i]==3) n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,"",T().text,chA);
      else         n = aiWrapAppend(aiLinesBuf,AI_MAX_LINES,n,mt[i],T().text,(mk[i]==0) ? chU : chA);
      mc[i] = n-ms[i];
      while(mc[i]>1 && aiLinesBuf[ms[i]+mc[i]-1].text.c[0]==0) mc[i]--;     // buang baris kosong di ujung
    }
    int contentH = 4;
    for(int i=first;i<mn;i++){
      int ml = 0;
      for(int k=0;k<mc[i];k++){ const char* ln = aiLinesBuf[ms[i]+k].text.c_str(); int cl = emjCells(ln,(int)strlen(ln)); if(cl>ml) ml=cl; }
      int minW = (mk[i]==0) ? 30 : (mk[i]==3 ? 46 : 36);
      mw[i]  = (mk[i]==3) ? 46 : (ml*6 > minW ? ml*6 : minW) + 2*PADX;
      if(mw[i] > ((mk[i]==0) ? maxBWu : maxBWa) + 2*PADX) mw[i] = ((mk[i]==0) ? maxBWu : maxBWa) + 2*PADX;
      mbh[i] = (mk[i]==3) ? 20 : (PADT + (mc[i]>0 ? mc[i] : 1)*LH + PADB);
      mg[i]  = (i==first) ? 4 : GAPN;
      mh[i]  = mg[i] + ((mk[i]==0) ? 0 : NAMEH) + mbh[i];
      py[i]  = contentH;
      contentH += mh[i];
    }
    contentH += 6;
    aiRespMaxScroll = contentH-chatH; if(aiRespMaxScroll<0) aiRespMaxScroll=0;
    int lastTop = (mn>first && mk[mn-1]!=0) ? py[mn-1] : -1;
    if(aiFollow==1) aiRespScrollY = (float)aiRespMaxScroll;
    else if(aiFollow==2){
      aiRespScrollY = (lastTop>=0) ? (float)min(lastTop-2,aiRespMaxScroll) : (float)aiRespMaxScroll;
      aiFollow = 0;
    }
    if(aiRespScrollY>aiRespMaxScroll) aiRespScrollY=aiRespMaxScroll;
    if(aiRespScrollY<0) aiRespScrollY=0;

    s.setClipRect(0,chatTop,SCR_W,chatH);
    int base = chatTop - (int)aiRespScrollY;
    for(int i=first;i<mn;i++){
      int sy = base + py[i];
      if(sy+mh[i] < chatTop) continue;
      if(sy > chatBot) break;
      bool user = (mk[i]==0);
      int bx = user ? (SCR_W-6-mw[i]) : 6;
      int by = sy + mg[i] + (user ? 0 : NAMEH);
      if(!user){ s.setTextColor(T().accent2); s.setCursor(bx+4,sy+mg[i]); s.print("Gemini"); }
      uint16_t bc = user ? (uint16_t)T().accent : (mk[i]==2 ? blend565(T().surface2,T().danger,150) : (uint16_t)T().surface2);
      cuBubble(s,bx,by,mw[i],mbh[i],bc,user,true);
      if(mk[i]==2) nyxAaRRRing(s,bx,by,mw[i],mbh[i],9,T().danger);
      if(mk[i]==3){
        float ph = (float)millis()/170.0f;
        unsigned long el = aiRequestStartMillis ? (millis()-aiRequestStartMillis) : 0;
        for(int k=0;k<3;k++){
          float bn = sinf(ph-(float)k*0.9f); if(bn<0.0f) bn = 0.0f;
          uint16_t dc = blend565(T().subtext,(el>AI_SOFT_TIMEOUT_MS) ? (uint16_t)T().accent : (uint16_t)T().text,(uint8_t)(255.0f*bn));
          nyxAaCircle(s,bx+14+k*10,by+10-(int)(3.0f*bn+0.5f),3,dc);
        }
        if(el>AI_SOFT_TIMEOUT_MS){ s.setTextColor(T().subtext); s.setCursor(bx+mw[i]+6,by+6); s.print(RPTR("agak lama...")); }
      } else {
        s.setTextColor(user ? (uint16_t)T().bg : (uint16_t)T().text);
        for(int k=0;k<mc[i];k++){
          int ly = by + PADT + k*LH;
          if(ly+LH < chatTop || ly > chatBot) continue;
          s.setCursor(bx+PADX,ly); s.print(aiLinesBuf[ms[i]+k].text.c_str());
        }
      }
    }
    s.clearClipRect();

    if(aiRespMaxScroll>0){                                   // scrollbar tipis di kanan
      int trackX = SCR_W-4, trackY = chatTop+2, trackH = chatH-4;
      int thumbH = max(14,(int)((float)chatH/contentH*trackH));
      int thumbY = trackY + (int)((float)aiRespScrollY/aiRespMaxScroll*(trackH-thumbH));
      nyxAaRR(s,trackX,trackY,2,trackH,1,T().divider);
      nyxAaRR(s,trackX,thumbY,2,thumbH,1,T().accent);
    }
    if(aiRespMaxScroll-(int)aiRespScrollY > 30) cuToBottomBtn(s,SCR_W-18,chatBot-14,0);
  }

  if(!kbVisible){
    int right = aiInputRight();
    iosRR(s,4,inputY,right-4,22,11,T().surface);
    int maxIn = (right-4-20)/6;
    String ipDisp = aiPrompt;
    if(!ipDisp.length()) ipDisp = RPTR("Tanya Gemini...");
    else if(emjCells(ipDisp.c_str(),(int)ipDisp.length()) > maxIn) ipDisp = String("..") + emjTail(ipDisp,maxIn-2);
    s.setTextColor(aiPrompt.length() ? (uint16_t)T().text : (uint16_t)T().subtext);
    s.setCursor(12,inputY+7); s.print(ipDisp.c_str());
    if(aiPrompt.length()){                                  // X: kosongkan pertanyaan sekali ketuk
      iosRR(s,SCR_W-90,inputY,30,22,11,T().danger);
      s.setTextColor(0xFFFF); s.setCursor(SCR_W-90+12,inputY+7); s.print("X");
    }
    cuSendBtn(s,SCR_W-56,inputY,52,22,aiPrompt.length()>0 && !aiLoading,aiLoading,RPTR("Kirim"));
  }

  if(kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

void aiTouch(int x,int y,bool held,bool isNew){
  static int dragLastY=0;
  static bool dragging=false;

  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){ kbVisible=false; kbTarget=nullptr; }
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }

  int inputY  = backY() - 26;
  int chatTop = AI_CHAT_TOP;
  int chatBot = inputY-6;

  if(isNew){
    dragging = false;
    if(isBack(x,y)){ navBack(); return; }

    if(x>=SCR_W-72 && x<=SCR_W-4 && y>=24 && y<=42){          // API Key
      kbTarget = &geminiApiKey;
      kbVisible = true;
      kbMode = KB_LOWER;
      showToast("Edit API Key");
      needRedraw = true;
      return;
    }

    if(x>=SCR_W-118 && x<=SCR_W-78 && y>=24 && y<=42){        // Baru: hapus memory SD + riwayat tampilan
      if(aiLoading){ showToast(RPTR("Tunggu jawaban selesai")); return; }
      clearAiMemory();
      aiHistClear();
      aiLastPrompt = ""; aiResponse = "";
      aiFollow = 1; aiRespScrollY = 0;
      showToast(RPTR("Percakapan baru dimulai"));
      needRedraw = true;
      return;
    }

    // Mic: pakai aiMicBtnX() yang SAMA dgn drawAiChat() (lihat catatan v27 di atas aiMicBtnX)
    int micBtnX=aiMicBtnX(), micBtnW=AI_MIC_BTN_W;
    if(x>=micBtnX && x<=micBtnX+micBtnW && y>=24 && y<=42){
      if(micRecording) micStopRecording();
      else if(!micTranscribing) micStartRecording();
      return;
    }

    if(aiEmptyState()){                                        // saran satu-ketuk
      for(int i=0;i<AI_CHIP_N;i++){
        int cyy = aiChipY(i);
        if(x>=20 && x<=SCR_W-20 && y>=cyy && y<=cyy+18){
          aiPrompt = String(RPTR(AI_CHIPS[i]));
          saveGeminiKey();
          triggerGeminiAI();
          vibTap();
          needRedraw = true;
          return;
        }
      }
    }

    if(aiRespMaxScroll-(int)aiRespScrollY > 30 && abs(x-(SCR_W-18))<=14 && abs(y-(chatBot-14))<=14){   // ke pesan terbaru
      aiFollow = 1; vibTap(); needRedraw = true;
      return;
    }

    if(y>=chatTop && y<=chatBot){                              // sentuh area chat = mulai geser
      dragging = true;
      dragLastY = y;
      aiRespScrollVel = 0;
      return;
    }

    if(y>=inputY && y<=inputY+22 && x<=aiInputRight()){        // kolom input
      kbTarget = &aiPrompt;
      kbVisible = true;
      kbMode = KB_LOWER;
      needRedraw = true;
      return;
    }

    if(aiPrompt.length() && x>=SCR_W-90 && x<=SCR_W-60 && y>=inputY && y<=inputY+22){   // X
      aiPrompt=""; showToast(RPTR("Pertanyaan dihapus")); needRedraw=true;
      return;
    }

    if(x>=SCR_W-56 && x<=SCR_W-4 && y>=inputY && y<=inputY+22){   // Kirim (kolom kosong = buka keyboard)
      if(!aiPrompt.length()){ kbTarget=&aiPrompt; kbVisible=true; kbMode=KB_LOWER; needRedraw=true; return; }
      saveGeminiKey();
      triggerGeminiAI();
      vibTap();
      return;
    }
    return;
  }

  // jari masih ditahan -> geser; kecepatan di-low-pass utk momentum di loop() (v12)
  if(held && dragging && y>=chatTop-20 && y<=chatBot+20){
    int dy = dragLastY - y;
    if(dy!=0){
      aiFollow = 0;
      aiRespScrollVel = aiRespScrollVel*0.5f + dy*0.5f;
      aiRespScrollY = constrain(aiRespScrollY + dy, 0.0f, (float)aiRespMaxScroll);
      dragLastY = y;
      needRedraw = true;
    }
  }
}

'''
ph = region(ph, "void drawAiChat(LGFX_Sprite& s){\n",
            "// =============================================\n// APP: FILE EXPLORER (DENGAN WEB UPLOADER)",
            AI_BLOCK, "drawAiChat..aiTouch")

# =====================================================================
# 3) chat_renphone.ino -- Chat MQTT
# =====================================================================
ch = sub(ch,
         "#define CB_TYPE_H   26                // tinggi slot indikator mengetik (bubble 20 + celah)\n",
         "#define CB_TYPE_H   26                // tinggi slot indikator mengetik (bubble 20 + celah)\n#define CB_DATE_H   16                // CHAT_UI_V2: tinggi chip tanggal (\"Hari ini\") saat hari berganti\n",
         "define CB_DATE_H")
ch = sub(ch,
         "static int    chatScroll = 0;\n",
         "static int    chatScroll = 0;\nstatic int    chatUnread = 0;                        // CHAT_UI_V2: pesan masuk selagi user menggulir ke atas\nstatic uint32_t chatSeenSeq = 0;                     // CHAT_UI_V2: seq tertinggi yang sudah dihitung UI\n",
         "state unread")
ch = sub(ch,
         "  chatScroll = 0;\n  chatCfgEdit = -1;\n",
         "  chatScroll = 0; chatUnread = 0; chatSeenSeq = chatLastSeq;   // CHAT_UI_V2\n  chatCfgEdit = -1;\n",
         "chatEnter")

LAYOUT = r'''// CHAT_UI_V2: chip tanggal saat hari berganti + warna nama per pengirim
static int chatDayKey(uint32_t ts) { return ts ? (int)((ts + CHAT_TZ_SEC) / 86400UL) : -1; }
static bool chatNewDay(int idx) {
  if (chatMsgs[idx].ts == 0) return false;
  if (idx == 0) return true;
  return chatDayKey(chatMsgs[idx].ts) != chatDayKey(chatMsgs[idx - 1].ts);
}
static void chatDayLabel(uint32_t ts, char* out, int cap) {
  time_t nowT = time(nullptr);
  int today = nowT > 1700000000L ? (int)(((uint32_t)nowT + CHAT_TZ_SEC) / 86400UL) : -2;
  int k = chatDayKey(ts);
  if (k == today) snprintf(out, cap, "%s", RPTR("Hari ini"));
  else if (k == today - 1) snprintf(out, cap, "%s", RPTR("Kemarin"));
  else {
    time_t tt = (time_t)(ts + CHAT_TZ_SEC); struct tm tmv; gmtime_r(&tt, &tmv);
    snprintf(out, cap, "%02d/%02d/%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
  }
}
static uint16_t chatNameColor(const char* nm) {
  uint32_t h = 0;
  for (; *nm; nm++) h = h * 31 + (uint8_t)*nm;
  switch (h % 3) {
    case 0:  return T().good;
    case 1:  return T().accent2;
    default: return blend565(T().accent, T().text, 110);
  }
}

// Tata letak satu pesan sbg bubble. Mengembalikan tinggi slot (chip tanggal + celah + nama + bubble).
// Out: jumlah baris, panjang baris terpanjang (karakter), celah di atas, tampilkan nama?
static int chatMsgLayout(int idx, int maxCh, int* nLines, int* maxLen, int* gap, bool* showName) {
  int st[8], ln[8];
  char qn[20], qt[32]; const char* body;
  bool hq = chatParseReply(chatMsgs[idx].text, qn, sizeof(qn), qt, sizeof(qt), &body);
  int n = chatWrap(body, maxCh, st, ln, 8);
  int ml = 0;
  if (n < 1) n = 1;
  else for (int k = 0; k < n; k++) { int cl = emjCells(body + st[k], ln[k]); if (cl > ml) ml = cl; }
  if (hq) { int ql = (int)strlen(qn) + 2 + (int)strlen(qt); if (ql > maxCh) ql = maxCh; if (ql > ml) ml = ql; }
  bool nd = chatNewDay(idx);
  bool mine = strcmp(chatName.c_str(), chatMsgs[idx].user) == 0;
  bool newGroup = (idx == 0) || nd || strcmp(chatMsgs[idx].user, chatMsgs[idx - 1].user) != 0;
  bool sn = !mine && newGroup;
  int g = newGroup ? CB_GAP_NEW : CB_GAP_SAME;
  if (nLines)   *nLines = n;
  if (maxLen)   *maxLen = ml;
  if (gap)      *gap = g;
  if (showName) *showName = sn;
  return (nd ? CB_DATE_H : 0) + g + (sn ? CB_NAMEH : 0) + (hq ? CB_QUOTE_H : 0) + CB_PADT + n * CB_LINE + CB_TIMEH + CB_PADB;
}

'''
ch = region(ch, "// Tata letak satu pesan sbg bubble. Mengembalikan tinggi slot (celah + nama + bubble).\n",
            "// CHAT_LAYOUT_FIX_V1: lebar teks = sel x 6 px", LAYOUT, "chatMsgLayout")

DRAW = r'''void drawChat(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false); s.setTextSize(1);
  if (chatToastFlag) { chatToastFlag = false; showToast(chatToastBuf, 2200); }

  if (!chatMsgs) {
    s.setTextColor(T().danger); s.setCursor(10, 60); s.print(RPTR("RAM tidak cukup untuk Chat."));
    drawBack(s); drawToast(s);
    return;
  }
  if (chatCfgMode) {
    chatDrawCfg(s);
    if (kbVisible) drawKb(s); else drawBack(s);
    drawToast(s);
    return;
  }

  // ---- header ----
  s.setTextColor(T().accent); s.setCursor(8, 26); s.print("Chat");
  uint16_t dot = (chatConn == 1) ? (uint16_t)T().good : ((chatConn == 2) ? (uint16_t)T().danger : (uint16_t)T().subtext);
  nyxAaCircle(s, 46, 30, 3, dot);
  String stt = (chatConn == 1) ? String("terhubung") : ((chatConn == 2) ? String(chatErrBuf) : String("menyambung..."));
  int maxSt = (SCR_W - 54 - 84) / 6;
  if ((int)stt.length() > maxSt) stt = stt.substring(0, maxSt > 2 ? maxSt - 2 : maxSt) + "..";
  s.setTextColor(T().subtext); s.setCursor(54, 26); s.print(stt.c_str());
  iosRR(s, SCR_W - 78, 24, 74, 18, 9, T().surface2);
  s.setTextColor(chatBcWait ? (uint16_t)T().subtext : (uint16_t)T().accent); chatPrintCentered(s, SCR_W - 78, 74, 29, RPTR("Ambil ulang"));
  s.drawFastHLine(0, CHAT_TOP - 1, SCR_W, T().divider);

  // ---- area pesan (bubble) ----
  int inputY = (kbVisible ? kbY() : backY()) - 26;
  int top = CHAT_TOP, bot = inputY - 4 - (chatReplyOn ? 18 : 0), areaH = bot - top;
  chatHitN = 0;
  int maxBW = SCR_W * 3 / 4;
  int maxCh = (maxBW - 2 * CB_PADX) / 6;
  if (areaH > 10 && chatMx && xSemaphoreTake(chatMx, pdMS_TO_TICKS(30)) == pdTRUE) {
    // CHAT_UI_V2: pesan baru masuk saat user menggulir ke atas -> posisi baca dijaga (daftar tidak melompat),
    // jumlahnya dihitung utk lencana di tombol "ke pesan terbaru"
    {
      int newN = 0, newH = 0;
      for (int i = chatCount - 1; i >= 0 && chatMsgs[i].seq > chatSeenSeq; i--) { newN++; newH += chatMsgLayout(i, maxCh, 0, 0, 0, 0); }
      if (chatSeenSeq != 0 && newN > 0 && chatScroll > 24) { chatScroll += newH; chatUnread += newN; }
      chatSeenSeq = chatLastSeq;
      if (chatScroll <= 24) chatUnread = 0;
    }
    int typeSlot = (int)lroundf((float)CB_TYPE_H * chatEaseOut(chatTypeAnim));
    // bubble "sedang dikirim / gagal" milik kita: slot tambahan paling bawah di atas indikator mengetik
    int outN = 0, outMl = 0, outH = 0, outGap = CB_GAP_NEW;
    if (chatOutState != 0) {
      int ost[8], oln[8];
      char oqn[20], oqt[32]; const char* obody;
      chatParseReply(chatOutText, oqn, sizeof(oqn), oqt, sizeof(oqt), &obody);
      outN = chatWrap(obody, maxCh, ost, oln, 8);
      if (outN < 1) outN = 1;
      else for (int k = 0; k < outN; k++) { int cl = emjCells(obody + ost[k], oln[k]); if (cl > outMl) outMl = cl; }
      if (chatCount > 0 && strcmp(chatMsgs[chatCount - 1].user, chatName.c_str()) == 0) outGap = CB_GAP_SAME;
      outH = outGap + CB_PADT + outN * CB_LINE + CB_TIMEH + CB_PADB;
    }
    int total = typeSlot + outH;
    for (int i = 0; i < chatCount; i++) total += chatMsgLayout(i, maxCh, 0, 0, 0, 0);
    int maxScroll = total > areaH ? total - areaH : 0;
    if (chatScroll > maxScroll) chatScroll = maxScroll;
    if (chatScroll < 0) chatScroll = 0;

    // pesan live baru: seluruh daftar "didorong naik" halus (digeser turun sebesar tinggi pesan lalu
    // mengendap ke 0), bubble barunya meluncur masuk dari tepi kanan/kiri. Hanya kalau user di dasar.
    int pushOff = 0, newX = 0;
    if (chatCount > 0 && chatScroll == 0) {
      uint32_t b0 = chatMsgs[chatCount - 1].born;
      uint32_t age = (b0 != 0) ? (uint32_t)(millis() - b0) : 0xFFFFFFFFUL;
      if (age < CHAT_ANIM_MS) {
        float e = chatEaseOut((float)age / (float)CHAT_ANIM_MS);
        int hNew = chatMsgLayout(chatCount - 1, maxCh, 0, 0, 0, 0);
        pushOff = (int)lroundf((float)hNew * (1.0f - e));
        newX = (int)lroundf(28.0f * (1.0f - e));
      }
    }

    s.setClipRect(0, top, SCR_W, areaH);
    int y = bot + chatScroll + pushOff;

    // indikator "sedang mengetik": bubble kecil dgn 3 titik memantul, meluncur naik dari bawah
    if (typeSlot > 4) {
      int bx = 6, by = y - CB_TYPE_H + 3 + (CB_TYPE_H - typeSlot);
      nyxAaRR(s, bx, by, 46, 20, 9, T().surface2);
      s.fillRect(bx, by + 11, 9, 9, T().surface2);
      float ph = (float)millis() / 170.0f;
      for (int k = 0; k < 3; k++) {
        float bn = sinf(ph - (float)k * 0.9f);
        if (bn < 0.0f) bn = 0.0f;
        int dy = (int)lroundf(3.0f * bn);
        uint16_t dc = blend565(T().subtext, T().text, (uint8_t)(255.0f * bn));
        nyxAaCircle(s, bx + 14 + k * 10, by + 10 - dy, 3, dc);
      }
      char who[CHAT_USER_LEN + 12];
      snprintf(who, sizeof(who), "%s mengetik", chatPeerName[0] ? chatPeerName : "Seseorang");
      s.setTextColor(T().subtext);
      s.setCursor(bx + 46 + 6, by + 6); s.print(who);
    }
    y -= typeSlot;

    if (outH > 0) {
      y -= outH;
      uint8_t os = chatOutState;
      int minW = (os == 2) ? 29 * 6 : 11 * 6;   // CHAT_LAYOUT_FIX_V1: "Gagal - ketuk utk kirim ulang" = 29 karakter
      int bw = (outMl * 6 > minW ? outMl * 6 : minW) + 2 * CB_PADX;
      int bh = CB_PADT + outN * CB_LINE + CB_TIMEH + CB_PADB;
      int bx = SCR_W - 6 - bw, by = y + outGap;
      uint16_t bc = (os == 2) ? blend565(T().surface2, T().danger, 150) : blend565(T().accent, T().bg, 90);   // pucat = belum terkirim
      cuBubble(s, bx, by, bw, bh, bc, true, true);
      if (os == 2) nyxAaRRRing(s, bx, by, bw, bh, 9, T().danger);
      int ost[8], oln[8];
      char oqn2[20], oqt2[32]; const char* obody2;
      chatParseReply(chatOutText, oqn2, sizeof(oqn2), oqt2, sizeof(oqt2), &obody2);
      int nn = chatWrap(obody2, maxCh, ost, oln, 8);
      s.setTextColor(T().bg);
      for (int k = 0; k < nn; k++) {
        char line[64];
        int L = oln[k] < (int)sizeof(line) - 1 ? oln[k] : (int)sizeof(line) - 1;
        memcpy(line, obody2 + ost[k], L); line[L] = 0;
        s.setCursor(bx + CB_PADX, by + CB_PADT + k * CB_LINE); s.print(line);
      }
      const char* st = (os == 2) ? "Gagal - ketuk utk kirim ulang" : "Mengirim...";
      s.setTextColor(os == 2 ? (uint16_t)T().danger : blend565(T().bg, T().accent, 90));
      s.setCursor(bx + bw - CB_PADX - chatTW(st), by + CB_PADT + outN * CB_LINE); s.print(st);
      chatOutX = bx; chatOutY = by; chatOutW = bw; chatOutH = bh;
    } else { chatOutW = 0; chatOutH = 0; }

    for (int i = chatCount - 1; i >= 0; i--) {
      int n, ml, gap; bool showName;
      int h = chatMsgLayout(i, maxCh, &n, &ml, &gap, &showName);
      y -= h;
      if (y + h <= top) break;
      if (y >= bot) continue;
      bool mine = (strcmp(chatName.c_str(), chatMsgs[i].user) == 0);
      bool nd = chatNewDay(i);
      int dh = nd ? CB_DATE_H : 0;
      int minIn = mine ? 42 : 30;
      int bw = (ml * 6 > minIn ? ml * 6 : minIn) + 2 * CB_PADX;
      char qn[20], qt[32]; const char* body;
      bool hq = chatParseReply(chatMsgs[i].text, qn, sizeof(qn), qt, sizeof(qt), &body);
      int qo = hq ? CB_QUOTE_H : 0;
      int bh = qo + CB_PADT + n * CB_LINE + CB_TIMEH + CB_PADB;
      int bx = mine ? (SCR_W - 6 - bw) : 6;
      if (i == chatCount - 1) bx += mine ? newX : -newX;
      int by = y + dh + gap + (showName ? CB_NAMEH : 0);
      uint16_t bc = mine ? (uint16_t)T().accent : (uint16_t)T().surface2;
      uint16_t tc = mine ? (uint16_t)T().bg : (uint16_t)T().text;
      // ekor bubble hanya di pesan TERAKHIR satu kelompok; pesan beruntun dari orang yang sama jadi kotak bulat penuh
      bool lastInGroup = (i == chatCount - 1) ? !(mine && outH > 0)
                                              : (strcmp(chatMsgs[i + 1].user, chatMsgs[i].user) != 0 || chatNewDay(i + 1));
      if (nd) {                                          // chip tanggal di tengah
        char dl[16]; chatDayLabel(chatMsgs[i].ts, dl, sizeof(dl));
        int dw = chatTW(dl) + 16, dx = (SCR_W - dw) / 2;
        nyxAaRR(s, dx, y + 2, dw, 12, 6, T().surface2);
        s.setTextColor(T().subtext); s.setCursor(dx + 8, y + 4); s.print(dl);
      }
      if (showName) {
        s.setTextColor(chatNameColor(chatMsgs[i].user));
        s.setCursor(bx + 4, y + dh + gap); s.print(chatMsgs[i].user);
      }
      cuBubble(s, bx, by, bw, bh, bc, mine, lastInGroup);
      if (hq) {                                          // kotak kutipan balasan
        uint16_t qbg = mine ? blend565(T().accent, T().bg, 70) : blend565(T().surface2, T().bg, 70);
        nyxAaRR(s, bx + 5, by + 4, bw - 10, CB_QUOTE_H - 3, 4, qbg);
        s.fillRect(bx + 5, by + 4, 2, CB_QUOTE_H - 3, mine ? (uint16_t)T().bg : (uint16_t)T().accent);
        char ql[60]; int qmax = (bw - 10 - 8) / 6;
        snprintf(ql, sizeof(ql), "%s: %s", qn, qt);
        if ((int)strlen(ql) > qmax && qmax > 3) { emjCutZ(ql, qmax - 2); strcat(ql, ".."); }
        s.setTextColor(mine ? (uint16_t)T().bg : (uint16_t)T().subtext);
        s.setCursor(bx + 10, by + 8); s.print(ql);
      }
      if (chatHitN < 24) { chatHitR[chatHitN][0] = bx; chatHitR[chatHitN][1] = by; chatHitR[chatHitN][2] = bw; chatHitR[chatHitN][3] = bh; chatHitIdx[chatHitN] = i; chatHitN++; }
      int st[8], ln[8];
      int nn = chatWrap(body, maxCh, st, ln, 8);
      s.setTextColor(tc);
      for (int k = 0; k < nn; k++) {
        char line[64];
        int L = ln[k] < (int)sizeof(line) - 1 ? ln[k] : (int)sizeof(line) - 1;
        memcpy(line, body + st[k], L); line[L] = 0;
        s.setCursor(bx + CB_PADX, by + CB_PADT + qo + k * CB_LINE); s.print(line);
      }
      char hm[8];
      if (chatMsgs[i].ts > 0) {
        time_t tt = (time_t)(chatMsgs[i].ts + CHAT_TZ_SEC);
        struct tm tmv; gmtime_r(&tt, &tmv);
        snprintf(hm, sizeof(hm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      } else snprintf(hm, sizeof(hm), "--:--");
      s.setTextColor(mine ? blend565(T().bg, T().accent, 90) : (uint16_t)T().subtext);
      s.setCursor(bx + bw - CB_PADX - chatTW(hm), by + CB_PADT + qo + n * CB_LINE); s.print(hm);
      if (mine) {                                       // tanda terkirim: sudah diterima broker (pesan kembali dari langganan)
        int kx = bx + bw - CB_PADX - chatTW(hm) - 11, ky = by + CB_PADT + qo + n * CB_LINE + 3;
        uint16_t kc = blend565(T().bg, T().accent, 60);
        s.drawLine(kx, ky + 2, kx + 2, ky + 4, kc); s.drawLine(kx + 2, ky + 4, kx + 7, ky - 1, kc);
      }
    }
    if (chatCount == 0) {                                // layar kosong: ikon gelembung + ajakan
      int cx = SCR_W / 2, cy = top + areaH / 2 - 16;
      if (cy < top + 16) cy = top + 16;
      nyxAaRR(s, cx - 18, cy - 13, 36, 24, 9, T().surface2);
      s.fillRect(cx - 18, cy + 2, 9, 9, T().surface2);
      for (int k = 0; k < 3; k++) nyxAaCircle(s, cx - 9 + k * 9, cy - 1, 2, blend565(T().subtext, T().text, 140));
      s.setTextColor(T().subtext);
      chatPrintCentered(s, 0, SCR_W, cy + 20, chatConn == 1 ? RPTR("Belum ada pesan") : RPTR("Menunggu koneksi..."));
      if (chatConn == 1 && areaH > 60) chatPrintCentered(s, 0, SCR_W, cy + 32, RPTR("Tulis yang pertama!"));
    }
    s.clearClipRect();
    if (chatScroll > 40) cuToBottomBtn(s, SCR_W - 18, bot - 14, chatUnread);   // ke pesan terbaru (+ jumlah belum dibaca)
    xSemaphoreGive(chatMx);
  }

  if (chatReplyOn) {                                      // bar "Membalas ..." di atas kotak input
    int ry = inputY - 18;
    nyxAaRR(s, 4, ry, SCR_W - 8, 16, 5, T().surface2);
    s.fillRect(8, ry + 3, 2, 10, T().accent);
    char rl[72]; int rmax = (SCR_W - 52) / 6; if (rmax > 70) rmax = 70;
    snprintf(rl, sizeof(rl), "Membalas %s: %s", chatReplyName, chatReplyQuote);
    if ((int)strlen(rl) > rmax && rmax > 3) { emjCutZ(rl, rmax - 2); strcat(rl, ".."); }
    s.setTextColor(T().subtext); s.setCursor(14, ry + 4); s.print(rl);
    s.setTextColor(T().danger); s.setCursor(SCR_W - 18, ry + 4); s.print("x");
  }
  // ---- baris input ----
  int boxW = SCR_W - 4 - 62;
  iosRR(s, 4, inputY, boxW, 22, 11, T().surface);
  bool typing = kbVisible && kbTarget == &chatInput;
  int remain = 200 - (int)chatInput.length();
  bool showCnt = chatInput.length() >= 150;                // sisa karakter muncul saat mendekati batas 200
  String shown = chatInput;
  int maxIn = (boxW - 20 - (showCnt ? 24 : 0)) / 6;
  if ((int)shown.length() > maxIn) shown = String("..") + emjTail(shown, maxIn - 2);
  if (typing) shown += "_";
  s.setTextColor(chatInput.length() || typing ? T().text : T().subtext);
  s.setCursor(12, inputY + 7); s.print(shown.length() ? shown.c_str() : RPTR("Ketik pesan..."));
  if (showCnt) {
    char cb[12]; snprintf(cb, sizeof(cb), "%d", remain < 0 ? 0 : remain);
    s.setTextColor(remain < 20 ? (uint16_t)T().danger : (uint16_t)T().subtext);
    s.setCursor(4 + boxW - 8 - chatTW(cb), inputY + 7); s.print(cb);
  }
  bool canSend = chatInput.length() > 0 && !chatSendPending && chatConn == 1;
  cuSendBtn(s, SCR_W - 56, inputY, 52, 22, canSend, chatSendPending, RPTR("Kirim"));

  if (kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

'''
ch = region(ch, "void drawChat(LGFX_Sprite& s) {\n",
            "// ---------------------------------------------------------------------\n// sentuhan\n",
            DRAW, "drawChat")

ch = sub(ch,
         "    if (isBack(x, y)) { navBack(); return; }\n    if (x >= SCR_W - 70 && x <= SCR_W - 4 && y >= 24 && y <= 42) {",
         "    if (isBack(x, y)) { navBack(); return; }\n    if (chatScroll > 40 && abs(x - (SCR_W - 18)) <= 14 && abs(y - (bot - 14)) <= 14) {   // CHAT_UI_V2: ke pesan terbaru\n      chatScroll = 0; chatUnread = 0; vibTap(); needRedraw = true; return;\n    }\n    if (x >= SCR_W - 78 && x <= SCR_W - 4 && y >= 24 && y <= 42) {",
         "chatTouch to-bottom")

# =====================================================================
# 4) kamus bahasa, changelog, docs
# =====================================================================
pairs = rd(PR)
NEWP = """# --- CHAT_UI_V2 (AI Chat & Chat MQTT) ---
Hari ini ::: Today
Kemarin ::: Yesterday
Belum ada pesan ::: No messages yet
Tulis yang pertama! ::: Write the first one!
Baru ::: New
merekam ::: recording
menyalin ::: transcribing
berpikir ::: thinking
agak lama... ::: taking a while...
Tanya apa saja ::: Ask me anything
Tanya Gemini... ::: Ask Gemini...
Ceritakan fakta unik ::: Tell me a fun fact
Buat timer 10 menit ::: Set a 10 minute timer
Ide menu makan malam ::: Dinner menu ideas
Tunggu jawaban selesai ::: Wait for the answer to finish
Percakapan baru dimulai ::: New conversation started
"""
if "CHAT_UI_V2" not in pairs:
    pairs = pairs.rstrip("\n") + "\n" + NEWP
cl = rd(CL)
ENTRY = ("- Improved: new look and easier use for AI Chat and for Chat over MQTT (CHAT_UI_V2), with the same look in both. "
         "AI Chat is now a real conversation: your questions are bubbles on the right, Gemini's answers bubbles on the left, "
         "the last 6 exchanges stay on screen (kept in PSRAM until restart; the Gemini memory on the SD card is unchanged), "
         "and while Gemini is working a bouncing-dots bubble shows (with a note after 15 s). A new answer opens at its first line, "
         "a down-arrow button jumps back to the newest message, and the empty screen offers three one-tap suggestions so you type less. "
         "The *Mem* button became *New* (it also clears the on-screen history), *Key: OK* became a dot plus *API Key*, "
         "the input box grows when the X button is not needed, and Send is a pill with an arrow that shows dots while waiting. "
         "Chat over MQTT gets day chips (Today / Yesterday / date), a different name colour per sender, bubble tails only on the last message of a group, "
         "a down-arrow button with an unread count (the list no longer jumps when a message arrives while you read older ones), "
         "a friendlier empty screen, a remaining-characters counter near the 200 limit, and the same Send pill. "
         "Only drawing and touch code changed (no MQTT, TLS, Gemini request or payload change). Checked with a PC stub build only, not yet on a device. "
         "Script: `tools/patches/apply_chat_ui_v2.py`\n")
if "CHAT_UI_V2" not in cl:
    cl = sub(cl, "## Unreleased\n", "## Unreleased\n" + ENTRY, "changelog")

d1 = rd(D1)
D1ADD = """
## Screen layout (CHAT_UI_V2)

- **Header:** status dot and text on the left, *Refetch* on the right, a thin line under it.
- **Messages:** your messages are accent-coloured bubbles on the right, others are grey bubbles on the left with the sender's name in a colour that stays the same per person. A chip with *Today*, *Yesterday* or the date appears when the day changes. The bubble tail is only drawn on the last message of a run from the same person.
- **Scrolling:** a round down-arrow button appears when you have scrolled up; it shows how many messages arrived meanwhile and jumps to the newest one. While you read older messages the list stays where it is when a new message arrives.
- **Input:** a rounded box, a counter with the remaining characters once you pass 150 of 200, and a *Send* pill (greyed out until you are connected and have typed something). Hold a bubble to reply, tap the red *Failed* bubble to resend (unchanged).
"""
if "Screen layout (CHAT_UI_V2)" not in d1:
    d1 = sub(d1, "## Configuration\n", D1ADD.lstrip("\n") + "\n## Configuration\n", "docs chat")

d2 = rd(D2)
D2ADD = """
**Screen layout (CHAT_UI_V2).** AI Chat is shown as a conversation: your questions are bubbles on the right, Gemini's answers bubbles on the left. The last 6 exchanges stay on screen while the phone is on (they are kept in PSRAM and are not saved; the Gemini memory on the SD card works as before). While Gemini works, a bubble with bouncing dots shows. A new answer opens at its first line; a down-arrow button jumps to the newest message. On an empty chat three suggestions can be tapped to send at once. *New* (was *Mem*) clears the SD memory and the on-screen history; the *API Key* pill shows a green or red dot and opens the key editor.
"""
if "Screen layout (CHAT_UI_V2)" not in d2:
    d2 = sub(d2, "\n## Voice Memo\n", D2ADD + "\n## Voice Memo\n", "docs voice")

if CHECK:
    print("OK: semua jangkar cocok (tidak ada yang ditulis)")
    sys.exit(0)

wr(PH, ph)
wr(CH, ch)
wr(PR, pairs)
wr(CL, cl)
wr(D1, d1)
wr(D2, d2)
print("terpasang (%s). Jalankan sekarang: python3 tools/gen_i18n.py" % MARK)
