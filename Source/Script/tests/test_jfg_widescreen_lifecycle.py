"""Compile and execute the real HUD lifecycle methods against mock RDRAM.

The production C++ method bodies and CGameHackCodePatcher implementation are
extracted on every run. Only memory, recompiler invalidation and ROM identity
are replaced. No ROM, emulator or external Python package is required.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HACKS = ROOT / "Project64-core/N64System/GameHacks"
WORKSPACE = ROOT.parent


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    # A forward declaration ends in a semicolon before any brace: skip it and
    # keep looking for the definition.
    while ";" in source[start:opening]:
        start = source.index(signature, start + len(signature))
        opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def translation_unit():
    source = (HACKS / "JetForceGemini.cpp").read_text(encoding="utf-8-sig")
    memory_source = (HACKS / "GameHackMemory.cpp").read_text(encoding="utf-8-sig")
    memory_header = (HACKS / "GameHackMemory.h").read_text(encoding="utf-8-sig")
    constants = source[source.index("const uint32_t WidescreenHudCaveStart"):
                       source.index("// These two words were used by the first experimental landing-skip build")]
    # This helper may precede the constants block as the implementation evolves.
    helper = ""
    if "bool IsWidescreenHudResolution(" in source and "bool IsWidescreenHudResolution(" not in constants:
        helper = function(source, "bool IsWidescreenHudResolution(")
    names = ("SetWidescreenHudReticle", "SetWidescreenHudBanner", "SetWidescreenHudShotGauge", "SetWidescreenHudFloyd",
             "RemoveWidescreenHudOverlayHooks", "PatchWidescreenHud")
    methods = "\n".join(function(source, "bool CJetForceGeminiRuntime::" + name + "(") for name in names)
    lifecycle = function(source, "bool CJetForceGeminiRuntime::PatchWidescreenHud(")
    fixed_fixture = lifecycle[lifecycle.index("    const GAME_HACK_CODE_PATCH LegacyFixedPatches[]"):
                              lifecycle.index("    // Earlier builds patched the framebuffer digit renderer")]
    patcher = memory_source[memory_source.index("CGameHackCodePatcher::CGameHackCodePatcher("):]
    declarations = memory_header[memory_header.index("struct GAME_HACK_CODE_PATCH"):]
    floyd_header = HACKS / "JetForceGeminiFloydHud.h"
    floyd_include = '\n#include "' + floyd_header.as_posix() + '"\n'
    floyd_include += '\n#include "' + (HACKS / "JetForceGeminiRocketOverlay.h").as_posix() + '"\n'
    return MOCKS + declarations + floyd_include + constants + "\n" + helper + RUNTIME + patcher + "\n" + methods + \
        "\nstd::vector<GAME_HACK_CODE_PATCH> FixedFixture() {\n" + fixed_fixture + \
        "\nreturn FixedPatches;\n}\n" + CASES


MOCKS = r'''
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <algorithm>
class CRecompiler {
public:
    enum { Remove_GameHack = 1 };
    void ClearRecompCode_Phys(uint32_t, uint32_t, int) {}
};
class CGameHackMemory {
public:
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x400000, 0);
    bool IsRdramAddress(uint32_t a, uint32_t n = 1) const {
        return a >= 0x80000000 && uint64_t(a) + n <= 0x80400000ULL;
    }
    bool ReadU8(uint32_t a, uint8_t &v) const {
        if (!IsRdramAddress(a)) return false;
        v = bytes[a - 0x80000000]; return true;
    }
    bool WriteU8(uint32_t a, uint8_t v) {
        if (!IsRdramAddress(a)) return false;
        bytes[a - 0x80000000] = v; return true;
    }
    bool ReadU32(uint32_t a, uint32_t &v) const {
        if (!IsRdramAddress(a, 4)) return false;
        v = 0;
        for (unsigned i = 0; i < 4; ++i) v = (v << 8) | bytes[a - 0x80000000 + i];
        return true;
    }
    bool WriteU32(uint32_t a, uint32_t v) {
        if (!IsRdramAddress(a, 4)) return false;
        for (unsigned i = 0; i < 4; ++i) bytes[a - 0x80000000 + i] = uint8_t(v >> (24 - i * 8));
        return true;
    }
    uint32_t Word(uint32_t a) const { uint32_t v = 0; if (!ReadU32(a, v)) throw std::runtime_error("bad read"); return v; }
};
const uint32_t OverlayTableAddress = 0x800FEAA0;
const uint32_t OverlayHeaderSize = 0x20;
const int JfgUsAddresses = 1;
const int JfgOtherAddresses = 2;
bool SupportedRom = true, ExactUsRomMock = true;
const int *JfgAddresses() { return ExactUsRomMock ? &JfgUsAddresses : &JfgOtherAddresses; }
bool IsSupportedRom() { return SupportedRom; }
uint32_t JumpTo(uint32_t a) { return 0x08000000 | ((a >> 2) & 0x03FFFFFF); }
uint32_t CallTo(uint32_t a) { return 0x0C000000 | ((a >> 2) & 0x03FFFFFF); }
'''

RUNTIME = r'''
class CJetForceGeminiRuntime {
public:
    CGameHackMemory &m_Memory;
    CRecompiler *m_Recompiler = nullptr;
    CGameHackCodePatcher m_CodePatcher;
    bool m_WidescreenHudCaveApplied = false;
    bool m_WidescreenHudFixedHooksApplied = false;
    bool m_WidescreenHudOverlayHookApplied = false;
    bool m_WidescreenHudScopeOwned = false;
    uint32_t m_WidescreenHudOverlayBase = 0;
    uint32_t m_WidescreenReticleOverlayBase = 0;
    uint32_t m_WidescreenHudScopeOriginal = 0;
    std::vector<uint32_t> m_WidescreenHudCaveOriginal;
    explicit CJetForceGeminiRuntime(CGameHackMemory &m) : m_Memory(m), m_CodePatcher(m, m_Recompiler) {}
    bool SetWidescreenHudReticle(bool Enabled);
    bool SetWidescreenHudBanner(uint32_t OverlayBase, bool Enabled);
    bool SetWidescreenHudShotGauge(uint32_t OverlayBase, bool Enabled);
    bool SetWidescreenHudFloyd(uint32_t OverlayBase, bool Enabled);
    bool RemoveWidescreenHudOverlayHooks();
    bool PatchWidescreenHud(bool Enabled);
};
'''

CASES = r'''
const uint32_t Table = 0x80180000, Overlay13 = 0x80310000, Overlay14 = 0x80320000;
const uint32_t GaugeCallOffset = 0x2B28, GaugeOriginalCall = 0x0C01657D;
const uint32_t FloydCallOffset = 0x468, FloydOriginalCall = 0x0C01B4E4, FloydDelay = 0xAFB90010;
void Require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<WIDESCREEN_HUD_BANNER_WORD_PATCH> LayoutFixture() {
    std::vector<WIDESCREEN_HUD_BANNER_WORD_PATCH> patches;
    for (const auto &p : WidescreenHudBannerPatches) patches.push_back(p);
    for (const auto &p : WidescreenHudFuelPatches) patches.push_back(p);
    return patches;
}
void Stock(CGameHackMemory &m, uint8_t mode) {
    m.WriteU8(WidescreenHudResolutionIndexAddress, mode);
    m.WriteU32(OverlayTableAddress, Table);
    m.WriteU32(Table + 13 * OverlayHeaderSize, Overlay13);
    m.WriteU32(Table + 14 * OverlayHeaderSize, Overlay14);
    // Some retired diagnostic entries lie inside a cave segment. Populate
    // filler first, then preserve the real fixed-site prologues below.
    for (size_t i = 0; i < WidescreenHudCaveWordCount; ++i)
        m.WriteU32(WidescreenHudCaveWordAddress(i), 0x34000000 | uint32_t(i));
    for (size_t i = 0; i < sizeof(JfgFloydHud::OriginalDiagnosticCode) / sizeof(uint32_t); ++i)
        m.WriteU32(0x800678C4 + uint32_t(i * 4), JfgFloydHud::OriginalDiagnosticCode[i]);
    for (size_t i = 0; i < sizeof(JfgRocketOverlay::Original) / 4; ++i)
        m.WriteU32(JfgRocketOverlay::Start + uint32_t(i * 4), JfgRocketOverlay::Original[i]);
    for (const auto &p : FixedFixture()) m.WriteU32(p.Address, p.Original);
    for (const auto &p : WidescreenHudReticleRasterRetired) m.WriteU32(p.Address, p.Original);
    m.WriteU32(WidescreenHudAmmoEntry + 4, WidescreenHudAmmoDelayOriginal);
    for (const auto &p : WidescreenHudDigitalRetired) m.WriteU32(p.Address, p.Original);
    m.WriteU32(Overlay14 + WidescreenHudOverlayEnterOffset, WidescreenHudOverlayEnterOriginal);
    m.WriteU32(Overlay14 + WidescreenHudOverlayEnterOffset + 4, WidescreenHudOverlayEnterDelayOriginal);
    m.WriteU32(Overlay14 + WidescreenHudOverlayExitOffset, WidescreenHudOverlayExitOriginal);
    m.WriteU32(Overlay14 + WidescreenHudOverlayExitOffset + 4, WidescreenHudOverlayExitDelayOriginal);
    for (const auto &p : LayoutFixture()) m.WriteU32(Overlay14 + p.Offset, p.Original);
    for (const auto &p : WidescreenHudShotGaugePatches) m.WriteU32(Overlay14 + p.Offset, p.Original);
    m.WriteU32(Overlay14 + GaugeCallOffset, GaugeOriginalCall);
    m.WriteU32(Overlay14 + GaugeCallOffset + 4, 0x00003825);
    m.WriteU32(Overlay14 + FloydCallOffset, FloydOriginalCall);
    m.WriteU32(Overlay14 + FloydCallOffset + 4, FloydDelay);
    const uint32_t signature[] = {0x27BDFF68, 0xAFB4002C, 0xAFB30028, 0xAFB10020};
    for (unsigned i = 0; i < 4; ++i) m.WriteU32(Overlay13 + WidescreenHudReticleDrawOffset + i * 4, signature[i]);
    for (const auto &p : WidescreenHudReticleCalls) {
        m.WriteU32(Overlay13 + p.Offset, WidescreenHudReticleLineCallOriginal);
        m.WriteU32(Overlay13 + p.Offset + 4, p.Delay);
    }
}
void CheckFloydDiagnosticStock(const CGameHackMemory &m) {
    for (size_t i = 0; i < sizeof(JfgRocketOverlay::Original) / 4; ++i)
        Require(m.Word(JfgRocketOverlay::Start + uint32_t(i * 4)) == JfgRocketOverlay::Original[i],
                "Rocket diagnostic was not restored");
    for (size_t i = 0; i < sizeof(JfgFloydHud::OriginalDiagnosticCode) / sizeof(uint32_t); ++i)
        Require(m.Word(0x800678C4 + uint32_t(i * 4)) == JfgFloydHud::OriginalDiagnosticCode[i],
                "Floyd diagnostic prologue/body was not fully restored");
}
void CheckOverlay14Stock(const CGameHackMemory &m, uint32_t base = Overlay14) {
    Require(m.Word(base + WidescreenHudOverlayEnterOffset) == WidescreenHudOverlayEnterOriginal, "HUD scope entry remains active");
    Require(m.Word(base + WidescreenHudOverlayExitOffset) == WidescreenHudOverlayExitOriginal, "HUD scope exit remains active");
    for (const auto &p : LayoutFixture())
        Require(m.Word(base + p.Offset) == p.Original, "banner remains patched");
    for (const auto &p : WidescreenHudShotGaugePatches)
        Require(m.Word(base + p.Offset) == p.Original, "gauge remains patched");
    Require(m.Word(base + GaugeCallOffset) == GaugeOriginalCall, "gauge wrapper remains active");
    Require(m.Word(base + FloydCallOffset) == FloydOriginalCall, "Floyd line wrapper remains active");
}
void CheckAllStock(const CGameHackMemory &m, uint32_t base13 = Overlay13, uint32_t base14 = Overlay14) {
    CheckOverlay14Stock(m, base14);
    for (const auto &p : WidescreenHudReticleRasterRetired)
        Require(m.Word(p.Address) == p.Original, "experimental reticle raster remains installed");
    CheckFloydDiagnosticStock(m);
    for (const auto &p : FixedFixture()) Require(m.Word(p.Address) == p.Original, "fixed hook remains active");
    for (const auto &p : WidescreenHudDigitalRetired)
        Require(m.Word(p.Address) == p.Original, "retired digital patch remains active");
    for (const auto &p : WidescreenHudReticleCalls)
        Require(m.Word(base13 + p.Offset) == WidescreenHudReticleLineCallOriginal, "reticle remains patched");
    uint8_t depth = 255;
    Require(m.ReadU8(WidescreenHudScopeDepthAddress, depth) && depth == 0, "scope remains active");
}
void CheckInstalled(const CGameHackMemory &m, uint8_t mode) {
    for (const auto &p : FixedFixture()) Require(m.Word(p.Address) == p.Replacement, "fixture did not install fixed hooks");
    for (const auto &p : WidescreenHudReticleCalls)
        Require(m.Word(Overlay13 + p.Offset) == CallTo(WidescreenHudReticleStub), "fixture did not install reticle");
    for (const auto &p : LayoutFixture())
        Require(m.Word(Overlay14 + p.Offset) == (mode == 3 ? p.HighResolution : p.LowResolution), "fixture did not install banner");
    for (const auto &p : WidescreenHudShotGaugePatches)
        Require(m.Word(Overlay14 + p.Offset) == p.Original, "gauge source coordinates were modified");
    Require(m.Word(Overlay14 + GaugeCallOffset) == CallTo(WidescreenHudShotGaugeWrapperStub),
            "fixture did not install gauge wrapper");
    Require(m.Word(Overlay14 + FloydCallOffset) == CallTo(WidescreenHudFloydLineStub),
            "fixture did not install Floyd line wrapper");
}
// Coordinate Y and colour are guest data, not ownership signatures. Simulate
// a saved table whose X values came from an older build while other fields
// have changed; the runtime must retire only its own horizontal translation.
void SeedGaugeTable(CGameHackMemory &m, bool legacy, bool partial = false) {
    unsigned index = 0;
    for (const auto &p : WidescreenHudShotGaugePatches) {
        const uint32_t x = legacy && (!partial || index % 3 != 0) ? p.Replacement : p.Original;
        const uint32_t y = ((p.Original & 0xFFFF) + 17 + index) & 0xFFFF;
        m.WriteU32(Overlay14 + p.Offset, (x & 0xFFFF0000) | y);
        if (index % 2 != 0) m.WriteU32(Overlay14 + p.Offset + 4, 0x12340000 | index * 0x321);
        ++index;
    }
}
void CheckGaugeTableStockX(const CGameHackMemory &m, uint32_t base = Overlay14) {
    unsigned index = 0;
    for (const auto &p : WidescreenHudShotGaugePatches) {
        const uint32_t word = m.Word(base + p.Offset);
        Require((word & 0xFFFF0000) == (p.Original & 0xFFFF0000), "legacy gauge X remains shifted");
        Require((word & 0xFFFF) == ((p.Original + 17 + index) & 0xFFFF), "gauge migration overwrote guest Y");
        if (index % 2 != 0)
            Require(m.Word(base + p.Offset + 4) == (0x12340000 | index * 0x321), "gauge migration overwrote guest colour");
        ++index;
    }
}
int main(int argc, char **argv) {
    try {
        const std::string scenario = argc > 1 ? argv[1] : "";
        const uint8_t mode = argc > 2 ? uint8_t(std::stoi(argv[2])) : 1;
        CGameHackMemory m;
        Stock(m, mode);
        CJetForceGeminiRuntime live(m);
        if (scenario == "rocket_exclusion_snapshot" || scenario == "rocket_exclusion_snapshot_disabled" ||
            scenario == "rocket_overlay_snapshot" || scenario == "rocket_overlay_snapshot_disabled") {
            Require(live.PatchWidescreenHud(true), "initial fixture failed");
            const bool overlay = scenario.find("rocket_overlay") == 0;
            m.WriteU32(WidescreenHudReticleWeaponStub + 4, overlay ? 0x112803DB : 0x11280116);
            if (!overlay) {
                m.WriteU32(0x8006E1C0, 0x3C058010);
                m.WriteU32(0x8006E1C4, 0x24A53B90);
                for (unsigned i=0;i<sizeof(JfgRocketOverlay::Original)/4;++i)
                    m.WriteU32(JfgRocketOverlay::Start+i*4,JfgRocketOverlay::Original[i]);
            }
            CJetForceGeminiRuntime fresh(m);
            const bool enable = scenario == "rocket_exclusion_snapshot" || scenario == "rocket_overlay_snapshot";
            Require(fresh.PatchWidescreenHud(enable), "excluded reticle snapshot migration failed");
            if (enable) {
                Require(m.Word(WidescreenHudReticleWeaponStub+4)==0x100003DB,"capture branch missing");
                Require(fresh.PatchWidescreenHud(false),"disable migrated overlay failed");
            }
            CheckAllStock(m);
        } else if (scenario == "reticle_old_snapshot" || scenario == "reticle_old_snapshot_disabled") {
            Require(live.PatchWidescreenHud(true), "initial reticle fixture failed");
            for (const auto &p : WidescreenHudReticleRasterRetired) m.WriteU32(p.Address, p.Replacement);
            for (unsigned i = 0; i < sizeof(WidescreenHudReticleLegacyCode) / sizeof(uint32_t); ++i)
                m.WriteU32(WidescreenHudReticleStub + i * 4, WidescreenHudReticleLegacyCode[i]);
            CJetForceGeminiRuntime fresh(m);
            const bool enabled = scenario == "reticle_old_snapshot";
            Require(fresh.PatchWidescreenHud(enabled), "legacy reticle migration failed");
            for (const auto &p : WidescreenHudReticleRasterRetired)
                Require(m.Word(p.Address) == p.Original, "legacy raster was not restored");
            if (enabled) {
                CheckInstalled(m, mode);
                for (unsigned i = 0; i < sizeof(WidescreenHudReticleCode) / sizeof(uint32_t); ++i)
                    Require(m.Word(WidescreenHudReticleStub + i * 4) == WidescreenHudReticleCode[i],
                            "legacy snapshot did not acquire the weapon guard");
                Require(fresh.PatchWidescreenHud(false), "reticle migrated disable failed");
            }
            CheckAllStock(m);
        } else if (scenario == "fuel_foreign") {
            m.WriteU32(Overlay14 + WidescreenHudFuelPatches[2].Offset, 0xDEADBEEF);
            const auto before = m.bytes;
            Require(!live.PatchWidescreenHud(true), "foreign fuel instruction was accepted");
            for (const auto &p : LayoutFixture())
                for (unsigned byte = 0; byte < 4; ++byte) {
                    const size_t offset = Overlay14 + p.Offset + byte - 0x80000000;
                    Require(m.bytes[offset] == before[offset], "layout was partially overwritten");
                }
            Require(m.Word(Overlay14 + WidescreenHudOverlayEnterOffset) == WidescreenHudOverlayEnterOriginal,
                    "foreign fuel acquired a HUD scope");
            // Conservative cleanup retains caves while a local signature is
            // unknown; it must finish once the original instruction returns.
            const auto &p = WidescreenHudFuelPatches[2];
            m.WriteU32(Overlay14 + p.Offset, p.Original);
            Require(live.PatchWidescreenHud(false), "fuel cleanup could not resume");
            CheckAllStock(m);
        } else if (scenario == "fuel_old_snapshot") {
            Require(live.PatchWidescreenHud(true), "initial fixture failed");
            // Pre-fuel-fix snapshots contain all other HUD hooks.
            for (const auto &p : WidescreenHudFuelPatches) m.WriteU32(Overlay14 + p.Offset, p.Original);
            CJetForceGeminiRuntime fresh(m);
            Require(fresh.PatchWidescreenHud(true), "old fuel snapshot migration failed");
            CheckInstalled(m, mode);
            m.WriteU8(WidescreenHudResolutionIndexAddress, mode ^ 2);
            Require(fresh.PatchWidescreenHud(true), "fuel resolution change failed");
            CheckInstalled(m, mode ^ 2);
            Require(fresh.PatchWidescreenHud(false), "fuel cleanup failed");
            CheckAllStock(m);
        } else if (scenario == "gauge_legacy_transition") {
            Require(live.PatchWidescreenHud(true), "initial gauge installation failed");
            SeedGaugeTable(m, true);
            m.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
            Require(live.PatchWidescreenHud(true), "legacy gauge transition cleanup failed");
            CheckGaugeTableStockX(m);
            Require(m.Word(Overlay14 + GaugeCallOffset) == GaugeOriginalCall, "gauge wrapper remains active in 4:3");
            // Repeated toggles must neither accumulate a shift nor freeze the
            // coordinates into the widescreen mode that produced the state.
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                m.WriteU8(WidescreenHudResolutionIndexAddress, mode);
                Require(live.PatchWidescreenHud(true), "gauge re-enable failed");
                CheckGaugeTableStockX(m);
                m.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
                Require(live.PatchWidescreenHud(true), "gauge repeated cleanup failed");
                CheckGaugeTableStockX(m);
            }
        } else if (scenario == "gauge_legacy_snapshot" || scenario == "gauge_legacy_snapshot_enable") {
            const auto stockImage = m.bytes;
            Require(live.PatchWidescreenHud(true), "initial snapshot fixture failed");
            SeedGaugeTable(m, true, true);
            m.WriteU32(Overlay14 + GaugeCallOffset, GaugeOriginalCall);
            m.WriteU32(Overlay14 + FloydCallOffset, FloydOriginalCall);
            for (const auto &p : JfgFloydHud::FixedPatches) m.WriteU32(p.Address, p.Original);
            for (unsigned i = 0; i < sizeof(WidescreenHudRectangleLegacyCode) / sizeof(uint32_t); ++i)
                m.WriteU32(WidescreenHudRectangleStub + 4 * i, WidescreenHudRectangleLegacyCode[i]);
            // The earlier build had only the reticle in this segment; restore
            // the newly claimed tail to the untouched diagnostic image.
            for (uint32_t address = WidescreenHudShotGaugeWrapperStub;
                 address < WidescreenHudReticleCaveEnd; ++address)
                m.bytes[address - 0x80000000] = stockImage[address - 0x80000000];
            CGameHackMemory restored;
            restored.bytes = m.bytes;
            const bool enable = scenario == "gauge_legacy_snapshot_enable";
            if (!enable) restored.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
            CJetForceGeminiRuntime fresh(restored);
            Require(fresh.PatchWidescreenHud(enable), "legacy snapshot migration failed");
            CheckGaugeTableStockX(restored);
            Require(restored.Word(Overlay14 + GaugeCallOffset) ==
                    (enable ? CallTo(WidescreenHudShotGaugeWrapperStub) : GaugeOriginalCall),
                    "legacy snapshot retained wrong gauge call");
            if (!enable) CheckFloydDiagnosticStock(restored);
        } else if (scenario == "gauge_orphan" || scenario == "gauge_orphan_partial" ||
                   scenario == "gauge_orphan_enable") {
            SeedGaugeTable(m, true, scenario == "gauge_orphan_partial");
            Require(live.PatchWidescreenHud(scenario == "gauge_orphan_enable"), "orphan gauge migration failed");
            CheckGaugeTableStockX(m);
            const uint32_t call = scenario == "gauge_orphan_enable" ? CallTo(WidescreenHudShotGaugeWrapperStub) : GaugeOriginalCall;
            Require(m.Word(Overlay14 + GaugeCallOffset) == call, "orphan migration left wrong gauge call");
        } else if (scenario == "gauge_foreign") {
            SeedGaugeTable(m, true, true);
            const auto &p = WidescreenHudShotGaugePatches[5];
            m.WriteU32(Overlay14 + p.Offset, (m.Word(Overlay14 + p.Offset) & 0xFFFF) | 0x01230000);
            const auto before = m.bytes;
            Require(!live.PatchWidescreenHud(true), "foreign gauge X was accepted");
            for (const auto &word : WidescreenHudShotGaugePatches)
                for (unsigned byte = 0; byte < 4; ++byte)
                    Require(m.bytes[Overlay14 + word.Offset + byte - 0x80000000] ==
                            before[Overlay14 + word.Offset + byte - 0x80000000], "foreign gauge table was partially overwritten");
            Require(m.Word(Overlay14 + GaugeCallOffset) == GaugeOriginalCall, "foreign gauge acquired a wrapper");
        } else if (scenario == "gauge_non_us" || scenario == "gauge_unsupported") {
            SeedGaugeTable(m, true);
            const auto before = m.bytes;
            SupportedRom = scenario != "gauge_unsupported";
            ExactUsRomMock = scenario != "gauge_non_us";
            Require(live.PatchWidescreenHud(false), "unsupported gauge cleanup returned failure");
            Require(m.bytes == before, "orphan gauge cleanup modified unsupported ROM");
        } else if (scenario == "floyd_invalid_delay") {
            m.WriteU32(Overlay14 + FloydCallOffset + 4, 0xAFB80010);
            const auto before = m.bytes;
            Require(!live.PatchWidescreenHud(true), "foreign Floyd delay was accepted");
            Require(m.bytes == before, "failed Floyd install did not roll back other hooks");
        } else if (scenario == "floyd_owned_bad_delay") {
            Require(live.PatchWidescreenHud(true), "initial Floyd installation failed");
            const auto caveImage = m.bytes;
            m.WriteU32(Overlay14 + FloydCallOffset + 4, 0xAFB80010);
            Require(!live.PatchWidescreenHud(false), "owned Floyd call with foreign delay was disconnected");
            Require(m.Word(Overlay14 + FloydCallOffset) == CallTo(WidescreenHudFloydLineStub),
                    "Floyd call with unknown delay was overwritten");
            Require(m.Word(Overlay14 + GaugeCallOffset) == GaugeOriginalCall,
                    "Floyd failure prevented independent gauge cleanup");
            for (const auto &p : LayoutFixture())
                Require(m.Word(Overlay14 + p.Offset) == p.Original,
                        "Floyd failure prevented independent banner cleanup");
            for (size_t i = 0; i < WidescreenHudCaveWordCount; ++i)
                for (unsigned byte = 0; byte < 4; ++byte) {
                    const size_t offset = WidescreenHudCaveWordAddress(i) + byte - 0x80000000;
                    Require(m.bytes[offset] == caveImage[offset], "referenced Floyd cave was restored early");
                }
            m.WriteU32(Overlay14 + FloydCallOffset + 4, FloydDelay);
            Require(live.PatchWidescreenHud(false), "Floyd cleanup could not resume after delay restored");
            CheckAllStock(m);
        } else if (scenario == "floyd_foreign_diagnostic_owned" || scenario == "floyd_foreign_diagnostic_snapshot") {
            Require(live.PatchWidescreenHud(true), "initial diagnostic fixture failed");
            const uint32_t modified = JfgFloydHud::InitStub + 4;
            m.WriteU32(modified, 0xDEADBEEF);
            if (scenario == "floyd_foreign_diagnostic_snapshot") {
                const auto before = m.bytes;
                CJetForceGeminiRuntime fresh(m);
                Require(!fresh.PatchWidescreenHud(false), "unknown saved diagnostic body was adopted");
                Require(m.bytes == before, "unknown saved diagnostic body was modified");
            } else {
                Require(!live.PatchWidescreenHud(false), "unknown diagnostic body was restored");
                for (size_t i = 0; i < sizeof(JfgFloydHud::GuardCode) / sizeof(uint32_t); ++i)
                    Require(m.Word(JfgFloydHud::GuardStub + uint32_t(i * 4)) == JfgFloydHud::GuardCode[i],
                            "diagnostic entry reopened over unknown helper body");
                Require(m.Word(modified) == 0xDEADBEEF, "unknown diagnostic word was overwritten");
                m.WriteU32(modified, JfgFloydHud::InitCode[1]);
                Require(live.PatchWidescreenHud(false), "diagnostic cleanup failed after recognized body restored");
                CheckAllStock(m);
            }
        } else if (scenario == "floyd_gauge_cleanup_failure") {
            Require(live.PatchWidescreenHud(true), "initial Floyd fixture failed");
            const auto &p = WidescreenHudShotGaugePatches[5];
            m.WriteU32(Overlay14 + p.Offset, 0x12340001);
            Require(!live.PatchWidescreenHud(false), "foreign gauge table was accepted during cleanup");
            Require(m.Word(Overlay14 + FloydCallOffset) == FloydOriginalCall,
                    "gauge cleanup failure left independent Floyd call installed");
            Require(m.Word(Overlay14 + p.Offset) == 0x12340001, "foreign gauge was overwritten");
            m.WriteU32(Overlay14 + p.Offset, p.Original);
            Require(live.PatchWidescreenHud(false), "cleanup failed after gauge repaired");
            CheckAllStock(m);
        } else if (scenario == "floyd_toggles") {
            for (unsigned repeat = 0; repeat < 4; ++repeat) {
                m.WriteU8(WidescreenHudResolutionIndexAddress, mode);
                Require(live.PatchWidescreenHud(true), "Floyd toggle installation failed");
                CheckInstalled(m, mode);
                m.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
                Require(live.PatchWidescreenHud(true), "Floyd mode toggle cleanup failed");
                CheckAllStock(m);
            }
        } else if (scenario == "stock") {
            const auto before = m.bytes;
            live.PatchWidescreenHud(true);
            Require(m.bytes == before, "stock non-wide mode was modified");
            CheckAllStock(m);
        } else if (scenario.rfind("digital", 0) == 0) {
            const auto stockImage = m.bytes;
            for (const auto &p : WidescreenHudDigitalRetired) m.WriteU32(p.Address, p.Replacement);
            if (scenario.find("jump") != std::string::npos)
                m.WriteU32(WidescreenHudDigitalAdvanceEntry, JumpTo(WidescreenHudDigitalAdvanceStub));
            const auto patchedImage = m.bytes;
            SupportedRom = scenario.find("unsupported") == std::string::npos;
            ExactUsRomMock = scenario.find("non_us") == std::string::npos;
            const bool enabled = scenario.find("disabled") == std::string::npos;
            Require(live.PatchWidescreenHud(enabled), "unowned digital patch cleanup failed");
            if (SupportedRom && ExactUsRomMock) {
                CheckAllStock(m);
                Require(m.bytes == stockImage, "digital cleanup modified unrelated memory");
            } else {
                Require(m.bytes == patchedImage, "digital cleanup modified an unsupported or non-US ROM");
            }
        } else if (scenario == "foreign") {
            m.WriteU32(WidescreenHudCamCopyEntry, 0xDEADBEEF);
            const auto before = m.bytes;
            Require(!live.PatchWidescreenHud(true), "foreign signature was accepted");
            Require(m.bytes == before, "foreign installation modified guest memory");
        } else {
            Require(live.PatchWidescreenHud(true), "initial installation failed");
            CheckInstalled(m, mode);
            if (scenario == "transition") {
                m.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
                Require(live.PatchWidescreenHud(true), "mode transition cleanup failed");
                CheckAllStock(m);
            } else if (scenario == "adopt" || scenario == "adopt_disabled" || scenario == "adopt_enabled") {
                CGameHackMemory restored;
                restored.bytes = m.bytes;
                // A snapshot can interrupt an active guest scope. Ownership is
                // absent on the fresh host, so cleanup must adopt known hooks.
                restored.WriteU8(WidescreenHudScopeDepthAddress, 1);
                if (scenario == "adopt") restored.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
                CJetForceGeminiRuntime fresh(restored);
                Require(fresh.PatchWidescreenHud(scenario != "adopt_disabled"), "fresh snapshot cleanup failed");
                if (scenario == "adopt_enabled") {
                    CheckInstalled(restored, mode);
                    Require(fresh.PatchWidescreenHud(false), "fresh enabled snapshot could not uninstall");
                }
                CheckAllStock(restored);
                // An adopted cave image may remain inert; all entry points and
                // data edits must nevertheless be disconnected/restored.
            } else if (scenario == "relocated") {
                const uint32_t moved13 = 0x80350000, moved14 = 0x80360000;
                const size_t oldBegin = Overlay13 - 0x80000000, oldEnd = Overlay14 - 0x80000000 + 0x10000;
                std::copy(m.bytes.begin() + oldBegin, m.bytes.begin() + oldBegin + 0x10000,
                          m.bytes.begin() + moved13 - 0x80000000);
                std::copy(m.bytes.begin() + Overlay14 - 0x80000000, m.bytes.begin() + oldEnd,
                          m.bytes.begin() + moved14 - 0x80000000);
                std::fill(m.bytes.begin() + oldBegin, m.bytes.begin() + oldEnd, 0xA5);
                const std::vector<uint8_t> oldImage(m.bytes.begin() + oldBegin, m.bytes.begin() + oldEnd);
                m.WriteU32(Table + 13 * OverlayHeaderSize, moved13);
                m.WriteU32(Table + 14 * OverlayHeaderSize, moved14);
                m.WriteU8(WidescreenHudResolutionIndexAddress, mode & ~1);
                Require(live.PatchWidescreenHud(true), "relocated overlay cleanup failed");
                CheckAllStock(m, moved13, moved14);
                Require(std::equal(oldImage.begin(), oldImage.end(), m.bytes.begin() + oldBegin),
                        "cleanup wrote to remembered overlay addresses after relocation");
            } else if (scenario == "bad_reticle") {
                m.WriteU32(Overlay13 + WidescreenHudReticleDrawOffset, 0xDEADBEEF);
                Require(!live.PatchWidescreenHud(false), "foreign reticle prologue was accepted");
                Require(m.Word(Overlay13 + WidescreenHudReticleDrawOffset) == 0xDEADBEEF, "foreign prologue overwritten");
                CheckOverlay14Stock(m);
            } else throw std::runtime_error("unknown scenario");
        }
        std::cout << "PASS " << scenario << " mode=" << unsigned(mode) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
'''


def compiler_command(directory, source, executable):
    if os.name != "nt":
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        if compiler:
            return [compiler, "-std=c++17", str(source), "-o", str(executable)]
        raise unittest.SkipTest("C++ compiler unavailable")
    setup = ""
    if not shutil.which("cl"):
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
            "Microsoft Visual Studio/Installer/vswhere.exe"
        if not vswhere.is_file():
            raise unittest.SkipTest("MSVC/vswhere unavailable")
        found = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                               capture_output=True, text=True, check=True).stdout.strip()
        if not found:
            raise unittest.SkipTest("MSVC C++ tools unavailable")
        setup = 'call "' + str(Path(found) / "Common7/Tools/VsDevCmd.bat") + '" -arch=x64 -host_arch=x64 >nul\nif errorlevel 1 exit /b 1\n'
    batch = directory / "compile.cmd"
    batch.write_text('@echo off\n' + setup + 'cl /nologo /EHsc /std:c++17 "' + str(source) + '" /Fe:"' + str(executable) + '"\n', encoding="utf-8")
    return ["cmd.exe", "/d", "/c", str(batch)]


class JfgWidescreenLifecycleTests(unittest.TestCase):
    def test_previous_rocket_exclusion_snapshot_is_migrated_and_restored(self):
        self.scenario("rocket_exclusion_snapshot", (1, 3))
        self.scenario("rocket_exclusion_snapshot_disabled", (1, 3))
        self.scenario("rocket_overlay_snapshot", (1, 3))
        self.scenario("rocket_overlay_snapshot_disabled", (1, 3))

    @classmethod
    def setUpClass(cls):
        build = WORKSPACE / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="jfg-lifecycle-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name).resolve()
        assert directory.is_relative_to(build.resolve())
        source = directory / "lifecycle.cpp"
        source.write_text(translation_unit(), encoding="utf-8")
        cls.executable = directory / ("lifecycle.exe" if os.name == "nt" else "lifecycle")
        command = compiler_command(directory, source, cls.executable)
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError("C++ lifecycle harness failed to compile:\n" + result.stdout + result.stderr)

    def scenario(self, name, modes):
        for mode in modes:
            with self.subTest(scenario=name, mode=mode):
                result = subprocess.run([str(self.executable), name, str(mode)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_previous_reticle_raster_is_retired_on_enable_and_disable(self):
        self.scenario("reticle_old_snapshot", (1, 3))
        self.scenario("reticle_old_snapshot_disabled", (1, 3))

    def test_existing_installation_tracks_widescreen_to_four_three(self):
        self.scenario("transition", (1, 3))

    def test_stock_non_wide_modes_never_install(self):
        self.scenario("stock", (0, 2, 12, 13, 14))

    def test_fresh_host_cleans_patched_snapshot_in_four_three(self):
        self.scenario("adopt", (1, 3))

    def test_fresh_host_cleans_patched_snapshot_when_disabled(self):
        self.scenario("adopt_disabled", (1, 3))

    def test_fresh_enabled_snapshot_restores_complete_diagnostic_function_on_disable(self):
        self.scenario("adopt_enabled", (1, 3))

    def test_fuel_layout_preserves_foreign_code_and_migrates_previous_snapshots(self):
        self.scenario("fuel_foreign", (1, 3))
        self.scenario("fuel_old_snapshot", (1, 3))

    def test_foreign_fixed_signature_is_preserved_without_partial_installation(self):
        self.scenario("foreign", (1, 3))

    def test_bad_reticle_signature_does_not_block_banner_and_gauge_cleanup(self):
        self.scenario("bad_reticle", (1, 3))

    def test_relocated_overlays_are_cleaned_without_writing_stale_addresses(self):
        self.scenario("relocated", (1, 3))

    def test_unowned_retired_digital_constants_are_restored(self):
        self.scenario("digital", (0, 2))
        self.scenario("digital_disabled", (1, 3))

    def test_unowned_retired_digital_trampoline_is_restored(self):
        self.scenario("digital_jump", (0, 2))
        self.scenario("digital_jump_disabled", (1, 3))

    def test_retired_digital_cleanup_requires_exact_supported_us_rom(self):
        for rom in ("unsupported", "non_us"):
            for variant in ("", "_jump"):
                self.scenario("digital_" + rom + variant, (0, 1, 2, 3))

    def test_legacy_gauge_coordinates_preserve_y_and_colour_across_repeated_mode_changes(self):
        self.scenario("gauge_legacy_transition", (1, 3))

    def test_orphan_legacy_gauge_table_is_restored_when_checkbox_is_off(self):
        self.scenario("gauge_orphan", (0, 1, 2, 3))
        self.scenario("gauge_orphan_partial", (0, 1, 2, 3))

    def test_legacy_rectangle_cave_snapshot_is_migrated_without_changing_guest_y(self):
        self.scenario("gauge_legacy_snapshot", (1, 3))
        self.scenario("gauge_legacy_snapshot_enable", (1, 3))

    def test_orphan_legacy_gauge_migrates_to_draw_time_hook_when_enabled(self):
        self.scenario("gauge_orphan_enable", (1, 3))

    def test_foreign_gauge_coordinates_are_preserved_and_cannot_install(self):
        self.scenario("gauge_foreign", (1, 3))

    def test_orphan_gauge_migration_requires_exact_supported_us_rom(self):
        self.scenario("gauge_non_us", (0, 1, 2, 3))
        self.scenario("gauge_unsupported", (0, 1, 2, 3))

    def test_floyd_installation_rolls_back_all_hooks_when_call_delay_is_unknown(self):
        self.scenario("floyd_invalid_delay", (1, 3))

    def test_owned_floyd_call_keeps_cave_until_safe_removal_without_blocking_other_callers(self):
        self.scenario("floyd_owned_bad_delay", (1, 3))

    def test_gauge_cleanup_failure_does_not_prevent_floyd_removal(self):
        self.scenario("floyd_gauge_cleanup_failure", (1, 3))

    def test_unknown_diagnostic_body_is_preserved_without_reopening_original_entry(self):
        self.scenario("floyd_foreign_diagnostic_owned", (1, 3))
        self.scenario("floyd_foreign_diagnostic_snapshot", (1, 3))

    def test_floyd_call_tracks_repeated_game_widescreen_toggles(self):
        self.scenario("floyd_toggles", (1, 3))


if __name__ == "__main__":
    unittest.main()
