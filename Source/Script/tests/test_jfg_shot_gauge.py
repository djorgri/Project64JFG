"""Run the actual shot-gauge wrapper and rectangle-anchor MIPS instructions.

The seven native rectangles below are from US overlay 14. The expected gauge
position is derived from the weapon frame's world origin, independently of the
helper's constants. Mode/scope transitions are exercised while hooks remain
installed, covering the interval before the emulator can remove them.
"""

import math
import unittest

from test_jfg_ammo_hud import (
    MASK32, SOURCE, code_array, constant, load_word, register_word,
    sign_extend, store_word,
)
from test_jfg_banner_hud import FpuMachine


NATIVE_RECTANGLES = (
    (57, 21, 74, 44),  # Transparent backing rectangle.
    (67, 24, 71, 26),
    (67, 27, 71, 29),
    (66, 30, 71, 32),
    (65, 33, 71, 35),
    (63, 36, 71, 38),
    (60, 39, 71, 41),
)


class CallMachine(FpuMachine):
    """Existing instruction model plus JAL/JR for the ABI wrapper."""

    def run(self, start, resume):
        pc = start
        for _ in range(96):
            if pc == resume:
                return
            self.visited.append(pc)
            word = self.program[pc]
            opcode = word >> 26
            rs, rt = (word >> 21) & 31, (word >> 16) & 31
            if opcode in (2, 3, 4, 5) or (opcode == 0 and word & 63 == 8):
                if opcode in (2, 3):
                    next_pc = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                    if opcode == 3:
                        self.write_register(31, register_word(pc + 8))
                elif opcode == 0:
                    next_pc = self.registers[rs] & MASK32
                else:
                    taken = self.registers[rs] == self.registers[rt]
                    if opcode == 5:
                        taken = not taken
                    next_pc = pc + 4 + sign_extend(word & 0xFFFF, 16) * 4 if taken else pc + 8
                self.visited.append(pc + 4)
                self.execute_ordinary(self.program[pc + 4])
                pc = next_pc & MASK32
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("Shot-gauge hook exceeded its bounded instruction budget")


class JfgShotGaugeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.scope = constant(cls.source, "WidescreenHudScopeDepthAddress")
        cls.mode = constant(cls.source, "WidescreenHudResolutionIndexAddress")
        cls.rectangle = constant(cls.source, "WidescreenHudRectangleStub")
        cls.wrapper = constant(cls.source, "WidescreenHudShotGaugeWrapperStub")
        cls.anchor = constant(cls.source, "WidescreenHudShotGaugeAnchorStub")
        cls.wrapper_return = cls.wrapper + 16
        cls.program = {}
        for name in ("Rectangle", "ShotGaugeWrapper", "ShotGaugeAnchor"):
            start = constant(cls.source, "WidescreenHud" + name + "Stub")
            words = code_array(cls.source, "WidescreenHud" + name + "Code")
            cls.program.update({start + index * 4: word for index, word in enumerate(words)})
        cls.sp = 0x800FD000
        cls.table = 0x8034C000
        cls.display_pointer = 0x800FF398

    def fixture(self, mode=1, scope=1, caller=None, rectangle=NATIVE_RECTANGLES[1]):
        registers = [0] + [0x1122334400000000 | index * 0x10203 for index in range(1, 32)]
        left, top, right, bottom = rectangle
        for register, value in {
            6: left, 7: top, 9: bottom, 16: self.display_pointer,
            17: self.table, 24: right, 29: self.sp, 31: 0x80059624,
        }.items():
            registers[register] = register_word(value)
        memory = {self.scope: scope, self.mode: mode}
        store_word(memory, self.sp + 0x24, self.wrapper_return if caller is None else caller)
        store_word(memory, self.display_pointer, 0x80213580)
        for index, (x0, y0, x1, y1) in enumerate(NATIVE_RECTANGLES):
            address = self.table + index * 12
            store_word(memory, address, x0 << 16 | y0)
            store_word(memory, address + 4, x1 << 16 | y1)
            store_word(memory, address + 8, 0 if not index else 0x00FF0055 + index * 17)
        machine = CallMachine(self.program, registers, memory, fcsr=0x01800007)
        machine.hi, machine.lo = 0x0123456789ABCDEF, 0xFEDCBA9876543210
        return machine

    def assert_rectangle(self, machine, expected_left, expected_right):
        registers, memory = list(machine.registers), dict(machine.memory)
        fpr, fcsr, hilo = list(machine.fpr), machine.fcsr, (machine.hi, machine.lo)
        machine.run(self.rectangle, 0x80059798)
        expected = {
            2: register_word(0x80213580), 6: register_word(expected_left),
            24: register_word(expected_right), 25: register_word(expected_right << 14),
        }
        for register in set(range(32)) - {15}:
            self.assertEqual(machine.registers[register], expected.get(register, registers[register]),
                             "Unexpected GPR %d change" % register)
        self.assertEqual(machine.memory, memory, "A draw changed the native gauge table or other memory")
        self.assertEqual(machine.fpr, fpr)
        self.assertEqual(machine.fcsr, fcsr)
        self.assertEqual((machine.hi, machine.lo), hilo)

    def test_all_seven_rectangles_match_the_weapon_frame_anchor(self):
        for mode, width, bias in ((1, 320, 48), (3, 448, 68)):
            # Stock low-resolution frame starts at x=19. Its world origin is
            # -141 and the widescreen matrix applies .75 and the left bias.
            frame_left = width / 2 + 0.75 * (-141 - bias)
            for rectangle in NATIVE_RECTANGLES:
                with self.subTest(mode=mode, rectangle=rectangle):
                    expected = [math.floor(frame_left + (x - 19) * 0.75)
                                for x in (rectangle[0], rectangle[2])]
                    self.assert_rectangle(self.fixture(mode=mode, rectangle=rectangle), *expected)

    def test_mode_and_scope_changes_are_safe_before_hook_removal(self):
        for mode, scope in ((1, 1), (1, 0), (0, 0), (0, 1), (1, 1),
                            (3, 1), (3, 0), (2, 0), (2, 1), (3, 1)):
            for rectangle in NATIVE_RECTANGLES:
                with self.subTest(mode=mode, scope=scope, rectangle=rectangle):
                    left, _, right, _ = rectangle
                    if mode & 1 and scope:
                        width, bias = (448, 68) if mode & 2 else (320, 48)
                        anchor = width / 2 + .75 * (-141 - bias)
                        expected = [math.floor(anchor + (x - 19) * .75) for x in (left, right)]
                    else:
                        expected = (left, right)
                    self.assert_rectangle(self.fixture(mode, scope, rectangle=rectangle), *expected)

    def test_unrelated_rectangles_keep_their_native_or_screen_centre_anchor(self):
        for mode in range(4):
            for scope in (0, 1):
                for caller in (0x80344444, self.wrapper_return - 4,
                               self.wrapper_return + 4, 0x80357804):
                    for left, right in ((0, 319), (57, 74), (160, 224), (300, 448)):
                        with self.subTest(mode=mode, scope=scope, caller=caller, x=(left, right)):
                            centre = 224 if mode & 2 else 160
                            expected = [math.floor(.75 * x + centre / 4) for x in (left, right)] \
                                if mode & 1 and scope else (left, right)
                            self.assert_rectangle(self.fixture(mode, scope, caller,
                                                               (left, 24, right, 26)), *expected)

    def test_wrapper_passes_arguments_and_restores_stack_and_return_address(self):
        for mode in range(4):
            machine = self.fixture(mode=mode)
            for register, value in {4: self.display_pointer, 5: 7, 6: self.table,
                                    7: 0, 31: 0x8034AB30}.items():
                machine.registers[register] = register_word(value)
            registers, memory = list(machine.registers), dict(machine.memory)
            machine.run(self.wrapper, 0x800595F4)
            self.assertEqual(machine.registers[4:8], registers[4:8])
            self.assertEqual(machine.registers[29], register_word(self.sp - 0x18))
            self.assertEqual(machine.registers[31], register_word(self.wrapper_return))
            self.assertEqual(load_word(machine.memory, self.sp - 4), registers[31] & MASK32)
            # Model an ordinary callee returning two result registers. The
            # wrapper must carry these through, with all saved registers intact.
            machine.registers[2], machine.registers[3] = register_word(0x81234567), 0x10203040
            machine.run(self.wrapper_return, registers[31] & MASK32)
            for register in set(range(32)) - {2, 3}:
                self.assertEqual(machine.registers[register], registers[register])
            self.assertEqual(machine.registers[2:4], [register_word(0x81234567), 0x10203040])
            self.assertEqual({address: machine.memory[address] for address in memory}, memory)

    def test_hook_layout_preserves_reticle_logger_and_legacy_recognition(self):
        reticle = constant(self.source, "WidescreenHudReticleStub")
        reticle_end = reticle + len(code_array(self.source, "WidescreenHudReticleCode")) * 4
        wrapper_end = self.wrapper + len(code_array(self.source, "WidescreenHudShotGaugeWrapperCode")) * 4
        anchor_end = self.anchor + len(code_array(self.source, "WidescreenHudShotGaugeAnchorCode")) * 4
        self.assertEqual((reticle_end, wrapper_end, anchor_end),
                         (self.wrapper, self.anchor, constant(self.source, "WidescreenHudReticleCaveEnd")))
        self.assertGreaterEqual(reticle, 0x80067790)  # Keep diCpuLogMessage before this intact.
        self.assertLessEqual(anchor_end, 0x800678C4)  # Next diagnostic function begins here.
        legacy = code_array(self.source, "WidescreenHudRectangleLegacyCode")
        current = code_array(self.source, "WidescreenHudRectangleCode")
        self.assertEqual(len(legacy), len(current))
        self.assertEqual([index for index, pair in enumerate(zip(legacy, current)) if pair[0] != pair[1]],
                         [9, 10, 11])
        self.assertEqual(constant(self.source, "WidescreenHudShotGaugeCallOffset"), 0x2B28)
        self.assertEqual(constant(self.source, "WidescreenHudShotGaugeCallOriginal"), 0x0C01657D)
        self.assertEqual(constant(self.source, "WidescreenHudShotGaugeCallDelay"), 0x00003825)


if __name__ == "__main__":
    unittest.main()
