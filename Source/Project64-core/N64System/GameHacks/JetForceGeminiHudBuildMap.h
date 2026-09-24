#pragma once

// The US -> PAL translation behind the Jet Force Gemini HUD hacks, shared by
// the core (JetForceGeminiHudBuild.h, which adds the running build) and the
// Parallel-RDP plugin's HUD capture. Self-contained C++14: no core headers.
//
// Every function takes the build explicitly. On the US build each is the
// identity; anything with no PAL counterpart translates to zero (addresses) or
// InvalidOffset (module offsets), which every caller treats as unavailable.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace JfgHudBuild
{
enum BuildId
{
    BuildNone,
    BuildUs,
    BuildPal,
};

// Identifies a build from the CRC pair of its ROM header (offsets 0x10/0x14).
inline BuildId BuildFromRomCrc(uint32_t Crc1, uint32_t Crc2)
{
    if (Crc1 == 0x8A6009B6 && Crc2 == 0x94ACE150)
    {
        return BuildUs; // NJFE
    }
    if (Crc1 == 0x68D7A1DE && Crc2 == 0x0079834A)
    {
        return BuildPal; // NJFP
    }
    return BuildNone;
}

struct RANGE
{
    uint32_t Start;
    uint32_t End;
    int32_t Delta;
};

// US [Start, End) -> PAL, for the main segment. Code ranges span whole
// functions; the PAL build reallocated a few registers inside some of them, so
// a site in a range is not proof of an identical word, and every patched word
// is still checked against its expected original before it is written.
const RANGE PalMainRanges[] =
{
    { 0x80040D64, 0x80040E64, 0x100 }, // camStandardOrtho
    { 0x800416CC, 0x80041860, 0x0F8 }, // camDo2DSprite, before PAL's Y-scale call
    { 0x80041868, 0x800419AC, 0x0FC }, // camDo2DSprite, after it
    { 0x80042134, 0x800421F0, 0x0FC }, // camCopyOrthoMatrix
    { 0x80048D84, 0x80048E20, 0x150 }, // mathMtxF2L
    { 0x800498E8, 0x80049978, 0x150 }, // matrixTranslate
    { 0x8004EDBC, 0x8004F700, 0x220 }, // rcpTileWrite, rcpTileWriteX
    { 0x80054EA0, 0x80054EF0, 0x1D8 }, // viGetCurrentSize
    { 0x80058EF0, 0x80059814, 0x210 }, // frontPrintNum, frontDrawRectangles
    { 0x8005A0D0, 0x8005A51C, 0x210 }, // frontDrawObj
    { 0x80066B80, 0x800683D4, 0x210 }, // diagnostic block holding every HUD cave
    { 0x8006D390, 0x8006D600, 0x254 }, // fxDrawLine, fxDrawLineInWindow
    { 0x8006DAE8, 0x8006E0C4, 0x254 }, // PlotAddRG
    { 0x8006E188, 0x8006EDF0, 0x254 }, // fxOutputLines
    { 0x8006FCEC, 0x800706D4, 0x250 }, // fontPrintWindowXY
    { 0x800A0800, 0x800A3471, 0x270 }, // data: camCopyOrthoMatrix constants
    { 0x800A3530, 0x800AAA21, 0x280 }, // data: video mode scales, players, front end, glyphs
    // PAL inserts a byte after the resolution index: gVideoDeltaTime at
    // 0x800FECAC and its neighbours from 0x800FECAB on move by -0x59E instead.
    { 0x800FDFE0, 0x800FECAB, -0x5A0 }, // bss: overlay table, resolution index
    { 0x800FECB0, 0x800FFE41, -0x5A0 }, // bss: currentScreen, frontgfx, HUD textures, objects
    { 0x80100840, 0x80102D41, -0x5A0 }, // bss: cpuTraceTrackBufStatus padding (HUD scope bytes)
    { 0x80102DC0, 0x80103B95, -0x5A8 }, // bss: CPU line queue index
};

struct OVERLAY_RANGE
{
    uint32_t Module;
    uint32_t Start;
    uint32_t End;
    int32_t Delta;
};

// US module offset [Start, End) -> PAL module offset, for the modules the HUD
// patches: 6 (health), 12 (menu frame), 13 (target reticle), 14 (instruments),
// 61 (multiplayer instruments) and 63 (title logo).
const OVERLAY_RANGE PalOverlayRanges[] =
{
    { 6, 0x0000, 0x15FC, 0 },
    { 12, 0x0000, 0x1BE0, 0 },
    { 13, 0x0000, 0x00CC, 0 },
    { 13, 0x00D4, 0x11C0, -4 },
    { 13, 0x11C0, 0x17D0, 0 },
    { 14, 0x0000, 0x0C70, 0 },
    { 14, 0x0C70, 0x0DD0, 0x10 },
    { 14, 0x0F3C, 0x2F0C, 0x10 },
    { 14, 0x2F0C, 0x3E70, 0x1C },
    { 14, 0x3E70, 0x4884, 0x20 },
    { 61, 0x0000, 0x0334, 0 },
    { 61, 0x0368, 0x2564, 0 },
    { 61, 0x2568, 0x2FA0, 8 },
    { 61, 0x2FA0, 0x3600, 0x10 },
    { 63, 0x0134, 0x0448, 0x58 },
    { 63, 0x0768, 0x0A50, 0xC4 },
};

// An offset no module is large enough to hold: base + this is never RDRAM.
const uint32_t InvalidOffset = 0x7FF00000;

inline uint32_t AddressFor(BuildId Build, uint32_t Us)
{
    if (Build == BuildUs)
    {
        return Us;
    }
    if (Build == BuildPal)
    {
        for (const RANGE & Range : PalMainRanges)
        {
            if (Us >= Range.Start && Us < Range.End)
            {
                return (uint32_t)((int32_t)Us + Range.Delta);
            }
        }
    }
    return 0;
}

inline uint32_t OffsetFor(BuildId Build, uint32_t Module, uint32_t Us)
{
    if (Build == BuildUs)
    {
        return Us;
    }
    if (Build == BuildPal)
    {
        for (const OVERLAY_RANGE & Range : PalOverlayRanges)
        {
            if (Range.Module == Module && Us >= Range.Start && Us < Range.End)
            {
                return (uint32_t)((int32_t)Us + Range.Delta);
            }
        }
    }
    return InvalidOffset;
}

inline uint32_t Hi16(uint32_t Address)
{
    return ((Address >> 16) + ((Address & 0x8000) != 0 ? 1 : 0)) & 0xFFFF;
}

// A lone j or jal: its target is translated. Anything else is returned as is;
// a placeholder whose target is filled at install time (0x08000000,
// 0x0C000000) is left alone. Zero if the target has no translation.
inline uint32_t WordFor(BuildId Build, uint32_t Word)
{
    const uint32_t Opcode = Word >> 26;
    if ((Opcode != 2 && Opcode != 3) || (Word & 0x03FFFFFF) == 0)
    {
        return Word;
    }
    const uint32_t Target = AddressFor(Build, 0x80000000 | ((Word & 0x03FFFFFF) << 2));
    return Target == 0 ? 0 : (Word & 0xFC000000) | ((Target >> 2) & 0x03FFFFFF);
}

// Rewrites a listing for the build. Registers loaded by `lui` with an upper
// half in [0x8000, 0x8080) are followed until they are written again; each
// load, store, addiu or ori through one gets its low half translated, and the
// lui the upper half every such use needs (they must agree). Returns false,
// leaving the output unusable, if any address has no translation or two uses
// of one lui disagree.
inline bool RelocateFor(BuildId Build, const uint32_t * Us, size_t Count, std::vector<uint32_t> & Out)
{
    Out.assign(Us, Us + Count);
    if (Build == BuildUs)
    {
        return true;
    }
    if (Build != BuildPal)
    {
        return false;
    }
    struct TRACKED
    {
        bool Valid;
        uint32_t Hi;
        size_t Index;
    };
    TRACKED Tracked[32] = {};
    std::vector<int32_t> NeedHi(Count, -1);
    for (size_t i = 0; i < Count; i++)
    {
        const uint32_t W = Us[i];
        const uint32_t Opcode = W >> 26;
        const uint32_t Rs = (W >> 21) & 31;
        const uint32_t Rt = (W >> 16) & 31;
        if (W == 0)
        {
            continue;
        }
        if (Opcode == 2 || Opcode == 3)
        {
            const uint32_t Relocated = WordFor(Build, W);
            if (Relocated == 0)
            {
                return false;
            }
            Out[i] = Relocated;
            continue;
        }
        if (Opcode == 0x0F)
        {
            const uint32_t Imm = W & 0xFFFF;
            Tracked[Rt].Valid = Imm >= 0x8000 && Imm < 0x8080;
            Tracked[Rt].Hi = Imm;
            Tracked[Rt].Index = i;
            continue;
        }
        const bool LoadStore = Opcode == 0x20 || Opcode == 0x21 || Opcode == 0x23 || Opcode == 0x24 ||
                               Opcode == 0x25 || Opcode == 0x28 || Opcode == 0x29 || Opcode == 0x2B ||
                               Opcode == 0x31 || Opcode == 0x35 || Opcode == 0x37 || Opcode == 0x39 ||
                               Opcode == 0x3D || Opcode == 0x3F;
        if ((LoadStore || Opcode == 0x09 || Opcode == 0x0D) && Tracked[Rs].Valid)
        {
            const uint32_t Hi = Tracked[Rs].Hi << 16;
            const uint32_t UsAddress = Opcode == 0x0D ? Hi | (W & 0xFFFF)
                                                      : Hi + (uint32_t)(int32_t)(int16_t)(W & 0xFFFF);
            const uint32_t Target = AddressFor(Build, UsAddress);
            if (Target == 0)
            {
                return false;
            }
            const int32_t Upper = (int32_t)(Opcode == 0x0D ? Target >> 16 : Hi16(Target));
            int32_t & Need = NeedHi[Tracked[Rs].Index];
            if (Need >= 0 && Need != Upper)
            {
                return false;
            }
            Need = Upper;
            Out[i] = (W & 0xFFFF0000) | (Target & 0xFFFF);
        }
        // A register written by anything else no longer holds the upper half.
        uint32_t Written = 32;
        if (Opcode == 0)
        {
            Written = (W >> 11) & 31;
        }
        else if ((Opcode >= 0x08 && Opcode <= 0x0E) || (Opcode >= 0x20 && Opcode <= 0x27) || Opcode == 0x37)
        {
            Written = Rt;
        }
        else if (Opcode == 0x11 && Rs <= 2)
        {
            Written = Rt;
        }
        if (Written < 32)
        {
            Tracked[Written].Valid = false;
        }
    }
    for (size_t i = 0; i < Count; i++)
    {
        if (NeedHi[i] >= 0)
        {
            Out[i] = (Out[i] & 0xFFFF0000) | (uint32_t)NeedHi[i];
        }
    }
    return true;
}

// The game's resolution index is 0..3 on NTSC (low, widescreen, medium,
// high-resolution widescreen). viChangeMode adds 8 on a PAL console, so the PAL
// build runs the same four modes as 8..11. Returns 0..3, or 0xFF for any other
// value (boot and reset modes).
inline uint8_t VideoModeFor(BuildId Build, uint8_t Raw)
{
    if (Build == BuildPal)
    {
        return Raw >= 8 && Raw <= 11 ? (uint8_t)(Raw - 8) : 0xFF;
    }
    return Raw <= 3 ? Raw : 0xFF;
}

inline bool IsWideVideoModeFor(BuildId Build, uint8_t Raw)
{
    const uint8_t Mode = VideoModeFor(Build, Raw);
    return Mode == 1 || Mode == 3;
}

} // namespace JfgHudBuild
