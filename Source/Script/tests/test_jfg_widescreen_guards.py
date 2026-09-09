"""Prove the remaining HUD trampolines keep stock rendering in 4:3 modes.

Run with PYTHONDONTWRITEBYTECODE=1:
python -m unittest discover -s Source/Script/tests -p test_jfg_*.py
The real C++ instruction arrays are run with resolution indices 0 and 2,
including a nonzero HUD scope. Results are compared with the displaced stock
instructions. Ammo, banner-cap and reticle guards have their own existing tests.
"""

import unittest

from test_jfg_ammo_hud import SOURCE, code_array, constant, register_word, store_word
from test_jfg_banner_hud import FpuMachine, float_bits


class JfgWidescreenGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8-sig")
        cls.scope_address = constant(cls.source, "WidescreenHudScopeDepthAddress")
        cls.resolution_address = constant(cls.source, "WidescreenHudResolutionIndexAddress")

    def assert_stock_rendering(self, name, resume, stock_words, scratch,
                               resolution, scope, gpr=None, fpr=None,
                               memory_words=None, hook_delay=None, replay_at_resume=None):
        stub = constant(self.source, "WidescreenHud" + name + "Stub")
        words = code_array(self.source, "WidescreenHud" + name + "Code")
        program = {stub + 4 * index: word for index, word in enumerate(words)}
        registers = [0] + [0x1122334400000000 | index * 0x10203 for index in range(1, 32)]
        for register, value in {1: 0x80100000, 7: 0x80101000, 16: 0x80210000,
                                29: 0x800FE000, 31: 0x80344008}.items():
            registers[register] = register_word(value)
        for register, value in (gpr or {}).items():
            registers[register] = register_word(value)
        floating_registers = [float_bits(index + 0.25) for index in range(32)]
        for register, value in (fpr or {}).items():
            floating_registers[register] = float_bits(value)
        memory = {self.scope_address: scope, self.resolution_address: resolution}
        for address, value in {
            0x80103128: float_bits(1.0),  # camCopyOrthoMatrix's original scalar
            0x80101000: float_bits(2.5),  # matrixTranslate's displaced load
            0x80103B90: 0x00000103,      # fxDrawLine queue state
            0x80210000: 0x87654321,      # frontDrawRectangles display-list word
            **(memory_words or {}),
        }.items():
            store_word(memory, address, value)
        original = FpuMachine({}, registers, memory, floating_registers, fcsr=0x01800007)
        patched = FpuMachine(program, registers, memory, floating_registers, fcsr=0x01800007)
        original.hi = patched.hi = 0x0123456789ABCDEF
        original.lo = patched.lo = 0xFEDCBA9876543210
        for word in stock_words:
            original.execute_ordinary(word)
        if hook_delay is not None:
            patched.execute_ordinary(hook_delay)
        patched.run(stub, resume)
        if replay_at_resume is not None:
            patched.execute_ordinary(replay_at_resume)
        for register in set(range(32)) - set(scratch):
            self.assertEqual(patched.registers[register], original.registers[register],
                             "%s changed stock GPR %d in resolution %d/scope %d" % (name, register, resolution, scope))
        self.assertEqual(patched.memory, original.memory)
        self.assertEqual(patched.fpr, original.fpr, name + " changed stock FPU values")
        self.assertEqual(patched.fcsr, original.fcsr, name + " changed the FCSR")
        self.assertEqual((patched.hi, patched.lo), (original.hi, original.lo))
        self.assertTrue(all(stub <= address < stub + len(words) * 4 for address in patched.visited))
        return patched

    def modes_and_scopes(self):
        for resolution in (0, 2):
            for scope in (0, 1):
                yield resolution, scope

    def test_orthographic_matrix_load_stays_stock(self):
        original = constant(self.source, "WidescreenHudCamCopyOriginal")
        for resolution, scope in self.modes_and_scopes():
            for scalar in (0.5, 1.0, 1.25, 2.0):
                with self.subTest(resolution=resolution, scope=scope, scalar=scalar):
                    result = self.assert_stock_rendering(
                        "CamCopy", 0x80042160, [original], {8, 9}, resolution, scope,
                        memory_words={0x80103128: float_bits(scalar)})
                    self.assertEqual(result.fpr[4], float_bits(scalar))

    def test_font_rectangle_y_and_packed_command_stay_stock(self):
        original = constant(self.source, "WidescreenHudFontYOriginal")
        # The original OR at +4 first executes prematurely in the hook's delay
        # slot, then the trampoline returns to it after restoring t8/t6.
        delay = 0x030E7825  # or t7,t8,t6
        for resolution, scope in self.modes_and_scopes():
            for top in (-100, 0, 97, 500, 4090):
                for height in (0, 1, 16, 44, 63):
                    with self.subTest(resolution=resolution, scope=scope, top=top, height=height):
                        result = self.assert_stock_rendering(
                            "FontY", 0x80070504, [original, delay], {25}, resolution, scope,
                            gpr={11: top, 13: top + height, 14: 0x12345678, 24: 0xE4123000},
                            hook_delay=delay, replay_at_resume=delay)
                        self.assertEqual(result.registers[11], register_word(top))
                        self.assertEqual(result.registers[13], register_word(top + height))

    def test_font_texture_step_stays_one_in_both_axes(self):
        original = constant(self.source, "WidescreenHudFontDtdyOriginal")
        delay = 0x35CE0400  # ori t6,t6,0x0400
        for resolution, scope in self.modes_and_scopes():
            for previous_step in (0, 0x12345678, 0xFFFFFFFF):
                with self.subTest(resolution=resolution, scope=scope, previous_step=previous_step):
                    result = self.assert_stock_rendering(
                        "FontDtdy", 0x80070558, [original, delay], {24, 25}, resolution, scope,
                        gpr={14: previous_step}, hook_delay=delay)
                    self.assertEqual(result.registers[14], 0x04000400)

    def test_both_sprite_scale_paths_preserve_native_scale_and_y(self):
        original = constant(self.source, "WidescreenHudSpriteScaleOriginal")
        for name, resume in (("SpriteScale", 0x80041858), ("SpriteScaleAlt", 0x800418A4)):
            for resolution, scope in self.modes_and_scopes():
                for scale in (0.0, 0.5, 1.0, 2.75):
                    with self.subTest(path=name, resolution=resolution, scope=scope, scale=scale):
                        result = self.assert_stock_rendering(
                            name, resume, [original], {24, 25}, resolution, scope,
                            fpr={0: scale, 2: 19.25, 12: -30.5, 14: 77.25})
                        self.assertEqual(result.registers[5], register_word(float_bits(scale)))

    def test_matrix_translation_never_reaches_aspect_comparison_or_changes_xy(self):
        original = constant(self.source, "WidescreenHudMatrixTranslateOriginal")
        for resolution, scope in self.modes_and_scopes():
            for x in (-200.0, -31.5, -31.0, 0.0, 31.5, 32.0, 448.0):
                with self.subTest(resolution=resolution, scope=scope, x=x):
                    result = self.assert_stock_rendering(
                        "MatrixTranslate", 0x800498F0, [original], {8, 9, 10}, resolution, scope,
                        fpr={0: -1.0, 2: 66.5, 12: x, 14: -29.25})
                    self.assertEqual(result.fpr[12], float_bits(x))
                    self.assertEqual(result.fpr[14], float_bits(-29.25))

    def test_lines_keep_both_endpoint_coordinates_and_queue_state(self):
        originals = [constant(self.source, "WidescreenHudLineOriginal"),
                     constant(self.source, "WidescreenHudLineDelayOriginal")]
        for resolution, scope in self.modes_and_scopes():
            for first, second in ((-300, -31), (0, 1), (17, 319), (32, 448), (448, 0)):
                with self.subTest(resolution=resolution, scope=scope, first=first, second=second):
                    result = self.assert_stock_rendering(
                        "Line", 0x8006D398, originals, {15, 24, 25}, resolution, scope,
                        gpr={4: first, 5: 73, 6: second, 7: -22})
                    self.assertEqual(result.registers[14], 0x103)

    def test_rectangles_keep_x_y_and_the_original_display_list_packing(self):
        originals = [constant(self.source, "WidescreenHudRectangleOriginal"),
                     constant(self.source, "WidescreenHudRectangleDelayOriginal")]
        for resolution, scope in self.modes_and_scopes():
            for left, right in ((-32, 31), (0, 319), (65, 215), (114, 264), (0, 448)):
                with self.subTest(resolution=resolution, scope=scope, left=left, right=right):
                    result = self.assert_stock_rendering(
                        "Rectangle", 0x80059798, originals, {15}, resolution, scope,
                        gpr={4: 17, 5: 29, 6: left, 7: 73, 24: right})
                    self.assertEqual(result.registers[24], register_word(right))
                    self.assertEqual(result.registers[6], register_word(left))
                    self.assertEqual(result.registers[25], register_word(right << 14))


if __name__ == "__main__":
    unittest.main()
