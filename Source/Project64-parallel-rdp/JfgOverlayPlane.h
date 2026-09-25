#pragma once

#include <algorithm>
#include <cstdint>
#include "../../external/parallel-rdp/parallel-rdp/vi_overlay.hpp"

// The HUD and reticle overlays share one presentation plane. The plugin reuses
// it for every scanout: reset() restores only what the previous frame touched,
// and the VI uploads only the rectangle the current frame touched.
namespace JfgOverlayPlane
{
// Half-open pixel rectangle, empty unless x0 < x1 and y0 < y1.
struct Rect
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
    void add(int ax0, int ay0, int ax1, int ay1)
    {
        if (ax0 >= ax1 || ay0 >= ay1) return;
        if (empty()) { *this = {ax0, ay0, ax1, ay1}; return; }
        x0 = std::min(x0, ax0); y0 = std::min(y0, ay0);
        x1 = std::max(x1, ax1); y1 = std::max(y1, ay1);
    }
};

constexpr uint32_t Transparent = 0x00FFFFFF; // Full transmission, no colour.

// A plane without geometry may take that of any framebuffer.
inline bool unused(const RDP::VIOverlay &plane) { return plane.pixels.empty() || !plane.width; }

inline void shape(RDP::VIOverlay &plane, uint32_t origin, uint32_t width, uint32_t height, unsigned scale)
{
    plane.origin = origin; plane.width = width; plane.height = height; plane.scale = scale;
    plane.rect_x = plane.rect_y = plane.rect_width = plane.rect_height = 0;
    const size_t size = size_t(width) * height * scale * scale * 2;
    // reset() already left every pixel of a same-sized plane transparent.
    if (plane.pixels.size() == size) return;
    plane.pixels.assign(size, 0);
    for (size_t i = 1; i < size; i += 2) plane.pixels[i] = Transparent;
}

// Make the pixels the last frame touched transparent again and drop the geometry.
inline void reset(RDP::VIOverlay &plane, const Rect &touched)
{
    if (!unused(plane))
    {
        const size_t stride = size_t(plane.width) * plane.scale;
        for (int y = touched.y0; y < touched.y1; ++y)
            for (int x = touched.x0; x < touched.x1; ++x)
            {
                auto *pixel = &plane.pixels[(y * stride + x) * 2];
                pixel[0] = 0; pixel[1] = Transparent;
            }
    }
    plane.width = plane.height = 0;
    plane.rect_x = plane.rect_y = plane.rect_width = plane.rect_height = 0;
}

// Upload only the touched rectangle; an untouched plane is not presented at all.
inline void publish(RDP::VIOverlay &plane, const Rect &touched)
{
    plane.rect_x = uint32_t(touched.x0); plane.rect_y = uint32_t(touched.y0);
    plane.rect_width = uint32_t(touched.x1 - touched.x0); plane.rect_height = uint32_t(touched.y1 - touched.y0);
}
}
