# Jet Force Gemini HUD margins and centring

Analysis of 8 September 2026, USA retail ROM, normal single-player HUD. The
screenshots provided were cropped: the margins below come from the game's
coordinates, not from distances to the edges of the captures. No placement
was modified during the initial analysis. The option added afterwards is
described at the end of the document.

## Frame of reference and original result

The values are **logical units on a 320 x 240 image**, before rasterisation,
filtering, any cropping by the plugin and the enlargement to the screen. The
screen origin is top left. The HUD's orthographic frame is centred, with Y
upwards: `screen x = 160 + HUD x`, `screen y = 120 - HUD y`.

| Measurement | Original value |
| --- | ---: |
| Left margin of the main weapon frame | 19 |
| Top margin of the main weapon frame | 13 |
| Left margin of the health arc's vertex envelope | 24 |
| Bottom margin of that envelope | 13 |
| Placement point of the health icon | (60, 188) |
| Mathematical centre of the health arc | (61, 190) |
| Icon minus arc centre | (-1, -2) |

The top and bottom margins are therefore identical in this geometry. The two
elements do not share the same left edge: the arc starts **5 units further
right**. The frame's left margin exceeds its top margin by **6 units**.

The icon's placement point is **1 unit left of and 2 above** the arc's centre.
This offset is written into the original code. It must not be confused with
the apparent centre of the green orb or the centre of all the sprite's opaque
pixels, whose drawing is asymmetric.

These integer differences compare the **placement coordinates before
projection**. Sprite rendering adds the W of their local matrix to the W of
their anchor; the final on-screen difference is therefore not exactly
`(-1,-2)`. The correction described below also accounts for that homogeneous
division.

## Evidence in the game

### Icon placement point

`setupFrontEndObject`, at `0x8005A048`, copies 32-byte records from the table
at `0x800A51DC` to the temporary objects at `0x800FF780`. Object 2, drawn by
`frontDrawObj(2)`, has its original definition at `0x800A521C` (offset
`0xA5E1C` in the ROM normalised to big-endian order).

The floats at `+0x08`, `+0x0C`, `+0x10` and `+0x14` are `1`, `-100`, `-68`,
`0` respectively: scale, X, Y and Z. The screen point is therefore
`(160 - 100, 120 + 68) = (60, 188)`.

### Arc centre and vertices

In `instDrawHealth`, overlay 6, the instructions at `+0x3C8` to `+0x3F4` read
this object's coordinates and call `matrixTranslate` with `X + 1` and `Y - 2`.
The arc's centre is therefore `(-99, -70)` in the HUD frame, i.e. `(61, 190)`
in the screen frame.

The vertex construction, from overlay 6 `+0xC74`, uses the theoretical radii
**38 outer** and **20 inner** in single player. It steps by angular increments
of `0x1555` (about 30 degrees) and truncates the coordinates towards zero. The
generated outer vertices, checked in a state's memory, have local extrema
`X = -37..37`, `Y = -37..38`.

The envelope of these vertices is therefore `x = 24..98`, `y = 152..227` on
screen. The sectors actually visible depend on the gauge's state; the
captures contain the sectors that reach the left and bottom extrema. The
geometric bottom margin is `240 - 227 = 13`.

An ideal circle of radius 38 would give margins of 23 on the left and 12 at
the bottom. It is the **game's truncated vertices**, not this circular
approximation, that give the margins of 24 and 13 above. The mathematical
centre stays (61,190), even if the centre of a bounding box differs slightly
or the arc is incomplete.

### Main weapon frame

Overlay 14 loads the translation `(-141, +107)` at offsets `+0x264C` to
`+0x2664`. Its 20-vertex array at `+0x3E80` has local extrema `X = 0..59`,
`Y = -59..0`. The screen translation of the top-left corner is therefore
`(160 - 141, 120 - 107) = (19, 13)`; the envelope is `x = 19..78`,
`y = 13..72`.

This measurement covers the main frame, not every protrusion, animated
sprite, filtered glow or weapon selector element.

## Comparison of display formats

In low resolution, the 4:3 and widescreen modes both use a 320 x 240 buffer
for these HUD coordinates. The game's video table distinguishes the
widescreen treatment of the scene. Presenting the original HUD on a 16:9
surface stretches its horizontal distances relative to the vertical ones.

For these left-anchored elements, the current patch applies
`corrected x = 0.75 x original x + 4`, in buffer coordinates; Y is preserved.
It thus gives a left margin of **18.25** for the frame and **22** for the arc.
These numbers must not be compared directly with the vertical margins: the
buffer's pixels are then presented on a 16:9 image.

At an **equal image height of 1080 pixels**, without cropping, this gives:

| Measurement in display pixels, before filtering | Original 4:3, 1440 x 1080 | Original 16:9, 1920 x 1080 | 16:9 with correction, 1920 x 1080 |
| --- | ---: | ---: | ---: |
| Frame top margin | 58.5 | 58.5 | 58.5 |
| Frame left margin | 85.5 | 114 | 109.5 |
| Arc left margin | 108 | 144 | 132 |
| Arc bottom margin | 58.5 | 58.5 | 58.5 |
| Horizontal offset icon / arc centre | -4.5 | -6 | -4.5 |
| Vertical offset icon / arc centre | -9 | -9 | -9 |

The icon's relative offset therefore already exists in the original. The
current correction keeps its proportions after the 16:9 presentation. Its
anchoring choice does however add **24 pixels of left margin at 1080 height**,
compared with 4:3 at the same height, for these two elements. That global
shift is distinct from the internal placement differences of the original
HUD.

## Limits

These values describe the single-player HUD and its base geometry, not the
margins of a cropped window or multiplayer. A measurement of visible pixels
can vary with filtering, transparency, animation and rendering rounding. The
high resolution was not compared completely here.

The visual check of the segment centres in the captures agrees with a slight
placement of the whole icon above the arc. It does not replace the ROM
coordinates: in particular, the green orb alone is higher than the centre of
the whole drawing.

## The "Align HUD elements" option

The independent option of the *Game-specific hacks* dialog targets the
single-player HUD of the USA retail ROM (and of the PAL one, whose four modes
draw into the same framebuffers), in modes 0/1/2/3. It works with or
without *Correct widescreen HUD*; only that second option corrects the
proportions. The choice made is **the same left margin of 13** for the frame
and the arc, rather than an alignment of their horizontal centres. Their
dimensions stay different. The margin is expressed in the 320 x 240 logical
frame, at equal display height and with the 4:3 or 16:9 presentation matching
the game.

| Target in the framebuffer | Mode 0 (320 x 240, 4:3) | Mode 1 (320 x 240, WS) | Mode 2 (448 x 336, 4:3) | Mode 3 (448 x 336, WS) |
| --- | ---: | ---: | ---: | ---: |
| Frame left, VI border compensated in widescreen | 13 | 13.75 | 18.25 | 19.25 |
| Geometric left of the arc, compensations included | 13 | 12.25 | 18.25 | 17.25 |
| Frame top / arc bottom | 13 | 13 | 18.25 | 18.25 |

The high-resolution targets are rounded to the framebuffer's quarter pixel,
to use the 10.2 grid of the RDP rectangles. For the base target of 13 the
error is below 0.1 logical unit. The VI compresses the whole buffer in
widescreen: 30 or 42 lines must not be subtracted arbitrarily from the HUD
coordinates.

In mode 0, the frame and its elements receive a screen translation of **-6**
in X, the arc **-11**, with no vertical movement of these two elements. In
mode 1 with the proportion correction, the moves are **-4.5** and **-9.75**
framebuffer pixels, visual and VI compensations included. The other modes are
computed from the same ROM coordinates, the buffer and the 48/68 widescreen
biases.

The validation captures show that the arc's visible edge still sits slightly
back despite the equal geometric margins. At the user's request, a visual
compensation of **-2 logical units** is added to the arc + icon group, only in
the widescreen modes 1/3. Converted to the buffer and rounded to the quarter
pixel, it is **-1.5** in low resolution and **-2** in high resolution. The
arc's geometric margin, measured after the VI's masked border, is therefore
about 11 units, bringing its apparent edge closer to the frame's. This
empirical adjustment keeps the centring, the height and the 4:3 placement; it
belongs to *Align HUD elements* and does not depend on the proportion
correction checkbox.

### VI border and visible margins - 9 September 2026

The capture after the arc compensation shows left edges that are now aligned,
but a left margin of about **23 visible pixels**, against **38 pixels at the
top and bottom**. The previous computation started from the buffer's edge,
without accounting for the black border produced by the video circuit.

In `external/parallel-rdp/parallel-rdp/video_interface.cpp`, `analyze_line`
sets `h_start_clamp = h_start + 8` and `h_end_clamp = h_end - 7` for the
normal mode; `vi_scale.frag` masks the pixels outside that range. This border
exists even when `OverscanCrop=0`. The HUD viewport, checked in the ROM and in
a state, stays exactly centred: no -4 offset is added there.

In low resolution, the eight masked VI samples correspond to **4 buffer
pixels**. On this capture at a horizontal factor of 4, the planned margin of
`9.75` therefore becomes `(9.75 - 4) x 4 = 23` visible pixels. The correction
adds **+4 framebuffer pixels** to the whole weapon group and the whole health
display, only in widescreen: the expected visible margin becomes **39
pixels**. The banner, its texts, the counters and the selection follow the
frame; the health centring and the relative compensation of its arc are
preserved.

In high resolution, the game's XScale register is `(448 << 9) / 320 = 716`.
The border is therefore `8 x 716 / 1024 = 5.59375` framebuffer pixels,
rounded to **5.5** to stay on the common grid. This correction modifies
neither the video plugin nor the already approved 4:3 placement. It targets
JFG's standard VI mode with ParaLLEl-RDP; an additional crop chosen in the
plugin can still change the visible margins.

The icon's pivot is placed at the arc's mathematical centre **after
projection**. With `C = width/2`, `S = 0.75` with the widescreen correction
(otherwise 1), and `B` = 48/68 with the correction (otherwise 0), the
complementary offset relative to the arc's translation is:

- `dx = S x (C - 99 - B) / (C + 1)`;
- `dy = 70 - 68 x C / (C + 1)` in the screen frame with Y downwards.

This compensates the billboard's W: `C+1` for the icon, `C` for the arc. The
centring concerns the **sprite's pivot**, not the barycentre of the opaque
pixels or of the green orb. The texture keeps its drawing, rotation and
animations.

### Implementation and restoration

The local drawing calls choose the translation of the weapon group or of the
health display. At the last `mathMtxF2L`, the matrix positions are translated
temporarily, then their sources are restored identically. The W added by the
billboard is taken into account for the sprites. The coordinates of objects,
vertices and animations are never rewritten.

The wrapper around the `overlay14+0xC9C -> +0x292C` call translates the text
rectangles, the two ammunition counters, the gauges and the clipping areas
emitted by the weapon group. The pickup banner, its closing and the selector
follow the same move as the frame. The full-screen clipping restores are
preserved. The region statistics and the 3D shapes are outside this
perimeter.

The stubs occupy `[0x800679A0,0x800680A0)`, separate from the widescreen
caves, with a distinct scope byte at `0x80102552`. This range belongs to the
register drawing of the game's crash screen: its entry `0x80067994` returns
immediately while the option is active. **The display of those registers on
the crash screen is therefore unavailable while the option is active**; the
exception log and the game's initialisation are preserved. The 448 original
words and the entry are fully restored after every call has been removed,
including when adopting an already patched state.

The tests run the MIPS stubs and the real C++ installer: geometry, argument
forwarding, nested scope, matrix restoration, rectangles/clipping,
independent checkboxes, resolutions, states, relocated overlays and refusal
of foreign signatures. The user validated the base rendering in 4:3 and 16:9,
then a pickup banner in 16:9 (text and closing joined to the frame). The
relative compensation of the arc then brought the left edges closer; the new
common compensation of the VI border, the selector in motion and the high
resolution are still to be compared in game; these tests do not prove the
legibility of every filtered pixel.
