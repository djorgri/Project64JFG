"""Execute fuel caller instructions and align them against the frame matrix.

Stock slices/signatures were verified in US overlay 14. No ROM is needed.
"""

import math
import re
import unittest

from test_jfg_ammo_hud import (
    MipsMachine, SOURCE, array_body, load_word, register_word, sign_extend,
)
import test_jfg_shot_gauge as rectangle_model


STOCK_GAUGE = (
    0x240D0042, 0x240EFFA4, 0xAFAE0014, 0xAFAD0010,
    0x24050E10, 0x2406FFD3, 0x2407FFAA,
)


class JfgFuelHudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text(encoding="utf-8-sig")
        cls.patches = {
            int(offset, 16): tuple(int(word, 16) for word in words)
            for offset, *words in re.findall(
                r"\{\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
                array_body(source, "WidescreenHudFuelPatches"))
        }
        rectangle_model.JfgShotGaugeTests.setUpClass()
        cls.rectangles = rectangle_model.JfgShotGaugeTests()

    def word(self, offset, mode, original):
        if offset not in self.patches:
            return original
        patch = self.patches[offset]
        self.assertEqual(patch[0], original, "Stock instruction signature changed")
        return patch[(mode + 1) // 2] if mode & 1 else original

    def test_gauge_bars_follow_frame_in_both_resolutions(self):
        for mode, centre, bias in ((0, 160, 0), (1, 160, 48), (2, 224, 0), (3, 224, 68)):
            with self.subTest(mode=mode):
                program = {0xF58 + i * 4: self.word(0xF58 + i * 4, mode, word)
                           for i, word in enumerate(STOCK_GAUGE)}
                registers = [0] * 32
                registers[29] = 0x800FF000
                machine = MipsMachine(program, registers, {})
                machine.run(0xF58, 0xF74)
                left = sign_extend(machine.registers[6], 64)
                right = sign_extend(load_word(machine.memory, registers[29] + 0x10), 32)
                self.assertEqual(right - left, 111, "Fuel range changed")
                self.assertEqual(machine.registers[5], 3600, "Fuel maximum changed")
                self.assertEqual(machine.registers[7], register_word(-86))
                self.assertEqual(load_word(machine.memory, registers[29] + 0x14), (-92) & 0xFFFFFFFF)
                scale = .75 if mode & 1 else 1
                frame_left = centre + scale * (-50 - bias)
                # Real gauge bars are 2 pixels wide, spaced 4 pixels apart.
                # Run every bar through the actual shared rectangle hook,
                # using a caller outside the weapon-gauge wrapper.
                for delta in range(0, right - left, 4):
                    rect = (centre + left + delta, 206, centre + left + delta + 2, 212)
                    replay = self.rectangles.fixture(mode=mode, caller=0x80344444, rectangle=rect)
                    expected = [math.floor(frame_left + scale * (5 + delta + edge)) for edge in (0, 2)]
                    self.rectangles.assert_rectangle(replay, *expected)

    def test_label_and_counter_share_frame_anchor_without_changing_y(self):
        self.assertEqual(set(self.patches), {0xF58, 0xF6C, 0xFE0, 0x1030})
        for mode, centre, bias in ((0, 160, 0), (1, 160, 48), (2, 224, 0), (3, 224, 68)):
            with self.subTest(mode=mode):
                registers = [0] * 32
                registers[13], registers[15], registers[29] = centre - 50, 200, 0x800FF000
                label = MipsMachine({
                    0xFE0: self.word(0xFE0, mode, 0x25A50005),
                    0xFE4: 0x25E6FFF6, 0xFE8: 0xAFA50064, 0xFEC: 0xAFA60060,
                }, registers, {})
                label.run(0xFE0, 0xFF0)
                scale = .75 if mode & 1 else 1
                frame_left = centre + scale * (-50 - bias)
                self.assertEqual(label.registers[5], math.floor(frame_left + scale * 5))
                self.assertEqual(label.registers[6], 190)
                # The effect reads the label's saved position, then offsets it.
                counter = MipsMachine({0x1030: self.word(0x1030, mode, 0x24840080),
                                       0x1034: 0x24A50009}, registers, label.memory)
                counter.registers[4] = load_word(label.memory, registers[29] + 0x64)
                counter.registers[5] = load_word(label.memory, registers[29] + 0x60)
                counter.run(0x1030, 0x1038)
                self.assertEqual(counter.registers[4], math.floor(frame_left + scale * 133))
                self.assertEqual(counter.registers[5], 199)


if __name__ == "__main__":
    unittest.main()
