
import sys, os
if not os.path.exists('about_renphone.ino'):
    print("ERROR: about_renphone.ino tidak ada di folder ini"); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'SCR_ABOUT' in s:
    print("About Phone sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

s=rep(s,"SCR_OWNER };\nenum Orientation","SCR_OWNER,\n              SCR_ABOUT };\nenum Orientation","enum Screen")
s=rep(s,"void ownerSetAvatarUi(const char*);\n","void ownerSetAvatarUi(const char*);\n\nvoid aboutEnter(); void aboutExit(); void drawAbout(LGFX_Sprite&); void aboutTouch(int,int,bool,bool);\nvoid aboutLoopPoll();\n","forward decl")
s=rep(s,"AppDef apps[36] = {","AppDef apps[37] = {","apps[36]")
s=rep(s,"#define APP_COUNT 36","#define APP_COUNT 37","APP_COUNT")
s=rep(s,"ownerEnter,   ownerExit,    drawOwner,        ownerTouch,    SCR_OWNER },\n};","ownerEnter,   ownerExit,    drawOwner,        ownerTouch,    SCR_OWNER },\n  { \"About Phone\", 'i', 0, aboutEnter,   aboutExit,    drawAbout,        aboutTouch,    SCR_ABOUT },\n};","apps entry")
s=rep(s,"  apps[35].color=0x5EBF; // Profil - biru muda\n","  apps[35].color=0x5EBF; // Profil - biru muda\n  apps[36].color=0x8C71; // About Phone\n","initAppColors")
s=rep(s,"    case 'o': { // Profil: kepala + bahu","    case 'i': { // About Phone\n      int rr=max(8,r-6);\n      s.drawCircle(cx,cy,rr,ic); s.drawCircle(cx,cy,rr-1,ic);\n      s.fillCircle(cx,cy-rr/2,max(1,rr/7),ic);\n      s.fillRect(cx-max(1,rr/9),cy-rr/6,max(2,rr/4),rr*2/3,ic);\n      break;\n    }\n    case 'o': { // Profil: kepala + bahu","drawAppIcon")
s=rep(s,"SR_CONNECT, SR_MPU, SR_TCAL, SR_OWNER };","SR_CONNECT, SR_MPU, SR_TCAL, SR_OWNER, SR_ABOUT };","enum SR")
s=rep(s,"  settAddRow(SR_TCAL,g,34,0,nullptr,y); g++;\n\n  y+=14;","  settAddRow(SR_TCAL,g,34,0,nullptr,y); g++;\n\n  settAddRow(SR_SECT,0,26,0,\"TENTANG\",y);\n  settAddRow(SR_ABOUT,g,34,0,nullptr,y); g++;\n\n  y+=14;","settBuildLayout")
s=rep(s,"      case SR_TCAL:\n        s.setTextColor(T().danger);","      case SR_ABOUT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"About Phone\");\n        settChevron(s,xr-4,ty+4,T().subtext);\n        break;\n      case SR_TCAL:\n        s.setTextColor(T().danger);","draw SR_TCAL")
s=rep(s,"    case SR_OWNER:\n      navPush(SCR_OWNER); break;","    case SR_OWNER:\n      navPush(SCR_OWNER); break;\n    case SR_ABOUT:\n      navPush(SCR_ABOUT); break;","touch SR_OWNER")
s=rep(s,"    ownerLoopPoll(); // Profil: tutup Galeri setelah foto profil dipilih\n","    ownerLoopPoll(); // Profil: tutup Galeri setelah foto profil dipilih\n    if(curScreen()==SCR_ABOUT) aboutLoopPoll();\n","loop")
wf='.github/workflows/build-firmware.yml'
w=None
if os.path.exists(wf):
    w=open(wf,encoding='utf-8').read()
    a="          cp owner_renphone.ino build_sketch/\n"
    if 'about_renphone.ino' not in w:
        if w.count(a)==1: w=w.replace(a,a+"          cp about_renphone.ino build_sketch/\n")
        else: print("PERINGATAN: tambahkan 'cp about_renphone.ino build_sketch/' ke workflow sendiri"); w=None
    else: w=None
open('phone.ino','w',encoding='utf-8').write(s)
if w: open(wf,'w',encoding='utf-8').write(w)
print("OK: About Phone terpasang")
