# JFG native HUD overlay experiment

Health, weapon models/icons, ammo and bitmap fonts render into isolated native RDP
images and are composited into the VI's temporary framebuffer input, before
AA, dither reconstruction, divot, scaling and gamma. The final window stretch
then presents the filtered scene and HUD together. Reticles also enter this
pre-VI composition; they are no longer drawn over the filtered output. The HUD's geometry compensates for VI scaling
and the display aspect ratio to preserve its corrected proportions.

## Activation and scope

- Retail US JFG (`8A6009B6-94ACE150`), existing widescreen HUD adaptation enabled.
  Health/weapon capture remains single player, game resolution mode 1 or 3.
- ParaLLEl-RDP at any internal resolution, including 1x. `ForceWidescreen`
  is not an activation requirement.
- Overlay 6 `instDrawHealth` and overlay 14's weapon/ammo group at `+0x292C`.
- The shared bitmap font renderer at `0x8006FD98` covers titles, messages and
  dialogs using `fontPrintXY`/`fontPrintWindowXY`, including window clipping.
  The separate small line-font path is unchanged. Font-only capture also covers
  title/options/save selection, multiplayer setup/pause and level-entry captions.
- Text activation requires the completed `frontInitMode` flag (`0x800A51A0`),
  a valid front mode (`0x800A51B0`, 2..24) and the signature of the live shared
  menu-frame renderer (overlay 12, `+0xA6C/+0xA70`). Cold boot remains excluded.
  Both gameplay and menus require active widescreen video mode 1 or 3.
  The enabled adaptation option alone never authorizes changes in 4:3 modes
  0/2. The plugin repeats this aspect check before beginning a font scope, so
  old states with font hooks cannot intercept 4:3 text before core restoration.
  Fonts do not depend on player count or joy/camera state.
  Font-only installation works when overlay 14 is unloaded or not yet ready.
  Health/weapon hooks are removed from resident modules; menu artwork and
  icons outside the font renderer continue through the ordinary scene path.
- The title logo uses the game's existing `rcpTileWriteX` path at overlay 63
  `+0x9A4`: scale X=.75, Y=1 around the original (160,Y) anchor, preserving its
  fade. A signature-checked branch at `+0x970` selects it in widescreen mode.
  No background/character or selection
  icon geometry is changed. The ordinary guest RDP/VI filters process the logo.
  Disabling the option restores the branch in the currently loaded allocation;
  reloaded modules and saved patches are recognized without cached addresses.
- Other graphics plugins execute the original font/HUD drawing routines. The
  logo's guest-side correction also works with them.

Restart the emulator to load the rebuilt executable and DLL, then load a game
or an existing state. `Logs/Project64-ParallelRDP.log` includes
`HUD overlay health/weapon/text/primitives` and `GPU+readback` timing in its
performance lines. Nonzero counts confirm interception of the intended draw
scopes. Turning off the existing widescreen HUD option restores game drawing.

## Rendering

The optional `JfgHudCommand` bridge appends standard RDP PipeSync commands
with signatures in their unused second words. They pass through the RSP in
draw order, so CPU timing does not classify asynchronous GPU primitives.

Two private native ParaLLEl-RDP processors mirror state and texture loads.
They record commands synchronously on the plugin's calling thread after the
scene processor has completed. This is a per-instance processor flag: the
scene keeps its normal asynchronous worker. Creation and teardown also wait
for scene recording. All processors share a Vulkan Device, and ParaLLEl's
command workers register the same Granite thread index (zero); running both
private workers concurrently races on command pools and descriptor allocation.
GPU execution of the two recorded replays may still overlap.
Only health/weapon primitives and recognized font glyphs render there; those primitives are
omitted from the main scene processor. Each group has a private color and
depth target. All normal state changes and hardware completion commands
still reach the scene processor. Foreign targets and unsupported formats
retain their original scene drawing.

The private processors undo the existing 3/4 horizontal HUD transform before
rasterization, including triangle edges, X attribute derivatives, texture
rectangles and scissors. Rectangle texture steps are rounded back to their
ten-bit fractional precision: `1365 * 3/4` becomes `1024`, avoiding a repeated
first texel with point sampling. The primary renderer never enables this
coordinate quirk. Game HUD positioning remains the anchor for each group;
the local pixel size comes from the vertical VI scale on both axes.

Bitmap fonts use a third private color/depth image as a packed glyph atlas.
The exact integer Y expansion `H + 2*floor(5*H/32)` is inverted and the texture
step is restored to 1:1 before drawing. Pause-menu glyphs already have native
Y bounds and a 1:1 texture step, so they enter the atlas directly. Each glyph retains its own X anchor,
vertical centre and clipping window; it is composited at 4/3 of the HUD pixel
size to retain the existing font size. This avoids scaling distances between
text blocks or line baselines. Glyphs and shadows retain submission order.
Unknown rectangle shapes/steps, non-glyph primitives and atlas overflow fall
back to ordinary scene rendering. Fonts do not add GPU processors or per-glyph
readbacks: the atlas is included in the same two native replays.

The font bridge receives the renderer's actual `Gfx**` argument rather than
assuming the global HUD cursor. Its entry/exit hooks occupy previously unused
space in the same diagnostic cave. The preceding HUD-only cave image can be
adopted and upgraded or removed safely when loading an older state.

The two images use black and white backgrounds. Their difference provides
per-channel transmission; composition uses `black + (white - black) * scene`.
This reproduces ordinary source-alpha blending within native RGB555
quantization, without copying the scene into the HUD. Arbitrary nonlinear
or saturating blend modes are not guaranteed to reconstruct exactly. This
experiment now applies VI gamma after composition, together with the scene.

At SyncFull the scene completes, guest texture RAM is mirrored, private
color/depth/coverage buffers are reset and the captured batch is replayed.
The cropped results are associated with the original framebuffer address.
Each scanout uploads its own immutable composition plane. GPU command buffers
retain that upload until completion; later CPU frames cannot overwrite it.
When presentation holds a previous frame, that image already includes its HUD. Scene draws without HUD publish an
empty layer so old icons disappear. Reset and state load clear host state.

This experiment adds two native GPU replays, RAM copies and readbacks per
completion point while the private processors are active. They remain active
until reset or renderer teardown, even if a later frame has no HUD scopes.
In-game visual quality and performance still require user validation.

## Composition before the VI

`before_vi` rasterizes the affine layers at the scene's internal scale (1x,
2x, 4x or 8x). Horizontal source-pixel size is derived from both VI increments,
the overscan crop and the final 4:3/16:9 display ratio; vertical size stays native
(or 4/3 for the established font size). Anchors and font scissors stay in guest
framebuffer coordinates. This inverse mapping compensates for the stretch which
will happen after filtering. At 1x, fractional positions necessarily quantize
to the native framebuffer grid; it cannot retain every separate source column.

`ScanoutOptions::overlay` uploads packed RGB color/transmission pairs.
`extract_vram.comp` combines them with the scene while producing the transient
VI input, before any VI filters run. RGBA5551 results are quantized back to five
bits per channel. Guest RDRAM and its upscaled counterpart are never modified;
repeated scanouts cannot accumulate the blend. Origin/width/scale checks prevent
applying a plane to another framebuffer. Without an overlay the ordinary VI
path remains active. There is no additional post-VI HUD composition.

Solid screen fades retain their RDP submission order relative to captured
glyphs. Capture recognizes the retail `drawClearScreen` rectangle state used
by `0x8006C124` and `0x8006CFC0`: primitive RGBA, one cycle, render mode
`0x00504340`. It records the color, framebuffer, scissor-clipped rectangle and
draw order; the original fade continues to render over the scene. Each glyph
has its own order, so text drawn after a fade is not affected by that fade.
Health/weapon groups use their last primitive's order.

For an overlay `H(S)=C+T*S` and a later fade `F(S)=A+B*S`, composition over the
already faded scene uses `C'=B*C+(1-T)*A`, with transmission `T` unchanged.
This produces `F(H(S))` rather than fading the background twice. The source
alpha uses the RDP's five-bit blend weight and opaque bypass. Multiple fades
are applied in submission order, only within their framebuffer and rectangle.
The resulting plane still goes through the VI once. No additional GPU replay
or synchronization is added. RGB555 quantization remains approximate, as for
the existing affine extraction.

This handles solid rectangular fades, including black, white and colored
transitions. It is not a general reordering of arbitrary scene primitives:
textured/noisy effects, triangle wipes, depth-tested blends and effects
interleaved within one health/weapon group are not reconstructed by this path.
`JfgHudFadeTest.h` checks capture/order, clipping, target isolation, frame reset,
unsupported-state rejection and real RDP blend references with translucent HUD
pixels. Its accepted RGB error is at most one RGB555 step plus rounding.

### Menu fades and level-selection map text

The map's scrolling labels require actual scene ordering: overlay 12 draws its
textured frame after overlay 9 `frontMap`, including the nonrectangular corners.
The pause menu can also invoke `frontMap` while front mode remains 16 (verified
with the supplied level-selection snapshot). Mode 17 alone is insufficient.
All font notifications outside a private health/weapon scope now emit scene
markers 7/8; nested HUD fonts retain atlas markers 5/6. The exit balances the
chosen marker even if CPU mode changes. RDP replay never reads the current CPU
menu state to classify an older draw.

Inside this scope, recognized glyph rectangles remain in the primary scene
command batch. Their bounds and texture increments are corrected in place,
using the same VI/display mapping, X anchor and vertical centre as `before_vi`.
The scene RDP samples the original font texture and performs normal blending;
later frame artwork, triangles and transitions naturally cover the text.
The glyphs never enter the final overlay atlas. The normal VI filters still run
once on the finished scene. Glyph edges retain the RDP's quarter-pixel precision.
Only these corrected glyph draws temporarily disable native_resolution_tex_rect
through ordered meta commands, so the configured 2x/4x/8x rasterizer can retain
columns and rows that would otherwise disappear on the native grid. The complete
scene quirks (including native LOD) are restored immediately afterwards. These
draws also use point sampling (sample-mode bits cleared), restoring the original
other-modes word after the glyph. Thus VI handles final filtering without a
preceding interpolation of the expanded glyph. Texture steps retain the RDP's
ten-bit fractional precision. Native 1x still has its original resolution limit.
The GPU regression checks all eight source columns of a glyph compressed to six
guest pixels at 2x, half-pixel row transitions, and unchanged native rasterization
of an unmarked rectangle drawn immediately afterwards.
Top-edge expansion clips and advances T rather than wrapping screen coordinates.
Unsupported font steps or invalid VI state retain their original commands.

Menus, pause submenus, map labels and standalone captions use this path.
Both marker variants retain the widescreen-only authorization, so
4:3 commands stay untouched. `JfgMapTextTest.h` checks proportions in both
framebuffer resolutions, top-edge clipping, scope isolation and an actual RDP
sequence of corrected text / textured occluder / later text / black fade. The CPU lifecycle
tests also check marker order across menu changes and rejection in 4:3 modes.

Untouched pixels preserve the scene's RDP coverage. Reconstructed overlay
samples use full coverage, with their transparency already resolved in RGB.
The original HUD subpixel coverage cannot be recovered from the black/white
layers, so this is not bit-exact emulation of the original draw's edge coverage.
The actual VI filters, including their handling of adjacent scene samples,
nevertheless run on the combined image. The GPU tests compare this with a
framebuffer reference using the same full-coverage convention.

Hidden RDRAM allocation is one byte per framebuffer halfword (`RdramSize / 2`),
as indexed by the RDP/VI shaders. The previous `/8` allocation treated the
coverage bytes as packed bits and left higher framebuffer addresses outside
the allocation. The 16-bit VI reference test exposed this mismatch.

The additional composition plane costs eight bytes per internally scaled
framebuffer pixel and one upload per submitted scanout. This grows with the
square of internal scale; high-resolution performance still needs game testing.

## Guest hooks and validation

Fuel positioning remains an overlay-14 layout correction: its frame already
uses the matrix's left HUD anchor, but the gauge rectangles, bitmap label and
deferred digital counter originally used different coordinates. Four checked
caller instructions align all three to that frame in both widescreen modes.
The rectangle endpoints receive the same -48 / -68 world-space bias before
the existing .75 scale; the label and counter receive the corresponding screen
coordinates. Their Y positions, fuel values and rendering paths are unchanged.
These words share the pickup banner's atomic installation and restoration,
including old save states, resolution changes and returning to 4:3.
`test_jfg_fuel_hud.py` executes the caller slices and rectangle hook against
the frame's anchor; the lifecycle suite checks migration and foreign code.

The exact-signature-checked diagnostic string writer at `0x800681D0` is retired
and holds six entry/exit trampolines. Its only retail direct caller is the
already-retired `cpuXYPrintf`. Original words are retained for restoration;
health/weapon prologues used by HUD alignment remain intact. Trampolines
preserve arguments, return values, saved registers, HI/LO and FPU state,
replaying displaced instructions and the health stack restoration.
The font-only cave uses zero health/weapon addresses in its ownership footer.
It is recognized independently when loading a pause save state; resuming play
restores the usual HUD installation once the game reloads overlay 14.

The cave is restored before state saving. Recognizable patched RAM snapshots
can be adopted without prior host ownership. Overlay relocation follows the
module table; stale allocations are not rewritten. Foreign instructions
prevent patch installation/removal.

- `test_jfg_hud_raster.py`: actual patch lifecycle, snapshot adoption,
  relocation, foreign instructions, boot/front-end/gameplay guards, marker nesting,
  invalid cursors, pause entry/exit with unloaded modules, text-only ownership
  title-logo installation/restoration/relocation and execution of the generated
  MIPS trampolines. Font markers are accepted for 1/2/4 players in widescreen
  menu video modes; health/weapon markers retain their single-player guards.
  Live-menu switching and old font-only state restoration leave all original
  font bytes intact in both 4:3 modes; stale font markers are rejected there.
- `Project64JfgHudLayerTest` (explicit CMake target): headless Vulkan rendering
  of rectangles, triangle slopes, every column of a compressed texture,
  transparency against an actual gray-background RDP reference, square final
  pixels at 4:3 and 16:9 output, frame ownership, nested bindings, foreign
  targets, scope reset, native font rows/columns, independent glyph anchors
  and text clipping. `JfgHudViTest.h` additionally checks aspect compensation,
  clipping, real VI output against precomposed framebuffer references, gamma,
  origin offsets, RGBA16/32, 1x/2x, repeated scanouts without guest writes,
  asynchronous upload ownership and later empty frames. This target is
  excluded from normal builds.
- Existing HUD, widescreen and reticle regression suites.

Retail hook signatures were also checked against the local saved state
without modifying that state. Automated GPU checks do not constitute an
in-game visual validation.

The GPU test's `--stress` mode exercises 600 frames with an asynchronous scene
processor, both HUD replays, descriptor/frame-context recycling, native glyph
readback checks and two HUD processor recreations. The earlier concurrent
private-worker version reproduced exit code `0xC0000005` in the paired-replay
test. The corrected per-instance synchronous recording passes both the pixel
checks and this extended stress test.

### Multiplayer instruments

Overlay 61 uses different renderers from solo overlay 14. A separate,
signature-checked trampoline image occupies the dormant diagnostic glyph writer
`[800680B0,800681D0)`, whose sole retail caller is the diagnostic string writer
already retired by the font hook. It has its own module-ownership footer.

For 2–4 players in native wide modes 1/3, selected overlay 61 and health matrices
are converted by the stock `mathMtxF2L` and then have only their three X
coefficients multiplied by 3/4. Translation and Y/Z/W remain unchanged. Original
float matrices remain untouched, so successive radar rotations cannot accumulate
scaling. The shared sprite conversion checks its saved caller; unrelated sprites
are excluded. Radar dot offsets use the same correction around their own centre.
The RSP and normal RDP/VI pipeline still draw these instruments in their original
order. The local multiplayer snapshot verifies all redirected call signatures.

`frontPrintNum` emits markers 9/10 around multiplayer counters. The beginning
marker carries the original counter anchor in quarter framebuffer pixels, so
replay never samples a later player's coordinates. Rectangle bounds and texture
steps change together around that anchor, retaining source digits and vertical
texture flips. Both split-screen players therefore retain their own positions.
The host rechecks the active aspect and loaded module before accepting any hook.
Snapshot restoration and switching to 4:3 restore original instructions; no
multiplayer pixel planes are composited above the scene.

### Reticles before VI, including multiplayer

Presentation no longer draws reticles into a Windows DIB after scanout. Their
native CPU line rasterizer now adds color to the immutable pre-VI HUD plane,
preserving the original additive red/green operation and aim centre. The actual
VI filters process scene, HUD and reticles together. Tests compare the result
against a CPU framebuffer reference at 1x/2x, RGBA16/32 and gamma on/off.

Multiplayer owns separate capture/submit trampolines in unused space inside its
existing cave. Overlay 13's seven local reticle calls are redirected as it loads
and restored when disabled; old HUD-only cave snapshots are migrated. Each line
copies its player's bounds using the retail `frontPlayerScreenLimits` table and
live X/Y conversion scales. Those bounds survive asynchronous scanout and prevent
bleeding into another viewport. Normal 4:3 drawing and unacknowledged plugin
capture use the stock guest line renderer.

Horizontal reticle footprints integrate fractional coverage on the internal grid
instead of rounding each source column independently. This prevents thin strokes
from disappearing at 1x or alternating between one and two full pixels at 2x.
Tests check constant integrated intensity across all quarter-pixel phases at
1x/2x/4x/8x and continuous adjoining segments without bright seams.

Font atlas boundary regression: corrected rectangles align their bounds to the
configured internal pixel grid (up to the RDP's quarter-pixel precision), then
derive both texture steps from those same bounds. The initial S/T samples use
internal pixel centres, and T compensates for the RDP's floor(Y) interpolation
origin as well as top-edge clipping. Keeping the old T origin with a fractional
rectangle edge could read the next atlas row at the bottom of a glyph. A GPU
test with differently coloured guard rows reproduced this failure before the
fix and now verifies both edges. Geometry checks also cover heights 1..24,
top clipping, and all four internal resolutions. Filtering and draw order are
unchanged; the font raster scale comes from the current plugin configuration.
