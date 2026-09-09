"""Execute the real HUD alignment and widescreen lifecycle code with mock RAM.

The shared harness supplies the actual CodePatcher implementation and widescreen
methods. This adds the alignment method and generated-code headers from the
working tree, so combined checkbox states exercise both production lifecycles.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_jfg_widescreen_lifecycle as widescreen


def translation_unit():
    source = (widescreen.HACKS / "JetForceGemini.cpp").read_text(encoding="utf-8-sig")
    method = widescreen.function(source, "bool CJetForceGeminiRuntime::PatchHudAlignment(")
    unit = widescreen.translation_unit().split("int main(int argc, char **argv)", 1)[0]
    memory = widescreen.MOCKS.replace("    uint32_t Word(uint32_t a) const", MEMORY_METHODS +
                                     "    uint32_t Word(uint32_t a) const", 1)
    runtime = widescreen.RUNTIME.replace("    bool PatchWidescreenHud(bool Enabled);",
                                        "    bool PatchWidescreenHud(bool Enabled);" + RUNTIME_FIELDS, 1)
    unit = unit.replace(widescreen.MOCKS, memory, 1).replace(widescreen.RUNTIME, runtime, 1)
    header = widescreen.HACKS / "JetForceGeminiHudAlignment.h"
    original = widescreen.HACKS / "JetForceGeminiHudAlignmentOriginal.h"
    return '#include <cstring>\n#include <cmath>\n' + unit + \
        '\n#include "' + header.as_posix() + '"\n' + \
        '\n#include "' + original.as_posix() + '"\n' + \
        "\nconst uint32_t PlayerCountAddress = 0x800F2D10;\n" + method + CASES


MEMORY_METHODS = r'''
    bool ReadS16(uint32_t a, int16_t &v) const {
        uint8_t hi = 0, lo = 0;
        if (!ReadU8(a, hi) || !ReadU8(a + 1, lo)) return false;
        v = int16_t((uint16_t(hi) << 8) | lo); return true;
    }
    bool WriteS16(uint32_t a, int16_t v) {
        if (!IsRdramAddress(a, 2)) return false;
        return WriteU8(a, uint8_t(uint16_t(v) >> 8)) && WriteU8(a + 1, uint8_t(v));
    }
    bool ReadF32(uint32_t a, float &v) const {
        uint32_t bits = 0;
        if (!ReadU32(a, bits)) return false;
        std::memcpy(&v, &bits, sizeof(v)); return true;
    }
    bool WriteF32(uint32_t a, float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits)); return WriteU32(a, bits);
    }
'''

RUNTIME_FIELDS = r'''
    std::vector<uint32_t> m_HudAlignmentCaveOriginal;
    std::vector<uint32_t> m_HudAlignmentImage;
    uint32_t m_HudAlignmentOverlay6Base = 0;
    uint32_t m_HudAlignmentOverlay14Base = 0;
    bool m_HudAlignmentScopeOwned = false;
    bool PatchHudAlignment(bool Enabled, bool WidescreenCorrected);
'''

CASES = r'''
namespace Sites = JfgHudAlignmentSites;
namespace Code = JfgHudAlignmentCode;
namespace Original = JfgHudAlignmentOriginal;
const uint32_t Overlay6 = 0x80300000;
const uint32_t AlignmentGuard = 0x80067994;
const uint32_t AlignmentEnd = 0x800680A0;

std::vector<GAME_HACK_CODE_PATCH> AlignmentSites(uint32_t base6 = Overlay6, uint32_t base14 = Overlay14) {
    std::vector<GAME_HACK_CODE_PATCH> sites;
    auto Add = [&](uint32_t base, const Sites::CallSite &site, uint32_t entry) {
        sites.push_back({base + site.Offset, site.Original, CallTo(entry)});
    };
    for (const auto &site : Sites::WeaponSpriteCalls) Add(base14, site, Code::SpriteWeaponEntry);
    for (const auto &site : Sites::WeaponMatrixCalls) Add(base14, site, Code::MatrixWeaponEntry);
    Add(base6, Sites::HealthSpriteCall, Code::SpriteHealthEntry);
    Add(base6, Sites::HealthMatrixCall, Code::MatrixHealthEntry);
    sites.push_back({Code::SpriteMatrixHookAddress, Code::SpriteMatrixHookOriginal, CallTo(Code::SpriteMatrixEntry)});
    return sites;
}

void AlignmentStock(CGameHackMemory &m) {
    m.WriteU32(PlayerCountAddress, 1);
    m.WriteU8(0x800A4FD0, 1);
    m.WriteU32(Table + 6 * OverlayHeaderSize, Overlay6);
    for (const auto &site : AlignmentSites()) m.WriteU32(site.Address, site.Original);
    for (const auto &site : Sites::WeaponSpriteCalls) m.WriteU32(Overlay14 + site.Offset + 4, site.Delay);
    for (const auto &site : Sites::WeaponMatrixCalls) m.WriteU32(Overlay14 + site.Offset + 4, site.Delay);
    m.WriteU32(Overlay6 + Sites::HealthSpriteCall.Offset + 4, Sites::HealthSpriteCall.Delay);
    m.WriteU32(Overlay6 + Sites::HealthMatrixCall.Offset + 4, Sites::HealthMatrixCall.Delay);
    m.WriteU32(Code::SpriteMatrixHookAddress + 4, Code::SpriteMatrixHookDelay);
    m.WriteU32(Overlay14 + Sites::WeaponGroupCallOffset,
               CallTo(Overlay14 + Sites::WeaponGroupFunctionOffset));
    m.WriteU32(Overlay14 + Sites::WeaponGroupCallOffset + 4, Sites::WeaponGroupCallDelay);
    for (size_t i = 0; i < sizeof(Sites::HealthFunctionPrologue) / sizeof(uint32_t); ++i)
        m.WriteU32(Overlay6 + Sites::HealthFunctionOffset + uint32_t(i * 4), Sites::HealthFunctionPrologue[i]);
    for (size_t i = 0; i < sizeof(Sites::WeaponGroupFunctionPrologue) / sizeof(uint32_t); ++i)
        m.WriteU32(Overlay14 + Sites::WeaponGroupFunctionOffset + uint32_t(i * 4), Sites::WeaponGroupFunctionPrologue[i]);
    static_assert(sizeof(Original::CaveWords) == AlignmentEnd - Code::CaveStart,
                  "the fixture must cover the complete production cave");
    for (size_t i = 0; i < sizeof(Original::GuardWords) / sizeof(uint32_t); ++i)
        m.WriteU32(AlignmentGuard + uint32_t(i * 4), Original::GuardWords[i]);
    for (size_t i = 0; i < sizeof(Original::CaveWords) / sizeof(uint32_t); ++i)
        m.WriteU32(Code::CaveStart + uint32_t(i * 4), Original::CaveWords[i]);
    // The diagnostic logger and adjacent widescreen scope are not alignment state.
    for (uint32_t address = 0x800676B4; address < 0x80067790; address += 4)
        m.WriteU32(address, 0x37000000 | (address & 0xFFFF));
    m.WriteU8(Code::ActiveKindAddress - 2, 0x51);
    m.WriteU8(Code::ActiveKindAddress - 1, 0x52);
    // Include nonzero live object/animation state, rather than letting an
    // accidental reset of game coordinates disappear into zero-filled RAM.
    for (uint32_t offset = 0; offset < 14 * 0x20; ++offset)
        m.WriteU8(0x800FF780 + offset, uint8_t(offset * 17 + 3));
}

void CheckAlignmentStock(const CGameHackMemory &m, uint32_t base6 = Overlay6, uint32_t base14 = Overlay14) {
    for (const auto &site : AlignmentSites(base6, base14))
        Require(m.Word(site.Address) == site.Original, "alignment call remains installed");
    Require(m.Word(base14 + Sites::WeaponGroupCallOffset) == CallTo(base14 + Sites::WeaponGroupFunctionOffset),
            "weapon group wrapper remains installed");
    for (size_t i = 0; i < sizeof(Original::GuardWords) / sizeof(uint32_t); ++i)
        Require(m.Word(AlignmentGuard + uint32_t(i * 4)) == Original::GuardWords[i],
                "diagnostic entry guard was not restored");
    for (size_t i = 0; i < sizeof(Original::CaveWords) / sizeof(uint32_t); ++i)
        Require(m.Word(Code::CaveStart + uint32_t(i * 4)) == Original::CaveWords[i],
                "alignment cave was not restored to the original ROM instruction");
    uint8_t kind = 255;
    Require(m.ReadU8(Code::ActiveKindAddress, kind) && kind == 0, "alignment scope remains active");
}

void CheckAlignmentInstalled(const CGameHackMemory &m, uint32_t base6 = Overlay6, uint32_t base14 = Overlay14) {
    for (const auto &site : AlignmentSites(base6, base14))
        Require(m.Word(site.Address) == site.Replacement, "alignment fixture failed to install a call");
    const uint32_t wrapper = m.Word(base14 + Sites::WeaponGroupCallOffset);
    const uint32_t target = 0x80000000 | ((wrapper & 0x03FFFFFF) << 2);
    Require(wrapper >> 26 == 3 && target >= Code::CaveStart && target < AlignmentEnd,
            "weapon group wrapper does not target the alignment cave");
    Require(m.Word(AlignmentGuard) == 0x03E00008 && m.Word(AlignmentGuard + 4) == 0,
            "diagnostic entry was not disconnected before cave use");
}

void RequireProtectedMemory(const CGameHackMemory &m, const std::vector<uint8_t> &before) {
    // Both widescreen cave segments and their intervening logger stay byte-identical.
    const uint32_t first = 0x80067280, last = 0x800678C4;
    Require(std::equal(before.begin() + first - 0x80000000, before.begin() + last - 0x80000000,
                       m.bytes.begin() + first - 0x80000000), "alignment overwrote widescreen or logger code");
    for (uint32_t address : {Code::ActiveKindAddress - 2, Code::ActiveKindAddress - 1, Code::ActiveKindAddress + 1})
        Require(m.bytes[address - 0x80000000] == before[address - 0x80000000], "alignment overwrote neighbouring scope/status byte");
    Require(std::equal(before.begin() + 0xFF780, before.begin() + 0xFF780 + 14 * 0x20,
                       m.bytes.begin() + 0xFF780), "alignment changed live HUD coordinates or animation state");
}

int main(int argc, char **argv) {
    try {
        const std::string scenario = argc > 1 ? argv[1] : "";
        const uint8_t mode = argc > 2 ? uint8_t(std::stoi(argv[2])) : 0;
        const unsigned flags = argc > 3 ? unsigned(std::stoi(argv[3])) : 1;
        const bool align = (flags & 1) != 0, wide = (flags & 2) != 0;
        const bool corrected = wide && (mode == 1 || mode == 3);
        CGameHackMemory m;
        Stock(m, mode);
        AlignmentStock(m);
        const auto stockImage = m.bytes;
        CJetForceGeminiRuntime live(m);
        Require(live.PatchWidescreenHud(wide), "widescreen fixture failed");
        const auto wideImage = m.bytes;
        if (scenario == "combination") {
            Require(live.PatchHudAlignment(align, corrected), "checkbox combination failed");
            if (align) CheckAlignmentInstalled(m);
            else Require(m.bytes == wideImage, "disabled alignment changed memory");
            RequireProtectedMemory(m, wideImage);
            Require(live.PatchHudAlignment(false, corrected), "alignment disable failed");
            CheckAlignmentStock(m);
            Require(m.bytes == wideImage, "alignment undo did not restore exact memory");
            Require(live.PatchWidescreenHud(false), "widescreen undo failed");
            Require(m.bytes == stockImage, "combined undo did not restore exact memory");
        } else if (scenario == "foreign" || scenario == "foreign_guard" || scenario == "scope_unowned" ||
                   scenario == "foreign_delay" || scenario == "foreign_prologue" ||
                   scenario.rfind("foreign_cave_", 0) == 0) {
            if (scenario == "foreign") m.WriteU32(Overlay14 + Sites::WeaponSpriteCalls[0].Offset, 0xDEADBEEF);
            else if (scenario == "foreign_guard") m.WriteU32(AlignmentGuard, 0xDEADBEEF);
            else if (scenario == "foreign_delay") m.WriteU32(Overlay14 + Sites::WeaponSpriteCalls[0].Offset + 4, 0xDEADBEEF);
            else if (scenario == "foreign_prologue") m.WriteU32(Overlay6 + Sites::HealthFunctionOffset, 0xDEADBEEF);
            else if (scenario.rfind("foreign_cave_", 0) == 0) {
                const uint32_t offset = scenario == "foreign_cave_first" ? 0 :
                                        scenario == "foreign_cave_middle" ? 0x380 : 0x6FC;
                m.WriteU32(Code::CaveStart + offset, 0xDEADBEEF);
            }
            else m.WriteU8(Code::ActiveKindAddress, 0xA5);
            const auto before = m.bytes;
            Require(!live.PatchHudAlignment(true, corrected), "foreign state was accepted");
            Require(m.bytes == before, "rejected installation modified guest memory");
        } else {
            Require(live.PatchHudAlignment(true, corrected), "initial alignment installation failed");
            CheckAlignmentInstalled(m);
            RequireProtectedMemory(m, wideImage);
            if (scenario == "mode_transition" || scenario == "resolution_transition") {
                const uint8_t nextMode = mode ^ (scenario == "resolution_transition" ? 2 : 1);
                const bool nextCorrected = wide && (nextMode == 1 || nextMode == 3);
                m.WriteU8(WidescreenHudResolutionIndexAddress, nextMode);
                Require(live.PatchWidescreenHud(wide), "widescreen mode transition failed");
                Require(live.PatchHudAlignment(true, nextCorrected), "alignment mode transition failed");
                CheckAlignmentInstalled(m);
                CGameHackMemory reference;
                Stock(reference, nextMode);
                AlignmentStock(reference);
                CJetForceGeminiRuntime referenceHost(reference);
                Require(referenceHost.PatchWidescreenHud(wide) && referenceHost.PatchHudAlignment(true, nextCorrected),
                        "reference mode installation failed");
                Require(live.m_HudAlignmentImage == referenceHost.m_HudAlignmentImage,
                        "alignment retained parameters from the previous mode");
                Require(live.PatchHudAlignment(false, nextCorrected) && live.PatchWidescreenHud(false),
                        "cleanup after mode transition failed");
                auto expected = stockImage;
                expected[WidescreenHudResolutionIndexAddress - 0x80000000] = nextMode;
                Require(m.bytes == expected, "mode transition lost the original cave backup");
            } else if (scenario == "adopt" || scenario == "adopt_enabled") {
                CGameHackMemory restored;
                restored.bytes = m.bytes;
                restored.WriteU8(Code::ActiveKindAddress, 2);
                CJetForceGeminiRuntime fresh(restored);
                if (scenario == "adopt_enabled") {
                    Require(fresh.PatchHudAlignment(true, corrected), "fresh host could not adopt an enabled snapshot");
                    CheckAlignmentInstalled(restored);
                    uint8_t kind = 255;
                    Require(restored.ReadU8(Code::ActiveKindAddress, kind) && kind == 0,
                            "adoption retained an interrupted sprite scope");
                }
                Require(fresh.PatchHudAlignment(false, false), "fresh host could not retire alignment snapshot");
                CheckAlignmentStock(restored);
                RequireProtectedMemory(restored, wideImage);
                Require(restored.bytes == wideImage, "fresh-host cleanup did not restore the exact ROM cave and hooks");
            } else if (scenario == "relocated") {
                const uint32_t moved6 = 0x80340000, moved14 = 0x80360000;
                for (const auto pair : {std::make_pair(Overlay6, moved6), std::make_pair(Overlay14, moved14)}) {
                    const size_t old = pair.first - 0x80000000, moved = pair.second - 0x80000000;
                    std::copy(m.bytes.begin() + old, m.bytes.begin() + old + 0x10000, m.bytes.begin() + moved);
                    std::fill(m.bytes.begin() + old, m.bytes.begin() + old + 0x10000, 0xA5);
                }
                m.WriteU32(Table + 6 * OverlayHeaderSize, moved6);
                m.WriteU32(Table + 14 * OverlayHeaderSize, moved14);
                const auto relocated = m.bytes;
                Require(live.PatchHudAlignment(false, corrected), "relocated overlay cleanup failed");
                CheckAlignmentStock(m, moved6, moved14);
                for (uint32_t old : {Overlay6, Overlay14})
                    Require(std::equal(relocated.begin() + old - 0x80000000,
                                       relocated.begin() + old - 0x80000000 + 0x10000,
                                       m.bytes.begin() + old - 0x80000000), "cleanup wrote stale overlay address");
                RequireProtectedMemory(m, wideImage);
            } else throw std::runtime_error("unknown scenario");
        }
        std::cout << "PASS " << scenario << " mode=" << unsigned(mode) << " flags=" << flags << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
'''


class JfgHudAlignmentLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = widescreen.WORKSPACE / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="jfg-alignment-lifecycle-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name).resolve()
        assert directory.is_relative_to(build.resolve())
        source = directory / "lifecycle.cpp"
        source.write_text(translation_unit(), encoding="utf-8")
        cls.executable = directory / ("lifecycle.exe" if os.name == "nt" else "lifecycle")
        command = widescreen.compiler_command(directory, source, cls.executable)
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError("C++ alignment lifecycle harness failed to compile:\n" + result.stdout + result.stderr)

    def scenario(self, name, modes=(0, 1, 2, 3), flags=(1, 3)):
        for mode in modes:
            for checkbox_flags in flags:
                with self.subTest(scenario=name, mode=mode, flags=checkbox_flags):
                    result = subprocess.run([str(self.executable), name, str(mode), str(checkbox_flags)],
                                            capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_checkbox_combinations_restore_bit_exact_memory_in_all_modes(self):
        self.scenario("combination", flags=(0, 1, 2, 3))

    def test_fresh_host_cleans_alignment_snapshot_when_option_is_off(self):
        self.scenario("adopt")

    def test_fresh_host_adoption_clears_interrupted_sprite_scope_before_enabling(self):
        self.scenario("adopt_enabled")

    def test_mode_changes_refresh_alignment_and_keep_the_original_backup(self):
        self.scenario("mode_transition")

    def test_resolution_changes_refresh_alignment_and_keep_the_original_backup(self):
        self.scenario("resolution_transition")

    def test_relocated_overlays_are_cleaned_without_writing_stale_addresses(self):
        self.scenario("relocated")

    def test_foreign_call_site_is_preserved_without_partial_installation(self):
        self.scenario("foreign")

    def test_foreign_diagnostic_guard_is_preserved(self):
        self.scenario("foreign_guard")

    def test_foreign_cave_words_are_rejected_before_writing_any_guest_memory(self):
        for position in ("first", "middle", "last"):
            self.scenario("foreign_cave_" + position)

    def test_foreign_delay_slot_and_overlay_prologue_are_preserved(self):
        self.scenario("foreign_delay")
        self.scenario("foreign_prologue")

    def test_unowned_scope_byte_is_not_cleared_or_claimed(self):
        self.scenario("scope_unowned")


if __name__ == "__main__":
    unittest.main()
