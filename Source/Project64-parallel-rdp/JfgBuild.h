#pragma once
#include <cstdint>
#include <cstring>
#include "../Project64-core/N64System/GameHacks/JetForceGeminiHudBuildMap.h"

// The Jet Force Gemini build the core is patching. The HUD capture below is
// spelled with US addresses and translated for PAL through the core's shared
// table, so both builds check exactly the words the core installs.
namespace JfgBuild
{
inline JfgHudBuild::BuildId &current()
{
    static JfgHudBuild::BuildId build = JfgHudBuild::BuildUs;
    return build;
}
// Reads the CRC pair at 0x10/0x14 of the ROM header (host-order words, as the
// core hands them over). Any other ROM keeps the US spelling: the core sends
// HUD commands only for a recognised JFG build.
inline void detect(const uint8_t *header)
{
    uint32_t crc1 = 0, crc2 = 0;
    if (header)
    {
        std::memcpy(&crc1, header + 0x10, 4);
        std::memcpy(&crc2, header + 0x14, 4);
    }
    const auto build = JfgHudBuild::BuildFromRomCrc(crc1, crc2);
    current() = build == JfgHudBuild::BuildNone ? JfgHudBuild::BuildUs : build;
}
inline uint32_t address(uint32_t us) { return JfgHudBuild::AddressFor(current(), us); }
inline uint32_t offset(uint32_t module, uint32_t us) { return JfgHudBuild::OffsetFor(current(), module, us); }
inline uint32_t word(uint32_t us) { return JfgHudBuild::WordFor(current(), us); }
// Index of a guest byte in the plugin's RDRAM copy (32-bit host-order words).
inline uint32_t byte(uint32_t us) { return (address(us) & 0x1FFFFFFF) ^ 3; }
inline bool wide(uint8_t raw) { return JfgHudBuild::IsWideVideoModeFor(current(), raw); }
inline bool high_resolution(uint8_t raw) { return (JfgHudBuild::VideoModeFor(current(), raw) & 2) != 0; }
// VI output lines per field: parallel-rdp scans out 240 on NTSC timing and 288
// on PAL timing (VI_V_SYNC beyond 525 + 25 lines, as video_interface.cpp).
inline unsigned vi_lines(uint32_t vSync) { return (vSync & 0x3FF) > 525 + 25 ? 288u : 240u; }
}
