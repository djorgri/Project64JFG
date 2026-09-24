"""Execute the actual JFG ammunition HUD trampoline and inspect its cave slots.

Run: python -m unittest discover -s Source/Script/tests -p test_jfg_ammo_hud.py
The model checks MIPS register/memory effects and both texture-step consumers.
Visual quality and identification of the renderer require separate game tests.
"""

import re
import unittest
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[3]
          / "Source/Project64-core/N64System/GameHacks/JetForceGemini.cpp")
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1


def sign_extend(value, bits):
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


def register_word(value):
    return sign_extend(value & MASK32, 32) & MASK64


def constant(source, name):
    # Plain constants, and the US-term constants of JetForceGeminiHudBuild.h:
    # UsAddress/UsWord { us }, UsOffset { module, us }, BuildWord { us, pal }.
    # The tests model the US build, so each yields its US value.
    number = r"(?:0x[0-9a-fA-F]+|\d+)"
    match = re.search(r"(?:JfgHudBuild::(\w+)\s+)?\b" + re.escape(name) + r"\s*=\s*(?:\{\s*(" + number +
                      r"(?:\s*,\s*" + number + r")*)\s*\}|(" + number + r"))\s*;", source)
    if match is None:
        raise AssertionError("Missing literal C++ constant: " + name)
    if match.group(3) is not None:
        return int(match.group(3), 0)
    values = [int(value, 0) for value in re.split(r"\s*,\s*", match.group(2))]
    return values[1] if match.group(1) == "UsOffset" else values[0]


def array_body(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\};", source, re.S)
    if match is None:
        raise AssertionError("Missing C++ array: " + name)
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.S)


def code_array(source, name):
    words = [value.strip() for value in array_body(source, name).split(",") if value.strip()]
    if not all(re.fullmatch(r"0x[0-9a-fA-F]+|\d+", value) for value in words):
        raise AssertionError("Instruction array contains an unsupported expression")
    return [int(value, 0) for value in words]


def store_word(memory, address, value):
    for offset in range(4):
        memory[(address + offset) & MASK32] = (value >> (24 - offset * 8)) & 255


def load_word(memory, address):
    return sum(memory[(address + offset) & MASK32] << (24 - offset * 8) for offset in range(4))


class MipsMachine:
    """Minimal VR4300 integer model with 64-bit GPRs and branch delay slots."""

    def __init__(self, program, registers, memory):
        self.program = dict(program)
        self.registers = list(registers)
        self.memory = dict(memory)
        self.visited = []

    def write_register(self, register, value):
        if register:
            self.registers[register] = value & MASK64

    def execute_ordinary(self, word):
        opcode = word >> 26
        rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
        immediate, function = word & 0xFFFF, word & 63
        if opcode == 15:  # lui
            self.write_register(rt, sign_extend(immediate << 16, 32))
        elif opcode in (35, 36):  # lw / lbu
            address = (self.registers[rs] + sign_extend(immediate, 16)) & MASK32
            value = self.memory[address] if opcode == 36 else sign_extend(load_word(self.memory, address), 32)
            self.write_register(rt, value)
        elif opcode == 43:  # sw
            address = (self.registers[rs] + sign_extend(immediate, 16)) & MASK32
            store_word(self.memory, address, self.registers[rt])
        elif opcode == 12:  # andi
            self.write_register(rt, self.registers[rs] & immediate)
        elif opcode == 13:  # ori
            self.write_register(rt, self.registers[rs] | immediate)
        elif opcode == 9:  # addiu (32-bit result, sign-extended)
            self.write_register(rt, register_word(self.registers[rs] + sign_extend(immediate, 16)))
        elif opcode == 10:  # slti, signed 64-bit GPR compared with signed immediate
            self.write_register(rt, int(sign_extend(self.registers[rs], 64) < sign_extend(immediate, 16)))
        elif opcode == 0 and function in (0, 2, 3):  # sll / srl / sra (also nop)
            shift = (word >> 6) & 31
            value = self.registers[rt] & MASK32
            value = value << shift if function == 0 else sign_extend(value, 32) >> shift if function == 3 else value >> shift
            self.write_register(rd, register_word(value))
        elif opcode == 0 and function in (33, 35):  # addu / subu
            value = self.registers[rs] + self.registers[rt] if function == 33 else self.registers[rs] - self.registers[rt]
            self.write_register(rd, register_word(value))
        elif opcode == 0 and function == 37:  # or
            self.write_register(rd, self.registers[rs] | self.registers[rt])
        else:
            raise AssertionError("Unsupported/control-flow word in ordinary or delay slot: %#010x" % word)

    def run(self, start, resume):
        pc = start
        for _ in range(64):
            if pc == resume:
                return
            self.visited.append(pc)
            word = self.program[pc]
            opcode = word >> 26
            if opcode in (2, 4, 5):  # j / beq / bne
                if opcode == 2:
                    next_pc = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                else:
                    rs, rt = (word >> 21) & 31, (word >> 16) & 31
                    taken = self.registers[rs] == self.registers[rt]
                    if opcode == 5:
                        taken = not taken
                    next_pc = pc + 4 + (sign_extend(word & 0xFFFF, 16) << 2) if taken else pc + 8
                self.visited.append(pc + 4)
                self.execute_ordinary(self.program[pc + 4])
                pc = next_pc & MASK32
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("Trampoline did not return within its bounded instruction budget")


class JfgAmmoHudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.words = code_array(cls.source, "WidescreenHudAmmoCode")
        cls.stub = constant(cls.source, "WidescreenHudAmmoStub")
        cls.entry = constant(cls.source, "WidescreenHudAmmoEntry")
        cls.delay = constant(cls.source, "WidescreenHudAmmoDelayOriginal")
        cls.scope_address = constant(cls.source, "WidescreenHudScopeDepthAddress")
        cls.resolution_address = constant(cls.source, "WidescreenHudResolutionIndexAddress")
        cls.sp = 0x800FF000
        cls.resume = 0x80059014
        cls.step_patches = {
            int(address, 16): (int(original, 16), int(replacement, 16))
            for address, original, replacement in re.findall(
                r"\{\s*(?:JfgHudBuild::Address\()?(0x[\da-fA-F]+)\)?,\s*(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\s*\}",
                array_body(cls.source, "AmmoPatches"))
        }

    def fixture(self, scope, resolution, texture, x):
        width, stride = {8: (11, 12), 9: (8, 8), 10: (13, 16)}[texture]
        registers = [0] + [0x1122334400000000 | (i * 0x10203) for i in range(1, 32)]
        registers[18] = register_word(0x80200000 + texture * 0x1000)  # s2 texture pointer
        registers[19] = register_word(x)  # s3 framebuffer X after adding screen centre
        registers[21] = 46 * 4  # s5 top Y (10.2), must not be changed
        registers[24] = width * 4  # t8 destination width (10.2)
        registers[29] = register_word(self.sp)
        memory = {self.scope_address: scope, self.resolution_address: resolution}
        for address, value in {
            0x800FF418: 0x80208000,
            0x800FF41C: 0x80209000,
            self.sp + 0x88: 0xDEADBEEF,
            self.sp + 0x8C: 180,
            self.sp + 0x90: 320,
            self.sp + 0x9C: width,
            self.sp + 0xA0: stride,
            self.sp + 0xA4: 10 << 5,  # atlas T coordinate
            self.sp + 0xAC: 57 * 4,  # bottom Y (10.2)
            self.sp + 0xB0: 0xA1B2C3D4,
            self.sp + 0xB8: 0x55667788,
        }.items():
            store_word(memory, address, value)
        program = {self.stub + 4 * i: word for i, word in enumerate(self.words)}
        program[self.entry] = 0x08000000 | ((self.stub >> 2) & 0x03FFFFFF)
        program[self.entry + 4] = self.delay
        return MipsMachine(program, registers, memory), registers, memory, width, stride

    def test_original_site_and_all_hud_cave_slots(self):
        self.assertEqual(self.entry, 0x8005900C)
        self.assertEqual(constant(self.source, "WidescreenHudAmmoOriginal"), 0x00135080)
        self.assertEqual(self.delay, 0x030A4021)
        self.assertEqual(self.entry + 8, self.resume)
        self.assertEqual(self.stub, 0x80067610)
        self.assertEqual(len(self.words), 32)
        cave_start = constant(self.source, "WidescreenHudCaveStart")
        cave_end = constant(self.source, "WidescreenHudCaveEnd")
        self.assertLessEqual(cave_end, 0x800676B4)  # next original function
        reticle_start = constant(self.source, "WidescreenHudReticleStub")
        reticle_end = constant(self.source, "WidescreenHudReticleCaveEnd")
        self.assertEqual((cave_end, reticle_start, reticle_end), (0x80067690, 0x80067790, 0x80067950))
        segments = ((cave_start, cave_end), (reticle_start, reticle_end))
        addresses = [address for start, end in segments for address in range(start, end, 4)]
        self.assertEqual(len(addresses), 372)
        self.assertEqual(len(set(addresses)), len(addresses))
        self.assertFalse(set(addresses).intersection(range(0x80067690, 0x80067790, 4)))
        # Listings are placed through HudCode(), which translates them for PAL.
        placements = re.findall(r"!PlaceCode\((WidescreenHud\w+),\s*HudCode\((WidescreenHud\w+)\)", self.source)
        self.assertIn(("WidescreenHudAmmoStub", "WidescreenHudAmmoCode"), placements)
        self.assertIn(("WidescreenHudReticleStub", "WidescreenHudReticleCode"), placements)
        self.assertIn(("WidescreenHudFloydLineStub", "WidescreenHudFloydLineCode"), placements)
        self.assertGreaterEqual(len(placements), 13)
        claimed = {}
        all_placements = [(self.source, address, code) for address, code in placements]
        floyd_source = SOURCE.with_name("JetForceGeminiFloydHud.h").read_text(encoding="utf-8-sig")
        floyd_placements = re.findall(r"!PlaceCode\(JfgFloydHud::(\w+),\s*HudCode\(JfgFloydHud::(\w+)\)", self.source)
        self.assertEqual(set(floyd_placements), {
            ("GuardStub", "GuardCode"), ("InitStub", "InitCode"),
            ("StepStub", "StepCode"), ("StepTailStub", "StepTailCode"),
        })
        all_placements.extend((floyd_source, address, code) for address, code in floyd_placements)
        for source, address_name, code_name in all_placements:
            address = constant(source, address_name)
            words = code_array(source, code_name)
            self.assertEqual(address & 3, 0)
            self.assertTrue(any(start <= address and address + 4 * len(words) <= end for start, end in segments),
                            "%s crosses an unowned cave range" % code_name)
            for index in range(len(words)):
                location = address + 4 * index
                self.assertIn(location, addresses, "Stub claims the diagnostic logger gap")
                self.assertNotIn(location, claimed, "%s overlaps %s at %#x" % (code_name, claimed.get(location), location))
                claimed[location] = code_name

    def test_guards_width_stride_anchor_and_preserved_registers_and_uvs(self):
        for scope in (0, 1):
            for resolution in range(4):
                for texture in (8, 9, 10):
                    for x in (-200, -1, 0, 25, 61, 160, 224, 448):
                        with self.subTest(scope=scope, resolution=resolution, texture=texture, x=x):
                            machine, registers, memory, width, stride = self.fixture(scope, resolution, texture, x)
                            machine.run(self.entry, self.resume)
                            active = scope != 0 and resolution & 1 and texture in (8, 9)
                            expected_x = 3 * x + (20 if resolution & 2 else 16) if active else x * 4
                            expected_width = width * 3 if active else width * 4
                            expected_stride = stride * 3 // 4 if active else stride
                            step = 0x0555FC00 if active else 0x0400FC00
                            self.assertEqual(machine.registers[10], register_word(expected_x))
                            self.assertEqual(machine.registers[24], expected_width)
                            self.assertEqual(machine.registers[8], register_word(expected_x + expected_width))
                            # t0/t2/t8 are result registers; t7/t9 are dead scratch
                            # at this site. Every other GPR must remain unchanged.
                            for index in set(range(32)) - {8, 10, 15, 24, 25}:
                                self.assertEqual(machine.registers[index], registers[index], "Changed live GPR %d" % index)
                            expected_memory = dict(memory)
                            store_word(expected_memory, self.sp + 0x88, step)
                            store_word(expected_memory, self.sp + 0xA0, expected_stride)
                            self.assertEqual(machine.memory, expected_memory)
                            self.assertTrue(all(pc in (self.entry, self.entry + 4) or self.stub <= pc < self.stub + len(self.words) * 4
                                                for pc in machine.visited))
                            # The complete texture step reaches both normal
                            # digits and dimmed leading zeroes, after texFrame.
                            for load_address, combine_address in ((0x800590D0, 0x800590F4), (0x8005921C, 0x80059228)):
                                branch = MipsMachine({}, machine.registers, machine.memory)
                                branch.registers[20] = 0xBAD0BAD0  # no dependence on prior s4
                                branch.execute_ordinary(self.step_patches[load_address][1])
                                branch.execute_ordinary(self.step_patches[combine_address][1])
                                self.assertEqual(branch.registers[20], step)
                                self.assertEqual(branch.registers[20] & 0xFFFF, 0xFC00)
                            if active:
                                # Each right-to-left digit receives the same
                                # affine transform; the 11px atlas cell stays
                                # 11px while the rectangle becomes 8.25px.
                                for digit in range(1, 5):
                                    original_left = x - digit * stride
                                    compressed_left = expected_x - digit * expected_stride * 4
                                    self.assertEqual(compressed_left, original_left * 3 + (20 if resolution & 2 else 16))
                                self.assertEqual(expected_width, 33 if texture == 8 else 24)
                                self.assertEqual(expected_stride, 9 if texture == 8 else 6)

    def test_both_step_patch_pairs_match_the_original_rom(self):
        self.assertEqual(self.step_patches, {
            0x800590D0: (0x3C140400, 0x8FB40088),
            0x800590F4: (0x3694FC00, 0),
            0x8005921C: (0x3C140400, 0x8FB40088),
            0x80059228: (0x3694FC00, 0),
        })


if __name__ == "__main__":
    unittest.main()
