"""Execute the US two-pixel line writer with the production Floyd helpers.

The original function is kept here as a ROM-independent instruction fixture.
Checks compare framebuffer pixels, including saturating green and both ends of
each diagonal, rather than merely checking transformed endpoint registers.
"""

import re
import unittest

from test_jfg_ammo_hud import (
    MASK32, MipsMachine, SOURCE, array_body, code_array, constant,
    load_word, register_word, sign_extend, store_word,
)


HEADER = SOURCE.with_name("JetForceGeminiFloydHud.h")
RASTER_START = 0x8006DB6C
CALLER_RETURN = 0x8006E2AC
FRAME = 0x80200000
STACK = 0x800FD000
QUEUE_ITEM = 0x80102DC0

STOCK_WORDS = """
    27BDFFB0 AFB00014 00A08025 AFBF001C AFB10018 AFA40050 00C08825 AFA7005C
    27A4003C 0C0153A8 27A50038 8FA30064 8FAC005C 14600003 0190082A 14200008
    02005025 1060000D 8FA5003C 8FAD0060 00000000 01B1082A 10200007 02005025
    01808025 01406025 8FAD0060 02205025 AFAA0060 01A08825 8FA5003C 8FAD0060
    02250019 8FB90050 01B15023 254A0001 020C082A 00A05825 00007012 01D07821
    000FC040 03191021 14600012 00404825 00051840 00057023 00623021 000E7840
    01905023 022D082A 01E23821 00664021 254A0001 10200002 240B0001 24AB0001
    01B1082A 1020000D 01402025 1000000A 01655823 24460002 2447FFFE 10200002
    24480004 24AB0001 0190082A 10200003 01402025 256BFFFF 01402025 11400020
    254AFFFF 000B2840 94F80000 00000000 A4F80000 95190000 00E53821 A5190000
    95230000 01054021 306407C0 24820200 304EF800 11C00002 00641826 240207C0
    00627825 A52F0000 94C30000 01254821 306407C0 24820200 3058F800 13000002
    00641826 240207C0 0062C825 01402025 A4D90000 00C53021 1540FFE3 254AFFFF
    8FBF001C 8FB00014 8FB10018 03E00008 27BD0050
"""
STOCK_PROGRAM = {RASTER_START + index * 4: int(word, 16)
                 for index, word in enumerate(STOCK_WORDS.split())}

# US overlay 14 +0x4678: only type-13 records. Types 0 and 12 keep their
# native horizontal/tick raster and are tested separately below.
RING_SEGMENTS = (
    (-22, 3, -22, 9), (-21, 10, -10, 21), (9, 21, 20, 10),
    (21, 9, 21, 3), (0, 25, 0, 23), (-22, -2, -22, -8),
    (-21, -9, -10, -20), (9, -20, 20, -9), (21, -8, 21, -2),
    (0, -24, 0, -22),
)


class FrameMemory(dict):
    """Sparse framebuffer initialized with varying RGB and alpha values."""

    def __init__(self, values, width):
        super().__init__(values)
        self.width = width
        self.size = width * 336 * 2

    def initial_pixel(self, address):
        pixel = (address - FRAME) // 2
        x, y = pixel % self.width, pixel // self.width
        return (((x + y) & 31) << 11 | ((x * 3 + y * 7) & 31) << 6
                | ((x * 5 + y) & 31) << 1 | ((x + y) & 1))

    def __missing__(self, address):
        if FRAME <= address < FRAME + self.size:
            value = self.initial_pixel(address)
            return value & 255 if address & 1 else value >> 8
        raise KeyError("Uninitialized memory read at %#x" % address)

    def pixels_changed(self):
        addresses = {address & ~1 for address in self
                     if FRAME <= address < FRAME + self.size}
        return {((address - FRAME) // 2 % self.width,
                 (address - FRAME) // 2 // self.width): self[address] << 8 | self[address + 1]
                for address in addresses
                if (self[address] << 8 | self[address + 1]) != self.initial_pixel(address)}


class RasterMachine(MipsMachine):
    def __init__(self, program, registers, memory, width):
        super().__init__(program, registers, memory)
        self.memory = FrameMemory(self.memory, width)
        self.width = width
        self.hi = self.lo = 0

    def execute_ordinary(self, word):
        opcode, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        rd, function = word >> 11 & 31, word & 63
        if opcode in (37, 41):  # lhu / sh
            address = (self.registers[rs] + sign_extend(word & 65535, 16)) & MASK32
            if opcode == 37:
                self.write_register(rt, self.memory[address] << 8 | self.memory[address + 1])
            else:
                self.memory[address] = self.registers[rt] >> 8 & 255
                self.memory[address + 1] = self.registers[rt] & 255
        elif opcode == 0 and function == 25:  # multu
            result = (self.registers[rs] & MASK32) * (self.registers[rt] & MASK32)
            self.hi, self.lo = register_word(result >> 32), register_word(result)
        elif opcode == 0 and function == 18:  # mflo
            self.write_register(rd, self.lo)
        elif opcode == 0 and function == 42:  # slt
            self.write_register(rd, int(sign_extend(self.registers[rs], 64)
                                        < sign_extend(self.registers[rt], 64)))
        elif opcode == 0 and function == 38:  # xor
            self.write_register(rd, self.registers[rs] ^ self.registers[rt])
        else:
            super().execute_ordinary(word)

    def run(self, start, resume):
        pc = start
        for _ in range(12000):
            if pc == resume:
                return
            if pc == 0x80054EA0:  # viGetCurrentSize, the only external call
                store_word(self.memory, self.registers[4] & MASK32, self.width)
                store_word(self.memory, self.registers[5] & MASK32, 240 if self.width == 320 else 336)
                pc = self.registers[31] & MASK32
                continue
            self.visited.append(pc)
            word = self.program[pc]
            opcode, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if opcode in (2, 3, 4, 5) or opcode == 0 and word & 63 == 8:
                if opcode in (2, 3):
                    following = ((pc + 4) & 0xF0000000) | (word & 0x03FFFFFF) << 2
                    if opcode == 3:
                        self.write_register(31, register_word(pc + 8))
                elif opcode == 0:
                    following = self.registers[rs] & MASK32
                else:
                    taken = self.registers[rs] == self.registers[rt]
                    if opcode == 5:
                        taken = not taken
                    following = pc + 4 + sign_extend(word & 65535, 16) * 4 if taken else pc + 8
                self.execute_ordinary(self.program[pc + 4])
                pc = following & MASK32
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("Floyd line writer exceeded its bounded instruction budget")


class JfgFloydRasterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HEADER.read_text(encoding="utf-8-sig")
        cls.patches = [tuple(int(value, 16) for value in match) for match in re.findall(
            r"\{\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
            array_body(cls.source, "FixedPatches"))]
        cls.program = dict(STOCK_PROGRAM)
        for address, original, replacement in cls.patches:
            if address in STOCK_PROGRAM:
                if STOCK_PROGRAM[address] != original:
                    raise AssertionError("Production hook does not match US renderer")
                cls.program[address] = replacement
        for name in ("Init", "Step", "StepTail"):
            address = constant(cls.source, name + "Stub")
            cls.program.update({address + index * 4: word
                                for index, word in enumerate(code_array(cls.source, name + "Code"))})

    def render(self, line, width=320, flags=0x4D, caller=CALLER_RETURN, patched=True):
        x0, y0, x1, y1 = line
        registers = [0] + [register_word(0x76540000 + i * 37) for i in range(1, 32)]
        for register, value in {4: FRAME, 5: x0, 6: y0, 7: x1, 29: STACK, 31: caller}.items():
            registers[register] = register_word(value)
        memory = {QUEUE_ITEM + 7: flags}
        store_word(memory, STACK + 0x10, y1)
        store_word(memory, STACK + 0x14, int(flags & 15 == 13))
        store_word(memory, STACK + 0xA0, QUEUE_ITEM)
        machine = RasterMachine(self.program if patched else STOCK_PROGRAM, registers, memory, width)
        machine.run(RASTER_START, caller)
        for register in (*range(16, 24), 28, 29, 30):
            self.assertEqual(machine.registers[register], registers[register],
                             "Line writer changed callee-saved register %d" % register)
        self.assertEqual(machine.registers[31], registers[31])
        return machine

    def expected_pixels(self, line, width):
        x0, y0, x1, y1 = line
        if y1 < y0:
            x0, y0, x1, y1 = x1, y1, x0, y0
        dy, dx = y1 - y0, x1 - x0
        self.assertLessEqual(abs(dx), dy)
        result = {}
        memory = FrameMemory({}, width)
        for row in range(dy + 1):
            x = x0 + (1 if dx >= 0 else -1) * (abs(dx) * row // dy if dy else 0)
            for column in (x, x + 1):
                address = FRAME + ((y0 + row) * width + column) * 2
                value = memory.initial_pixel(address)
                value = (value & ~0x7C0) | min((value & 0x7C0) + 0x200, 0x7C0)
                if value != memory.initial_pixel(address):
                    result[column, y0 + row] = value
        return result

    def test_compressed_ring_diagonals_keep_both_ends_and_two_pixel_stroke(self):
        for width, center, bias in ((320, 272, 76), (448, 381, 107)):
            for native in RING_SEGMENTS:
                line = ((3 * (center + native[0]) + 4 * bias) // 4, 192 + native[1],
                        (3 * (center + native[2]) + 4 * bias) // 4, 192 + native[3])
                with self.subTest(width=width, segment=native):
                    machine = self.render(line, width)
                    self.assertEqual(machine.memory.pixels_changed(), self.expected_pixels(line, width))

    def test_native_lines_and_foreign_callers_keep_exact_original_pixels(self):
        lines = [(240 + a, 192 + b, 240 + c, 192 + d) for a, b, c, d in RING_SEGMENTS]
        lines += [(210, 100, 219, 111), (219, 111, 210, 100)]
        for width in (320, 448):
            for flags, caller in ((0x0D, CALLER_RETURN), (0x8D, CALLER_RETURN),
                                  (0x4D, CALLER_RETURN + 4), (0x4D, 0x80351234)):
                for line in lines:
                    with self.subTest(width=width, flags=flags, caller=hex(caller), line=line):
                        native = self.render(line, width, flags, caller, False)
                        actual = self.render(line, width, flags, caller)
                        self.assertEqual(actual.memory.pixels_changed(), native.memory.pixels_changed())

    def test_type_twelve_ticks_remain_original_in_both_directions(self):
        for line in ((210, 120, 217, 120), (217, 120, 210, 120),
                     (210, 120, 212, 122), (212, 122, 210, 120),
                     (210, 122, 212, 120), (212, 120, 210, 122)):
            for flags in (0x0C, 0x4C):
                native = self.render(line, flags=flags, patched=False)
                actual = self.render(line, flags=flags)
                self.assertEqual(actual.memory.pixels_changed(), native.memory.pixels_changed())

    def test_private_tag_survives_other_queued_render_flags(self):
        line = (242, 170, 234, 181)
        for flags in (0x4D, 0x5D, 0x6D, 0xCD):
            self.assertEqual(self.render(line, flags=flags).memory.pixels_changed(),
                             self.expected_pixels(line, 320))

    def test_short_vertical_and_diagonal_lines_include_the_final_row(self):
        for dy in (1, 2, 3, 4, 11, 17):
            for dx in range(-dy, dy + 1):
                line = (240, 170, 240 + dx, 170 + dy)
                self.assertEqual(self.render(line).memory.pixels_changed(), self.expected_pixels(line, 320))

    def test_native_45_degree_raster_is_bit_exact_even_when_tagged(self):
        for dy in (1, 2, 11, 17):
            for dx in (-dy, 0, dy):
                line = (240, 170, 240 + dx, 170 + dy)
                native = self.render(line, flags=0x0D, patched=False)
                actual = self.render(line)
                self.assertEqual(actual.memory.pixels_changed(), native.memory.pixels_changed())

    def test_helpers_fit_the_retired_regions_and_keep_original_signatures(self):
        intervals = [(constant(self.source, name + "Stub"), len(code_array(self.source, name + "Code")) * 4)
                     for name in ("Guard", "Init", "Step", "StepTail")]
        claimed = set()
        for start, length in intervals:
            addresses = set(range(start, start + length, 4))
            self.assertFalse(claimed & addresses)
            claimed |= addresses
        self.assertTrue(all(0x80067340 <= address < 0x80067360 or 0x800678C4 <= address < 0x80067950
                            for address in claimed))
        original = code_array(self.source, "OriginalDiagnosticCode")
        self.assertEqual(len(original), 35)
        self.assertEqual(original[:2], [0x27BDFDB8, 0xAFBF0014])
        self.assertEqual(original[20:24], [0x0C019E65, 0xAFA9005C, 0x0C026284, 0])
        self.assertEqual(original[-4:], [0x8FBF0014, 0x27BD0248, 0x03E00008, 0])
        patch_by_address = {a: (o, r) for a, o, r in self.patches}
        self.assertEqual(patch_by_address[0x8006DC94], (0x94F80000, 0x94F80000))
        self.assertEqual(patch_by_address[0x8006DD08], (0x254AFFFF, 0x254AFFFF))


if __name__ == "__main__":
    unittest.main()
