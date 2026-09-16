"""Compile the production host queue/compositor and compare with the guest raster."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_jfg_widescreen_lifecycle import compiler_command, WORKSPACE
from jfg_reticle_fixtures import GLYPHS, RETICLES

HARNESS = r'''
#include "HEADER"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace JfgReticleOverlay;
GLYPH_SETUP
void check(bool b) { if (!b) throw std::runtime_error("overlay assertion failed"); }
int main(int argc, char **argv) {
 if (argc == 6) {
  Line l={std::stoi(argv[1]),std::stoi(argv[2]),std::stoi(argv[3]),std::stoi(argv[4]),160,120,unsigned(std::stoi(argv[5]))};
  if ((l.flags&15)>=4 && (l.flags&15)<=7) {
   const auto &g=glyphs[(l.flags&15)-4];l.glyph_count=unsigned(g.size()/2);
   std::copy(g.begin(),g.end(),l.glyph.begin());
  }
  raster(l,[](int x,int y,int,int){std::cout<<x<<","<<y<<"\n";}); return 0;
 }
 std::vector<uint8_t> ram(0x400000); Queue q;
 auto put=[&](unsigned a,unsigned v){std::memcpy(ram.data()+(a&0x1FFFFFFF),&v,4);};
 auto get=[&](unsigned a){unsigned v;std::memcpy(&v,ram.data()+(a&0x1FFFFFFF),4);return v;};
 const unsigned p=0x800F0000;
 ram[0xFECA8^3]=1;ram[0xA4FD0^3]=1;
 put(0x80068334,0xAD1D7FF0);put(0x8006E1C0,0x0801A0D8);
 unsigned data[]={144,111,152,103,2,0,160,120,0};
 for(unsigned i=0;i<9;++i)put(p+4*i,data[i]);
 put(0x8006E1C0,0x3C058010); // No installed HUD hook: keep native drawing.
 q.command(1,p,ram.data(),ram.size());check(get(p+32)==0);
 put(0x8006E1C0,0x0801A0D8);
 q.command(1,p,ram.data(),ram.size());check(get(p+32)==1&&q.pending[0].size()==1);
 // Submit flips the queue: it must not consume the frame still being built.
 put(p+0xBC,320);put(p+0xB8,240);put(0x800FECB0,0x80200000);
 q.command(2,p,ram.data(),ram.size());check(q.find(0x200000).lines.empty());
 put(0x80103B90,1);q.command(2,p,ram.data(),ram.size());
 auto f=q.find(0x200280);check(f.lines.size()==1&&q.pending[0].empty());
 // A copied scanout retains its own reticle when a later frame replaces the queue.
 auto saved=f;q.command(2,p,ram.data(),ram.size());
 check(q.find(0x200000).lines.empty()&&saved.lines.size()==1);
 for(unsigned mode: {0u,2u}) {ram[0xFECA8^3]=uint8_t(mode);put(p+32,0);q.command(1,p,ram.data(),ram.size());check(get(p+32)==0);}
 ram[0xFECA8^3]=1;ram[0xA4FD0^3]=2;put(p+32,0);
 q.command(1,p,ram.data(),ram.size());check(get(p+32)==0);
 q.command(0,0,nullptr,0);check(q.find(0x200000).lines.empty());
 // Capture every native cursor style, including immutable copies of guest glyphs.
 ram[0xA4FD0^3]=1;put(0x80103B90,0);
 const unsigned addresses[]={0xA6968,0xA698C,0xA69AC,0xA69CC};
 for(unsigned g=0;g<4;++g)for(unsigned i=0;i<glyphs[g].size();++i)
  ram[(addresses[g]+i)^3]=uint8_t(glyphs[g][i]);
 for(unsigned style: {0u,1u,2u,4u,5u,6u,7u}) {
  put(p+16,style|0x40);put(p+32,0);q.command(1,p,ram.data(),ram.size());check(get(p+32)==1);
  const auto &l=q.pending[0].back();
  if(style>=4) {
   check(l.glyph_count==glyphs[style-4].size()/2);
   for(unsigned i=0;i<l.glyph_count*2;++i)check(l.glyph[i]==glyphs[style-4][i]);
  }
 }
 for(unsigned style: {3u,8u,15u}) {put(p+16,style);put(p+32,0);q.command(1,p,ram.data(),ram.size());check(get(p+32)==0);}
 // VI mapping preserves an off-centre aim; X and Y pixels have equal size.
 View v;v.frame={0x200000,320,240,{{179,100,181,100,180,100,0}}};
 v.origin=0x200000;v.xscale=512;v.yscale=1024;v.hstart=108u<<16;v.vstart=34u<<16;
 for (int width : {960,1280}) { // Default 4:3 presentation and forced 16:9.
 std::vector<uint32_t> pixels(width*720);composite(v,pixels.data(),width,720);
 int x0=1280,x1=0,y0=720,y1=0,count=0;
 for(int y=0;y<720;++y)for(int x=0;x<width;++x)if(pixels[y*width+x]){x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);++count;}
 int centre=width*9/16;
 check(x0==centre-3&&x1==centre+5&&y0==300&&y1==302&&count==27);
 }
 return 0;
}
'''

class JfgReticleOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="jfg-overlay-", dir=WORKSPACE / "build")
        cls.addClassCleanup(cls.tmp.cleanup)
        directory=Path(cls.tmp.name)
        source=directory / "overlay.cpp"
        header=WORKSPACE / "Source/Project64-parallel-rdp/JfgReticleOverlay.h"
        glyph_setup='const std::vector<int8_t> glyphs[] = {' + ','.join(
            '{'+','.join(str(v) for p in points for v in p)+'}' for points in GLYPHS.values())+'};'
        source.write_text(HARNESS.replace("HEADER",header.as_posix()).replace('GLYPH_SETUP',glyph_setup))
        cls.exe=directory / ("overlay.exe" if os.name=="nt" else "overlay")
        r=subprocess.run(compiler_command(directory,source,cls.exe),cwd=directory,capture_output=True,text=True)
        if r.returncode: raise AssertionError(r.stdout+r.stderr)

    def test_queue_frame_matching_fallback_reset_and_square_pixels(self):
        r=subprocess.run([str(self.exe)],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_host_pixels_match_original_guest_renderer(self):
        from test_jfg_reticle_raster import JfgReticleRasterTests, STOCK_PROGRAM
        guest=JfgReticleRasterTests();guest.program=STOCK_PROGRAM
        lines=[(144,111,152,103),(150,114,155,109),(100,80,104,80),(100,80,100,84),
               (176,128,167,137),(137,97,140,125),(160,120,160,120)]
        for line in lines:
            style=0 if line[0]==line[2] or line[1]==line[3] else 2
            for flags in (style,style|0x10,style|0x20,style|0x80):
                r=subprocess.run([str(self.exe),*map(str,line),str(flags)],capture_output=True,text=True,check=True)
                actual=[tuple(map(int,l.split(','))) for l in r.stdout.splitlines()]
                expected=[] if line[:2]==line[2:] else guest.render(line,flags).plots
                self.assertEqual(actual,expected,(line,flags))

    def test_all_weapon_segments_and_mirrored_glyphs_match_guest(self):
        from test_jfg_reticle_raster import JfgReticleRasterTests, STOCK_PROGRAM
        guest=JfgReticleRasterTests();guest.program=STOCK_PROGRAM
        cases=set()
        for records in RETICLES:
            for x0,y0,x1,y1,flags in records:
                if flags >> 11 > 4: continue  # screen-edge sniper frame, separate from aim cursor
                for turn in range(4):
                    for f in (flags & 255, (flags & 255) | 0x10, (flags & 255) | 0x40):
                        cases.add((160+x0,120+y0,160+x1,120+y1,f))
                    x0,y0,x1,y1=-y0,x0,-y1,x1
        for *line,flags in sorted(cases):
            if flags & 15 != 2:
                line=[min(line[0],line[2]),min(line[1],line[3]),max(line[0],line[2]),max(line[1],line[3])]
            r=subprocess.run([str(self.exe),*map(str,line),str(flags)],capture_output=True,text=True,check=True)
            actual=[tuple(map(int,l.split(','))) for l in r.stdout.splitlines()]
            expected=[] if line[:2]==line[2:] else guest.render(line,flags).plots
            self.assertEqual(actual,expected,(line,flags))

if __name__ == '__main__': unittest.main()
