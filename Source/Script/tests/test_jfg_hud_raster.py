"""Exercise the production HUD patch lifecycle and ordered RDP scope markers."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_jfg_widescreen_lifecycle import compiler_command, WORKSPACE, HACKS, MOCKS

HARNESS = r'''
#include <fstream>
using namespace JfgHudRaster;
#define check(ok) do { if (!(ok)) { std::cerr << "HUD raster assertion: " << #ok << " at " << __LINE__ << std::endl; std::exit(1); } } while (false)
void stock(CGameHackMemory &m, unsigned h, unsigned w) {
    m.WriteU32(h, 0x27BDFEE8); m.WriteU32(h+4, 0xAFBF003C);
    m.WriteU32(w+0x292C, 0x27BDFFA0); m.WriteU32(w+0x2930, 0xAFBF0024);
    for (auto &p : Hooks(h,w)) m.WriteU32(p.Address,p.Original);
}
int main(int argc, char **argv) {
    CGameHackMemory m; CRecompiler *r=nullptr; CGameHackCodePatcher p(m,r);
    const unsigned h=0x80300000,w=0x80304000;
    for (unsigned i=0;i<sizeof(Original)/4;++i) m.WriteU32(Start+4*i,Original[i]);
    stock(m,h,w);
    if (argc>1 && std::string(argv[1])=="image") {
        for (auto v:Image(h,w)) std::cout<<v<<"\n";
        return 0;
    }
    if (argc>1 && std::string(argv[1])=="mp-image") {
        for(auto v:JfgMultiplayerHud::Image(h,w))std::cout<<v<<"\n";
        return 0;
    }
    if (argc>2 && std::string(argv[1])=="retail-mp") {
        std::ifstream input(argv[2],std::ios::binary);
        std::vector<uint8_t> native(m.bytes.size());input.read(reinterpret_cast<char *>(native.data()),native.size());
        check(bool(input));for(unsigned i=0;i<native.size();++i)m.bytes[i^3]=native[i];
        const unsigned health=Module(m,6,0x10D8),multi=Module(m,61,0x2FA0);
        check(TextReady(m));check(Update(m,p,true,health,0,true));auto retail=m.bytes;
        check(JfgMultiplayerHud::Update(m,p,true));
        for(auto hook:JfgMultiplayerHud::Hooks(health,multi))check(m.Word(hook.Address)==hook.Replacement);
        check(JfgMultiplayerHud::Update(m,p,false));check(m.bytes==retail);
        return 0;
    }
    // Install, reapply, then remove using only the saved RAM image (no host cache).
    auto before=m.bytes;
    check(Update(m,p,true,h,w)); auto installed=m.bytes;
    check(Update(m,p,true,h,w)); check(m.bytes==installed);
    check(Update(m,p,false,h,w)); check(m.bytes==before);
    m.bytes=installed; check(Update(m,p,false,h,w)); check(m.bytes==before);
    // Adopt a saved HUD-only cave from the preceding build, then add fonts.
    auto legacy=Image(h,w,false);
    for(unsigned i=0;i<legacy.size();++i)m.WriteU32(Start+4*i,legacy[i]);
    for(auto &hook:Hooks(h,w,false))m.WriteU32(hook.Address,hook.Replacement);
    check(Update(m,p,true,h,w)); check(m.bytes==installed);
    check(Update(m,p,false,h,w)); check(m.bytes==before);
    // Preflight an unknown instruction without partially installing the cave.
    m.WriteU32(w+WeaponEntry,0x12345678); before=m.bytes;
    check(!Update(m,p,true,h,w)); check(m.bytes==before); stock(m,h,w);
    check(Update(m,p,true,h,w));
    // A changed owned delay keeps the diagnostic retired until removal is safe.
    m.WriteU32(h+12,0x12345678); check(!Update(m,p,false,h,w));
    check(m.Word(Start)==0x03E00008);
    m.WriteU32(h+12,0); check(Update(m,p,false,h,w));
    // Relocation must not rewrite the abandoned allocation, even if its old
    // instruction bits happen to remain there.
    check(Update(m,p,true,h,w));
    const unsigned h2=0x80310000,w2=0x80314000; stock(m,h2,w2);
    check(Update(m,p,true,h2,w2)); check(m.Word(h+8)==Jump(HealthEnter));
    check(Update(m,p,false,h2,w2));

    // Pause unloads overlay 14, but the shared font remains resident. Only
    // text may be hooked; the still-live health routine must be restored.
    stock(m,h,w); check(Update(m,p,true,h,w));
    check(Update(m,p,true,h,0,true));
    for(auto &hook:Hooks(h,w,false))
        if(hook.Address<h+HealthReturn+8) check(m.Word(hook.Address)==hook.Original);
    auto pauseImage=m.bytes;
    check(Update(m,p,true,h,0,true)); check(m.bytes==pauseImage);
    check(m.Word(End-8)==0 && m.Word(End-4)==0);
    check(m.Word(0x8006FD9C)==Jump(TextEnter));
    // Save-state adoption and disabling while the weapon module is absent.
    check(Update(m,p,false,h,0)); check(m.Word(0x8006FD9C)==0xAFB30020);
    m.bytes=pauseImage; check(Update(m,p,false,h,0));
    check(Update(m,p,true,h,0,true));
    // The loader brings back the stock weapon module on leaving pause.
    for(auto &hook:Hooks(h,w,false)) m.WriteU32(hook.Address,hook.Original);
    check(Update(m,p,true,h,w)); check(m.Word(h+8)==Jump(HealthEnter));
    check(m.Word(w+WeaponEntry)==Jump(WeaponEnter));
    check(Update(m,p,false,h,w));
    // Directly loading a pause state also works without a prior HUD cave.
    check(Update(m,p,true,h,0,true));
    m.WriteU32(0x8006FDA0,0x12345678);
    check(!Update(m,p,false,h,0)); check(m.Word(Start)==0x03E00008);
    m.WriteU32(0x8006FDA0,0); check(Update(m,p,false,h,0));
    const unsigned table=0x80180000,menu=0x80200000;
    m.WriteU32(0x800FEAA0,table);m.WriteU32(table+12*32,menu);
    m.WriteU32(menu+0xA6C,0x27BDFF88);m.WriteU32(menu+0xA70,0xAFBF001C);
    // The adaptation option must never modify 4:3, including in menus.
    // Wide text remains independent of player count and joy/camera state.
    for(unsigned mode:{0u,1u,2u,3u,4u})for(unsigned front:{0u,1u,2u,3u,5u,16u,24u,25u})
    for(unsigned players:{1u,2u,4u})for(unsigned initialized:{0u,1u}) {
        m.WriteU8(0x800FECA8,uint8_t(mode));m.WriteU8(0x800A4FD0,uint8_t(players));
        m.WriteU8(0x800A51B0,uint8_t(front));m.WriteU8(0x800A51A0,uint8_t(initialized));
        check(TextReady(m)==(initialized==1 && front>=2 && front<=24 && (mode==1 || mode==3)));
    }
    m.WriteU8(0x800FECA8,1);m.WriteU8(0x800A51B0,16);m.WriteU8(0x800A51A0,1);
    m.WriteU32(menu+0xA6C,0);check(!TextReady(m));
    m.WriteU32(menu+0xA6C,0x27BDFF88);m.WriteU32(table+12*32,0);check(!TextReady(m));
    m.WriteU32(table+12*32,menu);check(TextReady(m));

    // Switching a live title menu to 4:3 removes every font hook. Loading an
    // old font-only snapshot in 4:3 must restore exactly the same stock bytes.
    m.WriteU8(0x800A51B0,3);m.WriteU8(0x800FECA8,0);auto stockMenu=m.bytes;
    m.WriteU8(0x800FECA8,1);check(Update(m,p,TextReady(m),0,0,true));
    auto wideMenu=m.bytes;
    for(unsigned mode:{0u,2u}) {
        m.bytes=wideMenu;m.WriteU8(0x800FECA8,uint8_t(mode));
        check(Update(m,p,TextReady(m),0,0,true));
        m.WriteU8(0x800FECA8,0);check(m.bytes==stockMenu);
    }

    // Logo: atomically select the existing scaled path, adopt a saved patch,
    // restore 4:3, and leave unloaded/reused allocations and foreign code alone.
    const unsigned title=0x80210000,title2=0x80220000;
    auto titleStock=[&](unsigned base) {
        m.WriteU32(base+0x140,0x27BDFF30);m.WriteU32(base+0x144,0xAFBF0044);
        m.WriteU32(base+0x964,0x3C013F40);m.WriteU32(base+0x970,0x1540000C);
        m.WriteU32(base+0x974,0);m.WriteU32(base+0x994,0x0C013B6F);
        m.WriteU32(base+0x9B0,0x3C013F80);m.WriteU32(base+0x9D0,0x0C013C0B);
    };
    titleStock(title);m.WriteU32(table+63*32,title);auto stockTitle=m.bytes;
    check(UpdateTitleLogo(m,p,true));auto patchedTitle=m.bytes;
    check(m.Word(title+0x970)==0x1000000C);
    check(UpdateTitleLogo(m,p,true));check(m.bytes==patchedTitle);
    check(UpdateTitleLogo(m,p,false));check(m.bytes==stockTitle);
    m.bytes=patchedTitle;check(UpdateTitleLogo(m,p,false));check(m.bytes==stockTitle);
    m.WriteU32(title+0x9D0,0x12345678);auto foreignTitle=m.bytes;
    check(!UpdateTitleLogo(m,p,true));check(m.bytes==foreignTitle);
    titleStock(title);check(UpdateTitleLogo(m,p,true));
    m.WriteU32(table+63*32,0);check(UpdateTitleLogo(m,p,false));
    check(m.Word(title+0x970)==0x1000000C);
    titleStock(title2);m.WriteU32(table+63*32,title2);check(UpdateTitleLogo(m,p,true));
    check(UpdateTitleLogo(m,p,false));check(m.Word(title2+0x970)==0x1540000C);
    check(m.Word(title+0x970)==0x1000000C);

    std::vector<uint8_t> ram(0x400000); State state;
    auto put=[&](unsigned a,unsigned v){std::memcpy(ram.data()+(a&0x1FFFFFFF),&v,4);};
    auto get=[&](unsigned a){unsigned v;std::memcpy(&v,ram.data()+(a&0x1FFFFFFF),4);return v;};
    const unsigned cursor=0x80200000;
    put(0x800FF398,cursor); ram[0xFECA8^3]=1; ram[0xA4FD0^3]=1;
    check(!state.notify(1,ram.data(),unsigned(ram.size())));
    put(0x800681D0,0x03E00008);put(0x800681EC,0xAD007FE0);
    put(0x8006822C,0xAD007FE8);put(0x80068334,0xAD1D7FF0);
    for(unsigned mode:{0u,2u,4u}) {
        ram[0xFECA8^3]=uint8_t(mode);check(!state.notify(1,ram.data(),unsigned(ram.size())));
    }
    for(unsigned mode:{1u,3u}) {
        ram[0xFECA8^3]=uint8_t(mode); put(0x800FF398,cursor); state={};
        for(unsigned cmd:{1u,3u,4u,2u})check(state.notify(cmd,ram.data(),unsigned(ram.size())));
        check(get(0x800FF398)==cursor+32 && state.pending==0 && state.drawing==0);
        unsigned expected[]={1,3,1,0};
        for(unsigned i=0;i<4;++i) {
            check(state.consume(get(cursor+8*i),get(cursor+8*i+4)));
            check(state.drawing==expected[i]);
        }
        check(!state.consume(0xE7000000,0));
        check(!state.notify(2,ram.data(),unsigned(ram.size())));
        state={};check(!state.consume(0xE7000000,Marker|1));
    }
    ram[0xA4FD0^3]=2; check(!state.notify(1,ram.data(),unsigned(ram.size())));
    ram[0xA4FD0^3]=1; put(0x800FF398,0x803FFFFC);
    check(!state.notify(1,ram.data(),unsigned(ram.size())));
    // Fonts must advance the caller's Gfx**, including a stack local.
    state={}; put(0x8006FD9C,0x0801A098);put(0x8006826C,0xAF047FD0);put(0x80068288,0xAF387FD4);
    put(0x803FF000,cursor);put(0x800FF398,0x80208000);
    check(state.notify(5,ram.data(),unsigned(ram.size()),0x803FF000));
    check(state.notify(6,ram.data(),unsigned(ram.size()),0x803FF000));
    check(get(0x803FF000)==cursor+16 && get(0x800FF398)==0x80208000);
    check(state.consume(get(cursor),get(cursor+4)) && state.drawing==8);
    check(state.consume(get(cursor+8),get(cursor+12)) && state.drawing==0);
    check(!state.notify(5,ram.data(),unsigned(ram.size()),0x807FFFFC));
    // Text inside a private HUD layer must remain in its atlas, while pause
    // and map captions use the scene regardless of front mode 16 versus 17.
    state={};ram[0xA51B0^3]=16;ram[0xFECA8^3]=1;put(0x800FF398,cursor);
    check(state.notify(1,ram.data(),unsigned(ram.size())));
    check(state.notify(5,ram.data(),unsigned(ram.size())));
    check(get(cursor+12)==(Marker|5));
    check(state.notify(6,ram.data(),unsigned(ram.size())));
    check(state.notify(2,ram.data(),unsigned(ram.size())));
    check(!state.pending);
    // A recognized font-only installation works in widescreen menus and
    // multiplayer, but stale hooks from an older 4:3 state cannot draw.
    put(0x800681EC,0);put(0x8006822C,0);put(0x80068334,0);
    put(0x800682E8,0);put(0x800682EC,0);
    for(unsigned mode:{0u,1u,2u,3u})for(unsigned players:{1u,2u,4u})for(unsigned paused:{0u,1u}) {
        state={};put(0x803FF000,cursor);ram[0xFECA8^3]=uint8_t(mode);
        ram[0xA4FD0^3]=uint8_t(players);ram[0xFD7BD^3]=uint8_t(paused);
        check(!state.notify(1,ram.data(),unsigned(ram.size())));
        check(!state.notify(3,ram.data(),unsigned(ram.size())));
        const bool allowed=mode==1 || mode==3;
        check(state.notify(5,ram.data(),unsigned(ram.size()),0x803FF000)==allowed);
        if(!allowed)check(get(0x803FF000)==cursor && state.pending==0);
        if(allowed) {
            // The exit still balances a marker after a mid-call mode change.
            ram[0xFD7BD^3]=0;ram[0xFECA8^3]=0;
            check(state.notify(6,ram.data(),unsigned(ram.size()),0x803FF000));
            check(get(0x803FF000)==cursor+16 && state.pending==0);
        }
    }
    // The map decision travels with the ordered markers even if CPU state
    // changes before exit or before the RSP consumes the display list.
    for(unsigned mode:{0u,1u,2u,3u})for(unsigned front:{3u,8u,16u,17u}) {
        state={};put(0x803FF000,cursor);ram[0xFECA8^3]=uint8_t(mode);ram[0xA51B0^3]=uint8_t(front);
        const bool allowed=mode==1 || mode==3;
        check(state.notify(5,ram.data(),unsigned(ram.size()),0x803FF000)==allowed);
        if(!allowed) {check(get(0x803FF000)==cursor);continue;}
        check(get(cursor+4)==(Marker|7));
        ram[0xA51B0^3]=16;ram[0xFECA8^3]=0;
        check(state.notify(6,ram.data(),unsigned(ram.size()),0x803FF000));
        check(get(cursor+12)==(Marker|8));
        check(state.consume(get(cursor),get(cursor+4)) && state.drawing==8);
        check(state.consume(get(cursor+8),get(cursor+12)) && state.drawing==0);
        check(!state.sceneText && !state.pending);
    }
    state={};put(0x8006FD9C,0xAFB30020);put(0x803FF000,cursor);
    check(!state.notify(5,ram.data(),unsigned(ram.size()),0x803FF000));
    check(get(0x803FF000)==cursor);
    // Multiplayer patch owns separate storage and never changes float inputs.
    CGameHackMemory mp; CGameHackCodePatcher mpPatcher(mp,r);
    const unsigned mh=0x80200000,mm=0x80210000,mt=0x80180000;
    auto mpStock=[&](unsigned hb,unsigned mb) {
        mp.WriteU32(hb,0x27BDFEE8);mp.WriteU32(hb+4,0xAFBF003C);
        mp.WriteU32(mb+0x43C,0x27BDFF38);mp.WriteU32(mb+0x440,0xAFB50038);
        for(auto hook:JfgMultiplayerHud::Hooks(hb,mb))mp.WriteU32(hook.Address,hook.Original);
    };
    for(unsigned i=0;i<72;++i)mp.WriteU32(JfgMultiplayerHud::Start+i*4,JfgMultiplayerHud::Original[i]);
    mp.WriteU32(0x800FEAA0,mt);mp.WriteU32(mt+6*32,mh);mp.WriteU32(mt+61*32,mm);
    mp.WriteU32(Start,0x03E00008);mpStock(mh,mm);
    for(unsigned mode:{0u,1u,2u,3u})for(unsigned players:{1u,2u,3u,4u}) {
        mp.WriteU8(0x800FECA8,uint8_t(mode));mp.WriteU8(0x800A4FD0,uint8_t(players));
        auto originalMp=mp.bytes;
        check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
        const bool enabled=players>=2 && (mode==1 || mode==3);
        check((mp.Word(0x800418D8)==JfgMultiplayerHud::Call(JfgMultiplayerHud::Matrix))==enabled);
        auto saved=mp.bytes;check(JfgMultiplayerHud::Update(mp,mpPatcher,true));check(mp.bytes==saved);
        check(JfgMultiplayerHud::Update(mp,mpPatcher,false));check(mp.bytes==originalMp);
        mp.bytes=saved;check(JfgMultiplayerHud::Update(mp,mpPatcher,false));check(mp.bytes==originalMp);
    }
    mp.WriteU8(0x800FECA8,1);mp.WriteU8(0x800A4FD0,2);
    auto stockMp=mp.bytes;
    mp.WriteU32(mm+0x249C,0x12345678);auto foreignMp=mp.bytes;
    check(!JfgMultiplayerHud::Update(mp,mpPatcher,true));check(mp.bytes==foreignMp);
    mp.bytes=stockMp;check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
    // Upgrade a prior multiplayer HUD cave without reticle hooks.
    check(JfgMultiplayerHud::Update(mp,mpPatcher,false));
    auto legacyMp=JfgMultiplayerHud::Image(mh,mm,0,false);
    for(unsigned i=0;i<72;++i)mp.WriteU32(JfgMultiplayerHud::Start+i*4,legacyMp[i]);
    for(auto hook:JfgMultiplayerHud::Hooks(mh,mm,0,false))mp.WriteU32(hook.Address,hook.Replacement);
    check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
    check(mp.Word(0x8006E1C0)==JfgHudRaster::Jump(JfgMultiplayerHud::ReticleSubmit));
    // Overlay 13 is loaded only while aiming in multiplayer, then discarded.
    const unsigned mr=0x80220000,mr2=0x80230000;
    auto reticleStock=[&](unsigned base) {
        const unsigned prologue[]={0x27BDFF68,0xAFB4002C,0xAFB30028,0xAFB10020};
        for(unsigned i=0;i<4;++i)mp.WriteU32(base+0x4A8+i*4,prologue[i]);
        for(unsigned i=0;i<7;++i) {
            mp.WriteU32(base+JfgMultiplayerHud::ReticleCalls[i],0x0C01B563);
            mp.WriteU32(base+JfgMultiplayerHud::ReticleCalls[i]+4,JfgMultiplayerHud::ReticleDelays[i]);
        }
    };
    reticleStock(mr);mp.WriteU32(mt+13*32,mr);
    check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
    check(mp.Word(mr+0xC68)==JfgMultiplayerHud::Call(JfgMultiplayerHud::ReticleCapture));
    auto reticleSave=mp.bytes;
    mp.WriteU8(0x800FECA8,0);check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
    check(mp.Word(mr+0xC68)==0x0C01B563 && mp.Word(0x8006E1C0)==0x3C058010);
    mp.bytes=reticleSave;reticleStock(mr2);mp.WriteU32(mt+13*32,mr2);
    check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
    check(mp.Word(mr+0xC68)==JfgMultiplayerHud::Call(JfgMultiplayerHud::ReticleCapture));
    check(mp.Word(mr2+0xC68)==JfgMultiplayerHud::Call(JfgMultiplayerHud::ReticleCapture));
    mp.WriteU32(mt+13*32,0);check(JfgMultiplayerHud::Update(mp,mpPatcher,true));
    // A partially unloaded pair cannot release storage still referenced by
    // the radar; unloading both modules leaves abandoned allocations intact.
    auto ownedMp=mp.bytes;mp.WriteU32(mt+6*32,0);auto partialMp=mp.bytes;
    check(!JfgMultiplayerHud::Update(mp,mpPatcher,false));check(mp.bytes==partialMp);
    mp.WriteU32(mt+61*32,0);check(JfgMultiplayerHud::Update(mp,mpPatcher,false));
    check(mp.Word(mm+0x249C)==JfgMultiplayerHud::Call(JfgMultiplayerHud::Matrix));
    check(mp.Word(0x800418D8)==0x0C012361);
    mp.bytes=ownedMp;
    // Convert the mock's big-endian RAM into Project64's word-swapped storage.
    for(unsigned i=0;i<ram.size();++i)ram[i^3]=mp.bytes[i];
    const unsigned desc=0x803FF000,dest=0x803FE000;
    auto fixed=[&](unsigned index,int32_t value) {
        unsigned a=dest+(index/2)*4,b=a+32,shift=(index&1)?0:16,mask=65535u<<shift;
        put(a,(get(a)&~mask)|((uint32_t(value)>>16)<<shift));
        put(b,(get(b)&~mask)|((uint32_t(value)&65535)<<shift));
    };
    auto readFixed=[&](unsigned index) {
        unsigned a=dest+(index/2)*4,shift=(index&1)?0:16;
        return int32_t(((get(a)>>shift)&65535)<<16 | ((get(a+32)>>shift)&65535));
    };
    put(desc+0x10,dest);
    for(unsigned caller:{mh+0x464,mm+0x1FA4,mm+0x220C,mm+0x24A4,mm+0x29B8,mm+0x2DB8,0x800418E0u}) {
        put(desc+0x14,caller);put(desc+0x4C,0x8005A2F4);
        for(unsigned i=0;i<16;++i)fixed(i,int32_t(i%2?-65536:65536)*(i+1));
        check(multiplayer_matrix(ram.data(),unsigned(ram.size()),desc));
        for(unsigned i=0;i<16;++i) {
            int32_t expected=int32_t(i%2?-65536:65536)*(i+1);
            if(i==0 || i==4 || i==8)expected=expected*3/4;
            check(readFixed(i)==expected);
        }
    }
    auto untouched=ram;put(desc+0x4C,0x80012340);untouched=ram;
    check(!multiplayer_matrix(ram.data(),unsigned(ram.size()),desc));check(ram==untouched);
    put(desc+0x14,mh+0x464);
    for(unsigned mode:{0u,2u}) {
        ram[0xFECA8^3]=uint8_t(mode);untouched=ram;
        check(!multiplayer_matrix(ram.data(),unsigned(ram.size()),desc));check(ram==untouched);
        state={};check(!state.notify_number(9,ram.data(),unsigned(ram.size()),80));
    }
    ram[0xFECA8^3]=1;put(desc+0xE8,100);put(desc+0xE0,120);
    check(multiplayer_radar_point(ram.data(),unsigned(ram.size()),desc));check(get(desc+0xE0)==115);
    const unsigned numberCursor=0x803D0000;
    for(unsigned mode:{1u,3u})for(int anchor:{-100,0,100}) {
        state={};ram[0xFECA8^3]=uint8_t(mode);put(0x800FF398,numberCursor);
        check(state.notify_number(9,ram.data(),unsigned(ram.size()),anchor));
        check(int16_t(get(numberCursor)&65535)==(anchor+(mode==3?224:160))*4);
        check(state.consume(get(numberCursor),get(numberCursor+4)) && state.drawing==16);
        ram[0xFECA8^3]=0;
        check(state.notify_number(10,ram.data(),unsigned(ram.size()),0));
        check(state.consume(get(numberCursor+8),get(numberCursor+12)) && !state.drawing && !state.pending);
    }
    return 0;
}
'''


class HudRasterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="jfg-hud-raster-", dir=WORKSPACE / "build")
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)
        source = directory / "hud.cpp"
        memory = (HACKS / "GameHackMemory.h").read_text(encoding="utf-8-sig")
        impl = (HACKS / "GameHackMemory.cpp").read_text(encoding="utf-8-sig")
        patch = (HACKS / "JetForceGeminiHudRaster.h").read_text().replace('#include "GameHackMemory.h"', '')
        patch = patch.replace('"JetForceGeminiHudRasterOriginal.h"',
                              '"' + (HACKS / "JetForceGeminiHudRasterOriginal.h").as_posix() + '"')
        multiplayer = (HACKS / "JetForceGeminiMultiplayerHud.h").read_text().replace('#include "JetForceGeminiHudRaster.h"', '')
        patch += '\n' + multiplayer
        host = WORKSPACE / "Source/Project64-parallel-rdp/JfgHudRaster.h"
        source.write_text(MOCKS + memory[memory.index("struct GAME_HACK_CODE_PATCH"):] +
                          impl[impl.index("CGameHackCodePatcher::CGameHackCodePatcher("):] +
                          patch + '\n#include "' + host.as_posix() + '"\n' + HARNESS)
        cls.exe = directory / ("hud.exe" if os.name == "nt" else "hud")
        result = subprocess.run(compiler_command(directory, source, cls.exe), cwd=directory,
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_lifecycle_save_states_relocation_guards_and_ordered_markers(self):
        result = subprocess.run([str(self.exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_local_multiplayer_snapshot_signatures_and_restoration(self):
        import zipfile
        paths = list((WORKSPACE / "Bin/x64/Release/Save").rglob("multiplayer.pj.zip"))
        if not paths:
            self.skipTest("optional local multiplayer snapshot unavailable")
        with zipfile.ZipFile(paths[0]) as archive:
            ram = archive.read(archive.namelist()[0])[0xA60:0xA60+0x400000]
        path = Path(self.tmp.name) / "multiplayer.ram"
        path.write_bytes(ram)
        result = subprocess.run([str(self.exe), "retail-mp", str(path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_multiplayer_trampolines_preserve_stack_and_arguments(self):
        from test_jfg_ammo_hud import register_word, load_word
        from test_jfg_banner_hud import FpuMachine
        result = subprocess.run([str(self.exe), "mp-image"], capture_output=True, text=True, check=True)
        words = list(map(int, result.stdout.split()))
        program = {0x800680B0+i*4: word for i,word in enumerate(words)}
        regs = [0]+[register_word(0x11000000+i) for i in range(1,32)]
        regs[29]=register_word(0x803FF000);regs[31]=register_word(0x802F29B8)
        machine=FpuMachine(program,regs[:],{})
        machine.run(0x800680C0,0x800680CC)  # Before stock conversion JAL.
        self.assertEqual(program[0x800680CC],0x0C012361)
        self.assertEqual(machine.registers[4:8],regs[4:8])
        self.assertEqual(load_word(machine.memory,0x803FEFF0),regs[5]&0xFFFFFFFF)
        self.assertEqual(load_word(machine.memory,0x803FEFF4),regs[31]&0xFFFFFFFF)
        # Callee can clobber argument/temporary registers; descriptor survives.
        machine.registers[5]=123;machine.registers[31]=register_word(0x800680D4)
        machine.run(0x800680D4,0x800680E0)
        self.assertEqual(load_word(machine.memory,0xB3FF7FC0),0x803FEFE0)
        self.assertEqual(machine.registers[31],regs[31])
        self.assertEqual(program[0x800680E0],0x03E00008)
        machine.execute_ordinary(program[0x800680E4])
        self.assertEqual(machine.registers[29],regs[29])
        machine=FpuMachine(program,regs[:],{})
        machine.run(0x80068100,0x80058EF8)
        self.assertEqual(machine.registers[4:8],regs[4:8])
        self.assertEqual(load_word(machine.memory,0xB3FF7FC4),regs[5]&0xFFFFFFFF)
        self.assertEqual(machine.registers[29],register_word(0x803FEF40))
        machine.run(0x80068120,0x80068128)
        self.assertEqual(load_word(machine.memory,0xB3FF7FC8),0)
        machine.execute_ordinary(program[0x8006812C])
        self.assertEqual(machine.registers[29],regs[29])
        machine=FpuMachine(program,regs[:],{})
        machine.run(0x80068140,0x8030107C)
        self.assertEqual(load_word(machine.memory,0xB3FF7FCC),0x803FF000)
        self.assertEqual(machine.registers[4:8],regs[4:8])
        self.assertEqual(machine.registers[31],regs[31])

    def test_multiplayer_reticle_acknowledgement_and_fallback(self):
        from test_jfg_ammo_hud import register_word, load_word, store_word
        from test_jfg_banner_hud import FpuMachine
        result=subprocess.run([str(self.exe),"mp-image"],capture_output=True,text=True,check=True)
        program={0x800680B0+i*4:w for i,w in enumerate(map(int,result.stdout.split()))}
        for accepted in (0,1):
            regs=[0]+[register_word(0x11000000+i) for i in range(1,32)]
            regs[29]=register_word(0x803FF000);regs[31]=register_word(0x802FC670)
            memory={};store_word(memory,0x803FF010,2);store_word(memory,0x803FF014,1)
            machine=FpuMachine(program,regs[:],memory)
            machine.run(0x80068150,0x80068188)
            self.assertEqual(load_word(machine.memory,0xB3FF7FF0),0x803FEFD0)
            self.assertEqual(load_word(machine.memory,0x803FEFE4),1)
            self.assertEqual(load_word(machine.memory,0x803FEFE8),regs[20]&0xFFFFFFFF)
            self.assertEqual(load_word(machine.memory,0x803FEFEC),regs[19]&0xFFFFFFFF)
            store_word(machine.memory,0x803FEFF0,accepted)
            machine.run(0x80068188,0x8006819C if accepted else 0x8006D58C)
            self.assertEqual(machine.registers[29],regs[29])
            self.assertEqual(machine.registers[4:8],regs[4:8])
            self.assertEqual(machine.registers[31],regs[31])

    def test_guest_trampolines_preserve_arguments_results_and_stack(self):
        from test_jfg_ammo_hud import register_word, store_word, load_word
        from test_jfg_banner_hud import FpuMachine, float_bits
        result = subprocess.run([str(self.exe), "image"], capture_output=True, text=True, check=True)
        words = list(map(int, result.stdout.split()))
        program = {0x800681D0 + i*4: word for i, word in enumerate(words)}
        for entry, target, port, delta in (
            (0x800681E0, 0x80300010, 0xB3FF7FE0, 0),
            (0x80068200, 0x80012340, 0xB3FF7FE4, 0x118),
            (0x80068220, 0x80306948, 0xB3FF7FE8, 0),
            (0x80068240, 0x80012340, 0xB3FF7FEC, 0),
            (0x80068260, 0x8006FDA4, 0xB3FF7FD0, 0),
            (0x80068280, 0x80012340, 0xB3FF7FD4, 0x80),
        ):
            with self.subTest(entry=hex(entry)):
                registers = [0] + [register_word(0x11000000 + i) for i in range(1,32)]
                registers[2]=register_word(0x800F8000)
                registers[29]=register_word(0x800FF000)
                registers[31]=register_word(0x80012340)
                memory={};store_word(memory,0x800F8000,0x803FFFF0)
                store_word(memory,0x800FF080,0x803FF000)
                machine=FpuMachine(program,registers[:],memory,fpr=[float_bits(i+.5) for i in range(32)])
                fpr=machine.fpr[:];machine.hi=123;machine.lo=456
                if entry in (0x80068200,0x80068240,0x80068280):
                    return_pc=entry+(12 if entry==0x80068280 else 8)
                    machine.run(entry,return_pc)
                    self.assertEqual(program[return_pc],0x03E00008)
                    self.assertEqual(machine.registers[31]&0xFFFFFFFF,target)
                    machine.execute_ordinary(program[return_pc+4])
                else:
                    machine.run(entry,target)
                for i in set(range(32))-({24,25,29} if entry>=0x80068260 else {8,14,29}):
                    self.assertEqual(machine.registers[i],registers[i])
                self.assertEqual(machine.registers[29],register_word(0x800FF000+delta))
                self.assertEqual(machine.registers[14],register_word(0x803FFFF0) if entry==0x80068220 else registers[14])
                self.assertEqual((machine.hi,machine.lo),(123,456));self.assertEqual(machine.fpr,fpr)
                packet = registers[4]&0xFFFFFFFF if entry==0x80068260 else (0x803FF000 if entry==0x80068280 else 0)
                self.assertEqual(load_word(machine.memory,port),packet)
                if entry==0x80068260:
                    self.assertEqual(load_word(machine.memory,0x800FF020),registers[19]&0xFFFFFFFF)
                    self.assertEqual(load_word(machine.memory,0x800FF014),registers[16]&0xFFFFFFFF)
                if entry==0x800681E0:
                    self.assertEqual(load_word(machine.memory,0x800FF038),registers[30]&0xFFFFFFFF)
                    self.assertEqual(load_word(machine.memory,0x800FF034),registers[23]&0xFFFFFFFF)


if __name__ == "__main__":
    unittest.main()
