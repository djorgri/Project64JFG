"""Execute the actual generated weapon-group RDP wrapper on synthetic streams.

The C++ BuildImage function produces the executed words. The stock body is a
call boundary which records arguments, emits a cursor and supplies return values.
This checks command coordinates and ABI effects without a ROM or emulator.
"""

from functools import lru_cache
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_jfg_ammo_hud import (
    MASK32, MASK64, MipsMachine, load_word, register_word, sign_extend, store_word,
)
import test_jfg_widescreen_lifecycle as compiler


START, END = 0x80067D20, 0x800680A0
PARAMS, CURSOR = 0x80068080, 0x800FF398
BODY, SP, RETURN = 0x8034292C, 0x800FD000, 0x80340CA4
BODY_V0, BODY_V1 = 0x123456789ABCDEF0, 0xFEDCBA9876543210


class RdpMachine(MipsMachine):
    def __init__(self, program, registers, memory, body, end):
        super().__init__(program, registers, memory)
        self.body = body
        self.end = end
        self.body_arguments = []

    def execute_ordinary(self, word):
        op = word >> 26
        rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
        if op in (55, 63):  # ld / sd
            address = (self.registers[rs] + sign_extend(word & 0xFFFF, 16)) & MASK32
            if op == 55:
                value = sum(self.memory[address + i] << (56 - i * 8) for i in range(8))
                self.write_register(rt, value)
            else:
                for i in range(8):
                    self.memory[address + i] = (self.registers[rt] >> (56 - i * 8)) & 255
        elif op == 0 and (word & 63) in (36, 42, 43, 45):
            function = word & 63
            left, right = self.registers[rs], self.registers[rt]
            if function == 36:  # and
                value = left & right
            elif function == 42:  # slt, signed 64-bit operands
                value = int(sign_extend(left, 64) < sign_extend(right, 64))
            elif function == 43:  # sltu
                value = int(left < right)
            else:  # daddu
                value = left + right
            self.write_register(rd, value)
        else:
            super().execute_ordinary(word)

    def invoke_body(self):
        stack = self.registers[29] & MASK32
        self.body_arguments.append((list(self.registers[4:8]),
                                    [load_word(self.memory, stack + offset) for offset in range(0x10, 0x20, 4)]))
        # An ordinary callee may destroy its argument and temporary registers.
        # The scanner must recover its start cursor from its own stack frame.
        for reg in list(range(4, 16)) + [24, 25]:
            self.write_register(reg, 0xCAFE000000000000 | reg)
        self.write_register(2, BODY_V0)
        self.write_register(3, BODY_V1)
        store_word(self.memory, CURSOR, self.end)

    def run(self, start=START, resume=RETURN):
        pc = start
        for _ in range(3_000_000):
            if pc == resume:
                return
            if pc == self.body:
                self.invoke_body()
                pc = self.registers[31] & MASK32
                continue
            self.visited.append(pc)
            word = self.program[pc]
            op = word >> 26
            is_jr = op == 0 and (word & 63) == 8
            if op in (2, 3, 4, 5) or is_jr:
                if is_jr:
                    target = self.registers[(word >> 21) & 31] & MASK32
                elif op in (2, 3):
                    target = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                    if op == 3:
                        self.write_register(31, register_word(pc + 8))
                else:
                    rs, rt = (word >> 21) & 31, (word >> 16) & 31
                    taken = self.registers[rs] == self.registers[rt]
                    if op == 5:
                        taken = not taken
                    target = pc + 4 + sign_extend(word & 0xFFFF, 16) * 4 if taken else pc + 8
                self.visited.append(pc + 4)
                self.execute_ordinary(self.program[pc + 4])
                pc = target & MASK32
            else:
                self.execute_ordinary(word)
                pc += 4
        raise AssertionError("RDP wrapper exceeded its bounded instruction budget")


def pack(opcode, x0, y0, x1, y1, metadata=0):
    return (opcode << 24) | (x0 << 12) | y0, (metadata << 24) | (x1 << 12) | y1


def translated(command, dx, dy, width, height):
    first, second = command
    op = first >> 24
    if op not in (0xE4, 0xE5, 0xF6, 0xED):
        return command
    if op == 0xED and first & 0xFFFFFF == 0 and (second >> 12) & 0xFFF == width and second & 0xFFF == height:
        return command
    def shift(word):
        x, y = (word >> 12) & 0xFFF, word & 0xFFF
        return (word & 0xFF000000) | (min(4095, max(0, x + dx)) << 12) | min(4095, max(0, y + dy))
    return shift(first), shift(second)


class JfgHudAlignmentRdpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = compiler.WORKSPACE / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="jfg-alignment-rdp-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name).resolve()
        assert directory.is_relative_to(build.resolve())
        header = compiler.HACKS / "JetForceGeminiHudAlignmentRdp.h"
        source = directory / "image.cpp"
        source.write_text('#include "' + header.as_posix() + r'''"
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    if (argc != 6) return 2;
    std::vector<uint32_t> image = {0xDEADBEEF};
    bool ok = JfgHudAlignmentRdp::BuildImage(image, uint32_t(std::stoull(argv[1], nullptr, 0)),
        int32_t(std::stoll(argv[2])), int32_t(std::stoll(argv[3])),
        uint32_t(std::stoull(argv[4])), uint32_t(std::stoull(argv[5])));
    std::cout << ok;
    for (uint32_t word : image) std::cout << ' ' << std::hex << word;
    return 0;
}
''', encoding="utf-8")
        cls.executable = directory / ("image.exe" if os.name == "nt" else "image")
        command = compiler.compiler_command(directory, source, cls.executable)
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError("RDP image builder failed to compile:\n" + result.stdout + result.stderr)

    @classmethod
    @lru_cache(maxsize=None)
    def build_image(cls, dx=-21, dy=13, width=1280, height=960, body=BODY):
        result = subprocess.run([str(cls.executable), str(body), str(dx), str(dy), str(width), str(height)],
                                capture_output=True, text=True, timeout=20)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        values = result.stdout.split()
        return values[0] == "1", [int(value, 16) for value in values[1:]]

    def fixture(self, commands, dx=-21, dy=13, width=1280, height=960,
                begin=0x80200000, end=None, body=BODY):
        ok, image = self.build_image(dx, dy, width, height, body)
        self.assertTrue(ok)
        end = begin + len(commands) * 8 if end is None else end
        program = {START + i * 4: word for i, word in enumerate(image)}
        memory = {}
        for address, word in program.items():
            store_word(memory, address, word)
        for i, command in enumerate(commands):
            for j, word in enumerate(command):
                store_word(memory, begin + i * 8 + j * 4, word)
        for address in range(SP - 0x60, SP + 0x40, 4):
            store_word(memory, address, 0xABCD0000 | (address & 0xFFFF))
        store_word(memory, CURSOR, begin)
        registers = [0] + [0x1122334400000000 | i * 0x10203 for i in range(1, 32)]
        registers[29] = register_word(SP)
        registers[31] = 0xCAFEBABE00000000 | RETURN
        return RdpMachine(program, registers, memory, body, end), registers, memory

    def assert_abi(self, machine, registers, memory):
        self.assertEqual(machine.body_arguments, [(
            registers[4:8], [load_word(memory, SP + offset) for offset in range(0x10, 0x20, 4)])])
        self.assertEqual(machine.registers[2:4], [BODY_V0, BODY_V1])
        for reg in list(range(16, 24)) + [26, 27, 28, 29, 30, 31]:
            self.assertEqual(machine.registers[reg], registers[reg], "Changed saved/special GPR %d" % reg)
        for address in range(SP, SP + 0x40):
            self.assertEqual(machine.memory[address], memory[address], "Modified caller stack")
        self.assertEqual(load_word(machine.memory, CURSOR), machine.end)
        self.assertTrue(all(START <= pc < PARAMS for pc in machine.visited))

    def test_builder_segment_body_relocation_parameters_and_validation(self):
        ok, image = self.build_image()
        self.assertTrue(ok)
        self.assertEqual(len(image) * 4, END - START)
        self.assertEqual(image[(0x80067D54 - START) // 4], 0x0C000000 | ((BODY >> 2) & 0x03FFFFFF))
        self.assertEqual(image[(PARAMS - START) // 4:(PARAMS - START) // 4 + 5],
                         [(-21) & MASK32, 13, 1280, 960, 0x4A464752])
        for body in (0, 0x7FFFFFFC, BODY + 1, START, 0x80800000, 0xA0000000):
            with self.subTest(body=hex(body)):
                self.assertEqual(self.build_image(body=body), (False, [0xDEADBEEF]))
        for width, height in ((0, 960), (1280, 0), (4096, 960), (1280, 4096)):
            self.assertEqual(self.build_image(width=width, height=height), (False, [0xDEADBEEF]))

    def test_translates_only_rectangle_and_local_scissor_coordinates(self):
        commands = [
            pack(0xE4, 101, 303, 61, 201, 7),
            (0xB3000000, 0xF6EDB2E4),  # UV payload is never a command
            (0xB2000000, 0x0555FC00),  # texture step remains exact
            pack(0xE5, 900, 800, 700, 600, 0xA3),
            pack(0xF6, 4095, 1, 0, 4095, 0x80),
            pack(0xED, 12, 16, 680, 240, 2),
            pack(0xED, 0, 0, 1280, 960, 3),
            (0x01000104, 0x802ABCDE),  # SP vertex command: matrix path shifts it
            (0xB1000000, 0x00020406),
            (0xDE000000, 0x80301200),  # no recursion into nested display lists
            (0xDF000000, 0),
        ]
        machine, registers, before = self.fixture(commands)
        machine.run()
        expected = dict(before)
        for index, command in enumerate(commands):
            shifted = translated(command, -21, 13, 1280, 960)
            for offset, word in enumerate(shifted):
                store_word(expected, 0x80200000 + index * 8 + offset * 4, word)
        store_word(expected, CURSOR, machine.end)
        for address, value in expected.items():
            if not SP - 0x60 <= address < SP:
                self.assertEqual(machine.memory[address], value, "Unexpected write at %#x" % address)
        self.assert_abi(machine, registers, before)

    def test_saturates_all_four_fields_without_wrapping_signed_offsets(self):
        commands = [pack(op, 0, 4095, 4094, 1, 0xA7) for op in (0xE4, 0xE5, 0xF6, 0xED)]
        offsets = (-2147483648, -4096, -4095, -128, -1, 0, 1, 128, 4095, 4096, 2147483647)
        for dx in offsets:
            for dy in offsets:
                with self.subTest(dx=dx, dy=dy):
                    machine, registers, before = self.fixture(commands, dx=dx, dy=dy)
                    machine.run()
                    for index, command in enumerate(commands):
                        actual = tuple(load_word(machine.memory, 0x80200000 + index * 8 + offset * 4) for offset in range(2))
                        self.assertEqual(actual, translated(command, dx, dy, 1280, 960))
                    self.assert_abi(machine, registers, before)

    def test_full_frame_scissor_reset_is_unchanged_at_both_resolutions(self):
        for width, height in ((1280, 960), (1792, 1344), (1280, 720), (1792, 1008)):
            commands = [pack(0xED, 0, 0, width, height, mode) for mode in (0, 1, 2, 3)]
            commands += [pack(0xED, 0, 1, width, height), pack(0xED, 0, 0, width - 1, height)]
            machine, registers, before = self.fixture(commands, width=width, height=height)
            machine.run()
            for index, command in enumerate(commands):
                actual = tuple(load_word(machine.memory, 0x80200000 + index * 8 + offset * 4) for offset in range(2))
                self.assertEqual(actual, translated(command, -21, 13, width, height))
            self.assert_abi(machine, registers, before)

    def test_invalid_or_empty_cursor_ranges_return_without_reading_the_stream(self):
        ranges = ((0x80200004, 0x80200008), (0x80200000, 0x8020000C),
                  (0x00200000, 0x00200008), (0x7FFFFFF8, 0x80000000),
                  (0x80200008, 0x80200000), (0x80200000, 0x80800008),
                  (0xA0200000, 0xA0200008), (0x80200000, 0x80220008),
                  (0x80200000, 0x80200000), (0x80800000, 0x80800000))
        for begin, end in ranges:
            with self.subTest(begin=hex(begin), end=hex(end)):
                machine, registers, before = self.fixture([], begin=begin, end=end)
                machine.run()  # unmapped stream reads would raise KeyError
                self.assert_abi(machine, registers, before)
                for address, value in before.items():
                    if not SP - 0x60 <= address < SP and not CURSOR <= address < CURSOR + 4:
                        self.assertEqual(machine.memory[address], value)

    def test_exact_span_limit_and_last_rdram_command_are_accepted(self):
        for begin, commands in ((0x807FFFF8, [pack(0xE4, 30, 40, 10, 20)]),
                                (0x80200000, [(0xB1000000, 0x00020406)] * (0x20000 // 8 - 1) +
                                 [pack(0xF6, 30, 40, 10, 20)])):
            machine, registers, before = self.fixture(commands, begin=begin)
            machine.run()
            last = begin + (len(commands) - 1) * 8
            self.assertEqual((load_word(machine.memory, last), load_word(machine.memory, last + 4)),
                             translated(commands[-1], -21, 13, 1280, 960))
            self.assert_abi(machine, registers, before)

    def test_relocated_body_receives_all_arguments_and_returns_64_bit_values(self):
        for body in (0x8034292C, 0x8038292C):
            machine, registers, before = self.fixture([pack(0xE4, 60, 70, 40, 50)], body=body)
            machine.run()
            self.assert_abi(machine, registers, before)


if __name__ == "__main__":
    unittest.main()
