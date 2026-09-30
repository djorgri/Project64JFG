# Porting the JFG hacks to the Japanese ROM

Target ROM: `Star Twins (Japan)`, cartridge ID `NJFJ`, version byte `0x00`,
internal name "STAR TWINS", an NTSC build.

| | Project64 identifier |
| --- | --- |
| USA retail | `8A6009B6-94ACE150-C:45` |
| PAL retail | `68D7A1DE-0079834A-C:50` |
| Japanese retail | `F163A242-F2449B3B-C:4A` |
| Kiosk | `DFD8AB47-3CDBEB89-C:45` |

The Japanese build is accepted by `IsSupportedRom()` like the others and has
its own `JFG_ADDRESSES` table, `JfgJpAddresses`, in
`Source/Project64-core/N64System/GameHacks/JetForceGeminiAddresses.cpp`. Every
feature of the USA build runs on it: controls, camera, aiming, sprint, Floyd,
enemy speed, cinematic skips, frame pacing, and all the HUD hacks (widescreen
correction, alignment, native HUD raster, Floyd outline, multiplayer HUD,
rocket reticle and title logo). This document records how the JP column was
established, what differs from the USA build, what was verified, and what
still has to be confirmed in game.

The ROM mapping of the base segment is the USA one:
`ROM offset = RAM address - 0x80000450 + 0x1050`.

## What differs between the builds

**The structures do not.** As for PAL and the Kiosk, the object, player and
camera fields the hacks use sit at the same offsets; only code and global
addresses move.

**The layout moves little.** JP is the USA source with Japanese text and an
NTSC timing, so the video modes are the USA `0` to `3` and the frame pacing
targets stay 60 and 30 FPS:

- Base-segment code moves by `-0x1D0` to `+0xC0` (`0` in `objMoveXYZ`, `-0x104`
  around the camera matrix code, `-0x1A8` in the front end, `+0x40` for the
  diagnostic block holding every HUD cave, `+0x84` in the effects renderer);
- data moves by `-0x140` from its start (`0x800A0660` -> `0x800A0520`), then
  `-0xF0` from the video mode scales onwards; BSS by `-0xF0` for most globals and
  `-0xE0` from `0x800FDFE0` (overlay table, resolution index, current screen,
  HUD textures, the scope bytes), `-0xE8` around the CPU line queue. Unlike
  PAL there is no inserted byte after the resolution index;
- the scratch padding (`0x8009FCAC` -> `0x8009FB6C`, 0x134 bytes) and both
  camera gaps are the same zero words, `-0x140` further. The five retired CPU
  trace helpers the stubs borrow keep their size and have no caller outside
  `diCpuTraceInit`, exactly as on the USA build.

**Some code differs.** Each difference is spelled per build, never guessed:

- the scheduler compiles like the Kiosk's (`$t2`/`$t3` instead of `$t0`/`$t1`),
  hence `SchedulerSignatureWord0/2` and `SchedulerFrameGateAddWord`;
- `fontPrintWindowXY` was rewritten for two-byte text: a 0x88-byte frame,
  other saved registers, the `Gfx**` argument kept in `$s3` and homed at
  `sp+0x88` only once the text is under way, and the glyph rectangle packed with
  other temporaries (lower Y in `$t4`, upper Y in `$t2`, texture step in `$t7`).
  The widescreen font stubs take their JP registers from `HudCodeOverrides`,
  the native HUD raster's text hooks replay the JP saves and home the argument
  themselves in the entry jump's delay slot (`TextEnterCode`/`TextExitCode`);
- overlay 14 (instruments) lays out its Japanese text differently. The fuel
  label keeps its X in `$t6` (`WidescreenHudFuelPatchesJp`); the pickup message
  starts at `capX - 42` rather than `capX - 32` and builds its scissor in other
  float registers (`WidescreenHudBannerPatchesJp`); the tribal counter spaces
  its icons 52 pixels apart, prints its counts at `s0 + 10` from
  `s0 = trunc(capX + 23)` and reschedules its floats
  (`WidescreenHudTribalPatchesJp`, and the JP delay words of the three object
  13 calls in `JetForceGeminiHudAlignmentSites.h`);
- the title module (63) is larger, its frame 8 bytes smaller, and its logo
  branch tests `$t9` like PAL's;
- the health module (6) and the multiplayer instruments (61) are rearranged
  but keep every word the HUD touches.

## Method

No JP symbol map exists (the decompilation's JP configuration names only
libultra), so the table was derived from the USA one by aligning the images:

1. **Base segment.** Both images were reduced to canonical words (jump
   targets, branch offsets, `lui` upper halves of `0x80xxxxxx` and global
   displacements masked). Unique 12-word sequences present once in each image
   were paired and chained by a longest increasing subsequence (160,000
   anchors); a code address maps through the anchor run that covers it. The
   method was first run on USA -> PAL, where it reproduced every code field of
   the established PAL table except the four frame-pacing sites whose
   registers PAL reallocated. On JP every code field maps; each mapped word was
   re-read in the JP image and is the USA word or differs only in a relocated
   target or global, except the scheduler words above. The one function the
   anchor chain skipped, `frontCharSelectSetQuitMode`, was placed by a direct
   comparison of its equal block (`0x8005A940`).
2. **Globals.** Every `lui`/low pair of an anchored instruction votes for the
   JP address of the global it references. All fourteen BSS globals of the
   table were voted unanimously (1 to 47 votes each); on PAL the same vote
   reproduces the PAL column exactly. The data globals agree with the anchor
   deltas.
3. **Overlays.** The runtime linker (`runlinkDownloadCode`: the overlay table,
   `overlayRomTable`, and both relocation tables of each module) was
   reimplemented to load modules 6, 12, 13, 14, 16, 22, 57, 61 and 63 into an
   8 MiB RDRAM image of each build at fixed bases. The relocated USA image
   reproduces the call words the runtime already checks (`0x0C01657D`,
   `0x0C01B4E4`, `0x0C012361`, `0x0C016834`, ...), and a PAL image relocates
   data references to the expected `+0x280`. Module offsets were mapped by
   aligning each module's text and data with `difflib` on canonical words.
4. **Originals.** `JetForceGeminiHudJpOriginals.h` holds the JP words of the
   diagnostic routines the HUD borrows (rocket overlay, HUD raster,
   multiplayer HUD, Floyd diagnostic, alignment cave and guard), read at
   `+0x40`. They differ from the USA words only in relocated targets and
   globals (canonically equal word for word).

## Core address table

`JfgJpAddresses` lists all 107 fields; the full correspondence is at the end
of this document. The overlay offsets that moved are those of module 16 (boy
aim, `+0x12C` / `+0x14C`); modules 13, 22 and 57 are unchanged. The stub
listings that encode their own addresses are built from the table as on PAL,
so their JP words follow it.

## HUD translation layer

`JetForceGeminiHudBuildMap.h` adds `BuildJp` (detected from the header CRCs by
the core and the plugin alike), `JpMainRanges` and `JpOverlayRanges`. The main
ranges cover the same functions and globals as PAL's; `fontPrintWindowXY` is
covered only at its four hook sites, each mapped to its JP counterpart. The
overlay ranges were trimmed to the ones the HUD and the plugin use. In the
tribal counter, which JP reschedules, single-word ranges map a US site to the
JP instruction with the same role.

`JfgHudBuild::BuildWord` now carries all three builds and takes them through a
constructor, so a word spelled for two builds does not compile; `ForBuild`
selects the build's own table.

## Validation performed

Everything below was run against the retail ROMs; none of it needs the
emulator to run the game.

- **Runtime against memory images.** The production `JetForceGemini.cpp`,
  `JetForceGeminiAddresses.cpp` and `GameHackMemory.cpp` were compiled against
  stub settings and the relocated RDRAM image of each build, and driven through
  `ProcessRuntimeFrame`, `ProcessVideoFrame` and `ProcessController`, then with
  everything switched off. The translation functions were wrapped to log every
  query. Per scenario, the words written with everything on (USA / JP):

  | Scenario | USA | JP |
  | --- | --- | --- |
  | widescreen, low and high resolution | 485 | 485 / 482 |
  | widescreen and alignment | 941 | 941 / 938 |
  | alignment in 4:3 | 456 | 456 |
  | widescreen option in 4:3 | 0 | 0 |
  | multiplayer widescreen (module 61) | 96 | 96 |
  | menu text only | 20 | 20 |
  | every non-HUD feature | 243 | 243 |
  | cinematic skips armed | 252 | 252 |

  JP writes the same sites as USA; in high resolution the tribal counts keep
  their stock `addiu a1, s0, 10`, three writes fewer. No translation failed.
  Every word written on JP is the USA word, the same jump retargeted by the
  table or the translation, or the same instruction with its low half moved by
  a known delta, except the intended JP differences listed above. Switching
  everything off restores the image except the four words the USA build also
  leaves (the three free-camera entries noted in JFG_PAL_PORT.md and a
  scratch word the runtime clears).
- **Plugin checks.** The plugin's own checks (health, weapon and font scopes,
  number scope, multiplayer readiness, solo and multiplayer reticle capture)
  accept the memory the core leaves on JP, and reject it when another build is
  selected. The data the plugin reads (reticle glyphs, video scales, screen
  limits) is identical at its JP address.
- **No regression.** The same harness built from the previous commit and from
  this one writes byte-identical logs on the USA and PAL images in all nine
  scenarios, and PAL passes the plugin checks too.
- The Python suite in `Source/Script/tests` passes.

The harness found one latent bug, shared with PAL: the widescreen cave capture
bounded the rocket reticle's image with its translated *end* address, which no
range covers. On PAL the retail words of that range were therefore taken from
memory as it stood, which is wrong when a save state carries the installed
overlay. The capture is now bounded by the image size.

The first save state taken on the JP build then crashed the game (a fetch
fault reported at `MemoryVirtualMem.cpp` line 226). The state showed why: the
CPU was inside `frontDrawRectangles`, called by the shot gauge's wrapper stub,
whose return address (`0x80067844`) was on the stack, and `StateSaving()` had
already put the retail diagnostic code back under it. The race exists on every
build. Saves now wait while any stub is in use and the per-frame HUD update does
too (`GuestCallInFlight()`, see JET_FORCE_GEMINI_HACKING.md). Run against that
state the check postpones the save; against the 27 other USA, PAL and JP states
on hand it allows it. A state saved by the earlier build at such a moment
cannot be repaired and should be discarded.

## In-game checks

The Japanese build has not been played with the patches yet. These depend on
the running game rather than on code signatures:

1. Boot to the title screen and a level with every option on (the HUD hooks
   must stay out of the boot sequence). Save and load states repeatedly in a
   level, with the HUD on screen.
2. Intro and landing cinematic skips. The landing skip assumes the JP scene and
   setup numbers are the USA ones.
3. Camera, mouse aim and gamepad aim, free camera in jumps, sprint, crouch and
   prone strafing, Floyd's thrusters, 60/30 FPS and the scheduler release.
4. The Japanese text in widescreen: menus, pause, level captions and dialogue
   go through the rewritten font renderer (glyph height and texture step).
5. HUD in widescreen and alignment: pickup banner (centred text and clipping),
   tribal counter (counts beside their icons), fuel label and counter,
   ammunition, reticles, Floyd's outline, multiplayer HUD, title logo.

## Correspondence table

| Field | USA | JP | Difference |
| --- | --- | --- | --- |
| `ObjectMoveEntry` | `0x80009A24` | `0x80009A24` | same |
| `ObjectMoveResume` | `0x80009A28` | `0x80009A28` | same |
| `CameraClampBranch` | `0x8002D128` | `0x8002D088` | -0xa0 |
| `CameraCenterBranch` | `0x8002D154` | `0x8002D0B4` | -0xa0 |
| `CameraOrbitGateBranch` | `0x8002DEC8` | `0x8002DE28` | -0xa0 |
| `CameraOrbitCenterBranch` | `0x8002DED8` | `0x8002DE38` | -0xa0 |
| `CameraOrbitBranch` | `0x8002DF94` | `0x8002DEF4` | -0xa0 |
| `CameraPositionXBaseCall` | `0x8002E0A8` | `0x8002E008` | -0xa0 |
| `CameraPositionZBaseCall` | `0x8002E0E0` | `0x8002E040` | -0xa0 |
| `CameraHeightBlendBase` | `0x8002E51C` | `0x8002E47C` | -0xa0 |
| `CameraLookHelperCall` | `0x8002E814` | `0x8002E774` | -0xa0 |
| `CameraYawHelperCall` | `0x8002EA34` | `0x8002E994` | -0xa0 |
| `CameraPitchHelperCall` | `0x8002EA5C` | `0x8002E9BC` | -0xa0 |
| `CameraTopDownEntry` | `0x8002EB6C` | `0x8002EACC` | -0xa0 |
| `SidekickStrafeEntry` | `0x80030060` | `0x8002FFC0` | -0xa0 |
| `SidekickStrafeDelay` | `0x80030064` | `0x8002FFC4` | -0xa0 |
| `CameraAngleHelper` | `0x80033FA4` | `0x80033F0C` | -0x98 |
| `ManualAimXVelocityStore` | `0x8003AF14` | `0x8003AE7C` | -0x98 |
| `ManualAimYVelocityStore` | `0x8003AF2C` | `0x8003AE94` | -0x98 |
| `ManualAimCursorXStore` | `0x8003B014` | `0x8003AF7C` | -0x98 |
| `ManualAimCursorYStore` | `0x8003B058` | `0x8003AFC0` | -0x98 |
| `LandingCinematicSkipEntry` | `0x80045FF0` | `0x80045EF8` | -0xf8 |
| `LegacyLandingCinematicSkipEntry` | `0x80046018` | `0x80045F20` | -0xf8 |
| `SchedulerSignatureBase` | `0x800506D0` | `0x80050620` | -0xb0 |
| `SchedulerFrameGateAdd` | `0x800506D8` | `0x80050628` | -0xb0 |
| `FramePacingSignatureBase` | `0x800550DC` | `0x80054F84` | -0x158 |
| `FramePacingEscalateStore` | `0x800550E8` | `0x80054F90` | -0x158 |
| `FramePacing60SignatureBase` | `0x800550F0` | `0x80054F98` | -0x158 |
| `FramePacing60Branch` | `0x800550F8` | `0x80054FA0` | -0x158 |
| `SidekickStrafeStub` | `0x80066C00` | `0x80066C40` | +0x40 |
| `SidekickVerticalStub` | `0x80066F00` | `0x80066F40` | +0x40 |
| `SidekickPadProbeStub` | `0x80066D80` | `0x80066DC0` | +0x40 |
| `ObjectMoveStub` | `0x80066E00` | `0x80066E40` | +0x40 |
| `LandingCinematicSkipStub` | `0x80067000` | `0x80067040` | +0x40 |
| `WaterWakeRingRateEntry` | `0x8006AAC8` | `0x8006AB08` | +0x40 |
| `CameraHelperBase` | `0x800968CC` | `0x8009678C` | -0x140 |
| `CameraTopDownHelperBase` | `0x80098D08` | `0x80098BC8` | -0x140 |
| `CameraNativeYAddress` | `0x8009F244` | `0x8009F104` | -0x140 |
| `CameraHeightOffsetAddress` | `0x8009F248` | `0x8009F108` | -0x140 |
| `CameraTopDownCounterAddress` | `0x8009F24C` | `0x8009F10C` | -0x140 |
| `SidekickControlObjectAddress` | `0x8009FCA0` | `0x8009FB60` | -0x140 |
| `DroneLateralMaxSpeedAddress` | `0x8009FCA4` | `0x8009FB64` | -0x140 |
| `DroneLateralSideFactorAddress` | `0x8009FCA8` | `0x8009FB68` | -0x140 |
| `DroneLateralVelocityAddress` | `0x8009FCAC` | `0x8009FB6C` | -0x140 |
| `DroneVerticalThrustAddress` | `0x8009FCD0` | `0x8009FB90` | -0x140 |
| `DroneVerticalVelocityAddress` | `0x8009FCD4` | `0x8009FB94` | -0x140 |
| `DroneLateralRightZAddress` | `0x8009FCB0` | `0x8009FB70` | -0x140 |
| `LandingCinematicSkipInputAddress` | `0x8009FCBC` | `0x8009FB7C` | -0x140 |
| `DroneLateralDragAddress` | `0x8009FCC0` | `0x8009FB80` | -0x140 |
| `DroneLateralForwardSpeedAddress` | `0x8009FCC4` | `0x8009FB84` | -0x140 |
| `DroneLateralRightXAddress` | `0x8009FCC8` | `0x8009FB88` | -0x140 |
| `SidekickPadProbeStateAddress` | `0x8009FCCC` | `0x8009FB8C` | -0x140 |
| `DroneLateralPreviousObjectAddress` | `0x8009FCD8` | `0x8009FB98` | -0x140 |
| `SidekickPadProbeObjectAddress` | `0x8009FCDC` | `0x8009FB9C` | -0x140 |
| `EnemyHalveFlagAddress` | `0x8009FCE0` | `0x8009FBA0` | -0x140 |
| `DroneLateralFlagsAddress` | `0x8009FCE4` | `0x8009FBA4` | -0x140 |
| `DroneLateralHookHitsAddress` | `0x8009FCE8` | `0x8009FBA8` | -0x140 |
| `SidekickPadProbeActorAddress` | `0x8009FCFC` | `0x8009FBBC` | -0x140 |
| `RobotMissionAddress` | `0x800A3208` | `0x800A30C8` | -0x140 |
| `MultiplayerGameAddress` | `0x800A4FC4` | `0x800A4ED4` | -0xf0 |
| `CooperativeGameAddress` | `0x800A4FC8` | `0x800A4ED8` | -0xf0 |
| `WaterWakeObjectListAddress` | `0x800F2CA4` | `0x800F2BB4` | -0xf0 |
| `WaterWakeObjectCountAddress` | `0x800F2CA8` | `0x800F2BB8` | -0xf0 |
| `PlayerListAddress` | `0x800F2D0C` | `0x800F2C1C` | -0xf0 |
| `PlayerCountAddress` | `0x800F2D10` | `0x800F2C20` | -0xf0 |
| `DisableJoyAddress` | `0x800F6DBC` | `0x800F6CCC` | -0xf0 |
| `ControlCameraAddress` | `0x800F6DC0` | `0x800F6CD0` | -0xf0 |
| `CameraActiveOverrideBase` | `0x800F6E58` | `0x800F6D68` | -0xf0 |
| `CameraArrayAddress` | `0x800FA4D0` | `0x800FA3E0` | -0xf0 |
| `CameraFovAddress` | `0x800FB078` | `0x800FAF88` | -0xf0 |
| `LobbyCameraInUseAddress` | `0x800FB080` | `0x800FAF90` | -0xf0 |
| `StaticCameraInUseAddress` | `0x800FB084` | `0x800FAF94` | -0xf0 |
| `OverlayTableAddress` | `0x800FEAA0` | `0x800FE9C0` | -0xe0 |
| `CurrentScreenAddress` | `0x800FECB0` | `0x800FEBD0` | -0xe0 |
| `AnimseqCameraAddress` | `0x801045B8` | `0x801044C8` | -0xf0 |
| `IntroCinematicSkipStub` | `0x80067200` | `0x80067240` | +0x40 |
| `CurrentSceneAddress` | `0x800A323C` | `0x800A30FC` | -0x140 |
| `CurrentSetupAddress` | `0x800A3248` | `0x800A3108` | -0x140 |
| `NextCharacterAddress` | `0x800A3260` | `0x800A3120` | -0x140 |
| `LoadingAddress` | `0x800A3294` | `0x800A3154` | -0x140 |
| `MainFrontInitFunction` | `0x80047608` | `0x80047560` | -0xa8 |
| `FrontCharSelectSetQuitModeFunction` | `0x8005AAE8` | `0x8005A940` | -0x1a8 |
| `FrontGetModeFunction` | `0x80058A5C` | `0x800588B4` | -0x1a8 |
| `MainChangeLevelFunction` | `0x8004665C` | `0x80046588` | -0xd4 |
| `OsActiveQueueAddress` | `0x800A9E8C` | `0x800A9D9C` | -0xf0 |
| `SoundPlayerEventTypeTest` | `0x80084488` | `0x80084348` | -0x140 |
| `FloydPadControlOffset` | `0x000002A4` | `0x000002A4` | same |
| `IntroCinematicSkipEntryOffset` | `0x000000D0` | `0x000000D0` | same |
| `LegacyIntroCinematicSkipEntryOffset` | `0x000000C0` | `0x000000C0` | same |
| `LegacyIntroCinematicSkipFmvUpdateOffset` | `0x0000037C` | `0x0000037C` | same |
| `TargetOverlayCursorXOffset` | `0x0000041C` | `0x0000041C` | same |
| `TargetOverlayCursorYOffset` | `0x00000444` | `0x00000444` | same |
| `TargetOverlayDrawOffset` | `0x000004A8` | `0x000004A8` | same |
| `BoyAimHelperOffset` | `0x00003DB0` | `0x00003EDC` | module offset +0x12c |
| `BoyAimFirstGroupOffset` | `0x00004198` | `0x000042C4` | module offset +0x12c |
| `BoyAimSecondGroupOffset` | `0x00004738` | `0x00004884` | module offset +0x14c |
| `CameraHelperCallWord` | `0x0C00CFE9` | `0x0C00CFC3` | instruction word |
| `FramePacing60SignatureWord0` | `0x8DCEECCC` | `0x8DCEEBEC` | instruction word |
| `FramePacingSignatureWord1` | `0x2442ECAB` | `0x2442EBCB` | instruction word |
| `ManualAimCursorXGuardWord` | `0x15E10002` | `0x15E10002` | same |
| `ManualAimCursorYGuardWord` | `0x15610002` | `0x15610002` | same |
| `ManualAimCursorXStoreWord` | `0xA7190000` | `0xA7190000` | same |
| `ManualAimCursorYStoreWord` | `0xA58D0000` | `0xA58D0000` | same |
| `SchedulerFrameGateAddWord` | `0x25090001` | `0x254B0001` | instruction word |
| `SchedulerSignatureWord0` | `0x8E480300` | `0x8E4A0300` | instruction word |
| `SchedulerSignatureWord2` | `0x2D210002` | `0x2D610002` | instruction word |
| `FramePacingEscalateStoreWord` | `0xA22D0000` | `0xA22D0000` | same |
| `FramePacing60StoreWord` | `0xA22F0000` | `0xA22F0000` | same |
