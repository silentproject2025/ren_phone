#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <string>
#include <vector>
#include "rplink.h"
#include "cam_tx.h"
#include "cam_rx.h"
#include "cam_avi.h"

static std::vector<uint8_t> mkjpeg(size_t n, unsigned seed){ std::vector<uint8_t> v(n); srand(seed); for(auto&b:v) b=rand(); v[0]=0xFF; v[1]=0xD8; v[n-2]=0xFF; v[n-1]=0xD9; return v; }
static CamRx rx; static uint8_t* S[3]; static uint8_t* ST;
static int framesDone=0;
static void pump(HardwareSerial& ser, RpParser& P, int corruptAt=-1){
  size_t i=0; for(uint8_t b: ser.out){ if((int)i==corruptAt) b^=0x55; i++; if(P.feed(b)){ if(P.type==RP_CAM_FRAME){ if(rx.feed(P.payload,P.len)) framesDone++; } } }
  ser.out.clear();
}
int main(){
  for(int i=0;i<3;i++) S[i]=(uint8_t*)malloc(96*1024); ST=(uint8_t*)malloc(448*1024);
  rx.attach(S[0],S[1],S[2],96*1024); rx.attachStill(ST,448*1024);
  RpParser P; HardwareSerial ser;
  // 1. preview utuh, beberapa ukuran
  size_t sizes[]={5,1017,1018,1019,2036,8000,60000};
  for(size_t sz: sizes){ if(sz<4) continue; auto j=mkjpeg(sz,sz); camTxFrame(ser,1,j.data(),j.size(),false,nullptr); pump(ser,P);
    int s=rx.take(); assert(s>=0); assert(rx.len(s)==j.size()); assert(memcmp(rx.data(s),j.data(),j.size())==0); }
  printf("preview utuh OK (%d frame)\n",framesDone); assert(framesDone==7);
  // 2. frame rusak (1 byte diubah -> xor gagal -> paket dibuang -> offset loncat) lalu frame baik
  { auto j=mkjpeg(5000,9); camTxFrame(ser,2,j.data(),j.size(),false,nullptr); pump(ser,P,1500); }
  assert(rx.take()<0); printf("frame rusak dibuang OK bad=%u\n",rx.bad);
  { auto j=mkjpeg(3000,10); camTxFrame(ser,3,j.data(),j.size(),false,nullptr); pump(ser,P); int s=rx.take(); assert(s>=0&&rx.len(s)==3000); }
  // 3. frame baru menggantikan yang belum diambil (tanpa kebocoran slot)
  for(int k=0;k<10;k++){ auto j=mkjpeg(4000+k,100+k); camTxFrame(ser,10+k,j.data(),j.size(),false,nullptr); pump(ser,P); }
  { int s=rx.take(); assert(s>=0 && rx.len(s)==4009); assert(rx.take()<0); }
  printf("drop frame lama OK dropped=%u\n",rx.dropped);
  // 4. slot habis karena UI memegang -> tetap pulih
  for(int k=0;k<50;k++){ auto j=mkjpeg(2000,k); camTxFrame(ser,k,j.data(),j.size(),false,nullptr); pump(ser,P); if(k%2) rx.take(); }
  { auto j=mkjpeg(2222,77); camTxFrame(ser,99,j.data(),j.size(),false,nullptr); pump(ser,P); int s=rx.take(); assert(s>=0&&rx.len(s)==2222);} 
  printf("tidak ada kebocoran slot OK\n");
  // 5. still: diabaikan kalau belum di-arm; diterima kalau sudah
  { auto j=mkjpeg(200000,5); camTxFrame(ser,50,j.data(),j.size(),true,nullptr); pump(ser,P); assert(rx.stillState==CAMRX_STILL_IDLE);
    rx.armStill(); camTxFrame(ser,51,j.data(),j.size(),true,nullptr); pump(ser,P); assert(rx.stillState==CAMRX_STILL_DONE && rx.stillLen==j.size() && memcmp(ST,j.data(),j.size())==0); }
  { auto j=mkjpeg(500000,6); rx.armStill(); camTxFrame(ser,52,j.data(),j.size(),true,nullptr); pump(ser,P); assert(rx.stillState==CAMRX_STILL_FAIL); }
  { auto j=mkjpeg(50000,7); rx.armStill(); camTxFrame(ser,53,j.data(),j.size(),true,nullptr); pump(ser,P,20000); assert(rx.stillState==CAMRX_STILL_FAIL); }
  printf("still OK\n");
  // 6. preview dan still bergantian
  { rx.armStill(); auto a=mkjpeg(7000,1), b=mkjpeg(30000,2); camTxFrame(ser,60,a.data(),a.size(),false,nullptr); camTxFrame(ser,61,b.data(),b.size(),true,nullptr); pump(ser,P);
    int s=rx.take(); assert(s>=0&&rx.len(s)==7000); assert(rx.stillState==CAMRX_STILL_DONE&&rx.stillLen==30000); }
  printf("campur preview+still OK\n");
  // 6b. dedup header JPEG (v2): header realistis (SOI,APP0,DQT x2,SOF0,DHT x4,SOS) + entropi acak
  {
    auto mkhdr=[&](uint8_t qseed){ std::vector<uint8_t> h={0xFF,0xD8, 0xFF,0xE0,0x00,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0};
      for(int t=0;t<2;t++){ h.push_back(0xFF); h.push_back(0xDB); h.push_back(0x00); h.push_back(0x43); h.push_back(t); for(int i=0;i<64;i++) h.push_back((uint8_t)(qseed+i+t)); }
      h.insert(h.end(),{0xFF,0xC0,0x00,0x11,8,0x00,0xF0,0x01,0x40,3,1,0x22,0,2,0x11,1,3,0x11,1});
      for(int t=0;t<4;t++){ h.push_back(0xFF); h.push_back(0xC4); int L=2+17+ (t%2?162:28); h.push_back(L>>8); h.push_back(L&255); for(int i=0;i<L-2;i++) h.push_back((uint8_t)(i*7+t)); }
      h.insert(h.end(),{0xFF,0xDA,0x00,0x0C,3,1,0,2,0x11,3,0x11,0,0x3F,0}); return h; };
    auto mkfr=[&](const std::vector<uint8_t>& h,size_t body,unsigned seed){ std::vector<uint8_t> v=h; srand(seed); for(size_t i=0;i<body;i++) v.push_back((uint8_t)(rand()&0x7F)); v.push_back(0xFF); v.push_back(0xD9); return v; };
    auto H1=mkhdr(3), H2=mkhdr(9);
    uint16_t hl=rpJpegHdrLen(H1.data(),H1.size()); assert(hl==H1.size()); printf("panjang header uji=%u\n",hl);
    assert(rpJpegHdrLen(H1.data(),hl-1)==0);                       // terpotong -> tidak valid
    rx.resetPreview(); CamHdrCache hc; size_t wireFull=0, wireSkip=0; unsigned hm0=rx.hdrMiss;
    for(int k=0;k<40;k++){                                          // 40 frame: tiap CAM_HDR_REFRESH+1 harus ada 1 yg lengkap
      auto j=mkfr(H1,3000+k*13,500+k); ser.out.clear();
      camTxFrame(ser,(uint8_t)(k+1),j.data(),j.size(),false,nullptr,&hc); size_t wire=ser.out.size(); bool skipped=(wire < j.size()+ (j.size()/1018+1)*11);
      pump(ser,P); int s=rx.take(); assert(s>=0); assert(rx.len(s)==j.size()); assert(memcmp(rx.data(s),j.data(),j.size())==0);
      if(k==0) { assert(!skipped); wireFull=wire; } else if(k==1) { assert(skipped); wireSkip=wire-(j.size()-wireFull+hl>0?0:0); }
    }
    printf("dedup header: roundtrip 40 frame bit-exact OK (hdrMiss=%u)\n",rx.hdrMiss-hm0); assert(rx.hdrMiss==hm0);
    // hemat byte: frame 2 kecil lebih sedikit ~ hl dari frame besar yg sama
    { rx.resetPreview(); CamHdrCache h2; auto a=mkfr(H1,2000,1); ser.out.clear(); camTxFrame(ser,1,a.data(),a.size(),false,nullptr,&h2); size_t w1=ser.out.size(); pump(ser,P); rx.take();
      auto b=mkfr(H1,2000,2); ser.out.clear(); camTxFrame(ser,2,b.data(),b.size(),false,nullptr,&h2); size_t w2=ser.out.size(); pump(ser,P); rx.take();
      printf("frame ukuran sama: lengkap=%zu B di kabel, header-dihilangkan=%zu B (hemat %zu B)\n",w1,w2,w1-w2); assert(w1-w2>=hl-10); }
    // header berubah (kualitas diganti) -> otomatis lengkap lagi, gambar tetap benar
    { auto a=mkfr(H2,4000,77); camTxFrame(ser,50,a.data(),a.size(),false,nullptr,&hc); pump(ser,P); int s=rx.take(); assert(s>=0&&memcmp(rx.data(s),a.data(),a.size())==0);
      auto b=mkfr(H2,4100,78); camTxFrame(ser,51,b.data(),b.size(),false,nullptr,&hc); pump(ser,P); s=rx.take(); assert(s>=0&&memcmp(rx.data(s),b.data(),b.size())==0); }
    printf("ganti header OK\n");
    // S3 kehilangan cache (mis. app dibuka ulang) padahal CAM masih menghilangkan header -> dibuang, TIDAK salah gambar; pulih di refresh
    { rx.resetPreview(); unsigned m0=rx.hdrMiss; int okc=0,bad=0;
      for(int k=0;k<CAM_HDR_REFRESH+3;k++){ auto j=mkfr(H2,1500,900+k); camTxFrame(ser,(uint8_t)(100+k),j.data(),j.size(),false,nullptr,&hc); pump(ser,P); if(rx.needHdr) hc.run=CAM_HDR_REFRESH;   /* = bit ACK "butuh header" */ int s=rx.take();
        if(s>=0){ assert(rx.len(s)==j.size() && memcmp(rx.data(s),j.data(),j.size())==0); okc++; } else bad++; }
      printf("cache S3 hilang: %d frame dibuang (hdrMiss+%u), %d pulih lewat bit ACK\n",bad,rx.hdrMiss-m0,okc); assert(bad==1 && okc==CAM_HDR_REFRESH+2); }
    // id header tidak cocok (CAM pikir S3 punya header baru, padahal tidak) -> dibuang, bukan salah gambar
    { rx.resetPreview(); auto f1=mkfr(H1,1200,5); CamHdrCache ha; camTxFrame(ser,1,f1.data(),f1.size(),false,nullptr,&ha); pump(ser,P); rx.take();   // S3 cache = H1
      CamHdrCache hb; auto f2=mkfr(H2,1200,6); camTxFrame(ser,2,f2.data(),f2.size(),false,nullptr,&hb); ser.out.clear();       // CAM-B "mengirim lengkap H2" tapi paket hilang
      auto f3=mkfr(H2,1200,7); unsigned m0=rx.hdrMiss; camTxFrame(ser,3,f3.data(),f3.size(),false,nullptr,&hb); pump(ser,P); assert(rx.take()<0 && rx.hdrMiss==m0+1); }
    printf("id header basi ditolak OK\n");
    // seq ACK terakhir
    { rx.resetPreview(); auto j=mkfr(H1,900,1); camTxFrame(ser,201,j.data(),j.size(),false,nullptr,nullptr); pump(ser,P); assert(rx.lastSeq==201); }
    printf("lastSeq OK\n");
    // frame hasil rusak di tengah (checksum) dgn dedup: dibuang, frame berikutnya lancar
    { rx.resetPreview(); CamHdrCache h3; auto a=mkfr(H1,6000,1); camTxFrame(ser,1,a.data(),a.size(),false,nullptr,&h3); pump(ser,P); rx.take();
      auto b=mkfr(H1,6000,2); camTxFrame(ser,2,b.data(),b.size(),false,nullptr,&h3); pump(ser,P,2500); assert(rx.take()<0);
      auto c=mkfr(H1,6000,3); camTxFrame(ser,3,c.data(),c.size(),false,nullptr,&h3); pump(ser,P); int s=rx.take(); assert(s>=0&&memcmp(rx.data(s),c.data(),c.size())==0); }
    printf("rusak + dedup pulih OK\n");
  }
  // 6c. CamFlow (kontrol aliran berbasis seq): wrap-around, ACK basi, ACK dari masa depan, rescue
  {
    CamFlow f; uint8_t sent=0; const uint8_t W=3;
    int sentCnt=0; for(int i=0;i<2000;i++){ if(f.canSend(sent,W)){ sent++; sentCnt++; } if(i%2==0 && f.inflight(sent)>0) f.onAck(sent,(uint8_t)(f.acked+1)); assert(f.inflight(sent)<=W); }
    assert(sentCnt>900);                                                // melewati wrap 256 berkali2
    f.sync(sent); assert(f.inflight(sent)==0);
    sent+=2; f.onAck(sent,(uint8_t)(sent-1)); assert(f.inflight(sent)==1);   // ACK parsial
    f.onAck(sent,(uint8_t)(sent-2)); assert(f.inflight(sent)==1);            // ACK basi -> diabaikan, tidak mundur
    f.onAck(sent,(uint8_t)(sent+5)); assert(f.inflight(sent)==1);            // ACK ngawur (masa depan) -> diabaikan
    sent+=2; assert(!f.canSend(sent,W)); f.rescue(sent,W); assert(f.canSend(sent,W) && f.inflight(sent)==W-1);
    // 3 frame hilang di kabel, ACK frame ke-4 mencakup semuanya (tidak ada kredit bocor)
    f.sync(sent); sent+=3; f.onAck(sent,(uint8_t)(sent+1-1)); assert(f.inflight(sent)==0);
    printf("CamFlow OK\n");
  }
  // 7. AVI
  struct FF { std::vector<uint8_t> d; size_t pos=0; size_t write(const uint8_t*b,size_t n){ if(pos+n>d.size()) d.resize(pos+n); memcpy(&d[pos],b,n); pos+=n; return n;} bool seek(uint32_t p){pos=p;return true;} } ff;
  CamAviWriter<FF> w; std::vector<uint32_t> o(100), z(100);
  assert(w.begin(&ff,320,240,o.data(),z.data(),100));
  for(int i=0;i<10;i++){ FILE* f=fopen(("/home/claude/tests/f"+std::to_string(i)+".jpg").c_str(),"rb"); if(!f){printf("tidak ada jpg uji, lewati AVI\n"); return 0;} std::vector<uint8_t> jb(100000); size_t n=fread(jb.data(),1,jb.size(),f); fclose(f); assert(w.addFrame(jb.data(),n)); }
  assert(w.finish(1000));   // 10 frame / 1000 ms = 10 fps
  FILE* fo=fopen("/home/claude/tests/out.avi","wb"); fwrite(ff.d.data(),1,ff.d.size(),fo); fclose(fo);
  printf("AVI ditulis %zu byte\n",ff.d.size());
  return 0;
}
