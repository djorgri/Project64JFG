"""Execute the real reticle trampoline up to the stock clipping wrapper.

Run with PYTHONDONTWRITEBYTECODE=1:
python -m unittest discover -s Source/Script/tests -p test_jfg_*hud.py
Tests model the existing JAL's link and execute each original delay before the
trampoline. They stop at fxDrawLineInWindow's original prologue, before clipping.
"""

import re
import unittest

from test_jfg_ammo_hud import (
    SOURCE, array_body, code_array, constant, register_word, sign_extend, store_word,
)
from test_jfg_banner_hud import FpuMachine, float_bits


def symmetric_three_quarters(value):
    """Nearest integer, half-way cases away from zero; independent reference."""
    magnitude, remainder = divmod(abs(value) * 3, 4)
    magnitude += remainder >= 2
    return -magnitude if value < 0 else magnitude


class JfgReticleHudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.stub = constant(cls.source, "WidescreenHudReticleStub")
        cls.end = constant(cls.source, "WidescreenHudReticleCaveEnd")
        cls.words = code_array(cls.source, "WidescreenHudReticleCode")
        cls.scope_address = constant(cls.source, "WidescreenHudScopeDepthAddress")
        cls.resolution_address = constant(cls.source, "WidescreenHudResolutionIndexAddress")
        cls.target = 0x8006D58C
        cls.sp, cls.overlay = 0x800FE000, 0x8034D780
        cls.calls = [tuple(int(value, 16) for value in row) for row in re.findall(
            r"\{\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
            array_body(cls.source, "WidescreenHudReticleCalls"))]

    def fixture(self, centre, first, second, scope=0, resolution=1):
        registers = [0] + [0x1122334400000000 | index * 0x10203 for index in range(1, 32)]
        for register, value in {4: first, 5: 73, 6: second, 7: 89, 20: centre,
                                29: self.sp, 31: self.overlay + 0xC70}.items():
            registers[register] = register_word(value)
        memory = {self.scope_address: scope, self.resolution_address: resolution}
        for offset in range(0, 0x20, 4):
            store_word(memory, self.sp + offset, 0xCA000000 + offset)
        fpr = [float_bits(index + 0.25) for index in range(32)]
        program = {self.stub + 4 * index: word for index, word in enumerate(self.words)}
        machine = FpuMachine(program, registers, memory, fpr=fpr, fcsr=0x01800004)
        machine.hi, machine.lo = 0x0123456789ABCDEF, 0xFEDCBA9876543210
        return machine

    def assert_run(self, machine, scope, resolution):
        before_registers, before_memory = list(machine.registers), dict(machine.memory)
        before_fpr, before_fcsr = list(machine.fpr), machine.fcsr
        before_hilo = machine.hi, machine.lo
        centre = sign_extend(before_registers[20], 64)
        expected = list(before_registers)
        if scope == 0 and resolution & 1:
            for register in (4, 6):
                endpoint = sign_extend(before_registers[register], 64)
                expected[register] = register_word(centre + symmetric_three_quarters(endpoint - centre))
        machine.run(self.stub, self.target)
        # Only X endpoints and the two caller-saved scratch registers may change.
        for register in set(range(32)) - {8, 9}:
            self.assertEqual(machine.registers[register], expected[register], "Changed GPR %d" % register)
        self.assertEqual(machine.memory, before_memory)
        self.assertEqual(machine.fpr, before_fpr)
        self.assertEqual(machine.fcsr, before_fcsr)
        self.assertEqual((machine.hi, machine.lo), before_hilo)
        self.assertTrue(all(self.stub <= address < self.end for address in machine.visited))
        return machine

    def test_tail_target_and_exactly_seven_local_calls(self):
        self.assertEqual((self.stub, self.stub + len(self.words) * 4, len(self.words)),
                         (0x80067790, 0x800677F4, 25))
        self.assertLessEqual(self.stub + len(self.words) * 4, self.end)
        self.assertEqual(self.end, 0x80067950)
        self.assertLess(self.end, 0x80067994)  # Independent alignment guard.
        self.assertEqual(constant(self.source, "WidescreenHudReticleOverlayModule"), 13)
        self.assertEqual(constant(self.source, "WidescreenHudReticleDrawOffset"), 0x4A8)
        original = constant(self.source, "WidescreenHudReticleLineCallOriginal")
        self.assertEqual(original, 0x0C01B563)
        self.assertEqual(0x80000000 | ((original & 0x03FFFFFF) << 2), self.target)
        self.assertEqual(self.words[-2:], [0x0801B563, 0])
        self.assertEqual(self.calls, [
            (0xC68, 0x02C08025), (0xCF4, 0xAFAB0010), (0xD48, 0xAFB10010),
            (0xD9C, 0xAFB80010), (0xDFC, 0xAFB00014), (0xE4C, 0xAFA30010),
            (0xE98, 0xAFB10010),
        ])
        # Original four screen-edge frame calls (types 5/6) are not hooked.
        self.assertFalse({offset for offset, _ in self.calls}.intersection({0xB10, 0xB60, 0xBBC, 0xC0C}))

    def test_scope_modes_off_centre_aim_and_signed_rounding(self):
        for scope in (0, 1):
            for resolution in range(4):
                for centre in (-32, 0, 1, 17, 160, 224, 319, 447, 480):
                    for delta in range(-64, 65):
                        with self.subTest(scope=scope, resolution=resolution, centre=centre, delta=delta):
                            # Asymmetric endpoints catch accidental reuse of a0
                            # while the second endpoint is being transformed.
                            machine = self.fixture(centre, centre + delta, centre + 17 - delta, scope, resolution)
                            self.assert_run(machine, scope, resolution)

    def test_mirrored_segments_are_symmetric_and_aim_centre_does_not_move(self):
        for centre in (0, 17, 160, 224, 447):
            for resolution in (1, 3):
                for distance in range(0, 257):
                    with self.subTest(centre=centre, resolution=resolution, distance=distance):
                        machine = self.fixture(centre, centre - distance, centre + distance, resolution=resolution)
                        self.assert_run(machine, 0, resolution)
                        first, second = (sign_extend(machine.registers[register], 64) for register in (4, 6))
                        self.assertEqual(first + second, centre * 2)
                        self.assertEqual(second - centre, symmetric_three_quarters(distance))
                        self.assertEqual(machine.registers[20], register_word(centre))

    def test_every_original_delay_and_stack_arguments_survive_the_tail_call(self):
        for offset, delay in self.calls:
            for scope in (0, 1):
                for resolution in range(4):
                    with self.subTest(offset=hex(offset), scope=scope, resolution=resolution):
                        machine = self.fixture(17, -2, 35, scope, resolution)
                        return_address = register_word(self.overlay + offset + 8)
                        machine.registers[31] = return_address  # original JAL link
                        machine.execute_ordinary(delay)  # unchanged caller delay executes once
                        self.assert_run(machine, scope, resolution)
                        self.assertEqual(machine.registers[31], return_address)

    def test_endpoints_are_transformed_before_stock_clipping(self):
        for resolution, width in ((1, 320), (3, 448)):
            cases = ((10, -2, 20), (10, -6, 20), (width - 11, width + 1, width - 21))
            for centre, first, second in cases:
                with self.subTest(resolution=resolution, centre=centre, first=first):
                    machine = self.fixture(centre, first, second, resolution=resolution)
                    self.assert_run(machine, 0, resolution)
                    output = sign_extend(machine.registers[4], 64)
                    transform = lambda x: centre + symmetric_three_quarters(x - centre)
                    clip = lambda x: min(width - 1, max(0, x))
                    self.assertEqual(output, transform(first))
                    self.assertNotEqual(clip(output), transform(clip(first)))
                    # An endpoint may remain outside until the ORIGINAL wrapper
                    # clips/rasterizes it. The stub itself must not clamp it.
                    if first == -6:
                        self.assertEqual(output, -2)


if __name__ == "__main__":
    unittest.main()
