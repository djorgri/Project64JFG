"""Execute the Floyd contour call-site wrapper and the real shared line hook.

The 22 native records are copied from verified US overlay 14 +0x4678. These
tests do not require a ROM or save state. Pixel coverage of the marked line
renderer is checked separately; here the contract is the original call ABI,
the outline's position relative to the existing sprite, and mode transitions.
"""

import math
import struct
import unittest

from test_jfg_ammo_hud import (
    MASK32, MASK64, SOURCE, code_array, constant, load_word, register_word,
    sign_extend, store_word,
)
from test_jfg_shot_gauge import CallMachine


NATIVE_FLOYD_LINES = (
    (-22, 3, -22, 9, 13), (-21, 10, -10, 21, 13),
    (-9, 22, 9, 22, 0), (-8, 21, 8, 21, 0),
    (9, 21, 20, 10, 13), (21, 9, 21, 3, 13),
    (-16, 16, -18, 18, 12), (0, 25, 0, 23, 13),
    (16, 16, 18, 18, 12), (-31, 0, -28, 0, 12),
    (-25, 0, -18, 0, 12), (18, 0, 25, 0, 12),
    (28, 0, 31, 0, 12), (-22, -2, -22, -8, 13),
    (-21, -9, -10, -20, 13), (-9, -21, 9, -21, 0),
    (-8, -20, 8, -20, 0), (9, -20, 20, -9, 13),
    (21, -8, 21, -2, 13), (-16, -16, -18, -18, 12),
    (0, -24, 0, -22, 13), (16, -16, 18, -18, 12),
)


class QueueMachine(CallMachine):
    def execute_ordinary(self, word):
        if word >> 26 == 11:  # sltiu, unsigned GPR against sign-extended immediate
            rs, rt = (word >> 21) & 31, (word >> 16) & 31
            self.write_register(rt, int(self.registers[rs] < (sign_extend(word & 0xFFFF, 16) & MASK64)))
        else:
            super().execute_ordinary(word)


class JfgFloydQueueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.scope = constant(cls.source, "WidescreenHudScopeDepthAddress")
        cls.mode = constant(cls.source, "WidescreenHudResolutionIndexAddress")
        cls.wrapper = constant(cls.source, "WidescreenHudFloydLineStub")
        cls.line = constant(cls.source, "WidescreenHudLineStub")
        cls.line_entry = constant(cls.source, "WidescreenHudLineEntry")
        cls.program = {}
        for name in ("FloydLine", "Line"):
            start = constant(cls.source, "WidescreenHud" + name + "Stub")
            words = code_array(cls.source, "WidescreenHud" + name + "Code")
            cls.program.update({start + i * 4: word for i, word in enumerate(words)})
        cls.program[cls.line_entry] = 0x08000000 | (cls.line >> 2 & 0x03FFFFFF)
        cls.program[cls.line_entry + 4] = 0
        cls.sp = 0x800FC000
        cls.table = 0x8035C8C8
        cls.native_bytes = b"".join(struct.pack(">hhhhh", *record) for record in NATIVE_FLOYD_LINES)

    def fixture(self, record, mode=1, scope=1, centre=None):
        centre = centre or ((380, 268) if mode & 2 else (272, 192))
        x0, y0, x1, y1, style = record
        registers = [0] + [0x1122334400000000 | i * 0x10203 for i in range(1, 32)]
        for register, value in {
            4: centre[0] + x0, 5: centre[1] + y0,
            6: centre[0] + x1, 7: centre[1] + y1,
            16: self.table, 29: self.sp, 31: 0x803586C0,
        }.items():
            registers[register] = register_word(value)
        memory = {self.scope: scope, self.mode: mode}
        for address, value in {
            self.sp + 0x10: style, self.sp + 0xDC: centre[1],
            self.sp + 0xE0: centre[0], self.sp + 0x44: 0x80358E9C,
            0x80103B90: 1,
        }.items():
            store_word(memory, address, value)
        memory.update({self.table + i: byte for i, byte in enumerate(self.native_bytes)})
        vm = QueueMachine(self.program, registers, memory, fcsr=0x01800007)
        vm.hi, vm.lo = 0x123456789ABCDEF0, 0xFEDCBA9876543210
        return vm

    def draw(self, record, mode=1, scope=1, centre=None):
        vm = self.fixture(record, mode, scope, centre)
        registers, memory = list(vm.registers), dict(vm.memory)
        fpr, fcsr, hilo = list(vm.fpr), vm.fcsr, (vm.hi, vm.lo)
        vm.run(self.wrapper, self.line_entry + 8)
        active = scope != 0 and mode & 1
        if active:
            target_x, target_y = (359, 240) if mode & 2 else (280, 192)
            expected = (target_x + math.floor(record[0] * .75), target_y + record[1],
                        target_x + math.floor(record[2] * .75), target_y + record[3])
        else:
            expected = tuple(registers[i] for i in range(4, 8))
        self.assertEqual(tuple(vm.registers[4:8]), tuple(register_word(v) for v in expected))
        expected_style = record[4] | 0x40 if active and record[4] & 15 == 13 else record[4]
        store_word(memory, self.sp + 0x10, expected_style)
        self.assertEqual(vm.memory, memory, "The contour must not change its source table or caller locals")
        self.assertEqual(load_word(vm.memory, self.sp + 0x10), expected_style)
        self.assertEqual(bytes(vm.memory[self.table + i] for i in range(len(self.native_bytes))),
                         self.native_bytes)
        # The tail wrapper and stock line prologue use only argument registers
        # and t6..t9. In particular s0 (table), saved registers, SP and RA survive.
        for register in set(range(32)) - {4, 5, 6, 7, 14, 15, 24, 25}:
            self.assertEqual(vm.registers[register], registers[register], "Changed GPR %d" % register)
        self.assertEqual(vm.fpr, fpr)
        self.assertEqual(vm.fcsr, fcsr)
        self.assertEqual((vm.hi, vm.lo), hilo)
        return vm

    def test_all_native_segments_follow_the_existing_icon_anchor(self):
        for mode in (1, 3):
            for record in NATIVE_FLOYD_LINES:
                with self.subTest(mode=mode, record=record):
                    self.draw(record, mode)

    def test_modes_scope_and_repeated_transitions_before_host_cleanup(self):
        for mode, scope in ((1, 1), (0, 1), (1, 0), (0, 0), (1, 1),
                            (3, 1), (2, 1), (3, 0), (2, 0), (3, 1)):
            for record in NATIVE_FLOYD_LINES:
                with self.subTest(mode=mode, scope=scope, record=record):
                    self.draw(record, mode, scope)

    def test_relocation_uses_the_actual_converted_centre(self):
        # A video-mode transition may leave caller coordinates from the prior
        # mode. Reading its centre makes the destination independent of that.
        for mode in (1, 3):
            for centre in ((272, 192), (380, 268), (300, 200)):
                for record in NATIVE_FLOYD_LINES:
                    with self.subTest(mode=mode, centre=centre, record=record):
                        self.draw(record, mode, centre=centre)

    def test_only_the_orthogonal_and_diagonal_style_13_is_marked(self):
        for style in (0, 1, 2, 3, 12, 13, 14, 15, 0x1D, 0x4D, 0xAD):
            for mode in range(4):
                with self.subTest(style=style, mode=mode):
                    self.draw((-21, 10, -10, 21, style), mode)

    def test_wrapper_guards_do_not_read_the_floyd_frame_when_inactive(self):
        for mode, scope in ((0, 1), (2, 1), (1, 0), (3, 0), (4, 1), (5, 1), (255, 1)):
            with self.subTest(mode=mode, scope=scope):
                vm = self.fixture(NATIVE_FLOYD_LINES[1], mode, scope)
                for address in (self.sp + 0xE0, self.sp + 0xDC):
                    for offset in range(4):
                        del vm.memory[address + offset]
                arguments, memory = list(vm.registers[4:8]), dict(vm.memory)
                vm.run(self.wrapper, self.line_entry)
                self.assertEqual(vm.registers[4:8], arguments)
                self.assertEqual(vm.memory, memory)

    def test_outline_centre_remains_within_one_pixel_of_the_unchanged_sprite_pivot(self):
        # The sprite uses world (114,-72), existing right-edge bias, and the
        # original billboard divide by C+1. None of these are part of this fix.
        for mode, width, height, bias, expected in (
            (1, 320, 240, 48, (280, 192)), (3, 448, 336, 68, (359, 240)),
        ):
            vm = self.draw((0, 0, 0, 0, 0), mode)
            half = width / 2
            pivot = (half + .75 * (114 + bias) * half / (half + 1),
                     height / 2 + 72 * half / (half + 1))
            self.assertEqual((vm.registers[4], vm.registers[5]), expected)
            for actual, target in zip(expected, pivot):
                self.assertLess(abs(actual - target), 1)

    def test_shared_endpoints_do_not_depend_on_line_direction_or_style(self):
        for mode in (1, 3):
            for record in NATIVE_FLOYD_LINES:
                x0, y0, x1, y1, style = record
                direct = self.draw(record, mode)
                reverse = self.draw((x1, y1, x0, y0, style), mode)
                self.assertEqual(direct.registers[4:6], reverse.registers[6:8])
                self.assertEqual(direct.registers[6:8], reverse.registers[4:6])

    def test_targeted_wrapper_does_not_overlap_the_line_painter_or_alignment(self):
        words = code_array(self.source, "WidescreenHudFloydLineCode")
        self.assertEqual(self.wrapper, 0x80067844)
        self.assertLessEqual(self.wrapper + len(words) * 4, 0x800678C4)
        end = constant(self.source, "WidescreenHudReticleCaveEnd")
        self.assertEqual(end, 0x80067950)
        self.assertLess(end, 0x80067994)  # Separate alignment diagnostic guard.


if __name__ == "__main__":
    unittest.main()
