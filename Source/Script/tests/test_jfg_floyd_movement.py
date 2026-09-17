"""Execute the production flight hooks through the game's XYZ integration.

No ROM is needed: the native integration instructions are a short US fixture.
The test runs actual patch words, including FPU branches and MIPS delay slots.
"""

import math
import unittest

from test_jfg_ammo_hud import SOURCE, array_body, constant, load_word, register_word, sign_extend, store_word
from test_jfg_banner_hud import FpuMachine, bits_float, float_bits


class FlightMachine(FpuMachine):
    def execute_ordinary(self, word):
        opcode, fmt, function = word >> 26, (word >> 21) & 31, word & 63
        if opcode == 17 and fmt == 16 and function in (3, 4, 5, 62):
            ft, fs, fd = (word >> 16) & 31, (word >> 11) & 31, (word >> 6) & 31
            a, b = bits_float(self.fpr[fs]), bits_float(self.fpr[ft])
            if function == 62:  # c.le.s
                self.fcsr = (self.fcsr & ~(1 << 23)) | (int(a <= b) << 23)
            else:
                value = a / b if function == 3 else math.sqrt(a) if function == 4 else abs(a)
                self.fpr[fd] = float_bits(value)
        else:
            super().execute_ordinary(word)

    def run(self, start, resume):
        pc = start
        for _ in range(200):
            if pc == resume:
                return
            self.visited.append(pc)
            word = self.program[pc]
            op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
            if op in (2, 4, 5) or (op == 17 and rs == 8):
                if op == 2:
                    target = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                else:
                    taken = bool(self.fcsr & (1 << 23)) == bool(rt & 1) if op == 17 else \
                        (self.registers[rs] == self.registers[rt]) == (op == 4)
                    target = pc + 4 + 4 * sign_extend(word & 0xFFFF, 16) if taken else pc + 8
                self.visited.append(pc + 4)
                self.execute_ordinary(self.program[pc + 4])
                pc = target & 0xFFFFFFFF
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("Flight hook did not return to native code")


INTEGRATION = [int(word, 16) for word in """
46043282 460A4180 E6060000 8FAA0120 C7A800AC C5440020 C6060004 46082282
460A3100 E6040004 8FAC0120 C7A600AC C5880024 C6040008 46064282 460A2200 E6080008
""".split()]
OBJECT, DATA, STACK = 0x80200000, 0x80200100, 0x803FF000


class FloydMovementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.entry = constant(cls.source, "SidekickStrafeEntry")
        cls.stub = constant(cls.source, "SidekickStrafeStub")
        cls.vertical = constant(cls.source, "SidekickVerticalStub")
        cls.program = {cls.entry: 0x08000000 | (cls.stub >> 2 & 0x3FFFFFF), cls.entry + 4: 0xC7A400AC}
        for name, address in (("SidekickStrafeHookCode", cls.stub), ("SidekickVerticalHookCode", cls.vertical)):
            body = array_body(cls.source, name).replace("SidekickStrafeDelayOriginal", "0xC6080000")
            words = [int(word.strip(), 0) for word in body.split(",") if word.strip()]
            cls.program.update({address + i * 4: word for i, word in enumerate(words)})
        cls.program.update({cls.entry + 8 + i * 4: word for i, word in enumerate(INTEGRATION)})
        cls.end = cls.entry + 8 + 4 * len(INTEGRATION)

    def fixture(self, thrust=0.15, lateral=0.0, step=1, native=(0, 0, 0), yaw=0, active=True):
        registers = [0] + [register_word(0x12340000 + i) for i in range(1, 32)]
        registers[1], registers[16], registers[29] = register_word(0x800B0000), register_word(DATA), register_word(STACK)
        fpr = [float_bits(i + 0.25) for i in range(32)]
        fpr[6] = float_bits(native[0])
        memory = {}
        words = {STACK + 0x120: OBJECT, STACK + 0xAC: float_bits(step), DATA + 0x34: float_bits(0),
                 0x8009FCE4: int(active), 0x8009FCE8: 0,
                 0x8009FCA4: float_bits(8), 0x8009FCA8: float_bits(lateral), 0x8009FCAC: float_bits(0),
                 0x8009FCC0: float_bits(0.975), 0x8009FCC8: float_bits(math.cos(yaw)),
                 0x8009FCB0: float_bits(-math.sin(yaw)), 0x8009FCD0: float_bits(thrust),
                 0x8009FCD4: float_bits(0), DATA + 0x60: 0x12345678}
        for axis in range(3):
            words[OBJECT + 0x1C + axis * 4] = float_bits(native[axis])
            words[DATA + axis * 4] = float_bits(100 * (axis + 1))
            words[DATA + 0x28 + axis * 4] = float_bits(0.25 + axis)
        for address, value in words.items():
            store_word(memory, address, value)
        return FlightMachine(self.program, registers, memory, fpr)

    def frame(self, vm, native=(0, 0, 0)):
        for axis in range(3):
            store_word(vm.memory, OBJECT + 0x1C + axis * 4, float_bits(native[axis]))
        vm.fpr[6] = float_bits(native[0])
        vm.run(self.entry, self.end)
        return tuple(bits_float(load_word(vm.memory, OBJECT + 0x1C + axis * 4)) for axis in range(3))

    def test_jump_and_crouch_accelerate_symmetrically_at_both_frame_steps(self):
        for step in (1, 2):
            up, down = self.fixture(step=step), self.fixture(thrust=-0.15, step=step)
            previous = 0
            for _ in range(120):
                a, b = self.frame(up), self.frame(down)
                self.assertEqual(a[0::2], (0, 0))
                self.assertAlmostEqual(a[1], -b[1])
                self.assertGreaterEqual(a[1], previous)
                self.assertLessEqual(a[1], 8)
                previous = a[1]
            self.assertGreater(bits_float(load_word(up.memory, DATA + 4)), 200)
            self.assertLess(bits_float(load_word(down.memory, DATA + 4)), 200)

    def test_release_brakes_gradually_and_opposite_thrust_reverses(self):
        vm = self.fixture()
        for _ in range(30):
            self.frame(vm)
        before = bits_float(load_word(vm.memory, 0x8009FCD4))
        store_word(vm.memory, 0x8009FCD0, float_bits(0))
        after = self.frame(vm)[1]
        self.assertGreater(after, 0)
        self.assertAlmostEqual(after, before * 0.975, places=5)
        store_word(vm.memory, 0x8009FCD0, float_bits(-0.15))
        for _ in range(60):
            result = self.frame(vm)
        self.assertLess(result[1], 0)

    def test_world_vertical_is_independent_of_heading_and_preserves_facing(self):
        for yaw in (0, math.pi / 2, math.pi, -math.pi / 2):
            vm = self.fixture(yaw=yaw, step=2)
            heading = [load_word(vm.memory, DATA + offset) for offset in (0x28, 0x2C, 0x30, 0x60)]
            result = self.frame(vm)
            self.assertAlmostEqual(result[1], 0.3)
            self.assertEqual(result[0::2], (0, 0))
            self.assertEqual(heading, [load_word(vm.memory, DATA + offset) for offset in (0x28, 0x2C, 0x30, 0x60)])

    def test_diagonal_flight_caps_total_speed_and_integrates_all_axes(self):
        for native in ((0, 0, 8), (3, 4, 5), (0, -8, 0)):
            vm = self.fixture(lateral=0.15, native=native)
            for _ in range(200):
                position = [bits_float(load_word(vm.memory, DATA + 4*i)) for i in range(3)]
                result = self.frame(vm, native)
                self.assertLessEqual(math.sqrt(sum(x*x for x in result)), 8.000001)
                for axis, value in enumerate(result):
                    after = bits_float(load_word(vm.memory, DATA + 4*axis))
                    self.assertAlmostEqual(after - position[axis], value, delta=0.0002)

    def test_disabled_hook_preserves_native_integration_and_live_registers(self):
        vm = self.fixture(active=False, step=2, native=(3, -4, 5))
        original_gpr, original_fpr = list(vm.registers), list(vm.fpr)
        self.assertEqual(self.frame(vm, (3, -4, 5)), (3, -4, 5))
        self.assertEqual(tuple(bits_float(load_word(vm.memory, DATA + 4*i)) for i in range(3)), (106, 192, 310))
        for reg in (1, 16, 29, 31):
            self.assertEqual(vm.registers[reg], original_gpr[reg])
        for reg in (0, 12, 20, 22, 24, 26, 28, 30):
            self.assertEqual(vm.fpr[reg], original_fpr[reg])
        self.assertEqual(load_word(vm.memory, 0x8009FCD4), float_bits(0))

    def test_lateral_only_keeps_existing_ramp(self):
        vm = self.fixture(thrust=0, lateral=0.15)
        first, second = self.frame(vm), self.frame(vm)
        self.assertAlmostEqual(first[0], 0.15)
        self.assertAlmostEqual(second[0], 0.29625)
        self.assertEqual(second[1:], (0, 0))


if __name__ == "__main__":
    unittest.main()
