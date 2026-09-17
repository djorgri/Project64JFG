"""Compile the real flight installer and controller-publication block with mock RAM."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_jfg_ammo_hud import SOURCE, array_body
from test_jfg_hud_alignment_lifecycle import MEMORY_METHODS
from test_jfg_widescreen_lifecycle import HACKS, MOCKS, WORKSPACE, compiler_command, function


def translation_unit():
    source = SOURCE.read_text(encoding="utf-8-sig")
    patcher = (HACKS / "GameHackMemory.cpp").read_text(encoding="utf-8-sig")
    declarations = (HACKS / "GameHackMemory.h").read_text(encoding="utf-8-sig")
    installer = function(source, "bool CJetForceGeminiRuntime::PatchSidekickStrafe(")
    helper = function(source, "uint32_t SidekickFlightHookWord(")
    mapping = source[source.index("    const bool DroneThrusters ="):source.index("    m_SprintActive =", source.index("    const bool DroneThrusters ="))]
    arrays = "\n".join("const uint32_t " + name + "[] = {" + array_body(source, name) + "};"
                       for name in ("SidekickStrafeHookCode", "SidekickVerticalHookCode"))
    names = set(re.findall(r"\b(?:Drone\w+|Sidekick\w+)\b", installer + helper + mapping + arrays))
    # The production reset writes are shared with otherwise unrelated HUD and
    # camera lifecycle work. Execute those exact statements, not a copied reset.
    resets = []
    for name in ("StateSaving", "StateLoaded", "Deactivate"):
        body = function(source, "void CJetForceGeminiRuntime::" + name + "(")
        writes = re.findall(r"    m_Memory\.Write\w+\(Drone\w+[^;]+;", body)
        resets.append("void " + name + "() {\n" + "\n".join(writes) + "\n}")
    names.update(re.findall(r"\bDrone\w+Address\b", "\n".join(resets)))
    globals_ = "\n".join(m.group(0) for m in re.finditer(
        r"^(?:const )?(?:uint32_t|float) (\w+) = [^;\n]+;", source, re.M) if m.group(1) in names)
    mocks = MOCKS[:MOCKS.index("const uint32_t OverlayTableAddress")]
    mocks = mocks.replace("    uint32_t Word(uint32_t a) const", MEMORY_METHODS + "    uint32_t Word(uint32_t a) const")
    funcs = "\n".join(function(source, "uint32_t " + name + "(") for name in ("JumpTo", "Hi16", "Lo16", "WithHi", "WithLo"))
    header = (HACKS / "JetForceGeminiAddresses.h").as_posix()
    tables = (HACKS / "JetForceGeminiAddresses.cpp").read_text(encoding="utf-8-sig")
    tables = "\n".join("const JFG_ADDRESSES " + name + " = {" + re.search(
        r"const JFG_ADDRESSES " + name + r"\s*=\s*\{(.*?)\};", tables, re.S).group(1) + "};"
        for name in ("JfgUsAddresses", "JfgKioskAddresses"))
    assignments = function(source, "void ApplyAddressTable(")
    assignments = "\n".join(line for line in assignments.splitlines()
                            if re.match(r"\s*(\w+) = A\.\1;", line) and line.strip().split()[0] in names)
    runtime = r'''
enum { Setting_JfgDroneLateralMovement };
struct Settings { bool enabled = true; bool LoadBool(int) { return enabled; } } settings;
Settings *g_Settings = &settings;
class CJetForceGeminiRuntime {
public:
    CGameHackMemory m_Memory;
    CRecompiler *m_Recompiler = nullptr;
    CGameHackCodePatcher m_CodePatcher{m_Memory, m_Recompiler};
    std::vector<uint32_t> m_SidekickStrafeHookStubOriginal;
    bool m_SidekickStrafeHookApplied = false;
    bool m_DroneLateralActive = false, m_DroneLateralRight = false;
    bool PatchSidekickStrafe(bool);
    void Map(bool DroneMode, bool Left, bool Right, bool CUp, bool CDown, bool ApplyCamera=false) {
        const uint32_t PlayerObject = 0x80200000;
''' + mapping + "\n}\n" + "\n".join(resets) + "\n};\n"
    return '#include <cstring>\n#include <cmath>\n#include "' + header + '"\n' + mocks + \
        declarations[declarations.index("struct GAME_HACK_CODE_PATCH"):] + globals_ + arrays + funcs + tables + \
        "\nvoid Select(const JFG_ADDRESSES &A) {\n" + assignments + \
        "\nSidekickStrafeJump = JumpTo(SidekickStrafeStub);\nSidekickStrafeResumeJump = JumpTo(SidekickStrafeDelay + 4);\n}\n" + \
        runtime + patcher[patcher.index("CGameHackCodePatcher::CGameHackCodePatcher("):] + helper + installer + CASES


CASES = r'''
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
float Value(CGameHackMemory &m, uint32_t a) { float v; Check(m.ReadF32(a, v), "read failed"); return v; }
void Stock(CGameHackMemory &m) {
    m.WriteU32(SidekickStrafeEntry, SidekickStrafeEntryOriginal);
    m.WriteU32(SidekickStrafeDelay, SidekickStrafeDelayOriginal);
    for (uint32_t a : {SidekickStrafeStub, SidekickVerticalStub})
        for (unsigned i=0; i<0xC4; i+=4) m.WriteU32(a+i, 0xA5000000+i);
}
int main(int argc, char **argv) {
    try {
        Select(argc > 1 && std::string(argv[1]) == "kiosk" ? JfgKioskAddresses : JfgUsAddresses);
        CJetForceGeminiRuntime r; auto &m=r.m_Memory; Stock(m);
        auto before=m.bytes;
        Check(r.PatchSidekickStrafe(true), "install failed");
        Check(m.Word(SidekickStrafeEntry)==JumpTo(SidekickStrafeStub), "entry missing");
        Check(m.Word(SidekickStrafeStub+sizeof(SidekickStrafeHookCode)-8)==JumpTo(SidekickVerticalStub), "vertical jump wrong");
        Check(m.Word(SidekickVerticalStub+sizeof(SidekickVerticalHookCode)-8)==JumpTo(SidekickStrafeEntry+8), "return wrong");
        Check(sizeof(SidekickStrafeHookCode)<=0xC0 && sizeof(SidekickVerticalHookCode)<=0xC4, "cave overflow");
        Check(m.Word(SidekickStrafeStub+4)==WithHi(0x3C190000,DroneLateralMaxSpeedAddress), "scratch high wrong");
        Check(m.Word(SidekickVerticalStub+8)==WithLo(0xC7300000,DroneVerticalVelocityAddress), "scratch low wrong");
        Check(r.PatchSidekickStrafe(false), "uninstall failed");
        // Code and the surrounding sentinels must be restored for save/load.
        for (uint32_t a : {SidekickStrafeStub, SidekickVerticalStub})
            for (unsigned i=0; i<0xC4; i+=4) Check(m.Word(a+i)==0xA5000000+i,"cave not restored or overrun");
        Check(m.Word(SidekickStrafeEntry)==SidekickStrafeEntryOriginal && m.Word(SidekickStrafeDelay)==SidekickStrafeDelayOriginal,"entry not restored");
        for (bool up : {false,true}) for (bool down : {false,true}) {
            r.Map(true,false,false,up,down);
            Check(Value(m,DroneVerticalThrustAddress)==(up==down ? 0.0f : up ? 0.15f : -0.15f),"jump/crouch mapping wrong");
            Check(m.Word(DroneLateralFlagsAddress)&1,"release must retain drag");
        }
        m.WriteF32(DroneVerticalVelocityAddress,2); m.WriteF32(DroneLateralVelocityAddress,3);
        auto held=m.bytes; r.Map(true,false,false,true,false,true);
        Check(m.bytes==held,"video pass overwrote controller input");
        r.Map(false,false,false,true,false);
        Check(Value(m,DroneVerticalThrustAddress)==0 && Value(m,DroneVerticalVelocityAddress)==0 && Value(m,DroneLateralVelocityAddress)==0,"mission exit retained thrust/drift");
        settings.enabled=false; r.Map(true,true,false,true,false);
        Check(m.Word(DroneLateralFlagsAddress)==0 && Value(m,DroneVerticalThrustAddress)==0,"disabled option still active");
        settings.enabled=true;
        for (int i=0;i<3;++i) {
            m.WriteF32(DroneVerticalThrustAddress,.15f); m.WriteF32(DroneVerticalVelocityAddress,5);
            if(i==0) r.StateSaving(); else if(i==1) r.StateLoaded(); else r.Deactivate();
            Check(Value(m,DroneVerticalThrustAddress)==0 && Value(m,DroneVerticalVelocityAddress)==0,"lifecycle retained vertical input/drift");
        }
        Stock(m); m.WriteU32(SidekickStrafeDelay,0x12345678); before=m.bytes;
        Check(!r.PatchSidekickStrafe(true) && m.bytes==before,"foreign entry was modified");
        Stock(m); Check(r.PatchSidekickStrafe(true),"reinstall failed");
        // A freshly loaded patched state has no installer bookkeeping.
        CJetForceGeminiRuntime loaded; loaded.m_Memory.bytes=m.bytes;
        Check(loaded.PatchSidekickStrafe(false),"stale jump cleanup failed");
        Check(loaded.m_Memory.Word(SidekickStrafeEntry)==SidekickStrafeEntryOriginal,"stale jump still active");
        Check(loaded.PatchSidekickStrafe(true),"reinstall after state load failed");
        std::cout << "flight lifecycle passed\n";
    } catch (const std::exception &e) { std::cerr << e.what(); return 1; }
}
'''


class FloydMovementLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="jfg-flight-", dir=WORKSPACE / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name).resolve()
        source = directory / "flight.cpp"
        source.write_text(translation_unit(), encoding="utf-8")
        cls.executable = directory / ("flight.exe" if os.name == "nt" else "flight")
        result = subprocess.run(compiler_command(directory, source, cls.executable), cwd=directory,
                                capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_controller_publication_installation_cleanup_and_state_load(self):
        for build in ("us", "kiosk"):
            with self.subTest(build=build):
                result = subprocess.run([str(self.executable), build], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
