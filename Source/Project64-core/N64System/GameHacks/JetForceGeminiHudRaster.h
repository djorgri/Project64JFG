#pragma once

#include "GameHackMemory.h"
#include "JetForceGeminiHudBuild.h"
#include "JetForceGeminiHudPalOriginals.h"
#include "JetForceGeminiHudRasterOriginal.h"
#include <vector>

// Optional isolated native rendering of the retail health, weapon and font
// renderers (US, and PAL through JetForceGeminiHudBuild.h). MMIO notifications
// append ordinary PipeSync markers through the supporting graphics plugin; with
// another plugin the original draw is intact.
namespace JfgHudRaster
{
constexpr uint32_t UsStart = 0x800681D0;
constexpr JfgHudBuild::UsAddress Start = { UsStart }, End = { 0x800682F0 };
constexpr JfgHudBuild::UsAddress HealthEnter = { UsStart + 0x10 }, HealthExit = { UsStart + 0x30 };
constexpr JfgHudBuild::UsAddress WeaponEnter = { UsStart + 0x50 }, WeaponExit = { UsStart + 0x70 };
constexpr JfgHudBuild::UsAddress TextEnter = { UsStart + 0x90 }, TextExit = { UsStart + 0xB0 };
constexpr JfgHudBuild::UsOffset HealthReturn = { 6, 0xC6C };
constexpr JfgHudBuild::UsOffset WeaponEntry = { 14, 0x2940 }, WeaponReturn = { 14, 0x2CB8 };
static_assert(sizeof(JfgHudPal::HudRasterOriginal) == sizeof(Original), "PAL image must pair with the US one");

inline std::vector<uint32_t> OriginalImage(void)
{
    const uint32_t * Words = JfgHudBuild::Current() == JfgHudBuild::BuildPal ? JfgHudPal::HudRasterOriginal : Original;
    return std::vector<uint32_t>(Words, Words + sizeof(Original) / 4);
}
inline uint32_t Jump(uint32_t address) { return 0x08000000 | ((address >> 2) & 0x03FFFFFF); }

inline uint32_t Module(CGameHackMemory &memory, unsigned index, uint32_t length)
{
    uint32_t table = 0, base = 0;
    if (!memory.ReadU32(JfgHudBuild::Address(0x800FEAA0), table) || table < 0x80000000 || (table & 3) ||
        !memory.IsRdramAddress(table, (index + 1) * 0x20) ||
        !memory.ReadU32(table + index * 0x20, base) || base < 0x80000000 || (base & 7) ||
        !memory.IsRdramAddress(base, length)) return 0;
    return base;
}

inline bool TextReady(CGameHackMemory &memory)
{
    uint8_t mode = 0, front = 0, initialized = 0;
    // frontInitMode sets this flag AFTER initializing the selected screen.
    // Module 12 is the shared menu frame (also resident during gameplay).
    // Require its relocated renderer, so the cold-boot diagnostic cannot be
    // used as trampoline storage before initialization has completed.
    const uint32_t menu = Module(memory, 12, 0xA74);
    uint32_t entry = 0, delay = 0;
    return menu && memory.ReadU32(menu + 0xA6C, entry) && entry == 0x27BDFF88 &&
        memory.ReadU32(menu + 0xA70, delay) && delay == 0xAFBF001C &&
        memory.ReadU8(JfgHudBuild::Address(0x800A51A0), initialized) && initialized == 1 &&
        memory.ReadU8(JfgHudBuild::Address(0x800A51B0), front) && front >= 2 && front <= 24 &&
        // The adaptation option is enabled by default; it does not mean that
        // the game is currently widescreen. Require the active wide video
        // mode for menus too. No joy/camera/player-count gate: level-entry
        // captions and multiplayer pause use the same font.
        memory.ReadU8(JfgHudBuild::Address(0x800FECA8), mode) && JfgHudBuild::IsWideVideoMode(mode);
}

inline bool UpdateTitleLogo(CGameHackMemory &memory, CGameHackCodePatcher &patcher, bool enabled)
{
    // PAL's title module is larger (0xD30 bytes of text), and tests the same
    // flag in $t9 rather than $t2 at the branch.
    const uint32_t title = Module(memory, 63, JfgHudBuild::BuildWord{ 0xC60, 0xD30 });
    if (!title) return true; // Never write back into a discarded allocation.
    auto at = [title](uint32_t offset) { return title + JfgHudBuild::Offset(63, offset); };
    const uint32_t tileWrite = JfgHudBuild::Word(0x0C013B6F), tileWriteX = JfgHudBuild::Word(0x0C013C0B);
    const GAME_HACK_CODE_PATCH signature[] = {
        { at(0x140), 0x27BDFF30, 0x27BDFF30 },
        { at(0x144), 0xAFBF0044, 0xAFBF0044 },
        { at(0x964), 0x3C013F40, 0x3C013F40 }, // scale X = .75
        { at(0x974), 0, 0 },
        { at(0x994), tileWrite, tileWrite }, // rcpTileWrite
        { at(0x9B0), 0x3C013F80, 0x3C013F80 }, // scale Y = 1
        { at(0x9D0), tileWriteX, tileWriteX }, // rcpTileWriteX
        // Select the existing scaled logo path only. Its anchor (160, Y),
        // texture list and fade are shared with the stock path. This keeps
        // drawing in the guest RDP, before VI filtering, with no extra layer.
        { at(0x970), JfgHudBuild::BuildWord{ 0x1540000C, 0x1720000C }, 0x1000000C },
    };
    const auto result = patcher.SetEnabled(signature, sizeof(signature) / sizeof(signature[0]), enabled);
    return result != CGameHackCodePatcher::Result_SignatureMismatch &&
        result != CGameHackCodePatcher::Result_MemoryUnavailable;
}

inline std::vector<uint32_t> Image(uint32_t health, uint32_t weapon, bool text = true)
{
    std::vector<uint32_t> image = OriginalImage();
    image[0] = 0x03E00008; image[1] = 0;
    const uint32_t enterHealth[] = {
        0xAFBE0038, 0xAFB70034, 0x3C08B3FF, 0xAD007FE0, Jump(health + 0x10), 0,
    };
    const uint32_t exitHealth[] = { 0x3C08B3FF, 0xAD007FE4, 0x03E00008, 0x27BD0118 };
    const uint32_t enterWeapon[] = {
        0x8C4E0000, 0, 0x3C08B3FF, 0xAD007FE8, Jump(weapon + WeaponEntry + 8), 0,
    };
    const uint32_t exitWeapon[] = { 0x3C08B3FF, 0xAD007FEC, 0x03E00008, 0 };
    auto place = [&image](JfgHudBuild::UsAddress address, const uint32_t *code, size_t count) {
        for (size_t i = 0; i < count; ++i) image[(address.Us - Start.Us) / 4 + i] = code[i];
    };
    if (health && weapon)
    {
        place(HealthEnter, enterHealth, sizeof(enterHealth) / 4);
        place(HealthExit, exitHealth, sizeof(exitHealth) / 4);
        place(WeaponEnter, enterWeapon, sizeof(enterWeapon) / 4);
        place(WeaponExit, exitWeapon, sizeof(exitWeapon) / 4);
    }
    if (text)
    {
        // font's shared renderer takes a Gfx** in a0 (not necessarily the
        // global HUD cursor). Its saved argument at sp+0x80 survives all exits.
        const uint32_t enterText[] = {
            0xAFB30020, 0xAFB00014, 0x3C18B3FF, 0xAF047FD0, Jump(JfgHudBuild::Address(0x8006FDA4)), 0,
        };
        const uint32_t exitText[] = {
            0x8FB80080, 0x3C19B3FF, 0xAF387FD4, 0x03E00008, 0x27BD0080,
        };
        place(TextEnter, enterText, sizeof(enterText) / 4);
        place(TextExit, exitText, sizeof(exitText) / 4);
    }
    image[image.size() - 2] = health;
    image[image.size() - 1] = weapon;
    return image;
}

inline std::vector<GAME_HACK_CODE_PATCH> Hooks(uint32_t health, uint32_t weapon, bool text = true)
{
    std::vector<GAME_HACK_CODE_PATCH> hooks = {
        { health + 8, 0xAFBE0038, Jump(HealthEnter) },
        { health + 12, 0xAFB70034, 0 },
        { health + HealthReturn, 0x03E00008, Jump(HealthExit) },
        { health + HealthReturn + 4, 0x27BD0118, 0 },
        { weapon + WeaponEntry, 0x8C4E0000, Jump(WeaponEnter) },
        { weapon + WeaponEntry + 4, 0, 0 },
        { weapon + WeaponReturn, 0x03E00008, Jump(WeaponExit) },
        { weapon + WeaponReturn + 4, 0, 0 },
    };
    if (!health || !weapon) hooks.clear(); // Menus use only the fixed font hooks.
    if (text)
    {
        hooks.push_back({ JfgHudBuild::Address(0x8006FD9C), 0xAFB30020, Jump(TextEnter) });
        hooks.push_back({ JfgHudBuild::Address(0x8006FDA0), 0xAFB00014, 0 });
        hooks.push_back({ JfgHudBuild::Address(0x800706CC), 0x03E00008, Jump(TextExit) });
        hooks.push_back({ JfgHudBuild::Address(0x800706D0), 0x27BD0080, 0 });
    }
    return hooks;
}

inline bool Update(CGameHackMemory &memory, CGameHackCodePatcher &patcher,
                   bool enabled, uint32_t health, uint32_t weapon, bool textOnly = false)
{
    auto valid = [&memory](uint32_t base, uint32_t length) {
        return base >= 0x80000000 && (base & 7) == 0 && memory.IsRdramAddress(base, length);
    };
    // Zero denotes an unloaded module; a nonzero invalid table entry cannot
    // authorize either installation or disposal of a still-referenced cave.
    if ((health && !valid(health, HealthReturn + 8)) ||
        (weapon && !valid(weapon, WeaponReturn + 8))) return false;
    auto matches = [&memory](const std::vector<uint32_t> &image) {
        for (size_t i = 0; i < image.size(); ++i)
        {
            uint32_t word;
            if (!memory.ReadU32(Start + uint32_t(i * 4), word) || word != image[i]) return false;
        }
        return true;
    };
    std::vector<uint32_t> original = OriginalImage();
    uint32_t oldHealth = 0, oldWeapon = 0;
    memory.ReadU32(End - 8, oldHealth);
    memory.ReadU32(End - 4, oldWeapon);
    const bool oldBasesValid = valid(oldHealth, HealthReturn + 8) && valid(oldWeapon, WeaponReturn + 8);
    const bool ownedTextOnly = oldHealth == 0 && oldWeapon == 0 && matches(Image(0, 0));
    const bool ownedText = ownedTextOnly || (oldBasesValid && matches(Image(oldHealth, oldWeapon)));
    const bool owned = ownedText || (oldBasesValid && matches(Image(oldHealth, oldWeapon, false)));
    if (!owned && !matches(original)) return false;
    if (enabled && !textOnly && (!valid(health, HealthReturn + 8) || !valid(weapon, WeaponReturn + 8)))
        enabled = false;
    if (enabled && !textOnly)
    {
        uint32_t h0 = 0, h1 = 0, w0 = 0, w1 = 0;
        if (!memory.ReadU32(health, h0) || !memory.ReadU32(health + 4, h1) ||
            !memory.ReadU32(weapon + JfgHudBuild::Offset(14, 0x292C), w0) ||
            !memory.ReadU32(weapon + JfgHudBuild::Offset(14, 0x2930), w1) ||
            h0 != 0x27BDFEE8 || h1 != 0xAFBF003C || w0 != 0x27BDFFA0 || w1 != 0xAFBF0024)
            enabled = false;
    }

    // Disconnect only a complete, recognized pair of hooks in each old
    // allocation. An unloaded overlay may already have been replaced by data.
    if (owned && (!enabled || ownedTextOnly != textOnly ||
        (!textOnly && (oldHealth != health || oldWeapon != weapon))))
    {
        auto hooks = Hooks(oldHealth, oldWeapon, ownedText);
        std::vector<GAME_HACK_CODE_PATCH> remove;
        for (size_t i = 0; i < hooks.size(); i += 2)
        {
            // The module table owns allocations; never repair stale memory
            // after the loader has relocated either overlay.
            if (!ownedTextOnly && ((i < 4 && oldHealth != health) || (i >= 4 && i < 8 && oldWeapon != weapon))) continue;
            uint32_t entry = 0, delay = 0;
            if (!memory.ReadU32(hooks[i].Address, entry) ||
                !memory.ReadU32(hooks[i + 1].Address, delay)) return false;
            if (entry != hooks[i].Replacement) continue;
            if (delay != hooks[i + 1].Replacement) return false;
            remove.push_back(hooks[i]); remove.push_back(hooks[i + 1]);
        }
        auto result = patcher.SetEnabled(remove.data(), remove.size(), false);
        if (result == CGameHackCodePatcher::Result_SignatureMismatch ||
            result == CGameHackCodePatcher::Result_MemoryUnavailable) return false;
    }

    std::vector<GAME_HACK_CODE_WRITE> writes;
    const uint32_t nextHealth = textOnly ? 0 : health, nextWeapon = textOnly ? 0 : weapon;
    auto desired = enabled ? Image(nextHealth, nextWeapon) : original;
    auto current = owned ? Image(oldHealth, oldWeapon, ownedText) : original;
    for (size_t i = 0; i < desired.size(); ++i)
        writes.push_back({ Start + uint32_t(i * 4), desired[i], { current[i] }, 1 });
    if (enabled)
        for (auto &hook : Hooks(nextHealth, nextWeapon))
            writes.push_back({ hook.Address, hook.Replacement, { hook.Original, hook.Replacement }, 2 });
    auto result = patcher.Apply(writes.data(), writes.size());
    return result != CGameHackCodePatcher::Result_SignatureMismatch &&
           result != CGameHackCodePatcher::Result_MemoryUnavailable;
}
}
