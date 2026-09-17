#pragma once
#include <cstdint>
#include <vector>

namespace RDP
{
// Per-scanout affine overlay: RGB = color + transmission * scene / 255.
// Two packed 0x00RRGGBB words per pixel; identity pixels preserve coverage.
// This is presentation data, never written back into guest RDRAM.
struct VIOverlay
{
    uint32_t origin = 0, width = 0, height = 0, scale = 1;
    std::vector<uint32_t> pixels;
};
}
