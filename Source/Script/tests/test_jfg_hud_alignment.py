"""Execute JFG alignment wrapper opcodes, including nested calls and restoration.

Run: python -m unittest discover -s Source/Script/tests -p test_jfg_hud_alignment.py
The stock matrix converter/drawer are call-boundary models; actual game rendering
and the identification of the selected display-list matrices need game tests.
"""

import unittest
from pathlib import Path

from test_jfg_ammo_hud import (
    MASK32, code_array, constant, load_word, register_word, sign_extend, store_word,
)
from test_jfg_banner_hud import FpuMachine, bits_float, float_bits


HEADER = (Path(__file__).resolve().parents[3]
          / "Source/Project64-core/N64System/GameHacks/JetForceGeminiHudAlignmentCode.h")


class CallMachine(FpuMachine):
    def __init__(self, program, registers, memory):
        super().__init__(program, registers, memory, fcsr=0x01800004)
        self.external = {}

    def execute_ordinary(self, word):
        if word >> 26 == 40:  # sb
            rs, rt = (word >> 21) & 31, (word >> 16) & 31
            address = (self.registers[rs] + sign_extend(word & 65535, 16)) & MASK32
            self.memory[address] = self.registers[rt] & 255
        elif word >> 26 == 11:  # sltiu
            rs, rt = (word >> 21) & 31, (word >> 16) & 31
            immediate = sign_extend(word & 65535, 16) & ((1 << 64) - 1)
            self.write_register(rt, int(self.registers[rs] < immediate))
        else:
            super().execute_ordinary(word)

    def run(self, start, resume):
        pc = start
        for _ in range(1024):
            if pc == resume:
                return
            if pc in self.external:
                return_pc = self.registers[31] & MASK32
                self.external[pc](self)
                pc = return_pc
                continue
            self.visited.append(pc)
            word = self.program[pc]
            opcode = word >> 26
            if opcode in (2, 3, 4, 5) or (opcode == 0 and word & 63 == 8):
                if opcode in (2, 3):
                    next_pc = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                    if opcode == 3:
                        self.write_register(31, register_word(pc + 8))
                elif opcode == 0:  # jr
                    next_pc = self.registers[(word >> 21) & 31] & MASK32
                else:
                    rs, rt = (word >> 21) & 31, (word >> 16) & 31
                    taken = self.registers[rs] == self.registers[rt]
                    if opcode == 5:
                        taken = not taken
                    next_pc = pc + 4 + (sign_extend(word & 65535, 16) << 2) if taken else pc + 8
                self.visited.append(pc + 4)
                self.execute_ordinary(self.program[pc + 4])
                pc = next_pc & MASK32
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("Alignment wrapper exceeded its instruction budget")


class JfgHudAlignmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HEADER.read_text(encoding="utf-8-sig")
        cls.names = ("SpriteWeapon", "SpriteHealth", "SpriteCommon",
                     "SpriteMatrix", "MatrixWeapon", "MatrixHealth")
        cls.entries = {name: constant(cls.source, name + "Entry") for name in cls.names}
        cls.program = {}
        for name in cls.names:
            for index, word in enumerate(code_array(cls.source, name + "Code")):
                address = cls.entries[name] + 4 * index
                if address in cls.program:
                    raise AssertionError("Overlapping alignment arrays")
                cls.program[address] = word
        cls.active = constant(cls.source, "ActiveKindAddress")
        cls.params = tuple(map(float_bits, (1 / 17, -2 / 19, 3 / 23, -4 / 29, 5 / 31, 6 / 37)))
        cls.source_matrix, cls.destination = 0x80200000, 0x80201000
        cls.sp, cls.resume = 0x800FE000, 0x80012340

    def fixture(self, kind=0, weight=160, half_width=160):
        registers = [0] + [register_word(0x80010000 + i * 128) for i in range(1, 32)]
        registers[4], registers[5] = map(register_word, (self.source_matrix, self.destination))
        registers[29], registers[31] = map(register_word, (self.sp, self.resume))
        memory = {self.active: kind, self.active - 1: 0x7C, self.active + 1: 0x9B}
        words = [float_bits(i + 0.125) for i in range(16)]
        words[12], words[13], words[15] = map(float_bits, (17.5, -33.25, weight))
        for index, word in enumerate(words):
            store_word(memory, self.source_matrix + index * 4, word)
        for index, word in enumerate(self.params):
            store_word(memory, constant(self.source, "WeaponDxNdcAddress") + index * 4, word)
        store_word(memory, constant(self.source, "HalfWidthAddress"), float_bits(half_width))
        machine = CallMachine(self.program, registers, memory)
        captured = []

        def convert(vm):
            self.assertEqual(vm.registers[4] & MASK32, self.source_matrix)
            self.assertEqual(vm.registers[5] & MASK32, self.destination)
            captured.append([load_word(vm.memory, self.source_matrix + i * 4) for i in range(16)])
            # The real conversion clobbers caller-saved temporaries. Restoration
            # must use the stack and integer source bits rather than these FPRs.
            vm.write_register(4, register_word(self.source_matrix + 64))
            vm.write_register(5, register_word(self.destination + 32))
            for reg in (8, 9, 25):
                vm.write_register(reg, register_word(0xBA000000 + reg))
            for reg in range(20):
                vm.fpr[reg] = float_bits(1000 + reg)
            vm.fcsr = 0x01801004

        machine.external[0x80048D84] = convert
        return machine, registers, memory, words, captured

    def expected_matrix(self, words, pair, billboard_weight=0):
        expected = list(words)
        if pair is not None:
            weight = bits_float(float_bits(bits_float(words[15]) + billboard_weight))
            for axis in (0, 1):
                delta = bits_float(self.params[pair * 2 + axis])
                amount = bits_float(float_bits(delta * weight))
                expected[12 + axis] = float_bits(bits_float(words[12 + axis]) + amount)
        return expected

    def test_slots_parameters_and_original_call_delay(self):
        self.assertEqual(constant(self.source, "CaveStart"), 0x800679A0)
        self.assertEqual(constant(self.source, "CaveEnd"), 0x80067D20)
        self.assertTrue(all(0x800679A0 <= address < 0x80067D00 for address in self.program))
        self.assertEqual(self.active, 0x80102552)
        self.assertEqual(constant(self.source, "HealthSpriteDyNdcAddress"), 0x80067D14)
        self.assertEqual(constant(self.source, "HalfWidthAddress"), 0x80067D18)
        self.assertEqual(constant(self.source, "SpriteMatrixHookAddress"), 0x800418D8)
        self.assertEqual(constant(self.source, "SpriteMatrixHookOriginal"), 0x0C012361)
        self.assertEqual(constant(self.source, "SpriteMatrixHookDelay"), 0x02402025)
        self.assertEqual(constant(self.source, "HealthMatrixHookOffset"), 0x45C)
        self.assertEqual(constant(self.source, "HealthMatrixHookDelay"), 0x02602025)
        self.assertEqual(constant(self.source, "MatrixDispatchEntry"),
                         self.entries["SpriteMatrix"] + 15 * 4)

    def test_active_sprite_kinds_and_forced_matrix_kinds(self):
        for entry in ("SpriteMatrix", "MatrixWeapon", "MatrixHealth"):
            for kind in (0, 1, 2, 3, 255):
                for weight in (1, 160, 224):
                    with self.subTest(entry=entry, kind=kind, weight=weight):
                        vm, registers, memory, original, captured = self.fixture(kind, weight)
                        sprite_kind = kind if kind in (1, 2) else 0
                        expected_kind = {"MatrixWeapon": 1, "MatrixHealth": 3}.get(entry, sprite_kind)
                        pair = {1: 0, 2: 2, 3: 1}.get(expected_kind)
                        billboard_weight = 160 if entry == "SpriteMatrix" else 0
                        before_saved_fpr = vm.fpr[20:]
                        vm.run(self.entries[entry], self.resume)
                        self.assertEqual(captured, [self.expected_matrix(original, pair, billboard_weight)])
                        self.assertEqual([load_word(vm.memory, self.source_matrix + i * 4) for i in range(16)], original)
                        self.assertEqual(vm.registers[29], registers[29])
                        self.assertEqual(vm.registers[31], registers[31])
                        for reg in range(16, 24):
                            self.assertEqual(vm.registers[reg], registers[reg])
                        self.assertEqual(vm.fpr[20:], before_saved_fpr)
                        if pair is not None:
                            for reg in (4, 5, 8, 9, 25):
                                self.assertEqual(vm.registers[reg], registers[reg])
                            self.assertEqual(vm.fcsr, 0x01800004)
                        self.assertEqual(vm.memory[self.active], kind)
                        self.assertEqual(vm.memory[self.active - 1], 0x7C)
                        self.assertEqual(vm.memory[self.active + 1], 0x9B)
                        self.assertEqual([load_word(vm.memory, constant(self.source, "WeaponDxNdcAddress") + i * 4) for i in range(6)], list(self.params))
                        self.assertEqual(load_word(vm.memory, constant(self.source, "HalfWidthAddress")), float_bits(160))

    def test_sprite_wrappers_restore_nested_kind_and_stack(self):
        vm, registers, memory, original, captured = self.fixture(kind=3)
        kinds = []

        def draw(machine):
            kind = machine.memory[self.active]
            kinds.append(kind)
            saved_ra = machine.registers[31]
            if kind == 1:
                machine.write_register(31, register_word(0x80012348))
                machine.run(self.entries["SpriteHealth"], 0x80012348)
                self.assertEqual(machine.memory[self.active], 1)
            machine.write_register(4, register_word(self.source_matrix))
            machine.write_register(5, register_word(self.destination))
            machine.write_register(31, register_word(0x8001234C))
            machine.run(self.entries["SpriteMatrix"], 0x8001234C)
            machine.write_register(31, saved_ra)

        vm.external[0x8005A0D0] = draw
        vm.run(self.entries["SpriteWeapon"], self.resume)
        self.assertEqual(kinds, [1, 2])
        self.assertEqual(captured, [self.expected_matrix(original, 2, 160), self.expected_matrix(original, 0, 160)])
        self.assertEqual(vm.memory[self.active], 3)
        for reg in (24, 25, 29, 31):
            self.assertEqual(vm.registers[reg], registers[reg])

    def test_source_translation_is_restored_bit_for_bit_including_negative_zero(self):
        vm, registers, memory, original, captured = self.fixture(kind=2)
        original[12] = 0x80000000
        store_word(vm.memory, self.source_matrix + 48, original[12])
        vm.run(self.entries["SpriteMatrix"], self.resume)
        self.assertEqual(load_word(vm.memory, self.source_matrix + 48), 0x80000000)
        self.assertEqual(load_word(vm.memory, self.source_matrix + 52), original[13])

    def test_final_pixel_shift_accounts_for_billboard_anchor_weight(self):
        # ucode05 adds the ortho anchor x/y/w AFTER the sprite matrix transform.
        # camCopyOrtho supplies anchor.w = W/2, unchanged by widescreen. The
        # explicit frame/arc matrices already have m[15] = W/2 and no billboard.
        # Include conversion to 16.16 before the perspective divide, so this
        # catches a correct-looking matrix shift that is almost invisible on screen.
        fixed = lambda value: int(value * 65536) / 65536
        for width in (320, 448):
            half_width = width / 2
            for widescreen in (False, True):
                for entry, kind, dx, dy in (("SpriteMatrix", 1, -6.25, -42.75),
                                            ("SpriteMatrix", 2, -10.75, 44.75),
                                            ("MatrixWeapon", 0, -6.25, -42.75),
                                            ("MatrixHealth", 0, -10.75, 42.75)):
                    with self.subTest(width=width, widescreen=widescreen, entry=entry, kind=kind):
                        billboard = entry == "SpriteMatrix"
                        weight = 1 if billboard else half_width
                        vm, _, _, original, captured = self.fixture(kind, weight, half_width)
                        pair = 2 if kind == 2 else (1 if entry == "MatrixHealth" else 0)
                        address = constant(self.source, "WeaponDxNdcAddress") + pair * 8
                        store_word(vm.memory, address, float_bits(dx / half_width))
                        store_word(vm.memory, address + 4, float_bits(-dy / half_width))
                        vm.run(self.entries[entry], self.resume)
                        self.assertEqual(len(captured), 1)
                        anchor_x = -100
                        if widescreen:
                            anchor_x = 0.75 * (anchor_x - (48 if width == 320 else 68))
                        anchor_y = -68
                        anchor_w = half_width if billboard else 0
                        divisor = weight + anchor_w
                        for axis, desired, anchor in ((0, dx, anchor_x), (1, dy, anchor_y)):
                            before = fixed(bits_float(original[12 + axis])) + (anchor if billboard else 0)
                            after = fixed(bits_float(captured[0][12 + axis])) + (anchor if billboard else 0)
                            scale = half_width if axis == 0 else -half_width
                            actual = (after / divisor - before / divisor) * scale
                            self.assertAlmostEqual(actual, desired, delta=0.001)


if __name__ == "__main__":
    unittest.main()
