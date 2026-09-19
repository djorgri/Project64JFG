# Jet Force Gemini hacking reference

Developer notes for the game-specific runtime in Project64JFG. This is a
reference for extending or reviewing the emulator-side hacks; it is not a ROM
patch set and contains no game data.

## Scope and target ROM

The runtime is deliberately restricted to the **USA 1.0** release:

| Field | Value |
| --- | --- |
| Internal identifier used by the runtime | `8A6009B6-94ACE150-C:45` |
| Internal name | `JET FORCE GEMINI` |
| Cartridge ID | `NJFE` |
| Version byte | `0x00` |
| CRC1 / CRC2 | `8A6009B6` / `94ACE150` |
| ROM size | 32 MiB |

The target check is implemented by `CJetForceGeminiRuntime::IsSupportedRom()`.
Do not reuse any address in this document for a different revision without
reversing and validating that revision independently. PAL, Japanese, Kiosk and
later revisions are not supported yet.

## Where the implementation lives

| Purpose | Source |
| --- | --- |
| Runtime, patches, address map and MIPS stubs | [`Source/Project64-core/N64System/GameHacks/JetForceGemini.cpp`](../Source/Project64-core/N64System/GameHacks/JetForceGemini.cpp) |
| Runtime interface and lifecycle | [`Source/Project64-core/N64System/GameHacks/JetForceGemini.h`](../Source/Project64-core/N64System/GameHacks/JetForceGemini.h) |
| Checked RDRAM access and code patcher | [`Source/Project64-core/N64System/GameHacks/GameHackMemory.h`](../Source/Project64-core/N64System/GameHacks/GameHackMemory.h) |
| Setting identifiers | [`Source/Project64-core/Settings/SettingsID.h`](../Source/Project64-core/Settings/SettingsID.h) |
| Jet Force Gemini settings dialog | [`Source/Project64/UserInterface/GameSpecificHacks.cpp`](../Source/Project64/UserInterface/GameSpecificHacks.cpp) and [`.rc`](../Source/Project64/UserInterface/GameSpecificHacks.rc) |

The primary reverse-engineering reference is the
[Jet Force Gemini decompilation](https://github.com/Ryan-Myers/Jet-Force-Gemini).
Its US symbol map and source names are used to identify functions and globals;
the implementation still validates the live instructions before modifying them.

## Address conventions

- Addresses below are **runtime RDRAM virtual addresses** in the USA 1.0 game,
  not offsets in a ROM file.
- The main executable is loaded at `0x80000450`. For code in that main image,
  the corresponding big-endian ROM offset is:

  ```text
  rom_offset = ram_address - 0x80000450 + 0x1050
  ```

- This conversion does not apply to relocatable overlays. Resolve their live
  base address from the game's overlay table instead.
- A `.n64` image is halfword-byte-swapped. Normalize it to big-endian order
  before comparing it with disassembly or the decompilation.
- N64 words are big-endian. Always use `CGameHackMemory` rather than indexing
  the emulator memory buffer directly.

## Runtime lifecycle and safety model

`CJetForceGeminiRuntime` has three relevant paths:

1. `ProcessController()` maps the sources routed to a port, keyboard/mouse and
   up to two gamepads, to that N64 controller. Port one gets the full scheme;
   ports two to four get `MapSecondaryPort()`: the button layout, plus the
   game's own aim from the trigger (right stick on the N64 stick, left stick
   on the C buttons) since no camera hook exists for those ports. Port two
   reverses stick Y while `cooperativeGame` is set and `multiPlayerGame` is
   clear, matching Floyd's aim when the second player joins solo play, and
   reads either stick there. The flags are read on each poll, including after
   a state load. The X/Y weapon notches of these ports are edge detected on
   both the poll and the video interrupt and queued per port, see
   `QueueSecondaryScroll()`, as port one's are in `m_QueuedMouseWheel`.
2. `ProcessVideoFrame()` performs camera work and applies timing/gameplay
   patches once the level has a valid player object. It also turns the right
   stick into mouse counts, see `BankStickCamera()`.
3. `StateSaving()` and `StateLoaded()` remove hooks or discard local state when
   needed so that a save state cannot preserve an unsafe injected hook.

The design rules are important when adding a new hack:

- Gate it with `IsSupportedRom()` and, where appropriate, wait for a valid
  in-level object before touching game memory.
- Use `CGameHackMemory` for range-checked reads and writes. Validate pointer
  alignment and `IsRdramAddress()` before dereferencing game pointers.
- Express code changes as `GAME_HACK_CODE_PATCH` records. `CGameHackCodePatcher`
  accepts only the expected original or replacement word and invalidates the
  recompiler page after a write.
- Validate neighbouring instructions as a signature when a single matching word
  would be ambiguous. A mismatch means **do nothing**, not "patch anyway".
- Install injected stub words first and the live jump/call entry last. Restore
  in the reverse order: entry first, then displaced instructions, then stub.
- Treat scratch RDRAM as occupied unless a live entry proves it belongs to this
  hook. This matters after loading a state written by an older build.
- Re-test saving and loading with each enabled option. The movement hooks are
  removed before saving because a state containing an installed movement hook
  can otherwise hang before drawing.

## Address inventory

The tables in `JetForceGemini.cpp` are authoritative. This is a map of the
major entry points and data used by the current work.

| Area | Runtime address / location | Use |
| --- | --- | --- |
| Video delta | `0x800FECAC` | `gVideoDeltaTime`: 1/2/3 VI periods correspond to 60/30/20 FPS. |
| 30 FPS fallback | `0x800550E8` | Store that escalates a slow 30 FPS frame to 20 FPS. |
| 60 FPS branch | `0x800550F8` | Branch around the engine's `gVideoDeltaTime = 1` path. |
| Scheduler gate | `0x800506D8` | Graphics-task release cadence. |
| Triple buffer | `0x800551E8`, `0x800FECA6` | Third-framebuffer request and active flag. |
| Frame counter | `0x800FECB0` | Current framebuffer pointer, used by the input-rate diagnostic. |
| Generic object move | `0x80009A24` | `objMoveXYZ` hook entry for generic enemy correction and sprint dispatch. |
| Generic move stub | `0x8009FE00` | MIPS stub for the generic object-move hook. |
| Squaddie overlay table | `0x800FEAA0` | Module 3's live base; never assume a fixed overlay address. |
| Squaddie offsets | `+0xAE38` / `+0xAE68` | X/Z shared-heading helper entries inside module 3. |
| Squaddie stubs | `0x8009FD60` / `0x8009FD80` | MIPS stubs that scale X/Z velocity by 0.5. |
| Player list | `0x800F2D0C` / `0x800F2D10` | Player-object list and count. |
| Camera array / selected camera | `0x800FA4D0` / `0x800F6DC0` | Camera state and controlled camera. |
| Robot mission flag | `0x800A3208` | Detects Floyd flight, which does not change the ordinary camera mode. |
| Cutscene step | `0x800AEFE0` PAL / `0x800AEFE4` NTSC | `animseqUpdate` time step. |

### Reserved scratch area

The current MIPS stubs and their small host-to-game control words use the
`0x8009FCE0`-`0x8009FEB0` region. In particular:

- `0x8009FCE0`-`0x8009FCE8`: active player and movement/sprint flags;
- `0x8009FD00`, `0x8009FD60`, `0x8009FD80`, `0x8009FE00`: injected code;
- nearby words are also used by historical water-wake probes.

The camera uses a second, ten-word zero gap at `0x8009F228`-`0x8009F24C` (US;
`0x8009FAB8` in Kiosk): the per-player native camera height at `F228`-`F234`,
the per-player free-orbit height offset at `F238`-`F244`, the retired single
height offset word at `F248`, and the top-down camera counter at `F24C`. An
older prototype kept a player-object pointer at `F240`, so the tables are
zeroed whenever the camera state is cleared.

Do not add another stub in this area casually. Check every occupied word,
avoid overlap, and assume save states may retain old contents. Prefer a new,
verified free region if the existing layout cannot accommodate a hook.

## Implemented feature groups

### Keyboard, mouse and gamepad controller replacement

`CControl_Plugin` reads the keyboard/mouse snapshot and up to two gamepads from
the input plugin once per controller sweep (`RefreshJfgInput()`), then routes
each source to the port chosen by `Setting_JfgKeyboardMousePort`,
`Setting_JfgGamepad1Port` and `Setting_JfgGamepad2Port` as a `JFG_PORT_INPUT`.
A gamepad only counts while `Setting_JfgGamepad1`/`2` is on and the pad is
attached, so a port routed to an absent pad falls back to the plugin. A port
with at least one source makes `UsesExclusiveInput()` true: its plugin input is
suppressed, and `ApplyJfgPortPresence()` reports it to the PIF as a plugged-in
standard controller for as long as a source feeds it.

`ReadControls()` merges the sources of a port into `JFG_CONTROLS`: keys give
full stick deflection, a stick gives its analogue value and also reads as the
matching keys past `GamepadDigitalThreshold`, and the axis pushed furthest
wins. `MapController()` starts from a neutral `BUTTONS` value and generates the
complete N64 state from those controls. It supports both WASD and ZQSD
physical/printed layouts, context-sensitive camera modes, mouse and right stick
aiming, Floyd handling, Q/D posture behaviour, Floyd lateral flight on
C-left/C-right, and wheel or X/Y impulses for A/B weapon cycling. The game
keeps two button layouts, selected per player by the byte at `0x800FF38D + n`
(US) through a mask table at `0x800A18B4` / `0x800A18D8`; the saved games seen
so far use the second, where C-up/C-down jump and crouch and the N64 A/B cycle
weapons on their press edge. Both the keyboard scheme (Space/Ctrl) and the
gamepad scheme (pad A/B on C-up/C-down, X/Y as the weapon notches, LB/RB on
C-left/C-right) are written for that layout. With
`Setting_JfgGamepadStockAim`, an aim held from the trigger alone (`StockAim`
in `MapController()`) hands the manual aim back to the game: `SetCameraCode()`
restores the reticle cursor windows, the angle helper calls and the overlay
`BoyAimPatches` to their original words, `ApplyManualAimMouse()` is skipped,
and the right stick is written straight to the N64 stick so
`controlGetManualAim` places the reticle and turns the view itself.

`controlGetManualAim` is shared by every player, and its two reticle cursor
stores used to be replaced by `sh $zero` outright, which also took the reticle
away from a second controller in split screen and in co-op. They are now
gated per player: the eleven words of each store's dead division guard
(`ManualAimCursorXStore - 0x28` onwards, the divisor being the constant 0x28)
are rewritten in place to test the player index byte at `PlayerData + 0`,
store nothing for player one and the game's own value for anyone else. See
`ManualAimCursorPatches` / `FillManualAimCursorPatches()` for the listing and
the register argument; the guard's `bne` and the store come from the address
table since the two builds allocate them differently. The velocity stores of
the same function are no longer redirected: their target is zero whenever
player one's stick is at rest and the runtime zeroes them every video frame,
so the redirect only cost the other players their smoothing. Their table
entries remain so a state saved with the old word is restored. The right
stick otherwise joins the mouse bank through `BankStickCamera()`, at
`GamepadCameraCountsPerSpeed` mouse counts per video frame per step of
`Setting_JfgGamepadCameraSpeed`, so every camera path reads it as mouse travel.
The sprint, the drone work and the mouse aim are bound to the first player's
objects, so only port one runs them. The free orbit camera is per player:
`ORBIT_CAMERA_STATE m_Orbit[4]` holds each player's tracked camera, orbit yaw,
height and banked stick counts, `EvaluateOrbitCamera()` / `ApplyOrbitCamera()`
are the state checks and writes that `ApplyMouseCamera()` used to do for
player one alone, and `ProcessSecondaryVideoFrame()` runs them for ports two
to four from their right stick after port one's pass each video frame. A
player's object is found by its index byte (`PlayerData + 0`) in the player
list and its camera is the array slot of that index, `controlcam` being only
the camera the game processed last. The camera code is one copy for every
player: its free orbit words go in while any driven player wants them
(`FreeOrbitWanted`, OR-ed in `SetCameraCode()`'s caller), the mode-gated
helpers already tell the players apart, and the camera height is per player:
the height blend at `CameraHeightBlendBase` is rewritten over 31 words, up to
the position copies that followed it, to index `CameraHeightTableAddress` and
`CameraNativeYTableAddress` (`CameraNativeYAddress - 0x0C` and `- 0x1C`, four
words each in the same zero gap) by the player index in `$s0`. Every pass
writes its player's height word, zero while its orbit is inactive, since the
blend adds whatever it finds there. What remains shared is the trio of words
at `CameraLookHelperCall + 0x0C/+0x38` and `CameraYawHelperCall + 0x14`, whose
"original" values are a tuned variant (no look-ahead, unsmoothed yaw) for the
states where nobody orbits: while one player orbits, another who is aiming
gets the game's stock look-ahead and smoothing instead.

The gamepad itself comes from the `GetGamepadState` plugin extension in
`Project64-plugin-spec/Input.h`. The Project64 input plugin fills it from
`SDL_GameController`, so any pad SDL has a mapping for presents the same Xbox
style layout.

When `Setting_JfgDroneLateralMovement` is enabled during a robot mission, Q/D
request lateral thrust and Jump/Crouch request world-up/world-down thrust.
These use Space/Ctrl on the keyboard and A/B on the gamepad. Opposite inputs
cancel thrust on their axis. Neither axis requests the native A/B throttle.
Outside a robot mission, Jump/Crouch retain their normal mapping.

Each added axis keeps its own velocity and advances once per game update:
`velocity = velocity * 0.975 + signed_thrust * frame_step`, with thrust `0.15`
and a scalar cap of `8.0`. Releasing a button leaves the hook active so drag
slows the drift. The same frame step as lateral movement is used (normally
1 at 60 fps and 2 at 30 fps). The final XYZ velocity, including native flight,
is limited to magnitude `8.0` before the game's position integration.

Floyd never moves through `objMoveXYZ`. `sidekickControl` integrates his flight
inline at `0x8003001C`-`0x800300A8`, where the sidekick structure at
`object+0x68` holds a unit heading at `+0x28/+0x2C/+0x30` and a speed scalar at
`+0x34`:

```
object+0x1C/0x20/0x24 = sidekick+0x28/0x2C/0x30 * sidekick+0x34
sidekick+0x00/0x04/0x08 += object+0x1C/0x20/0x24 * frame step
```

`PatchSidekickStrafe()` hooks `0x80030060`, the first instruction after the
three velocity stores and the point every branch into the block reaches. Like
every other hook here it moves the displaced instruction into the jump delay
slot and resumes at entry + 8, replaying the instruction it overwrote.
Resuming at entry + 4 instead jumps into the delay slot of the installed jump,
which leaves the recompiler building a block that starts on a delay slot; that
hangs the emulator on the next state load rather than failing visibly. The stubs
add thrust velocity to `object+0x1C/0x20/0x24`; the heading at
`sidekick+0x28/0x2C/0x30` is persistent state that the next frame smooths and
renormalises, so rotating it in place would accumulate 90 degrees per frame.
Leaving it untouched is also what keeps the drone and the camera facing forward
while strafing. `$at` carries `0x800B0000` across the block for the constant
read at `0x800300B0`, so the stub keeps its scratch base in `$t9`.

The lateral half uses `0x80066C00`, then jumps to the vertical half at
`0x80066F00`. Both are installed/restored together and fit their existing
retired-hook caves (`0xC0` and `0xC4` bytes). The vertical scratch words at
`0x8009FCD0/FCD4` replace position deltas of retired hooks; they hold thrust
and velocity. The installer relocates both stubs and their scratch references
for the selected address table. Save/load, deactivation, leaving a mission
and disabling the option clear the added velocity.

The vertical direction uses world Y and does not depend on the camera. The
pre-existing lateral orientation still reads `PlayerObject+0x1040`; that is
not a stable reference to Floyd across object allocations and remains a
separate known defect. The native collision response still works from the
engine's heading/speed, so obstacle behaviour needs in-game validation.

`sidekickpadMovePlayer` (overlay 22, `overlay+0x2F38`) looks like the mover but
is not: it searches the object list for id `0x5D` and drives its caller towards
that object at `+45.0`, `+47.5` or `+55.0` depending on the character, with an
`8.0` amplitude bob. It is the player-follows-hover-pad handler, and it holds
the only `objMoveXYZ` call in the whole overlay. Hooking it rotates a delta
that is recomputed from absolute positions every frame, which is why that
approach reported correct flags and hook hits while Floyd kept flying straight.

Mouse movement is sampled through both controller polling and video interrupts.
`BankMouseDelta()` prevents the first consumer from losing movement needed by
the other path; `QueueMouseWheel()` and `QueueGamepadScroll()` preserve a wheel
notch or an X/Y press until a controller poll can return its one-shot A/B
impulse. Keep that separation if new input paths are introduced.

Relevant player-data offsets include `+0x568` (camera mode), `+0x104` (orbit
yaw), `+0x10A` (camera yaw), `+0x11C` (movement yaw), and the manual-aim fields
starting at `+0x1CE`. The runtime recognizes normal, jump, manual aim, crouch
aim, boss aim, crouch and prone modes. The exact camera code replacements are
kept in `CameraCodePatches[]`; do not copy only one of a dependent group.

### Frame pacing and 60 FPS corrections

The 60 FPS mode combines independently guarded changes:

- neutralize the one-frame-in-two path in `viFrameSync`;
- release the scheduler's graphics task after the first retrace;
- request the game's third framebuffer before a level allocates framebuffers;
- optionally double the emulator VI CPU budget while gameplay is active, independently for 30 FPS and 60 FPS modes;
- scale generic enemies and the module-3 Squaddie velocity helpers by 0.5.

The frame-pacing-only option has a narrower purpose: it prevents the 30 FPS
mode from falling to 20 FPS after a brief slow frame. It is not a substitute for
the CPU-budget overclock: the latter gives the emulator more CPU work per video
interrupt while retaining the 30 FPS game target. It is enabled by default for
the 30 FPS profile, but can be disabled because it is an overclock rather than
original-hardware timing.

The generic movement hook sits after the caller has prepared the three movement
deltas, so it scales distance without dropping the game's timer, collision, or
event updates. Squaddies use a relocatable overlay, hence the live overlay-table
resolution and the extra surrounding signatures.

### Sprint

Sprint is host-side and is available only while the player is standing, in
normal gameplay, moving, not aiming, and not controlling Floyd. Holding Left
Shift ramps linearly over 0.5 seconds to a 1.25 multiplier. It observes the
player's X/Z displacement and adds the remaining fraction, preserving the
game's acceleration, collision and slope response. It advances the selected
looped animation frame by the same fraction so the run animation and footstep
events follow the speed increase.

### Cutscenes

The code for faster cutscenes is present but the user-facing option is disabled.
It changes the `animseqUpdate` step while an animation sequence/path is live,
rather than skipping data outright. Treat it as experimental and validate every
sequence before enabling or redesigning it.

## Water wake / ripple investigation - not an active patch

The 60 FPS water wake still has an unresolved rendering issue: it may vanish
after first appearing. The runtime currently calls all water-wake patch methods
with `false`; none of the following exploratory hooks is active in a normal
build:

| Area | Address |
| --- | --- |
| `wakeUpdateRipple` | `0x8006B090` |
| General render-list culling probe | `0x800146E4` |
| Stock wake draw | `0x80014C44` |
| Fallback draw point | `0x80014CA0` |
| Ring-buffer frame-rate probe | `0x8006B1A0` |
| Wake object list / count | `0x800F2CA4` / `0x800F2CA8` |
| General render list | `0x800F2FB0` |
| Global wake fade | `0x800A6950` |

Useful findings so far:

- the game mixes delta-time-scaled movement with state that advances once per
  call, so simply halving one alpha increment was insufficient;
- wake geometry is reached through `wake + 0x10`; the count byte is at `+0x38`,
  entries are `0x10` bytes, and one observed per-entry range is `+0x0E`;
- `wake + 0x84` is **not** a geometry buffer. Writing there caused a black
  screen and must not be repeated;
- compare equivalent 30 and 60 FPS captures with passive diagnostics first.
  Do not write wake structures until a field's ownership and lifetime are
  established.

## Recommended workflow for another ROM hack

1. Identify the exact ROM with header, CRCs, version and hashes. Add a separate
   target gate; do not broaden the USA 1.0 gate.
2. Locate the behaviour in the decompilation, then confirm it in a normalized
   ROM disassembly and in live RDRAM.
3. Determine whether the code is in the main image or an overlay. Resolve
   overlays dynamically and validate their surrounding instructions.
4. First add read-only logging or the input-rate diagnostic. Compare a
   reproducible save/scene at 30 and 60 FPS before changing memory.
5. Make the smallest reversible data or code patch. Include expected original
   words and disable/restore it cleanly.
6. Test boot, level transition, gameplay, menus, pause/unpause, save, load,
   disabled setting, 30 FPS and 60 FPS. Re-test with the recompiler enabled.
7. Document the ROM revision, function name, addresses, signatures, scratch
   ownership, option dependencies and known failures beside the implementation.

## Test checklist

For any change to this runtime, at minimum test:

- every JFG source disabled: ordinary controller-plugin input still works;
- keyboard/mouse enabled: only the custom port-one input reaches the game;
- a gamepad enabled on player 1 with and without the keyboard/mouse, then
  routed to player 2 for the secondary mapping, then unplugged;
- normal play, manual aim, crouch, prone, boss aim and Floyd, with Floyd strafe
  disabled and enabled in both directions;
- entering and leaving a level, then toggling the relevant option;
- 30 FPS and 60 FPS, with and without the enemy-speed correction;
- save/load both before and after the affected scenario;
- a clean restart after the option was enabled.

If a signature mismatch is hit, preserve it as a diagnostic outcome. It is
evidence that the code, overlay generation, ROM revision, or hook state differs
from the one this runtime was designed for.
