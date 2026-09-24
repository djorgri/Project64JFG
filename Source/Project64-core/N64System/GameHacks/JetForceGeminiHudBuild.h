#pragma once

// Build translation for the Jet Force Gemini HUD hacks.
//
// The HUD hacks (widescreen correction, HUD alignment, the native HUD raster,
// the Floyd outline, the multiplayer HUD and the rocket reticle) were written
// against the US executable and are spelled in US terms throughout: fixed
// addresses of game code and globals, offsets inside overlays, and MIPS
// listings whose jumps and lui/low pairs encode US addresses. Rather than a
// second copy of every listing, the PAL build runs the same tables through the
// translation below:
//
// - Address() maps a US main-segment address by range. Only the functions and
//   globals the HUD reaches are covered, each range checked against the PAL
//   image word for word (Docs/JFG_PAL_PORT.md); anything else translates to
//   zero, which every caller treats as unavailable.
// - Offset() does the same for an offset inside a relocatable overlay.
// - Relocate() rewrites a listing: jump targets, and every lui/low pair it can
//   follow linearly. A low half whose lui lives in game code (a displaced
//   instruction replayed by a stub) cannot be followed; the few such words are
//   given per build with BuildWord.
//
// On the US build every function here is the identity, so the US hacks run
// exactly the words they always did. The Kiosk demo has no HUD support.
//
// The tables and the build-explicit functions live in
// JetForceGeminiHudBuildMap.h, which the Parallel-RDP plugin shares; this
// header adds the running build and the US-term constant types.

#include "JetForceGeminiAddresses.h"
#include "JetForceGeminiHudBuildMap.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace JfgHudBuild
{
inline BuildId Current(void)
{
    const JFG_ADDRESSES * Addresses = JfgAddresses();
    if (Addresses == &JfgUsAddresses)
    {
        return BuildUs;
    }
    if (Addresses == &JfgPalAddresses)
    {
        return BuildPal;
    }
    return BuildNone;
}

inline uint32_t Address(uint32_t Us)
{
    return AddressFor(Current(), Us);
}

inline uint32_t Offset(uint32_t Module, uint32_t Us)
{
    return OffsetFor(Current(), Module, Us);
}

inline uint32_t Word(uint32_t Word)
{
    return WordFor(Current(), Word);
}

inline bool Relocate(const uint32_t * Us, size_t Count, std::vector<uint32_t> & Out)
{
    return RelocateFor(Current(), Us, Count, Out);
}

inline uint8_t VideoMode(uint8_t Raw)
{
    return VideoModeFor(Current(), Raw);
}

inline bool IsWideVideoMode(uint8_t Raw)
{
    return IsWideVideoModeFor(Current(), Raw);
}

// A US address written as a constant, translated wherever it is used. Use .Us
// for compile-time arithmetic (static_assert); the conversion is a runtime one.
struct UsAddress
{
    uint32_t Us;
    operator uint32_t() const
    {
        return Address(Us);
    }
};

// A US offset inside overlay Module.
struct UsOffset
{
    uint32_t Module;
    uint32_t Us;
    operator uint32_t() const
    {
        return Offset(Module, Us);
    }
};

// A lone j/jal word written in US terms.
struct UsWord
{
    uint32_t Us;
    operator uint32_t() const
    {
        return Word(Us);
    }
};

// A word the two builds spell differently for a reason no translation covers:
// a displaced instruction whose upper half is set in game code, or a site where
// the PAL compile chose another register.
struct BuildWord
{
    uint32_t Us;
    uint32_t Pal;
    operator uint32_t() const
    {
        return Current() == BuildPal ? Pal : Us;
    }
};
} // namespace JfgHudBuild
