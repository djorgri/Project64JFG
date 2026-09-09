"""Check the live JFG pickup-banner patch words without requiring a ROM file.

Run with PYTHONDONTWRITEBYTECODE=1:
python -m unittest discover -s Source/Script/tests -p test_jfg_*hud.py
The unpatched instruction slices below come from the verified US overlay 14.
Only the actual C++ table's replacements are substituted into those slices.
Font drawing calls themselves are outside this integer/FPU model.
"""

import math
import re
import struct
import unittest

from test_jfg_ammo_hud import (
    MASK32, MipsMachine, SOURCE, array_body, code_array, constant,
    load_word, register_word, sign_extend, store_word,
)


def float_bits(value):
    return struct.unpack(">I", struct.pack(">f", value))[0]


def bits_float(value):
    return struct.unpack(">f", struct.pack(">I", value & MASK32))[0]


class FpuMachine(MipsMachine):
    def __init__(self, program, registers, memory, fpr=None, fcsr=0):
        super().__init__(program, registers, memory)
        self.fpr = list(fpr) if fpr is not None else [float_bits(i + 0.25) for i in range(32)]
        self.fcsr = fcsr

    def execute_ordinary(self, word):
        opcode, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        if opcode == 14:  # xori
            self.write_register(rt, self.registers[rs] ^ (word & 0xFFFF))
        elif opcode in (49, 57):  # lwc1 / swc1
            address = (self.registers[rs] + sign_extend(word & 0xFFFF, 16)) & MASK32
            if opcode == 49:
                self.fpr[rt] = load_word(self.memory, address)
            else:
                store_word(self.memory, address, self.fpr[rt])
        elif opcode == 17:  # COP1
            fs, fd, function = (word >> 11) & 31, (word >> 6) & 31, word & 63
            if rs == 0:  # mfc1: converted integers are raw FPR bits
                self.write_register(rt, register_word(self.fpr[fs]))
            elif rs == 2:  # cfc1
                if fs != 31:
                    raise AssertionError("Only the FCSR control register is modelled")
                self.write_register(rt, register_word(self.fcsr))
            elif rs == 4:  # mtc1
                self.fpr[fs] = self.registers[rt] & MASK32
            elif rs == 6:  # ctc1
                if fs != 31:
                    raise AssertionError("Only the FCSR control register is modelled")
                self.fcsr = self.registers[rt] & MASK32
            elif rs == 16 and function in (0, 1, 2):  # add.s / sub.s / mul.s
                left, right = bits_float(self.fpr[fs]), bits_float(self.fpr[rt])
                value = (left + right) if function == 0 else (left - right) if function == 1 else left * right
                self.fpr[fd] = float_bits(value)
            elif rs == 16 and function == 36:  # cvt.w.s, respecting the FCSR rounding mode
                value = bits_float(self.fpr[fs])
                rounding = (round, math.trunc, math.ceil, math.floor)[self.fcsr & 3]
                converted = rounding(value)
                self.fpr[fd] = converted & MASK32
                if converted != value:
                    self.fcsr |= 0x1004  # inexact cause/flag; stock ctc1 must restore them
            else:
                raise AssertionError("Unsupported COP1 instruction: %#010x" % word)
        else:
            super().execute_ordinary(word)


# Contiguous slices immediately before the scissor and fontPrintXY calls.
# Retaining the original cfc1/ctc1 sequences tests FCSR restoration, and keeping
# the Y calculations catches accidental reuse of f0's vertical factor of four.
STOCK_SLICES = {
    0x1D98: """
        3C014080 44810000 3C0142F0 44811000 3C014100 44816000 3C018010 C432F82C
        3C0142F4 44812000 3C0142B4 44814000 8E300000 3C018010 C430F830 46049181
        260F0008 AE2F0000 C424F830 46083280 00002025 460C2180 E7AA0118 46061201
        00002825 46004282 46101481 00003025 E7B20114 4458F800 240700FF 37010003
        38210002 44C1F800 3C01ED14 46005424 34218000 44198000 44D8F800 332E0FFF
        01C16825 3C018010 AE0D0000 C432F82C 3C014320 44812000 00000000 46049180
        46003202 444FF800 00000000 35E10003 38210002 44C1F800 3C018010 460042A4
        C430F830 44CFF800 44185000 460C8481 33190FFF 46121101 00197300 46002182
        444DF800 00000000 35A10003 38210002 44C1F800 00000000 46003224 440F4000
        44CDF800 31F80FFF 01D8C825 240D00FF AE190004
    """,
    0x1ED4: """
        444FF800 C7AA0118 35E10003 38210002 44C1F800 C7B20114 46005424 8FA700F8
        44CFF800 44058000 24180008 444EF800 AFB80010 35C10003 38210002 44C1F800
        02202025 46009124 44062000 44CEF800
    """,
    0x1F48: """
        3C013F80 44810000 C7A60118 C7B00114 46003200 8FA700F8 444DF800 240E0008
        35A10003 38210002 44C1F800 AFAE0010 460042A4 02202025 44CDF800 44055000
        46008480 444FF800 00000000 35E10003 38210002 44C1F800 00000000 46009124
        44062000 44CFF800
    """,
}
STOCK_PROGRAM = {
    start + 4 * index: int(word, 16)
    for start, words in STOCK_SLICES.items()
    for index, word in enumerate(words.split())
}


class JfgBannerHudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.sprite_words = code_array(cls.source, "WidescreenHudSpritePositionCode")
        cls.sprite_stub = constant(cls.source, "WidescreenHudSpritePositionStub")
        cls.scope_address = constant(cls.source, "WidescreenHudScopeDepthAddress")
        cls.resolution_address = constant(cls.source, "WidescreenHudResolutionIndexAddress")
        rows = re.findall(
            r"\{\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
            array_body(cls.source, "WidescreenHudBannerPatches"))
        cls.patch_rows = [tuple(int(value, 16) for value in row) for row in rows]
        cls.patches = {offset: (original, low, high) for offset, original, low, high in cls.patch_rows}
        cls.sp, cls.display_list, cls.display_pointer = 0x800FE000, 0x80200000, 0x800FF300

    def test_cap_keeps_left_anchor_through_entire_slide_and_other_sprites_keep_thresholds(self):
        program = {self.sprite_stub + index * 4: word for index, word in enumerate(self.sprite_words)}
        self.assertEqual(len(self.sprite_words), 30)
        for scope in (0, 1):
            for resolution in range(4):
                for object_address in (0x800FF820, 0x800FF7D0, 0x800FF870):
                    for x in range(-78, 123):  # Includes the exact -31 and +32 boundaries.
                        with self.subTest(scope=scope, resolution=resolution, object=hex(object_address), x=x):
                            registers = [0] + [0x1122334400000000 | (i * 0x10203) for i in range(1, 32)]
                            registers[3] = register_word(object_address)
                            fpr = [float_bits(i + 0.25) for i in range(32)]
                            fpr[6] = x & MASK32  # The original game already converted f6 to an integer.
                            memory = {self.scope_address: scope, self.resolution_address: resolution}
                            machine = FpuMachine(program, registers, memory, fpr, fcsr=0x01800004)
                            machine.run(self.sprite_stub, 0x80041724)
                            bias = 68 if resolution & 2 else 48
                            active = scope and resolution & 1
                            left = object_address == 0x800FF820 or x < -31
                            expected = x - bias if active and left else x + bias if active and x >= 32 else x
                            self.assertEqual(machine.registers[24], register_word(expected))
                            for register in set(range(32)) - {14, 15, 24, 25}:
                                self.assertEqual(machine.registers[register], registers[register], "Changed live GPR %d" % register)
                            self.assertEqual(machine.memory, memory)
                            self.assertEqual(machine.fpr, fpr)
                            self.assertEqual(machine.fcsr, 0x01800004)
                            self.assertTrue(all(self.sprite_stub <= pc < self.sprite_stub + 120 for pc in machine.visited))

    def make_banner(self, cap_x, y, initial_fcsr):
        registers = [0] + [register_word(0x1000 + index) for index in range(1, 32)]
        registers[17] = register_word(self.display_pointer)
        registers[29] = register_word(self.sp)
        memory = {}
        for address, value in {
            self.display_pointer: self.display_list,
            0x800FF82C: float_bits(cap_x),
            0x800FF830: float_bits(y),
            self.sp + 0xF8: 0x80300000,
        }.items():
            store_word(memory, address, value)
        return FpuMachine({}, registers, memory, fcsr=initial_fcsr)

    def execute_slice(self, machine, start, variant):
        for index, stock in enumerate(STOCK_SLICES[start].split()):
            offset = start + index * 4
            word = self.patches[offset][variant] if offset in self.patches else int(stock, 16)
            machine.execute_ordinary(word)

    def test_overlay_patch_signatures_and_all_twelve_replacements_are_exercised(self):
        self.assertEqual(len(self.patch_rows), 12)
        self.assertEqual(len(self.patches), 12)
        for offset, (original, low, high) in self.patches.items():
            self.assertEqual(STOCK_PROGRAM[offset], original, "Wrong original at overlay +%#x" % offset)
            self.assertNotEqual(low, original)
            self.assertNotEqual(high, original)
        for offset in (0x1EFC, 0x1F64):
            for variant in (1, 2):
                # Preserve the independent horizontal-centre and vertical-middle bits.
                self.assertEqual(self.patches[offset][variant] & 0xFFFF, 12)

    def test_text_scissor_shadow_and_y_follow_the_same_anchor_without_changing_fcsr(self):
        for variant, text_bias, clip_bias, left in ((1, 46, 124, 65.5), (2, 95, 173, 114.5)):
            for cap_x in range(-78, 123):
                for y in (-20.25, 0.0, 20.5):
                    for rounding_mode in range(4):
                        with self.subTest(variant=variant, cap_x=cap_x, y=y, rounding=rounding_mode):
                            initial_fcsr = 0x01000004 | rounding_mode
                            patched = self.make_banner(cap_x, y, initial_fcsr)
                            stock = self.make_banner(cap_x, y, initial_fcsr)
                            for machine, selected in ((patched, variant), (stock, 0)):
                                self.execute_slice(machine, 0x1D98, selected)
                            text_x = 0.75 * cap_x + text_bias
                            self.assertEqual(bits_float(load_word(patched.memory, self.sp + 0x118)), text_x)
                            self.assertEqual(load_word(patched.memory, self.sp + 0x114), load_word(stock.memory, self.sp + 0x114))
                            self.assertEqual(bits_float(patched.fpr[0]), 4.0)
                            clip0, clip1 = (load_word(patched.memory, self.display_list + offset) for offset in (0, 4))
                            original0, original1 = (load_word(stock.memory, self.display_list + offset) for offset in (0, 4))
                            self.assertEqual(clip0 >> 24, 0xED)
                            self.assertEqual(((clip0 >> 12) & 0xFFF) / 4.0, left)
                            self.assertEqual(((clip1 >> 12) & 0xFFF) / 4.0, 0.75 * cap_x + clip_bias)
                            self.assertEqual(clip0 & 0xFFF, original0 & 0xFFF)
                            self.assertEqual(clip1 & 0xFFF, original1 & 0xFFF)
                            self.assertEqual(patched.fcsr, initial_fcsr)

                            for machine, selected in ((patched, variant), (stock, 0)):
                                self.execute_slice(machine, 0x1ED4, selected)
                            self.assertEqual(patched.registers[5], register_word(math.trunc(text_x)))
                            self.assertEqual(patched.registers[6], stock.registers[6])
                            self.assertEqual(load_word(patched.memory, self.sp + 0x10), 12)
                            self.assertEqual(patched.fcsr, initial_fcsr)
                            if cap_x == 122:
                                self.assertEqual(patched.registers[5], 137 if variant == 1 else 186)

                            for machine, selected in ((patched, variant), (stock, 0)):
                                self.execute_slice(machine, 0x1F48, selected)
                            # The shadow adds one in floating point before the
                            # game's truncation (off-screen values can cross zero).
                            self.assertEqual(bits_float(patched.fpr[8]), text_x + 1)
                            self.assertEqual(patched.registers[5], register_word(math.trunc(text_x + 1)))
                            self.assertEqual(patched.registers[6], stock.registers[6])
                            self.assertEqual(load_word(patched.memory, self.sp + 0x10), 12)
                            self.assertEqual(patched.fcsr, initial_fcsr)
                            if cap_x == 122:
                                self.assertEqual(patched.registers[5], 138 if variant == 1 else 187)


if __name__ == "__main__":
    unittest.main()
