// Independent placement correction for the retail US single-player HUD.
#pragma once

#include "JetForceGeminiHudAlignmentCode.h"
#include "JetForceGeminiHudAlignmentOriginal.h"
#include "JetForceGeminiHudAlignmentRdp.h"
#include "JetForceGeminiHudAlignmentSites.h"
#include <cmath>

namespace JfgHudAlignment
{
constexpr uint32_t CaveStart = 0x800679A0;
constexpr uint32_t CaveEnd = 0x800680A0;
constexpr uint32_t GuardAddress = 0x80067994;
constexpr uint32_t PlayerCountAddress = 0x800A4FD0;
constexpr uint32_t GuardRetired[] = { 0x03E00008, 0x00000000 };

// This is the register-display part of the game's fault handler, not its
// exception logger or boot initializer. Retire its public entry before using
// the body, and restore the complete retail body before restoring that entry.
// Keeping the original words also makes old patched save states reversible.
static_assert(CaveEnd - CaveStart == sizeof(JfgHudAlignmentOriginal::CaveWords),
              "The complete original diagnostic body must be available");
static_assert(JfgHudAlignmentCode::CaveEnd == JfgHudAlignmentRdp::CaveStart,
              "Alignment code segments must be adjacent and disjoint");
static_assert(JfgHudAlignmentRdp::CaveEnd == CaveEnd, "Unexpected alignment cave end");

struct Layout
{
    uint32_t Width, Height;
    float WeaponDx, WeaponDy;
    float HealthArcDx, HealthArcDy;
    float HealthSpriteDx, HealthSpriteDy;
};

inline Layout CalculateLayout(uint8_t ResolutionIndex, bool WidescreenCorrected)
{
    const bool Wide = ResolutionIndex == 1 || ResolutionIndex == 3;
    const bool Corrected = Wide && WidescreenCorrected;
    const uint32_t Width = ResolutionIndex >= 2 ? 448 : 320;
    const uint32_t Height = ResolutionIndex >= 2 ? 336 : 240;
    const float Scale = Corrected ? 0.75f : 1.0f;
    const float Bias = Corrected ? (Width == 320 ? 48.0f : 68.0f) : 0.0f;
    // RDP rectangles have a 10.2 coordinate grid. Use that same quarter-pixel
    // target for matrices, text and clipping. The high-resolution margin is
    // rounded by less than 0.1 logical unit; the low-resolution target is exact.
    const float LogicalMargin = 13.0f * (float)Height / 240.0f;
    // The VI blanks eight output samples at the left edge even with overscan
    // cropping disabled (parallel-rdp/video_interface.cpp: h_start_clamp).
    // JFG's X scale is floor((Width<<9)/320) in 10-bit fractional units:
    // this hides 4 framebuffer pixels at 320, or 5.59375 at 448. Measure the
    // widescreen margin from the visible image by restoring that inset, rounded
    // to our common quarter-pixel grid. Keep the approved 4:3 placement intact.
    const uint32_t ViXScale = (Width << 9) / 320;
    const float ViLeftInset = Wide ?
        std::round(8.0f * (float)ViXScale / 1024.0f * 4.0f) / 4.0f : 0.0f;
    const float TargetX = ViLeftInset +
        std::round(LogicalMargin * (Wide ? 0.75f : 1.0f) * 4.0f) / 4.0f;
    const float TargetY = std::round(LogicalMargin * 4.0f) / 4.0f;
    // The arc's visible rim sits slightly inside its geometric envelope in
    // the approved widescreen captures. Offset the complete health group by
    // two logical display units, independently of the aspect-correction option.
    // This is an optical adjustment, not a change to the arc or sprite shape.
    const float HealthOpticalCompensation = Wide ?
        std::round(2.0f * (float)Height / 240.0f * 0.75f * 4.0f) / 4.0f : 0.0f;
    Layout Result = {};
    Result.Width = Width;
    Result.Height = Height;
    Result.WeaponDx = TargetX - ((float)Width / 2.0f + Scale * (-141.0f - Bias));
    Result.WeaponDy = TargetY - ((float)Height / 2.0f - 107.0f);
    // Use the actual truncated outer vertices (-37..37), not an ideal radius
    // of 38, then apply the widescreen optical adjustment to arc and icon alike.
    Result.HealthArcDx = TargetX - HealthOpticalCompensation -
                         ((float)Width / 2.0f + Scale * (-99.0f - Bias - 37.0f));
    Result.HealthArcDy = (float)Height - TargetY - ((float)Height / 2.0f + 70.0f + 37.0f);
    // Stock instDrawHealth uses icon.X+1, icon.Y-2, but the final screen delta
    // is not exactly (1,2): billboarding adds the local sprite W=1 to the
    // anchor W=Width/2. Account for that division when centring its local
    // origin on the arc, while preserving the asymmetric sprite artwork.
    const float HalfWidth = (float)Width / 2.0f;
    Result.HealthSpriteDx = Result.HealthArcDx +
                           Scale * (HalfWidth - 99.0f - Bias) / (HalfWidth + 1.0f);
    Result.HealthSpriteDy = Result.HealthArcDy + 70.0f -
                           68.0f * HalfWidth / (HalfWidth + 1.0f);
    return Result;
}

inline bool BuildImage(std::vector<uint32_t> & Image, uint32_t Overlay14Base,
                       uint8_t ResolutionIndex, bool WidescreenCorrected)
{
    if (ResolutionIndex > 3)
    {
        return false;
    }
    const Layout Values = CalculateLayout(ResolutionIndex, WidescreenCorrected);
    // HUD viewport X and Y scale are both Width/2, even in 448x336 mode.
    const float NdcScale = 2.0f / (float)Values.Width;
    std::vector<uint32_t> Matrices, Rectangles;
    if (!JfgHudAlignmentCode::BuildImage(
            Matrices, Values.WeaponDx * NdcScale, -Values.WeaponDy * NdcScale,
            Values.HealthArcDx * NdcScale, -Values.HealthArcDy * NdcScale,
            Values.HealthSpriteDx * NdcScale, -Values.HealthSpriteDy * NdcScale,
            (float)Values.Width / 2.0f) ||
        !JfgHudAlignmentRdp::BuildImage(
            Rectangles, Overlay14Base + JfgHudAlignmentSites::WeaponGroupFunctionOffset,
            (int32_t)std::round(Values.WeaponDx * 4.0f),
            (int32_t)std::round(Values.WeaponDy * 4.0f),
            Values.Width * 4, Values.Height * 4))
    {
        return false;
    }
    Image = Matrices;
    Image.insert(Image.end(), Rectangles.begin(), Rectangles.end());
    return Image.size() * sizeof(uint32_t) == CaveEnd - CaveStart;
}

inline bool IsParameterAddress(uint32_t Address)
{
    return (Address >= JfgHudAlignmentCode::WeaponDxNdcAddress &&
            Address < JfgHudAlignmentCode::WeaponDxNdcAddress + 7 * sizeof(uint32_t)) ||
           (Address >= JfgHudAlignmentRdp::ParamsStart &&
            Address < JfgHudAlignmentRdp::ParamsStart + 0x10) ||
           Address == JfgHudAlignmentRdp::BodyCallAddress;
}
} // namespace JfgHudAlignment
