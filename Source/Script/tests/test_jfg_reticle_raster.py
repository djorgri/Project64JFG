"""Execute the original US queued renderer, also used for editable reticle exports.

The instruction fixture includes the native style dispatch, arbitrary-slope
walker, thin style-2 writer and saturating pixel blend. No ROM is required.
The input is one already clipped queue item, as produced by fxDrawLine.
"""

import re
import unittest
from jfg_reticle_fixtures import GLYPHS, GLYPH_WORDS

from test_jfg_ammo_hud import (
    MASK32, MASK64, SOURCE, array_body, load_word, register_word,
    sign_extend, store_word,
)
from test_jfg_floyd_hud import FRAME, STACK, QUEUE_ITEM, RasterMachine, FrameMemory


# Original US instructions, before any HUD patches.
STOCK_BLOCKS = (
    (0x8006DA1C, """
    AFA50004 AFA60008 AFA7000C 94830000 00057600 000362C3 000E7E03 318D001F
    0006C600 01AF1021 00183603 00075600 00027400 0003C183 00036043 000A3E03
    000E1403 3319001F 318D001F 03264021 01A74821 00085400 00097400 28410020
    000A4403 14200002 000E4C03 2402001F 29010020 14200003 29210020 2408001F
    29210020 14200002 00000000 2409001F 04410002 00000000 00001025 05010002
    0002C2C0 00004025 05210002 0008C980 00004825 03195025 00095840 014B6025
    358D0001 03E00008 A48D0000 AFA50004 AFA60008 94830000 30AE00FF 0003C2C3
    3319001F 00035983 30CF00FF 032E1021 316C001F 00024C00 018F3821 00034043
    00091403 310F001F 00076C00 000FC400 28410020 000D3C03 14200002 00184403
    2402001F 28E10020 14200002 00024AC0 2407001F 00075180 012A5825 00086040
    016C6825 35AE0001 03E00008 A48E0000
    """),
    (0x8006E220, """
    81220007 2401000E 3045000F 14A1000E 38A8000D 8D230004 85240000 85260002
    00033B00 00077503 304F0010 AFAF0010 01C03825 AFA900A0 0C01B7E4 00032D03
    10000236 8FB900B4 3C048010 8C84ECB0 2D080001 15000003 2401000C 14A1000D
    30580020 8D230004 85250000 85270002 0003C300 0018CD03 AFB90010 AFA900A0
    AFA80014 0C01B6DB 00033503 10000223 8FB900B4 8D230004 8FAA00C0 00033D03
    00EA0019 85260000 304F0010 00067040 AFA00094 0000F025 24170008 2CA10010
    00005812 000B6040 01846821 11E00004 01AE9821 241E0018 10000001 0000B825
    13000005 02E0C825 00195640 000A5E03 10000005 0160B825 304C0080 11800002
    00000000 24170006 102001E2 AFA900A0 00056880 3C01800B 002D0821 8C2DECF8
    00000000 01A00008 00000000 AFA900A0 8FAE00A0 0003C300 85CF0002 0018CD03
    00CF2023 00F92823 04810003 2408FFFF 10000002 00041023 00801025 04A10003
    00A01825 10000001 00051823 0062082A 1020001F 00000000 00055C00 0162001A
    8FAE00C0 00075400 8FAD00C0 AFA200CC AFAA00D8 14400002 00000000 0007000D
    2401FFFF 14410004 3C018000 15610002 00000000 0006000D 2410FFFF 000E7823
    00006012 AFAC00D0 04810003 00000000 10000001 24100001 04A10003 00000000
    10000021 AFAD00C4 1000001F AFAF00C4 0004CC00 0323001A 8FB000C0 0006C400
    AFA300CC AFB800D8 14600002 00000000 0007000D 2401FFFF 14610004 3C018000
    17210002 00000000 0006000D 240B0001 00108023 00005012 AFAA00D0 04A10004
    00000000 8FB000C0 10000001 00000000 04810003 00000000 10000003 AFAB00C4
    240CFFFF AFAC00C4 8FAD00CC 00107840 25AEFFFF AFAE00CC 11A00186 01A01025
    AFAF0048 8FB800A0 AFA800D4 83190007 00000000 332A000F 254BFFFD 2D61000D
    102000CB 000B5880 3C01800B 002B0821 8C2BED38 00000000 01600008 00000000
    8FB100C0 001E9040 0017A040 001E7880 0017C880 00116040 026C8023 332A00FF
    31F800FF 328E00FF 324D00FF 33D500FF 32F600FF 32C600FF 32A500FF 01A09025
    01C0A025 AFB80050 AFAA004C 2604FFFE 0C01B6BA 01808825 02002025 324500FF
    0C01B6BA 328600FF 26040002 32A500FF 0C01B6BA 32C600FF 2664FFFE 324500FF
    0C01B6BA 328600FF 93A50053 93A6004F 0C01B6BA 02602025 26640002 324500FF
    0C01B6BA 328600FF 02338021 2604FFFE 32A500FF 0C01B6BA 32C600FF 02002025
    324500FF 0C01B6BA 328600FF 26040002 32A500FF 0C01B6BA 32C600FF 8FA800D4
    1000008C 8FB900D8 8FB100C0 00002825 00115840 01608825 026B2023 0C01B6BA
    24060010 2664FFFE 00002825 0C01B6BA 24060010 02602025 00002825 0C01B6BA
    24060010 26640002 00002825 0C01B6BA 24060010 02332021 24050010 24060010
    0C01B687 24070010 8FA800D4 10000071 8FB900D8 8FB100C0 00002825 00116040
    01808825 026C2023 0C01B6BA 24060010 2664FFFE 00002825 0C01B6BA 24060010
    02602025 00002825 0C01B6BA 24060010 26640002 00002825 0C01B6BA 24060010
    02332021 2405FFF0 2406FFF0 0C01B687 2407FFF0 8FA800D4 10000056 8FB900D8
    8FB100C0 24050010 00116840 01A08825 026D2023 0C01B6BA 00003025 2664FFFE
    24050010 0C01B6BA 00003025 02602025 24050010 0C01B6BA 00003025 26640002
    24050010 0C01B6BA 00003025 02332021 24050010 24060010 0C01B687 24070010
    8FA800D4 1000003B 8FB900D8 8FB100C0 24050010 00117040 01C08825 026E2023
    0C01B6BA 00003025 2664FFFE 24050010 0C01B6BA 00003025 02602025 24050010
    0C01B6BA 00003025 26640002 24050010 0C01B6BA 00003025 02332021 2405FFF0
    2406FFF0 0C01B687 2407FFF0 8FA800D4 10000020 8FB900D8 8FB100C0 00002825
    00117840 01E08825 026F2023 00003025 0C01B687 24070010 2664FFFE 00002825
    00003025 0C01B687 24070010 02602025 00002825 00003025 0C01B687 24070010
    26640002 00002825 00003025 0C01B687 24070010 02332021 24050010 24060010
    0C01B687 24070010 8FA800D4 00000000 8FB900D8 8FAA00D0 8FB80048 032A5821
    000B1403 AFAB00D8 11020005 02789821 8FAC00C4 00404025 000C6840 026D9821
    8FA200CC 00000000 244FFFFF 1440FF1D AFAF00CC 100000A0 8FAC0094 8FB800A0
    00035B00 87020002 000B6503 00C2082A 10200005 00C25023 0046C823 AFB900CC
    10000003 24100001 AFAA00CC 2410FFFF 00EC082A 10200005 8FA200C0 8FA200C0
    10000005 8FAD00CC 8FA200C0 00000000 00021023 8FAD00CC 02028021 25AEFFFF
    11A00084 AFAE00CC 3C0F8010 8DEFECB0 02602025 026F082B 1420007E 33C500FF
    0010C040 AFB80048 0C01B6BA 32E600FF 8FAA00CC 8FB90048 254BFFFF AFAB00CC
    1540FFF1 02799821 10000073 8FAC0094 8FAC00A0 00000000 858D0002 00000000
    01A67023 25CF0001 AFAF00CC 25F8FFFF 11E00068 AFB800CC 8FB100C0 33D500FF
    0011C840 03208825 32F600FF 02712023 32A500FF 0C01B6BA 32C600FF 02602025
    32A500FF 0C01B6BA 32C600FF 02332021 32A500FF 0C01B6BA 32C600FF 8FAA00CC
    26730002 254BFFFF 1540FFF0 AFAB00CC 10000051 8FAC0094 3C0D800A 240C0012
    25AD6968 AFAC00CC AFAD0094 10000049 AFA900A0 3C0F800A 240E0010 25EF698C
    AFAE00CC AFAF0094 10000042 AFA900A0 3C19800A 2418000F 273969AC AFB800CC
    AFB90094 1000003B AFA900A0 3C0B800A 240A000F 256B69CC AFAA00CC AFAB0094
    10000034 AFA900A0 00032300 00046503 15870019 01802025 8FAD00A0 00000000
    85AE0002 00000000 01C67823 25F80001 AFB800CC 2719FFFF 13000026 AFB900CC
    33D500FF 32F600FF 02602025 26730002 32A500FF 0C01B6BA 32C600FF 8FAA00CC
    00000000 254BFFFF 1540FFF7 AFAB00CC 10000019 8FAC0094 8FAC00A0 00877023
    858D0002 25CF0001 15A60012 25F8FFFF AFAF00CC 11E0000F AFB800CC 8FB100C0
    33D500FF 0011C840 03208825 32F600FF 02602025 32A500FF 0C01B6BA 32C600FF
    8FAA00CC 02719821 254BFFFF 1540FFF8 AFAB00CC
    """),
)
STOCK_PROGRAM = {start + 4 * i: int(word, 16)
                 for start, words in STOCK_BLOCKS
                 for i, word in enumerate(words.split())}
STOCK_PROGRAM.update({0x8006EAB4 + 4*i: word for i,word in enumerate(GLYPH_WORDS)})
# fxDrawLine rejects coincident endpoints after clipping, before queueing.
QUEUE_FILTER_WORDS = """
    87A30052 87A4005A 87A50056 14640005 2401000C 87A6005E 00000000 10A6003B 8FBF0024
"""
STYLE_TARGETS = (
    0x8006E9E8, 0x8006E8F0, 0x8006E83C, 0x8006E34C,
    0x8006E978, 0x8006E994, 0x8006E9B0, 0x8006E9CC,
    0x8006E34C, 0x8006E34C, 0x8006E34C, 0x8006E34C,
    0x8006EAB4, 0x8006EAB4, 0x8006EAB4, 0x8006E350,
)
PIXEL_TARGETS = (
    0x8006E4E0, 0x8006E7F0, 0x8006E7F0, 0x8006E7F0,
    0x8006E7F0, 0x8006E5C8, 0x8006E634, 0x8006E6A0,
    0x8006E70C, 0x8006E7F0, 0x8006E7F0, 0x8006E7F0,
    0x8006E778,
)


class ReticleRasterMachine(RasterMachine):
    def __init__(self, *args):
        super().__init__(*args)
        self.plots = []

    def execute_ordinary(self, word):
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        rd, function = word >> 11 & 31, word & 63
        if op in (32, 33):  # lb / lh
            address = (self.registers[rs] + sign_extend(word & 65535, 16)) & MASK32
            value = self.memory[address]
            if op == 33:
                value = value << 8 | self.memory[address + 1]
            self.write_register(rt, sign_extend(value, 8 if op == 32 else 16))
        elif op == 11:  # sltiu
            self.write_register(rt, int(self.registers[rs] <
                                        (sign_extend(word & 65535, 16) & MASK64)))
        elif op == 14:  # xori
            self.write_register(rt, self.registers[rs] ^ (word & 65535))
        elif op == 0 and function == 43:  # sltu
            self.write_register(rd, int(self.registers[rs] < self.registers[rt]))
        elif op == 0 and function == 26:  # signed div, truncating toward zero
            dividend = sign_extend(self.registers[rs], 32)
            divisor = sign_extend(self.registers[rt], 32)
            if not divisor:
                raise AssertionError("The clipped nondegenerate line divided by zero")
            quotient = abs(dividend) // abs(divisor)
            if (dividend < 0) != (divisor < 0):
                quotient = -quotient
            self.lo = register_word(quotient)
            self.hi = register_word(dividend - quotient * divisor)
        else:
            super().execute_ordinary(word)

    def run_line(self, start=0x8006E220, stops=(0x8006EAB4, 0x8006EAB8)):
        pc = start
        for _ in range(30000):
            if pc in stops:
                return pc
            if pc in (0x8006DAE8, 0x8006DA1C):
                address = self.registers[4] & MASK32
                self.plots.append(((address - FRAME) // 2 % self.width,
                                   (address - FRAME) // 2 // self.width))
            self.visited.append(pc)
            word = self.program[pc]
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (1, 2, 3, 4, 5) or op == 0 and word & 63 == 8:
                if op in (2, 3):
                    target = ((pc + 4) & 0xF0000000) | (word & 0x03FFFFFF) << 2
                    if op == 3:
                        self.write_register(31, register_word(pc + 8))
                elif op == 0:
                    target = self.registers[rs] & MASK32
                else:
                    if op == 1:
                        if rt != 1:
                            raise AssertionError("Unexpected REGIMM branch")
                        taken = sign_extend(self.registers[rs], 64) >= 0
                    else:
                        taken = self.registers[rs] == self.registers[rt]
                        if op == 5:
                            taken = not taken
                    target = pc + 4 + sign_extend(word & 65535, 16) * 4 if taken else pc + 8
                self.execute_ordinary(self.program[pc + 4])
                pc = target & MASK32
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("Queued reticle renderer exceeded its instruction budget")


class JfgReticleRasterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text(encoding="utf-8-sig")
        cls.patches = [tuple(int(v, 16) for v in row) for row in re.findall(
            r"\{\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
            array_body(source, "WidescreenHudReticleRasterRetired"))]
        cls.program = dict(STOCK_PROGRAM)
        for address, original, replacement in cls.patches:
            if STOCK_PROGRAM[address] != original:
                raise AssertionError("Reticle patch does not match original US renderer")
            cls.program[address] = original

    def render(self, line, flags=2, width=320, patched=True):
        x0, y0, x1, y1 = line
        registers = [0] + [register_word(0x12340000 + i * 17) for i in range(1, 32)]
        registers[9] = register_word(QUEUE_ITEM)
        registers[29] = register_word(STACK)
        memory = {}
        for address, points in GLYPHS.items():
            for i,(x,y) in enumerate(points):
                memory[0x80000000 + address + i*2] = x & 255
                memory[0x80000000 + address + i*2+1] = y & 255
        packed = ((y0 & 4095) << 20) | ((y1 & 4095) << 8) | flags
        store_word(memory, QUEUE_ITEM, (x0 & 65535) << 16 | x1 & 65535)
        store_word(memory, QUEUE_ITEM + 4, packed)
        store_word(memory, 0x800FECB0, FRAME)
        store_word(memory, STACK + 0xC0, width)
        for base, targets in ((0x800AECF8, STYLE_TARGETS), (0x800AED38, PIXEL_TARGETS)):
            for i, target in enumerate(targets):
                store_word(memory, base + 4 * i, target)
        machine = ReticleRasterMachine(self.program if patched else STOCK_PROGRAM,
                                       registers, memory, width)
        machine.run_line(stops=(0x8006EB38,) if 4 <= flags & 15 <= 7 else (0x8006EAB4, 0x8006EAB8))
        self.assertEqual(load_word(machine.memory, QUEUE_ITEM + 4), packed)
        self.assertEqual(machine.registers[29], register_word(STACK))
        return machine

    def test_native_rocket_diagonals_keep_the_original_exclusive_endpoint(self):
        # Outer and inner diagonals of weapon 5, rotated by frontDrawTarget.
        for width in (320, 448):
            for x0, y0, x1, y1 in ((-16, -9, -8, -17), (-10, -6, -5, -11)):
                for rotate in (lambda x,y:(x,y), lambda x,y:(-x,-y),
                               lambda x,y:(-y,x), lambda x,y:(y,-x)):
                    a,b=rotate(x0,y0),rotate(x1,y1)
                    line=(160+a[0],120+a[1],160+b[0],120+b[1])
                    dx,dy=line[2]-line[0],line[3]-line[1]
                    expected=[(line[0]+(1 if dx>0 else -1)*i,
                               line[1]+(1 if dy>0 else -1)*i) for i in range(abs(dx))]
                    self.assertEqual(self.render(line,width=width).plots,expected)

    def test_thin_blend_keeps_native_colours_and_saturation(self):
        line = (100, 80, 106, 88)
        for flags in (2, 0x12, 0x22, 0x42, 0x82, 0x62, 0xD2):
            actual = self.render(line, flags)
            initial = FrameMemory({}, 320)
            red = 24 if flags & 0x10 else 0
            green = 0 if flags & 0x10 else 8
            if flags & 0x20:
                green = sign_extend((green << 25) & MASK32, 32) >> 24
            elif flags & 0x80:
                green = 6
            green &= 255
            expected = {}
            for x, y in actual.plots:
                address = FRAME + (320 * y + x) * 2
                before = initial.initial_pixel(address)
                after = (min((before >> 11 & 31) + red, 31) << 11 |
                         min((before >> 6 & 31) + green, 31) << 6 |
                         before & 62 | 1)
                if after != before:
                    expected[x, y] = after
            self.assertEqual(actual.memory.pixels_changed(), expected)

    def test_clipped_zero_length_lines_are_rejected_before_queueing(self):
        program = {0x8006D474 + 4 * i: int(word, 16)
                   for i, word in enumerate(QUEUE_FILTER_WORDS.split())}
        for line in ((160, 120, 160, 120), (1, 1, 1, 1),
                     (318, 238, 318, 238), (160, 120, 161, 120),
                     (160, 120, 160, 121)):
            registers, memory = [0] * 32, {}
            registers[29] = register_word(STACK)
            for offset, value in zip((0x50, 0x54, 0x58, 0x5C), line):
                store_word(memory, STACK + offset, value)
            store_word(memory, STACK + 0x24, 0x8034E3F0)
            machine = ReticleRasterMachine(program, registers, memory, 320)
            stop = machine.run_line(0x8006D474, (0x8006D498, 0x8006D580))
            self.assertEqual(stop, 0x8006D580 if line[:2] == line[2:] else 0x8006D498)


if __name__ == "__main__":
    unittest.main()
