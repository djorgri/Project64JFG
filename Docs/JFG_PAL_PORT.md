# Porting the JFG hacks to the PAL ROM

Target ROM: `Jet Force Gemini (Europe)`, cartridge ID `NJFP`, version byte
`0x00`, internal name "JET FORCE GEMINI".

| | Project64 identifier |
| --- | --- |
| USA retail | `8A6009B6-94ACE150-C:45` |
| PAL retail | `68D7A1DE-0079834A-C:50` |
| Kiosk | `DFD8AB47-3CDBEB89-C:45` |

The PAL build is accepted by `IsSupportedRom()` like the other two and has its
own `JFG_ADDRESSES` table, `JfgPalAddresses`, in
`Source/Project64-core/N64System/GameHacks/JetForceGeminiAddresses.cpp`. Every
feature of the USA build runs on it: controls, camera, aiming, sprint, Floyd,
enemy speed, cinematic skips, frame pacing, and all the HUD hacks (widescreen
correction, alignment, native HUD raster, Floyd outline, multiplayer HUD,
rocket reticle and title logo). This document records how the PAL column was
established, what was verified, and what still has to be confirmed in game.

The ROM mapping of the base segment is the same as the USA one:
`ROM offset = RAM address - 0x80000450 + 0x1050`.

## What differs between the builds

**The structures do not.** The object and player fields the hacks use appear
with exactly the same frequencies in both base segments (loads and stores
counted by displacement):

| Offset | Field | USA | PAL |
| --- | --- | --- | --- |
| `0x11C` | player movement yaw | 58 | 58 |
| `0x1E2` | aim pitch | 13 | 13 |
| `0x1E4` / `0x1E8` | camera rotation speeds | 11 / 12 | 11 / 12 |
| `0x568` | camera mode | 39 | 39 |
| `0x68` | object -> PlayerData | 633 | 633 |
| `0x58` | object -> ripple | 403 | 403 |
| `0x5C0` | PlayerData -> camera object | 3 | 3 |

So, as for the Kiosk, only code and global addresses move.

**The layout does.** The PAL compile is the same source with 50 Hz changes:

- Code moves by a growing amount, from `-0x20` in `objMoveXYZ` to `+0x270` at
  the end of the text; the diagnostic block holding every HUD cave moves by
  `+0x210` as a whole.
- Data moves by `+0x270` / `+0x280`; BSS by `-0x5A0` for most globals (`-0x59E`
  from `0x800FECAB`, where PAL inserts a byte, and `-0x5A8` around the CPU line
  queue). The scratch cave (`0x8009FCA0` -> `0x8009FF10`) and the camera gap
  (`0x8009F228` -> `0x8009F498`) are the same unused words, `+0x270` further.
- In a few functions the compiler allocated other registers. Each such word is
  spelled per build, never guessed: `FramePacingEscalateStoreWord`,
  `FramePacing60StoreWord` and the scheduler and frame-pacing signature words
  in the table; in the HUD, the sprite matrix
  hook's delay slot (`$s2` -> `$s1`), the fuel gauge edits (`$t5` -> `$t6`) and
  the title logo branch (`$t2` -> `$t9`).

**The rate does.** On a PAL console the game runs its video modes as `8` to
`11` (`viChangeMode` adds 8), at 50 Hz with the same frame step, and keeps the
NTSC pace by scaling its own speed constants by 60/50 (Floyd's forward cap at
`0x8003045C` becomes 9.6). The runtime follows suit:

- Floyd's lateral and vertical thrusters use a drag of `0.975^1.2`, a cap of
  9.6 and a thrust that keeps the NTSC terminal speed (360 units a second), see
  `PalDroneLateral*` in `JetForceGemini.cpp`;
- the gamepad camera, sampled once per video interrupt, is scaled by 60/50;
- the frame pacing modes are the game's own, so "60 FPS" gives 50 FPS and
  "30 FPS" gives 25 on PAL, and the toggle says so;
- the HUD normalises the video mode with `JfgHudBuild::VideoMode()`. The four
  PAL modes draw into the same 320x240 and 448x336 framebuffers as NTSC, so the
  HUD layout is shared; only the VI's vertical scale differs.

## Method

No PAL symbol map exists, so the table was derived from the USA one by aligning
the two images:

1. **Function matching.** Functions were delimited in both base segments and
   paired by their canonical instruction stream (jump targets, `lui` upper
   halves and memory displacements masked, registers canonicalised), then
   aligned instruction by instruction inside each pair. A code address maps
   through the pair and the alignment; every mapped site is then re-read in the
   PAL image and must hold the expected instruction.
2. **Data references.** Globals were mapped through the `lui`/low pairs of
   matched instructions that reference them, and the resulting ranges checked
   against every reference in the two images.
3. **Overlays.** The ROM overlay table and the per-overlay relocation tables
   were decoded for both builds. Module offsets were mapped by aligning each
   overlay's text, and relocated words (calls into the base segment, global
   references) compared after applying the relocations.

The Kiosk table was used as an independent cross-check of the method: every
direct Kiosk mapping it produced agreed with the established Kiosk table.

## Core address table

`JfgPalAddresses` lists all 106 fields; the full correspondence is at the end
of this document. Beyond addresses it carries the overlay offsets that moved
(module 13 target overlay, module 16 boy aim) and the instruction words listed
above.

The stub areas are the same retired diagnostic code as on the USA build,
`+0x210` further (`0x80066E10` to `0x80067410`), and the scratch and camera
words the same unused data, `+0x270` further. The stub listings that encode
their own addresses (the Floyd and landing-skip stubs) are built from the table
(`ScratchCaveWord()`, `LandingCinematicSkipStubCode()`), so the US words are
unchanged and the PAL ones follow the table.

## HUD translation layer

The HUD hacks are written against the USA executable in terms that no table
field covers: fixed addresses, module offsets, and MIPS listings whose jumps
and `lui`/low pairs encode USA addresses. Rather than a second copy of every
listing, they stay spelled in USA terms and are translated:

- `JetForceGeminiHudBuildMap.h` holds the USA -> PAL ranges for the base
  segment (only the functions and globals the HUD reaches, each range checked
  word for word against the PAL image), the module ranges for overlays 6, 12,
  13, 14, 61 and 63, and the listing relocator: `j`/`jal` targets, and every
  load, store, `addiu` or `ori` through a register loaded by a `lui` in
  `[0x8000, 0x8080)`, with the `lui` rewritten to the upper half its uses need.
  It is self-contained C++14 and shared with the Parallel-RDP plugin.
- `JetForceGeminiHudBuild.h` adds the running build (from the address table in
  use) and the constant types the HUD tables are written with:
  `UsAddress { us }`, `UsOffset { module, us }`, `UsWord { us }` and
  `BuildWord { us, pal }`, the last for the few words no translation covers (a
  displaced instruction whose upper half lives in game code, or another
  register). On the USA build every conversion is the identity.
- `JetForceGeminiHudPalOriginals.h` holds the PAL words of the diagnostic
  routines the HUD borrows as caves (rocket overlay, HUD raster, multiplayer HUD,
  Floyd diagnostic, alignment cave and guard), read from the PAL ROM. The
  runtime restores and recognises these instead of the USA images.

The plugin selects its build from the ROM header CRC (`JfgBuild.h`, on
`InitiateGFX` and `RomOpen`), translates the cave words and globals it checks
the same way, and takes the VI geometry from `VI_V_SYNC`: 288 output lines per
field on PAL timing instead of 240, with the X crop and the scan-out offsets
(128/44 instead of 108/34) to match. The core's ISViewer bridge accepts both
CRCs.

## Validation performed

Everything below was run against the retail ROMs; none of it needs the
emulator to run the game.

- **Core table:** 127 code sites were re-read in the PAL image at their mapped
  addresses. 123 hold the expected original word (or the per-build word the
  table gives). Of the other four, three are the camera entries described
  below, which read the same way on the USA ROM; the fourth is the delay word of
  `RemoveLegacyLandingCinematicSkip()`, a cleanup for states made by an early
  USA build, whose PAL entry never matches the legacy hook, so it never writes.
  The Floyd and landing-skip stubs built from the table reproduce the former
  hand-written USA words exactly.
- **Runtime against a PAL memory image.** An 8 MiB RDRAM image was built from
  each ROM: base segment, zeroed BSS, and overlays 6, 12, 13, 14, 16, 61 and 63
  loaded at fixed bases with their relocations applied. The production
  `JetForceGemini.cpp`, `JetForceGeminiAddresses.cpp` and `GameHackMemory.cpp`
  were compiled against stub settings and this memory, and driven through
  `ProcessRuntimeFrame`, `ProcessVideoFrame` and `ProcessController` in seven
  scenarios: solo widescreen with and without alignment, 4:3 alignment, 4:3 with
  the widescreen option on, multiplayer widescreen, menu text only, and every
  non-HUD feature at once. In every HUD scenario the two builds wrote the same
  number of words (929, 473, 456, 456, 96, 20; 650 for the features), and
  switching everything off restored the image word for word. Every word the
  hacks wrote on PAL is the USA word, the same jump retargeted by the
  translation, or the same instruction with its low half moved by one of the
  known range deltas, except the intended differences: the PAL video-mode test
  in the Floyd line helper (`sltiu 12` instead of `4`), the recomputed Floyd
  step branch, the fuel gauge's `$t6`, the frame-pacing store's `$s2`, and the
  50 Hz Floyd thruster constants.
- **Plugin checks:** the plugin's own signature checks (HUD scopes, font scope,
  multiplayer readiness, radar matrix, number scope, solo and multiplayer
  reticle capture) accept the memory the core leaves on each build, and reject
  it when the other build is selected.
- The Python suite in `Source/Script/tests` passes, the plugin's GPU HUD tests
  pass, and the core, the executable and the plugin build without warnings.

These checks also showed one pre-existing oddity, identical on the USA build: three
free-camera entries of `CameraCodePatches` (`CameraLookHelperCall + 0x0C` and
`+ 0x38`, `CameraYawHelperCall + 0x14`) list the retail word as their
replacement, so switching the free camera off leaves their alternative words in
place. It is unrelated to the port and has been left as it is.

## In-game checks

A first play session on the PAL ROM (24 September 2026) found everything
working. These areas depend on the running game rather than on code
signatures, so they are the ones to recheck after a change:

1. Boot to the title screen and a level with every option on (the HUD hooks
   must stay out of the boot sequence, as on the USA build).
2. Intro and landing cinematic skips. The landing skip assumes the PAL scene
   and setup numbers are the USA ones.
3. Camera, mouse aim and gamepad aim, free camera in jumps, sprint, crouch and
   prone strafing.
4. Floyd: lateral and vertical thrusters, their feel at 50 Hz, player 2 on
   Floyd in co-op.
5. 50/25 FPS switching and the scheduler release, enemy and Squaddie speed at
   50 FPS, the water wake.
6. HUD in the game's widescreen modes (PAL modes 9 and 11) with Force 16:9, and
   alignment in 4:3: ammunition, gauges, banner, reticles, rocket reticle,
   Floyd's outline, menu and pause text, the multiplayer HUD, the title logo.
7. The plugin's HUD layer on PAL timing with overscan cropping on and off.

## Correspondence table

| Field | USA | PAL | Difference |
| --- | --- | --- | --- |
| `ObjectMoveEntry` | `0x80009A24` | `0x80009A04` | -0x20 |
| `ObjectMoveResume` | `0x80009A28` | `0x80009A08` | -0x20 |
| `CameraClampBranch` | `0x8002D128` | `0x8002D1A0` | +0x78 |
| `CameraCenterBranch` | `0x8002D154` | `0x8002D1CC` | +0x78 |
| `CameraOrbitGateBranch` | `0x8002DEC8` | `0x8002DF40` | +0x78 |
| `CameraOrbitCenterBranch` | `0x8002DED8` | `0x8002DF50` | +0x78 |
| `CameraOrbitBranch` | `0x8002DF94` | `0x8002E00C` | +0x78 |
| `CameraPositionXBaseCall` | `0x8002E0A8` | `0x8002E120` | +0x78 |
| `CameraPositionZBaseCall` | `0x8002E0E0` | `0x8002E158` | +0x78 |
| `CameraHeightBlendBase` | `0x8002E51C` | `0x8002E594` | +0x78 |
| `CameraLookHelperCall` | `0x8002E814` | `0x8002E88C` | +0x78 |
| `CameraYawHelperCall` | `0x8002EA34` | `0x8002EAAC` | +0x78 |
| `CameraPitchHelperCall` | `0x8002EA5C` | `0x8002EAD4` | +0x78 |
| `CameraTopDownEntry` | `0x8002EB6C` | `0x8002EBE4` | +0x78 |
| `SidekickStrafeEntry` | `0x80030060` | `0x800300F0` | +0x90 |
| `SidekickStrafeDelay` | `0x80030064` | `0x800300F4` | +0x90 |
| `CameraAngleHelper` | `0x80033FA4` | `0x8003403C` | +0x98 |
| `ManualAimXVelocityStore` | `0x8003AF14` | `0x8003B010` | +0xfc |
| `ManualAimYVelocityStore` | `0x8003AF2C` | `0x8003B028` | +0xfc |
| `ManualAimCursorXStore` | `0x8003B014` | `0x8003B110` | +0xfc |
| `ManualAimCursorYStore` | `0x8003B058` | `0x8003B154` | +0xfc |
| `LandingCinematicSkipEntry` | `0x80045FF0` | `0x800460F0` | +0x100 |
| `LegacyLandingCinematicSkipEntry` | `0x80046018` | `0x80046118` | +0x100 |
| `SchedulerSignatureBase` | `0x800506D0` | `0x800508F0` | +0x220 |
| `SchedulerFrameGateAdd` | `0x800506D8` | `0x800508F8` | +0x220 |
| `FramePacingSignatureBase` | `0x800550DC` | `0x800552B8` | +0x1dc |
| `FramePacingEscalateStore` | `0x800550E8` | `0x800552C4` | +0x1dc |
| `FramePacing60SignatureBase` | `0x800550F0` | `0x800552CC` | +0x1dc |
| `FramePacing60Branch` | `0x800550F8` | `0x800552D4` | +0x1dc |
| `SidekickStrafeStub` | `0x80066C00` | `0x80066E10` | +0x210 |
| `SidekickVerticalStub` | `0x80066F00` | `0x80067110` | +0x210 |
| `SidekickPadProbeStub` | `0x80066D80` | `0x80066F90` | +0x210 |
| `ObjectMoveStub` | `0x80066E00` | `0x80067010` | +0x210 |
| `LandingCinematicSkipStub` | `0x80067000` | `0x80067210` | +0x210 |
| `WaterWakeRingRateEntry` | `0x8006AAC8` | `0x8006ACD8` | +0x210 |
| `CameraHelperBase` | `0x800968CC` | `0x80096B3C` | +0x270 |
| `CameraTopDownHelperBase` | `0x80098D08` | `0x80098F78` | +0x270 |
| `CameraNativeYAddress` | `0x8009F244` | `0x8009F4B4` | +0x270 |
| `CameraHeightOffsetAddress` | `0x8009F248` | `0x8009F4B8` | +0x270 |
| `CameraTopDownCounterAddress` | `0x8009F24C` | `0x8009F4BC` | +0x270 |
| `SidekickControlObjectAddress` | `0x8009FCA0` | `0x8009FF10` | +0x270 |
| `DroneLateralMaxSpeedAddress` | `0x8009FCA4` | `0x8009FF14` | +0x270 |
| `DroneLateralSideFactorAddress` | `0x8009FCA8` | `0x8009FF18` | +0x270 |
| `DroneLateralVelocityAddress` | `0x8009FCAC` | `0x8009FF1C` | +0x270 |
| `DroneVerticalThrustAddress` | `0x8009FCD0` | `0x8009FF40` | +0x270 |
| `DroneVerticalVelocityAddress` | `0x8009FCD4` | `0x8009FF44` | +0x270 |
| `DroneLateralRightZAddress` | `0x8009FCB0` | `0x8009FF20` | +0x270 |
| `LandingCinematicSkipInputAddress` | `0x8009FCBC` | `0x8009FF2C` | +0x270 |
| `DroneLateralDragAddress` | `0x8009FCC0` | `0x8009FF30` | +0x270 |
| `DroneLateralForwardSpeedAddress` | `0x8009FCC4` | `0x8009FF34` | +0x270 |
| `DroneLateralRightXAddress` | `0x8009FCC8` | `0x8009FF38` | +0x270 |
| `SidekickPadProbeStateAddress` | `0x8009FCCC` | `0x8009FF3C` | +0x270 |
| `DroneLateralPreviousObjectAddress` | `0x8009FCD8` | `0x8009FF48` | +0x270 |
| `SidekickPadProbeObjectAddress` | `0x8009FCDC` | `0x8009FF4C` | +0x270 |
| `EnemyHalveFlagAddress` | `0x8009FCE0` | `0x8009FF50` | +0x270 |
| `DroneLateralFlagsAddress` | `0x8009FCE4` | `0x8009FF54` | +0x270 |
| `DroneLateralHookHitsAddress` | `0x8009FCE8` | `0x8009FF58` | +0x270 |
| `SidekickPadProbeActorAddress` | `0x8009FCFC` | `0x8009FF6C` | +0x270 |
| `RobotMissionAddress` | `0x800A3208` | `0x800A3478` | +0x270 |
| `MultiplayerGameAddress` | `0x800A4FC4` | `0x800A5244` | +0x280 |
| `CooperativeGameAddress` | `0x800A4FC8` | `0x800A5248` | +0x280 |
| `WaterWakeObjectListAddress` | `0x800F2CA4` | `0x800F2704` | -0x5a0 |
| `WaterWakeObjectCountAddress` | `0x800F2CA8` | `0x800F2708` | -0x5a0 |
| `PlayerListAddress` | `0x800F2D0C` | `0x800F276C` | -0x5a0 |
| `PlayerCountAddress` | `0x800F2D10` | `0x800F2770` | -0x5a0 |
| `DisableJoyAddress` | `0x800F6DBC` | `0x800F681C` | -0x5a0 |
| `ControlCameraAddress` | `0x800F6DC0` | `0x800F6820` | -0x5a0 |
| `CameraActiveOverrideBase` | `0x800F6E58` | `0x800F68B8` | -0x5a0 |
| `CameraArrayAddress` | `0x800FA4D0` | `0x800F9F30` | -0x5a0 |
| `CameraFovAddress` | `0x800FB078` | `0x800FAAD8` | -0x5a0 |
| `LobbyCameraInUseAddress` | `0x800FB080` | `0x800FAAE0` | -0x5a0 |
| `StaticCameraInUseAddress` | `0x800FB084` | `0x800FAAE4` | -0x5a0 |
| `OverlayTableAddress` | `0x800FEAA0` | `0x800FE500` | -0x5a0 |
| `CurrentScreenAddress` | `0x800FECB0` | `0x800FE710` | -0x5a0 |
| `AnimseqCameraAddress` | `0x801045B8` | `0x80104008` | -0x5b0 |
| `IntroCinematicSkipStub` | `0x80067200` | `0x80067410` | +0x210 |
| `CurrentSceneAddress` | `0x800A323C` | `0x800A34AC` | +0x270 |
| `CurrentSetupAddress` | `0x800A3248` | `0x800A34B8` | +0x270 |
| `NextCharacterAddress` | `0x800A3260` | `0x800A34D0` | +0x270 |
| `LoadingAddress` | `0x800A3294` | `0x800A3504` | +0x270 |
| `MainFrontInitFunction` | `0x80047608` | `0x80047758` | +0x150 |
| `FrontCharSelectSetQuitModeFunction` | `0x8005AAE8` | `0x8005ACF8` | +0x210 |
| `FrontGetModeFunction` | `0x80058A5C` | `0x80058C6C` | +0x210 |
| `MainChangeLevelFunction` | `0x8004665C` | `0x8004675C` | +0x100 |
| `FloydPadControlOffset` | `0x000002A4` | `0x000002A4` | same |
| `IntroCinematicSkipEntryOffset` | `0x000000D0` | `0x000000D0` | same |
| `LegacyIntroCinematicSkipEntryOffset` | `0x000000C0` | `0x000000C0` | same |
| `LegacyIntroCinematicSkipFmvUpdateOffset` | `0x0000037C` | `0x0000037C` | same |
| `TargetOverlayCursorXOffset` | `0x0000041C` | `0x00000418` | module offset -0x4 |
| `TargetOverlayCursorYOffset` | `0x00000444` | `0x00000440` | module offset -0x4 |
| `TargetOverlayDrawOffset` | `0x000004A8` | `0x000004A4` | module offset -0x4 |
| `BoyAimHelperOffset` | `0x00003DB0` | `0x00003ECC` | module offset +0x11c |
| `BoyAimFirstGroupOffset` | `0x00004198` | `0x000042B4` | module offset +0x11c |
| `BoyAimSecondGroupOffset` | `0x00004738` | `0x00004874` | module offset +0x13c |
| `CameraHelperCallWord` | `0x0C00CFE9` | `0x0C00D00F` | instruction word |
| `FramePacing60SignatureWord0` | `0x8DCEECCC` | `0x8DCEE72C` | instruction word |
| `FramePacingSignatureWord1` | `0x2442ECAB` | `0x2442E70D` | instruction word |
| `ManualAimCursorXGuardWord` | `0x15E10002` | `0x15E10002` | same |
| `ManualAimCursorYGuardWord` | `0x15610002` | `0x15610002` | same |
| `ManualAimCursorXStoreWord` | `0xA7190000` | `0xA7190000` | same |
| `ManualAimCursorYStoreWord` | `0xA58D0000` | `0xA58D0000` | same |
| `SchedulerFrameGateAddWord` | `0x25090001` | `0x25090001` | same |
| `SchedulerSignatureWord0` | `0x8E480300` | `0x8E480300` | same |
| `SchedulerSignatureWord2` | `0x2D210002` | `0x2D210002` | same |
| `FramePacingEscalateStoreWord` | `0xA22D0000` | `0xA24D0000` | instruction word |
| `FramePacing60StoreWord` | `0xA22F0000` | `0xA24F0000` | instruction word |
