#pragma once
#include "JetForceGeminiHudRaster.h"
#include <algorithm>

namespace JfgMultiplayerHud
{
// Dormant diagnostic glyph writer. Its only retail caller is the diagnostic
// string writer already retired by HudRaster. Keep its public entry harmless.
// Addresses and module offsets are US ones, translated for PAL on use
// (JetForceGeminiHudBuild.h).
constexpr uint32_t UsStart = 0x800680B0;
constexpr JfgHudBuild::UsAddress Start = { UsStart }, End = { 0x800681D0 };
constexpr JfgHudBuild::UsAddress Matrix = { UsStart + 0x10 }, NumberEnter = { UsStart + 0x50 },
                                 NumberExit = { UsStart + 0x70 };
constexpr JfgHudBuild::UsAddress RadarPoint = { UsStart + 0x90 };
constexpr JfgHudBuild::UsAddress ReticleCapture = { UsStart + 0xA0 }, ReticleSubmit = { UsStart + 0xF8 };
constexpr uint32_t ReticleTag = 0x4A46524D;
constexpr uint32_t HealthModule = 6, MultiModule = 61, ReticleModule = 13;
constexpr uint32_t ReticleCalls[] = {0xC68,0xCF4,0xD48,0xD9C,0xDFC,0xE4C,0xE98}; // module 13
constexpr uint32_t ReticleDelays[] = {0x02C08025,0xAFAB0010,0xAFB10010,0xAFB80010,0xAFB00014,0xAFA30010,0xAFB10010};
constexpr uint32_t Original[] = {
    0x27BDFFB8, 0xAFBF001C, 0xAFB00018, 0xAFA40048,
    0xAFA5004C, 0x00C08025, 0x27A50028, 0x0C0153A8,
    0x27A4002C, 0x8FAE004C, 0x8FAF002C, 0x8FB90048,
    0x01CF0019, 0x3C0C8010, 0x3C02800A, 0x8D8CECB8,
    0x8C426520, 0x24010002, 0x3C09800A, 0x3C05800A,
    0x2529651C, 0x0000C012, 0x03195021, 0x000A5840,
    0x14410003, 0x016C3821, 0x10000007, 0x24A56778,
    0x10400004, 0x3C05800A, 0x3C05800A, 0x10000002,
    0x24A56770, 0x24A56768, 0x24020005, 0x10400020,
    0x24080004, 0x8D2D0000, 0x24060001, 0x11A00003,
    0x00C01025, 0x24060002, 0x00C01025, 0x10C00014,
    0x24C6FFFF, 0x96030000, 0x00E02025, 0x1060000A,
    0x306E0003, 0x000E7840, 0x00AFC021, 0x97190000,
    0x00031882, 0x306AFFFF, 0x24840002, 0x01401825,
    0x1540FFF7, 0xA499FFFE, 0x8FAB002C, 0x00C01025,
    0x000B6040, 0x00EC3821, 0x14C0FFEE, 0x24C6FFFF,
    0x01001025, 0x26100002, 0x1500FFE2, 0x2508FFFF,
    0x8FBF001C, 0x8FB00018, 0x03E00008, 0x27BD0048,
};
static_assert(sizeof(JfgHudPal::MultiplayerHudOriginal) == sizeof(Original), "PAL image must pair with the US one");
inline std::vector<uint32_t> OriginalImage()
{
    const uint32_t *words = JfgHudBuild::Current() == JfgHudBuild::BuildPal ? JfgHudPal::MultiplayerHudOriginal : Original;
    return std::vector<uint32_t>(words, words + 72);
}
inline uint32_t Call(uint32_t address) { return JfgHudRaster::Jump(address) | 0x04000000; }
inline uint32_t At(uint32_t base, uint32_t module, uint32_t offset) { return base + JfgHudBuild::Offset(module, offset); }
// mathMtxF2L, and fxOutputLines' queue load displaced by the reticle submit
// hook: its upper half is set by the game's own lui, so PAL spells it out.
inline uint32_t MatrixF2L() { return JfgHudBuild::Address(0x80048D84); }
inline uint32_t LineQueueLow() { return JfgHudBuild::BuildWord{ 0x24A53B90, 0x24A535E8 }; }
inline std::vector<uint32_t> Image(uint32_t health, uint32_t multi, uint32_t reticle = 0, bool capture = true)
{
    std::vector<uint32_t> image = OriginalImage();
    image[0] = 0x03E00008; image[1] = 0;
    auto place = [&](JfgHudBuild::UsAddress address, std::initializer_list<uint32_t> code) {
        std::copy(code.begin(), code.end(), image.begin() + (address.Us - Start.Us) / 4);
    };
    // Convert first, then adjust only the output matrix. The caller's float
    // matrix can be reused for another radar rotation without double scaling.
    // MMIO receives a descriptor with the destination and original return PC.
    place(Matrix, {0x27BDFFE0, 0xAFBF0014, 0xAFA50010, Call(MatrixF2L()), 0,
        0x3C19B3FF, 0xAF3D7FC0, 0x8FBF0014, 0x03E00008, 0x27BD0020});
    // frontPrintNum's anchor is a1, relative to the full framebuffer centre.
    place(NumberEnter, {0x27BDFF40, 0xAFB30020, 0x3C19B3FF, 0xAF257FC4,
        JfgHudRaster::Jump(JfgHudBuild::Address(0x80058EF8)), 0});
    place(NumberExit, {0x3C19B3FF, 0xAF207FC8, 0x03E00008, 0x27BD00C0});
    place(RadarPoint, {0x3C19B3FF, 0xAF3D7FCC, JfgHudRaster::Jump(At(health, HealthModule, 0x107C)), 0});
    if (capture) {
        place(ReticleCapture, {0x27BDFFD0,0xAFA40000,0xAFA50004,0xAFA60008,
            0xAFA7000C,0x8FA80040,0xAFA80010,0x8FA80044,0xAFA80014,
            0xAFB40018,0xAFB3001C,0xAFA00020,0x3C08B3FF,0xAD1D7FF0,
            0x8FA80020,0x15000003,0x27BD0030,JfgHudBuild::Word(0x0801B563),0,0x03E00008,0});
        place(ReticleSubmit, {0x3C08B3FF,0xAD1D7FF4,JfgHudBuild::Word(0x0801B872),LineQueueLow()});
        image[70] = reticle; image[71] = ReticleTag;
    }
    image[68] = health; image[69] = multi;
    return image;
}
inline std::vector<GAME_HACK_CODE_PATCH> Hooks(uint32_t health, uint32_t multi, uint32_t reticle = 0, bool capture = true)
{
    std::vector<GAME_HACK_CODE_PATCH> hooks = {
        {JfgHudBuild::Address(0x800418D8), Call(MatrixF2L()), Call(Matrix)}, // camDo2DSprite (host checks caller)
        {JfgHudBuild::Address(0x80058EF0), 0x27BDFF40, JfgHudRaster::Jump(NumberEnter)},
        {JfgHudBuild::Address(0x80058EF4), 0xAFB30020, 0},
        {JfgHudBuild::Address(0x800595EC), 0x03E00008, JfgHudRaster::Jump(NumberExit)},
        {JfgHudBuild::Address(0x800595F0), 0x27BD00C0, 0},
    };
    if (health) hooks.push_back({At(health, HealthModule, 0x45C), Call(MatrixF2L()), Call(Matrix)});
    if (multi) for (uint32_t offset : {0x1F9Cu, 0x2204u, 0x249Cu, 0x29B0u, 0x2DB0u})
        hooks.push_back({At(multi, MultiModule, offset), Call(MatrixF2L()), Call(Matrix)});
    if (health && multi) hooks.push_back({At(multi, MultiModule, 0x2188), Call(At(health, HealthModule, 0x107C)), Call(RadarPoint)});
    if (capture) {
        hooks.push_back({JfgHudBuild::Address(0x8006E1C0),0x3C058010,JfgHudRaster::Jump(ReticleSubmit)});
        hooks.push_back({JfgHudBuild::Address(0x8006E1C4),LineQueueLow(),0x3C058010});
        if (reticle) for (unsigned i=0;i<7;++i) {
            const uint32_t call = At(reticle, ReticleModule, ReticleCalls[i]);
            hooks.push_back({call,JfgHudBuild::Word(0x0C01B563),Call(ReticleCapture)});
            hooks.push_back({call+4,ReticleDelays[i],ReticleDelays[i]});
        }
    }
    return hooks;
}
inline bool Update(CGameHackMemory &memory, CGameHackCodePatcher &patcher, bool enabled)
{
    const uint32_t health = JfgHudRaster::Module(memory, 6, 0x10D8);
    const uint32_t multi = JfgHudRaster::Module(memory, 61, 0x2FA0);
    const uint32_t reticle = JfgHudRaster::Module(memory, 13, 0xEA0);
    uint32_t oldHealth = 0, oldMulti = 0, oldReticle = 0, tag = 0, font = 0;
    memory.ReadU32(Start + 70 * 4, oldReticle); memory.ReadU32(Start + 71 * 4, tag);
    const bool oldCapture = tag == ReticleTag;
    if (!oldCapture) oldReticle = 0;
    memory.ReadU32(Start + 68 * 4, oldHealth); memory.ReadU32(Start + 69 * 4, oldMulti);
    auto matches = [&](const std::vector<uint32_t> &image) {
        for (size_t i = 0; i < image.size(); ++i) {
            uint32_t w = 0;
            if (!memory.ReadU32(Start + uint32_t(i * 4), w) || w != image[i]) return false;
        }
        return true;
    };
    const bool owned = oldHealth >= 0x80000000 && oldMulti >= 0x80000000 &&
        memory.IsRdramAddress(oldHealth, 0xC74) && memory.IsRdramAddress(oldMulti, 0x2FA0) &&
        (!oldReticle || memory.IsRdramAddress(oldReticle,0xEA0)) &&
        matches(Image(oldHealth, oldMulti, oldReticle, oldCapture));
    const std::vector<uint32_t> original = OriginalImage();
    if (!owned && !matches(original)) return false;
    uint8_t players = 0, mode = 0;
    uint32_t h0 = 0, h1 = 0, m0 = 0, m1 = 0;
    enabled = enabled && health && multi && memory.ReadU8(JfgHudBuild::Address(0x800A4FD0), players) &&
        players >= 2 && players <= 4 && memory.ReadU8(JfgHudBuild::Address(0x800FECA8), mode) &&
        JfgHudBuild::IsWideVideoMode(mode) &&
        memory.ReadU32(JfgHudRaster::Start, font) && font == 0x03E00008 &&
        memory.ReadU32(health, h0) && h0 == 0x27BDFEE8 &&
        memory.ReadU32(health + 4, h1) && h1 == 0xAFBF003C &&
        memory.ReadU32(At(multi, MultiModule, 0x43C), m0) && m0 == 0x27BDFF38 &&
        memory.ReadU32(At(multi, MultiModule, 0x440), m1) && m1 == 0xAFB50038;
    if (enabled && reticle) {
        const uint32_t signature[] = {0x27BDFF68,0xAFB4002C,0xAFB30028,0xAFB10020};
        for (unsigned i=0;i<4;++i) {
            uint32_t word=0;
            if (!memory.ReadU32(At(reticle,ReticleModule,0x4A8)+i*4,word) || word != signature[i]) return false;
        }
    }
    auto good = [](CGameHackCodePatcher::Result result) {
        return result != CGameHackCodePatcher::Result_SignatureMismatch &&
            result != CGameHackCodePatcher::Result_MemoryUnavailable;
    };
    if (owned && oldMulti == multi && oldHealth != health) {
        // A live radar call still references this cave and its old health
        // helper. Do not free the cave during a partial overlay transition.
        return false;
    }
    if (owned && (!enabled || oldHealth != health || oldMulti != multi || oldReticle != reticle || !oldCapture)) {
        auto remove = Hooks(oldHealth == health ? health : 0, oldMulti == multi ? multi : 0,
            oldReticle == reticle ? reticle : 0, oldCapture);
        auto image = Image(oldHealth, oldMulti, oldReticle, oldCapture);
        for (unsigned i = 0; i < 72; ++i) remove.push_back({Start + i * 4, original[i], image[i]});
        if (!good(patcher.SetEnabled(remove.data(), remove.size(), false))) return false;
    }
    if (!enabled) return true;
    auto install = Hooks(health, multi, reticle);
    auto image = Image(health, multi, reticle);
    for (unsigned i = 0; i < 72; ++i) install.push_back({Start + i * 4, original[i], image[i]});
    return good(patcher.SetEnabled(install.data(), install.size(), true));
}
}
