"""Export the original rocket reticle from a user's US Project64 save state.

Uses the original CPU line renderer fixture, not a screenshot or a resampler.
Requires Pillow. Coordinates and source records accompany the editable BMP.
"""

import argparse
import json
from pathlib import Path
import struct
import sys
import zipfile

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / "tests"))
from test_jfg_reticle_raster import JfgReticleRasterTests, STOCK_PROGRAM


def export(state, output):
    with zipfile.ZipFile(state) as archive:
        raw = archive.read(archive.namelist()[0])
    ram_size = struct.unpack_from("<I", raw, 4)[0]
    if ram_size not in (0x400000, 0x800000):
        raise ValueError("Unsupported Project64 state RAM size")
    ram = bytearray(raw[0xA60:0xA60 + ram_size])
    for i in range(0, len(ram), 4):
        ram[i:i + 4] = ram[i:i + 4][::-1]

    def offset(address, size):
        if not 0x80000000 <= address <= 0x80000000 + len(ram) - size:
            raise ValueError("Invalid save-state pointer")
        return address - 0x80000000

    def word(address):
        return struct.unpack_from(">I", ram, offset(address, 4))[0]

    base = word(word(0x800FEAA0) + 13 * 0x20)
    if word(base + 0x4A8) != 0x27BDFF68:
        raise ValueError("US frontDrawTarget signature does not match")
    descriptor = base + 0x166C + 5 * 16
    source = word(descriptor)
    count = struct.unpack_from(">H", ram, offset(descriptor + 4, 2))[0]
    if count != 11:
        raise ValueError("Rocket reticle descriptor does not match")
    records = [struct.unpack_from(">hhhhH", ram, offset(source + i * 10, 10))
               for i in range(count)]
    if records[0] != (-16, -9, -8, -17, 0x1C02):
        raise ValueError("Rocket reticle geometry does not match the original")

    transforms = {
        0: (lambda x, y: (x, y),),
        1: (lambda x, y: (x, y), lambda x, y: (-x, y)),
        2: (lambda x, y: (x, y), lambda x, y: (x, -y)),
        3: (lambda x, y: (x, y), lambda x, y: (-x, -y),
            lambda x, y: (-y, x), lambda x, y: (y, -x)),
        4: (lambda x, y: (x, y), lambda x, y: (-x, -y)),
    }
    # Normal aiming state (no target lock), without dynamic distance/angle text.
    renderer = JfgReticleRasterTests()
    renderer.program = STOCK_PROGRAM
    mask = Image.new("1", (65, 65), 0)
    segments = []
    for x0, y0, x1, y1, flags in records:
        if flags & 0x200:
            continue  # visible only when a target is acquired
        style = flags & 15
        if style not in (0, 2):
            raise ValueError("Unexpected rocket line style")
        for transform in transforms[flags >> 11]:
            a, b = transform(x0, y0), transform(x1, y1)
            segments.append([*a, *b, flags & 255])
            if style == 0:
                # fxDrawLine sorts both axes for this native axis-aligned style.
                a, b = (min(a[0], b[0]), min(a[1], b[1])), (max(a[0], b[0]), max(a[1], b[1]))
            if a == b:
                continue
            line = (160 + a[0], 120 + a[1], 160 + b[0], 120 + b[1])
            for x, y in renderer.render(line, flags & 255).plots:
                px, py = x - 160 + 32, y - 120 + 32
                if not (0 <= px < 65 and 0 <= py < 65):
                    raise ValueError("Reticle exceeded export canvas")
                mask.putpixel((px, py), 255)

    output.mkdir(parents=True, exist_ok=True)
    mask.save(output / "rocket-reticle.bmp")
    mask.save(output / "rocket-reticle.png")
    mask.resize((520, 520), Image.Resampling.NEAREST).save(output / "preview-x8.png")
    metadata = {"weapon_index": 5, "name": "Tri-Rocket Launcher", "state": "no target lock",
                "source_state": state.name, "size": [65, 65], "aim_center": [32, 32],
                "foreground": "white", "background": "black", "scale": "1:1 original framebuffer pixels",
                "dynamic_text_included": False, "source_records": records, "expanded_segments": segments}
    (output / "coordinates.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    (output / "README.txt").write_text(
        "Tri-Rocket Launcher reticle, original rendering without the 16:9 correction.\n"
        "\nFile to edit: rocket-reticle.bmp (65 x 65 pixels, monochrome).\n"
        "White = stroke; black = empty background. Aim centre: x=32, y=32 (origin top left).\n"
        "One pixel of the file is one pixel of the original framebuffer.\n"
        "Keep the dimensions and the centre; draw with a pencil tool, no anti-aliasing.\n"
        "The PNG holds the same mask. preview-x8.png is only an enlargement for reading.\n"
        "The distance/angle digits and the colour changes are not included.\n"
        "The game's green intensities are deliberately merged into white to get an editable mask.\n"
        "This export is not loaded by the emulator yet: editing the BMP does not change the game.\n",
        encoding="utf-8")
    print(output / "rocket-reticle.bmp")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    export(args.state, args.output)
