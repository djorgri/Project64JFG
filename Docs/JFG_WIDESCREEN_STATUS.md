# Jet Force Gemini 16:9 prototype - status

Status as of 9 September 2026, with later additions dated inline. The
**Correct widescreen HUD** checkbox enables the prototype. The user validated
the placement and proportions of the ammunition counter, accepting a slight
loss of sharpness. The reticle segment correction was also validated visually
by the user during the 8 September trial. The 3D shapes, already correct, are
untouched. The user also validated the pickup banner showing **Gemini Capacity
Increased**. The last adjustment of the green bars and their format change are
covered by the automated tests; their visual validation is still pending.
Floyd's icon outline also receives a targeted correction of its centring and
of the slope of its diagonals, to be validated visually in game. Claude's
historical observations are kept separate from the follow-up measurements.

The [measurements of the original HUD margins and centring](./JFG_HUD_LAYOUT_MEASUREMENTS.md)
come from the ROM coordinates and the game's vertices: top and bottom margins
of 13 units, left margins of 19 for the weapon frame and 24 for the health
arc, and an icon pivot offset 1 unit left and 2 up from the arc's centre, in
the 320 x 240 logical frame.

## Enabling the prototype

1. Use the USA retail ROM supported by the project. The HUD patch explicitly
   excludes the Kiosk build, even though other adaptations handle it.
2. Select the widescreen mode in the game's own options.
3. In the **Project64 Parallel RDP** plugin settings, enable **Force 16:9
   display (stretches image)** to present the image in 16:9.
4. Under **Options -> Game-specific hacks**, tick **Correct widescreen HUD**,
   then resume a game to observe the correction.

These settings are independent: the plugin chooses the presentation aspect,
the game chooses its video mode, and the hack corrects some HUD drawing. The
HUD checkbox does not switch the game's video mode. It does not require the
keyboard/mouse controls and is **enabled by default**.

## Activation limited to the game's widescreen mode

The check uses the **active video mode** byte at `0x800FECA8`, written by
`viChangeMode`: `0` and `2` are 4:3, `1` and `3` the low- and high-resolution
widescreen modes of the USA ROM. The menu preference is not used as a
substitute for the active mode. The aspect forced in the graphics plugin plays
no part in this decision.

The installer itself checks that the mode is `1` or `3`, in addition to the
per-frame check. Returning to 4:3 removes every corrected call as well as the
banner constants. The gauges' native coordinates now stay intact while they
are drawn, so the checkbox can stay ticked while the game runs in 4:3. The
MIPS drawing routines also carry their own guard on the widescreen bit.

Loading an old state that contains the prototype's known signatures now
triggers their recovery and removal, even in 4:3 or with the box unticked.
The overlays currently loaded are checked, including when they have moved.
An invalid reticle signature no longer prevents the independent cleanup of the
banner and gauges; the code caves stay available for as long as a recognised
call could still lead into them. The forced-scope diagnostic is no longer
re-armed during a removal. The four old digit corrections, abandoned while the
ammunition counter was being tracked down, are also removed when they remain
alone in a state, without a recognised HUD installation.

The tests run the real C++ methods against a simulated memory to verify these
transitions and loads. The MIPS tests also compare the routines' output to the
original behaviour in modes `0` and `2`, even with an active HUD scope. They do
not replace a visual check of the 4:3 <-> 16:9 switch in the emulator.

## Existing architecture

The work lives mostly in
[`JetForceGemini.cpp`](../Source/Project64-core/N64System/GameHacks/JetForceGemini.cpp),
in the `WidescreenHud*` constants, `ProcessRuntimeFrame()` and
`PatchWidescreenHud()`.

The patch expects a resolution index of `1` or `3`, a valid player and camera,
`DisableJoy == 0` and the recognised signature of overlay 14. It installs MIPS
instructions in a diagnostic area that goes unused during play, from
`0x80067280` to `0x80067690` exclusive. This late installation avoids
overwriting code that is still executing at boot. The counter's extension
occupies the start of `diCpuReportWatchpoint`, a partly unused fault
diagnostic, and stops before `diCpuLogMessage` at `0x800676B4`. The reticle,
the gauges and Floyd use a second, separate segment from `0x80067790` to
`0x80067950` exclusive, in the old diagnostic log display. The reticle code
still ends at `0x800677F4`; the gauge wrapper starts at that address and its
anchoring helper at `0x80067810`. Floyd's wrapper starts at `0x80067844`. The
memory error reporting function at `0x800678C4` is neutralised before its body
is reused; all of it is restored on removal, including after adopting an old
state that carries this fix. `diCpuTraceGetFault` at `0x80067950` stays
intact. The only caller of the old display, at `0x800674BC`, is already
replaced by the HUD cave. The memory image concatenates the two segments: the
code between them, `diCpuLogMessage` in particular, is neither read nor
written by the cave installer.

Most corrections apply between the entry and the exit of the
`frontSingleInstruments` drawing, at offsets `+0xC00` and `+0x10D0` of overlay
14. A byte at `0x80102553` says whether that part of the HUD is being drawn.
The reticle is drawn before the entry: its segments now have seven distinct
corrected calls in overlay 13. The textured fonts only consult the video mode
once their hooks are installed.

The correction prototype places nineteen stubs: two to open and close the
scope, ten for HUD rendering, one for the reticle segments and two to identify
and anchor the gauge drawing, plus a wrapper and three helpers for Floyd. All
ten HUD corrections check the widescreen bit; eight also check the scope. The
reticle stub checks the widescreen mode and the absence of a HUD scope, to
avoid a double correction when the O diagnostic forces that scope. The two
textured font stubs are deliberately global while the widescreen mode is
active. The missing guard mentioned in the report was therefore not found in
these current paths. The shot-bar helper only runs after the mode and scope
guards of the rectangle stub.

| Element or drawing path | Correction present in the code |
| --- | --- |
| HUD orthographic matrix | Horizontal scale multiplied by 0.75. |
| Sprites and weapon icons | Two `camDo2DSprite` paths corrected in width, with left/right position compensation. |
| Frames and gauges using matrices | Anchor compensation in `matrixTranslate`. |
| Textured font | Vertical enlargement close to 21/16 and a texture step of 3/4 to preserve the thin glyph columns. |
| Lines, radar and small stroke font | X coordinates transformed at queue time, before the deferred drawing. |
| Filled rectangles | Horizontal compression before the drawing commands are emitted. |
| Six green shot-capacity bars | Weapon-frame anchor applied to the rectangles while they are drawn; native table preserved, mode and scope guards. |
| Floyd's icon outline | Same right anchor as the icon; diagonals adapted to the 16:9 ratio while keeping the native two-pixel green stroke. |
| Ammunition counter digits | `frontPrintNum`: horizontal compression of the glyphs and their spacing, anchoring on the left panel and adapted texture step; first rendering validated in the interpreter. |
| Pickup banner | Cap attached to the left for the whole animation, text centred at full opening and clipping aligned with the banner; full display validated visually by the user. |
| Reticle segments | Horizontal compression around the aimed point, before clipping and CPU drawing; symmetric rounding and 3D shapes intact. Rendering validated by the user during the 8 September trial. |

The installer checks the original instructions and the bounds of the stub
area. It also handles hook removal, overlay changes and some patches
inherited from old states.

## Green shot-capacity bars

The old patch subtracted 45 from the X coordinates of the seven rectangles of
overlay 14: six bars and their background. Their low-resolution anchor stayed
about 2.25 pixels to the right of the frame's. If the widescreen mode or the
HUD scope was switched off before the emulator-side removal, the renderer
still read these shifted coordinates with its 4:3 behaviour. This leftward
shift is reproduced by the transition tests.

The `frontDrawRectangles` call at `overlay14 +0x2B28` now goes through a
dedicated wrapper. Its fixed return address lets the rectangle stub recognise
the gauges without modifying their table or the other rectangle calls. After
the mode and scope guards, the anchor becomes `X' = floor(0.75 x X + 4)` in
low resolution and `X' = floor(0.75 x X + 53)` in high resolution. These
formulas follow the weapon frame, whose origin is expressed in the matrix
frame, while the gauge table keeps its 320-pixel reference coordinates. The
optional **Align HUD elements** translation is then applied to the whole.

Old states are migrated by restoring only the X values recognised as native
or shifted by -45, including a mix of the two variants. The game's Y values
and colours are preserved. An unknown X prevents any migration of the table;
the installer does not replace these data arbitrarily.

The tests run the real MIPS stubs for the seven rectangles, the two widescreen
resolutions and the mode/scope changes before the hooks are removed. They also
check the other rectangles, the wrapper's arguments and the absence of writes
to the table. The C++ tests cover installation, removal and migration. On
9 September the 67 JFG tests passed and the x64 Release build completed
without errors or warnings. The visual comparison of this last adjustment,
notably across 4:3 <-> 16:9 switches, is still to be done.

## Floyd's icon outline

The `floyd widescreen.pj.zip` state confirms that the filled icon uses
`frontDrawObj(12)`, already corrected by the sprite path. Its outline goes
through a separate `fxDrawLine` call, in overlay 14 at `+0x468`, with a table
of 22 segments at `+0x4678`. The general compression moved its centre to
X=244, while the corrected sprite's pivot is close to X=281.

The wrapper re-anchors the converted centre stored on the game's stack before
the lines are compressed. The outline ends up at `(280,192)` in low resolution
and `(359,240)` in high resolution, less than a pixel from the sprite's real
pivot. The filled icon and the segment table are not modified. The wrapper
requires modes 1/3 and an active HUD scope; 4:3 follows the original path.

Style 13 of `fxOutputLines` normally draws a two-pixel column per line, with a
fixed X step of -1, 0 or +1. The diagonals therefore stayed at 45 degrees
despite the compression of their endpoints. Only the Floyd segments marked at
queue time now use an integer accumulator that spreads the X moves across the
Y lines. The rendering keeps the two green pixels, their saturated addition
and the inclusion of both endpoints. The other strokes keep their original
behaviour. The queue receives no additional segment.

The tests run the queueing and rasterisation instructions, check the centres
in both resolutions and the pixels of the diagonals. The 88 JFG tests pass;
the x64 Release build of 9 September completes without errors or warnings.
The visual comparison in the emulator is still to be done.

## Reticle segments

The user confirmed the correct visual result after trying the corrected
version with the `reticle.pj.zip` state. That validation does not yet
explicitly cover every weapon or the high resolution.

`frontPlayerTarget` projects the aimed point, then `frontDrawTarget` (overlay
13, offset `+0x4A8`) draws the reticles. The ordinary segments go through
`fxDrawLineInWindow` before being queued and drawn by the CPU into the
framebuffer. They were outside the HUD scope and therefore kept their 4:3
width on a 16:9 display.

Seven calls of segment types 0 to 4 now go through the new stub, which
transforms each endpoint: `X' = centreX + round(0.75 x (X - centreX))`. Half
integers round away from zero so the sides stay symmetric. The projected
centre is preserved even when the aim is off-centre. The correction happens
after the rotations/mirrors and before clipping; the line engine keeps its
native stroke. Y coordinates, colours, stack arguments and the
collision/aiming computations are unchanged.

Since 16 September, at the user's request, the rocket launcher (Tri-Rocket
Launcher, index 5) and the homing missiles (index 1) are excluded from the
compression. The filter reads the index saved by `frontDrawTarget` at
`sp+0x50` on every call: it follows weapon changes immediately and keeps the
original coordinates of these two reticles. The other reticles keep their
current correction. The filter occupies the 20 free bytes at `0x8006738C`,
without moving the other stubs.

Next trial of 16 September: the two exclusions now offer their native
segments to a presentation layer of Parallel-RDP. In single player, with
**Correct widescreen HUD** and a 16:9 game mode, the plugin accepts the
strokes and the game no longer writes them into its framebuffer. Without that
support the original path is still used. The centre comes from `s4/s3`, the
colours and the lock state from the game's arguments. The distance/angle
digits stay in the original rendering.

The `cpuXYPrintf` diagnostic is retired at its entry and its exact US image is
kept for restoration (`0x800682F0..0x800683D4`). Two private ISViewer writes
pass the stack packet and the end of queue to an optional plugin export. The
plugin matches the segments to the CPU queue, then to the framebuffer address
processed by `fxOutputLines`. Each asynchronous GPU readback carries its own
copy of the strokes and VI registers; holding a previous image also holds its
reticle. Loading a state empties the host copies. The strokes are composited
after the stretch, at a uniform X/Y scale, before a single copy to the window.
No replacement BMP is required.

The tests run the accepted/refused trampolines, the transitions and
restorations, compare the host raster to the native MIPS raster and check the
alternating queues, the image change and the square pixels off centre. The
x64 build of the emulator and of Parallel-RDP is verified. In-game rendering
is still to be validated by a user trial, notably with a target lock and in
high resolution. Multiplayer keeps the original reticles.

Fix after the first trial: activation no longer depends on the **Force
widescreen** video option, which was disabled in the test configuration and
systematically caused the fallback to the original stretched drawing. The
game's mode and the HUD hooks are enough, including after a state load; the
window's overall aspect stays under the existing video settings.

After the user validated both rocket reticles visually, the same path was
extended to every weapon index of `frontDrawTarget` (0 to 13). The local
segments go uncompressed to the host drawer: styles 0/2, thick strokes of
style 1 and pixel patterns 4 to 7. The latter are copied from the game's
tables at draw time, with their mirroring and colour, then kept with the
presented image. The edge-anchored sniper frames (types 5/6), the digits and
the 3D geometry keep their current treatment. States from the rocket-only
prototype are recognised and migrated. The automated comparison against the
MIPS drawer covers the segments of the 14 weapon tables, the rotations and the
inverted patterns; the visual validation of the other weapons is still to be
done in game.

The attempted correction of the style 2 diagonal drawer was withdrawn, its
rendering having been judged unsatisfactory. Its old instructions are also
restored when a state still carries that patch. Earlier notes wrongly
identified the affected reticle as the sniper's.

An editable monochrome mask of the rocket launcher reticle can be regenerated
from a user state with `Source/Script/export_jfg_reticle.py`, which replays
the original drawer's instructions: 65 x 65 pixels, centre at (32, 32), white
stroke on black, original rendering without the variable text. The BMP is a
reference for retouching, not yet a texture loaded in game. (The exported
folder that used to be committed under `Exports/` has been removed from the
repository.)

The four calls of the edge-anchored frames (types 5/6) and the small 3D shapes
stay intact. The compression must not be applied to the reticle's 3D
projection, which the user validated. The 4:3 mode and a disabled **Correct
widescreen HUD** keep the original calls.

The installer checks the active module 13, the prologue of `frontDrawTarget`,
the seven calls and the instructions that follow them. The calls are
installed after the cave and removed before its restoration. An unloaded or
moved module is forgotten without writing to its old allocation. The
signatures were verified in the user state `Save/reticle.pj.zip`.

For the visual test, load that state with the box ticked, move the aim
horizontally and vertically, then compare with the box unticked. Check the
symmetry, the aimed point, weapon changes and the high resolution. Do not use
the O diagnostic for this comparison.

The [`jfg-reticle-trace.js`](../Source/Script/jfg-reticle-trace.js) script,
run manually with the Interpreter core, compares the coordinates at the stub's
entry and at the clipping engine's. It writes at most 512 distinct records to
`JfgReticleTrace.log` next to the executable, without modifying the game's
memory or registers. The automated reticle tests cover the modes, the scope,
the off-centre aim, the rounding, the arguments, the clipping and the
per-weapon exclusions; they do not replace this observation of the rendering.

## Pickup banner

The trace of the **Gemini Capacity Increased** pickup confirms that the
banner's background goes through `matrixTranslate`, the cap through
`frontDrawObj(5)` then `camDo2DSprite`, and the two text passes through
`fontPrintXY` in overlay 14. Object 5 travels from X = -78 to 122. The old
left/centre/right classification changed its anchor during that travel, adding
up to 72 pixels of separation in low resolution from the background, which
stayed attached to the left.

The position stub recognises this object at `v1 = 0x800FF820` and keeps its
left anchor. It occupies 30 words of its 32-word slot. The high-resolution
bias selection is also corrected: +/-68 is no longer overwritten by +/-48 in
the left/right paths.

Twelve instructions of overlay 14 correct the text and its clipping without
enlarging the cave. The text keeps its glyphs and slides at the same speed as
the cap: `X = 0.75 x capX + 46` at 320 pixels, `+95` at 448 pixels. The
centred horizontal alignment places the message in the middle of the fully
open background. The one-pixel offset between the two passes and the Y
coordinates are unchanged. The width of **Gemini Capacity Increased**,
computed from the character conversion and font 2 of the USA ROM, is
136 pixels.

The clipping follows the same anchor: `[65.5 ; 0.75 x capX + 124]` in low
resolution and `[114.5 ; 0.75 x capX + 173]` in high resolution. It goes from
a zero width to 150 pixels during the opening. The original signatures of the
twelve instructions were verified in the loaded USA overlay. The installer
recognises the variants of both resolutions on a mode change or a removal; it
restores the original instructions on deactivation and does not touch an old
allocation after the overlay is unloaded. The removal also recognises the old
full version of the position stub (27 instructions), so an experimental state
does not reintroduce the old anchor while a newer installation is already
active.

The HUD instruction tests and the activation lifecycle tests run with:

```text
python -m unittest discover -s Source/Script/tests -p "test_jfg_*.py" -v
```

The lifecycle tests compile the production C++ methods with MSVC on Windows
(or an available C++17 compiler elsewhere). They are reported as skipped when
no compiler is available. No ROM is needed for this suite.

The banner tests run the MIPS replacements over 9,648 scenarios: the cap's
full travel, the left/centre/right thresholds, the scope, the video modes,
the text, the shadow and the clipping. They also check that the Y coordinates
and the FPU control state are preserved. They do not replace the in-game
comparison.

The diagnostic script
[`jfg-banner-trace.js`](../Source/Script/jfg-banner-trace.js) logs these
paths, the coordinates and the text width to `JfgBannerTrace.log` next to the
executable. Like the counter trace, it requires the Interpreter core and only
reads registers and memory. It must be started before the state is loaded.
For the visual validation, reload the same state, pick up the item and
compare with the box on and off; check the opening, the full display and the
closing of the banner.

## Observations reported by Claude

The weapon frame, the gauges, the radar, the sprites and the text responded
correctly to the corrections during its trials. The ammunition counter stayed
unchanged. The text quality was slightly below the 4:3 reference.

| Path tested | Reported observation |
| --- | --- |
| `fxDrawDigitalNumber` queue | No item of that type over about 2,400 frames; compressing its cursor had no visible effect. |
| `fontPrintXY` font renderer | Correcting its `TextureRectangle` commands changed "Battle Cruiser", but not the counter. |
| Six paths under the instrument scope | Forcing the scope open did not change the counter. |
| Thin text queue | Claude had proposed a test with a quadrupled step, without a collected visual result. The follow-up probe at `0x8006ED3C` was then never reached during the user trial. |

These results steer the follow-up in the situations observed. They do not
prove that a renderer is never used by the game in another scene or mode.

## Ammunition counter: renderer confirmed

The follow-up probe at `0x8006ED3C` was installed, but its persistent marker
was never triggered: `JfgAmmoTextProbe.log` reported **path NOT SEEN after 120
runtime checks**. The user observed no change, including while firing and
changing weapons. This measurement rules that site out for the trial
performed; it does not show that the thin text engine is never used
elsewhere. The diagnostic checkbox and its probe have been removed.

The counter actually uses `frontPrintNum` at `0x80058EF0`. This function
emits its own `TextureRectangle` commands, bypassing the `fontPrintXY`
renderer, `fxDrawDigitalNumber` and the candidate chain `fxInttostr`
(`0x8006D70C`) / `fxTinyPrint` (`0x8006D60C`). That explains why the earlier
patches had no effect, including the textured font one. The four
`WidescreenHudDigitalRetired` patches remain only to restore the instructions
of old states.

The analysis of overlay 14 identified two calls to `frontPrintNum`, and an
execution trace in the interpreter then confirmed them on a copy of the game
showing **96 / 100**, with `resolution=1` and `hudScope=1`:

| Call in overlay 14 | `a0` value | Centred coordinates `a1`, `a2` | Texture `a3` | Digits and colours |
| --- | --- | --- | --- | --- |
| `+0x2C2C`, return `+0x2C34` | 96 (`0x60`), available ammunition | -99, 74 | 8 | 3 positions, yellow `FFFF00A0`, dimmed zeroes `FFFF0040` |
| `+0x2CA8`, return `+0x2CB0` | 100 (`0x64`), capacity | -102, 61 | 9 | 3 positions, white `FFFFFFA0`, dimmed zeroes `FFFFFF40` |

In that capture the overlay base is `0x80342740`: the observed return
addresses are therefore `0x80345374` and `0x803453F0`. The base can change as
the game loads. Matching drawing commands are also present in the state's RAM:
the first line uses 11-pixel wide glyphs spaced 12 apart; the second 8-pixel
glyphs spaced 8 apart. The engine draws the digits from right to left.

### Correction added

`WidescreenHudAmmoCode` holds 32 MIPS words in the range
`0x80067610..0x80067690` exclusive. Five patches wire it into `frontPrintNum`:
a jump at `0x8005900C`, two loads at `0x800590D0` and `0x8005921C`, and two
removals of old ORIs at `0x800590F4` and `0x80059228`. Both paths are
covered: significant digits and dimmed zeroes.

The stub requires the active HUD scope, the widescreen bit, and a texture
equal to one of the pointers of items 8 or 9. It multiplies the destination
width and the spacing by 0.75; the spacings become 9 and 6 pixels
respectively. The rectangle coordinates keep their quarter-pixel precision.
The anchor follows the left panel: `x' = 0.75*x + 4` in mode 1 (320 pixels
wide), `x' = 0.75*x + 5` in mode 3 (448 pixels).

The source width kept at `stack+0x9C` still selects the atlas columns: it is
not compressed. Only the destination width, the spacing at `stack+0xA0` and
`dsdx` change. The horizontal step goes from 1024 to 1365, a fixed
approximation of 4/3. The Y coordinates, the starting UVs and the vertical
step `dtdy=-1024` are unchanged. The ordinary font keeps its existing
correction.

The unused local `stack+0x88` carries the texture step word. It is initialised
to the normal value even when the guards refuse the correction, since the two
instructions that load it are installed globally. The installer captures and
restores the cave words and verifies the instruction signatures. The cave
bound prevents encroaching on `diCpuLogMessage`.

A run with the compiled correction confirms the values prepared before the
loop that places the digits:

| Texture | X coordinates prepared in 10.2 (quarter pixel) | Source width | Corrected spacing | Texture step word |
| --- | --- | --- | --- | --- |
| 8 | Left 199, right 232 | 11 | 9 pixels | `0555FC00` |
| 9 | Left 190, right 214 | 8 | 6 pixels | `0555FC00` |

The `0555FC00` step is observed in both the significant-digit and dimmed-zero
paths. A first capture of the **96 / 100** scene, in the interpreter and in
low resolution, shows both lines compressed inside the frame. This validation
covers a single scene: legibility across digits, transitions, weapon changes,
the high resolution and the recompiler core still have to be checked.

### Reproducing the trace without modifying the game's RAM

The [`jfg-ammo-trace.js`](../Source/Script/jfg-ammo-trace.js) script uses the
debugger's `events.onexec` events. It reads registers and RAM; it patches no
instruction and modifies no counter.

1. Use the USA ROM and temporarily select the **Interpreter** core. The
   script's execution events require that core.
2. Start the script from the debugger's **Scripts** window before loading the
   game state. It can also be copied to the `Scripts` folder next to the
   executable and enabled at start-up.
3. Load the scene, watch the counter, fire and change weapons.
4. Read `JfgAmmoRendererTrace.log` next to the executable. For
   `frontPrintNum`, each line gives the caller, the value, the coordinates, the
   texture item, the colours, the resolution and the HUD scope depth.
5. Stop the script, remove its automatic start if it was configured, then
   restore the usual core for the rendering trials.

The script also traces the old candidates `fxInttostr`, `fxTinyPrint`,
`tinyRender` and `tinyAdvance`. It keeps at most **512 distinct argument
combinations**, not counting repetitions. It writes the log only at
initialisation and when a new combination is observed. Each run starts the
file over: keep a copy to compare two sessions. A missing line is only
meaningful once it has been checked that the script was active in the
interpreter during the scene concerned.

## Defects and limits noticed while reading

- **O diagnostic key:** it forces the scope counter to `0x20`. On the second
  press only the boolean is cleared; the byte is not reset to zero. The
  normal entries and exits balance out and can leave it non-zero. The "scope
  restored" message therefore does not prove a return to normal behaviour.
  For a reliable comparison, start from a fresh session without using O. This
  key requires the keyboard/mouse controls.
- **Drawing outside the scope:** the deferred paths, or those executed outside
  `frontSingleInstruments`, do not automatically benefit from the correction.
  Moreover, `DisableJoy != 0` removes the hooks: the rendering of dialogues,
  cinematics and transitions is still to be checked, even for the font
  described as global.
- **Text sampling:** the nominal destination factor `21/16` multiplied by the
  source step `3/4` gives `63/64`, about 98.44% of the source height. The
  actual computation is `H + 2 * floor(5H/32)` in 10.2 coordinates; rounding
  and rasterisation therefore affect the exact coverage. A step of `3/4`
  corresponds to a destination of `4/3`, while a destination of `21/16` would
  call for a step of `16/21`. The code comments explain the current choice by
  the stability of the interpolation pattern. The reported degradation
  concerns this compromise with the native bitmap font; it does not show that
  another font or another rendering path could not produce a better image.

These limits of the existing corrections remain documented for the follow-up.
The 48/68 selection defect of the sprites was fixed together with the banner;
its visual result in high resolution is still to be validated.

## Comparisons to perform in game

Keep the same scene, the same weapon and the same plugin settings for each
comparison. Start at 30 fps, in low resolution, without the O diagnostic.

1. Compare the 4:3 reference, the native widescreen without HUD correction,
   then the native widescreen with the correction.
2. Prioritise the two lines of the counter, including the dimmed zeroes, the
   100/99/10/9/0 transitions and the weapon changes. Check that the other HUD
   elements keep their validated rendering.
3. Repeat the comparison in high resolution to check the agreement between the
   frames, the sprites and the banner with the 68-pixel bias.
4. Test the menus, dialogues, cinematics and returns to play.
5. Check enabling/disabling, a cold start, a level change and a state load,
   then repeat at 60 fps. Watch the anchoring of the green bars during the
   4:3 <-> 16:9 switches in particular.

Compilation and instruction checks do not replace this rendering validation.
The trace establishes which renderer draws the counter; it judges neither its
proportions nor its legibility after correction.
