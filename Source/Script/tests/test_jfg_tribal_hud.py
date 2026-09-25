"""Check the live JFG tribal-counter patch words without requiring a ROM file.

Run with PYTHONDONTWRITEBYTECODE=1:
python -m unittest discover -s Source/Script/tests -p test_jfg_*hud.py
The unpatched instruction slices below come from the verified US overlay 14
(identical at +0x10 on PAL). Only the C++ table's replacements are substituted
into them; calls are skipped, their delay slots still run.
"""

import math
import re
import unittest

from test_jfg_ammo_hud import MASK32, SOURCE, array_body, load_word, register_word, sign_extend, store_word
from test_jfg_banner_hud import FpuMachine, bits_float, float_bits


class TribalMachine(FpuMachine):
    def execute_ordinary(self, word):
        if word >> 26 == 17 and (word >> 21) & 31 == 20 and word & 63 == 32:  # cvt.s.w
            fs, fd = (word >> 11) & 31, (word >> 6) & 31
            self.fpr[fd] = float_bits(float(sign_extend(self.fpr[fs], 32)))
        else:
            super().execute_ordinary(word)


# +0x2200..+0x244C: scissor around the icons and counts.
# +0x2450..+0x262C: icon positions, then the three counts (sprintf + fontPrintXY).
STOCK_SLICES = {
    0x2200: """
        3C018010 C42AF84C 3C0140A0 44818000 3C014320 44812000 46105481 8E300000
        46049180 3C0D0531 444FF800 26190008 35E10003 38210002 44C1F800 3C0142F0
        44815000 46003224 3C018010 C430F850 44CFF800 3C014100 44812000 46105481
        440E4000 46049181 AE390000 4458F800 35AD0040 AFAE00EC AE0D0000 8FAF003C
        37010003 38210002 44C1F800 AE0F0004 8E300000 46003224 260E0008 AE2E0000
        44D8F800 44094000 3C18E700 AE180000 AE000004 02202025 00002825 00003025
        00003825 0C015700 AFA900E8 8E300000 8FA900E8 26190008 AE390000 3C0F0028
        35EF3260 3C0DFB00 AE0D0000 AE0F0004 8E300000 3C19001E 260E0008 AE2E0000
        37395A00 3C18FA00 AE180000 AE190004 8E300000 3C0F0702 260D0008 AE2D0000
        35EF0010 AE0F0000 8FAE0038 3C014080 AE0E0004 240E0045 448E5000 44810000
        46805420 8E300000 3C190511 46008482 26180008 AE380000 37390020 4458F800
        AE190000 37010003 38210002 44C1F800 8FAD0034 46009124 44893000 44D8F800
        AE0D0004 8E300000 46803220 44192000 260F0008 AE2F0000 332D0FFF 46004282
        000D7B00 3C01ED00 01E17025 37010003 38210002 44C1F800 3C058010 46005424
        24A5F39C 44198000 44D8F800 332D0FFF 01CD7825 AE0F0000 8FB800EC 02202025
        44989000 25380011 46809120 44985000 46002182 4459F800 00000000 37210003
        38210002 44C1F800 37210003 46003224 38210002 44D9F800 440E4000 46805420
        31CD0FFF 000D7B00 46008482 44C1F800 00000000 46009124 440E2000 44D9F800
        31CD0FFF 01EDC025 0C010359 AE180004
    """,
    0x2450: """
        3C01C2B6 44814000 3C028010 2442F780 3C01C37B 44818000 C44600CC 3C0141A0
        44812000 46083281 3C014278 44813000 46105480 3C014334 44814000 46049000
        2404000D 46080280 E44001AC 4459F800 E44601B0 37210003 38210002 44C1F800
        3C013F80 46005424 44819000 44108000 44D9F800 0C016834 E45201B8 3C018010
        C424F92C 3C014240 44813000 3C018010 46062200 2404000D E428F92C 3C014000
        44815000 3C018010 0C016834 E42AF938 3C018010 C430F92C 3C014240 44819000
        3C018010 46128100 44803000 E424F92C 3C018010 2404000D 0C016834 E426F938
        0C01BF15 24040002 240E00FF AFAE0010 240400FF 240500FF 240600FF 0C01BF1A
        240700FF 00002025 00002825 00003025 0C01BF23 00003825 8FAF00D8 3C05803A
        91E60004 24A5A350 0C018E7A 27A400DC 3C0142EA 44814000 3C014278 44815000
        02202025 460A4401 02002825 444DF800 27A700DC 35A10003 38210002 44C1F800
        AFA00010 460084A4 44069000 44CDF800 0C01BF2A AFA6003C 8FB800D8 3C05803A
        93060005 26100030 24A5A354 0C018E7A 27A400DC 8FA6003C 02202025 02002825
        27A700DC 0C01BF2A AFA00010 8FB900D8 3C05803A 93260006 26100030 24A5A358
        0C018E7A 27A400DC 8FA6003C 02202025 02002825 27A700DC 0C01BF2A AFA00010
    """,
}
STOCK_PROGRAM = {
    start + 4 * index: int(word, 16)
    for start, words in STOCK_SLICES.items()
    for index, word in enumerate(words.split())
}
FONT_PRINT = 0x0C01BF2A  # jal fontPrintXY
CAP_X, CAP_Y, ICON_X = 0x800FF84C, 0x800FF850, 0x800FF92C


class JfgTribalHudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text(encoding="utf-8-sig")
        rows = re.findall(
            r"\{\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
            array_body(source, "WidescreenHudTribalPatches"))
        cls.patch_rows = [tuple(int(value, 16) for value in row) for row in rows]
        cls.patches = {offset: (original, low, high) for offset, original, low, high in cls.patch_rows}
        cls.sp, cls.display_list, cls.display_pointer, cls.counts = 0x800FE000, 0x80200000, 0x800FF398, 0x80210000

    def run_slice(self, start, variant, cap_x, fcsr):
        registers = [0] + [register_word(0x1000 + index) for index in range(1, 32)]
        registers[17] = register_word(self.display_pointer)
        registers[29] = register_word(self.sp)
        memory = {}
        for address, value in {
            self.display_pointer: self.display_list,
            CAP_X: float_bits(cap_x),
            CAP_Y: float_bits(61.0),
            self.sp + 0x34: 0x80300000, self.sp + 0x38: 0x80300100, self.sp + 0x3C: 0x80300200,
            self.sp + 0xD8: self.counts,
            self.counts + 4: 0x01020300,
        }.items():
            store_word(memory, address, value)
        machine = TribalMachine({}, registers, memory, fcsr=fcsr)
        prints = []
        for index, stock in enumerate(STOCK_SLICES[start].split()):
            offset = start + index * 4
            word = self.patches[offset][variant] if offset in self.patches else int(stock, 16)
            if word >> 26 == 3:
                if word == FONT_PRINT:
                    prints.append(sign_extend(machine.registers[5], 32))
                continue  # skip the call itself; the delay slot follows
            machine.execute_ordinary(word)
        return machine, prints

    def test_patch_signatures_match_the_stock_overlay_and_every_word_changes(self):
        self.assertEqual(len(self.patch_rows), 11)
        self.assertEqual(len(self.patches), 11)
        for offset, (original, low, high) in self.patches.items():
            self.assertEqual(STOCK_PROGRAM[offset], original, "Wrong original at overlay +%#x" % offset)
            self.assertNotEqual(low, original)
            self.assertNotEqual(high, original)

    def test_counts_follow_the_left_anchored_icons_with_stock_truncation(self):
        for variant, bias in ((1, 4), (2, 53)):
            for cap_x in range(-90, 70):
                for rounding_mode in range(4):
                    with self.subTest(variant=variant, cap_x=cap_x, rounding=rounding_mode):
                        fcsr = 0x01000004 | rounding_mode
                        patched, printed = self.run_slice(0x2450, variant, cap_x, fcsr)
                        stock, stock_printed = self.run_slice(0x2450, 0, cap_x, fcsr)
                        self.assertEqual(len(printed), 3)
                        self.assertEqual(stock_printed, [math.trunc(cap_x + 40.0) + 48 * k for k in range(3)])
                        # Screen mapping of the weapon group: x' = .75 * x + 4 / 53.
                        base = math.trunc(0.75 * (cap_x + 91)) + (-34 if variant == 1 else 15)
                        for k in range(3):
                            self.assertEqual(printed[k], base + 36 * k)
                            self.assertLessEqual(abs(printed[k] - (0.75 * (cap_x + 40 + 48 * k) + bias)), 1.0)
                        # Icons, the counts' Y and the FPU state are untouched.
                        self.assertEqual(load_word(patched.memory, ICON_X), load_word(stock.memory, ICON_X))
                        self.assertEqual(patched.registers[6], stock.registers[6])
                        self.assertEqual(patched.fcsr, fcsr)

    def test_scissor_moves_with_the_cap_and_keeps_its_rows(self):
        for variant, left, right_bias in ((1, 56, 120), (2, 105, 169)):
            for cap_x in range(-90, 70):
                with self.subTest(variant=variant, cap_x=cap_x):
                    fcsr = 0x01000004
                    patched, _ = self.run_slice(0x2200, variant, cap_x, fcsr)
                    stock, _ = self.run_slice(0x2200, 0, cap_x, fcsr)

                    def scissor(machine):
                        for offset in range(0, 0x100, 8):
                            first = load_word(machine.memory, self.display_list + offset)
                            if first >> 24 == 0xED:
                                return first, load_word(machine.memory, self.display_list + offset + 4)
                        self.fail("No SetScissor command")

                    (p0, p1), (s0, s1) = scissor(patched), scissor(stock)
                    self.assertEqual((s0 >> 12) & 0xFFF, 69 * 4)
                    self.assertEqual((s1 >> 12) & 0xFFF, math.trunc(cap_x + 155.0) * 4)
                    self.assertEqual((p0 >> 12) & 0xFFF, left * 4)
                    self.assertEqual((p1 >> 12) & 0xFFF, math.trunc(0.75 * cap_x + right_bias) * 4)
                    self.assertEqual(p0 & 0xFFF, s0 & 0xFFF)
                    self.assertEqual(p1 & 0xFFF, s1 & 0xFFF)
                    self.assertEqual(patched.fcsr, fcsr)


if __name__ == "__main__":
    unittest.main()
