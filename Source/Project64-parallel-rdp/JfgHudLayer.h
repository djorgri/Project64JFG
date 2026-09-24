#pragma once
#include "JfgHudRaster.h"
#include "JfgReticleOverlay.h"
#include <memory>
#include <vi_overlay.hpp>
#include <rdp_device.hpp>

// Native, isolated RDP images. No scene pixels are copied into these layers.
namespace JfgHudLayer
{
constexpr unsigned Width = 768, Height = 576;
constexpr uint32_t RamSize = 16 * 1024 * 1024;
constexpr uint32_t ImageBytes = Width * Height * 2;
constexpr unsigned Groups = 3; // health, weapons, packed font glyphs
constexpr uint32_t ColorBase = 8 * 1024 * 1024;
constexpr uint32_t DepthBase = ColorBase + Groups * ImageBytes;
static_assert(DepthBase + Groups * ImageBytes <= RamSize, "Private HUD images exceed RAM");

inline bool primitive(unsigned op) { return (op >= 8 && op <= 15) || op == 0x24 || op == 0x25 || op == 0x36; }
struct Target
{
    uint32_t address = 0, width = 0, height = 0;
    bool valid() const { return address && (width == 320 || width == 448); }
};
struct Pixel
{
    // F(background) = color + transmit * background. Keeping transmission per
    // channel also preserves the RDP's colored blending at native precision.
    std::array<uint8_t, 3> color, transmit;
};
struct Layer
{
    Target target;
    int x = 0, y = 0, width = 0, height = 0;
    std::vector<Pixel> pixels;
    bool text = false;
    std::array<int, 4> clip = {};
    uint64_t order = 0;
};
struct Glyph
{
    Target target;
    int x, y, width, height, atlasX, atlasY;
    std::array<int, 4> clip;
    uint64_t order = 0;
};
struct Fade
{
    Target target;
    uint64_t order;
    std::array<int, 4> clip; // Guest framebuffer coordinates, in quarter pixels.
    uint32_t rgba;
};
struct Frame
{
    Target target;
    std::vector<Layer> layers;
    std::vector<Fade> fades;
};
using FramePtr = std::shared_ptr<const Frame>;
struct View
{
    FramePtr frame;
    JfgReticleOverlay::View vi;
};

struct FontRectangle
{
    int x, y, width, height;
};
inline bool native_font_rectangle(const uint32_t *words, unsigned count, FontRectangle &rect)
{
    if (count != 4 || (words[0] >> 24) != 0xE4 ||
        (words[3] != 0x04000300 && words[3] != 0x04000400)) return false;
    const int x = (words[1] >> 12) & 4095, right = (words[0] >> 12) & 4095;
    const int top = words[1] & 4095, bottom = words[0] & 4095;
    const int expanded = (bottom - top) & 4095;
    const bool stock = words[3] == 0x04000400;
    int native = stock && expanded <= 1020 && !(expanded & 3) ? expanded : 0;
    if (!stock)
        for (int h = 4; h <= 1020; h += 4)
            if (h + 2 * ((5 * h) / 32) == expanded) { native = h; break; }
    const int y = (top + (stock ? 0 : (5 * native) / 32)) & 4095;
    if (!native || right <= x || ((x | right | y) & 3)) return false;
    rect = {x / 4, y / 4, (right - x) / 4, native / 4};
    return true;
}

struct Capture
{
    Target target;
    unsigned scope = 0;
    int numberAnchor = 0;
    std::vector<unsigned> scopes;
    std::array<Target, 2> layers = {};
    std::vector<Target> touched;
    std::vector<uint32_t> commands;
    std::array<uint32_t, 2> scissor = { 0xED000000, (320 * 4 << 12) | (240 * 4) };
    std::vector<Glyph> glyphs;
    int atlasX = 0, atlasY = 0, atlasRow = 0;
    uint64_t order = 0;
    std::array<uint64_t, 2> layerOrders = {};
    std::vector<Fade> fades;
    std::array<uint32_t, 2> combine = {}, otherModes = {};
    uint32_t primColor = 0;

    bool ordered_font_rectangle(const uint32_t *words, unsigned count, uint32_t xscale,
        uint32_t yscale, unsigned crop, double aspect, std::array<uint32_t, 4> &draw,
        unsigned rasterScale = 1, unsigned lines = 240) const
    {
        FontRectangle r;
        const unsigned xa = xscale & 4095, ya = yscale & 4095;
        if (scope != 4 || !target.valid() || !native_font_rectangle(words, count, r) ||
            !xa || !ya || crop >= lines / 2 || aspect <= 0 ||
            (rasterScale != 1 && rasterScale != 2 && rasterScale != 4 && rasterScale != 8)) return false;
        // Same anchor, vertical centre and pixel proportions as before_vi(),
        // but submit the glyph to the scene RDP at its original draw position.
        // Subsequent textured frames, triangles and fades naturally cover it.
        const double size = 4.0 / 3.0;
        const double viewW = JfgReticleOverlay::view_width(crop, lines);
        const double viewH = JfgReticleOverlay::view_height(crop, lines);
        const double dx = viewW / viewH / aspect * xa / ya * size;
        const double top = r.y + r.height * (1 - size) * .5;
        // Align to the actual raster grid, retaining half/quarter guest pixels
        // at 2x/4x/8x. Derive texture increments from these SAME final bounds.
        // Independent rounding of bounds and increments can read a neighbouring
        // atlas row at the bottom of the character.
        const unsigned grid = std::min(rasterScale, 4u); // RDP coordinates are 10.2.
        auto edge = [grid](double value) { return int(std::round(value * grid)) * int(4 / grid); };
        const int left = r.x * 4, right = edge(r.x + r.width * dx);
        const int y0 = edge(top), y1 = edge(top + r.height * size);
        if (right <= left || right > 4095 || y1 <= std::max(0, y0) || y1 > 4095) return false;
        const int step = int(std::round(4096.0 * r.width / (right - left)));
        const int stepY = int(std::round(4096.0 * r.height / (y1 - y0)));
        if (step < 1 || step > 32767 || stepY < 1 || stepY > 32767) return false;
        // RDP texture interpolation starts at floor(Y), not at the fractional
        // rectangle edge. Compensate that origin before submitting the glyph;
        // otherwise its last rows can read past the glyph into the atlas.
        // Point sampling reads pixel centres. Account for half an INTERNAL
        // pixel, and for clipping a negative top, in the same 10.5 mapping.
        const int interpolationY = std::max(0, y0) & ~3;
        const int s = int(int16_t(words[2] >> 16)) + int(std::round(double(step) / (64 * rasterScale)));
        const int t = int(int16_t(words[2] & 65535)) + int(std::round(
            (interpolationY - y0) * stepY / 128.0 + double(stepY) / (64 * rasterScale)));
        if (s < -32768 || s > 32767 || t < -32768 || t > 32767) return false;
        draw = {(words[0] & 0xFF000000) | (unsigned(right) << 12) | unsigned(y1),
            (words[1] & 0xFF000000) | (unsigned(left) << 12) | unsigned(std::max(0, y0)),
            (uint32_t(uint16_t(s)) << 16) | uint16_t(t), (unsigned(step) << 16) | unsigned(stepY)};
        return true;
    }

    bool ordered_font_commands(const uint32_t *words, unsigned count, uint32_t xscale,
        uint32_t yscale, unsigned crop, double aspect, std::vector<uint32_t> &batch,
        const RDP::Quirks &sceneQuirks = {}, unsigned rasterScale = 1, unsigned lines = 240) const
    {
        std::array<uint32_t, 4> draw;
        if (!ordered_font_rectangle(words, count, xscale, yscale, crop, aspect, draw, rasterScale, lines)) return false;
        // NativeTexRects is useful for unscaled game sprites, but would snap
        // this resized glyph back to the guest grid even at 2x/4x/8x. Override
        // it in the command stream for this draw only (also safe asynchronously).
        auto fontQuirks = sceneQuirks;
        fontQuirks.set_native_resolution_tex_rect(false);
        const uint32_t meta = uint32_t(RDP::Op::MetaSetQuirks) << 24;
        batch.insert(batch.end(), {2, meta, fontQuirks.u.words[0]});
        // Sample original texels, then let VI filter the high-resolution result.
        // Preserve combiner/blender/TLUT and immediately restore guest state.
        const bool filter = (otherModes[0] >> 24) == 0xEF && (otherModes[0] & 0x3000);
        if (filter) batch.insert(batch.end(), {2, otherModes[0] & ~0x3000u, otherModes[1]});
        batch.push_back(4); batch.insert(batch.end(), draw.begin(), draw.end());
        if (filter) batch.insert(batch.end(), {2, otherModes[0], otherModes[1]});
        batch.insert(batch.end(), {2, meta, sceneQuirks.u.words[0]});
        return true;
    }

    bool ordered_number_rectangle(const uint32_t *words, unsigned count, std::array<uint32_t, 4> &draw) const
    {
        if (scope != 5 || !target.valid() || count != 4 || (words[0] >> 24) != 0xE4) return false;
        const int x0 = (words[1] >> 12) & 4095, x1 = (words[0] >> 12) & 4095;
        if (x1 <= x0) return false;
        const int left = int(std::round(numberAnchor + (x0 - numberAnchor) * .75));
        const int right = int(std::round(numberAnchor + (x1 - numberAnchor) * .75));
        const int step = int(std::round(int16_t(words[3] >> 16) * (4.0 / 3.0)));
        if (left < 0 || right > 4095 || right <= left || step < -32768 || step > 32767) return false;
        draw = { (words[0] & 0xFF000FFF) | (unsigned(right) << 12),
            (words[1] & 0xFF000FFF) | (unsigned(left) << 12), words[2],
            (uint32_t(uint16_t(step)) << 16) | (words[3] & 65535) };
        return true;
    }

    void scene_rectangle(const uint32_t *words)
    {
        // Retail drawClearScreen's solid fade (8006C124 / 8006CFC0):
        // one cycle, primitive RGBA, forced source-alpha over memory, no Z.
        // Do not infer fades from rectangle size or from a CPU fade flag.
        if (!target.valid() || combine[0] != 0xFCFFFFFF || combine[1] != 0xFFFDF6FB ||
            (otherModes[0] & 0x00300000) || otherModes[1] != 0x00504340) return;
        const std::array<int, 4> clip = {
            std::max(int((words[1] >> 12) & 4095), int((scissor[0] >> 12) & 4095)),
            std::max(int(words[1] & 4095), int(scissor[0] & 4095)),
            std::min(int((words[0] >> 12) & 4095), int((scissor[1] >> 12) & 4095)),
            std::min(int(words[0] & 4095), int(scissor[1] & 4095)) };
        if (clip[0] < clip[2] && clip[1] < clip[3])
            fades.push_back({target, order, clip, primColor});
    }

    void append(const uint32_t *words, unsigned count)
    {
        commands.push_back(count);
        commands.insert(commands.end(), words, words + count);
    }
    void touch()
    {
        if (!target.valid()) return;
        for (const auto &t : touched) if (t.address == target.address) return;
        touched.push_back(target);
    }
    bool font_rectangle(const uint32_t *words, unsigned count)
    {
        // The retail bitmap font leaves X native and expands Y by
        // H + 2*floor(5*H/32). Invert that exact integer operation, rather
        // than shrinking an already rasterized glyph. Unknown draws fall back.
        FontRectangle r;
        if (!target.valid() || !native_font_rectangle(words, count, r)) return false;
        const int w = r.width, h = r.height;
        if (w > int(Width) || h > int(Height)) return false;
        int ax = atlasX, ay = atlasY, row = atlasRow;
        if (ax + w > int(Width)) { ax = 0; ay += row; row = 0; }
        if (ay + h > int(Height)) return false;
        glyphs.push_back({ target, r.x, r.y, w, h, ax, ay,
            { int((scissor[0] >> 12) & 4095), int(scissor[0] & 4095),
              int((scissor[1] >> 12) & 4095), int(scissor[1] & 4095) }, order });
        atlasX = ax + w; atlasY = ay; atlasRow = std::max(row, h);
        touch();
        const uint32_t bind[] = { 0xFF100000 | (Width - 1), ColorBase + 2 * ImageBytes };
        const uint32_t depth[] = { 0xFE000000, DepthBase + 2 * ImageBytes };
        const uint32_t clip[] = { 0xED000000, (Width * 3 << 12) | (Height * 4) };
        // The private renderer already undoes a 3/4 X transform. Encode atlas
        // coordinates in those units; its rounded step recovers exactly 1:1.
        const uint32_t draw[] = { (words[0] & 0xFF000000) | (unsigned((ax + w) * 3) << 12) | unsigned((ay + h) * 4),
            (words[1] & 0xFF000000) | (unsigned(ax * 3) << 12) | unsigned(ay * 4), words[2], 0x05550400 };
        append(bind, 2); append(depth, 2); append(clip, 2); append(draw, 4);
        unsigned outer = 1;
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (*it <= 2) { outer = *it; break; }
        const uint32_t restore[] = { 0xFF100000 | (Width - 1), ColorBase + (outer - 1) * ImageBytes };
        const uint32_t restoreDepth[] = { 0xFE000000, DepthBase + (outer - 1) * ImageBytes };
        append(restore, 2); append(restoreDepth, 2); append(scissor.data(), 2);
        return true;
    }
    // Return true only for a draw that has been routed into a private layer.
    // State changes still go to the main RDP so subsequent scene rendering is
    // identical. Never classify asynchronous RSP commands using a CPU flag.
    bool command(const uint32_t *words, unsigned count, bool marker)
    {
        const unsigned op = (words[0] >> 24) & 63;
        if (op == 0x3C) combine = {words[0], words[1]};
        if (op == 0x2F) otherModes = {words[0], words[1]};
        if (op == 0x3A) primColor = words[1];
        if (op == 0x2D) scissor = { words[0], words[1] };
        if (op == 0x3F)
        {
            const unsigned w = (words[0] & 1023) + 1;
            target = { words[1] & 0xFFFFFF, w, w == 448 ? 336u : 240u };
            if ((words[0] & 0x00F80000) != 0x00100000) target = {}; // RGBA16 only.
            return false;
        }
        if (marker)
        {
            const unsigned id = words[1] & 255;
            const unsigned group = (id + 1) / 2;
            if (id & 1)
            {
                if (group == 5) numberAnchor = int16_t(words[0] & 65535);
                scopes.push_back(group);
                scope = target.valid() ? group : 0;
                if (scope && scope <= 2)
                {
                    layers[scope - 1] = target;
                    touch();
                    const uint32_t bind[] = { 0xFF100000 | (Width - 1), ColorBase + (scope - 1) * ImageBytes };
                    const uint32_t depth[] = { 0xFE000000, DepthBase + (scope - 1) * ImageBytes };
                    append(bind, 2); append(depth, 2);
                }
            }
            else
            {
                scopes.erase(std::remove(scopes.begin(), scopes.end(), group), scopes.end());
                scope = scopes.empty() ? 0 : scopes.back();
                if (scope && scope <= 2)
                {
                    const uint32_t bind[] = { 0xFF100000 | (Width - 1), ColorBase + (scope - 1) * ImageBytes };
                    const uint32_t depth[] = { 0xFE000000, DepthBase + (scope - 1) * ImageBytes };
                    append(bind, 2); append(depth, 2);
                }
            }
            return false;
        }
        if (primitive(op))
        {
            ++order;
            touch();
            if (scope >= 4) return false; // Corrected in place in the scene batch.
            if (scope == 3) return font_rectangle(words, count);
            if (!scope || !target.valid() || target.address != layers[scope - 1].address)
            {
                if (op == 0x36) scene_rectangle(words);
                return false;
            }
            layerOrders[scope - 1] = order;
            append(words, count);
            return true;
        }
        // Preserve all texture loads and RDP state, including across SyncFull.
        // Guest color/depth bindings must never redirect private rendering into
        // the mirrored texture data. Main-image draws are omitted above.
        if (op != 0x3E) append(words, count);
        return false;
    }
    void next()
    {
        commands.clear(); touched.clear(); layers = {}; scope = 0; scopes.clear();
        glyphs.clear(); atlasX = atlasY = atlasRow = 0;
        fades.clear(); layerOrders = {}; order = 0;
        // RDP state persists across SyncFull, just like scissor and target.
    }
};

inline std::array<uint8_t, 3> rgb(uint16_t value)
{
    auto expand = [](unsigned v) { return uint8_t((v << 3) | (v >> 2)); };
    return { expand((value >> 11) & 31), expand((value >> 6) & 31), expand((value >> 1) & 31) };
}
inline Layer extract(const uint8_t *black, const uint8_t *white, unsigned group, Target target)
{
    Layer layer; layer.target = target;
    if (!target.valid()) return layer;
    const auto b = reinterpret_cast<const uint16_t *>(black + ColorBase + group * ImageBytes);
    const auto w = reinterpret_cast<const uint16_t *>(white + ColorBase + group * ImageBytes);
    int x0 = Width, y0 = Height, x1 = -1, y1 = -1;
    const unsigned limitX = (target.width * 4 + 2) / 3;
    for (unsigned y = 0; y < target.height; ++y)
        for (unsigned x = 0; x < limitX; ++x)
        {
            const unsigned i = (y * Width + x) ^ 1;
            if ((b[i] & 0xFFFE) == 0 && (w[i] & 0xFFFE) == 0xFFFE) continue;
            x0 = std::min(x0, int(x)); x1 = std::max(x1, int(x));
            y0 = std::min(y0, int(y)); y1 = std::max(y1, int(y));
        }
    if (x1 < x0) return layer;
    layer.x = x0; layer.y = y0; layer.width = x1 - x0 + 1; layer.height = y1 - y0 + 1;
    layer.pixels.reserve(layer.width * layer.height);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const unsigned i = (y * Width + x) ^ 1;
            Pixel p; p.color = rgb(b[i]); const auto bright = rgb(w[i]);
            for (unsigned c = 0; c < 3; ++c)
                p.transmit[c] = uint8_t(std::max(0, int(bright[c]) - int(p.color[c])));
            layer.pixels.push_back(p);
        }
    return layer;
}

inline Layer extract_glyph(const uint8_t *black, const uint8_t *white, const Glyph &glyph)
{
    Layer layer; layer.target = glyph.target; layer.text = true; layer.clip = glyph.clip;
    layer.order = glyph.order;
    layer.x = glyph.x; layer.y = glyph.y; layer.width = glyph.width; layer.height = glyph.height;
    const auto b = reinterpret_cast<const uint16_t *>(black + ColorBase + 2 * ImageBytes);
    const auto w = reinterpret_cast<const uint16_t *>(white + ColorBase + 2 * ImageBytes);
    for (int y = 0; y < glyph.height; ++y)
        for (int x = 0; x < glyph.width; ++x)
        {
            const unsigned i = ((glyph.atlasY + y) * Width + glyph.atlasX + x) ^ 1;
            Pixel p; p.color = rgb(b[i]); const auto bright = rgb(w[i]);
            for (unsigned c = 0; c < 3; ++c)
                p.transmit[c] = uint8_t(std::max(0, int(bright[c]) - int(p.color[c])));
            layer.pixels.push_back(p);
        }
    return layer;
}

struct Bindings
{
    std::array<FramePtr, 3> frames;
    unsigned next = 0;
    void publish(std::shared_ptr<Frame> frame)
    {
        for (auto &f : frames) if (f && f->target.address == frame->target.address) { f = frame; return; }
        frames[next++ % frames.size()] = frame;
    }
    FramePtr find(uint32_t origin) const
    {
        origin &= 0xFFFFFF;
        for (const auto &f : frames)
            if (f && origin >= f->target.address && origin < f->target.address + f->target.width * f->target.height * 2)
                return f;
        return {};
    }
};

// Invert the final display mapping: the VI and window stretch will bring each
// corrected source pixel back to the same size on both axes. Rasterize at the
// scene's internal scale so 2x/4x/8x scanouts keep fractional HUD placement.
inline RDP::VIOverlay before_vi(const View &view, unsigned scale, double aspect)
{
    RDP::VIOverlay out;
    if (!view.frame || view.frame->layers.empty() || !view.frame->target.valid() ||
        (scale != 1 && scale != 2 && scale != 4 && scale != 8) || aspect <= 0) return out;
    const auto &v = view.vi;
    const unsigned xa = v.xscale & 4095, ya = v.yscale & 4095;
    const double viewW = JfgReticleOverlay::view_width(v.crop, v.lines);
    const double viewH = JfgReticleOverlay::view_height(v.crop, v.lines);
    if (!xa || !ya || viewW <= 0 || viewH <= 0) return out;
    out.origin = view.frame->target.address; out.width = view.frame->target.width;
    out.height = view.frame->target.height; out.scale = scale;
    const int width = out.width * scale, height = out.height * scale;
    out.pixels.resize(size_t(width) * height * 2);
    for (size_t i = 1; i < out.pixels.size(); i += 2) out.pixels[i] = 0x00FFFFFF;
    const double horizontal = viewW / viewH / aspect * xa / ya;
    for (const auto &layer : view.frame->layers)
    {
        const double size = layer.text ? 4.0 / 3.0 : 1.0;
        const double dx = horizontal * size * scale, dy = size * scale;
        const double left = layer.x * (layer.text ? 1.0 : .75) * scale;
        const double top = (layer.y + (layer.text ? layer.height * (1 - size) * .5 : 0)) * scale;
        int cx0 = 0, cy0 = 0, cx1 = width, cy1 = height;
        if (layer.text)
        {
            cx0 = std::max(0, int(std::ceil(layer.clip[0] * scale / 4.0)));
            cy0 = std::max(0, int(std::ceil(layer.clip[1] * scale / 4.0)));
            cx1 = std::min(width, int(std::ceil(layer.clip[2] * scale / 4.0)));
            cy1 = std::min(height, int(std::ceil(layer.clip[3] * scale / 4.0)));
        }
        for (int y = 0; y < layer.height; ++y) for (int x = 0; x < layer.width; ++x)
        {
            const auto &p = layer.pixels[y * layer.width + x];
            if (p.color == std::array<uint8_t, 3>{0, 0, 0} &&
                p.transmit == std::array<uint8_t, 3>{255, 255, 255}) continue;
            const int x0 = std::max(cx0, int(std::round(left + x * dx)));
            const int x1 = std::min(cx1, int(std::round(left + (x + 1) * dx)));
            const int y0 = std::max(cy0, int(std::round(top + y * dy)));
            const int y1 = std::min(cy1, int(std::round(top + (y + 1) * dy)));
            for (int py = y0; py < y1; ++py) for (int px = x0; px < x1; ++px)
            {
                auto *dest = &out.pixels[(size_t(py) * width + px) * 2];
                auto foreground = p.color;
                for (const auto &fade : view.frame->fades)
                {
                    if (fade.target.address != layer.target.address || fade.order <= layer.order ||
                        (px + .5) * 4 < fade.clip[0] * scale || (px + .5) * 4 >= fade.clip[2] * scale ||
                        (py + .5) * 4 < fade.clip[1] * scale || (py + .5) * 4 >= fade.clip[3] * scale) continue;
                    // The scene ALREADY contains this fade. For H(S)=C+T*S
                    // and F(S)=A+B*S, use C'=B*C+(1-T)*A, T'=T. Thus
                    // H'(F(S))=F(H(S)), without fading the scene twice.
                    // Match the RDP's 5-bit blender weight and opaque bypass.
                    const unsigned alpha = fade.rgba & 255;
                    const unsigned weight = alpha == 255 ? 32 : alpha >> 3;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const unsigned rgb = (fade.rgba >> ((3 - c) * 8)) & 255;
                        foreground[c] = uint8_t(std::min(255u, ((32 - weight) * foreground[c] +
                            ((255 - p.transmit[c]) * rgb * weight + 127) / 255 + 16) / 32));
                    }
                }
                uint32_t color = 0, transmit = 0;
                for (unsigned c = 0; c < 3; ++c)
                {
                    const unsigned shift = (2 - c) * 8;
                    color |= std::min(255u, unsigned(foreground[c]) +
                        (p.transmit[c] * ((dest[0] >> shift) & 255) + 127) / 255) << shift;
                    transmit |= (p.transmit[c] * ((dest[1] >> shift) & 255) + 127) / 255 << shift;
                }
                dest[0] = color; dest[1] = transmit;
            }
        }
    }
    return out;
}

// Reference compositor retained for geometry regression checks. Presentation
// now uses before_vi(), so the HUD traverses the real VI filters exactly once.
inline void composite(const View &view, uint32_t *pixels, int width, int height)
{
    if (!view.frame || !pixels || width <= 0 || height <= 0) return;
    const auto &v = view.vi;
    const unsigned xa = v.xscale & 4095, ya = v.yscale & 4095;
    const double cropX = std::round(v.crop * (640.0 / v.lines));
    const double viewW = JfgReticleOverlay::view_width(v.crop, v.lines);
    const double viewH = JfgReticleOverlay::view_height(v.crop, v.lines);
    if (!xa || !ya || viewW <= 0 || viewH <= 0) return;
    const auto &target = view.frame->target;
    const uint32_t offset = ((v.origin & 0xFFFFFF) - target.address) / 2;
    const double ox = offset % target.width, oy = offset / target.width;
    // VI_H_OFFSET/VI_V_OFFSET of the scanout: 108/34 on NTSC, 128/44 on PAL.
    const double hx = int((v.hstart >> 16) & 1023) - (v.lines == 288 ? 128 : 108);
    const double vy = (int((v.vstart >> 16) & 1023) - (v.lines == 288 ? 44 : 34)) / 2.0;
    const double sx = width / viewW, sy = height / viewH;
    auto screenX = [&](double x) { return (hx + ((x - ox) * 1024 - ((v.xscale >> 16) & 4095)) / xa - cropX) * sx; };
    auto screenY = [&](double y) { return (vy + ((y - oy) * 1024 - ((v.yscale >> 16) & 4095)) / ya - v.crop) * sy; };
    for (const auto &layer : view.frame->layers)
    {
        // Preserve the group's anchor through the VI. Within that group both
        // axes use the same final-window pixel size, independent of stretching.
        const double pixel = sy * 1024 / ya * (layer.text ? 4.0 / 3.0 : 1.0);
        const double left = screenX(layer.x * (layer.text ? 1.0 : .75));
        // Match the font's established size and glyph centres, without moving
        // line baselines, other text blocks, shadows or clipping windows.
        const double top = layer.text ? screenY(layer.y + layer.height * .5) - layer.height * pixel * .5 : screenY(layer.y);
        int clipX0 = 0, clipY0 = 0, clipX1 = width, clipY1 = height;
        if (layer.text)
        {
            clipX0 = std::max(0, int(std::ceil(screenX(layer.clip[0] / 4.0))));
            clipY0 = std::max(0, int(std::ceil(screenY(layer.clip[1] / 4.0))));
            clipX1 = std::min(width, int(std::ceil(screenX(layer.clip[2] / 4.0))));
            clipY1 = std::min(height, int(std::ceil(screenY(layer.clip[3] / 4.0))));
        }
        for (int y = 0; y < layer.height; ++y)
            for (int x = 0; x < layer.width; ++x)
            {
                const auto &p = layer.pixels[y * layer.width + x];
                if (p.transmit[0] == 255 && p.transmit[1] == 255 && p.transmit[2] == 255) continue;
                int x0 = std::max(clipX0, int(std::round(left + x * pixel)));
                int x1 = std::min(clipX1, int(std::round(left + (x + 1) * pixel)));
                int y0 = std::max(clipY0, int(std::round(top + y * pixel)));
                int y1 = std::min(clipY1, int(std::round(top + (y + 1) * pixel)));
                for (int py = y0; py < y1; ++py)
                    for (int px = x0; px < x1; ++px)
                    {
                        auto &dst = pixels[py * width + px]; uint32_t out = 0;
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            const unsigned shift = (2 - c) * 8, bg = (dst >> shift) & 255;
                            out |= std::min(255u, unsigned(p.color[c]) + (bg * p.transmit[c] + 127) / 255) << shift;
                        }
                        dst = out;
                    }
            }
    }
}
}
