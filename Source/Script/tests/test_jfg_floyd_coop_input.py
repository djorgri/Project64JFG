"""Exercise the real secondary-port input mapping with mock game-mode flags."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_jfg_widescreen_lifecycle import HACKS, MOCKS, WORKSPACE, compiler_command, function


def translation_unit():
    source = (HACKS / "JetForceGemini.cpp").read_text(encoding="utf-8-sig")
    header = (HACKS / "JetForceGemini.h").read_text(encoding="utf-8-sig")
    tables = (HACKS / "JetForceGeminiAddresses.cpp").read_text(encoding="utf-8-sig")
    constants = "\n".join(m.group(0) for m in re.finditer(
        r"^(?:const )?(?:uint32_t|int8_t|int16_t|float) (\w+) = [^;\n]+;", source, re.M)
        if m.group(1) in {"MultiplayerGameAddress", "CooperativeGameAddress", "JfgStickLimit",
                          "MouseButtonLeft", "MouseButtonRight", "GamepadStickDeadZone",
                          "GamepadDigitalThreshold", "GamepadTriggerThreshold"})
    helpers = "\n".join(function(source, signature) for signature in (
        "bool GamepadButtonDown(", "bool GamepadTriggerDown(",
        "void NormaliseStick(", "int8_t StickToN64("))
    methods = "\n".join(function(source, signature) for signature in (
        "bool CJetForceGeminiRuntime::KeyDown(", "bool CJetForceGeminiRuntime::MouseButtonDown(",
        "void CJetForceGeminiRuntime::ReadControls(", "int32_t CJetForceGeminiRuntime::ReadScrollButtons(",
        "void CJetForceGeminiRuntime::MapSecondaryPort("))
    declarations = "\n".join(method[:method.index("{")].replace("CJetForceGeminiRuntime::", "") + ";"
                             for method in (function(source, signature) for signature in (
                                 "bool CJetForceGeminiRuntime::KeyDown(",
                                 "bool CJetForceGeminiRuntime::MouseButtonDown(",
                                 "void CJetForceGeminiRuntime::ReadControls(",
                                 "int32_t CJetForceGeminiRuntime::ReadScrollButtons(",
                                 "void CJetForceGeminiRuntime::MapSecondaryPort(")))
    tables = "\n".join("const JFG_ADDRESSES " + name + " = {" + re.search(
        r"const JFG_ADDRESSES " + name + r"\s*=\s*\{(.*?)\};", tables, re.S).group(1) + "};"
        for name in ("JfgUsAddresses", "JfgKioskAddresses"))
    assignments = "\n".join(line for line in function(source, "void ApplyAddressTable(").splitlines()
                            if re.match(r"\s*(MultiplayerGameAddress|CooperativeGameAddress) = A\.", line))
    return ('#include <cmath>\n#include <cstring>\n#include <cstdlib>\n#include "' +
            (WORKSPACE / "Source/Project64-plugin-spec/Input.h").as_posix() + '"\n#include "' +
            (HACKS / "JetForceGeminiAddresses.h").as_posix() + '"\n' +
            MOCKS[:MOCKS.index("const uint32_t OverlayTableAddress")] + constants + helpers + tables +
            "\nvoid Select(const JFG_ADDRESSES &A) {\n" + assignments + "\n}\n" +
            function(header, "struct JFG_PORT_INPUT") + ";\n" +
            "class CJetForceGeminiRuntime { public:\n" +
            function(header, "struct JFG_CONTROLS") + ";\n" +
            function(header, "struct SCROLL_BUTTON_STATE") + ";\n" +
            "CGameHackMemory m_Memory; bool supported = true;\n"
            "SCROLL_BUTTON_STATE m_SecondaryScrollButtons[3] = {};\n"
            "bool IsSupportedRom() { return supported; }\n" + declarations + "\n};\n" + methods + CASES)


CASES = r'''
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    try {
        Select(argc > 1 && std::string(argv[1]) == "kiosk" ? JfgKioskAddresses : JfgUsAddresses);
        CJetForceGeminiRuntime r;
        GAMEPAD_STATE pad = {}; pad.LeftX = 10000; pad.RightTrigger = GamepadAxisMax;
        JFG_PORT_INPUT input = {nullptr, {&pad, nullptr}};
        BUTTONS baseline, actual;
        // Sweep analogue values, both directions and neutral, across modes and ports.
        for (int y : {-32768, -24000, -8000, 0, 8000, 24000, 32767}) {
            pad.LeftY = int16_t(y);
            r.m_Memory.WriteU8(CooperativeGameAddress, 0);
            r.MapSecondaryPort(1, input, baseline);
            for (uint8_t coop : {0, 1}) for (uint8_t multi : {0, 1}) for (int port : {1, 2, 3}) {
                r.m_Memory.WriteU8(CooperativeGameAddress, coop);
                r.m_Memory.WriteU8(MultiplayerGameAddress, multi);
                r.MapSecondaryPort(port, input, actual);
                Check(actual.Y_AXIS == (coop && !multi && port == 1 ? -baseline.Y_AXIS : baseline.Y_AXIS),
                      "incorrect Y sign or scope");
                Check((actual.Value & 0x00FFFFFF) == (baseline.Value & 0x00FFFFFF), "X/buttons changed");
            }
        }
        // Keyboard movement goes through the same mapping, including release.
        KEYBOARD_MOUSE_STATE keyboard = {}; input = {&keyboard, {nullptr, nullptr}};
        r.m_Memory.WriteU8(MultiplayerGameAddress, 0);
        r.m_Memory.WriteU8(CooperativeGameAddress, 1);
        for (auto key : {KeyboardMouseKey_W, KeyboardMouseKey_Z, KeyboardMouseKey_S}) {
            keyboard.Keys[key] = 0x80; r.MapSecondaryPort(1, input, actual);
            Check(actual.Y_AXIS == (key == KeyboardMouseKey_S ? 80 : -80), "keyboard Y wrong");
            keyboard.Keys[key] = 0;
        }
        r.MapSecondaryPort(1, input, actual); Check(actual.Y_AXIS == 0, "release retained input");
        keyboard.Keys[KeyboardMouseKey_W] = 0x80;
        // Joining, leaving and restoring flags take effect on the next poll.
        for (uint8_t coop : {1, 0, 1}) {
            r.m_Memory.WriteU8(CooperativeGameAddress, coop); r.MapSecondaryPort(1, input, actual);
            Check(actual.Y_AXIS == (coop ? -80 : 80), "stale co-op state");
        }
        r.supported = false; r.MapSecondaryPort(1, input, actual);
        Check(actual.Y_AXIS == 80, "unsupported ROM inverted"); r.supported = true;
        for (auto address : {&CooperativeGameAddress, &MultiplayerGameAddress}) {
            const auto saved = *address; *address = 0; r.MapSecondaryPort(1, input, actual);
            Check(actual.Y_AXIS == 80, "failed read inverted"); *address = saved;
        }
        r.MapSecondaryPort(1, JFG_PORT_INPUT{}, actual); Check(actual.Value == 0, "absent source not neutral");
        std::cout << "cooperative Floyd input passed\n";
    } catch (const std::exception &e) { std::cerr << e.what(); return 1; }
}
'''


class FloydCoopInputTests(unittest.TestCase):
    def test_secondary_port_aim_direction_and_mode_transitions(self):
        with tempfile.TemporaryDirectory(prefix="jfg-coop-", dir=WORKSPACE / "build") as temporary:
            directory = Path(temporary).resolve()
            source = directory / "coop.cpp"
            source.write_text(translation_unit(), encoding="utf-8")
            executable = directory / ("coop.exe" if os.name == "nt" else "coop")
            result = subprocess.run(compiler_command(directory, source, executable), cwd=directory,
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for build in ("us", "kiosk"):
                with self.subTest(build=build):
                    result = subprocess.run([str(executable), build], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
