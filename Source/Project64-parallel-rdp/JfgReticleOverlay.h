#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../../external/parallel-rdp/parallel-rdp/vi_overlay.hpp"

// Host copy of the game's CPU line queue, separate from the emulated image.
// All access is serialized by the plugin mutex. No guest pointers survive a call.
namespace JfgReticleOverlay
{
struct Line
{
    int x0, y0, x1, y1, cx, cy;
    unsigned flags;
    // Styles 4..7 are guest pixel-offset glyphs, including mirrored variants.
    std::array<int8_t, 36> glyph = {};
    unsigned glyph_count = 0;
    std::array<int, 4> clip = {1, 1, 1023, 1023};
};
struct Frame
{
    uint32_t address = 0, width = 0, height = 0;
    std::vector<Line> lines;
};
struct View
{
    Frame frame;
    uint32_t origin = 0, xscale = 0, yscale = 0, hstart = 0, vstart = 0;
    unsigned crop = 0;
};
class Queue
{
public:
    std::array<std::vector<Line>, 2> pending;
    std::array<Frame, 3> frames;
    size_t next = 0;

    void clear() { *this = Queue{}; }

    void command(unsigned command, uint32_t packet, uint8_t *ram, uint32_t size)
    {
        if (command == 0) { clear(); return; }
        auto valid = [size](uint32_t a, unsigned bytes) {
            return a >= 0x80000000 && (a & 3) == 0 && uint64_t(a) + bytes <= 0x80000000ull + size;
        };
        if (!ram || size < 0x104000 || !valid(packet, command == 1 ? 36 : 0xC0)) return;
        auto word = [ram](uint32_t a) {
            uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFF), 4); return v;
        };
        auto set = [ram](uint32_t a, uint32_t v) { std::memcpy(ram + (a & 0x1FFFFFFF), &v, 4); };
        const unsigned index = word(0x80103B90);
        if (index > 1) return;
        if (command == 1)
        {
            // Only a live, recognized trampoline can suppress a guest line.
            // Other graphics plugins simply leave the zero acknowledgement alone.
            const unsigned mode = ram[0xFECA8 ^ 3];
            // The guest mode and installed HUD hooks own activation. The video
            // plugin's aspect override only controls the whole window; it must
            // not silently select the stretched stock reticle as a fallback.
            const unsigned players = ram[0xA4FD0 ^ 3];
            const bool solo = players == 1 && word(0x80068334) == 0xAD1D7FF0 &&
                word(0x8006E1C0) == 0x0801A0D8;
            const bool multi = players >= 2 && players <= 4 &&
                word(0x80068184) == 0xAD1D7FF0 && word(0x800681CC) == 0x4A46524D &&
                word(0x8006E1C0) == 0x0801A06A;
            if ((mode != 1 && mode != 3) || (!solo && !multi)) return;
            Line l = { int(word(packet)), int(word(packet+4)), int(word(packet+8)), int(word(packet+12)),
                       int(word(packet+24)), int(word(packet+28)), word(packet+16) & 255 };
            if (multi && int32_t(word(packet+20)) != -1) {
                // frontPlayerScreenLimits uses the number of rendered views,
                // which can differ from the number of active players.
                const unsigned views = ram[0xA4FCC ^ 3], player = word(packet+20);
                if (views < 1 || views > 4 || player >= views) return;
                const uint32_t bounds = 0x800A508C + (((views-1)*4+player)*4+0x40)*2;
                uint32_t xb = word(0x800A391C), yb = word(0x800A3920);
                float xs, ys; std::memcpy(&xs,&xb,4);std::memcpy(&ys,&yb,4);
                if (!std::isfinite(xs) || !std::isfinite(ys) || xs <= 0 || ys <= 0 || xs > 4 || ys > 4) return;
                for (unsigned i=0;i<4;++i) {
                    const uint32_t a=bounds+i*2;
                    const int16_t n=int16_t(word(a&~3u) >> ((a&2)?0:16));
                    l.clip[i]=int(n*(i&1?ys:xs))+(i<2?1:-2);
                }
                if (l.clip[0]>l.clip[2] || l.clip[1]>l.clip[3]) return;
            }
            const unsigned style = l.flags & 15;
            if (style > 7 || style == 3) return;
            if (std::abs(int64_t(l.x0)-l.cx) > 128 || std::abs(int64_t(l.x1)-l.cx) > 128 ||
                std::abs(int64_t(l.y0)-l.cy) > 128 || std::abs(int64_t(l.y1)-l.cy) > 128 ||
                std::abs(int64_t(l.cx)) > 1024 || std::abs(int64_t(l.cy)) > 1024 ||
                pending[index].size() >= 148) return;
            if (style >= 4)
            {
                const uint32_t addresses[] = { 0xA6968, 0xA698C, 0xA69AC, 0xA69CC };
                const unsigned counts[] = { 18, 16, 15, 15 };
                l.glyph_count = counts[style-4];
                for (unsigned i=0; i<l.glyph_count*2; ++i)
                    l.glyph[i] = int8_t(ram[(addresses[style-4]+i)^3]);
            }
            pending[index].push_back(l);
            set(packet+32, 1);
        }
        else if (command == 2)
        {
            const uint32_t address = word(0x800FECB0), width = word(packet+0xBC), height = word(packet+0xB8);
            if (!valid(address, 4) || width < 160 || width > 640 || height < 120 || height > 576 ||
                !valid(address, width * height * 2)) { pending[index ^ 1].clear(); return; }
            // fxOutputLines consumes the opposite queue, then flips the index.
            // Replace even an empty frame: switching weapons/menus must clear it.
            Frame *destination = nullptr;
            for (auto &f : frames) if (f.address == (address & 0x1FFFFFFF)) destination = &f;
            if (!destination) destination = &frames[next++ % frames.size()];
            *destination = { address & 0x1FFFFFFF, width, height, std::move(pending[index ^ 1]) };
            pending[index ^ 1].clear();
        }
    }

    Frame find(uint32_t origin) const
    {
        origin &= 0xFFFFFF;
        for (const auto &f : frames)
            if (f.width && origin >= f.address && origin < f.address + f.width * f.height * 2)
                return f;
        return {};
    }
};

// Match the original thin diagonal's exclusive endpoint and the axis-aligned
// writer's inclusive endpoint. Never resample or compress an already drawn line.
template<class Plot> void raster(const Line &l, Plot plot)
{
    if (l.x0 == l.x1 && l.y0 == l.y1) return;
    int x = l.x0, y = l.y0;
    const int red = l.flags & 0x10 ? 24 : 0;
    int green = l.flags & 0x10 ? 0 : 8;
    if (l.flags & 0x20) green *= 2;
    else if (l.flags & 0x80) green = 6;
    const unsigned style = l.flags & 15;
    if (style == 2)
    {
        for (int i = 0; i < std::abs(l.x1-l.x0); ++i)
            plot(x + (l.x1 > x ? i : -i), y + (l.y1 > y ? i : -i), red, green);
    }
    else if (style == 0)
    {
        // Native style 0 only draws horizontal/vertical segments.
        if (x != l.x1 && y != l.y1) return;
        for (int py = std::min(y,l.y1); py <= std::max(y,l.y1); ++py)
            for (int px = std::min(x,l.x1); px <= std::max(x,l.x1); ++px)
                plot(px, py, red, green);
    }
    else if (style == 1)
    {
        for (int px=std::min(x,l.x1); px<=std::max(x,l.x1); ++px)
            for (int dy=-1; dy<=1; ++dy)
                plot(px, std::min(y,l.y1)+dy, red, green);
    }
    else if (style >= 4 && style <= 7)
    {
        // fxDrawLine sorts both endpoints for glyph styles before queueing.
        for (unsigned i=0; i<std::min(l.glyph_count,18u); ++i)
            plot(std::min(x,l.x1) + (l.flags & 0x40 ? -l.glyph[i*2] : l.glyph[i*2]),
                 std::min(y,l.y1) + l.glyph[i*2+1], red, green);
    }
}

// Additive CPU lines join the same immutable framebuffer plane as the HUD,
// before extract_vram and all VI filters. Pixel footprints are constructed on
// the internal framebuffer grid, rather than the final Windows blit surface.
inline void before_vi(const View &v, unsigned scale, double aspect, RDP::VIOverlay &out)
{
    const unsigned xa=v.xscale&4095, ya=v.yscale&4095;
    const double viewW=640-2*std::round(v.crop*(640.0/240.0)), viewH=240-2*int(v.crop);
    if (v.frame.lines.empty() || !v.frame.width || !v.frame.height || !xa || !ya ||
        viewW<=0 || viewH<=0 || aspect<=0 || (scale!=1 && scale!=2 && scale!=4 && scale!=8)) return;
    if (out.pixels.empty()) {
        out.origin=v.frame.address;out.width=v.frame.width;out.height=v.frame.height;out.scale=scale;
        out.pixels.resize(size_t(out.width)*out.height*scale*scale*2);
        for(size_t i=1;i<out.pixels.size();i+=2)out.pixels[i]=0xFFFFFF;
    }
    if (out.origin!=v.frame.address || out.width!=v.frame.width || out.height<v.frame.height || out.scale!=scale) return;
    const int width=int(out.width*scale),height=int(out.height*scale);
    const double dx=viewW/viewH/aspect*xa/ya*scale;
    for(const auto &l:v.frame.lines) {
        const int clipLeft=std::max(1,l.clip[0])*int(scale);
        const int clipTop=std::max(1,l.clip[1])*int(scale);
        const int clipRight=std::min(int(v.frame.width)-1,l.clip[2]+1)*int(scale);
        const int clipBottom=std::min(int(v.frame.height)-1,l.clip[3]+1)*int(scale);
        raster(l,[&](int x,int y,int red,int green) {
            // Integrate the actual footprint instead of rounding each column
            // independently. At 3/4 width, a one-pixel stroke must not disappear
            // or change brightness when its position advances by one pixel.
            const double left=double(l.cx)*scale+(x-l.cx)*dx;
            const double right=double(l.cx)*scale+(x-l.cx+1)*dx;
            const int x0=std::max(clipLeft,int(std::floor(left)));
            const int x1=std::min(clipRight,int(std::ceil(right)));
            const int y0=std::max(clipTop,y*int(scale)),y1=std::min(clipBottom,(y+1)*int(scale));
            for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px) {
                if(px<0 || py<0 || px>=width || py>=height)continue;
                auto &color=out.pixels[(size_t(py)*width+px)*2];
                const double coverage=std::max(0.0,std::min(right,double(px+1))-std::max(left,double(px)));
                const unsigned r=std::min(255u,((color>>16)&255)+unsigned(std::round(red*8*coverage)));
                const unsigned g=std::min(255u,((color>>8)&255)+unsigned(std::round(green*8*coverage)));
                color=(color&255)|(r<<16)|(g<<8);
            }
        });
    }
}

// Reference compositor for raster/geometry tests; never used for presentation.
inline void composite(const View &v, uint32_t *pixels, int width, int height)
{
    const unsigned xa = v.xscale & 4095, ya = v.yscale & 4095;
    if (!pixels || !xa || !ya || !v.frame.width || width <= 0 || height <= 0) return;
    const double cropX = std::round(v.crop * (640.0 / 240.0));
    const double viewW = 640 - 2*cropX, viewH = 240 - 2*int(v.crop);
    if (viewW <= 0 || viewH <= 0) return;
    const double sx = width/viewW, sy = height/viewH;
    // Uniform scale in the final window gives each source pixel a square footprint.
    const double pixel = sy*1024/ya;
    const uint32_t offset = ((v.origin & 0xFFFFFF)-v.frame.address)/2;
    const double ox = offset % v.frame.width, oy = offset / v.frame.width;
    const double hx = int((v.hstart >> 16) & 1023)-108;
    const double vy = (int((v.vstart >> 16) & 1023)-34)/2.0;
    for (const auto &l : v.frame.lines)
    {
        const double cx = (hx+((l.cx-ox)*1024-((v.xscale>>16)&4095))/xa-cropX)*sx;
        const double cy = (vy+((l.cy-oy)*1024-((v.yscale>>16)&4095))/ya-v.crop)*sy;
        raster(l, [&](int x, int y, int red, int green) {
            if (x < 1 || y < 1 || x > int(v.frame.width)-2 || y > int(v.frame.height)-2) return;
            const int left = std::max(0, int(std::round(cx+(x-l.cx)*pixel)));
            const int top = std::max(0, int(std::round(cy+(y-l.cy)*pixel)));
            const int right = std::min(width, int(std::round(cx+(x-l.cx+1)*pixel)));
            const int bottom = std::min(height, int(std::round(cy+(y-l.cy+1)*pixel)));
            // The game adds red/green to the underlying 5-bit framebuffer.
            for (int py=top; py<bottom; ++py) for (int px=left; px<right; ++px)
            {
                auto &p = pixels[py*width+px];
                const unsigned r = std::min(255u, ((p>>16)&255) + unsigned(red*255/31));
                const unsigned g = std::min(255u, ((p>>8)&255) + unsigned(green*255/31));
                p = (p & 0xFF0000FF) | (r<<16) | (g<<8);
            }
        });
    }
}
}
