#pragma once
#include "rdp_data_structures.hpp"

namespace RDP
{
// Undo JFG's 3/4 horizontal correction before *native* rasterization in a
// private renderer. The scene processor never enables this option. Attributes
// remain attached to their edges; only derivatives with respect to X change.
inline void restore_hud_coordinates(TriangleSetup &setup, AttributeSetup &attr, bool texture_rectangle = false)
{
    auto expand = [](int32_t v) { return int32_t(int64_t(v) * 4 / 3); };
    auto derivative = [](int32_t v) { return int32_t(int64_t(v) * 3 / 4); };
    setup.xh = expand(setup.xh); setup.xm = expand(setup.xm); setup.xl = expand(setup.xl);
    setup.dxhdy = expand(setup.dxhdy); setup.dxmdy = expand(setup.dxmdy); setup.dxldy = expand(setup.dxldy);
    attr.drdx = derivative(attr.drdx); attr.dgdx = derivative(attr.dgdx);
    attr.dbdx = derivative(attr.dbdx); attr.dadx = derivative(attr.dadx);
    attr.dsdx = derivative(attr.dsdx); attr.dtdx = derivative(attr.dtdx);
    attr.dzdx = derivative(attr.dzdx); attr.dwdx = derivative(attr.dwdx);
    if (texture_rectangle)
    {
        // Texrect steps have ten fractional bits. Recover that precision after
        // undoing the patch: 1365 * 3/4 must become 1024, not 1023.75, which
        // would repeat column zero and drop the last column with point sampling.
        auto round_step = [](int32_t v) {
            const int64_t magnitude = v < 0 ? -int64_t(v) : int64_t(v);
            const int32_t rounded = int32_t((magnitude + 1024) / 2048 * 2048);
            return v < 0 ? -rounded : rounded;
        };
        attr.dsdx = round_step(attr.dsdx); attr.dtdx = round_step(attr.dtdx);
    }
}
}
