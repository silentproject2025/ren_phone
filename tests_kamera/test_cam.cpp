#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <string>
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
