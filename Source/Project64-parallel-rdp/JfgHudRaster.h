#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "JfgBuild.h"

namespace JfgHudRaster
{
// PipeSync's second word is ignored by the hardware. These markers survive
// the RSP in command order, unlike sampling a CPU scope while it builds a DL.
constexpr uint32_t Marker = 0x4A464700;
inline uint32_t ram_word(const uint8_t *ram, uint32_t address)
{
    uint32_t word; std::memcpy(&word, ram + (address & 0x1FFFFFFF), 4); return word;
}
// Addresses below are the US ones; JfgBuild translates them for PAL.
inline bool multiplayer_ready(const uint8_t *ram, uint32_t size)
{
    using JfgBuild::address;
    if (!ram || size < 0x110000) return false;
    const unsigned mode = ram[JfgBuild::byte(0x800FECA8)], players = ram[JfgBuild::byte(0x800A4FD0)];
    if (!JfgBuild::wide(uint8_t(mode)) || players < 2 || players > 4 ||
        ram_word(ram, address(0x800680B0)) != 0x03E00008 ||
        ram_word(ram, address(0x800680D8)) != 0xAF3D7FC0 ||
        ram_word(ram, address(0x8006810C)) != 0xAF257FC4) return false;
    const uint32_t table = ram_word(ram, address(0x800FEAA0));
    if (table < 0x80000000 || (table & 3) || uint64_t(table & 0x1FFFFFFF) + 62 * 32 > size) return false;
    const uint32_t health = ram_word(ram, table + 6 * 32);
    if (health < 0x80000000 || (health & 7) || uint64_t(health & 0x1FFFFFFF) + 0x10D8 > size ||
        health != ram_word(ram, address(0x800681C0)) || ram_word(ram, health) != 0x27BDFEE8 ||
        ram_word(ram, health + 4) != 0xAFBF003C) return false;
    const uint32_t multi = ram_word(ram, table + 61 * 32);
    return multi >= 0x80000000 && !(multi & 7) && uint64_t(multi & 0x1FFFFFFF) + 0x2FA0 <= size &&
        multi == ram_word(ram, address(0x800681C4)) &&
        ram_word(ram, multi + JfgBuild::offset(61, 0x43C)) == 0x27BDFF38 &&
        ram_word(ram, multi + JfgBuild::offset(61, 0x440)) == 0xAFB50038;
}
inline bool multiplayer_matrix(uint8_t *ram, uint32_t size, uint32_t descriptor)
{
    if (!multiplayer_ready(ram, size) || descriptor < 0x80000000 || (descriptor & 7) ||
        uint64_t(descriptor & 0x1FFFFFFF) + 0x50 > size) return false;
    const uint32_t destination = ram_word(ram, descriptor + 0x10);
    const uint32_t caller = ram_word(ram, descriptor + 0x14);
    const uint32_t multi = ram_word(ram, JfgBuild::address(0x800681C4));
    const uint32_t health = ram_word(ram, JfgBuild::address(0x800681C0));
    // Return addresses of the hooked mathMtxF2L calls (US module offsets).
    bool accepted = caller == health + JfgBuild::offset(6, 0x464);
    for (unsigned offset : {0x1FA4u, 0x220Cu, 0x24A4u, 0x29B8u, 0x2DB8u})
        accepted |= caller == multi + JfgBuild::offset(61, offset);
    if (caller == JfgBuild::address(0x800418E0)) {
        // camDo2DSprite also renders unrelated objects. Its saved caller is
        // at +0x2c, above our wrapper's 0x20-byte frame.
        const uint32_t parent = ram_word(ram, descriptor + 0x4C);
        accepted = parent == JfgBuild::address(0x8005A2F4) || parent == multi + JfgBuild::offset(61, 0x1770) ||
            parent == multi + JfgBuild::offset(61, 0x17DC);
    }
    if (!accepted || destination < 0x80000000 || (destination & 7) ||
        uint64_t(destination & 0x1FFFFFFF) + 64 > size) return false;
    // N64 Mtx stores all integer halves followed by all fractional halves.
    // Scale the three X coefficients, preserving translation, Y, Z and W.
    // This happens before the RSP consumes the matrix and before VI filtering.
    for (unsigned index : {0u, 4u, 8u}) {
        const uint32_t a = destination + index * 2, b = a + 32;
        uint32_t hi = ram_word(ram, a), lo = ram_word(ram, b);
        const int32_t fixed = int32_t((hi & 0xFFFF0000) | (lo >> 16));
        const uint32_t scaled = uint32_t(int64_t(fixed) * 3 / 4);
        hi = (hi & 65535) | (scaled & 0xFFFF0000);
        lo = (lo & 65535) | (scaled << 16);
        std::memcpy(ram + (a & 0x1FFFFFFF), &hi, 4);
        std::memcpy(ram + (b & 0x1FFFFFFF), &lo, 4);
    }
    return true;
}
inline bool multiplayer_radar_point(uint8_t *ram, uint32_t size, uint32_t stack)
{
    if (!multiplayer_ready(ram, size) || stack < 0x80000000 || (stack & 7) ||
        uint64_t(stack & 0x1FFFFFFF) + 0xEC > size) return false;
    const int32_t x = int32_t(ram_word(ram, stack + 0xE0));
    const int32_t center = int32_t(ram_word(ram, stack + 0xE8));
    const int64_t offset = int64_t(x) - center;
    if (offset < -1024 || offset > 1024) return false;
    const int32_t corrected = int32_t(center + offset * 3 / 4);
    std::memcpy(ram + (stack & 0x1FFFFFFF) + 0xE0, &corrected, 4);
    return true;
}
struct State
{
    unsigned pending = 0, drawing = 0;
    bool authorized = false;
    bool sceneText = false;

    // The HUD display-list cursor, gHudDl (US 0x800FF398).
    static uint32_t cursor_pointer() { return JfgBuild::address(0x800FF398); }

    bool notify(unsigned command, uint8_t *ram, uint32_t size)
    {
        return notify(command, ram, size, cursor_pointer());
    }

    bool notify(unsigned command, uint8_t *ram, uint32_t size, uint32_t cursorPointer)
    {
        using JfgBuild::address;
        if (command < 1 || command > 6 || !ram || size < 0x110000 ||
            cursorPointer < 0x80000000 || cursorPointer >= 0x80800000 || (cursorPointer & 3) ||
            uint64_t(cursorPointer & 0x1FFFFFFF) + 4 > size) return false;
        auto word = [ram](uint32_t address) {
            uint32_t value; std::memcpy(&value, ram + (address & 0x1FFFFFFF), 4); return value;
        };
        auto set = [ram](uint32_t address, uint32_t value) {
            std::memcpy(ram + (address & 0x1FFFFFFF), &value, 4);
        };
        const unsigned bit = 1u << ((command - 1) / 2);
        const bool begin = (command & 1) != 0;
        if (begin)
        {
            const auto mode = ram[JfgBuild::byte(0x800FECA8)];
            const bool hud = word(address(0x800681EC)) == 0xAD007FE0 &&
                word(address(0x8006822C)) == 0xAD007FE8 && word(address(0x80068334)) == 0xAD1D7FF0;
            const bool textOnly = command == 5 &&
                word(address(0x800682E8)) == 0 && word(address(0x800682EC)) == 0;
            // The core owns font installation after front-end initialization.
            // Check the active aspect again here: an older state can still
            // contain font hooks until the next core update restores them.
            // Only health/weapon capture requires a single player.
            if (!JfgBuild::wide(mode) || (command != 5 && ram[JfgBuild::byte(0x800A4FD0)] != 1) ||
                word(address(0x800681D0)) != 0x03E00008 || (!hud && !textOnly) ||
                (pending & bit)) return false;
            if (command == 5 && (word(address(0x8006FD9C)) != JfgBuild::word(0x0801A098) ||
                word(address(0x8006826C)) != 0xAF047FD0 || word(address(0x80068288)) != 0xAF387FD4)) return false;
        }
        else if (!(pending & bit)) return false;

        const uint32_t cursor = word(cursorPointer);
        if (cursor < 0x80000000 || cursor >= 0x80800000 || (cursor & 7) ||
            uint64_t(cursor & 0x1FFFFFFF) + 8 > size) return false;
        // Only fonts inside a private health/weapon layer need atlas capture.
        // Pause submenus (including frontMap) keep front mode 16, so a mode
        // number cannot identify their real drawing order.
        const bool orderedText = command == 5 ? !(pending & 3) : sceneText;
        const unsigned markerCommand = (command == 5 || command == 6) && orderedText ? command + 2 : command;
        set(cursor, 0xE7000000);
        set(cursor + 4, Marker | markerCommand);
        set(cursorPointer, cursor + 8);
        if (begin) pending |= bit;
        else pending &= ~bit;
        if (command == 5) sceneText = orderedText;
        if (command == 6) sceneText = false;
        authorized = true;
        return true;
    }

    bool notify_number(unsigned command, uint8_t *ram, uint32_t size, int32_t anchor)
    {
        if ((command != 9 && command != 10) || !ram || size < 0x110000) return false;
        if (command == 9) {
            if ((pending & 16) || !multiplayer_ready(ram, size) || anchor < -1024 || anchor > 1024) return false;
        } else if (!(pending & 16)) return false;
        const uint32_t cursorPointer = cursor_pointer();
        const uint32_t cursor = ram_word(ram, cursorPointer);
        if (cursor < 0x80000000 || (cursor & 7) || uint64_t(cursor & 0x1FFFFFFF) + 8 > size) return false;
        const int width = JfgBuild::high_resolution(ram[JfgBuild::byte(0x800FECA8)]) ? 448 : 320;
        const uint32_t words[] = {0xE7000000u | (command == 9 ? uint16_t((anchor + width / 2) * 4) : 0), Marker | command};
        std::memcpy(ram + (cursor & 0x1FFFFFFF), words, 8);
        const uint32_t next = cursor + 8; std::memcpy(ram + (cursorPointer & 0x1FFFFFFF), &next, 4);
        if (command == 9) pending |= 16; else pending &= ~16u;
        authorized = true;
        return true;
    }

    bool consume(uint32_t first, uint32_t second)
    {
        if (!authorized || (first & 0xFFFF0000) != 0xE7000000 || (second & 0xFFFFFF00) != Marker) return false;
        const unsigned command = second & 255;
        if (command < 1 || command > 10 || (command <= 8 && first != 0xE7000000)) return false;
        const unsigned bit = 1u << ((command - 1) / 2);
        if (command & 1) drawing |= bit;
        else drawing &= ~bit;
        return true;
    }
};
}
