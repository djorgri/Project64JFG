"""Check the compiled HUD layout against native projected geometry.

The fixtures are retail US HUD coordinates, including the sprite billboard W.
No ROM or rendering plugin is required. The icon assertion covers its placement
pivot; the texture's integer vertex grid and transparent artwork are separate.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_jfg_hud_alignment as opcodes
from test_jfg_ammo_hud import constant, load_word, store_word
from test_jfg_banner_hud import bits_float, float_bits
from test_jfg_widescreen_lifecycle import US_BUILD, compiler_command


WORKSPACE = Path(__file__).resolve().parents[3]
HEADER = WORKSPACE / "Source/Project64-core/N64System/GameHacks/JetForceGeminiHudAlignment.h"


class JfgHudAlignmentGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = WORKSPACE / "build"
        build.mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="jfg-alignment-geometry-", dir=build)
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name).resolve()
        assert directory.is_relative_to(build.resolve())
        source = directory / "geometry.cpp"
        source.write_text('#include "' + HEADER.as_posix() + '"\n' + US_BUILD + r'''
#include <iostream>
#include <iomanip>
int main() {
    std::cout << std::setprecision(9);
    for (uint8_t mode = 0; mode < 4; ++mode) {
        for (unsigned corrected = 0; corrected < 2; ++corrected) {
            const auto layout = JfgHudAlignment::CalculateLayout(mode, corrected != 0);
            std::vector<uint32_t> image;
            if (!JfgHudAlignment::BuildImage(image, 0x80300000, mode, corrected != 0)) return 1;
            std::cout << unsigned(mode) << ' ' << corrected << ' ' << layout.Width << ' ' << layout.Height
                      << ' ' << layout.WeaponDx << ' ' << layout.WeaponDy
                      << ' ' << layout.HealthArcDx << ' ' << layout.HealthArcDy
                      << ' ' << layout.HealthSpriteDx << ' ' << layout.HealthSpriteDy;
            for (uint32_t address = JfgHudAlignmentCode::WeaponDxNdcAddress;
                 address <= JfgHudAlignmentCode::HalfWidthAddress; address += 4)
                std::cout << ' ' << image.at((address - JfgHudAlignment::CaveStart) / 4);
            for (uint32_t address = JfgHudAlignmentRdp::ParamsStart;
                 address < JfgHudAlignmentRdp::ParamsStart + 16; address += 4)
                std::cout << ' ' << image.at((address - JfgHudAlignment::CaveStart) / 4);
            std::cout << ' ' << image.size() << '\n';
        }
    }
    std::vector<uint32_t> invalid;
    return JfgHudAlignment::BuildImage(invalid, 0x80300000, 4, false) ? 2 : 0;
}
''', encoding="utf-8")
        executable = directory / ("geometry.exe" if os.name == "nt" else "geometry")
        result = subprocess.run(compiler_command(directory, source, executable), cwd=directory,
                                capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError("C++ geometry harness failed to compile:\n" + result.stdout + result.stderr)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
        if result.returncode:
            raise AssertionError("C++ geometry harness failed:\n" + result.stdout + result.stderr)
        cls.layouts = {}
        for line in result.stdout.splitlines():
            values = line.split()
            mode, corrected, width, height = map(int, values[:4])
            cls.layouts[mode, corrected] = (width, height, tuple(map(float, values[4:10])),
                                            tuple(map(int, values[10:17])),
                                            tuple(map(int, values[17:21])), int(values[21]))
        opcodes.JfgHudAlignmentTests.setUpClass()
        cls.machine_fixture = opcodes.JfgHudAlignmentTests()

    @staticmethod
    def baseline(mode, corrected):
        width, height = ((320, 240) if mode < 2 else (448, 336))
        wide = mode in (1, 3)
        scale = 0.75 if wide and corrected else 1.0
        bias = (48 if width == 320 else 68) if wide and corrected else 0
        half = width / 2
        weapon = (half + scale * (-141 - bias), height / 2 - 107)
        arc = (half + scale * (-99 - bias), height / 2 + 70)
        sprite = (half + scale * (-100 - bias) * half / (half + 1),
                  height / 2 + 68 * half / (half + 1))
        return width, height, scale, weapon, arc, sprite

    def test_quarter_pixel_margins_match_all_native_modes(self):
        self.assertEqual(len(self.layouts), 8)
        # Widescreen X targets include the VI's left blanking: +4 low, +5.5 high.
        targets = ((13, 13), (13.75, 13), (18.25, 18.25), (19.25, 18.25))
        # Only the health group receives the widescreen optical compensation:
        # -1.5 framebuffer pixels in mode 1 and -2.0 in mode 3. In 4:3 both
        # groups retain exactly the original 13-unit alignment targets.
        health_targets_x = (13, 12.25, 18.25, 17.25)
        for key, (width, height, shifts, _, rdp, count) in self.layouts.items():
            with self.subTest(mode=key[0], corrected=key[1]):
                _, _, scale, weapon, arc, _ = self.baseline(*key)
                target_x, target_y = targets[key[0]]
                wx, wy, ax, ay, _, _ = shifts
                self.assertAlmostEqual(weapon[0] + wx, target_x, places=5)
                self.assertAlmostEqual(weapon[1] + wy, target_y, places=5)
                self.assertAlmostEqual(arc[0] - 37 * scale + ax,
                                       health_targets_x[key[0]], places=5)
                self.assertAlmostEqual(height - (arc[1] + 37 + ay), target_y, places=5)
                signed = lambda word: word if word < 0x80000000 else word - 0x100000000
                self.assertEqual((signed(rdp[0]), signed(rdp[1])), (round(wx * 4), round(wy * 4)))
                self.assertEqual(rdp[2:], (width * 4, height * 4))
                self.assertEqual(count, 0x700 // 4)

    def test_widescreen_visible_margin_matches_height_after_vi_blanking(self):
        # Model the display separately from the framebuffer targets. The VI
        # hides eight horizontal output samples; a 640-wide active 16:9 image
        # has an equivalent square-pixel height of 360. This catches a margin
        # that is correct in RDRAM but too small at the visible left edge.
        for mode in (1, 3):
            for corrected in (0, 1):
                with self.subTest(mode=mode, corrected=corrected):
                    width, height, shifts, _, _, _ = self.layouts[mode, corrected]
                    _, _, _, weapon, _, _ = self.baseline(mode, corrected)
                    x_scale = ((width << 9) // 320) / 1024
                    visible_left = (weapon[0] + shifts[0]) / x_scale - 8
                    visible_top = (weapon[1] + shifts[1]) * 360 / height
                    self.assertAlmostEqual(visible_left, visible_top, delta=0.15)

    def test_health_pivot_centers_after_billboard_projection(self):
        for key, (_, _, shifts, _, _, _) in self.layouts.items():
            with self.subTest(mode=key[0], corrected=key[1]):
                _, _, _, _, arc, sprite = self.baseline(*key)
                _, _, ax, ay, sx, sy = shifts
                self.assertAlmostEqual(sprite[0] + sx, arc[0] + ax, places=5)
                self.assertAlmostEqual(sprite[1] + sy, arc[1] + ay, places=5)

    def test_compiled_parameters_produce_pixel_shifts_without_changing_shape_or_depth(self):
        fixture = self.machine_fixture
        fixed = lambda value: int(value * 65536) / 65536
        for key, (width, _, shifts, params, _, _) in self.layouts.items():
            half = width / 2
            self.assertEqual(bits_float(params[-1]), half)
            scale = self.baseline(*key)[2]
            for entry, kind, pair in (("MatrixWeapon", 0, 0), ("MatrixHealth", 0, 1),
                                      ("SpriteMatrix", 1, 0), ("SpriteMatrix", 2, 2)):
                with self.subTest(mode=key[0], corrected=key[1], entry=entry, kind=kind):
                    billboard = entry == "SpriteMatrix"
                    weight = 1 if billboard else half
                    vm, _, _, _, captured = fixture.fixture(kind, weight, half)
                    original = [float_bits(value) for value in
                                (scale, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, weight)]
                    for index, word in enumerate(original):
                        store_word(vm.memory, fixture.source_matrix + index * 4, word)
                    for index, word in enumerate(params):
                        store_word(vm.memory, constant(fixture.source, "WeaponDxNdcAddress") + index * 4, word)
                    vm.run(fixture.entries[entry], fixture.resume)
                    self.assertEqual(len(captured), 1)
                    for index in range(16):
                        self.assertEqual(load_word(vm.memory, fixture.source_matrix + index * 4), original[index])
                        if index not in (12, 13):
                            self.assertEqual(captured[0][index], original[index])
                    divisor = weight + (half if billboard else 0)
                    for axis in (0, 1):
                        converted = fixed(bits_float(captured[0][12 + axis]))
                        displacement = converted / divisor * half * (1 if axis == 0 else -1)
                        self.assertAlmostEqual(displacement, shifts[pair * 2 + axis], delta=0.001)

    def test_widescreen_correction_flag_has_no_effect_in_four_three(self):
        for mode in (0, 2):
            self.assertEqual(self.layouts[mode, 0], self.layouts[mode, 1])


if __name__ == "__main__":
    unittest.main()
