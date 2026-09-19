# Porting the JFG hacks to the Kiosk ROM

Target ROM: `Jet Force Gemini (USA) (Demo) (Kiosk)`, internal name
"J F G DISPLAY".

| | Project64 identifier |
| --- | --- |
| USA retail | `8A6009B6-94ACE150-C:45` |
| Kiosk | `DFD8AB47-3CDBEB89-C:45` |

Both builds are accepted by `IsSupportedRom()`; each has its own
`JFG_ADDRESSES` table in
`Source/Project64-core/N64System/GameHacks/JetForceGeminiAddresses.cpp`. This
document records how the Kiosk column of that table was established, what
confirms each entry, and how to validate a feature on the Kiosk. It started
as a working journal, so the sections below follow the order the work was
done in.

The ROM mapping of the base segment is **the same as the USA one**:
`ROM offset = RAM address - 0x80000450 + 0x1050`. Verified on the prologues of
`amSetMuteMode`, `wakeUpdateRipple`, `controlGetManualAim` and
`controlPlayer`.

## The result that decides everything: the structures are identical

The field offsets the hack uses appear with the same frequencies in both
images:

| Offset | Field | Occurrences USA | Kiosk |
| --- | --- | --- | --- |
| `0x11C` | player movement yaw | 58 | 55 |
| `0x1E2` | aim pitch | 13 | 15 |
| `0x1E4` / `0x1E8` | camera rotation speeds | 11 / 9 | 11 / 9 |
| `0x568` | camera mode | 39 | 37 |
| `0x68` | object -> PlayerData | 633 | 590 |
| `0x58` | object -> ripple | 403 | 420 |

And the code match confirms it word for word: the `swc1 $f4, 0x1E4($s0)` of
`controlGetManualAim` is found identically in the Kiosk.

**Consequence: no `PlayerData + offset` access needs touching.** Only code and
global addresses change. That is what makes the port realistic.

## Method

The decompilation provides two symbol tables, `jfg_us_syms_full.txt` and
`jfg_kiosk_syms_full.txt`. Three techniques, in order of reliability:

1. **Symbol + offset** - exact for globals, which almost all land right on a
   named symbol (`currentScreen+0`, `playerlist+0`, `disablejoy+0`,
   `controlcam+0`, `ObjList+0`, `animcamera+0`).
2. **Instruction signature** - indispensable *inside* functions: the Kiosk is a
   separate compilation, the bodies differ, the offset does not carry over. A
   window of instructions is compared with everything that has to move masked
   out (`jal`/`j` targets, `lui` high halves, memory displacements). The rest -
   opcodes, registers, ALU immediates, branch displacements - is compared
   exactly, so a match is structural rather than guessed.
3. **Reference from the code** - the safest for a global: the `lui`/`addiu`
   pair is re-read at the Kiosk location found. This is how
   `WaterWakeGlobalFade` was obtained: USA `0x800A6950` -> Kiosk `0x800A7310`.

Every code patch carries its expected original word, which gives an
independent check: 18 of the 27 verifiable sites find their exact word at the
computed Kiosk address.

## Coverage

**87 of the 118 address constants were resolved** by the first pass. Of the
31 left:

* **7 are not addresses** - `0x8FBF003C`, `0x8FAD00E8`, `0x86020090`... are
  expected instruction words, wrongly captured by the extraction. They had to
  be re-read in the Kiosk, like the relocated `jal` targets.
* **4 are in expansion RAM** (`0x8034xxxx`, `0x8035xxxx`): overlay-resident
  addresses, a separate class of problem.
* **9 are stub bases** (`0x80066C00`...`0x80067000`).
* **11 are real entry points**, 4 of which were resolved in a second pass.

### Resolved in the second pass, with a clear margin

| Constant | USA | Kiosk | Score | Margin over the 2nd candidate |
| --- | --- | --- | --- | --- |
| `CameraTopDownEntry` | `8002EB6C` | `8002EB78` | 19/21 | +15 |
| `PlayerVelocityEntry` | `800341A4` | `800341B8` | 27/29 | +16 |
| `SidekickLateralMoveEntry` | `80031088` | `80031094` | 26/29 | +18 |
| `SidekickLateralMoveOldTailEntry` | `80031080` | `8003108C` | 25/29 | +16 |

### `SchedulerFrameGateAdd` - resolved by hand

This is the 60 fps lock, and the automatic search failed (6/13) for an
instructive reason: the Kiosk allocates different registers. The signature
compared the register fields exactly, which cannot work across two separate
compilations.

Searching for the **shape** rather than the bytes - a `lw` at `0x300`, an
`addiu` of 1 on that register, its store back to `0x300`, and the
`sltiu ..., 2` that drives the release - the pattern appears **exactly once in
each image**. Cross-check: on the USA image the search lands exactly on
`0x800506D8`, the known address. No ambiguity.

The Kiosk context is identical instruction for instruction, same field
offsets and same branch displacements (0x19, 0x16, 0x10, 0x05, 0x06); only
the registers change, `t0`/`t1` becoming `t2`/`t3`.

| | USA | Kiosk |
| --- | --- | --- |
| `SchedulerSignatureBase` | `800506D0` | **`8005124C`** |
| `SchedulerFrameGateAdd` | `800506D8` | **`80051254`** |
| Original word | `25090001` | **`254B0001`** |
| Replacement | `25090002` | **`254B0002`** |
| `Signature[0]` (+0x00) | `8E480300` | `8E4A0300` |
| `Signature[1]` (+0x04) | `8E4302FC` | `8E4302FC` (identical) |
| `Signature[2]` (+0x14) | `2D210002` | `2D610002` |

**Lesson of method**: for matches *inside* a function, the registers have to
stop counting. Erasing them would lose too much; so the registers are renamed
in order of appearance, which keeps the *relations* - the value written here
is the one read back two instructions later - while making the allocation
irrelevant. `zero`, `sp`, `ra` and `gp` are left as they are, not being
allocatable. See `jfgsig2.py`.

Validation before use: on six addresses whose match was already established,
the method finds the right answer **6 times out of 6**, and
`SchedulerFrameGateAdd` goes from 6/13 to 21/21 with a margin of +17.

### Resolved by register canonicalisation

All confirmed by disassembly on both sides.

| Constant | USA | Kiosk | USA word | Kiosk word | Score |
| --- | --- | --- | --- | --- | --- |
| `ManualAimCursorXStore` | `8003B014` | **`8003ADDC`** | `A7190000` | `A5F80000` | 21/21, +10 |
| `ManualAimCursorYStore` | `8003B058` | **`8003AE20`** | `A58D0000` | `A56C0000` | 21/21, +10 |
| `WaterWakeStockDrawEntry` | `80014C44` | **`80014908`** | `0C01ADA7` | `0C01AE78` | 21/21, +12 |
| `WaterWakeDrawFallbackEntry` | `80014CA0` | **`80014964`** | `0C01A2D5` | `0C01A39D` | 26/29, +17 |

The division guards that precede the two cursor stores (eleven words,
rewritten in place by `FillManualAimCursorPatches`) have the same shape in
both builds; only the guard's `bne $tX, $at` changes register, hence the
`ManualAimCursorXGuardWord` / `YGuardWord` fields (`15E10002` / `15610002` in
the USA build, `15C10002` / `17210002` in the Kiosk). The `mflo` is deduced
from the store's register. Verified by disassembly of both ROMs.

Two independent cross-checks reinforce the two water-wake results:

* the two water-wake sites are `0x5C` apart in the USA image **and** in the
  Kiosk;
* their `jal` target, symbol table in hand, the same named function on both
  sides - `wakeDrawRipple` for the first, `fxDrawLevelEffects` for the second.
  That is a semantic confirmation, not only a structural one.

### `CameraHelperBase` - misclassified at first

This is not a code site but **72 bytes of padding** (`800968C8`) in which the
hack installs its camera stubs. Searching for a signature there makes no
sense. It belongs with the stub areas below - and the Kiosk has nothing zero
at that address, so it needed a location of its own.

### Landing cinematic skip - absent from the Kiosk

`LandingCinematicSkipEntry` and its *legacy* variant target a loop of
`mainGameWindowSize` testing a `0x1000` flag. The function does exist in the
Kiosk, but that code is not in it: a search over **the whole base segment**
tops out at 6/21 and 5/21 with no margin - noise.

The Kiosk demo does not have this sequence. This is not a mapping failure:
the feature is **moot** on this ROM and does not have to be ported. Its
three table fields (`LandingCinematicSkipEntry`,
`LegacyLandingCinematicSkipEntry`, `LandingCinematicSkipStub`) are `0` in the
Kiosk table; zero means "no target on this build" and is treated as such by
the callers, never patched.

## Stub areas - assigned

### Why these addresses are safe

The `0x80066C00`...`0x80066F00` bases are not empty space: they fall in a run
of **static CPU trace functions**, right after `diCpuTraceInit`. The USA hack
preserves `diCpuTraceInit` itself - the only one called, 0x54 bytes - and
overwrites its helpers.

That criterion can be checked, and it holds identically on the Kiosk: the
five helpers have **zero callers outside the block**, in both images. They
are reachable only from `diCpuTraceInit`.

The run of functions matches almost exactly:

| # | USA | size | Kiosk | size |
| --- | --- | --- | --- | --- |
| 1 `diCpuTraceInit` | `80066B80` | 0x54 | `80067550` | 0x54 |
| 2 | `80066BD4` | 0xE8 | `800675A4` | 0xE8 |
| 3 | `80066CBC` | 0x6C | `8006768C` | 0x6C |
| 4 | `80066D28` | 0xEC | `800676F8` | 0xEC |
| 5 | `80066E14` | 0x9C | `800677E4` | 0xA4 |

Usable area in the Kiosk: `800675A4`..`80067920`, i.e. **0x37C bytes** - less
than the USA's 0xA90, hence the need to pack rather than keep the original
offsets.

### Assignment

Size retained per lot = the largest of the features sharing the base, these
being mutually exclusive.

| Lot | USA | **Kiosk** | Size | End |
| --- | --- | --- | --- | --- |
| `PlayerVelocity` / `FloydMoveHook` / `SidekickStrafe` | `80066C00` | **`800675B0`** | 0xC0 | `80067670` |
| `SidekickPadProbe` | `80066D80` | **`80067680`** | 0x34 | `800676B4` |
| `ObjectMove` | `80066E00` | **`800676C0`** | 0xD0 | `80067790` |
| `FloydCameraLateral` / `SidekickLateralMove` / `SidekickVelocityLateral` | `80066F00` | **`800677A0`** | 0xC4 | `80067864` |

No overlap, entirely within the safe area, **0xBC bytes to spare**.

`LandingCinematicSkipStub` (`0x80067000`) disappears: the feature does not
exist on this ROM, which frees precisely the room that was missing.

### Camera bases - padding of identical size

| Base | USA | **Kiosk** | Need | Padding |
| --- | --- | --- | --- | --- |
| `CameraHelperBase` | `800968CC` | **`8009350C`** | 0x40 | 0x48, after `osGetCount` |
| `CameraTopDownHelperBase` | `80098D08` | **`80098CC8`** | 0x18 | 0x18, after `osCreateViManager` |

Verified zero over the whole required range in both cases. `CameraHelperBase`
had also been found at the same address by the signature search - two
independent routes that agree.

### Squaddie stubs

In the 308-byte cave, so direct: `SquaddieXStub` `8009FD60` ->
**`800A05F0`**, `SquaddieZStub` `8009FD80` -> **`800A0610`**. Ranges verified
free.

## Historical note: the hard point, as it first presented itself

The `0x80066C00`, `0x80066D80`, `0x80066E00`, `0x80066F00` and `0x80067000`
bases **are not empty space**, in the USA image or in the Kiosk. The hack
installs its stubs there because a judgement was made that this code is
unreachable in the intended context. That judgement does not carry over
mechanically: it had to be redone on the Kiosk before writing anything at
those addresses (see "Why these addresses are safe" above).

The hack's scratch, on the other hand, is simple: the working variables live
in 308 bytes of padding at `0x8009FCAC` in the USA image, and the Kiosk has
padding **of identical size** at `0x800A053C`. The correspondence is direct,
offset for offset.

## State of the code

**The mapping is complete, the C++ table is written, and the code is
migrated.**

The 125 fields of `JFG_ADDRESSES` - 115 addresses and 10 instruction words -
now feed the whole hack. `ApplyAddressTable()` places the addresses,
recomputes the derived values and rebuilds the 241 entries of the patch
tables; `SelectAddressTable()` triggers it on a ROM change, and
`JfgAddresses()` picks the table from the loaded ROM's identifier.
`IsSupportedRom()` accepts both ROMs.

The stub payloads that carried an address are no longer literals: they are
**computed** from the table through `JumpTo`, `CallTo`, `WithHi` and
`WithLo`. The fifteen concerned were checked one by one - on the USA build the
computed expression gives back exactly the original literal.

One deliberate exception: `CameraCodePatches[37]` is an orphan `lui` whose
low half is supplied by the game code the stub jumps into. No pair here lets
the targeted global be named; both builds keep their `0x800F` globals in the
same 64 KB page, so the high half is the same.

Non-regression check: the 113 migrated constants were compared to the USA
column of the table, **zero divergence**.

What makes accepting the Kiosk reasonable rather than reckless:
`CGameHackCodePatcher::SetEnabled` checks every entry of a table before
writing any of them, and gives up on the first word that is neither the
expected original nor an already applied replacement. A mapping mistake
therefore leaves the feature switched off instead of corrupting code.

The features that save the original words themselves and write without that
check escape this net: object movement, player velocity and the sidekick
hooks. They are the ones to suspect first if the Kiosk misbehaves.

The widescreen HUD correction and the HUD alignment are still gated on the
USA table alone (`JfgAddresses() == &JfgUsAddresses`): their overlay
signatures have not been established on the Kiosk.

### The trap that cost two test sessions

A migration that looks for addresses does not find the **instruction words
that contain one**. A `j` or a `jal` starts with `0x08` or `0x0C`, not with
`0x8`: 21 constants of the form

```c
const uint32_t ObjectMoveJump = 0x08019B80; // j ObjectMoveStub
```

slipped through. The stub was written at its Kiosk address, but the hook
jumped to the **USA** address - the game went off into arbitrary code. Hence a
crash that only triggered with the enemy-speed option, the only one to install
that particular hook.

They are now computed by `JumpTo`/`CallTo` from the stub addresses. Still
frozen, deliberately: `ObjectMoveLegacyJump` and `ObjectMovePreviousJump`,
which serve to **detect** an old USA patch in a save state, and the two Floyd
constants whose target `0x80049C84` has no equivalent in the table - a
disabled feature.

The same mistake repeated once more, in another form: the load and store
**displacements**. A `lui`/`lwc1` pair carries the address in two halves, and
the `lui` had been handled without the low halves. The result was silent
rather than fatal: the host wrote the camera elevation to its Kiosk slot, but
the patched code read it back from the USA slot. The camera turned left and
right - that path does not go through a pair - and no longer went up or down.
Eighteen words were concerned.

The lesson is general, and it cost three test sessions: looking for addresses
is not enough, everything **that encodes one** has to be looked for - jump
target, high half, low half. The check that finds them all is to decode every
word of the tables and test whether the reconstructed value lands on a known
address, rather than trusting the shape of the literal.

## Validation order

A feature that **does nothing** on the Kiosk is the benign case: the
`SetEnabled` net has played and the address is to be reviewed. A crash or
erratic behaviour points instead to the unprotected hooks of step 4. When
re-validating after a change that touches the table:

1. **Check the USA non-regression first.** The migration touched the whole
   file; the USA ROM has to behave exactly as before. If it moved, the problem
   is in the migration, not in the port.
2. **Then the Kiosk, one feature at a time.** Start with 60 fps: it depends
   only on `SchedulerFrameGateAdd` and three signature words, all verified in
   the disassembly, and it shows immediately.
3. Then the camera and the reticle, whose sites were confirmed by disassembly
   on both sides.
4. Last, the hooks that write without a signature check - object movement,
   player velocity, sidekick - since they are the only ones where a mistake
   does not report itself.

## Tooling

Session scripts, to keep in case they are needed again: `jfgmap.py` (symbol
tables and ROM access), `jfgsig.py` / `jfgsig2.py` (masked signature search,
with and without register canonicalisation). The whole mapping can be
reproduced by running them again. They are not part of the repository.

## Correspondence table

Format: `constant  USA_address  Kiosk_address  method  note`. "cave" means
the hack's scratch area, equivalent padding of 308 bytes.

```text
AnimseqCameraAddress                       801045B8 80105288  symbol            animcamera+0x0
CameraActiveOverrideBase                   800F6E58 800F7918  symbol            controlchr_gravity+0x94
CameraArrayAddress                         800FA4D0 800FAF90  symbol            controlchr_gravity+0x370C
CameraCenterBranch                         8002D154 8002D160  signature 13/13   2nd candidate 5
CameraClampBranch                          8002D128 8002D134  signature 13/13   2nd candidate 3
CameraFovAddress                           800FB078 800FBB38  symbol            controlchr_gravity+0x42B4
CameraHeightBlendBase                      8002E51C 8002E528  signature 13/13   2nd candidate 7
CameraHeightOffsetAddress                  8009F248 8009FAD8  signature 12/13   2nd candidate 8
CameraLookHelperCall                       8002E814 8002E820  signature 12/13   2nd candidate 2
CameraNativeYAddress                       8009F244 8009FAD4  signature 12/13   2nd candidate 9
CameraOrbitBranch                          8002DF94 8002DFA0  signature 13/13   2nd candidate 3
CameraOrbitCenterBranch                    8002DED8 8002DEE4  signature 13/13   2nd candidate 5
CameraOrbitGateBranch                      8002DEC8 8002DED4  signature 13/13   2nd candidate 5
CameraPitchHelperCall                      8002EA5C 8002EA68  signature 12/13   2nd candidate 2
CameraPositionXBaseCall                    8002E0A8 8002E0B4  signature 12/13   2nd candidate 2
CameraPositionZBaseCall                    8002E0E0 8002E0EC  signature 12/13   2nd candidate 2
CameraTopDownCounterAddress                8009F24C 8009FADC  signature 12/13   2nd candidate 7
CameraTopDownHelperBase                    80098D08 80098CC8  signature 12/13   2nd candidate 5
CameraYawHelperCall                        8002EA34 8002EA40  signature 12/13   2nd candidate 2
ControlCameraAddress                       800F6DC0 800F7880  symbol            controlcam+0x0
CurrentScreenAddress                       800FECB0 800FF990  symbol            currentScreen+0x0
DisableJoyAddress                          800F6DBC 800F787C  symbol            disablejoy+0x0
DroneLateralDragAddress                    8009FCC0 800A0550  cave
DroneLateralFlagsAddress                   8009FCE4 800A0574  cave
DroneLateralForwardSpeedAddress            8009FCC4 800A0554  cave
DroneLateralHookFlagsAddress               8009FCEC 800A057C  cave
DroneLateralHookHitsAddress                8009FCE8 800A0578  cave
DroneLateralMaxSpeedAddress                8009FCA4 800A0534  signature 12/13   2nd candidate 5
DroneLateralPreviousObjectAddress          8009FCD8 800A0568  cave
DroneLateralPreviousXAddress               8009FCD0 800A0560  cave
DroneLateralPreviousZAddress               8009FCD4 800A0564  cave
DroneLateralRightXAddress                  8009FCC8 800A0558  cave
DroneLateralRightZAddress                  8009FCB0 800A0540  cave
DroneLateralSideFactorAddress              8009FCA8 800A0538  signature 12/13   2nd candidate 6
DroneLateralVelocityAddress                8009FCAC 800A053C  cave
EnemyHalveFlagAddress                      8009FCE0 800A0570  cave
FloydCameraPreviousObjectAddress           8009FCB8 800A0548  cave
FloydCameraPreviousXAddress                8009FCB0 800A0540  cave
FloydCameraPreviousZAddress                8009FCB4 800A0544  cave
FramePacing60Branch                        800550F8 80055BDC  signature 12/13   2nd candidate 2
FramePacing60SignatureBase                 800550F0 80055BD4  signature 12/13   2nd candidate 2
FramePacingEscalateStore                   800550E8 80055BCC  signature 12/13   2nd candidate 2
FramePacingSignatureBase                   800550DC 80055BC0  signature 12/13   2nd candidate 2
GeneralRenderListAddress                   800F2FB0 800F3A70  symbol            generalBuffer+0x0
LandingCinematicSkipInputAddress           8009FCBC 800A054C  cave
LobbyCameraInUseAddress                    800FB080 800FBB40  symbol            controlchr_gravity+0x42BC
ManualAimXVelocityStore                    8003AF14 8003ACDC  signature 13/13   2nd candidate 3
ManualAimYVelocityStore                    8003AF2C 8003ACF4  signature 13/13   2nd candidate 4
ObjectMoveEntry                            80009A24 800099FC  signature 12/13   2nd candidate 4
ObjectMoveResume                           80009A28 80009A00  signature 12/13   2nd candidate 3
OverlayTableAddress                        800FEAA0 800FF780  symbol            PatrolNodes+0x4
PlayerCountAddress                         800F2D10 800F3910  symbol            playerlist+0x4
PlayerListAddress                          800F2D0C 800F390C  symbol            playerlist+0x0
RobotMissionAddress                        800A3208 800A3A58  signature 13/13   2nd candidate 8
MultiplayerGameAddress                     800A4FC4 800A5994  lbu sidekickControl USA+0x58 / Kiosk+0x58
CooperativeGameAddress                     800A4FC8 800A5998  lbu sidekickControl USA+0x68 / Kiosk+0x68, sb +0x90/+0xBC
SidekickControlEnd                         8003109C 800310A8  signature 12/13   2nd candidate 2
SidekickControlEntry                       8002F728 8002F734  signature 13/13   2nd candidate 2
SidekickControlObjectAddress               8009FCA0 800A0530  signature 12/13   2nd candidate 4
SidekickControlProbeEntry                  8002F8AC 8002F8B8  signature 13/13   2nd candidate 2
SidekickControlProbeStub                   80066D00 800676D0  signature 12/13   2nd candidate 4
SidekickLateralMoveInputLegacyEntry        8002F8F4 8002F900  signature 12/13   2nd candidate 3
SidekickPadProbeActorAddress               8009FCFC 800A058C  cave
SidekickPadProbeObjectAddress              8009FCDC 800A056C  cave
SidekickPadProbeStateAddress               8009FCCC 800A055C  cave
SidekickStrafeDelay                        80030064 80030070  signature 13/13   2nd candidate 2
SidekickStrafeEntry                        80030060 8003006C  signature 13/13   2nd candidate 2
SidekickVelocityLateralDelay               8002F8B0 8002F8BC  signature 13/13   2nd candidate 3
SidekickVelocityLateralEntry               8002F8AC 8002F8B8  signature 13/13   2nd candidate 2
SidekickVelocityLateralResume              8002F8B4 8002F8C0  signature 13/13   2nd candidate 3
SquaddieXStub                              8009FD60 800A05F0  cave
SquaddieZStub                              8009FD80 800A0610  cave
StaticCameraInUseAddress                   800FB084 800FBB44  symbol            controlchr_gravity+0x42C0
TripleBufferActive                         800FECA6 800FF986  symbol            PatrolNodes+0x20A
TripleBufferRequest                        800551E8 80055CCC  signature 12/13   2nd candidate 4
WaterWakeCullingEntry                      800146E4 800144A8  signature 12/13   2nd candidate 3
WaterWakeDrawFallbackCalledAddress         8009FCF0 800A0580  cave
WaterWakeDrawFallbackStub                  8009FD00 800A0590  cave
WaterWakeFrameRateEntry                    8006B1A0 8006B4E4  signature 13/13   2nd candidate 5
WaterWakeGateCounter                       8009FCEC 800A057C  cave
WaterWakeGateStub                          8009FD00 800A0590  cave
WaterWakeGlobalFadeAddress                 800A6950 800A7310  signature 13/13   2nd candidate 4
WaterWakeLegacyCallSite                    80009278 80009260  signature 12/13   2nd candidate 5
WaterWakeObjectCountAddress                800F2CA8 800F38A8  symbol            ObjList+0x4
WaterWakeObjectListAddress                 800F2CA4 800F38A4  symbol            ObjList+0x0
WaterWakeRingRateEntry                     8006AAC8 8006AE0C  signature 13/13   2nd candidate 3
WaterWakeStockDrawCalledAddress            8009FCF8 800A0588  cave
WaterWakeStockDrawTargetAddress            8009FCF4 800A0584  cave
WaterWakeUpdate                            8006B090 8006B3D4  signature 13/13   2nd candidate 6
```
