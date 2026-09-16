#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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
            if ((mode != 1 && mode != 3) || ram[0xA4FD0 ^ 3] != 1 ||
                word(0x80068334) != 0xAD1D7FF0 || word(0x8006E1C0) != 0x0801A0D8)
                return;
            Line l = { int(word(packet)), int(word(packet+4)), int(word(packet+8)), int(word(packet+12)),
                       int(word(packet+24)), int(word(packet+28)), word(packet+16) & 255 };
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
