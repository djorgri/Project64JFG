#include "stdafx.h"

#include "JetForceGemini.h"
#include "JetForceGeminiAddresses.h"
#include "JetForceGeminiHudBuild.h"
#include "JetForceGeminiHudPalOriginals.h"
#include "JetForceGeminiHudAlignment.h"
#include "JetForceGeminiFloydHud.h"
#include "JetForceGeminiRocketOverlay.h"
#include "JetForceGeminiHudRaster.h"
#include "JetForceGeminiMultiplayerHud.h"
#include <math.h>
#include <algorithm>
#include <cstdlib>

namespace
{
// Both supported builds and every address that differs between them are in
// JetForceGeminiAddresses, which is also where the US identifier now lives.
// The constants below are still the US ones, spelled out, so this file keeps
// working exactly as before while the migration to the table proceeds.
const int8_t JfgStickLimit = 80;
const float SprintMovementMultiplier = 1.25f;
const float SprintAnimationMultiplier = 1.10f;
const float SprintMaximumStep = 100.0f;
const uint64_t SprintRampMicroseconds = 500000;
const uint64_t SprintMaximumElapsedMicroseconds = 100000;
const uint32_t MouseButtonLeft = 0;
const uint32_t MouseButtonRight = 2;
// Live 30/60 FPS switch for testing: numpad + selects 60, numpad - selects 30.
// Both flip Setting_JfgTarget60Fps, which ProcessRuntimeFrame re-reads and
// re-applies every frame, so the mode changes on the spot.
const KeyboardMouseKey Fps60ToggleKey = (KeyboardMouseKey)87; // USB HID Keypad +
const KeyboardMouseKey Fps30ToggleKey = (KeyboardMouseKey)86; // USB HID Keypad -

const int32_t MouseAimYawSensitivity = 32;
const int32_t MouseAimPitchSensitivity = 32;
// Angles are 0x10000 to the turn, so these are roughly straight down to
// straight up. The stick could only reach about -15 to +30 degrees, which is
// what a mouse makes needlessly limiting.
const int32_t MouseAimPitchMin = -0x3F00;
const int32_t MouseAimPitchMax = 0x3F00;
// The live field of view camGetFOV returns, in degrees. Aiming down a sniper
// shrinks it, and the aim step is scaled by it so the smallest usable notch
// stays the same size on screen however far the view is zoomed in.
uint32_t CameraFovAddress = 0x800FB078;
// The drone turns at a rate set by how far the stick is pushed rather than by
// an angle we can write, so mouse travel becomes stick deflection instead.
const int32_t DroneStickSensitivity = 6;
// Matched to the on foot camera so the two feel alike under the hand.
const int32_t DroneCameraYawSensitivity = 64;
const int32_t DroneCameraPitchSensitivity = 64;
// The drone flies wherever the player object faces, and its camera heading is
// published from two synchronised copies of that facing inside the object, both
// holding it negated. Steering means adding to those rather than to the camera
// struct, which is only ever a render copy the game rewrites every frame. The
// render struct keeps pitch right after yaw, and these copies mirror it, so the
// pitch each drives sits one halfword past its yaw.
const uint32_t DroneYawSourceOffsetA = 0x1040;
const uint32_t DroneYawSourceOffsetB = 0x1158;
const uint32_t DronePitchSourceOffsetA = 0x1042;
const uint32_t DronePitchSourceOffsetB = 0x115A;
const int32_t MouseCameraYawSensitivity = 64;
const float MouseCameraHeightSensitivity = 1.5f;
// Gamepad sticks. Deflection below the dead zone is rest; past the digital
// threshold a stick direction also counts as the matching held key, which is
// what the C button strafes and the drone throttle read. Triggers fire a
// quarter of the way in. The right stick feeds the mouse bank at a rate of
// GamepadCameraCountsPerSpeed mouse counts per video frame for each step of
// Setting_JfgGamepadCameraSpeed, at full deflection.
const float GamepadStickDeadZone = 0.2f;
const float GamepadDigitalThreshold = 0.5f;
const int16_t GamepadTriggerThreshold = GamepadAxisMax / 4;
const float GamepadCameraCountsPerSpeed = 2.0f;
const uint32_t GamepadCameraSpeedMin = 1;
const uint32_t GamepadCameraSpeedMax = 10;
// The manual aim spends the same counts at half the camera's sensitivity, and
// a stick cannot make up the difference the way a mouse flick does, so the
// stick's counts are boosted while the trigger aims through the mouse scheme.
// Doubling them made the aim too twitchy; 1.5x keeps the boost noticeable
// without overshooting.
const float GamepadAimRateMultiplier = 1.5f;
const float MouseCameraHeightLimit = 200.0f;
const float CameraElevationMinTangent = 0.087488664f;
const float CameraElevationMaxTangent = 1.732050808f;
const float N64AngleToRadians = 0.000095873799f;
const int32_t TopDownHoldPolls = 6;

uint32_t CameraClampBranch = 0x8002D128;
uint32_t CameraCenterBranch = 0x8002D154;
uint32_t CameraOrbitGateBranch = 0x8002DEC8;
uint32_t CameraOrbitCenterBranch = 0x8002DED8;
uint32_t CameraOrbitBranch = 0x8002DF94;
uint32_t CameraPositionXBaseCall = 0x8002E0A8;
uint32_t CameraPositionZBaseCall = 0x8002E0E0;
uint32_t CameraHeightBlendBase = 0x8002E51C;
uint32_t CameraLookHelperCall = 0x8002E814;
uint32_t CameraYawHelperCall = 0x8002EA34;
uint32_t CameraPitchHelperCall = 0x8002EA5C;
// dAngle, the angle helper the stock camera code calls. Our stub jumps to it,
// so it has to follow the build like any other address.
uint32_t CameraAngleHelper = 0x80033FA4;
uint32_t CameraHelperBase = 0x800968CC;
uint32_t CameraTopDownEntry = 0x8002EB6C;
uint32_t CameraTopDownHelperBase = 0x80098D08;
uint32_t ManualAimCursorXStore = 0x8003B014;
uint32_t ManualAimCursorYStore = 0x8003B058;
uint32_t ManualAimXVelocityStore = 0x8003AF14;
uint32_t ManualAimYVelocityStore = 0x8003AF2C;
// Two overlays the manual aim reaches into. The US build first spelled them as
// the fixed addresses those modules occupy there in normal play (module 13 at
// 0x80348120, module 16 at 0x803585F0); they are now resolved from the live
// overlay table, which is what lets another build's layout apply at all.
// Module 13 holds frontPlayerTarget and frontDrawTarget, module 16 Juno's arm
// aim, whose two copies of the same four loads the mouse aim rewrites.
const uint32_t TargetOverlayModule = 13;
uint32_t TargetOverlayCursorXOffset = 0x41C;
uint32_t TargetOverlayCursorYOffset = 0x444;
uint32_t TargetOverlayDrawOffset = 0x4A8;
const uint32_t BoyAimOverlayModule = 16;
uint32_t BoyAimHelperOffset = 0x3DB0;
uint32_t BoyAimFirstGroupOffset = 0x4198;
uint32_t BoyAimSecondGroupOffset = 0x4738;
// Addresses from the US symbol map of the Ryan-Myers/Jet-Force-Gemini
// decompilation. gVideoDeltaTime at 0x800FECAC is the minimum number of VI
// periods per rendered frame: 1 = 60fps, 2 = 30fps, 3 = 20fps.
//
// viFrameSync escalates gVideoDeltaTime from 2 (30fps) to 3 (20fps) after a
// single slow frame, then needs 31 consecutive fast frames to come back. On an
// emulator with headroom to spare that trades a brief spike for half a second
// at 20fps, and during sustained action it ping pongs between the two.
// Neutralising the store leaves the rest of the function alone: viFrameSync
// still returns the real frame time to its caller, and the blocking loop
// "while (tempUpdateRate < gVideoDeltaTime)" simply stops forcing extra waits.
uint32_t FramePacingEscalateStore = 0x800550E8;
uint32_t FramePacingSignatureBase = 0x800550DC;

// viFrameSync already ends with "if (runInOneFrame) gVideoDeltaTime = 1", the
// 60fps path the engine uses for its own purposes. Dropping the branch makes
// that store unconditional, and it sits just before the reload feeding the
// "while (tempUpdateRate < gVideoDeltaTime)" wait, so the game stops being held
// to one frame out of two. It renders as fast as it can build a frame instead,
// which is 60fps only while it keeps up.
uint32_t FramePacing60Branch = 0x800550F8;
uint32_t FramePacing60SignatureBase = 0x800550F0;

// fb_swap rotates the framebuffer pointers once per rendered frame, so watching
// currentScreen change counts actual frames rather than display lists.
uint32_t CurrentScreenAddress = 0x800FECB0;

// __scHandleRetrace holds the finished graphics task and only signals its
// completion queue once "sc->frameCount >= 2", so the game loop is released
// every second retrace. Counting two per retrace instead of one satisfies that
// on the first one. The decompilation flags this very line as the 60fps knob.
// The function is static and has no symbol; it sits between osScGetAudioSPStats
// and __scTaskReady, and the store was located by disassembling that range.
uint32_t SchedulerFrameGateAdd = 0x800506D8;
uint32_t SchedulerSignatureBase = 0x800506D0;

// The generic object list objObjectsTick walks (the water wake lives on it, hence
// the name), and its count. HalveNamedEnemyMovement scans it by model name.
uint32_t WaterWakeObjectListAddress = 0x800F2CA4;
uint32_t WaterWakeObjectCountAddress = 0x800F2CA8;
const uint32_t WaterWakeObjectLimit = 1024;

// The actual 60fps defect, found by disassembling wakeAllocate. It stores the
// sample lifetime in wake+0x03 as "seconds * 60" ticks but sizes the sample
// ring in wake+0x01 at only "(ticks + 5) / 2" slots, because the stock game
// steps the wake by a delta of two once per frame. wakeUpdate still appends
// exactly one sample per call, so at 60fps -- one call, delta of one -- samples
// survive twice as many calls and the ring saturates after about half the
// configured lifetime.
//
// Saturation is a latch, not a flicker. A full ring puts the write index
// wake+0x3A on the read index wake+0x39, so the "beq $s6, $t6" at 0x8006AC04
// skips the whole geometry pass. That pass also holds the only "life -= delta"
// (0x8006ACE0), so with it skipped nothing ages, the expiry loop at the top of
// wakeUpdate never frees a slot, and the ring can never drain. wakeDraw is
// still called and still sees its object -- it just reads a single geometry
// record with a zero vertex span, which is the "stock1 same1" probe result.
//
// Appending on alternate calls restores the stock occupancy of "lifetime / 2"
// samples and the two spare slots the "+ 5" was there to provide. The walk
// still runs every frame with a delta of one, so the trail keeps its correct
// wall-clock lifetime and 60fps motion; only its sampling returns to 30Hz,
// which is exactly the stock geometry. wake+0x02 is the engine's own
// per-call toggle for the double-buffered vertex arrays, so it is already
// alternating and needs no state of its own.
uint32_t WaterWakeRingRateEntry = 0x8006AAC8;

// Enemy movement: objMoveXYZ below catches movers that add a step directly to
// their transform; HalveNamedEnemyMovement handles the named fliers whose own
// movers bypass it.

// objMoveXYZ(obj, dx, dy, dz) adds the three deltas straight onto the object
// transform, so its callers pre-scale them. Many Overlay Control functions pass
// a per frame step without weighting it, which at 60fps moves those enemies
// twice as fast. Halving the deltas inside the helper fixes them, and unlike
// skipping calls it drops no hit or timer event: every function still runs
// every frame, only the distance changes. Callers that DO weight their step by
// the game delta (e.g. laserboltControl, whose movement and lifetime both scale
// by it) are already frame-rate-correct and must be excluded by behaviorId in
// the stub below, or halving would give them half range. The hook sits after the call at
// 0x80009A1C so the arguments are already on the stack, and scales them in
// place before they are read back. Its code is installed over diCpuTraceInit:
// that one-off debug-thread initializer has completed before gameplay starts,
// unlike the old 0x8009FDxx location which is RSP microcode rather than RAM.
uint32_t ObjectMoveEntry = 0x80009A24;
uint32_t ObjectMoveResume = 0x80009A28;
uint32_t ObjectMoveStub = 0x80066E00;
uint32_t ObjectMoveJump = 0x08019B80; // j ObjectMoveStub
const uint32_t ObjectMoveLegacyJump = 0x08027F40;
const uint32_t ObjectMovePreviousJump = 0x08027F80;
// 0x8009FDxx is reused by RSP microcode. Keep the hook next to the proven
// objMoveXYZ code cave instead, after its 0x64-byte dispatcher.
uint32_t DroneLateralFlagsAddress = 0x8009FCE4;
uint32_t DroneLateralHookHitsAddress = 0x8009FCE8;
uint32_t EnemyHalveFlagAddress = 0x8009FCE0;
uint32_t DroneLateralPreviousObjectAddress = 0x8009FCD8;
const uint32_t DroneLateralActive = 1;
const uint32_t DroneLateralRight = 2;
// Lateral thruster input, written every controller poll, and the drag and speed
// cap the stub applies to the velocity it accumulates from it.
uint32_t DroneLateralSideFactorAddress = 0x8009FCA8;
uint32_t DroneLateralDragAddress = 0x8009FCC0;
uint32_t DroneLateralMaxSpeedAddress = 0x8009FCA4;
// Our lateral velocity. The engine has none to borrow, since it stores movement
// as heading times speed, so this is the one piece of state the stub owns.
uint32_t DroneLateralVelocityAddress = 0x8009FCAC;
// The flight hook's vertical thrust and its persistent vertical velocity.
uint32_t DroneVerticalThrustAddress = 0x8009FCD0;
uint32_t DroneVerticalVelocityAddress = 0x8009FCD4;
// The axis the thrust pushes along, from the camera rather than from
// sk->0x28/0x30: that pair is the direction Floyd is travelling, which drifts
// away from where the player is looking.
//
// Calibrated against the engine's own heading while flying straight, where the
// heading is the camera forward. At a raw yaw of -3568 the heading read
// (-0.335, +0.942), which is (sin, cos) of -19.60 degrees, so the published
// value maps directly with no negation despite being stored negated for the
// camera struct. The right vector is then the same (fz, -fx) perpendicular
// already confirmed in game.
uint32_t DroneLateralForwardSpeedAddress = 0x8009FCC4;
uint32_t DroneLateralRightXAddress = 0x8009FCC8;
uint32_t DroneLateralRightZAddress = 0x8009FCB0;
const float DroneLateralAngleScale = 6.283185307f / 65536.0f;
// Thruster strength, the fraction of the lateral velocity kept each frame, and
// the lateral speed cap. The engine's own forward speed is set to 8.0f at
// 0x8003045C and clamped to 2.0f at 0x8002FFEC, so those two are the anchors to
// match the forward axis against.
//
// The integrator settles at thrust * step / (1 - drag), so the ratio sets the
// top speed while thrust alone sets how hard it pulls away from rest. Halving
// both thrust and 1 - drag keeps the same top speed and softens the onset,
// which is the pair to move when the acceleration feels too sharp.
//
// The engine's forward acceleration is accel * (1 - k) at 0x8002FE58, where k
// comes from 0x80049BEC and is already compensated for the update rate. Ours is
// not, so the settling point scales with the frame step; the cap is what keeps
// the two update rates from diverging.
const float DroneLateralSideThrust = 0.15f;
const float DroneLateralDrag = 0.975f;
// Also the cap on forward, lateral and vertical speeds combined, matching the value
// the engine sets for its own forward axis at 0x8003045C.
const float DroneLateralMaxSpeed = 8.0f;
// The PAL build updates at 50 Hz with the same frame step, and keeps the NTSC
// pace by scaling its own speed constants by 60/50: Floyd's forward cap at
// 0x8003045C becomes 9.6, loaded from rodata. The thrusters follow suit so they
// feel the same in real time: the drag compounds over 1.2 NTSC frames
// (0.975^1.2), the cap scales like the engine's, and the thrust is the one that
// keeps the NTSC terminal speed, 360 units a second, under that drag.
const float PalDroneLateralSideThrust = 0.2154564f;
const float PalDroneLateralDrag = 0.9700755f;
const float PalDroneLateralMaxSpeed = 9.6f;
// Samples taken once per video interrupt run 50 times a second on PAL.
const float PalVideoRateScale = 60.0f / 50.0f;

// The first word of each 0x20-byte overlay table entry is its relocated RAM
// base.
uint32_t OverlayTableAddress = 0x800FEAA0;
uint32_t CurrentSceneAddress = 0x800A323C;
uint32_t CurrentSetupAddress = 0x800A3248;
uint32_t NextCharacterAddress = 0x800A3260;
uint32_t LoadingAddress = 0x800A3294;
const uint32_t OverlayHeaderSize = 0x20;
const uint32_t FloydOverlayModule = 22;
uint32_t FloydPadControlOffset = 0x2A4;
// sidekickControl integrates Floyd's own flight inline; it never calls
// objMoveXYZ. sk = obj->0x68 holds the unit heading at 0x28/0x2C/0x30 and the
// speed scalar at 0x34, and 0x8003001C..0x800300A8 turns those into
// obj->0x1C/0x20/0x24 before adding them to sk->0x00/0x04/0x08. That block is
// the only place Floyd moves, which is why every earlier hook missed it.
//
// The engine has an acceleration input at 0x8002FD8C whose local X slot sits
// unused, but feeding it does not strafe. sidekickControl stores movement as
// heading times speed, not as a velocity: 0x8002FDC8 rebuilds the velocity from
// the heading and 0x8002FED0 turns the result back into one. Any component
// pushed sideways is absorbed into the heading within the frame, so the drone
// pivots onto it and flies straight again. That model cannot represent a
// velocity that is not aligned with the facing.
//
// 0x8003001C is where the two separate: it expands heading times speed into
// obj->0x1C/0x20/0x24 and the position integration consumes that, while the
// facing keeps reading the heading. Adding a lateral term there moves Floyd
// sideways without turning him, which is why the earlier rotation worked.
//
// The engine has no lateral velocity to borrow, so this keeps its own at
// 0x8009FCAC and integrates it against the frame step, then applies it along
// the perpendicular of the current heading. Inertia and drag are therefore
// ours; the engine's own speed cap and collision still bound the forward axis.
//
// The displaced instruction moves into the jump delay slot and the stub resumes
// at entry + 8, replaying the instruction it overwrote. Resuming at entry + 4
// instead would jump straight into the delay slot of our own jump, which leaves
// the recompiler building a block that starts on a delay slot and hangs.
uint32_t SidekickStrafeEntry = 0x80030060;
uint32_t SidekickStrafeDelay = 0x80030064;
const uint32_t SidekickStrafeEntryOriginal = 0xC7A400AC; // lwc1 $f4, 0xAC($sp)
const uint32_t SidekickStrafeDelayOriginal = 0xC6080000; // lwc1 $f8, 0x0($s0)
// The lateral half reuses the retired sidekickpadMovePlayer cave. The vertical
// half uses the retired camera/velocity hook cave, keeping both clear of the
// active pad probe and object-movement dispatcher on US and Kiosk.
uint32_t SidekickStrafeStub = 0x80066C00;
uint32_t SidekickVerticalStub = 0x80066F00;
uint32_t SidekickStrafeJump = 0x08019B00; // j SidekickStrafeStub
uint32_t SidekickStrafeResumeJump = 0x0800C01A; // j 0x80030068
uint32_t SidekickPadProbeStub = 0x80066D80;
uint32_t SidekickPadProbeJump = 0x08019B60; // j SidekickPadProbeStub
uint32_t SidekickPadProbeObjectAddress = 0x8009FCDC;
uint32_t SidekickPadProbeStateAddress = 0x8009FCCC;
uint32_t SidekickPadProbeActorAddress = 0x8009FCFC;
const uint32_t SidekickPadProbeEntryOriginal = 0x27BDFF98; // addiu sp, sp, -0x68
const uint32_t SidekickPadProbeSecondOriginal = 0xAFBF0034; // sw ra, 0x34(sp)


// Defined with the address table below; forward declared so the stub listings
// in between can spell their addresses from the table rather than as US words.
uint32_t JumpTo(uint32_t Target);
uint32_t CallTo(uint32_t Target);
uint32_t WithHi(uint32_t Instruction, uint32_t Address);
uint32_t WithLo(uint32_t Instruction, uint32_t Address);

// The runtime's scratch cave, 308 bytes of padding, is laid out word for word
// the same in every build; only its base moves (0x8009FCA0 on US). Stubs are
// listed in US terms, reaching the cave through a `lui $rX, 0x800A` and a
// negative low half, so a word of such a stub is relocated by keeping its offset
// in the cave. Use this only on stubs whose every 0x800A upper half is the
// cave's: the landing skip's playState globals share that upper half on US.
const uint32_t UsScratchCaveBase = 0x8009FCA0;
const uint32_t ScratchCaveSize = 0x160;
uint32_t SidekickControlObjectAddress = 0x8009FCA0;

uint32_t ScratchCaveWord(uint32_t Word)
{
    const uint32_t Opcode = Word >> 26;
    if (Opcode == 0x0F && (Word & 0xFFFF) == 0x800A)
    {
        // Every address of the cave shares one upper half in every build.
        return WithHi(Word, SidekickControlObjectAddress);
    }
    const bool LoadStore = Opcode == 0x20 || Opcode == 0x21 || Opcode == 0x23 || Opcode == 0x24 ||
                           Opcode == 0x25 || Opcode == 0x28 || Opcode == 0x29 || Opcode == 0x2B ||
                           Opcode == 0x31 || Opcode == 0x39;
    const uint32_t UsAddress = 0x800A0000 + (uint32_t)(int32_t)(int16_t)(Word & 0xFFFF);
    if (LoadStore && UsAddress >= UsScratchCaveBase && UsAddress < UsScratchCaveBase + ScratchCaveSize)
    {
        return WithLo(Word, SidekickControlObjectAddress + (UsAddress - UsScratchCaveBase));
    }
    return Word;
}

// The US controller loop calls joyGetButtons at 0x80045FD4, combines all held
// controller states at 0x80045FE4, and stores that aggregate at 0x40(sp) in the
// delay slot of joyGetPressed. The native JP landing-skip check happens at this
// precise point in its corresponding loop. In US, the next two instructions
// are displaced by a small subroutine which returns to the second one, keeping
// the original start-button handling byte-for-byte intact.
uint32_t LandingCinematicSkipEntry = 0x80045FF0;
const uint32_t LandingCinematicSkipEntryOriginal = 0x02228825; // addu $s1, $s1, $v0
const uint32_t LandingCinematicSkipEntryDelayOriginal = 0x32381000; // andi $t8, $s1, 0x1000
uint32_t LandingCinematicSkipStub = 0x80067000;
uint32_t LandingCinematicSkipStubTable = LandingCinematicSkipStub + 0x160;
// The routines the landing stub calls, see LandingCinematicSkipStubCode.
uint32_t MainFrontInitFunction = 0x80047608;
uint32_t FrontCharSelectSetQuitModeFunction = 0x8005AAE8;
uint32_t FrontGetModeFunction = 0x80058A5C;
uint32_t MainChangeLevelFunction = 0x8004665C;
uint32_t LandingCinematicSkipJump = 0x0C019C00; // jal LandingCinematicSkipStub
// This free word sits with the existing JFG runtime scratch values. It carries
// the physical E/Return state from the keyboard/mouse path directly to MIPS,
// rather than depending on the later PIF controller aggregation.
uint32_t LandingCinematicSkipInputAddress = 0x8009FCBC;

// The boot logo sequence is handled by frontRarepage in the US overlay_57. The
// front dispatcher selects the subsequent page from its active mode: at boot
// this is frontStartScreen (the Solo / Multiplayer / Options title screen). The
// runtime overlay table includes the main module at index zero, so overlay_57 is
// module 0x39 even though the raw ROM table entry is 0x38.
//
// Do not bypass frontRarepage itself. It owns the fades, FMV compositor, cleanup
// and frontend state transition. Patch just after the prologue has built the
// state pointer in s0, force states 1-6 to the FMV terminal state when E/Return
// is held, then run the original switch and let the native code perform the
// handoff to the 3D main menu.
const uint32_t IntroCinematicSkipOverlayModule = 0x39;
uint32_t IntroCinematicSkipEntryOffset = 0xD0;
uint32_t IntroCinematicSkipResumeOffset = 0xD8;
const uint32_t IntroCinematicSkipEntryOriginal = 0x8E0E0000; // lw $t6, 0($s0)
const uint32_t IntroCinematicSkipEntryDelayOriginal = 0xAFBF0024; // sw $ra, 0x24($sp)
uint32_t IntroCinematicSkipStub = 0x80067200;
uint32_t IntroCinematicSkipJump = 0x08019C80; // j IntroCinematicSkipStub
const size_t IntroCinematicSkipResumeJumpIndex = 25;
uint32_t LegacyIntroCinematicSkipEntryOffset = 0xC0;
const uint32_t LegacyIntroCinematicSkipEntryOriginal = 0x27BDFF90; // addiu $sp, $sp, -0x70
const uint32_t LegacyIntroCinematicSkipEntryDelayOriginal = 0xAFB00020; // sw $s0, 0x20($sp)
uint32_t LegacyIntroCinematicSkipFmvUpdateOffset = 0x37C;
uint32_t LegacyIntroCinematicSkipCall = 0x0C019C80; // jal IntroCinematicSkipStub
const uint32_t LegacyIntroCinematicSkipFmvUpdateAddress = 0x8009FC90;

// s0 points at frontRarepage's state word when this stub runs. The FMV player
// pointer sits immediately before it, and the six alpha/timer words after it
// drive the first three logo screens. Clearing those values avoids drawing stale
// logo layers after a skip from screen one or two.
const uint32_t IntroCinematicSkipHookCode[] =
{
    0x3C01800A, // lui   $at, 0x800A
    0x8C28FCBC, // lw    $t0, 0xFCBC($at)   ; direct E / Return request
    0x11000014, // beq   $t0, $zero, resume_original
    0x00000000, // nop
    0x8E0E0000, // lw    $t6, 0($s0)        ; frontRarepage state
    0x11C00011, // beq   $t6, $zero, resume_original
    0x00000000, // nop
    0x2DC10007, // sltiu $at, $t6, 7        ; only states 1..6 are skippable
    0x1020000E, // beq   $at, $zero, resume_original
    0x00000000, // nop
    0x8E0FFFFC, // lw    $t7, -4($s0)       ; live FMV player
    0x11E0000B, // beq   $t7, $zero, resume_original
    0x00000000, // nop
    0x240E0006, // addiu $t6, $zero, 6      ; enter native FMV update case
    0xAE0E0000, // sw    $t6, 0($s0)
    0xAE000004, // sw    $zero, 4($s0)
    0xAE000008, // sw    $zero, 8($s0)
    0xAE00000C, // sw    $zero, 0x0C($s0)
    0xAE000010, // sw    $zero, 0x10($s0)
    0xAE000014, // sw    $zero, 0x14($s0)
    0xAE000018, // sw    $zero, 0x18($s0)
    0x24192500, // addiu $t9, $zero, 0x2500 ; terminal FMV counter
    0xADF90004, // sw    $t9, 4($t7)
    0x8E0E0000, // resume_original: lw $t6, 0($s0)
    0xAFBF0024, // sw    $ra, 0x24($sp)
    0x08000000, // j     frontRarepage + 0xD8 (patched at install time)
    0x00000000, // nop
};

// The stock widescreen mode renders at 320x180 and is then stretched back to
// 4:3 by the VI. Its 3D projection is correct, but the HUD is built with the
// same uncorrected horizontal scale. Keep this prototype scoped to the body of
// overlay 14's frontSingleInstruments: its call at +0xC00 establishes
// the first HUD orthographic matrix after the reticle, and every return path
// converges on the epilogue at +0x10D0.
//
// Every address and listing below is spelled in US terms and translated for
// the PAL build where it is used, see JetForceGeminiHudBuild.h; .Us is the US
// spelling, for the compile-time layout checks.
//
// Reuse the same retired diagnostic region as the other US JFG trampolines,
// but only after gameplay and overlay 14 are live. Writing this while the boot
// thread is still running diCpuTraceInit prevents a cold boot, so the runtime
// readiness gate below is part of the cave's safety contract.
//
// The scope counter deliberately lives on a different page: storing it twice
// per HUD pass on the code page would continually invalidate every stub under
// the recompiler's self-modifying-code tracking.
constexpr JfgHudBuild::UsAddress WidescreenHudCaveStart = { 0x80067280 };
constexpr JfgHudBuild::UsAddress WidescreenHudCaveEnd = { 0x80067690 };
const size_t WidescreenHudMainCaveWordCount =
    (WidescreenHudCaveEnd.Us - WidescreenHudCaveStart.Us) / sizeof(uint32_t);
// A second, disjoint segment reuses the retired diagnostic ring-buffer display.
// Its sole caller at 0x800674BC is already replaced by SpriteScaleAltCode.
// Floyd also uses the adjacent malloc-fault report after retiring its entry.
// The original report is restored in full, including after loading a saved hook.
// Keep the intervening diCpuLogMessage code entirely outside our memory image.
constexpr JfgHudBuild::UsAddress WidescreenHudReticleStub = { 0x80067790 };
constexpr JfgHudBuild::UsAddress WidescreenHudReticleWeaponStub = { 0x8006738C };
constexpr JfgHudBuild::UsAddress WidescreenHudReticleCaveEnd = { 0x80067950 };
const size_t WidescreenHudOldCaveWordCount = WidescreenHudMainCaveWordCount +
    (WidescreenHudReticleCaveEnd.Us - WidescreenHudReticleStub.Us) / sizeof(uint32_t);
const size_t WidescreenHudCaveWordCount = WidescreenHudOldCaveWordCount +
    sizeof(JfgRocketOverlay::Code) / sizeof(uint32_t);

uint32_t WidescreenHudCaveWordAddress(size_t Index)
{
    if (Index >= WidescreenHudOldCaveWordCount)
        return JfgRocketOverlay::Start + (uint32_t)((Index - WidescreenHudOldCaveWordCount) * 4);
    return Index < WidescreenHudMainCaveWordCount ?
        WidescreenHudCaveStart + (uint32_t)(Index * sizeof(uint32_t)) :
        WidescreenHudReticleStub + (uint32_t)((Index - WidescreenHudMainCaveWordCount) * sizeof(uint32_t));
}
constexpr JfgHudBuild::UsAddress WidescreenHudScopeEnterStub = { 0x80067280 };
constexpr JfgHudBuild::UsAddress WidescreenHudScopeExitStub = { 0x800672A0 };
constexpr JfgHudBuild::UsAddress WidescreenHudCamCopyStub = { 0x800672C0 };
constexpr JfgHudBuild::UsAddress WidescreenHudFontYStub = { 0x80067300 };
constexpr JfgHudBuild::UsAddress WidescreenHudDigitalAdvanceStub = { 0x80067340 };
constexpr JfgHudBuild::UsAddress WidescreenHudFontDtdyStub = { 0x80067360 };
constexpr JfgHudBuild::UsAddress WidescreenHudSpriteScaleStub = { 0x800673A0 };
constexpr JfgHudBuild::UsAddress WidescreenHudLineStub = { 0x800673E0 };
constexpr JfgHudBuild::UsAddress WidescreenHudRectangleStub = { 0x80067440 };
constexpr JfgHudBuild::UsAddress WidescreenHudShotGaugeWrapperStub = { 0x800677F4 };
constexpr JfgHudBuild::UsAddress WidescreenHudShotGaugeAnchorStub = { 0x80067810 };
constexpr JfgHudBuild::UsAddress WidescreenHudFloydLineStub = { 0x80067844 };
constexpr JfgHudBuild::UsOffset WidescreenHudShotGaugeCallOffset = { 14, 0x2B28 };
constexpr JfgHudBuild::UsWord WidescreenHudShotGaugeCallOriginal = { 0x0C01657D };
const uint32_t WidescreenHudShotGaugeCallDelay = 0x00003825;
constexpr JfgHudBuild::UsAddress WidescreenHudSpriteScaleAltStub = { 0x800674A0 };
constexpr JfgHudBuild::UsAddress WidescreenHudSpritePositionStub = { 0x800674E0 };
constexpr JfgHudBuild::UsAddress WidescreenHudMatrixTranslateStub = { 0x80067560 };
// Extend into the first 0x80 bytes of the unused diCpuReportWatchpoint.
// The next original function starts at 0x800676B4; no other stub occupies it.
constexpr JfgHudBuild::UsAddress WidescreenHudAmmoStub = { 0x80067610 };
constexpr JfgHudBuild::UsAddress WidescreenHudAmmoEntry = { 0x8005900C };
const uint32_t WidescreenHudAmmoOriginal = 0x00135080;
const uint32_t WidescreenHudAmmoDelayOriginal = 0x030A4021;
// cpuTraceTrackBufStatus is the byte at 0x80102550; the following three bytes
// are alignment padding before the pointer at 0x80102554. Claim only the last
// padding byte so a non-zero diagnostic status can never make the HUD hooks
// appear active outside their scope.
constexpr JfgHudBuild::UsAddress WidescreenHudScopeDepthAddress = { 0x80102553 };
constexpr JfgHudBuild::UsAddress WidescreenHudResolutionIndexAddress = { 0x800FECA8 };

bool IsWidescreenHudResolution(uint8_t Resolution)
{
    // Active gameplay modes: 0/2 are 4:3, 1/3 are widescreen (8..11 on PAL,
    // see JfgHudBuild::VideoMode). Do not treat an odd boot/reset mode or the
    // graphics plugin's aspect ratio as the game's widescreen setting.
    return JfgHudBuild::IsWideVideoMode(Resolution);
}

const uint32_t WidescreenHudOverlayModule = 14;
constexpr JfgHudBuild::UsOffset WidescreenHudOverlayEnterOffset = { 14, 0xC00 };
constexpr JfgHudBuild::UsOffset WidescreenHudOverlayExitOffset = { 14, 0x10D0 };
constexpr JfgHudBuild::UsWord WidescreenHudOverlayEnterOriginal = { 0x0C010359 }; // jal camStandardOrtho
const uint32_t WidescreenHudOverlayEnterDelayOriginal = 0x02002025; // or a0, s0, zero
const uint32_t WidescreenHudOverlayExitOriginal = 0x03E00008; // jr ra
const uint32_t WidescreenHudOverlayExitDelayOriginal = 0x27BD00B0; // addiu sp, sp, 0xB0
const uint32_t WidescreenHudReticleOverlayModule = 13;
constexpr JfgHudBuild::UsOffset WidescreenHudReticleDrawOffset = { 13, 0x4A8 };
constexpr JfgHudBuild::UsWord WidescreenHudReticleLineCallOriginal = { 0x0C01B563 }; // jal fxDrawLineInWindow
struct WIDESCREEN_HUD_RETICLE_CALL
{
    uint32_t Offset;
    uint32_t Delay;
};
// Only the local, rotating/mirrored segment paths (types 0..4). The four
// screen-edge frame calls (types 5/6) and the 3D target geometry stay stock.
const WIDESCREEN_HUD_RETICLE_CALL WidescreenHudReticleCalls[] =
{
    { 0xC68, 0x02C08025 },
    { 0xCF4, 0xAFAB0010 },
    { 0xD48, 0xAFB10010 },
    { 0xD9C, 0xAFB80010 },
    { 0xDFC, 0xAFB00014 },
    { 0xE4C, 0xAFA30010 },
    { 0xE98, 0xAFB10010 },
};

// Recognition/removal only: retire the previous experimental style-2 slope
// patch, including snapshots made by that build. All CPU line styles now use
// the original renderer. Local reticle segments now use the host overlay.
const GAME_HACK_CODE_PATCH WidescreenHudReticleRasterRetired[] =
{
    { 0x8006E8B0, 0x02602025, 0x02602025 }, // move a0,s3 ; current pixel
    { 0x8006E8B4, 0x026F082B, 0x026F082B },
    { 0x8006E8B8, 0x1420007E, 0x00000000 }, // endpoints were already clipped
    { 0x8006E8BC, 0x33C500FF, 0x33C500FF }, // andi a1,fp,255 ; native red
    { 0x8006E8C0, 0x0010C040, 0x0010C040 }, // s0 is now the general major stride
    { 0x8006E8C4, 0xAFB80048, 0xAFB80048 },
    { 0x8006E8C8, 0x0C01B6BA, 0x0C01B6BA }, // jal PlotAddRG
    { 0x8006E8CC, 0x32E600FF, 0x32E600FF }, // andi a2,s7,255 ; native green
    { 0x8006E8D0, 0x8FAA00CC, 0x0801B9FC }, // j general slope advance
    { 0x8006E8D4, 0x8FB90048, 0x8FA800D4 }, // lw t0,0xd4(sp) ; restore accumulator
    { 0x8006E4C0, 0x102000CB, 0x102000FB }, // style 2 uses the thin pixel call
    { 0x8006E4C4, 0x000B5880, 0x000B5880 }, // preserve other styles' table index
    { 0x8006E840, 0x00035B00, 0x00035B00 }, // original entry delay is harmless
    { 0x8006E83C, 0x8FB800A0, 0x0801B8D4 }, // j general slope setup
};

// frontDrawTarget passes projected/rotated endpoints and its aim centre in s4.
// The legacy image compressed each X offset by 3/4 before clipping. Keep that
// image and the current stub's layout recognizable for saved-code migration.
// The live dispatcher below now captures native endpoints for every weapon;
// its old compression arithmetic is unreachable.
// The original call delay has run; this tail call preserves ra, stack arguments,
// Y, colour, all saved registers, HI/LO and the FPU. t0/t1 are caller-saved.
// Normal reticles run outside the HUD scope. If a diagnostic forces that scope
// open, leave the existing fxDrawLine correction to act once, not twice.
// Keep the old full image recognizable if a state replaces an owned cave.
const uint32_t WidescreenHudReticleLegacyCode[] =
{
    0x3C088010, // lui   t0, 0x8010
    0x91092553, // lbu   t1, 0x2553(t0) ; scope depth
    0x15200014, // bne   t1, zero, stock
    0x9108ECA8, // lbu   t0, -0x1358(t0) ; resolution index
    0x31080001, // andi  t0, t0, 1
    0x11000011, // beq   t0, zero, stock
    0x00000000, // nop
    0x00944023, // subu  t0, a0, s4 ; first endpoint relative to aim centre
    0x00084840, // sll   t1, t0, 1
    0x01094021, // addu  t0, t0, t1 ; 3*dx
    0x00084FC3, // sra   t1, t0, 31 ; -1 for negative, 0 otherwise
    0x25080002, // addiu t0, t0, 2
    0x01094021, // addu  t0, t0, t1 ; symmetric rounding before /4
    0x00084083, // sra   t0, t0, 2
    0x01142021, // addu  a0, t0, s4
    0x00D44023, // subu  t0, a2, s4 ; second endpoint
    0x00084840, // sll   t1, t0, 1
    0x01094021, // addu  t0, t0, t1
    0x00084FC3, // sra   t1, t0, 31
    0x25080002, // addiu t0, t0, 2
    0x01094021, // addu  t0, t0, t1
    0x00084083, // sra   t0, t0, 2
    0x01143021, // addu  a2, t0, s4
    0x0801B563, // stock: j fxDrawLineInWindow (original entry/prologue)
    0x00000000, // nop
};
const uint32_t WidescreenHudReticleCode[] =
{
    0x3C088010, // lui   t0, 0x8010
    0x91092553, // lbu   t1, 0x2553(t0) ; scope depth
    0x15200014, // bne   t1, zero, stock
    0x9108ECA8, // lbu   t0, -0x1358(t0) ; resolution index
    0x31080001, // andi  t0, t0, 1
    0x11000011, // beq   t0, zero, stock
    0x8FA90050, // lw    t1,0x50(sp) ; frontDrawTarget's saved weapon index
    0x08019CE3, // j     weapon filter in the unused FontDtdy slot tail
    0x24080005, // addiu t0,zero,5
    0x01094021, // addu  t0, t0, t1 ; 3*dx
    0x00084FC3, // sra   t1, t0, 31 ; -1 for negative, 0 otherwise
    0x25080002, // addiu t0, t0, 2
    0x01094021, // addu  t0, t0, t1 ; symmetric rounding before /4
    0x00084083, // sra   t0, t0, 2
    0x01142021, // addu  a0, t0, s4
    0x00D44023, // subu  t0, a2, s4 ; second endpoint
    0x00084840, // sll   t1, t0, 1
    0x01094021, // addu  t0, t0, t1
    0x00084FC3, // sra   t1, t0, 31
    0x25080002, // addiu t0, t0, 2
    0x01094021, // addu  t0, t0, t1
    0x00084083, // sra   t0, t0, 2
    0x01143021, // addu  a2, t0, s4
    0x0801B563, // stock: j fxDrawLineInWindow (original entry/prologue)
    0x00000000, // nop
};
// All frontDrawTarget weapon indices now offer their uncompressed segments to
// the host overlay. Keep the five-word slot and unused arithmetic tail stable
// for old save states; the branch is unconditional instead of filtering 1/5.
// A plugin without the overlay still uses the game's original line renderer.
const uint32_t WidescreenHudReticleWeaponCode[] =
{
    0x35290004, // ori   t1,t1,4
    0x100003DB, // b     0x80068300 ; optional host capture for every weapon
    0x00944023, // subu  t0,a0,s4
    0x08019DED, // j     0x800677B4 ; continue first endpoint's 3*dx
    0x00084840, // sll   t1,t0,1
};
static_assert(WidescreenHudReticleWeaponStub.Us + sizeof(WidescreenHudReticleWeaponCode) == WidescreenHudSpriteScaleStub.Us,
              "Reticle weapon filter must fit the unused font slot tail");
static_assert(WidescreenHudReticleStub.Us + sizeof(WidescreenHudReticleCode) == WidescreenHudShotGaugeWrapperStub.Us,
              "Reticle and shot-gauge stubs must not overlap");
static_assert(WidescreenHudReticleStub.Us >= 0x80067790 && WidescreenHudReticleCaveEnd.Us <= 0x80067950,
              "Reticle cave must not overwrite neighbouring diagnostic functions");
struct WIDESCREEN_HUD_OVERLAY_WORD_PATCH
{
    uint32_t Offset;
    uint32_t Original;
    uint32_t Replacement;
};

// Legacy save-state recognition only. Earlier builds subtracted 45 from the
// X coordinates in the overlay's live rectangle table. Leaving those values
// behind even briefly while the game changes aspect mode moves the gauge to
// the left of the weapon. New builds keep the table native and choose the
// weapon anchor only while packing this draw call's display-list rectangles.
const WIDESCREEN_HUD_OVERLAY_WORD_PATCH WidescreenHudShotGaugePatches[] =
{
    { 0x447C, 0x00390015, 0x000C0015 },
    { 0x4480, 0x004A002C, 0x001D002C },
    { 0x4488, 0x00430018, 0x00160018 },
    { 0x448C, 0x0047001A, 0x001A001A },
    { 0x4494, 0x0043001B, 0x0016001B },
    { 0x4498, 0x0047001D, 0x001A001D },
    { 0x44A0, 0x0042001E, 0x0015001E },
    { 0x44A4, 0x00470020, 0x001A0020 },
    { 0x44AC, 0x00410021, 0x00140021 },
    { 0x44B0, 0x00470023, 0x001A0023 },
    { 0x44B8, 0x003F0024, 0x00120024 },
    { 0x44BC, 0x00470026, 0x001A0026 },
    { 0x44C4, 0x003C0027, 0x000F0027 },
    { 0x44C8, 0x00470029, 0x001A0029 },
};

struct WIDESCREEN_HUD_BANNER_WORD_PATCH
{
    uint32_t Offset;
    uint32_t Original;
    uint32_t LowResolution;
    uint32_t HighResolution;
};

// The pickup message uses a matrix-backed bar, front-end object 5 as its cap,
// and a separately clipped font. Give all three the bar's left anchor. Keep
// the stock slide animation and native glyph widths; horizontal font centring
// puts the fully open message at 137.5 / 186.5 framebuffer pixels (320 / 448).
// These overlay-local instructions are installed only while widescreen HUD
// correction is active. No additional cave or global font hook is needed.
const WIDESCREEN_HUD_BANNER_WORD_PATCH WidescreenHudBannerPatches[] =
{
    // text X = .75 * capX + 46 (95 in high resolution), shadow still adds 1
    { 0x1DB8, 0x3C0142F4, 0x3C013F40, 0x3C013F40 }, // f4: 122 -> .75
    { 0x1DC0, 0x3C0142B4, 0x3C014238, 0x3C0142BE }, // f8: 90 -> 46 / 95
    { 0x1DD4, 0x46049181, 0x46049182, 0x46049182 }, // mul.s f6, f18, f4
    // scissor left: 65.5 / 114.5 pixels, encoded in 10.2 at bits 12..23
    { 0x1E20, 0x3C01ED14, 0x3C01ED10, 0x3C01ED1C },
    { 0x1E28, 0x34218000, 0x34216000, 0x3421A000 },
    // scissor right (10.2) = 3 * capX + 496 / 692; f0 stays 4 for both Y bounds
    { 0x1E48, 0x3C014320, 0x3C014040, 0x3C014040 }, // f4: 160 -> 3
    { 0x1E50, 0x00000000, 0x3C0143F8, 0x3C01442D }, // at: 496 / 692
    { 0x1E54, 0x46049180, 0x46049182, 0x46049182 }, // mul.s f6, f18, f4
    { 0x1E58, 0x46003202, 0x44814000, 0x44814000 }, // mtc1 at, f8
    { 0x1E60, 0x00000000, 0x46083200, 0x46083200 }, // add.s f8, f6, f8
    { 0x1EFC, 0x24180008, 0x2418000C, 0x2418000C }, // centre + middle
    { 0x1F64, 0x240E0008, 0x240E000C, 0x240E000C }, // shadow, same alignment
};

// Fuel's matrix-backed frame receives the left HUD bias (-48 / -68) before
// the .75 X scale. Its rectangles use the screen centre instead, while the
// font and deferred digital counter take unscaled screen coordinates. Align
// these callers with the frame without changing shared renderer behaviour.
const WIDESCREEN_HUD_BANNER_WORD_PATCH WidescreenHudFuelPatches[] =
{
    { 0x0F58, 0x240D0042, 0x240D0012, 0x240DFFFE }, // gauge right: 66 - bias
    { 0x0F6C, 0x2406FFD3, 0x2406FFA3, 0x2406FF8F }, // gauge left: -45 - bias
    // Label starts from screenCentre - 50: move to centre - 70 / -85.
    { 0x0FE0, 0x25A50005, 0x25A5FFEC, 0x25A5FFDD },
    { 0x1030, 0x24840080, 0x24840060, 0x24840060 }, // counter: label X + .75 * 128
};

// The same four edits on PAL, whose compile keeps the gauge's right edge and
// the label's X in $t6 rather than $t5. Offsets stay in US terms.
const WIDESCREEN_HUD_BANNER_WORD_PATCH WidescreenHudFuelPatchesPal[] =
{
    { 0x0F58, 0x240E0042, 0x240E0012, 0x240EFFFE }, // addiu t6, zero, 66 - bias
    { 0x0F6C, 0x2406FFD3, 0x2406FFA3, 0x2406FF8F },
    { 0x0FE0, 0x25C50005, 0x25C5FFEC, 0x25C5FFDD }, // addiu a1, t6, ...
    { 0x1030, 0x24840080, 0x24840060, 0x24840060 },
};
static_assert(sizeof(WidescreenHudFuelPatchesPal) == sizeof(WidescreenHudFuelPatches),
              "PAL fuel table must pair with the US one");

constexpr JfgHudBuild::UsAddress WidescreenHudCamCopyEntry = { 0x80042158 };
constexpr JfgHudBuild::BuildWord WidescreenHudCamCopyOriginal = { 0xC4243128, 0xC4243398 }; // lwc1 f4, 0x3128(at)
constexpr JfgHudBuild::UsAddress WidescreenHudFontYEntry = { 0x80070500 };
const uint32_t WidescreenHudFontYOriginal = 0x31AE0FFF; // andi t6, t5, 0x0FFF
constexpr JfgHudBuild::UsAddress WidescreenHudFontDtdyEntry = { 0x80070550 };
const uint32_t WidescreenHudFontDtdyOriginal = 0x3C0E0400; // lui t6, 0x0400
constexpr JfgHudBuild::UsAddress WidescreenHudSpriteScaleEntry = { 0x80041850 };
const uint32_t WidescreenHudSpriteScaleOriginal = 0x44050000; // mfc1 a1, f0
constexpr JfgHudBuild::UsAddress WidescreenHudSpriteScaleAltEntry = { 0x8004189C };
constexpr JfgHudBuild::UsAddress WidescreenHudSpritePositionEntry = { 0x8004171C };
const uint32_t WidescreenHudSpritePositionOriginal = 0x44183000; // mfc1 t8, f6
constexpr JfgHudBuild::UsAddress WidescreenHudMatrixTranslateEntry = { 0x800498E8 };
const uint32_t WidescreenHudMatrixTranslateOriginal = 0xC4E00000; // lwc1 f0, 0(a3)
constexpr JfgHudBuild::UsAddress WidescreenHudLineEntry = { 0x8006D390 };
const uint32_t WidescreenHudLineOriginal = 0x3C0E8010; // lui t6, 0x8010
constexpr JfgHudBuild::BuildWord WidescreenHudLineDelayOriginal = { 0x8DCE3B90, 0x8DCE35E8 }; // lw t6, 0x3B90(t6)
constexpr JfgHudBuild::UsAddress WidescreenHudRectangleEntry = { 0x80059790 };
const uint32_t WidescreenHudRectangleOriginal = 0x8E020000; // lw v0, 0(s0)
const uint32_t WidescreenHudRectangleDelayOriginal = 0x0018CB80; // sll t9, t8, 14
constexpr JfgHudBuild::UsAddress WidescreenHudDigitalColumnAEntry = { 0x8006DE64 };
constexpr JfgHudBuild::UsAddress WidescreenHudDigitalColumnBEntry = { 0x8006DF14 };
const uint32_t WidescreenHudDigitalColumnOriginal = 0x24E70002; // addiu a3, a3, 2
const uint32_t WidescreenHudDigitalColumnCompressed = 0x24E70000; // merge one source column
constexpr JfgHudBuild::UsAddress WidescreenHudDigitalRowAdvanceEntry = { 0x8006DF70 };
const uint32_t WidescreenHudDigitalRowAdvanceOriginal = 0x2442FFF7; // addiu v0, v0, -9
const uint32_t WidescreenHudDigitalRowAdvanceCompressed = 0x2442FFF9; // compensate two merged columns
// func_8006DF90 walks the formatted digit string and advances this cursor by
// ten framebuffer columns per character. Compress the stride the same way the
// two column patches above compress the glyph itself.
//
// This site cannot be trampolined: the next word is the loop's `bnez t1`, so a
// jump here would leave a branch in a delay slot. That is undefined on the
// VR4300, and Compile_Branch only emits the comparison before hitting an
// unhandled-case breakpoint. A constant needs no trampoline anyway.
//
// The exact 0.75 HUD scale wants a 7.5 stride. Over the three digits this
// counter ever shows, a flat 8 differs from an alternating 7/8 by one pixel in
// total, so the alternation is not worth a cave stub.
constexpr JfgHudBuild::UsAddress WidescreenHudDigitalAdvanceEntry = { 0x8006E088 };
const uint32_t WidescreenHudDigitalAdvanceOriginal = 0x2673000A; // addiu s3, s3, 10
const uint32_t WidescreenHudDigitalAdvanceCompressed = 0x26730008; // addiu s3, s3, 8

// The four patches above are retired. The probe proved this renderer never
// runs for the visible counter: no item of its queue type was produced across
// two sessions. The panel actually uses frontPrintNum's own TextureRectangles,
// confirmed by a runtime trace of its yellow/white 96/100 arguments.
// They are still recognised here so a save state that
// carries them can be returned to stock.
const uint32_t WidescreenHudDigitalRetiredCount = 4;
// US addresses: RetireDeadDigitalPatches translates them where it uses them.
const GAME_HACK_CODE_PATCH WidescreenHudDigitalRetired[] =
{
    { WidescreenHudDigitalColumnAEntry.Us, WidescreenHudDigitalColumnOriginal,
      WidescreenHudDigitalColumnCompressed },
    { WidescreenHudDigitalColumnBEntry.Us, WidescreenHudDigitalColumnOriginal,
      WidescreenHudDigitalColumnCompressed },
    { WidescreenHudDigitalRowAdvanceEntry.Us,
      WidescreenHudDigitalRowAdvanceOriginal,
      WidescreenHudDigitalRowAdvanceCompressed },
    { WidescreenHudDigitalAdvanceEntry.Us, WidescreenHudDigitalAdvanceOriginal,
      WidescreenHudDigitalAdvanceCompressed },
};

// frontPrintNum draws the current/max ammunition with textures 8/9. It does
// not use the ordinary font renderer. Scale destination width and digit stride
// only: stack+0x9C also selects source atlas columns and must remain unchanged.
// Its unused stack+0x88 local carries the texture step through texFrame and
// both loops (significant digits and dimmed leading zeroes). Initialize that
// local even outside the correction scope, because the step loads are global.
// The anchor matches the left HUD panel: x' = .75*x + C/4 - .75*B, with
// C=160/B=48 in mode 1 and C=224/B=68 in mode 3 (+4/+5 destination pixels).
// t7/t9 are dead at this site; all saved registers, Y, UVs and dtdy stay intact.
// The original entry delay computes t0 prematurely; replay it on every exit.
const uint32_t WidescreenHudAmmoCode[] =
{
    0x00135080, // sll   t2, s3, 2 ; displaced stock X (10.2)
    0x3C190400, // lui   t9, 0x0400
    0x3739FC00, // ori   t9, t9, 0xFC00 ; stock dsdx=1024, dtdy=-1024
    0xAFB90088, // sw    t9, 0x88(sp) ; unused local, initialized on every path
    0x3C198010, // lui   t9, 0x8010
    0x932F2553, // lbu   t7, 0x2553(t9) ; gameplay HUD scope
    0x11E00017, // beq   t7, zero, resume
    0x9328ECA8, // lbu   t0, -0x1358(t9) ; resolution index
    0x310F0001, // andi  t7, t0, 1
    0x11E00014, // beq   t7, zero, resume
    0x8F2FF418, // lw    t7, -0xBE8(t9) ; texture 8 pointer
    0x124F0003, // beq   s2, t7, compress
    0x8F2FF41C, // lw    t7, -0xBE4(t9) ; texture 9 pointer
    0x164F0010, // bne   s2, t7, resume
    0x00000000, // nop
    0x00187882, // srl   t7, t8, 2 ; destination width/4
    0x030FC023, // subu  t8, t8, t7 ; preserve fractional quarter pixels
    0x00135040, // sll   t2, s3, 1
    0x01535021, // addu  t2, t2, s3 ; 3*X
    0x31080002, // andi  t0, t0, 2 ; high resolution
    0x11000002, // beq   t0, zero, stride
    0x254A0010, // addiu t2, t2, 16 ; low-res anchor +4 pixels
    0x254A0004, // addiu t2, t2, 4 ; high-res anchor +5 pixels
    0x8FAF00A0, // lw    t7, 0xA0(sp) ; destination stride (source width stays intact)
    0x000FC882, // srl   t9, t7, 2
    0x01F97823, // subu  t7, t7, t9 ; 12/8 -> 9/6 pixels
    0xAFAF00A0, // sw    t7, 0xA0(sp)
    0x3C190555, // lui   t9, 0x0555 ; dsdx=1365, approximately 4/3
    0x3739FC00, // ori   t9, t9, 0xFC00 ; preserve vertical sampling
    0xAFB90088, // sw    t9, 0x88(sp)
    0x08016405, // j     0x80059014
    0x030A4021, // addu  t0, t8, t2 ; replay entry delay after X/width are ready
};
static_assert(WidescreenHudAmmoStub.Us + sizeof(WidescreenHudAmmoCode) <= WidescreenHudCaveEnd.Us,
              "Ammo stub exceeds its reserved cave slot");
static_assert(WidescreenHudCaveEnd.Us <= 0x800676B4,
              "HUD cave must not overwrite diCpuLogMessage");

// Increment the scope then tail-call the displaced camStandardOrtho. The JAL
// in overlay 14 already put its +0xC08 resume address in ra, so the tail call
// preserves the stock return path.
const uint32_t WidescreenHudScopeEnterCode[] =
{
    0x3C088010, // lui   t0, 0x8010
    0x91092553, // lbu   t1, 0x2553(t0) ; scope depth
    0x25290001, // addiu t1, t1, 1
    0xA1092553, // sb    t1, 0x2553(t0)
    0x08010359, // j     camStandardOrtho (0x80040D64)
    0x00000000, // nop
};

const uint32_t WidescreenHudScopeExitCode[] =
{
    0x3C088010, // lui   t0, 0x8010
    0x91092553, // lbu   t1, 0x2553(t0)
    0x11200003, // beq   t1, zero, return
    0x00000000, // nop
    0x2529FFFF, // addiu t1, t1, -1
    0xA1092553, // sb    t1, 0x2553(t0)
    0x03E00008, // return: jr ra
    0x00000000, // nop
};

// Replay camCopyOrthoMatrix's displaced load first. While the instrument scope
// is active and the live resolution index is odd (the game's widescreen bit),
// replace only matrix element [0] with 0.75. The matrix translation remains at
// width/2, so 2D geometry is compressed around the framebuffer centre.
const uint32_t WidescreenHudCamCopyCode[] =
{
    0xC4243128, // lwc1  f4, 0x3128(at) ; stock value
    0x3C088010, // lui   t0, 0x8010
    0x91092553, // lbu   t1, 0x2553(t0) ; scope depth
    0x11200008, // beq   t1, zero, return
    0x00000000, // nop
    0x3C088010, // lui   t0, 0x8010
    0x9109ECA8, // lbu   t1, -0x1358(t0) ; sResolutionIndex
    0x31290001, // andi  t1, t1, 1
    0x11200003, // beq   t1, zero, return
    0x00000000, // nop
    0x3C0A3F40, // lui   t2, 0x3F40 ; 0.75f
    0x448A2000, // mtc1  t2, f4
    0x08010858, // j     0x80042160 ; resume after the entry jump
    0x00000000, // nop
};

// Textured glyphs lose thin strokes when reduced horizontally with nearest
// sampling. Keep every source column and compensate for the VI's 4:3-to-16:9
// stretch by growing the glyph vertically instead. At this packing site t5 and
// t3 are the lower and upper Y endpoints in 10.2 units. Expand both around the
// glyph centre by 21/16 (close to 4/3), then replay the displaced mask. This is
// global in native widescreen so gameplay messages and dialogues follow too.
const uint32_t WidescreenHudFontYCode[] =
{
    // The replacement jump executes the following original `or t7,t8,t6` in
    // its delay slot. Preserve the still-valid partial E4 word from t8 first;
    // the stock instruction at the resume address will rebuild t7 correctly.
    0x03007825, // or    t7, t8, zero
    0x3C188010, // lui   t8, 0x8010
    0x9319ECA8, // lbu   t9, -0x1358(t8) ; sResolutionIndex
    0x33390001, // andi  t9, t9, 1
    0x13200007, // beq   t9, zero, rebuild
    0x00000000, // nop
    0x01AB7023, // subu  t6, t5, t3 ; glyph height
    0x000EC080, // sll   t8, t6, 2
    0x030EC021, // addu  t8, t8, t6 ; height * 5
    0x0018C143, // sra   t8, t8, 5 ; half of the added 5/16
    0x01785823, // subu  t3, t3, t8
    0x01B86821, // addu  t5, t5, t8
    // t8 contains the partially assembled E4 TextureRectangle command at the
    // hook site. Restore it after using t8 for the mode test/calculation.
    0x01E0C025, // rebuild: or t8, t7, zero
    0x31AE0FFF, // stock: andi t6, t5, 0x0FFF
    0x0801C141, // j     0x80070504
    0x00000000, // nop
};

// Keep the local 21/16 destination expansion, but use the exact 3/4 texture
// step. 0x030C is the closest 10.10 approximation of 16/21, yet it walks the
// texel phase across successive glyph rows. 0x0300 gives a stable 3-to-4 row
// pattern; the static font display list enables bilerp to soften that regular
// vertical interpolation. Horizontal sampling stays stock at 0x0400,
// preserving every column of every glyph.
const uint32_t WidescreenHudFontDtdyCode[] =
{
    0x3C0E0400, // lui   t6, 0x0400 ; displaced instruction
    0x35CE0400, // ori   t6, t6, 0x0400
    0x3C188010, // lui   t8, 0x8010
    0x9319ECA8, // lbu   t9, -0x1358(t8) ; sResolutionIndex
    0x33390001, // andi  t9, t9, 1
    0x13200003, // beq   t9, zero, resume
    0x00000000, // nop
    0x3C0E0400, // lui   t6, 0x0400 ; dsdx stays 1.0
    0x35CE0300, // ori   t6, t6, 0x0300 ; dtdy = 3/4
    0x0801C156, // resume: j 0x80070558
    0x00000000, // nop
};

// WidescreenHudDigitalAdvanceStub no longer contains a digit hook: the stride is a
// plain constant patch now. The address is kept so the jump the previous build
// installed at WidescreenHudDigitalAdvanceEntry can still be recognised and
// retired when a save state from that build is loaded.

// frontDrawObj reaches camDo2DSprite without using camCopyOrthoMatrix. Split
// its uniform object scale into X=75% and Y=100% while the instrument scope is
// active so weapon, ammo and other textured HUD objects follow the same rule.
const uint32_t WidescreenHudSpriteScaleCode[] =
{
    0x44050000, // mfc1  a1, f0 ; displaced X scale
    0x3C188010, // lui   t8, 0x8010
    0x93192553, // lbu   t9, 0x2553(t8)
    0x1320000A, // beq   t9, zero, resume
    0x00000000, // nop
    0x3C188010, // lui   t8, 0x8010
    0x9319ECA8, // lbu   t9, -0x1358(t8) ; sResolutionIndex
    0x33390001, // andi  t9, t9, 1
    0x13200005, // beq   t9, zero, resume
    0x00000000, // nop
    0x3C183F40, // lui   t8, 0x3F40 ; 0.75f
    0x44981000, // mtc1  t8, f2
    0x46020082, // mul.s f2, f0, f2
    0x44051000, // mfc1  a1, f2
    0x08010616, // resume: j 0x80041858
    0x00000000, // nop
};

// camDo2DSprite contains a second matrix path selected by its internal matrix
// mode flag. The stock US initialization uses this path, so mirror the same X
// scale correction and resume at its own matrixScale call.
const uint32_t WidescreenHudSpriteScaleAltCode[] =
{
    0x44050000, // mfc1  a1, f0 ; displaced X scale
    0x3C188010, // lui   t8, 0x8010
    0x93192553, // lbu   t9, 0x2553(t8)
    0x1320000A, // beq   t9, zero, resume
    0x00000000, // nop
    0x3C188010, // lui   t8, 0x8010
    0x9319ECA8, // lbu   t9, -0x1358(t8) ; sResolutionIndex
    0x33390001, // andi  t9, t9, 1
    0x13200005, // beq   t9, zero, resume
    0x00000000, // nop
    0x3C183F40, // lui   t8, 0x3F40 ; 0.75f
    0x44981000, // mtc1  t8, f2
    0x46020082, // mul.s f2, f0, f2
    0x44051000, // mfc1  a1, f2
    0x08010629, // resume: j 0x800418A4
    0x00000000, // nop
};

// camDo2DSprite puts ObjectTransform.x_position into a temporary vertex before
// applying the matrix scale below. Scaling that position around screen centre
// is what moved the left HUD roughly 40 source pixels inward and pushed the
// scrolling weapon icons outside their clip band. Keep centred sprites centred,
// but bias clearly left/right anchored sprites before the 0.75 matrix scale so
// the resulting local compression preserves their edge margin. The pre-scale
// bias is 48 pixels in low resolution (36 after scaling), and 68 in high res.
const uint32_t WidescreenHudSpritePositionCode[] =
{
    0x44183000, // mfc1  t8, f6 ; displaced converted X position
    0x3C0E8010, // lui   t6, 0x8010
    0x91CF2553, // lbu   t7, 0x2553(t6) ; scope depth
    0x11E00018, // beq   t7, zero, resume
    0x00000000, // nop
    0x91CFECA8, // lbu   t7, -0x1358(t6) ; sResolutionIndex
    0x31F90001, // andi  t9, t7, 1
    0x13200014, // beq   t9, zero, resume
    0x31F90002, // andi  t9, t7, 2 ; high-resolution bit
    0x3C0E8010, // lui   t6, 0x8010
    0x25CEF820, // addiu t6, t6, -0x7E0 ; front-end object 5, pickup banner cap
    0x106E000C, // beq   v1, t6, left_anchor ; anchored to the bar throughout its slide
    0x2B0EFFE1, // slti  t6, t8, -31
    0x15C0000A, // bne   t6, zero, left_anchor
    0x00000000, // nop
    0x2B0E0020, // slti  t6, t8, 32
    0x15C0000B, // bne   t6, zero, resume ; centred sprite
    0x00000000, // nop
    0x17200002, // bne   t9, zero, right_set
    0x240E0044, // addiu t6, zero, 68
    0x240E0030, // right_low: addiu t6, zero, 48
    0x030EC021, // right_set: addu t8, t8, t6
    0x10000005, // b     resume
    0x00000000, // nop
    0x17200002, // left_anchor: bne t9, zero, left_set
    0x240EFFBC, // addiu t6, zero, -68
    0x240EFFD0, // left_low: addiu t6, zero, -48
    0x030EC021, // left_set: addu t8, t8, t6
    0x080105C9, // resume: j 0x80041724 (0x41720 ran in hook delay slot)
    0x00000000, // nop
};

// Recognition only: an older experimental state can replace the current cave
// while the host still owns its installation. Accept this complete old stub
// during restoration so the next frame can install the new cap positioning.
// Never accept isolated legacy words without matching all 27 instructions.
const uint32_t WidescreenHudSpritePositionLegacyCode[] =
{
    0x44183000, 0x3C0E8010, 0x91CF2553, 0x11E00015, 0x00000000,
    0x91CFECA8, 0x31F90001, 0x13200011, 0x31F90002, 0x2B0EFFE1,
    0x15C0000A, 0x00000000, 0x2B0E0020, 0x15C0000B, 0x00000000,
    0x13200001, 0x240E0044, 0x240E0030, 0x030EC021, 0x10000005,
    0x00000000, 0x13200001, 0x240EFFBC, 0x240EFFD0, 0x030EC021,
    0x080105C9, 0x00000000,
};

// Matrix-backed HUD frames and gauges do not pass through camDo2DSprite. Their
// real X anchor reaches matrixTranslate in f12 after camCopyOrthoMatrix has set
// the 0.75 X scale. Apply the same pre-scale +/-48 (or +/-68 high-res) bias as
// the sprite path, while leaving centred translations untouched. Unlike matrix
// element [15], this is a genuine affine translation and therefore preserves
// the projection and clipping behaviour.
const uint32_t WidescreenHudMatrixTranslateCode[] =
{
    0xC4E00000, // lwc1  f0, 0(a3) ; displaced instruction
    0x3C088010, // lui   t0, 0x8010
    0x91092553, // lbu   t1, 0x2553(t0) ; scope depth
    0x11200023, // beq   t1, zero, resume
    0x00000000, // nop
    0x9109ECA8, // lbu   t1, -0x1358(t0) ; sResolutionIndex
    0x312A0001, // andi  t2, t1, 1
    0x1140001F, // beq   t2, zero, resume
    0x312A0002, // andi  t2, t1, 2 ; high-resolution bit
    0x3C0BC1F8, // lui   t3, 0xC1F8 ; -31.0f
    0x448B1000, // mtc1  t3, f2
    0x4602603C, // c.lt.s f12, f2
    0x45010008, // bc1t  left_anchor
    0x00000000, // nop
    0x3C0B4200, // lui   t3, 0x4200 ; 32.0f
    0x448B1000, // mtc1  t3, f2
    0x460C103C, // c.lt.s f2, f12
    0x4501000D, // bc1t  right_anchor
    0x00000000, // nop
    0x10000013, // b     resume ; centred translation
    0x00000000, // nop
    0x11400004, // left_anchor: beq t2, zero, left_low
    0x00000000, // nop
    0x3C0B4288, // lui   t3, 0x4288 ; 68.0f
    0x10000002, // b     left_set
    0x00000000, // nop
    0x3C0B4240, // left_low: lui t3, 0x4240 ; 48.0f
    0x448B1000, // left_set: mtc1 t3, f2
    0x46026301, // sub.s f12, f12, f2
    0x10000009, // b     resume
    0x00000000, // nop
    0x11400004, // right_anchor: beq t2, zero, right_low
    0x00000000, // nop
    0x3C0B4288, // lui   t3, 0x4288 ; 68.0f
    0x10000002, // b     right_set
    0x00000000, // nop
    0x3C0B4240, // right_low: lui t3, 0x4240 ; 48.0f
    0x448B1000, // right_set: mtc1 t3, f2
    0x46026300, // add.s f12, f12, f2
    0x0801263C, // resume: j 0x800498F0 (0x498EC ran in hook delay slot)
    0x00000000, // nop
};

// fxDrawLine queues its endpoints for a later render pass. Transform them at
// queue time, while the overlay scope is still known, so radar strokes and the
// tiny line font retain their proportions even after the scope has returned.
// Resolution modes 1 and 3 use 320- and 448-pixel framebuffers respectively.
const uint32_t WidescreenHudLineCode[] =
{
    0x3C0E8010, // lui   t6, 0x8010 ; displaced pair
    0x8DCE3B90, // lw    t6, 0x3B90(t6)
    0x3C188010, // lui   t8, 0x8010
    0x93192553, // lbu   t9, 0x2553(t8)
    0x13200011, // beq   t9, zero, resume
    0x00000000, // nop
    0x3C188010, // lui   t8, 0x8010
    0x9319ECA8, // lbu   t9, -0x1358(t8) ; sResolutionIndex
    0x332F0001, // andi  t7, t9, 1
    0x11E0000C, // beq   t7, zero, resume
    0x332F0002, // andi  t7, t9, 2 ; high-resolution bit
    0x11E00002, // beq   t7, zero, low_res_center
    0x240F00A0, // addiu t7, zero, 160
    0x240F00E0, // addiu t7, zero, 224
    0x0004C040, // low_res_center: sll t8, a0, 1
    0x0304C021, // addu  t8, t8, a0
    0x030FC021, // addu  t8, t8, t7
    0x00182083, // sra   a0, t8, 2
    0x0006C040, // sll   t8, a2, 1
    0x0306C021, // addu  t8, t8, a2
    0x030FC021, // addu  t8, t8, t7
    0x00183083, // sra   a2, t8, 2
    0x0801B4E6, // resume: j 0x8006D398
    0x00000000, // nop
};

// frontDrawRectangles packs clipped pixel coordinates straight into F6 fill
// commands. At this site t8 and a2 are the right and left X endpoints; scale
// both before replaying the displaced display-list load and pack operation.
const uint32_t WidescreenHudRectangleLegacyCode[] =
{
    0x3C198010, // lui   t9, 0x8010
    0x93392553, // lbu   t9, 0x2553(t9)
    0x13200011, // beq   t9, zero, stock
    0x00000000, // nop
    0x3C198010, // lui   t9, 0x8010
    0x9339ECA8, // lbu   t9, -0x1358(t9) ; sResolutionIndex
    0x332F0001, // andi  t7, t9, 1
    0x11E0000C, // beq   t7, zero, stock
    0x332F0002, // andi  t7, t9, 2 ; high-resolution bit
    0x11E00002, // beq   t7, zero, low_res_center
    0x240F00A0, // addiu t7, zero, 160
    0x240F00E0, // addiu t7, zero, 224
    0x0018C840, // low_res_center: sll t9, t8, 1
    0x0338C821, // addu  t9, t9, t8
    0x032FC821, // addu  t9, t9, t7
    0x0019C083, // sra   t8, t9, 2
    0x0006C840, // sll   t9, a2, 1
    0x0326C821, // addu  t9, t9, a2
    0x032FC821, // addu  t9, t9, t7
    0x00193083, // sra   a2, t9, 2
    0x8E020000, // stock: lw v0, 0(s0)
    0x0018CB80, // sll   t9, t8, 14
    0x080165E6, // j     0x80059798
    0x00000000, // nop
};

const uint32_t WidescreenHudRectangleCode[] =
{
    0x3C198010, // lui   t9, 0x8010
    0x93392553, // lbu   t9, 0x2553(t9)
    0x13200011, // beq   t9, zero, stock
    0x00000000, // nop
    0x3C198010, // lui   t9, 0x8010
    0x9339ECA8, // lbu   t9, -0x1358(t9) ; sResolutionIndex
    0x332F0001, // andi  t7, t9, 1
    0x11E0000C, // beq   t7, zero, stock
    0x332F0002, // andi  t7, t9, 2 ; high-resolution bit
    0x08019E04, // j     WidescreenHudShotGaugeAnchorStub
    0x00000000, // nop
    0x00000000, // nop
    0x0018C840, // transform: sll t9, t8, 1
    0x0338C821, // addu  t9, t9, t8
    0x032FC821, // addu  t9, t9, t7
    0x0019C083, // sra   t8, t9, 2
    0x0006C840, // sll   t9, a2, 1
    0x0326C821, // addu  t9, t9, a2
    0x032FC821, // addu  t9, t9, t7
    0x00193083, // sra   a2, t9, 2
    0x8E020000, // stock: lw v0, 0(s0)
    0x0018CB80, // sll   t9, t8, 14
    0x080165E6, // j     0x80059798
    0x00000000, // nop
};

// A fixed return address distinguishes the weapon gauge from other rectangle
// callers after frontDrawRectangles has saved ra at 0x24(sp). The original
// four arguments, native table and game-updated colours pass through intact.
const uint32_t WidescreenHudShotGaugeWrapperCode[] =
{
    0x27BDFFE8, // addiu sp, sp, -0x18
    0xAFBF0014, // sw    ra, 0x14(sp)
    0x0C01657D, // jal   frontDrawRectangles
    0x00000000, // nop
    0x8FBF0014, // lw    ra, 0x14(sp) ; fixed callee return is 0x80067804
    0x03E00008, // jr    ra
    0x27BD0018, // addiu sp, sp, 0x18
};

// Called only after the HUD scope and current game aspect guards have passed.
// t7 is resolution & 2; ordinary rectangles retain the screen-centre anchor.
// The gauge's table stays in 320-pixel coordinates, whereas its weapon frame
// uses the viewport centre plus world coordinates. Matching that frame gives
// x' = .75*x + 4 at 320, or .75*x + 53 at 448, before HUD alignment translation.
// v0 is scratch here because the displaced display-list load overwrites it
// before any stock consumer. No coordinates are stored back to the game table.
const uint32_t WidescreenHudShotGaugeAnchorCode[] =
{
    0x8FB90024, // lw    t9, 0x24(sp) ; frontDrawRectangles' saved caller
    0x3C028006, // lui   v0, 0x8006
    0x34427804, // ori   v0, v0, 0x7804
    0x17220006, // bne   t9, v0, normal
    0x00000000, // nop
    0x11E00002, // beq   t7, zero, gauge_return
    0x240F0010, // addiu t7, zero, 16 ; low-res weapon anchor
    0x240F00D4, // addiu t7, zero, 212 ; high-res weapon anchor
    0x08019D1C, // j     WidescreenHudRectangleStub + 0x30
    0x00000000, // nop
    0x000F7940, // normal: sll t7, t7, 5 ; 0 / 64
    0x08019D1C, // j     WidescreenHudRectangleStub + 0x30
    0x25EF00A0, // addiu t7, t7, 160 ; normal: 160 / 224
};
static_assert(WidescreenHudShotGaugeWrapperStub.Us + sizeof(WidescreenHudShotGaugeWrapperCode) == WidescreenHudShotGaugeAnchorStub.Us,
              "Shot-gauge wrapper must end before its rectangle anchor helper");
static_assert(WidescreenHudShotGaugeAnchorStub.Us + sizeof(WidescreenHudShotGaugeAnchorCode) == WidescreenHudFloydLineStub.Us,
              "Shot-gauge helper must fit within the retired ring-display function");

// Only overlay 14's Floyd outline calls this wrapper. The caller has converted
// its native (272,192) centre with viConvertXY and saved it at sp+E0/DC.
// Relocate that centre before the common .75 line transform: its final centre
// becomes (280,192) / (359,240), following the already right-anchored sprite.
// Mark only style 13 for the slope-aware rasterizer; the table stays native.
// The original stack arguments and return address pass through to fxDrawLine.
const uint32_t WidescreenHudFloydLineCode[] =
{
    0x3C188010, // lui   t8, 0x8010
    0x93192553, // lbu   t9, 0x2553(t8) ; HUD scope
    0x13200019, // beq   t9, zero, stock
    0x9319ECA8, // lbu   t9, -0x1358(t8) ; current game mode
    0x2F2F0004, // sltiu t7, t9, 4
    0x11E00016, // beq   t7, zero, stock
    0x332F0001, // andi  t7, t9, 1
    0x11E00014, // beq   t7, zero, stock
    0x332F0002, // andi  t7, t9, 2
    0x24190140, // addiu t9, zero, 320 ; pre-transform centre X
    0x11E00003, // beq   t7, zero, centre_ready
    0x241800C0, // addiu t8, zero, 192 ; centre Y
    0x24190194, // addiu t9, zero, 404
    0x241800F0, // addiu t8, zero, 240
    0x8FAF00E0, // centre_ready: lw t7, 0xE0(sp)
    0x032FC823, // subu  t9, t9, t7
    0x00992021, // addu  a0, a0, t9
    0x00D93021, // addu  a2, a2, t9
    0x8FAF00DC, // lw    t7, 0xDC(sp)
    0x030FC023, // subu  t8, t8, t7
    0x00B82821, // addu  a1, a1, t8
    0x00F83821, // addu  a3, a3, t8
    0x8FB90010, // lw    t9, 0x10(sp) ; line style
    0x332F000F, // andi  t7, t9, 0xF
    0x25EFFFF3, // addiu t7, t7, -13
    0x15E00002, // bne   t7, zero, stock
    0x37390040, // ori   t9, t9, 0x40 ; queued outline marker
    0xAFB90010, // sw    t9, 0x10(sp)
    0x0801B4E4, // stock: j fxDrawLine (current shared HUD guard/transform)
    0x00000000, // nop
};
static_assert(WidescreenHudFloydLineStub.Us + sizeof(WidescreenHudFloydLineCode) <= 0x800678C4,
              "Floyd queue wrapper must end before the next diagnostic entry");

// Words of the HUD listings no relocation can produce, per build. The US word
// is checked before it is replaced, so an edit that moves one fails loudly
// (the listing comes back empty) instead of patching the wrong instruction.
struct HUD_CODE_OVERRIDE
{
    const uint32_t * Listing;
    size_t Index;
    JfgHudBuild::BuildWord Word;
};
const HUD_CODE_OVERRIDE HudCodeOverrides[] =
{
    // The displaced camCopyOrthoMatrix load: its upper half is the game's own
    // lui, so the low half is spelled out (0x800A3128 on US, 0x800A3398 on PAL).
    { WidescreenHudCamCopyCode, 0, { 0xC4243128, 0xC4243398 } },
    // "sltiu t7, t9, 4": gameplay video modes are 0..3 on NTSC, 8..11 on PAL.
    { WidescreenHudFloydLineCode, 4, { 0x2F2F0004, 0x2F2F000C } },
    // fxOutputLines' displaced low half of the line queue index (0x80103B90 on
    // US, 0x801035E8 on PAL), replayed after the game's lui moved into the
    // entry's delay slot.
    { JfgRocketOverlay::Code, 31, { 0x24A53B90, 0x24A535E8 } },
};

// A HUD listing for the ROM in hand: relocated, with the overrides above put
// in. Empty when the build has no HUD support or a word has no translation;
// every caller treats an empty listing as "do not install".
std::vector<uint32_t> HudCode(const uint32_t * Listing, size_t Count)
{
    std::vector<uint32_t> Code;
    if (!JfgHudBuild::Relocate(Listing, Count, Code))
    {
        return {};
    }
    for (const HUD_CODE_OVERRIDE & Override : HudCodeOverrides)
    {
        if (Override.Listing != Listing)
        {
            continue;
        }
        if (Override.Index >= Count || Listing[Override.Index] != Override.Word.Us)
        {
            return {};
        }
        Code[Override.Index] = Override.Word;
    }
    return Code;
}

template <size_t N>
std::vector<uint32_t> HudCode(const uint32_t (&Listing)[N])
{
    return HudCode(Listing, N);
}

// These two words were used by the first experimental landing-skip build. They
// are only restored when exactly that old hook is found in a loaded state; its
// old code cave is deliberately never touched because it may now belong to a
// different JFG hook.
uint32_t LegacyLandingCinematicSkipEntry = 0x80046018;
const uint32_t LegacyLandingCinematicSkipEntryOriginal = 0x3C108010; // lui $s0, 0x8010
const uint32_t LegacyLandingCinematicSkipEntryDelayOriginal = 0x2610B408; // addiu $s0, $s0, -0x4BF8
uint32_t LegacyLandingCinematicSkipJump = 0x08019BC0; // j 0x80066F00
uint32_t LegacyLandingCinematicSkipCall = 0x0C019BC0; // jal 0x80066F00

// This is the JP TrySkipLandingCutscene control flow adapted only where the US
// symbol map differs: playState globals, frontGetMode, and mainChangeLevel. It
// checks the direct keyboard/mouse A-or-Start request, then only the sixteen
// native landing scene/setup pairs. Unlike JP, US clears mapLevelSelected by
// the time its landing scene is active (verified on scene 0x0065), so the US
// route table itself is the guard against non-landing cinematics. The same hook
// also whitelists the long new-save story intro scene/setup sequence captured
// from US: 0142:0, 0144:0, 0143:0-3, 0145:0, 0146:0 and 0147:0. That path uses
// the native front-end transition to character select instead of a level table
// destination.
//
// The table
// lives in the captured code cave, so the runtime owns no saved game state and
// restores every original word before a save state.
//
// 0x80067000 is dormant diCpuTrace diagnostic code. Existing JFG stubs use the
// same completed diagnostics block, but this reserved range does not overlap
// any of them. The original words are captured rather than assumed to be zero.
//
// The listing is built for the ROM in hand: its playState globals, the four
// routines it calls, its table and its return address all move between builds.
// The comments keep the US spelling of each word.
const size_t LandingCinematicSkipStubCodeWords = 87;
std::vector<uint32_t> LandingCinematicSkipStubCode(void)
{
    const uint32_t Return = LandingCinematicSkipEntry + 0x04;
    return {
        WithHi(0x3C010000, LandingCinematicSkipInputAddress), // lui   $at, 0x800A
        WithLo(0x8C280000, LandingCinematicSkipInputAddress), // lw    $t0, 0xFCBC($at)  ; direct E / Return request
        0x11000003, // beq   $t0, $zero, resume
        0x00000000, // nop
        0x10000005, // b     trySkip
        0x00000000, // nop
        0x3C1F0000 | (Return >> 16), // resume: lui $ra, 0x8004
        0x37FF0000 | (Return & 0xFFFF), // ori   $ra, $ra, 0x5FF4
        0x03E00008, // jr    $ra
        0x00000000, // nop

        WithHi(0x3C0E0000, LoadingAddress), // trySkip: lui $t6, 0x800A
        WithLo(0x81CE0000, LoadingAddress), // lb    $t6, 0x3294($t6)  ; loading
        0x27BDFFD8, // addiu $sp, $sp, -0x28
        0x15C00044, // bne   $t6, $zero, return
        0x00000000, // nop

        WithHi(0x3C040000, CurrentSceneAddress), // lui   $a0, 0x800A
        WithLo(0x84840000, CurrentSceneAddress), // lh    $a0, 0x323C($a0)  ; currentScene
        WithHi(0x3C060000, CurrentSetupAddress), // lui   $a2, 0x800A
        WithLo(0x24C60000, CurrentSetupAddress), // addiu $a2, $a2, 0x3248  ; currentSetup
        0x84D90000, // lh    $t9, 0($a2)
        0x24180143, // addiu $t8, $zero, 0x0143
        0x1098000B, // beq   $a0, $t8, story_scene_143
        0x00000000, // nop
        0x17200016, // bne   $t9, $zero, landing_check
        0x00000000, // nop
        0x28810142, // slti  $at, $a0, 0x0142
        0x14200013, // bne   $at, $zero, landing_check
        0x00000000, // nop
        0x28810148, // slti  $at, $a0, 0x0148
        0x10200010, // beq   $at, $zero, landing_check
        0x00000000, // nop
        0x10000006, // b     story_skip
        0x00000000, // nop
        0x2F210004, // story_scene_143: sltiu $at, $t9, 4
        0x14200003, // bne   $at, $zero, story_skip
        0x00000000, // nop
        0x10000009, // b     landing_check
        0x00000000, // nop

        0x24040005, // story_skip: addiu $a0, $zero, 5
        0x2405007F, // addiu $a1, $zero, 0x7F
        CallTo(MainFrontInitFunction), // jal   mainFrontInit(5, 0x7F, 0)
        0x00003025, // or    $a2, $zero, $zero
        CallTo(FrontCharSelectSetQuitModeFunction), // jal   frontCharSelectSetQuitMode(0)
        0x00002025, // or    $a0, $zero, $zero
        0x10000025, // b     return
        0x00000000, // nop

        WithHi(0x3C020000, LandingCinematicSkipStubTable), // landing_check: lui $v0, 0x8006
        WithLo(0x24430000, LandingCinematicSkipStubTable), // addiu $v1, $v0, 0x7160  ; LandingCinematicSkipStubTable
        0x846F0002, // loop: lh $t7, 2($v1)    ; destination
        0x2405FFFF, // addiu $a1, $zero, -1
        0x10AF001F, // beq   $a1, $t7, return
        WithHi(0x3C040000, CurrentSceneAddress), // lui   $a0, 0x800A
        WithHi(0x3C060000, CurrentSetupAddress), // lui   $a2, 0x800A
        WithLo(0x84840000, CurrentSceneAddress), // lh    $a0, 0x323C($a0)  ; currentScene
        WithLo(0x24C60000, CurrentSetupAddress), // addiu $a2, $a2, 0x3248  ; currentSetup
        0x94620000, // lhu   $v0, 0($v1)       ; scene << 4 | setup
        0x00000000, // nop
        0x0002C102, // srl   $t8, $v0, 4
        0x14980013, // bne   $a0, $t8, next
        0x00000000, // nop
        0x84D90000, // lh    $t9, 0($a2)
        0x3048000F, // andi  $t0, $v0, 0x000F
        0x1728000F, // bne   $t9, $t0, next
        0x00000000, // nop
        CallTo(FrontGetModeFunction), // jal   frontGetMode
        0xAFA30024, // sw    $v1, 0x24($sp)
        0x8FA30024, // lw    $v1, 0x24($sp)
        WithHi(0x3C050000, NextCharacterAddress), // lui   $a1, 0x800A
        0x84640002, // lh    $a0, 2($v1)       ; destination scene
        WithLo(0x84A50000, NextCharacterAddress), // lh    $a1, 0x3260($a1)  ; nextCharacter
        0x240A0001, // addiu $t2, $zero, 1
        0xAFAA0010, // sw    $t2, 0x10($sp)
        0xAFA00014, // sw    $zero, 0x14($sp)
        0x00003025, // or    $a2, $zero, $zero
        CallTo(MainChangeLevelFunction), // jal   mainChangeLevel
        0x00403825, // or    $a3, $v0, $zero   ; frontGetMode result
        0x10000005, // b     return
        0x00000000, // nop
        0x846B0006, // next: lh $t3, 6($v1)
        0x24630004, // addiu $v1, $v1, 4
        0x14ABFFDF, // bne   $a1, $t3, loop
        0x00000000, // nop
        0x3C1F0000 | (Return >> 16), // return: lui $ra, 0x8004
        0x37FF0000 | (Return & 0xFFFF), // ori   $ra, $ra, 0x5FF4
        0x27BD0028, // addiu $sp, $sp, 0x28
        0x03E00008, // jr    $ra
        0x00000000, // nop
    };
}

// The stub above walks a scene-to-destination table that begins 0x160 bytes
// into the cave (LandingCinematicSkipStub + 0x160, just past the 87 code words).
// A held E/Return plus a match on (currentScene, currentSetup) warps past the
// cinematic through mainChangeLevel. Keeping the table as data - rather than
// hand-encoded words - lets the catalogue (first recovered with a cinematic
// probe that has since been removed) grow by editing a list. BuildLandingCinematicSkipImage stitches the code, the
// padding, the encoded entries and the terminating sentinel back into the exact
// image the stub expects.
struct JFG_CINEMATIC_SKIP_ENTRY
{
    uint16_t Scene;
    uint8_t Setup; // only the low four bits form the table key
    uint16_t Destination;
};

// The 87 stub-code words end at 0x15C; the table starts one padding word later
// at 0x160. (It formerly sat at 0x180, wasting eight words of the cave.)
const uint32_t LandingCinematicSkipTableWordOffset = 0x160 / sizeof(uint32_t);
// IntroCinematicSkipStub sits at LandingCinematicSkipStub + 0x200, so the whole
// image - code, padding, every entry and the sentinel - must fit in 0x80 words.
const uint32_t LandingCinematicSkipImageWordLimit = 0x200 / sizeof(uint32_t);

static_assert(
    LandingCinematicSkipStubCodeWords <= LandingCinematicSkipTableWordOffset,
    "landing cinematic skip stub code overruns its table offset");

// Live table. First the seventeen validated in-game, then the highest-confidence
// "clean" mappings recovered from the probe (one cinematic scene that resolved
// to a single regain-control marker). The cave holds no more than these;
// everything else is staged just below.
const JFG_CINEMATIC_SKIP_ENTRY LandingCinematicSkipTable[] =
{
    // Original seventeen, confirmed by play.
    { 0x141, 0, 0x051 }, // Rith Essa
    { 0x14F, 0, 0x14C }, // Asteroid
    { 0x133, 0, 0x09B }, // Mizar Palace (Juno / Vela)
    { 0x16F, 0, 0x137 }, // Mizar Palace (Lupus)
    { 0x16A, 0, 0x02B }, // Ichor
    { 0x14A, 0, 0x0D8 }, // Eschebone
    { 0x087, 0, 0x09E }, // Tawfret
    { 0x042, 0, 0x043 }, // Sekhmet
    { 0x170, 0, 0x035 }, // Cerulean
    { 0x065, 0, 0x023 }, // SS Anubis
    { 0x064, 0, 0x048 }, // Spawnship
    { 0x16D, 0, 0x05C }, // Goldwood story transition
    { 0x05D, 0, 0x05C }, // Goldwood
    { 0x19B, 0, 0x110 }, // Gem Quarry
    { 0x113, 0, 0x114 }, // UFO
    { 0x156, 0, 0x0E5 }, // Water Ruin
    { 0x00F, 0, 0x016 }, // Walkway
    // New "clean" entries from JfgCinematicProbe.log: one cinematic scene, one
    // regain-control marker. Single observations whose destinations match the
    // log exactly, but not yet each re-checked in game.
    { 0x0C1, 0, 0x0C3 },
    { 0x0E6, 0, 0x0B5 },
    { 0x0FC, 0, 0x084 },
    { 0x0FD, 0, 0x084 },
    { 0x0FF, 0, 0x0F2 },
    { 0x132, 0, 0x0C9 },
    { 0x134, 0, 0x130 },
    { 0x139, 0, 0x07F },
    { 0x13A, 0, 0x07F },
    { 0x160, 0, 0x0D2 },
    { 0x178, 0, 0x088 },
    { 0x189, 0, 0x15F },
    { 0x18E, 0, 0x16B },
    { 0x192, 0, 0x0C9 },
    // Recorded and replay-verified from the probe: one cinematic spanning scene
    // 0x0C7 then 0x18F (setup 1), both returning control at scene 0x015.
    { 0x0C7, 0, 0x015 },
    { 0x18F, 1, 0x015 },
    // Area 0x030 intro, spanning scenes 0x0A3-0x0A6, control returns at 0x030.
    { 0x0A3, 0, 0x030 },
    { 0x0A4, 0, 0x030 },
    { 0x0A5, 0, 0x030 },
    { 0x0A6, 0, 0x030 },
};

static_assert(
    LandingCinematicSkipTableWordOffset +
            sizeof(LandingCinematicSkipTable) / sizeof(JFG_CINEMATIC_SKIP_ENTRY) +
            1 <=
        LandingCinematicSkipImageWordLimit,
    "cinematic skip table overflows the diagnostic cave; relocate it before "
    "adding more entries");

// Staged catalogue: the remaining cinematics recovered from the probe that do
// not fit the cave. To enable one, move it up into LandingCinematicSkipTable
// (dropping a clean entry to stay within the cap), or relocate the table to a
// larger verified free region and compile the whole set. "Chained" entries are
// scenes that shared a single regain-control marker in the log, so each
// destination is a best guess that still needs an in-game check.
const JFG_CINEMATIC_SKIP_ENTRY LandingCinematicSkipStaged[] =
{
    // Clean, but with a non-zero setup.
    { 0x197, 2, 0x0FA },
    { 0x19A, 1, 0x0F3 },
    // Chained - destination to verify in game.
    { 0x058, 0, 0x035 },
    { 0x05E, 0, 0x035 },
    { 0x061, 0, 0x023 },
    { 0x066, 0, 0x023 },
    { 0x0CF, 0, 0x182 },
    { 0x0D4, 0, 0x182 },
    { 0x0E9, 0, 0x0E9 },
    { 0x0EE, 0, 0x023 },
    { 0x0EF, 0, 0x023 },
    { 0x0F0, 0, 0x023 },
    { 0x107, 0, 0x012 },
    { 0x10C, 1, 0x11D },
    { 0x11A, 0, 0x110 },
    { 0x11B, 0, 0x110 },
    { 0x11C, 1, 0x110 },
    { 0x135, 1, 0x110 },
    { 0x13F, 0, 0x09B },
    { 0x14F, 1, 0x14C },
    { 0x158, 0, 0x0F1 },
    { 0x159, 0, 0x0F1 },
    { 0x15A, 0, 0x14C },
    { 0x15B, 0, 0x0E9 },
    { 0x15E, 0, 0x152 },
    { 0x15E, 1, 0x152 },
    { 0x165, 0, 0x10B },
    { 0x167, 0, 0x07F },
    { 0x167, 1, 0x07F },
    { 0x172, 0, 0x012 },
    { 0x173, 0, 0x012 },
    { 0x175, 0, 0x152 },
    { 0x17A, 2, 0x0F1 },
    { 0x17B, 1, 0x0F1 },
    { 0x17C, 3, 0x0F1 },
    { 0x17C, 4, 0x0F1 },
    { 0x183, 0, 0x182 },
    { 0x184, 0, 0x07F },
    { 0x184, 1, 0x07F },
    { 0x184, 2, 0x07F },
    { 0x185, 0, 0x07F },
    { 0x186, 0, 0x07F },
    { 0x187, 0, 0x07F },
    { 0x188, 0, 0x07F },
    { 0x18A, 0, 0x11D },
    { 0x193, 1, 0x10B },
    { 0x194, 1, 0x10B },
    { 0x195, 0, 0x14C },
    { 0x197, 0, 0x11D },
};

static_assert(sizeof(LandingCinematicSkipStaged) > 0,
              "keep the staged cinematic catalogue referenced");

// Rebuild the full stub image the installer writes: fixed code, zero padding up
// to the table offset, the encoded live entries, then the -1 sentinel.
std::vector<uint32_t> BuildLandingCinematicSkipImage(void)
{
    std::vector<uint32_t> Image = LandingCinematicSkipStubCode();
    if (Image.size() != LandingCinematicSkipStubCodeWords)
    {
        // The listing and its word count disagree: install nothing.
        return {};
    }
    Image.resize(LandingCinematicSkipTableWordOffset, 0);
    for (const JFG_CINEMATIC_SKIP_ENTRY & Entry : LandingCinematicSkipTable)
    {
        Image.push_back(
            (((uint32_t)Entry.Scene << 4 | (Entry.Setup & 0xF)) << 16) |
            Entry.Destination);
    }
    Image.push_back(0x0000FFFF);
    return Image;
}

uint32_t CameraNativeYAddress = 0x8009F244;
uint32_t CameraHeightOffsetAddress = 0x8009F248;
uint32_t CameraTopDownCounterAddress = 0x8009F24C;
// The free orbit's camera height, one word per player, and the native target
// height the injected blend saves before adding it, again per player. They
// sit in the same zero gap the three words above already use, just below
// them: the gap runs from CameraNativeYAddress - 0x1C in both builds, and the
// injected code indexes both tables from one $at, see CameraHeightBlendCode.
// CameraHeightOffsetAddress is no longer read by anything and is kept only
// as a name for the word.
const uint32_t CameraPlayerCount = 4;
uint32_t CameraNativeYTableAddress = 0x8009F228;
uint32_t CameraHeightTableAddress = 0x8009F238;

uint32_t PlayerListAddress = 0x800F2D0C;
uint32_t PlayerCountAddress = 0x800F2D10;
uint32_t DisableJoyAddress = 0x800F6DBC;
uint32_t ControlCameraAddress = 0x800F6DC0;
uint32_t CameraArrayAddress = 0x800FA4D0;
uint32_t LobbyCameraInUseAddress = 0x800FB080;
uint32_t StaticCameraInUseAddress = 0x800FB084;
uint32_t AnimseqCameraAddress = 0x801045B8;
uint32_t CameraActiveOverrideBase = 0x800F6E58;
const uint32_t CameraActiveOverrideStride = 0x2C;
const uint32_t CameraStructSize = 0x4C;
const uint32_t CameraCount = 4;

// The drone the player flies is the game's robot mission: a timed course with
// targets, collectables and checkpoints. Nothing about the player or the camera
// mode changes when it takes over, so this is what tells the two apart. It sits
// in the main code rather than in an overlay, so it reads the same whatever the
// game has paged in.
uint32_t RobotMissionAddress = 0x800A3208;
uint32_t MultiplayerGameAddress = 0x800A4FC4;
uint32_t CooperativeGameAddress = 0x800A4FC8;
const uint32_t ObjectPlayerDataOffset = 0x68;
const uint32_t ObjectYawOffset = 0x00;
const uint32_t PlayerIndexOffset = 0x00;
const uint32_t PlayerTypeOffset = 0x01;
const uint32_t PlayerAnimationIndexOffset = 0x3A;
const uint32_t PlayerAnimationListOffset = 0x6C;
const uint32_t AnimationLoopFlagOffset = 0x08;
const uint32_t AnimationFrameOffset = 0x28;
const uint32_t PlayerCameraOrbitYawOffset = 0x104;
const uint32_t PlayerCameraYawOffset = 0x10A;
const uint32_t PlayerCameraCenterOffset = 0x10C;
const uint32_t PlayerCameraControlFlagsOffset = 0x10D;
const uint32_t PlayerMovementYawOffset = 0x11C;
const uint32_t PlayerAltCameraBaseYawOffset = 0x128;
const uint32_t PlayerCameraTransitionOffset = 0x198;
const uint32_t PlayerCameraBehaviorOffset = 0x19D;
const uint32_t PlayerCameraForceOffset = 0x19F;
const uint32_t PlayerCameraScriptOffset = 0x1F9;
const uint32_t PlayerCameraOverrideOffset = 0x1FA;
const uint32_t PlayerCameraPathOffset = 0x1FC;
const uint32_t PlayerCameraModeOffset = 0x568;
const uint8_t PlayerCameraModeNormal = 0;
const uint8_t PlayerCameraModeJump = 3;
const uint8_t PlayerCameraModeManualAim = 11;
// Aiming while crouched, and the fixed camera some boss fights impose. Both put
// the game in manual aim without using mode 11, so the mouse had no effect.
const uint8_t PlayerCameraModeCrouchAim = 5;
const uint8_t PlayerCameraModeBossAim = 32;
// Crouched and prone with no aim, where left/right shuffle sideways.
const uint8_t PlayerCameraModeCrouch = 1;
const uint8_t PlayerCameraModeProne = 2;
const uint32_t PlayerCameraObjectOffset = 0x5C0;
const uint32_t PlayerManualAimX1Offset = 0x1CE;
const uint32_t PlayerManualAimY1Offset = 0x1D0;
const uint32_t PlayerManualAimX2Offset = 0x1DC;
const uint32_t PlayerManualAimY2Offset = 0x1DE;
const uint32_t PlayerManualAimVerticalOffset = 0x1E0;
const uint32_t PlayerManualAimPitchOffset = 0x1E2;
const uint32_t PlayerManualAimXVelocityOffset = 0x1E4;
const uint32_t PlayerManualAimYVelocityOffset = 0x1E8;

// How the stock boss aim turns the view, from controlGetManualAim at 0x8003AABC.
// The stick is clamped to +-0x28 and the reticle is placed linearly from it, so
// the reticle is pinned at the edge of its box for anything past that. Between
// 0x28 and 0x2D nothing else happens: that band is a deliberate dead zone. Past
// 0x2D the surplus indexes a twenty entry ramp at 0x800A2E60 -- 0, 2, 6, 12, 20,
// ... 380, i.e. n*(n+1) -- and the result is smoothed into the camera turn rate
// at PlayerData+0x1E4, which the same function finally spends as
// "PlayerData+0x11C += rate", the very yaw the normal path writes.
//
// Driving 0x1E4 from here does not work: controlGetManualAim reloads it, lerps
// it back toward the stick's own target of zero and snaps it to zero below one,
// so the write is attenuated and smeared. Applying the step directly is what the
// game itself ends up doing, and it is predictable frame to frame.
//
// 0x11C is the wrong field here, and this is what makes the boss camera its own
// case. controlPlayer hands the player object straight to the camera as the pair
// of angles it orients from, so the view's yaw is the object's own facing at
// ObjectYawOffset and its pitch is that pair's second entry offset by 0x1E2.
// Pitch therefore answers to an aim field and yaw has no equivalent at all,
// while 0x11C is only the heading the strafe rail runs along. The turn goes to
// the facing alone: the rail must stay where the section put it.
//
// An earlier attempt did write 0x11C, but for every mouse pixel and without a
// bound, so it ran far past what the boss camera will follow and snagged on the
// way back. Both stock gates are reproduced instead: the step is only spent
// while the reticle is pinned past its edge, and the total is held inside
// BossAimYawLimit of where the view sat when the section began.
// The box is small and central, not the screen. controlGetManualAim places the
// reticle as "halfExtent * stick / 0x28" with the stick clamped to 0x28, so it
// is already pinned at half deflection, and the view starts turning at 0x2D --
// just past. The half extent itself is that function's third argument, so it
// lives in an overlay the ROM keeps compressed and is out of reach both there
// and in the decompilation, where the function is still an unnamed GLOBAL_ASM
// stub. BossAimReticleScreenEdge is the one part measured in game: it is the
// offset at which the reticle sits on the edge of the screen. The stock box is
// a fraction of it, and that fraction is the only value here left to taste.
const int32_t BossAimReticleScreenEdge = 0x3F00;
const int32_t BossAimReticleEdge = BossAimReticleScreenEdge / 3;
const int32_t BossAimTurnRateMax = 380;
const int32_t BossAimYawLimit = 0x2000;

// A stick springs back the moment it is released and a mouse cannot, so the
// surplus past the edge is bled off every frame instead. The view then turns
// while the mouse keeps pushing outward and coasts to a stop shortly after it
// stops, and the reticle settles back onto the edge rather than sitting far
// outside it behind a stretch of dead travel that has to be dragged back
// through before it moves at all.
const int32_t BossAimSurplusDecayNumerator = 15;
const int32_t BossAimSurplusDecayDenominator = 16;

// Held against that decay, the surplus settles at "mouse speed * sensitivity *
// decay / (1 - decay)", so a steady drag parks it at a height that depends only
// on how fast the mouse is moving. The two thresholds are therefore quoted as
// the speed in pixels per frame that should just start the view moving and the
// speed that should reach the ramp's fastest notch, which is the only form in
// which they can be reasoned about or tuned. Scaling them off the reticle box
// instead, as the stock stick geometry does, is what made the turn ask for
// hundreds of pixels of extra travel before it would engage.
const int32_t BossAimTurnStartSpeed = 2;
const int32_t BossAimTurnFullSpeed = 12;
const int32_t BossAimSurplusPerSpeed = (MouseAimYawSensitivity * BossAimSurplusDecayNumerator) /
                                       (BossAimSurplusDecayDenominator - BossAimSurplusDecayNumerator);
const int32_t BossAimTurnDeadBand = BossAimTurnStartSpeed * BossAimSurplusPerSpeed;
const int32_t BossAimTurnRange =
    (BossAimTurnFullSpeed - BossAimTurnStartSpeed) * BossAimSurplusPerSpeed;
const int32_t BossAimReticleTravel = BossAimReticleEdge + BossAimTurnDeadBand + BossAimTurnRange;
const uint32_t CameraPitchOffset = 0x4A;
const uint32_t CameraRenderPitchOffset = 0x02;
const uint32_t TransformXOffset = 0x0C;
const uint32_t TransformYOffset = 0x10;
const uint32_t TransformZOffset = 0x14;

// Instruction words the two builds spell differently, see JFG_ADDRESSES.
// Kept beside the addresses so a use site reads the same either way.
uint32_t CameraHelperCallWord = 0x0C00CFE9;
uint32_t ManualAimCursorXGuardWord = 0x15E10002;
uint32_t ManualAimCursorYGuardWord = 0x15610002;
uint32_t ManualAimCursorXStoreWord = 0xA7190000;
uint32_t ManualAimCursorYStoreWord = 0xA58D0000;
uint32_t SchedulerSignatureWord0 = 0x8E480300;
uint32_t SchedulerSignatureWord2 = 0x2D210002;
uint32_t FramePacing60SignatureWord0 = 0x8DCEECCC;
uint32_t FramePacingSignatureWord1 = 0x2442ECAB;
// sb $t7, 0($s1): the gVideoDeltaTime = 1 store the 60fps branch skips ($s2 on PAL).
uint32_t FramePacing60StoreWord = 0xA22F0000;
uint32_t CameraHelperCallReplacement = 0x0C025A33;
// The same store with its source register forced to $zero, so it follows
// whichever base register the build happened to pick.
uint32_t ManualAimCursorXStoreClear = 0xA7000000;
uint32_t ManualAimCursorYStoreClear = 0xA5800000;

struct CAMERA_CODE_PATCH
{
    uint32_t Address;
    uint32_t Original;
    uint32_t Replacement;
    bool FreeCameraPatch;
};

CAMERA_CODE_PATCH CameraCodePatches[] =
{
    { CameraClampBranch, 0x14200004, 0x10000008, true },
    { CameraCenterBranch, 0x1160002F, 0x1000002F, true },
    { CameraOrbitGateBranch, 0x1140003F, 0x00000000, true },
    { CameraOrbitCenterBranch, 0x1560003B, 0x00000000, true },
    { CameraOrbitBranch, 0x11C00006, 0x00000000, true },
    { CameraPositionXBaseCall, 0x8FA200F0, 0x8FA200F0, true },
    { CameraPositionZBaseCall, 0x8FAA00F0, 0x8FAA00F0, true },
    { CameraLookHelperCall, 0x8FA200F0, 0x0C025A3B, true },
    { CameraYawHelperCall, CameraHelperCallWord, CameraHelperCallReplacement, true },
    { CameraPitchHelperCall, CameraHelperCallWord, CameraHelperCallReplacement, true },
    { ManualAimCursorXStore, ManualAimCursorXStoreWord, 0x00000000, false },
    { ManualAimCursorYStore, ManualAimCursorYStoreWord, 0x00000000, false },
    { ManualAimXVelocityStore, 0xE60401E4, 0xE60801E4, false },
    { ManualAimYVelocityStore, 0xE60401E8, 0xE60801E8, false },

    { CameraHeightBlendBase + 0x00, 0x8D230000, 0x92020000, true },
    { CameraHeightBlendBase + 0x04, 0xC7A40090, 0xC7A40090, true },
    { CameraHeightBlendBase + 0x08, 0xC46C0010, 0x00021080, true },
    { CameraHeightBlendBase + 0x0C, 0xC7A800A8, 0x3C01800A, true },
    { CameraHeightBlendBase + 0x10, 0x460C2181, 0x00220821, true },
    { CameraHeightBlendBase + 0x14, 0x46083482, 0xE424F228, true },
    { CameraHeightBlendBase + 0x18, 0x460C9280, 0xC426F238, true },
    { CameraHeightBlendBase + 0x1C, 0xE46A0010, 0xC46C0010, true },
    { CameraHeightBlendBase + 0x20, 0x8D230000, 0x46062100, true },
    { CameraHeightBlendBase + 0x24, 0xC7A40094, 0x460C2181, true },
    { CameraHeightBlendBase + 0x28, 0xC4620014, 0x46083482, true },
    { CameraHeightBlendBase + 0x2C, 0xC7A800A8, 0x460C9280, true },
    { CameraHeightBlendBase + 0x30, 0x46022181, 0xE46A0010, true },
    { CameraHeightBlendBase + 0x34, 0x46083482, 0xC7A40094, true },
    { CameraHeightBlendBase + 0x38, 0x46029280, 0xC4620014, true },
    { CameraHeightBlendBase + 0x3C, 0xE46A0014, 0x46022181, true },
    { CameraHeightBlendBase + 0x40, 0x8D230000, 0x46083482, true },
    { CameraHeightBlendBase + 0x44, 0x00000000, 0x46029280, true },
    { CameraHeightBlendBase + 0x48, 0xC464000C, 0xE46A0014, true },
    { CameraHeightBlendBase + 0x4C, 0x00000000, 0xC464000C, true },
    { CameraHeightBlendBase + 0x50, 0xE4640018, 0xC4660010, true },
    { CameraHeightBlendBase + 0x54, 0x8D230000, 0xC4680014, true },
    { CameraHeightBlendBase + 0x58, 0x00000000, 0xE4640018, true },
    { CameraHeightBlendBase + 0x5C, 0xC4660010, 0xE466001C, true },
    { CameraHeightBlendBase + 0x60, 0x00000000, 0xE4680020, true },
    { CameraHeightBlendBase + 0x64, 0xE466001C, 0x00000000, true },
    { CameraHeightBlendBase + 0x68, 0x8D230000, 0x00000000, true },
    { CameraHeightBlendBase + 0x6C, 0x00000000, 0x00000000, true },
    { CameraHeightBlendBase + 0x70, 0xC4680014, 0x00000000, true },
    { CameraHeightBlendBase + 0x74, 0x00000000, 0x00000000, true },
    { CameraHeightBlendBase + 0x78, 0xE4680020, 0x00000000, true },

    { CameraLookHelperCall + 0x0C, 0x44802000, 0xC7A40098, true },
    { CameraLookHelperCall + 0x38, 0x44805000, 0xC7AA00A0, true },
    { CameraYawHelperCall + 0x14, 0xA5C50000, 0xA5C20000, true },

    { CameraTopDownEntry + 0x00, 0x44866000, 0x08026342, false },
    { CameraTopDownEntry + 0x04, 0x3C06800F, 0x44866000, false },
    { CameraTopDownHelperBase + 0x00, 0x00000000, 0x3C08800A, false },
    { CameraTopDownHelperBase + 0x04, 0x00000000, 0x8D09F24C, false },
    { CameraTopDownHelperBase + 0x08, 0x00000000, 0x3C06800F, false },
    { CameraTopDownHelperBase + 0x0C, 0x00000000, 0x25290001, false },
    { CameraTopDownHelperBase + 0x10, 0x00000000, 0x0800BADD, false },
    { CameraTopDownHelperBase + 0x14, 0x00000000, 0xAD09F24C, false },

    { CameraHelperBase + 0x00, 0x00000000, 0x92080568, false },
    { CameraHelperBase + 0x04, 0x00000000, 0x3108FFFC, false },
    { CameraHelperBase + 0x08, 0x00000000, 0x00000000, false },
    { CameraHelperBase + 0x0C, 0x00000000, 0x15000003, false },
    { CameraHelperBase + 0x10, 0x00000000, 0x00A01025, false },
    { CameraHelperBase + 0x14, 0x00000000, 0x03E00008, false },
    { CameraHelperBase + 0x1C, 0x00000000, 0x0800CFE9, false },
    { CameraHelperBase + 0x20, 0x00000000, 0x92080568, false },
    { CameraHelperBase + 0x24, 0x00000000, 0x3108FFFC, false },
    { CameraHelperBase + 0x28, 0x00000000, 0x00000000, false },
    { CameraHelperBase + 0x2C, 0x00000000, 0x15000003, false },
    { CameraHelperBase + 0x30, 0x00000000, 0x8FA200F0, false },
    { CameraHelperBase + 0x34, 0x00000000, 0xAFA00098, false },
    { CameraHelperBase + 0x38, 0x00000000, 0xAFA000A0, false },
    { CameraHelperBase + 0x3C, 0x00000000, 0x03E00008, false },
};

// Juno's arm aim in overlay 16. The module holds two copies of the same eight
// words, one per group anchor (BoyAimFirstGroupOffset / SecondGroupOffset);
// the offsets here are from that anchor and match in every build. Resolved
// against the live module in PatchManualAimCode; the US build used to spell
// them as the fixed addresses 0x8035C788.. and 0x8035CD28...
struct BOY_AIM_PATCH
{
    uint32_t Offset;
    uint32_t Original;
    uint32_t Replacement;
};
const BOY_AIM_PATCH BoyAimGroupPatches[] =
{
    { 0x00, 0x860401DC, 0x00002025 }, // lh $a0, 0x1DC($s0) -> or $a0, $zero, $zero
    { 0x04, 0x87A50056, 0x24050000 }, // lh $a1, 0x56($sp)  -> addiu $a1, $zero, 0
    { 0x18, 0x87A50054, 0x860501E2 }, // lh $a1, 0x54($sp)  -> lh $a1, 0x1E2($s0)
    { 0x1C, 0x860401DE, 0x00A02025 }, // lh $a0, 0x1DE($s0) -> or $a0, $a1, $zero
    { 0x30, 0x87A50056, 0x24050000 },
    { 0x34, 0x860401CE, 0x00002025 },
    { 0x48, 0x87A50054, 0x860501E2 },
    { 0x4C, 0x860401D0, 0x00A02025 },
};
const size_t BoyAimGroupPatchCount = sizeof(BoyAimGroupPatches) / sizeof(BoyAimGroupPatches[0]);
const uint32_t BoyAimGroupSize = 0x50;

// controlGetManualAim places the reticle from the stick with one halfword
// store per axis (ManualAimCursorXStore / YStore), each preceded by the
// compiler's division guard:
//
//   bnez  $v1, +2          ; the divisor is the constant 0x28, so neither
//   nop                    ; the zero check nor the overflow check can fire
//   break 7
//   addiu $at, $zero, -1
//   bne   $v1, $at, +4
//   lui   $at, 0x8000
//   bne   $tX, $at, +2
//   nop
//   break 6
//   mflo  $rt
//   sh    $rt, 0($base)
//
// The mouse scheme owns those fields for player one, so the store used to be
// turned into `sh $zero`. That took the reticle away from every player at
// once: in split screen and in co-op the second controller's stick could not
// aim at all. The guard being dead code, the eleven words are rewritten in
// place to store nothing for player one and the game's own value for anyone
// else, keyed on the player index byte at the start of the player data:
//
//   lbu   $at, 0($s0)      ; player index; $s0 is the player data throughout
//   nop
//   bne   $at, $zero, +3   ; another player: the game's store
//   mflo  $rt              ; (delay slot) the quotient either way
//   b     +3
//   sh    $zero, 0($base)  ; (delay slot) player one: keep the mouse value
//   sh    $rt, 0($base)
//   nop                    ; x4, through the original store
//
// $at is dead across the window in both builds and $rt is only read by the
// store. The store and the guard's `bne` are where the two compiles picked
// different registers, so both words come from the address table; the mflo
// follows from the store's register. The window entries hold the ten words
// before the store; the store itself stays in CameraCodePatches.
const uint32_t ManualAimCursorWindowBytes = 0x28;
const size_t ManualAimCursorWindowWords = ManualAimCursorWindowBytes / sizeof(uint32_t);
CAMERA_CODE_PATCH ManualAimCursorPatches[2 * ManualAimCursorWindowWords] = {};

void FillManualAimCursorPatches(
    CAMERA_CODE_PATCH * Patches, uint32_t Store, uint32_t StoreWord, uint32_t GuardWord)
{
    const uint32_t Rt = (StoreWord >> 16) & 0x1F;
    const uint32_t Mflo = 0x00000012 | (Rt << 11);
    const uint32_t StoreClear = StoreWord & ~(0x1Fu << 16);
    const CAMERA_CODE_PATCH Window[ManualAimCursorWindowWords] =
    {
        { Store - 0x28, 0x14600002, 0x92010000, false }, // bnez  $v1, +2       -> lbu  $at, 0($s0)
        { Store - 0x24, 0x00000000, 0x00000000, false }, // nop                 -> nop
        { Store - 0x20, 0x0007000D, 0x14200003, false }, // break 7             -> bne  $at, $zero, +3
        { Store - 0x1C, 0x2401FFFF, Mflo,       false }, // addiu $at, $zero,-1 -> mflo $rt
        { Store - 0x18, 0x14610004, 0x10000003, false }, // bne   $v1, $at, +4  -> b    +3
        { Store - 0x14, 0x3C018000, StoreClear, false }, // lui   $at, 0x8000   -> sh   $zero, 0($base)
        { Store - 0x10, GuardWord,  StoreWord,  false }, // bne   $tX, $at, +2  -> sh   $rt, 0($base)
        { Store - 0x0C, 0x00000000, 0x00000000, false }, // nop                 -> nop
        { Store - 0x08, 0x0006000D, 0x00000000, false }, // break 6             -> nop
        { Store - 0x04, Mflo,       0x00000000, false }, // mflo  $rt           -> nop
    };
    memcpy(Patches, Window, sizeof(Window));
}

GAME_HACK_CODE_PATCH FramePacingPatches[] =
{
    { FramePacingEscalateStore, 0xA22D0000, 0x00000000 },
};

GAME_HACK_CODE_PATCH FramePacing60Patches[] =
{
    { FramePacing60Branch, 0x11C00002, 0x00000000 },
};

GAME_HACK_CODE_PATCH SchedulerReleasePatches[] =
{
    { SchedulerFrameGateAdd, 0x25090001, 0x25090002 },
};

// Appends a wake sample on alternate calls only, see WaterWakeRingRateEntry.
// The two nops around the "wake+0x08 is zero" test and the nop in the branch
// delay slot below it are enough room, so no stub is needed. $t5 is dead here
// and $v1 is the engine's own "this is the first sample of a new wake" flag,
// which is carried through so the strip-break marker is never dropped.
GAME_HACK_CODE_PATCH WaterWakeRingRatePatches[] =
{
    { WaterWakeRingRateEntry + 0x00, 0x00000000, 0x928D0002 }, // lbu  $t5, 2($s4)
    { WaterWakeRingRateEntry + 0x08, 0x00000000, 0x01A36825 }, // or   $t5, $t5, $v1
    { WaterWakeRingRateEntry + 0x1C, 0x1020002E, 0x002D0824 }, // and  $at, $at, $t5
    { WaterWakeRingRateEntry + 0x20, 0x00000000, 0x1020002D }, // beq  $at, $zero, 0x8006ABA0
};

// The stub is written before the jump that reaches it.
GAME_HACK_CODE_PATCH ObjectMovePatches[] =
{
    // objMoveXYZ has placed its object and X/Y/Z deltas on the stack. When
    // this is Floyd's actor selected by sidekickpadControl, rotate just X/Z; every
    // other call follows the original optional enemy-speed path unchanged.
    { ObjectMoveStub + 0x00, 0x00000000, 0x8FA70040 }, // lw    $a3, 0x40($sp)
    { ObjectMoveStub + 0x04, 0x00000000, 0x3C01800A }, // lui   $at, 0x800A
    { ObjectMoveStub + 0x08, 0x00000000, 0x8C29FCE4 }, // lw    $t1, 0xFCE4($at)
    { ObjectMoveStub + 0x0C, 0x00000000, 0x312A0001 }, // andi  $t2, $t1, 1
    { ObjectMoveStub + 0x10, 0x00000000, 0x11400016 }, // beq   $t2, $zero, enemy
    { ObjectMoveStub + 0x14, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0x18, 0x00000000, 0x8C28FCFC }, // lw    $t0, 0xFCFC($at)
    { ObjectMoveStub + 0x1C, 0x00000000, 0x14E80013 }, // bne   $a3, $t0, enemy
    { ObjectMoveStub + 0x20, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0x24, 0x00000000, 0x8C2BFCE8 }, // lw    $t3, 0xFCE8($at)
    { ObjectMoveStub + 0x28, 0x00000000, 0x256B0001 }, // addiu $t3, $t3, 1
    { ObjectMoveStub + 0x2C, 0x00000000, 0xAC2BFCE8 }, // sw    $t3, 0xFCE8($at)
    { ObjectMoveStub + 0x30, 0x00000000, 0xC7A00044 }, // lwc1  $f0, 0x44($sp)
    { ObjectMoveStub + 0x34, 0x00000000, 0xC7A2004C }, // lwc1  $f2, 0x4C($sp)
    { ObjectMoveStub + 0x38, 0x00000000, 0x312A0002 }, // andi  $t2, $t1, 2
    { ObjectMoveStub + 0x3C, 0x00000000, 0x11400006 }, // beq   $t2, $zero, left
    { ObjectMoveStub + 0x40, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0x44, 0x00000000, 0x46001107 }, // neg.s $f4, $f2
    { ObjectMoveStub + 0x48, 0x00000000, 0xE7A40044 }, // swc1  $f4, 0x44($sp)
    { ObjectMoveStub + 0x4C, 0x00000000, 0xE7A0004C }, // swc1  $f0, 0x4C($sp)
    { ObjectMoveStub + 0x50, 0x00000000, 0x0800268A }, // j     0x80009A28
    { ObjectMoveStub + 0x54, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0x58, 0x00000000, 0xE7A20044 }, // left: swc1 $f2, 0x44($sp)
    { ObjectMoveStub + 0x5C, 0x00000000, 0x46000107 }, // neg.s $f4, $f0
    { ObjectMoveStub + 0x60, 0x00000000, 0xE7A4004C }, // swc1  $f4, 0x4C($sp)
    { ObjectMoveStub + 0x64, 0x00000000, 0x0800268A }, // j     0x80009A28
    { ObjectMoveStub + 0x68, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0x6C, 0x00000000, 0x8C28FCE0 }, // enemy: lw $t0, 0xFCE0($at)
    { ObjectMoveStub + 0x70, 0x00000000, 0x11000015 }, // beq   $t0, $zero, resume
    { ObjectMoveStub + 0x74, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0x78, 0x00000000, 0x84E80048 }, // lh    $t0, 0x48($a3)  ; behaviorId
    { ObjectMoveStub + 0x7C, 0x00000000, 0x24010001 }, // addiu $at, $zero, 1
    { ObjectMoveStub + 0x80, 0x00000000, 0x11010011 }, // beq   $t0, $at, resume ; player-type (bid 1): don't halve
    { ObjectMoveStub + 0x84, 0x00000000, 0x240100A2 }, // addiu $at, $zero, 0xA2 ; (delay slot) at = 162 laserbolt
    { ObjectMoveStub + 0x88, 0x00000000, 0x1101000F }, // beq   $t0, $at, resume ; laserbolt (bid 162): delta-weighted, don't halve
    { ObjectMoveStub + 0x8C, 0x00000000, 0x00000000 }, // nop (delay slot)
    { ObjectMoveStub + 0x90, 0x00000000, 0x3C013F00 }, // lui   $at, 0x3F00
    { ObjectMoveStub + 0x94, 0x00000000, 0x44818000 }, // mtc1  $at, $f16
    { ObjectMoveStub + 0x98, 0x00000000, 0xC7B20044 }, // lwc1  $f18, 0x44($sp)
    { ObjectMoveStub + 0x9C, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0xA0, 0x00000000, 0x46109482 }, // mul.s $f18, $f18, $f16
    { ObjectMoveStub + 0xA4, 0x00000000, 0xE7B20044 }, // swc1  $f18, 0x44($sp)
    { ObjectMoveStub + 0xA8, 0x00000000, 0xC7B20048 }, // lwc1  $f18, 0x48($sp)
    { ObjectMoveStub + 0xAC, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0xB0, 0x00000000, 0x46109482 }, // mul.s $f18, $f18, $f16
    { ObjectMoveStub + 0xB4, 0x00000000, 0xE7B20048 }, // swc1  $f18, 0x48($sp)
    { ObjectMoveStub + 0xB8, 0x00000000, 0xC7B2004C }, // lwc1  $f18, 0x4C($sp)
    { ObjectMoveStub + 0xBC, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveStub + 0xC0, 0x00000000, 0x46109482 }, // mul.s $f18, $f18, $f16
    { ObjectMoveStub + 0xC4, 0x00000000, 0xE7B2004C }, // swc1  $f18, 0x4C($sp)
    { ObjectMoveStub + 0xC8, 0x00000000, 0x0800268A }, // resume: j 0x80009A28
    { ObjectMoveStub + 0xCC, 0x00000000, 0x00000000 }, // nop
    { ObjectMoveEntry, 0x8FA70040, ObjectMoveJump },
};

// Integrates our own lateral velocity and adds it to the object velocity along
// the perpendicular of Floyd's heading, leaving the heading itself untouched so
// the drone and camera keep facing forward.
//
// $at must survive: 0x80030030 loaded 0x800B0000 into it for the constant read
// at 0x800300B0, so the scratch base lives in $t9. $f6 must hold the X velocity
// and $f4 the frame step on the way out, which is what the resumed code
// consumes at 0x80030068; $f4 comes from the displaced instruction in the delay
// slot. $f2, $f10, $f14, $f16 and $f18 are all written before they are read
// again, $t2 is reloaded at 0x80030074, and $t1, $t3, $t8 and $t9 are never
// read in this function. When the flag is clear $f6 is still the value loaded
// at 0x80030054, so the pass-through path is bit-identical to the original.
const uint32_t SidekickStrafeHookCode[] =
{
    0x8FB80120, // lw    $t8, 0x120($sp)     Floyd object
    0x3C19800A, // lui   $t9, 0x800A
    0xC6020034, // lwc1  $f2, 0x34($s0)      engine forward speed
    0xE722FCC4, // swc1  $f2, 0xFCC4($t9)    published to compare the two ramps
    0x8F29FCE4, // lw    $t1, 0xFCE4($t9)
    0x312A0001, // andi  $t2, $t1, 1
    0x1140001A, // beq   $t2, $zero, vertical (its inactive path replays $f8)
    0x00000000, // nop
    0x8F2BFCE8, // lw    $t3, 0xFCE8($t9)    diagnostic: hook hit count
    0x256B0001, // addiu $t3, $t3, 1
    0xAF2BFCE8, // sw    $t3, 0xFCE8($t9)
    0xC730FCAC, // lwc1  $f16, 0xFCAC($t9)   lateral velocity, ours to keep
    0xC732FCC0, // lwc1  $f18, 0xFCC0($t9)   drag
    0x46128402, // mul.s $f16, $f16, $f18
    0xC732FCA8, // lwc1  $f18, 0xFCA8($t9)   side thrust, zero when not strafing
    0x46049482, // mul.s $f18, $f18, $f4     * frame step
    0x46128400, // add.s $f16, $f16, $f18
    0xC732FCA4, // lwc1  $f18, 0xFCA4($t9)   lateral speed cap
    0x46008085, // abs.s $f2, $f16
    0x4612103E, // c.le.s $f2, $f18
    0x00000000, // nop
    0x45010003, // bc1t  within the cap
    0x00000000, // nop
    0x46028403, // div.s $f16, $f16, $f2     sign, safe since |v| > cap > 0
    0x46128402, // mul.s $f16, $f16, $f18
    0xE730FCAC, // swc1  $f16, 0xFCAC($t9)
    0xC72AFCC8, // lwc1  $f10, 0xFCC8($t9)   camera right X
    0xC72EFCB0, // lwc1  $f14, 0xFCB0($t9)   camera right Z
    0x460A8082, // mul.s $f2, $f16, $f10     lateral * right X
    0x460E8482, // mul.s $f18, $f16, $f14    lateral * right Z
    0x46023180, // add.s $f6, $f6, $f2       velocity X += lateral * rightX
    0xC7020024, // lwc1  $f2, 0x24($t8)
    0x46121080, // add.s $f2, $f2, $f18      velocity Z += lateral * rightZ, signed
    0x08019BC0, // j     SidekickVerticalStub (relocated when installed)
    0x00000000, // nop
};

// Continue with world-up thrust, independent of camera pitch/yaw. $f6/$f2
// carry X/Z from the lateral half; $f4 is still the native frame step. The
// engine reads object+0x20 for Y immediately after this hook, then applies its
// normal position/collision work. Both axes share acceleration, drag and cap.
const uint32_t SidekickVerticalHookCode[] =
{
    0x11400024, // beq   $t2, $zero, done
    0x00000000, // nop
    0xC730FCD4, // lwc1  $f16, 0xFCD4($t9)   vertical velocity
    0xC732FCC0, // lwc1  $f18, 0xFCC0($t9)   drag
    0x46128402, // mul.s $f16, $f16, $f18
    0xC732FCD0, // lwc1  $f18, 0xFCD0($t9)   signed vertical thrust
    0x46049482, // mul.s $f18, $f18, $f4
    0x46128400, // add.s $f16, $f16, $f18
    0xC732FCA4, // lwc1  $f18, 0xFCA4($t9)   speed cap
    0x46008385, // abs.s $f14, $f16
    0x4612703E, // c.le.s $f14, $f18
    0x00000000, // nop
    0x45010003, // bc1t  store vertical velocity
    0x00000000, // nop
    0x460E8403, // div.s $f16, $f16, $f14
    0x46128402, // mul.s $f16, $f16, $f18
    0xE730FCD4, // swc1  $f16, 0xFCD4($t9)
    0xC70A0020, // lwc1  $f10, 0x20($t8)     native vertical velocity
    0x46105280, // add.s $f10, $f10, $f16
    // Bound all three components together, including native forward flight.
    0x46063382, // mul.s $f14, $f6, $f6
    0x46021482, // mul.s $f18, $f2, $f2
    0x46127380, // add.s $f14, $f14, $f18
    0x460A5482, // mul.s $f18, $f10, $f10
    0x46127380, // add.s $f14, $f14, $f18
    0x46007384, // sqrt.s $f14, $f14
    0xC730FCA4, // lwc1  $f16, 0xFCA4($t9)
    0x4610703E, // c.le.s $f14, $f16
    0x00000000, // nop
    0x45010005, // bc1t  store velocity
    0x00000000, // nop
    0x460E8403, // div.s $f16, $f16, $f14
    0x46103182, // mul.s $f6, $f6, $f16
    0x46101082, // mul.s $f2, $f2, $f16
    0x46105282, // mul.s $f10, $f10, $f16
    0xE706001C, // swc1  $f6, 0x1C($t8)
    0xE70A0020, // swc1  $f10, 0x20($t8)
    0xE7020024, // swc1  $f2, 0x24($t8)
    SidekickStrafeDelayOriginal, // done: replay the overwritten lwc1 $f8, 0x0($s0)
    0x0800C01A, // j     SidekickStrafeResumeJump (relocated when installed)
    0x00000000, // nop
};
static_assert(sizeof(SidekickStrafeHookCode) <= 0xC0, "Floyd lateral cave overflow");
static_assert(sizeof(SidekickVerticalHookCode) <= 0xC4, "Floyd vertical cave overflow");

// sidekickpadControl tail-dispatches through a twelve-entry state table. This
// probe preserves its complete prologue, then records the selected state in a
// scratch word for the input-rate diagnostic. It deliberately changes neither
// the selected handler nor its inputs.
const uint32_t SidekickPadProbeCode[] =
{
    0xAFBF0034, // sw    $ra, 0x34($sp)
    0xAFB00030, // sw    $s0, 0x30($sp)
    0xAFA40068, // sw    $a0, 0x68($sp)
    0xAFA5006C, // sw    $a1, 0x6C($sp)
    0x8C900068, // lw    $s0, 0x68($a0)
    0x3C01800A, // lui   $at, 0x800A
    0xAC24FCDC, // sw    $a0, 0xFCDC($at)
    0x8E08000C, // lw    $t0, 0x0C($s0)
    0xAC28FCFC, // sw    $t0, 0xFCFC($at)
    0x92090017, // lbu   $t1, 0x17($s0)
    0xAC29FCCC, // sw    $t1, 0xFCCC($at)
    0x00000000, // j     sidekickpadControl + 0x18 (filled at install)
    0x00000000, // nop
};
const uint32_t SidekickPadProbeResumeIndex = 11;

bool IsManualAimCameraMode(uint8_t CameraMode)
{
    return CameraMode == PlayerCameraModeManualAim || CameraMode == PlayerCameraModeCrouchAim ||
           CameraMode == PlayerCameraModeBossAim;
}

bool IsManualAimAnglePatch(uint32_t Address)
{
    return Address == CameraYawHelperCall || Address == CameraPitchHelperCall;
}

// The words that take the reticle away from the game's stick for player one:
// the two cursor stores of controlGetManualAim with the guard window rewritten
// in front of each, see ManualAimCursorPatches. Stock aim puts them all back.
bool IsManualAimStorePatch(uint32_t Address)
{
    return (Address >= ManualAimCursorXStore - ManualAimCursorWindowBytes &&
            Address <= ManualAimCursorXStore) ||
           (Address >= ManualAimCursorYStore - ManualAimCursorWindowBytes &&
            Address <= ManualAimCursorYStore);
}

// The velocity stores of the same function used to be redirected to store the
// stick's target instead of the smoothed value. With player one's stick at
// rest in the mouse aim that target is zero and the runtime zeroes the
// velocities every video frame anyway, so the redirect changed nothing for
// player one while denying the other players their smoothing. They stay at
// the original words now; the entries remain so a state saved with the old
// replacement is recognised and restored.
bool IsManualAimVelocityPatch(uint32_t Address)
{
    return Address == ManualAimXVelocityStore || Address == ManualAimYVelocityStore;
}

bool GetLegacyCameraHeightInstruction(uint32_t Address, uint32_t & Instruction)
{
    // Switching on the offset keeps these labels compile-time constants
    // now that the base address comes from the per-ROM table.
    const uint32_t Offset = Address - CameraHeightBlendBase;
    switch (Offset)
    {
    case 0x00: Instruction = 0x3C01800A; return true;
    case 0x04: Instruction = 0xC424F248; return true;
    case 0x08: Instruction = 0xC7A60090; return true;
    case 0x0C: Instruction = 0x46043100; return true;
    case 0x10: Instruction = 0xC46C0010; return true;
    case 0x14: Instruction = 0x460C2181; return true;
    case 0x18: Instruction = 0x46083482; return true;
    case 0x1C: Instruction = 0x460C9280; return true;
    case 0x20: Instruction = 0xE46A0010; return true;
    default: return false;
    }
}

uint32_t WithHi(uint32_t Instruction, uint32_t Address);
uint32_t WithLo(uint32_t Instruction, uint32_t Address);

// The height blend as the builds before the per-player tables wrote it, so a
// state carrying it is recognised and rewritten.
bool GetPreviousCameraHeightInstruction(uint32_t Address, uint32_t & Instruction)
{
    const uint32_t Offset = Address - CameraHeightBlendBase;
    switch (Offset)
    {
    case 0x00: Instruction = WithHi(0x3C010000, CameraHeightOffsetAddress); return true;
    case 0x04: Instruction = 0xC7A40090; return true;
    case 0x08: Instruction = WithLo(0xE4240000, CameraNativeYAddress); return true;
    case 0x0C: Instruction = WithLo(0xC4260000, CameraHeightOffsetAddress); return true;
    case 0x10: Instruction = 0x46062100; return true;
    case 0x14: Instruction = 0xC46C0010; return true;
    case 0x18: Instruction = 0x460C2181; return true;
    case 0x1C: Instruction = 0x46083482; return true;
    case 0x20: Instruction = 0x460C9280; return true;
    case 0x24: Instruction = 0xE46A0010; return true;
    case 0x28: Instruction = 0xC7A40094; return true;
    case 0x2C: Instruction = 0xC4620014; return true;
    case 0x30: Instruction = 0x46022181; return true;
    case 0x34: Instruction = 0x46083482; return true;
    case 0x38: Instruction = 0x46029280; return true;
    case 0x3C: Instruction = 0xE46A0014; return true;
    default: return false;
    }
}

bool GetLegacyCameraLookHelperInstruction(uint32_t Address, uint32_t & Instruction)
{
    // Switching on the offset keeps these labels compile-time constants
    // now that the base address comes from the per-ROM table.
    const uint32_t Offset = Address - CameraHelperBase;
    switch (Offset)
    {
    case 0x00: Instruction = 0x92080568; return true;
    case 0x04: Instruction = 0x8FA900DC; return true;
    case 0x08: Instruction = 0x01094025; return true;
    case 0x0C: Instruction = 0x15000003; return true;
    case 0x10: Instruction = 0x00A01025; return true;
    case 0x14: Instruction = 0x03E00008; return true;
    case 0x1C: Instruction = 0x0800CFE9; return true;
    case 0x20: Instruction = 0x92080568; return true;
    case 0x24: Instruction = 0x8FA900DC; return true;
    case 0x28: Instruction = 0x01094025; return true;
    case 0x2C: Instruction = 0x15000003; return true;
    case 0x30: Instruction = 0x8FA200F0; return true;
    case 0x34: Instruction = 0xAFA00098; return true;
    case 0x38: Instruction = 0xAFA000A0; return true;
    case 0x3C: Instruction = 0x03E00008; return true;
    default: return false;
    }
}

bool GetStrictCameraLookHelperInstruction(uint32_t Address, uint32_t & Instruction)
{
    // Switching on the offset keeps these labels compile-time constants
    // now that the base address comes from the per-ROM table.
    const uint32_t Offset = Address - CameraHelperBase;
    switch (Offset)
    {
    case 0x20: Instruction = 0x8FA200F0; return true;
    case 0x24: Instruction = 0x3C014220; return true;
    case 0x28: Instruction = 0xAFA00098; return true;
    case 0x2C: Instruction = 0xAFA1009C; return true;
    case 0x30: Instruction = 0xAFA000A0; return true;
    case 0x34: Instruction = 0x03E00008; return true;
    case 0x38: Instruction = 0x00000000; return true;
    case 0x3C: Instruction = 0x00000000; return true;
    default: return false;
    }
}

bool GetPlayerObjectCameraLookHelperInstruction(
    uint32_t Address, bool UseAlternateScratch, uint32_t & Instruction)
{
    // Switching on the offset keeps these labels compile-time constants
    // now that the base address comes from the per-ROM table.
    const uint32_t Offset = Address - CameraHelperBase;
    switch (Offset)
    {
    case 0x20: Instruction = 0x3C02800A; return true;
    case 0x24: Instruction = UseAlternateScratch ? 0x8C42F250 : 0x8C42F240; return true;
    case 0x28: Instruction = 0x3C014220; return true;
    case 0x2C: Instruction = 0xAFA00098; return true;
    case 0x30: Instruction = 0xAFA1009C; return true;
    case 0x34: Instruction = 0xAFA000A0; return true;
    case 0x38: Instruction = 0x03E00008; return true;
    case 0x3C: Instruction = 0x00000000; return true;
    default: return false;
    }
}

bool GetObsoleteCameraPositionBaseInstruction(uint32_t Address, uint32_t & Instruction)
{
    // Two unrelated addresses rather than one base and offsets, so this cannot
    // switch on an offset the way the helpers above do.
    if (Address != CameraPositionXBaseCall && Address != CameraPositionZBaseCall)
    {
        return false;
    }
    Instruction = 0x0C027F2B;
    return true;
}

void AddAllowedCodeValue(GAME_HACK_CODE_WRITE & Write, uint32_t Value)
{
    for (size_t i = 0; i < Write.AllowedCount; i++)
    {
        if (Write.Allowed[i] == Value)
        {
            return;
        }
    }
    if (Write.AllowedCount < GAME_HACK_CODE_WRITE::MaxAllowedValues)
    {
        Write.Allowed[Write.AllowedCount++] = Value;
    }
}

float ClampCameraHeight(float Value)
{
    if (Value > MouseCameraHeightLimit)
    {
        return MouseCameraHeightLimit;
    }
    if (Value < -MouseCameraHeightLimit)
    {
        return -MouseCameraHeightLimit;
    }
    return Value;
}

bool IsCameraFloat(float Value)
{
    return Value > -1000000.0f && Value < 1000000.0f;
}
// Points every address and every build-specific instruction word at the table
// for the ROM in hand. The declarations above keep their US values as their
// initialiser so this file still reads naturally, but nothing relies on them:
// this runs before any patch is applied, and re-runs whenever the ROM changes.
// A jump or a call to an address, and the two halves of an address as a
// lui/low pair. The low half is sign extended when it is used, so the upper
// half has to carry the borrow -- getting that wrong is a one-in-two chance of
// an address that is off by 0x10000 and only on some builds.
uint32_t JumpTo(uint32_t Target)
{
    return 0x08000000 | ((Target >> 2) & 0x03FFFFFF);
}

uint32_t CallTo(uint32_t Target)
{
    return 0x0C000000 | ((Target >> 2) & 0x03FFFFFF);
}

uint32_t Hi16(uint32_t Address)
{
    return ((Address >> 16) + ((Address & 0x8000) != 0 ? 1 : 0)) & 0xFFFF;
}

uint32_t Lo16(uint32_t Address)
{
    return Address & 0xFFFF;
}

// Rebuilds an instruction, keeping its opcode and registers and replacing only
// the immediate. That way each payload below stays recognisable as the
// instruction it was written as.
uint32_t WithHi(uint32_t Instruction, uint32_t Address)
{
    return (Instruction & 0xFFFF0000) | Hi16(Address);
}

uint32_t WithLo(uint32_t Instruction, uint32_t Address)
{
    return (Instruction & 0xFFFF0000) | Lo16(Address);
}

void ApplyAddressTable(const JFG_ADDRESSES & A)
{
    ObjectMoveEntry = A.ObjectMoveEntry;
    ObjectMoveResume = A.ObjectMoveResume;
    CameraClampBranch = A.CameraClampBranch;
    CameraCenterBranch = A.CameraCenterBranch;
    CameraOrbitGateBranch = A.CameraOrbitGateBranch;
    CameraOrbitCenterBranch = A.CameraOrbitCenterBranch;
    CameraOrbitBranch = A.CameraOrbitBranch;
    CameraPositionXBaseCall = A.CameraPositionXBaseCall;
    CameraPositionZBaseCall = A.CameraPositionZBaseCall;
    CameraHeightBlendBase = A.CameraHeightBlendBase;
    CameraLookHelperCall = A.CameraLookHelperCall;
    CameraYawHelperCall = A.CameraYawHelperCall;
    CameraPitchHelperCall = A.CameraPitchHelperCall;
    CameraTopDownEntry = A.CameraTopDownEntry;
    SidekickStrafeEntry = A.SidekickStrafeEntry;
    SidekickStrafeDelay = A.SidekickStrafeDelay;
    ManualAimXVelocityStore = A.ManualAimXVelocityStore;
    ManualAimYVelocityStore = A.ManualAimYVelocityStore;
    ManualAimCursorXStore = A.ManualAimCursorXStore;
    ManualAimCursorYStore = A.ManualAimCursorYStore;
    LandingCinematicSkipEntry = A.LandingCinematicSkipEntry;
    LegacyLandingCinematicSkipEntry = A.LegacyLandingCinematicSkipEntry;
    SchedulerSignatureBase = A.SchedulerSignatureBase;
    SchedulerFrameGateAdd = A.SchedulerFrameGateAdd;
    FramePacingSignatureBase = A.FramePacingSignatureBase;
    FramePacingEscalateStore = A.FramePacingEscalateStore;
    FramePacing60SignatureBase = A.FramePacing60SignatureBase;
    FramePacing60Branch = A.FramePacing60Branch;
    SidekickStrafeStub = A.SidekickStrafeStub;
    SidekickVerticalStub = A.SidekickVerticalStub;
    SidekickPadProbeStub = A.SidekickPadProbeStub;
    ObjectMoveStub = A.ObjectMoveStub;
    LandingCinematicSkipStub = A.LandingCinematicSkipStub;
    WaterWakeRingRateEntry = A.WaterWakeRingRateEntry;
    CameraAngleHelper = A.CameraAngleHelper;
    CameraHelperBase = A.CameraHelperBase;
    CameraTopDownHelperBase = A.CameraTopDownHelperBase;
    CameraNativeYAddress = A.CameraNativeYAddress;
    CameraHeightOffsetAddress = A.CameraHeightOffsetAddress;
    CameraNativeYTableAddress = CameraNativeYAddress - 0x1C;
    CameraHeightTableAddress = CameraNativeYAddress - 0x0C;
    CameraTopDownCounterAddress = A.CameraTopDownCounterAddress;
    SidekickControlObjectAddress = A.SidekickControlObjectAddress;
    DroneLateralMaxSpeedAddress = A.DroneLateralMaxSpeedAddress;
    DroneLateralSideFactorAddress = A.DroneLateralSideFactorAddress;
    DroneLateralVelocityAddress = A.DroneLateralVelocityAddress;
    DroneVerticalThrustAddress = A.DroneVerticalThrustAddress;
    DroneVerticalVelocityAddress = A.DroneVerticalVelocityAddress;
    DroneLateralRightZAddress = A.DroneLateralRightZAddress;
    LandingCinematicSkipInputAddress = A.LandingCinematicSkipInputAddress;
    DroneLateralDragAddress = A.DroneLateralDragAddress;
    DroneLateralForwardSpeedAddress = A.DroneLateralForwardSpeedAddress;
    DroneLateralRightXAddress = A.DroneLateralRightXAddress;
    SidekickPadProbeStateAddress = A.SidekickPadProbeStateAddress;
    DroneLateralPreviousObjectAddress = A.DroneLateralPreviousObjectAddress;
    SidekickPadProbeObjectAddress = A.SidekickPadProbeObjectAddress;
    EnemyHalveFlagAddress = A.EnemyHalveFlagAddress;
    DroneLateralFlagsAddress = A.DroneLateralFlagsAddress;
    DroneLateralHookHitsAddress = A.DroneLateralHookHitsAddress;
    SidekickPadProbeActorAddress = A.SidekickPadProbeActorAddress;
    RobotMissionAddress = A.RobotMissionAddress;
    MultiplayerGameAddress = A.MultiplayerGameAddress;
    CooperativeGameAddress = A.CooperativeGameAddress;
    WaterWakeObjectListAddress = A.WaterWakeObjectListAddress;
    WaterWakeObjectCountAddress = A.WaterWakeObjectCountAddress;
    PlayerListAddress = A.PlayerListAddress;
    PlayerCountAddress = A.PlayerCountAddress;
    DisableJoyAddress = A.DisableJoyAddress;
    ControlCameraAddress = A.ControlCameraAddress;
    CameraActiveOverrideBase = A.CameraActiveOverrideBase;
    CameraArrayAddress = A.CameraArrayAddress;
    CameraFovAddress = A.CameraFovAddress;
    LobbyCameraInUseAddress = A.LobbyCameraInUseAddress;
    StaticCameraInUseAddress = A.StaticCameraInUseAddress;
    OverlayTableAddress = A.OverlayTableAddress;
    CurrentScreenAddress = A.CurrentScreenAddress;
    AnimseqCameraAddress = A.AnimseqCameraAddress;
    IntroCinematicSkipStub = A.IntroCinematicSkipStub;
    CurrentSceneAddress = A.CurrentSceneAddress;
    CurrentSetupAddress = A.CurrentSetupAddress;
    NextCharacterAddress = A.NextCharacterAddress;
    LoadingAddress = A.LoadingAddress;
    MainFrontInitFunction = A.MainFrontInitFunction;
    FrontCharSelectSetQuitModeFunction = A.FrontCharSelectSetQuitModeFunction;
    FrontGetModeFunction = A.FrontGetModeFunction;
    MainChangeLevelFunction = A.MainChangeLevelFunction;

    // Overlay-relative offsets. Every group keeps its internal spacing in all
    // builds, so only its anchor comes from the table.
    FloydPadControlOffset = A.FloydPadControlOffset;
    IntroCinematicSkipEntryOffset = A.IntroCinematicSkipEntryOffset;
    IntroCinematicSkipResumeOffset = IntroCinematicSkipEntryOffset + 0x08;
    LegacyIntroCinematicSkipEntryOffset = A.LegacyIntroCinematicSkipEntryOffset;
    LegacyIntroCinematicSkipFmvUpdateOffset = A.LegacyIntroCinematicSkipFmvUpdateOffset;
    TargetOverlayCursorXOffset = A.TargetOverlayCursorXOffset;
    TargetOverlayCursorYOffset = A.TargetOverlayCursorYOffset;
    TargetOverlayDrawOffset = A.TargetOverlayDrawOffset;
    BoyAimHelperOffset = A.BoyAimHelperOffset;
    BoyAimFirstGroupOffset = A.BoyAimFirstGroupOffset;
    BoyAimSecondGroupOffset = A.BoyAimSecondGroupOffset;
    FramePacing60StoreWord = A.FramePacing60StoreWord;

    // Recomputed rather than tabulated, so they cannot drift from the addresses.
    LandingCinematicSkipStubTable = LandingCinematicSkipStub + 0x160;
    IntroCinematicSkipJump = JumpTo(IntroCinematicSkipStub);

    // Build-specific instruction words, and the three replacements derived from
    // them so they follow whichever registers the compile happened to choose.
    CameraHelperCallWord = A.CameraHelperCallWord;
    ManualAimCursorXGuardWord = A.ManualAimCursorXGuardWord;
    ManualAimCursorYGuardWord = A.ManualAimCursorYGuardWord;
    ManualAimCursorXStoreWord = A.ManualAimCursorXStoreWord;
    ManualAimCursorYStoreWord = A.ManualAimCursorYStoreWord;
    SchedulerSignatureWord0 = A.SchedulerSignatureWord0;
    SchedulerSignatureWord2 = A.SchedulerSignatureWord2;
    FramePacing60SignatureWord0 = A.FramePacing60SignatureWord0;
    FramePacingSignatureWord1 = A.FramePacingSignatureWord1;
    CameraHelperCallReplacement = 0x0C000000 | ((CameraHelperBase >> 2) & 0x03FFFFFF);
    ManualAimCursorXStoreClear = ManualAimCursorXStoreWord & ~(0x1Fu << 16);
    ManualAimCursorYStoreClear = ManualAimCursorYStoreWord & ~(0x1Fu << 16);
    FillManualAimCursorPatches(
        ManualAimCursorPatches, ManualAimCursorXStore, ManualAimCursorXStoreWord, ManualAimCursorXGuardWord);
    FillManualAimCursorPatches(
        ManualAimCursorPatches + ManualAimCursorWindowWords, ManualAimCursorYStore, ManualAimCursorYStoreWord,
        ManualAimCursorYGuardWord);

    // The camera table carries addresses too, so it is rebuilt like the others.
    CameraCodePatches[0] = { CameraClampBranch, 0x14200004, 0x10000008, true };
    CameraCodePatches[1] = { CameraCenterBranch, 0x1160002F, 0x1000002F, true };
    CameraCodePatches[2] = { CameraOrbitGateBranch, 0x1140003F, 0x00000000, true };
    CameraCodePatches[3] = { CameraOrbitCenterBranch, 0x1560003B, 0x00000000, true };
    CameraCodePatches[4] = { CameraOrbitBranch, 0x11C00006, 0x00000000, true };
    CameraCodePatches[5] = { CameraPositionXBaseCall, 0x8FA200F0, 0x8FA200F0, true };
    CameraCodePatches[6] = { CameraPositionZBaseCall, 0x8FAA00F0, 0x8FAA00F0, true };
    CameraCodePatches[7] = { CameraLookHelperCall, 0x8FA200F0, CallTo(CameraHelperBase + 0x20), true };
    CameraCodePatches[8] = { CameraYawHelperCall, CameraHelperCallWord, CameraHelperCallReplacement, true };
    CameraCodePatches[9] = { CameraPitchHelperCall, CameraHelperCallWord, CameraHelperCallReplacement, true };
    CameraCodePatches[10] = { ManualAimCursorXStore, ManualAimCursorXStoreWord, 0x00000000, false };
    CameraCodePatches[11] = { ManualAimCursorYStore, ManualAimCursorYStoreWord, 0x00000000, false };
    CameraCodePatches[12] = { ManualAimXVelocityStore, 0xE60401E4, 0xE60801E4, false };
    CameraCodePatches[13] = { ManualAimYVelocityStore, 0xE60401E8, 0xE60801E8, false };
    CameraCodePatches[14] = { CameraHeightBlendBase + 0x00, 0x8D230000, 0x92020000, true };
    CameraCodePatches[15] = { CameraHeightBlendBase + 0x04, 0xC7A40090, 0xC7A40090, true };
    CameraCodePatches[16] = { CameraHeightBlendBase + 0x08, 0xC46C0010, 0x00021080, true };
    CameraCodePatches[17] = { CameraHeightBlendBase + 0x0C, 0xC7A800A8, WithHi(0x3C010000, CameraNativeYTableAddress), true };
    CameraCodePatches[18] = { CameraHeightBlendBase + 0x10, 0x460C2181, 0x00220821, true };
    CameraCodePatches[19] = { CameraHeightBlendBase + 0x14, 0x46083482, WithLo(0xE4240000, CameraNativeYTableAddress), true };
    CameraCodePatches[20] = { CameraHeightBlendBase + 0x18, 0x460C9280, WithLo(0xC4260000, CameraHeightTableAddress), true };
    CameraCodePatches[21] = { CameraHeightBlendBase + 0x1C, 0xE46A0010, 0xC46C0010, true };
    CameraCodePatches[22] = { CameraHeightBlendBase + 0x20, 0x8D230000, 0x46062100, true };
    CameraCodePatches[23] = { CameraHeightBlendBase + 0x24, 0xC7A40094, 0x460C2181, true };
    CameraCodePatches[24] = { CameraHeightBlendBase + 0x28, 0xC4620014, 0x46083482, true };
    CameraCodePatches[25] = { CameraHeightBlendBase + 0x2C, 0xC7A800A8, 0x460C9280, true };
    CameraCodePatches[26] = { CameraHeightBlendBase + 0x30, 0x46022181, 0xE46A0010, true };
    CameraCodePatches[27] = { CameraHeightBlendBase + 0x34, 0x46083482, 0xC7A40094, true };
    CameraCodePatches[28] = { CameraHeightBlendBase + 0x38, 0x46029280, 0xC4620014, true };
    CameraCodePatches[29] = { CameraHeightBlendBase + 0x3C, 0xE46A0014, 0x46022181, true };
    CameraCodePatches[30] = { CameraHeightBlendBase + 0x40, 0x8D230000, 0x46083482, true };
    CameraCodePatches[31] = { CameraHeightBlendBase + 0x44, 0x00000000, 0x46029280, true };
    CameraCodePatches[32] = { CameraHeightBlendBase + 0x48, 0xC464000C, 0xE46A0014, true };
    CameraCodePatches[33] = { CameraHeightBlendBase + 0x4C, 0x00000000, 0xC464000C, true };
    CameraCodePatches[34] = { CameraHeightBlendBase + 0x50, 0xE4640018, 0xC4660010, true };
    CameraCodePatches[35] = { CameraHeightBlendBase + 0x54, 0x8D230000, 0xC4680014, true };
    CameraCodePatches[36] = { CameraHeightBlendBase + 0x58, 0x00000000, 0xE4640018, true };
    CameraCodePatches[37] = { CameraHeightBlendBase + 0x5C, 0xC4660010, 0xE466001C, true };
    CameraCodePatches[38] = { CameraHeightBlendBase + 0x60, 0x00000000, 0xE4680020, true };
    CameraCodePatches[39] = { CameraHeightBlendBase + 0x64, 0xE466001C, 0x00000000, true };
    CameraCodePatches[40] = { CameraHeightBlendBase + 0x68, 0x8D230000, 0x00000000, true };
    CameraCodePatches[41] = { CameraHeightBlendBase + 0x6C, 0x00000000, 0x00000000, true };
    CameraCodePatches[42] = { CameraHeightBlendBase + 0x70, 0xC4680014, 0x00000000, true };
    CameraCodePatches[43] = { CameraHeightBlendBase + 0x74, 0x00000000, 0x00000000, true };
    CameraCodePatches[44] = { CameraHeightBlendBase + 0x78, 0xE4680020, 0x00000000, true };
    CameraCodePatches[45] = { CameraLookHelperCall + 0x0C, 0x44802000, 0xC7A40098, true };
    CameraCodePatches[46] = { CameraLookHelperCall + 0x38, 0x44805000, 0xC7AA00A0, true };
    CameraCodePatches[47] = { CameraYawHelperCall + 0x14, 0xA5C50000, 0xA5C20000, true };
    CameraCodePatches[48] = { CameraTopDownEntry + 0x00, 0x44866000, JumpTo(CameraTopDownHelperBase), false };
    CameraCodePatches[49] = { CameraTopDownEntry + 0x04, 0x3C06800F, 0x44866000, false };
    CameraCodePatches[50] = { CameraTopDownHelperBase + 0x00, 0x00000000, WithHi(0x3C080000, CameraTopDownCounterAddress), false };
    CameraCodePatches[51] = { CameraTopDownHelperBase + 0x04, 0x00000000, WithLo(0x8D090000, CameraTopDownCounterAddress), false };
    // A lone lui: its low half is supplied by the game code jumped to below, so
    // there is no pair here to name the global. Both builds keep their 0x800F
    // globals in the same 64K page, so the upper half is the same either way.
    CameraCodePatches[52] = { CameraTopDownHelperBase + 0x08, 0x00000000, 0x3C06800F, false };
    CameraCodePatches[53] = { CameraTopDownHelperBase + 0x0C, 0x00000000, 0x25290001, false };
    CameraCodePatches[54] = { CameraTopDownHelperBase + 0x10, 0x00000000, JumpTo(CameraTopDownEntry + 0x08), false };
    CameraCodePatches[55] = { CameraTopDownHelperBase + 0x14, 0x00000000, WithLo(0xAD090000, CameraTopDownCounterAddress), false };
    CameraCodePatches[56] = { CameraHelperBase + 0x00, 0x00000000, 0x92080568, false };
    CameraCodePatches[57] = { CameraHelperBase + 0x04, 0x00000000, 0x3108FFFC, false };
    CameraCodePatches[58] = { CameraHelperBase + 0x08, 0x00000000, 0x00000000, false };
    CameraCodePatches[59] = { CameraHelperBase + 0x0C, 0x00000000, 0x15000003, false };
    CameraCodePatches[60] = { CameraHelperBase + 0x10, 0x00000000, 0x00A01025, false };
    CameraCodePatches[61] = { CameraHelperBase + 0x14, 0x00000000, 0x03E00008, false };
    CameraCodePatches[62] = { CameraHelperBase + 0x1C, 0x00000000, JumpTo(CameraAngleHelper), false };
    CameraCodePatches[63] = { CameraHelperBase + 0x20, 0x00000000, 0x92080568, false };
    CameraCodePatches[64] = { CameraHelperBase + 0x24, 0x00000000, 0x3108FFFC, false };
    CameraCodePatches[65] = { CameraHelperBase + 0x28, 0x00000000, 0x00000000, false };
    CameraCodePatches[66] = { CameraHelperBase + 0x2C, 0x00000000, 0x15000003, false };
    CameraCodePatches[67] = { CameraHelperBase + 0x30, 0x00000000, 0x8FA200F0, false };
    CameraCodePatches[68] = { CameraHelperBase + 0x34, 0x00000000, 0xAFA00098, false };
    CameraCodePatches[69] = { CameraHelperBase + 0x38, 0x00000000, 0xAFA000A0, false };
    CameraCodePatches[70] = { CameraHelperBase + 0x3C, 0x00000000, 0x03E00008, false };

    // Every jump word that encodes one of our own stub addresses, or a game
    // routine a stub returns to. These were the last thing still spelled in
    // US terms: an instruction word starts 0x08 or 0x0C, so the migration
    // that looked for 0x8xxxxxxx addresses walked straight past them.
    ObjectMoveJump = JumpTo(ObjectMoveStub);
    SidekickStrafeJump = JumpTo(SidekickStrafeStub);
    SidekickPadProbeJump = JumpTo(SidekickPadProbeStub);
    LandingCinematicSkipJump = CallTo(LandingCinematicSkipStub);
    LegacyLandingCinematicSkipJump = JumpTo(SidekickVerticalStub); // the cave the old skip used
    LegacyLandingCinematicSkipCall = CallTo(SidekickVerticalStub);
    LegacyIntroCinematicSkipCall = CallTo(IntroCinematicSkipStub);
    SidekickStrafeResumeJump = JumpTo(SidekickStrafeDelay + 0x04);

    // The patch tables embed addresses, so they are rebuilt from the same
    // initialisers rather than duplicated here by hand.
    FramePacingPatches[0] = { FramePacingEscalateStore, A.FramePacingEscalateStoreWord, 0x00000000 };
    FramePacing60Patches[0] = { FramePacing60Branch, 0x11C00002, 0x00000000 };
    SchedulerReleasePatches[0] = { SchedulerFrameGateAdd, A.SchedulerFrameGateAddWord, (A.SchedulerFrameGateAddWord + 1) };
    WaterWakeRingRatePatches[0] = { WaterWakeRingRateEntry + 0x00, 0x00000000, 0x928D0002 };
    WaterWakeRingRatePatches[1] = { WaterWakeRingRateEntry + 0x08, 0x00000000, 0x01A36825 };
    WaterWakeRingRatePatches[2] = { WaterWakeRingRateEntry + 0x1C, 0x1020002E, 0x002D0824 };
    WaterWakeRingRatePatches[3] = { WaterWakeRingRateEntry + 0x20, 0x00000000, 0x1020002D };
    ObjectMovePatches[0] = { ObjectMoveStub + 0x00, 0x00000000, 0x8FA70040 };
    ObjectMovePatches[1] = { ObjectMoveStub + 0x04, 0x00000000, WithHi(0x3C010000, DroneLateralFlagsAddress) };
    ObjectMovePatches[2] = { ObjectMoveStub + 0x08, 0x00000000, WithLo(0x8C290000, DroneLateralFlagsAddress) };
    ObjectMovePatches[3] = { ObjectMoveStub + 0x0C, 0x00000000, 0x312A0001 };
    ObjectMovePatches[4] = { ObjectMoveStub + 0x10, 0x00000000, 0x11400016 };
    ObjectMovePatches[5] = { ObjectMoveStub + 0x14, 0x00000000, 0x00000000 };
    ObjectMovePatches[6] = { ObjectMoveStub + 0x18, 0x00000000, WithLo(0x8C280000, SidekickPadProbeActorAddress) };
    ObjectMovePatches[7] = { ObjectMoveStub + 0x1C, 0x00000000, 0x14E80013 };
    ObjectMovePatches[8] = { ObjectMoveStub + 0x20, 0x00000000, 0x00000000 };
    ObjectMovePatches[9] = { ObjectMoveStub + 0x24, 0x00000000, WithLo(0x8C2B0000, DroneLateralHookHitsAddress) };
    ObjectMovePatches[10] = { ObjectMoveStub + 0x28, 0x00000000, 0x256B0001 };
    ObjectMovePatches[11] = { ObjectMoveStub + 0x2C, 0x00000000, WithLo(0xAC2B0000, DroneLateralHookHitsAddress) };
    ObjectMovePatches[12] = { ObjectMoveStub + 0x30, 0x00000000, 0xC7A00044 };
    ObjectMovePatches[13] = { ObjectMoveStub + 0x34, 0x00000000, 0xC7A2004C };
    ObjectMovePatches[14] = { ObjectMoveStub + 0x38, 0x00000000, 0x312A0002 };
    ObjectMovePatches[15] = { ObjectMoveStub + 0x3C, 0x00000000, 0x11400006 };
    ObjectMovePatches[16] = { ObjectMoveStub + 0x40, 0x00000000, 0x00000000 };
    ObjectMovePatches[17] = { ObjectMoveStub + 0x44, 0x00000000, 0x46001107 };
    ObjectMovePatches[18] = { ObjectMoveStub + 0x48, 0x00000000, 0xE7A40044 };
    ObjectMovePatches[19] = { ObjectMoveStub + 0x4C, 0x00000000, 0xE7A0004C };
    ObjectMovePatches[20] = { ObjectMoveStub + 0x50, 0x00000000, JumpTo(ObjectMoveResume) };
    ObjectMovePatches[21] = { ObjectMoveStub + 0x54, 0x00000000, 0x00000000 };
    ObjectMovePatches[22] = { ObjectMoveStub + 0x58, 0x00000000, 0xE7A20044 };
    ObjectMovePatches[23] = { ObjectMoveStub + 0x5C, 0x00000000, 0x46000107 };
    ObjectMovePatches[24] = { ObjectMoveStub + 0x60, 0x00000000, 0xE7A4004C };
    ObjectMovePatches[25] = { ObjectMoveStub + 0x64, 0x00000000, JumpTo(ObjectMoveResume) };
    ObjectMovePatches[26] = { ObjectMoveStub + 0x68, 0x00000000, 0x00000000 };
    ObjectMovePatches[27] = { ObjectMoveStub + 0x6C, 0x00000000, WithLo(0x8C280000, EnemyHalveFlagAddress) };
    ObjectMovePatches[28] = { ObjectMoveStub + 0x70, 0x00000000, 0x11000015 };
    ObjectMovePatches[29] = { ObjectMoveStub + 0x74, 0x00000000, 0x00000000 };
    // Spare every player-type object (behaviorId 1), not just playerList[0].
    // Race opponents are AI "players" (behaviorId 1) that the game already paces
    // for 60fps; halving them left them crawling. Enemies (behaviorId 24, etc.)
    // are still halved.
    // Also spare laserbolt (behaviorId 162): laserboltControl already weights
    // both its movement AND its lifetime by the game delta (2 at 30fps, 1 at
    // 60fps), so a bolt travels vel*lifetime regardless of frame rate. Halving
    // its already-correct delta left the movement at half speed while lifetime
    // still expired at the full rate, so the shot died at half range - the
    // sniper could no longer reach distant targets at 60fps. Both player shots
    // and enemy bolts share this behaviour, so excluding the id fixes both.
    ObjectMovePatches[30] = { ObjectMoveStub + 0x78, 0x00000000, 0x84E80048 }; // lh    $t0, 0x48($a3)  ; behaviorId
    ObjectMovePatches[31] = { ObjectMoveStub + 0x7C, 0x00000000, 0x24010001 }; // addiu $at, $zero, 1
    ObjectMovePatches[32] = { ObjectMoveStub + 0x80, 0x00000000, 0x11010011 }; // beq   $t0, $at, resume ; player-type (bid 1): don't halve
    ObjectMovePatches[33] = { ObjectMoveStub + 0x84, 0x00000000, 0x240100A2 }; // addiu $at, $zero, 0xA2 ; (delay slot) at = 162 laserbolt
    ObjectMovePatches[34] = { ObjectMoveStub + 0x88, 0x00000000, 0x1101000F }; // beq   $t0, $at, resume ; laserbolt (bid 162): delta-weighted, don't halve
    ObjectMovePatches[35] = { ObjectMoveStub + 0x8C, 0x00000000, 0x00000000 }; // nop (delay slot)
    ObjectMovePatches[36] = { ObjectMoveStub + 0x90, 0x00000000, 0x3C013F00 };
    ObjectMovePatches[37] = { ObjectMoveStub + 0x94, 0x00000000, 0x44818000 };
    ObjectMovePatches[38] = { ObjectMoveStub + 0x98, 0x00000000, 0xC7B20044 };
    ObjectMovePatches[39] = { ObjectMoveStub + 0x9C, 0x00000000, 0x00000000 };
    ObjectMovePatches[40] = { ObjectMoveStub + 0xA0, 0x00000000, 0x46109482 };
    ObjectMovePatches[41] = { ObjectMoveStub + 0xA4, 0x00000000, 0xE7B20044 };
    ObjectMovePatches[42] = { ObjectMoveStub + 0xA8, 0x00000000, 0xC7B20048 };
    ObjectMovePatches[43] = { ObjectMoveStub + 0xAC, 0x00000000, 0x00000000 };
    ObjectMovePatches[44] = { ObjectMoveStub + 0xB0, 0x00000000, 0x46109482 };
    ObjectMovePatches[45] = { ObjectMoveStub + 0xB4, 0x00000000, 0xE7B20048 };
    ObjectMovePatches[46] = { ObjectMoveStub + 0xB8, 0x00000000, 0xC7B2004C };
    ObjectMovePatches[47] = { ObjectMoveStub + 0xBC, 0x00000000, 0x00000000 };
    ObjectMovePatches[48] = { ObjectMoveStub + 0xC0, 0x00000000, 0x46109482 };
    ObjectMovePatches[49] = { ObjectMoveStub + 0xC4, 0x00000000, 0xE7B2004C };
    ObjectMovePatches[50] = { ObjectMoveStub + 0xC8, 0x00000000, JumpTo(ObjectMoveResume) };
    ObjectMovePatches[51] = { ObjectMoveStub + 0xCC, 0x00000000, 0x00000000 };
    ObjectMovePatches[52] = { ObjectMoveEntry, 0x8FA70040, ObjectMoveJump };
}

// The table actually in force. Comparing pointers rather than identifiers keeps
// the switch to one assignment per ROM change instead of one per frame.
const JFG_ADDRESSES * AppliedAddresses = nullptr;

bool SelectAddressTable(void)
{
    const JFG_ADDRESSES * Addresses = JfgAddresses();
    if (Addresses == nullptr)
    {
        return false;
    }
    if (Addresses != AppliedAddresses)
    {
        ApplyAddressTable(*Addresses);
        AppliedAddresses = Addresses;
    }
    return true;
}

} // namespace

CJetForceGeminiRuntime::CJetForceGeminiRuntime(CMipsMemoryVM & MMU, CRecompiler *& Recompiler) :
    m_Memory(MMU),
    m_CodePatcher(m_Memory, Recompiler),
    m_Enabled(false),
    m_CameraPatchApplied(false),
    m_AimFovReference(0.0f),
    m_AimYawCarry(0.0f),
    m_AimPitchCarry(0.0f),
    m_BossAimReticleX(0),
    m_BossAimYawApplied(0),
    m_TopDownCounterInitialized(false),
    m_TopDownCounter(0),
    m_TopDownHoldPolls(0),
    m_QueuedMouseWheel(0),
    m_FramePacingPatchApplied(false),
    m_FramePacing60PatchApplied(false),
    m_SchedulerReleasePatchApplied(false),
    m_WaterWakeRingRatePatchApplied(false),
    m_GameplayReady(false),
    m_SprintActive(false),
    m_SprintTimeValid(false),
    m_SprintBlend(0.0f),
    m_SprintApplied(false),
    m_SprintPositionValid(false),
    m_SprintPlayerObject(0),
    m_SprintPreviousX(0.0f),
    m_SprintPreviousZ(0.0f),
    m_SprintAnimationApplied(false),
    m_SprintAnimationValid(false),
    m_SprintAnimation(0),
    m_SprintPreviousAnimationFrame(0.0f),
    m_DroneLateralActive(false),
    m_DroneLateralApplied(false),
    m_DroneLateralState(0),
    m_DroneLateralHookHits(0),
    m_SidekickStrafeHookApplied(false),
    m_SidekickPadControlProbeApplied(false),
    m_SidekickPadControlProbeEntry(0),
    m_BaseViRefreshRate(0),
    m_ObjectMovePatchApplied(false),
    m_LandingCinematicSkipHookApplied(false),
    m_IntroCinematicSkipHookApplied(false),
    m_IntroCinematicSkipOverlayBase(0),
    m_WidescreenHudCaveApplied(false),
    m_WidescreenHudFixedHooksApplied(false),
    m_WidescreenHudOverlayHookApplied(false),
    m_WidescreenHudScopeOwned(false),
    m_WidescreenHudOverlayBase(0),
    m_WidescreenReticleOverlayBase(0),
    m_WidescreenHudScopeOriginal(0),
    m_HudAlignmentOverlay6Base(0),
    m_HudAlignmentOverlay14Base(0),
    m_HudAlignmentScopeOwned(false),
    m_Fps60ToggleDown(false),
    m_Fps30ToggleDown(false),
    m_SyncAudioEnabledState(-1),
    m_HalveFrameCounter(0),
    m_InputRateWindowValid(false),
    m_InputRateSamples(0),
    m_FrameSwaps(0),
    m_LastCurrentScreen(0)
{
    memset(m_HalvedEnemySlots, 0, sizeof(m_HalvedEnemySlots));
    memset(m_Orbit, 0, sizeof(m_Orbit));
    memset(&m_ScrollButtons, 0, sizeof(m_ScrollButtons));
    memset(m_SecondaryScrollButtons, 0, sizeof(m_SecondaryScrollButtons));
    memset(m_SecondaryQueuedScroll, 0, sizeof(m_SecondaryQueuedScroll));
}

CJetForceGeminiRuntime::~CJetForceGeminiRuntime()
{
    // The controller plugin outlives CN64System during application cleanup.
    // At this point m_Memory refers to an already-destroyed MMU, so restoring
    // game patches here would dereference invalid host memory. Runtime resets
    // and disabled settings still call Deactivate() while the MMU is alive.
}

void CJetForceGeminiRuntime::Reset(void)
{
    PatchHudRaster(false);
    PatchHudAlignment(false, false);
    PatchWidescreenHud(false);
    Deactivate();
}

// A save state swaps the game's memory while it keeps running, so the patches it
// restores are whatever was live when the state was written. Unpatching here, as
// a reset would, rewrites code the game is in the middle of and then puts it
// back a frame later; leaving 60fps in that gap was enough to hang the render
// loop. Drop our own bookkeeping instead and let the next video frame line the
// patches back up with what is actually in memory. The VI budget is an emulator
// setting rather than game memory, so it is the one thing to hand back.
// A state written while a movement hook was installed cannot be loaded back:
// the game hangs before it draws, and it hangs even if the hooks are taken down
// on the way in, so the trigger is a hook being in the saved memory rather than
// anything it does. A state written without them loads fine and the hooks then
// install cleanly on top, so take them out before the state is written and
// let the next video frame put it back.
void CJetForceGeminiRuntime::StateSaving(void)
{
    // The guest words below exist only in a supported ROM, see Deactivate; the
    // host-side bookkeeping is dropped for every game.
    if (!IsSupportedRom())
    {
        ClearMovementState();
        return;
    }
    PatchHudRaster(false);
    PatchHudAlignment(false, false);
    PatchWidescreenHud(false);
    PatchLandingCinematicSkip(false);
    PatchIntroCinematicSkip(false);
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, 0);
    PatchWaterWakeRingRate(false);
    PatchObjectMove(false);
    PatchSidekickStrafe(false);
    PatchSidekickPadControlProbe(false);
    m_Memory.WriteU32(DroneLateralFlagsAddress, 0);
    m_Memory.WriteF32(DroneLateralSideFactorAddress, 0.0f);
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalThrustAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalVelocityAddress, 0.0f);
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);

    ClearMovementState();
}

void CJetForceGeminiRuntime::StateLoaded(void)
{
    // The guest words below exist only in a supported ROM, see Deactivate; the
    // host-side bookkeeping is dropped for every game.
    if (!IsSupportedRom())
    {
        ApplyViBudget(false);
        ClearCameraState();
        ClearMovementState();
        return;
    }
    PatchHudRaster(false);
    PatchHudAlignment(false, false);
    PatchWidescreenHud(false);
    // States made by the first HUD prototype may contain an interrupted scope.
    // This byte is reserved alignment padding, so normalise it even when the
    // host-side ownership bookkeeping was reset before loading the state.
    if (JfgHudBuild::Current() != JfgHudBuild::BuildNone)
    {
        m_Memory.WriteU8(WidescreenHudScopeDepthAddress, 0);
    }
    PatchLandingCinematicSkip(false);
    PatchIntroCinematicSkip(false);
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, 0);
    RemoveLegacyLandingCinematicSkip();
    RemoveLegacyIntroCinematicSkip();
    ApplyViBudget(false);
    PatchWaterWakeRingRate(false);
    ClearCameraState();
    ClearMovementState();

    PatchObjectMove(false);
    PatchSidekickStrafe(false);
    PatchSidekickPadControlProbe(false);
    m_Memory.WriteU32(DroneLateralFlagsAddress, 0);
    m_Memory.WriteF32(DroneLateralSideFactorAddress, 0.0f);
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalThrustAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalVelocityAddress, 0.0f);
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
}

bool CJetForceGeminiRuntime::IsEnabled(void) const
{
    return m_Enabled;
}

// A port fed by any JFG source is owned completely by the scheme. Query the
// routed sources rather than m_Enabled so the input plugin is suppressed from
// the first poll, before a level has made the runtime active.
bool CJetForceGeminiRuntime::UsesExclusiveInput(const JFG_PORT_INPUT & Input) const
{
    return Input.HasSource() && IsSupportedRom();
}

// Mouse capture follows the keyboard/mouse option alone: a gamepad-only setup
// leaves the pointer free.
bool CJetForceGeminiRuntime::UsesKeyboardMouse(void) const
{
    return g_Settings->LoadBool(Setting_JfgKeyboardMouse) && IsSupportedRom();
}

bool CJetForceGeminiRuntime::SupportsCurrentRom(void) const
{
    return IsSupportedRom();
}

// Enhancements which do not consume controller input run on their own VI
// path. In particular, do not put this behind UpdateEnabledState(): that helper
// requires a JFG source on port one and would uninstall an otherwise
// independent HUD option every frame. The HUD runs on the US build and, through
// JetForceGeminiHudBuild.h, on PAL; the Kiosk demo's HUD has not been mapped.
void CJetForceGeminiRuntime::ProcessRuntimeFrame(void)
{
    const bool Supported = IsSupportedRom() && JfgHudBuild::Current() != JfgHudBuild::BuildNone;
    const bool WidescreenRequested = Supported && g_Settings->LoadBool(Setting_JfgWidescreenHud);
    const bool AlignmentRequested = Supported && g_Settings->LoadBool(Setting_JfgAlignHud);
    if (!WidescreenRequested && !AlignmentRequested)
    {
        PatchHudRaster(false);
        PatchHudAlignment(false, false);
        PatchWidescreenHud(false);
        return;
    }

    // Never install the gameplay hooks during the boot/front-end sequence. Their
    // trampoline storage is in the retired tail of JFG's CPU diagnostic code,
    // which is still executing during a cold boot. A loaded gameplay state
    // skips that initializer, which is why the old eager installation appeared
    // to work only after loading a state.
    //
    // The shared font has a separate post-initialization guard below. For the
    // gameplay HUD, require the live overlay and its four-word signature.
    // Placement works in all four video modes; aspect correction additionally
    // requires the game's own widescreen bit below.
    uint8_t ResolutionIndex = 0;
    uint32_t PlayerObject = 0;
    uint32_t PlayerData = 0;
    uint32_t ControlCamera = 0;
    uint32_t JoyDisabled = 1;
    uint32_t OverlayTable = 0;
    uint32_t OverlayBase = 0;
    uint32_t EnterWord = 0;
    uint32_t EnterDelay = 0;
    uint32_t ExitWord = 0;
    uint32_t ExitDelay = 0;
    const uint32_t RequiredTableSize =
        (WidescreenHudOverlayModule + 1) * OverlayHeaderSize;
    const bool GameplayHudReady =
        m_Memory.ReadU8(WidescreenHudResolutionIndexAddress, ResolutionIndex) &&
        JfgHudBuild::VideoMode(ResolutionIndex) != 0xFF &&
        GetPlayerData(PlayerObject, PlayerData) && PlayerObject != 0 &&
        GetControlCamera(ControlCamera) && ControlCamera != 0 &&
        m_Memory.ReadU32(DisableJoyAddress, JoyDisabled) && JoyDisabled == 0 &&
        m_Memory.ReadU32(OverlayTableAddress, OverlayTable) &&
        (OverlayTable & 3) == 0 &&
        m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize) &&
        m_Memory.ReadU32(
            OverlayTable + WidescreenHudOverlayModule * OverlayHeaderSize,
            OverlayBase) &&
        OverlayBase != 0 && (OverlayBase & 3) == 0 &&
        m_Memory.IsRdramAddress(
            OverlayBase + WidescreenHudOverlayEnterOffset,
            2 * sizeof(uint32_t)) &&
        m_Memory.IsRdramAddress(
            OverlayBase + WidescreenHudOverlayExitOffset,
            2 * sizeof(uint32_t)) &&
        m_Memory.ReadU32(
            OverlayBase + WidescreenHudOverlayEnterOffset, EnterWord) &&
        m_Memory.ReadU32(
            OverlayBase + WidescreenHudOverlayEnterOffset + 0x04, EnterDelay) &&
        m_Memory.ReadU32(
            OverlayBase + WidescreenHudOverlayExitOffset, ExitWord) &&
        m_Memory.ReadU32(
            OverlayBase + WidescreenHudOverlayExitOffset + 0x04, ExitDelay) &&
        (EnterWord == WidescreenHudOverlayEnterOriginal ||
         EnterWord == CallTo(WidescreenHudScopeEnterStub)) &&
        EnterDelay == WidescreenHudOverlayEnterDelayOriginal &&
        (ExitWord == WidescreenHudOverlayExitOriginal ||
         ExitWord == JumpTo(WidescreenHudScopeExitStub)) &&
        ExitDelay == WidescreenHudOverlayExitDelayOriginal;

    uint8_t multiplayerPlayers = 0;
    if (Supported &&
        (!WidescreenRequested || !m_Memory.ReadU8(JfgHudBuild::Address(0x800A4FD0), multiplayerPlayers) ||
        multiplayerPlayers < 2 || multiplayerPlayers > 4 ||
        !IsWidescreenHudResolution(ResolutionIndex)))
        JfgMultiplayerHud::Update(m_Memory, m_CodePatcher, false);

    const bool EnableWidescreen = GameplayHudReady && WidescreenRequested &&
                                  IsWidescreenHudResolution(ResolutionIndex);
    const bool HudPatched = PatchWidescreenHud(EnableWidescreen);
    PatchHudAlignment(GameplayHudReady && AlignmentRequested && HudPatched,
                      EnableWidescreen && HudPatched);
    const bool NativeHud = EnableWidescreen && HudPatched;
    const bool NativeText = WidescreenRequested && JfgHudRaster::TextReady(m_Memory);
    PatchHudRaster(NativeHud || NativeText, !NativeHud && NativeText);

}

// Called when the game reads the controller, so the buttons it gets are live
// rather than a snapshot: joyRead detects a jump on the rising edge between two
// completed reads, and quantising the state to the video interrupt made two
// consecutive reads land on the same value often enough to swallow the edge.
// The mouse delta is banked here because reading it also consumes it.
void CJetForceGeminiRuntime::ProcessController(
    int32_t Control, const JFG_PORT_INPUT & Input, BUTTONS & Buttons)
{
    // The camera, sprint and drone work below is bound to the first player's
    // objects, so only port one gets the full scheme.
    if (Control != 0)
    {
        MapSecondaryPort(Control, Input, Buttons);
        return;
    }
    if (!UpdateEnabledState(Input))
    {
        return;
    }
    JFG_CONTROLS Controls;
    ReadControls(Input, Controls);

    // Arm the guest hook in the same controller-read path that maps the skip
    // keys, and publish them for the MIPS stub before the game reaches its
    // next joyGetButtons call.
    const bool CinematicSkipRequested =
        g_Settings->LoadBool(Setting_JfgFastCutscenes) && Controls.SkipCinematic;
    // The landing stub is spliced into the shared button-combine point and
    // its table walk clobbers $t8 - the register the displaced
    // `andi $t8, $s1, 0x1000` uses for START. Left armed during ordinary
    // play, a held E (the A / fire key) or Return returns from the no-match
    // path with $t8 non-zero, which the game reads as a phantom START. Only
    // arm it once the current scene is one the table can actually skip, so
    // the controller path is untouched everywhere else.
    const bool LandingSkipArmed =
        CinematicSkipRequested && CurrentSceneIsCinematicSkippable();
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, LandingSkipArmed ? 1 : 0);
    PatchLandingCinematicSkip(LandingSkipArmed);
    PatchIntroCinematicSkip(CinematicSkipRequested);

    // The live-switch keys are keyboard only
    if (Input.KeyboardMouse != nullptr)
    {
        const KEYBOARD_MOUSE_STATE & Keyboard = *Input.KeyboardMouse;
        // Numpad +/- switch the frame-rate target live. Flip the setting only on
        // the key's rising edge, and only when it actually changes, so a held
        // key does not spam saves; the notification still confirms every press.
        const bool Fps60ToggleDown = KeyDown(Keyboard, Fps60ToggleKey);
        if (Fps60ToggleDown && !m_Fps60ToggleDown)
        {
            if (!g_Settings->LoadBool(Setting_JfgTarget60Fps))
            {
                g_Settings->SaveBool(Setting_JfgTarget60Fps, true);
            }
            // A PAL console refreshes at 50 Hz, so the same two targets are 50 and 25.
            g_Notify->DisplayMessage(0, JfgAddresses() == &JfgPalAddresses ? "JFG 50 FPS (PAL)" : "JFG 60 FPS");
        }
        m_Fps60ToggleDown = Fps60ToggleDown;

        const bool Fps30ToggleDown = KeyDown(Keyboard, Fps30ToggleKey);
        if (Fps30ToggleDown && !m_Fps30ToggleDown)
        {
            if (g_Settings->LoadBool(Setting_JfgTarget60Fps))
            {
                g_Settings->SaveBool(Setting_JfgTarget60Fps, false);
            }
            g_Notify->DisplayMessage(0, JfgAddresses() == &JfgPalAddresses ? "JFG 25 FPS (PAL)" : "JFG 30 FPS");
        }
        m_Fps30ToggleDown = Fps30ToggleDown;

        BankMouseDelta(Keyboard);
        QueueMouseWheel(Keyboard);
    }
    QueueGamepadScroll(Input);

    Controls.Scroll = m_QueuedMouseWheel;
    m_QueuedMouseWheel = 0;
    MapController(Controls, Buttons, false);
}

// Called once per video interrupt, so the mouse camera runs at the render rate
// even while the game polls the controller at its own logic rate.
void CJetForceGeminiRuntime::ProcessVideoFrame(const JFG_PORT_INPUT & Input, BUTTONS & Buttons)
{
    if (!UpdateEnabledState(Input))
    {
        return;
    }

    if (Input.KeyboardMouse != nullptr)
    {
        BankMouseDelta(*Input.KeyboardMouse);
        QueueMouseWheel(*Input.KeyboardMouse);
    }
    QueueGamepadScroll(Input);
    UpdateInputRate();

    const bool Target60Fps = g_Settings->LoadBool(Setting_JfgTarget60Fps);
    bool SchedulerRelease = Target60Fps && g_Settings->LoadBool(Setting_JfgSchedulerRelease);

    // Sync-to-audio paces the CPU against the audio buffer, so any audio hitch
    // becomes frame stutter. In 60 fps mode the VI limiter already paces the
    // frame at the native rate, so keep sync-to-audio off there and let DRC and
    // the underrun declick smooth the sound instead; 30 fps mode, where it was
    // added to hide CPU-overclock stutter, keeps it. Pushed live so the +/- FPS
    // toggle applies immediately - the setting's change callback refreshes the
    // core's syncToAudio.
    const int8_t DesiredSyncAudio =
        (IsSupportedRom() && g_Settings->LoadBool(Setting_JfgSyncAudio) && !Target60Fps) ? 1 : 0;
    if (DesiredSyncAudio != m_SyncAudioEnabledState)
    {
        g_Settings->SaveBool(Setting_SyncViaAudioEnabled, DesiredSyncAudio != 0);
        m_SyncAudioEnabledState = DesiredSyncAudio;
    }
    // The scheduler release itself is needed for a real 60 Hz frame cadence,
    // but requesting a third framebuffer before the title-to-level transition
    // completes prevents a cold boot with Parallel RSP. Keep the stock buffer
    // allocation and enable the scheduler only after gameplay is ready.

    // Nothing may touch the game before a level is live. A player object can
    // already exist while the title-to-level transition is still allocating
    // video state, so require the active control camera and enabled controls as
    // well. The pacing patches rewrite the scheduler and video sync; applying
    // them during that transition prevents a cold boot from reaching gameplay.
    uint32_t PlayerObject = 0;
    uint32_t PlayerData = 0;
    uint32_t ControlCamera = 0;
    uint32_t JoyDisabled = 1;
    m_GameplayReady = GetPlayerData(PlayerObject, PlayerData) && PlayerObject != 0 &&
                      GetControlCamera(ControlCamera) &&
                      m_Memory.ReadU32(DisableJoyAddress, JoyDisabled) && JoyDisabled == 0;
    if (!m_GameplayReady)
    {
        return;
    }

    // The stick is banked only once a level is live, so holding it through a
    // menu or cinematic cannot pile up a turn to spend on the first frame back.
    BankStickCamera(m_Orbit[0], Input);

    // A wheel notch is a one-poll A/B impulse. The video path can run before
    // that poll, so it queues the event but never consumes it itself.
    JFG_CONTROLS Controls;
    ReadControls(Input, Controls);
    Controls.Scroll = 0;
    MapController(Controls, Buttons, true);
    UpdateSprintBlend();
    ApplySprint(PlayerObject);
    bool HalveEnemySpeed = Target60Fps && g_Settings->LoadBool(Setting_JfgHalveEnemySpeed);
    bool BoostViBudget = Target60Fps ? g_Settings->LoadBool(Setting_JfgBoostViBudget) :
                                      g_Settings->LoadBool(Setting_JfgBoostViBudget30);
    ApplyViBudget(BoostViBudget);
    m_Memory.WriteU32(EnemyHalveFlagAddress, HalveEnemySpeed ? 1 : 0);
    PatchObjectMove(HalveEnemySpeed);
    if (HalveEnemySpeed)
    {
        HalveNamedEnemyMovement();
    }
    PatchSidekickStrafe(g_Settings->LoadBool(Setting_JfgDroneLateralMovement));
    PatchSidekickPadControlProbe(g_Settings->LoadBool(Setting_JfgDroneLateralMovement));
    m_DroneLateralState = 0;
    m_DroneLateralHookHits = 0;
    m_DroneLateralApplied = false;
    if (m_DroneLateralActive)
    {
        m_Memory.ReadU32(SidekickPadProbeStateAddress, m_DroneLateralState);
        m_Memory.ReadU32(DroneLateralHookHitsAddress, m_DroneLateralHookHits);
        m_DroneLateralApplied = m_DroneLateralHookHits != 0;
    }
    PatchFramePacing(Target60Fps || g_Settings->LoadBool(Setting_JfgUncapFramePacing));
    PatchFramePacing60(Target60Fps);
    PatchSchedulerRelease(SchedulerRelease);
    PatchWaterWakeRingRate(Target60Fps);
}

bool CJetForceGeminiRuntime::UpdateEnabledState(const JFG_PORT_INPUT & Input)
{
    if (!Input.HasSource() || !IsSupportedRom())
    {
        Deactivate();
        return false;
    }
    m_Enabled = true;
    return true;
}

// GetKeyboardMouseState reports the movement since the cursor was last recentred
// and the recentring happens on every read, so whichever path reads first would
// otherwise steal it. Banking the total keeps both paths harmless.
void CJetForceGeminiRuntime::BankMouseDelta(const KEYBOARD_MOUSE_STATE & Input)
{
    m_Orbit[0].MouseDeltaX += Input.MouseX;
    m_Orbit[0].MouseDeltaY += Input.MouseY;
}

// The video interrupt and the game controller poll acquire input separately.
// Mouse movement can be banked for either path, but a wheel notch is a one-poll
// A/B impulse: preserve it until a real controller poll can return it.
void CJetForceGeminiRuntime::QueueMouseWheel(const KEYBOARD_MOUSE_STATE & Input)
{
    if (Input.MouseWheel != 0)
    {
        m_QueuedMouseWheel = Input.MouseWheel > 0 ? 1 : -1;
    }
}

// X and Y are wheel notches on a gamepad: the rising edge of either queues the
// same one-poll impulse the wheel does. Both sampling paths call this with the
// shared edge state, so whichever sees the press first is the one to queue it.
void CJetForceGeminiRuntime::QueueGamepadScroll(const JFG_PORT_INPUT & Input)
{
    const int32_t Scroll = ReadScrollButtons(Input, m_ScrollButtons);
    if (Scroll != 0)
    {
        m_QueuedMouseWheel = Scroll;
    }
}

// The secondary ports' counterpart of the two above: their X/Y notches and
// wheel are one-poll impulses too, so they are sampled from the video
// interrupt as well as from the game's own poll, with the per-port edge state
// shared between the two paths, and held until MapSecondaryPort returns them.
// Sampling only at the poll left a tap shorter than one game frame unseen.
void CJetForceGeminiRuntime::QueueSecondaryScroll(int32_t Control, const JFG_PORT_INPUT & Input)
{
    if (Control < 1 || Control > 3)
    {
        return;
    }
    int32_t Scroll = ReadScrollButtons(Input, m_SecondaryScrollButtons[Control - 1]);
    if (Scroll == 0 && Input.KeyboardMouse != nullptr && Input.KeyboardMouse->MouseWheel != 0)
    {
        Scroll = Input.KeyboardMouse->MouseWheel > 0 ? 1 : -1;
    }
    if (Scroll != 0)
    {
        m_SecondaryQueuedScroll[Control - 1] = Scroll;
    }
}

namespace
{
bool GamepadButtonDown(const GAMEPAD_STATE & Pad, GamepadButton Button)
{
    return (Pad.Buttons & (1u << Button)) != 0;
}

bool GamepadTriggerDown(int16_t Trigger)
{
    return Trigger >= GamepadTriggerThreshold;
}

// Radial dead zone, rescaled so the first live position sits just off centre
// rather than jumping to the dead zone's edge. Output is -1..1 on each axis
// with SDL's sign convention, up and left negative.
void NormaliseStick(int16_t RawX, int16_t RawY, float & X, float & Y)
{
    X = (float)RawX / (float)GamepadAxisMax;
    Y = (float)RawY / (float)GamepadAxisMax;
    const float Magnitude = sqrtf(X * X + Y * Y);
    if (Magnitude <= GamepadStickDeadZone)
    {
        X = 0.0f;
        Y = 0.0f;
        return;
    }
    float Scale = (Magnitude - GamepadStickDeadZone) / (1.0f - GamepadStickDeadZone);
    Scale = Scale > 1.0f ? 1.0f : Scale;
    X *= Scale / Magnitude;
    Y *= Scale / Magnitude;
}

int8_t StickToN64(float Deflection)
{
    const float Value = Deflection * (float)JfgStickLimit;
    return (int8_t)(Value >= 0.0f ? (int32_t)(Value + 0.5f) : -(int32_t)(-Value + 0.5f));
}
} // namespace

// The right stick stands in for the mouse: its deflection becomes a per video
// frame delta in mouse counts and joins the bank the camera paths already
// spend, so on foot, aiming, the boss camera and the drone all follow it
// without further wiring. The response is squared for fine control near the
// centre, and the truncated fraction carries to the next frame so slow pans
// still move. The pads sharing the port contribute whichever is pushed further.
// Orbit is the state of the player the port feeds.
void CJetForceGeminiRuntime::BankStickCamera(ORBIT_CAMERA_STATE & Orbit, const JFG_PORT_INPUT & Input)
{
    float X = 0.0f;
    float Y = 0.0f;
    bool PadAiming = false;
    for (size_t i = 0; i < sizeof(Input.Gamepads) / sizeof(Input.Gamepads[0]); i++)
    {
        if (Input.Gamepads[i] == nullptr)
        {
            continue;
        }
        PadAiming = PadAiming || GamepadTriggerDown(Input.Gamepads[i]->LeftTrigger);
        float PadX;
        float PadY;
        NormaliseStick(Input.Gamepads[i]->RightX, Input.Gamepads[i]->RightY, PadX, PadY);
        if (fabsf(PadX) > fabsf(X))
        {
            X = PadX;
        }
        if (fabsf(PadY) > fabsf(Y))
        {
            Y = PadY;
        }
    }
    if (X == 0.0f && Y == 0.0f)
    {
        Orbit.StickCarryX = 0.0f;
        Orbit.StickCarryY = 0.0f;
        return;
    }

    uint32_t Speed = g_Settings->LoadDword(Setting_JfgGamepadCameraSpeed);
    Speed = Speed < GamepadCameraSpeedMin ? GamepadCameraSpeedMin : Speed;
    Speed = Speed > GamepadCameraSpeedMax ? GamepadCameraSpeedMax : Speed;
    float Rate = (float)Speed * GamepadCameraCountsPerSpeed;
    // Banked once per video interrupt: keep the same turn per second at 50 Hz.
    if (JfgAddresses() == &JfgPalAddresses)
    {
        Rate *= PalVideoRateScale;
    }
    // With the game's own reticle the stick bypasses the bank while aiming, so
    // the boost only concerns the mouse-style aim; see MapController.
    if (PadAiming && !g_Settings->LoadBool(Setting_JfgGamepadStockAim))
    {
        Rate *= GamepadAimRateMultiplier;
    }

    const float AmountX = X * fabsf(X) * Rate + Orbit.StickCarryX;
    const int32_t StepX = (int32_t)AmountX;
    Orbit.StickCarryX = AmountX - (float)StepX;
    const float AmountY = Y * fabsf(Y) * Rate + Orbit.StickCarryY;
    const int32_t StepY = (int32_t)AmountY;
    Orbit.StickCarryY = AmountY - (float)StepY;
    Orbit.MouseDeltaX += StepX;
    Orbit.MouseDeltaY += StepY;
}

// Merges every source routed to a port into the scheme's own controls. Keys
// give full stick deflection; a gamepad's stick gives its analogue value and
// also reads as the matching keys past the digital threshold. Where both push
// an axis the further one wins, so a key held beside a resting stick still
// moves at full speed.
void CJetForceGeminiRuntime::ReadControls(const JFG_PORT_INPUT & Input, JFG_CONTROLS & Controls)
{
    memset(&Controls, 0, sizeof(Controls));
    int32_t StickX = 0;
    int32_t StickY = 0;

    if (Input.KeyboardMouse != nullptr)
    {
        const KEYBOARD_MOUSE_STATE & Keyboard = *Input.KeyboardMouse;
        // The input backend exposes physical keys. Keep the usual ZQSD and WASD
        // positions, then accept their printed-letter counterparts too so either
        // layout stays usable when Windows and the physical keyboard differ.
        Controls.Forward = KeyDown(Keyboard, KeyboardMouseKey_W) || KeyDown(Keyboard, KeyboardMouseKey_Z);
        Controls.Backward = KeyDown(Keyboard, KeyboardMouseKey_S);
        Controls.Left = KeyDown(Keyboard, KeyboardMouseKey_A) || KeyDown(Keyboard, KeyboardMouseKey_Q);
        Controls.Right = KeyDown(Keyboard, KeyboardMouseKey_D);
        Controls.CUp = KeyDown(Keyboard, KeyboardMouseKey_Space);
        Controls.CDown = KeyDown(Keyboard, KeyboardMouseKey_LeftControl) ||
                         KeyDown(Keyboard, KeyboardMouseKey_RightControl);
        Controls.Sprint = KeyDown(Keyboard, KeyboardMouseKey_LeftShift);
        Controls.A = KeyDown(Keyboard, KeyboardMouseKey_E);
        Controls.B = KeyDown(Keyboard, KeyboardMouseKey_F);
        Controls.Start = KeyDown(Keyboard, KeyboardMouseKey_Return);
        Controls.SkipCinematic = Controls.A || Controls.Start;
        Controls.Fire = MouseButtonDown(Keyboard, MouseButtonLeft);
        Controls.AimMouse = MouseButtonDown(Keyboard, MouseButtonRight);
        StickX = (Controls.Right ? JfgStickLimit : 0) - (Controls.Left ? JfgStickLimit : 0);
        StickY = (Controls.Forward ? JfgStickLimit : 0) - (Controls.Backward ? JfgStickLimit : 0);
    }

    for (size_t i = 0; i < sizeof(Input.Gamepads) / sizeof(Input.Gamepads[0]); i++)
    {
        if (Input.Gamepads[i] == nullptr)
        {
            continue;
        }
        const GAMEPAD_STATE & Pad = *Input.Gamepads[i];
        float X;
        float Y;
        NormaliseStick(Pad.LeftX, Pad.LeftY, X, Y);
        Controls.Forward = Controls.Forward || Y < -GamepadDigitalThreshold;
        Controls.Backward = Controls.Backward || Y > GamepadDigitalThreshold;
        Controls.Left = Controls.Left || X < -GamepadDigitalThreshold;
        Controls.Right = Controls.Right || X > GamepadDigitalThreshold;
        // The N64 stick reads up as positive, the opposite of SDL
        const int32_t PadX = StickToN64(X);
        const int32_t PadY = StickToN64(-Y);
        if (abs(PadX) > abs(StickX))
        {
            StickX = PadX;
        }
        if (abs(PadY) > abs(StickY))
        {
            StickY = PadY;
        }

        // The game's control setup jumps and crouches on C-up and C-down, the
        // keyboard's Space and Ctrl, and spends the N64 A and B on weapon
        // cycling, so the pad's A and B land on the C buttons and X/Y on the
        // weapon notches; see ReadScrollButtons.
        const bool PadA = GamepadButtonDown(Pad, GamepadButton_A);
        const bool PadStart = GamepadButtonDown(Pad, GamepadButton_Start);
        Controls.CUp = Controls.CUp || PadA;
        Controls.CDown = Controls.CDown || GamepadButtonDown(Pad, GamepadButton_B);
        Controls.CLeft = Controls.CLeft || GamepadButtonDown(Pad, GamepadButton_LeftShoulder);
        Controls.CRight = Controls.CRight || GamepadButtonDown(Pad, GamepadButton_RightShoulder);
        Controls.Start = Controls.Start || PadStart;
        Controls.SkipCinematic = Controls.SkipCinematic || PadA || PadStart;
        Controls.Fire = Controls.Fire || GamepadTriggerDown(Pad.RightTrigger);
        Controls.AimPad = Controls.AimPad || GamepadTriggerDown(Pad.LeftTrigger);
        Controls.Sprint = Controls.Sprint || GamepadButtonDown(Pad, GamepadButton_LeftStick);

        float CameraX;
        float CameraY;
        NormaliseStick(Pad.RightX, Pad.RightY, CameraX, CameraY);
        if (fabsf(CameraX) > fabsf(Controls.CameraX))
        {
            Controls.CameraX = CameraX;
        }
        if (fabsf(CameraY) > fabsf(Controls.CameraY))
        {
            Controls.CameraY = CameraY;
        }
        Controls.DpadUp = Controls.DpadUp || GamepadButtonDown(Pad, GamepadButton_DpadUp);
        Controls.DpadDown = Controls.DpadDown || GamepadButtonDown(Pad, GamepadButton_DpadDown);
        Controls.DpadLeft = Controls.DpadLeft || GamepadButtonDown(Pad, GamepadButton_DpadLeft);
        Controls.DpadRight = Controls.DpadRight || GamepadButtonDown(Pad, GamepadButton_DpadRight);
    }

    Controls.StickX = (int8_t)StickX;
    Controls.StickY = (int8_t)StickY;
    Controls.Aim = Controls.AimMouse || Controls.AimPad;
}

// Rising edges of Y (next weapon, the wheel-up B impulse) and X (previous
// weapon, the wheel-down A impulse) across the pads on a port. State keeps the
// last seen level per pad slot so a held button is one notch, not one per poll.
int32_t CJetForceGeminiRuntime::ReadScrollButtons(const JFG_PORT_INPUT & Input, SCROLL_BUTTON_STATE & State)
{
    int32_t Scroll = 0;
    for (size_t i = 0; i < sizeof(Input.Gamepads) / sizeof(Input.Gamepads[0]); i++)
    {
        const bool PreviousDown =
            Input.Gamepads[i] != nullptr && GamepadButtonDown(*Input.Gamepads[i], GamepadButton_X);
        const bool NextDown =
            Input.Gamepads[i] != nullptr && GamepadButtonDown(*Input.Gamepads[i], GamepadButton_Y);
        if (NextDown && !State.NextDown[i])
        {
            Scroll = 1;
        }
        else if (PreviousDown && !State.PreviousDown[i])
        {
            Scroll = -1;
        }
        State.PreviousDown[i] = PreviousDown;
        State.NextDown[i] = NextDown;
    }
    return Scroll;
}

// Ports two to four get the button layout without the camera work, which is
// bound to the first player's objects: mouse travel has nowhere to go and the
// right stick cannot turn the view. It still aims, though. The trigger hands
// the aim to the game's own reticle the way port one's stock aim does, the
// right stick on the N64 stick and the left stick on the C buttons the game
// moves with while targeting. There is no mouse-style aim to fall back on
// here, so this does not follow the port-one option. Floyd in co-op is always
// aiming and reads either stick.
void CJetForceGeminiRuntime::MapSecondaryPort(
    int32_t Control, const JFG_PORT_INPUT & Input, BUTTONS & Buttons)
{
    Buttons.Value = 0;
    if (Control < 1 || Control > 3 || !Input.HasSource())
    {
        return;
    }

    JFG_CONTROLS Controls;
    ReadControls(Input, Controls);
    QueueSecondaryScroll(Control, Input);
    const int32_t Scroll = m_SecondaryQueuedScroll[Control - 1];
    m_SecondaryQueuedScroll[Control - 1] = 0;

    Buttons.A_BUTTON = Controls.A || Scroll < 0;
    Buttons.B_BUTTON = Controls.B || Scroll > 0;
    Buttons.Z_TRIG = Controls.Fire;
    Buttons.R_TRIG = Controls.Aim;
    Buttons.START_BUTTON = Controls.Start;
    Buttons.U_CBUTTON = Controls.CUp;
    Buttons.D_CBUTTON = Controls.CDown;
    Buttons.L_CBUTTON = Controls.CLeft;
    Buttons.R_CBUTTON = Controls.CRight;
    Buttons.U_DPAD = Controls.DpadUp;
    Buttons.D_DPAD = Controls.DpadDown;
    Buttons.L_DPAD = Controls.DpadLeft;
    Buttons.R_DPAD = Controls.DpadRight;

    // In solo play, START on port two toggles cooperativeGame and hands Floyd
    // to that port. His aiming Y runs opposite to the normal movement stick.
    // Read the live flags so joining/leaving co-op and loading a state take
    // effect immediately, without reversing other players or multiplayer.
    uint8_t MultiplayerGame = 0;
    uint8_t CooperativeGame = 0;
    if (Control == 1 && IsSupportedRom() &&
        m_Memory.ReadU8(MultiplayerGameAddress, MultiplayerGame) && MultiplayerGame == 0 &&
        m_Memory.ReadU8(CooperativeGameAddress, CooperativeGame) && CooperativeGame != 0)
    {
        // Either stick aims him, the one pushed further winning each axis.
        // The right stick is brought to N64 up-positive first so both share
        // the reversal below.
        int32_t AimX = Controls.StickX;
        int32_t AimY = Controls.StickY;
        const int32_t RightX = StickToN64(Controls.CameraX);
        const int32_t RightY = StickToN64(-Controls.CameraY);
        if (abs(RightX) > abs(AimX))
        {
            AimX = RightX;
        }
        if (abs(RightY) > abs(AimY))
        {
            AimY = RightY;
        }
        Buttons.X_AXIS = (int8_t)AimX;
        Buttons.Y_AXIS = (int8_t)-AimY;
        return;
    }

    // Aiming from the trigger, as in port one's stock aim: controlGetManualAim
    // pins the reticle at half deflection and turns past it, so the right
    // stick goes through as it is, and its Y runs the other way from the
    // camera's, so it is not flipped into N64 up-positive: pushing up moves
    // the reticle up. The left stick moves on the C buttons meanwhile, the
    // shoulder buttons still sidestepping on the same two.
    if (Controls.AimPad && !Controls.AimMouse)
    {
        Buttons.X_AXIS = StickToN64(Controls.CameraX);
        Buttons.Y_AXIS = StickToN64(Controls.CameraY);
        Buttons.U_CBUTTON = Controls.Forward || Controls.CUp;
        Buttons.D_CBUTTON = Controls.Backward || Controls.CDown;
        Buttons.L_CBUTTON = Controls.CLeft || Controls.Left;
        Buttons.R_CBUTTON = Controls.CRight || Controls.Right;
        return;
    }

    Buttons.X_AXIS = Controls.StickX;
    Buttons.Y_AXIS = Controls.StickY;
}

// Removes the 30fps -> 20fps escalation in viFrameSync, see FramePacingPatches.
void CJetForceGeminiRuntime::PatchFramePacing(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(FramePacingEscalateStore, Current))
    {
        m_FramePacingPatchApplied = Current == FramePacingPatches[0].Replacement;
    }

    if (!Enabled && !m_FramePacingPatchApplied)
    {
        return;
    }

    // bne $t4, $zero, 800550F0 / addiu $v0, $v0, -0x1355 / addiu $t5, $zero, 3,
    // the three instructions leading into the store being patched.
    uint32_t Signature[3];
    if (!m_Memory.ReadU32(FramePacingSignatureBase + 0x00, Signature[0]) ||
        !m_Memory.ReadU32(FramePacingSignatureBase + 0x04, Signature[1]) ||
        !m_Memory.ReadU32(FramePacingSignatureBase + 0x08, Signature[2]) ||
        Signature[0] != 0x15800004 || Signature[1] != FramePacingSignatureWord1 ||
        Signature[2] != 0x240D0003)
    {
        return;
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        FramePacingPatches, sizeof(FramePacingPatches) / sizeof(FramePacingPatches[0]), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }

    m_FramePacingPatchApplied = Enabled;
}

// The landing-skip code cave belongs to a completed retail diagnostic routine,
// not a blank RDRAM page. Capture and restore it exactly, with the live entry
// always removed first, so save states can never contain this trampoline.
bool CJetForceGeminiRuntime::PatchLandingCinematicSkip(bool Enabled)
{
    const std::vector<uint32_t> StubImage = BuildLandingCinematicSkipImage();
    const size_t StubCount = StubImage.size();
    if (LandingCinematicSkipEntry == 0 || LandingCinematicSkipStub == 0 || StubCount == 0)
    {
        // No landing skip on this build (the Kiosk demo): nothing to install
        // and nothing that could have been installed.
        return !Enabled;
    }
    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { LandingCinematicSkipEntry, LandingCinematicSkipEntryOriginal, LandingCinematicSkipJump },
        { LandingCinematicSkipEntry + 0x04, LandingCinematicSkipEntryDelayOriginal,
          LandingCinematicSkipEntryOriginal },
    };

    auto BuildStubWrites = [this, &StubImage, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[i];
            Write.Address = LandingCinematicSkipStub + (uint32_t)(i * sizeof(uint32_t));
            Write.Desired = Install ? StubImage[i] :
                                     m_LandingCinematicSkipStubOriginal[i];
            Write.Allowed[0] = m_LandingCinematicSkipStubOriginal[i];
            Write.Allowed[1] = StubImage[i];
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
            EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        m_LandingCinematicSkipHookApplied = false;
        if (m_LandingCinematicSkipStubOriginal.empty())
        {
            return true;
        }
        if (m_LandingCinematicSkipStubOriginal.size() != StubCount)
        {
            return false;
        }

        std::vector<GAME_HACK_CODE_WRITE> Writes;
        BuildStubWrites(Writes, false);
        CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
        if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        m_LandingCinematicSkipStubOriginal.clear();
        return true;
    }

    if (m_LandingCinematicSkipHookApplied)
    {
        uint32_t Entry = 0;
        if (m_Memory.ReadU32(LandingCinematicSkipEntry, Entry) && Entry == LandingCinematicSkipJump)
        {
            return true;
        }
        m_LandingCinematicSkipHookApplied = false;
    }

    if (m_LandingCinematicSkipStubOriginal.empty())
    {
        uint32_t Entry = 0;
        uint32_t Delay = 0;
        if (!m_Memory.ReadU32(LandingCinematicSkipEntry, Entry) ||
            !m_Memory.ReadU32(LandingCinematicSkipEntry + 0x04, Delay) ||
            Entry != LandingCinematicSkipEntryOriginal || Delay != LandingCinematicSkipEntryDelayOriginal)
        {
            return false;
        }
        m_LandingCinematicSkipStubOriginal.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            if (!m_Memory.ReadU32(LandingCinematicSkipStub + (uint32_t)(i * sizeof(uint32_t)),
                                  m_LandingCinematicSkipStubOriginal[i]))
            {
                m_LandingCinematicSkipStubOriginal.clear();
                return false;
            }
        }
    }
    if (m_LandingCinematicSkipStubOriginal.size() != StubCount)
    {
        return false;
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return false;
    }
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_LandingCinematicSkipStubOriginal.clear();
        return false;
    }
    m_LandingCinematicSkipHookApplied = true;
    return true;
}

// frontRarepage belongs to a relocatable front-end overlay. Resolve the live
// entry each time the hook is armed, and patch only non-relocated words after
// the prologue has already built the state pointer.
bool CJetForceGeminiRuntime::PatchIntroCinematicSkip(bool Enabled)
{
    const size_t StubCount = sizeof(IntroCinematicSkipHookCode) /
                             sizeof(IntroCinematicSkipHookCode[0]);
    auto FindEntry = [this](uint32_t & OverlayBase, uint32_t & Entry, uint32_t & Resume) {
        OverlayBase = 0;
        Entry = 0;
        Resume = 0;
        uint32_t OverlayTable = 0;
        const uint32_t RequiredTableSize = (IntroCinematicSkipOverlayModule + 1) * OverlayHeaderSize;
        if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) || (OverlayTable & 3) != 0 ||
            !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize))
        {
            return false;
        }

        const uint32_t OverlayHeader = OverlayTable + IntroCinematicSkipOverlayModule * OverlayHeaderSize;
        if (!m_Memory.ReadU32(OverlayHeader, OverlayBase) || (OverlayBase & 3) != 0 ||
            !m_Memory.IsRdramAddress(OverlayBase + IntroCinematicSkipEntryOffset,
                                     2 * sizeof(uint32_t)) ||
            !m_Memory.IsRdramAddress(OverlayBase + IntroCinematicSkipResumeOffset,
                                     sizeof(uint32_t)))
        {
            OverlayBase = 0;
            return false;
        }
        Entry = OverlayBase + IntroCinematicSkipEntryOffset;
        Resume = OverlayBase + IntroCinematicSkipResumeOffset;
        return true;
    };
    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install,
                                             uint32_t Resume) {
        Writes.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            uint32_t HookWord = ScratchCaveWord(IntroCinematicSkipHookCode[i]);
            if (i == IntroCinematicSkipResumeJumpIndex && Resume != 0)
            {
                HookWord = JumpTo(Resume);
            }

            GAME_HACK_CODE_WRITE & Write = Writes[i];
            Write.Address = IntroCinematicSkipStub + (uint32_t)(i * sizeof(uint32_t));
            Write.Desired = Install ? HookWord : m_IntroCinematicSkipStubOriginal[i];
            Write.Allowed[0] = m_IntroCinematicSkipStubOriginal[i];
            Write.Allowed[1] = HookWord;
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        uint32_t Resume = 0;
        if (m_IntroCinematicSkipOverlayBase != 0)
        {
            Resume = m_IntroCinematicSkipOverlayBase + IntroCinematicSkipResumeOffset;
        }

        if (m_IntroCinematicSkipHookApplied && m_IntroCinematicSkipOverlayBase != 0 &&
            m_Memory.IsRdramAddress(m_IntroCinematicSkipOverlayBase + IntroCinematicSkipEntryOffset,
                                     2 * sizeof(uint32_t)))
        {
            const uint32_t Entry = m_IntroCinematicSkipOverlayBase + IntroCinematicSkipEntryOffset;
            uint32_t EntryWord = 0;
            uint32_t DelayWord = 0;
            if (m_Memory.ReadU32(Entry, EntryWord) &&
                m_Memory.ReadU32(Entry + 0x04, DelayWord) &&
                EntryWord == IntroCinematicSkipJump && DelayWord == 0x00000000)
            {
                const GAME_HACK_CODE_PATCH EntryPatches[] =
                {
                    { Entry, IntroCinematicSkipEntryOriginal, IntroCinematicSkipJump },
                    { Entry + 0x04, IntroCinematicSkipEntryDelayOriginal, 0x00000000 },
                };
                CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
                    EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
                if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
                    EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
                {
                    return false;
                }
            }
        }
        m_IntroCinematicSkipHookApplied = false;
        m_IntroCinematicSkipOverlayBase = 0;
        if (m_IntroCinematicSkipStubOriginal.empty())
        {
            return true;
        }
        if (m_IntroCinematicSkipStubOriginal.size() != StubCount)
        {
            return false;
        }

        std::vector<GAME_HACK_CODE_WRITE> Writes;
        BuildStubWrites(Writes, false, Resume);
        CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
        if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        m_IntroCinematicSkipStubOriginal.clear();
        return true;
    }

    uint32_t OverlayBase = 0;
    uint32_t Entry = 0;
    uint32_t Resume = 0;
    if (!FindEntry(OverlayBase, Entry, Resume))
    {
        return false;
    }
    if (m_IntroCinematicSkipHookApplied)
    {
        uint32_t EntryWord = 0;
        uint32_t DelayWord = 0;
        if (OverlayBase == m_IntroCinematicSkipOverlayBase &&
            m_Memory.ReadU32(Entry, EntryWord) &&
            m_Memory.ReadU32(Entry + 0x04, DelayWord) &&
            EntryWord == IntroCinematicSkipJump && DelayWord == 0x00000000)
        {
            return true;
        }
        m_IntroCinematicSkipHookApplied = false;
        m_IntroCinematicSkipOverlayBase = 0;
    }

    uint32_t EntryWord = 0;
    uint32_t DelayWord = 0;
    if (!m_Memory.ReadU32(Entry, EntryWord) ||
        !m_Memory.ReadU32(Entry + 0x04, DelayWord) ||
        EntryWord != IntroCinematicSkipEntryOriginal ||
        DelayWord != IntroCinematicSkipEntryDelayOriginal)
    {
        return false;
    }
    if (m_IntroCinematicSkipStubOriginal.empty())
    {
        m_IntroCinematicSkipStubOriginal.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            if (!m_Memory.ReadU32(IntroCinematicSkipStub + (uint32_t)(i * sizeof(uint32_t)),
                                  m_IntroCinematicSkipStubOriginal[i]))
            {
                m_IntroCinematicSkipStubOriginal.clear();
                return false;
            }
        }
    }
    if (m_IntroCinematicSkipStubOriginal.size() != StubCount)
    {
        return false;
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true, Resume);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return false;
    }

    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { Entry, IntroCinematicSkipEntryOriginal, IntroCinematicSkipJump },
        { Entry + 0x04, IntroCinematicSkipEntryDelayOriginal, 0x00000000 },
    };
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false, Resume);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_IntroCinematicSkipStubOriginal.clear();
        return false;
    }
    m_IntroCinematicSkipHookApplied = true;
    m_IntroCinematicSkipOverlayBase = OverlayBase;
    return true;
}

bool CJetForceGeminiRuntime::SetWidescreenHudReticle(bool Enabled)
{
    if (!Enabled && m_WidescreenReticleOverlayBase == 0 &&
        m_WidescreenHudCaveOriginal.empty() && !m_WidescreenHudCaveApplied)
    {
        return true;
    }
    uint8_t Resolution = 0;
    if (Enabled && (!m_WidescreenHudCaveApplied ||
                    !m_Memory.ReadU8(WidescreenHudResolutionIndexAddress, Resolution) ||
                    !IsWidescreenHudResolution(Resolution)))
    {
        return false;
    }

    uint32_t Table = 0;
    uint32_t Base = 0;
    const uint32_t TableSize = (WidescreenHudReticleOverlayModule + 1) * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayTableAddress, Table) || (Table & 3) != 0 ||
        !m_Memory.IsRdramAddress(Table, TableSize) ||
        !m_Memory.ReadU32(Table + WidescreenHudReticleOverlayModule * OverlayHeaderSize, Base))
    {
        return false;
    }
    if (m_WidescreenReticleOverlayBase != 0 && Base != m_WidescreenReticleOverlayBase)
    {
        // Overlay 13 moved or was freed. Never write into its former allocation.
        m_WidescreenReticleOverlayBase = 0;
    }
    // A state load may have replaced the live overlay without restoring host
    // bookkeeping. Validate the current allocation even when the old one moved.
    if (Base == 0)
    {
        return true;
    }
    if ((Base & 3) != 0 || !m_Memory.IsRdramAddress(Base, 0xEA0))
    {
        return false;
    }

    const uint32_t DrawSignature[] = { 0x27BDFF68, 0xAFB4002C, 0xAFB30028, 0xAFB10020 };
    for (size_t i = 0; i < sizeof(DrawSignature) / sizeof(DrawSignature[0]); i++)
    {
        uint32_t Current = 0;
        if (!m_Memory.ReadU32(Base + WidescreenHudReticleDrawOffset + (uint32_t)(i * 4), Current) ||
            Current != DrawSignature[i])
        {
            return false;
        }
    }

    std::vector<GAME_HACK_CODE_PATCH> Patches;
    const uint32_t Replacement = CallTo(WidescreenHudReticleStub);
    for (const WIDESCREEN_HUD_RETICLE_CALL & Call : WidescreenHudReticleCalls)
    {
        uint32_t Current = 0;
        uint32_t Delay = 0;
        const uint32_t CallAddress = Base + JfgHudBuild::Offset(WidescreenHudReticleOverlayModule, Call.Offset);
        if (!m_Memory.ReadU32(CallAddress, Current) ||
            !m_Memory.ReadU32(CallAddress + 4, Delay) || Delay != Call.Delay ||
            (Current != WidescreenHudReticleLineCallOriginal && Current != Replacement))
        {
            return false;
        }
        Patches.push_back({ CallAddress, WidescreenHudReticleLineCallOriginal, Replacement });
    }
    const CGameHackCodePatcher::Result Result =
        m_CodePatcher.SetEnabled(Patches.data(), Patches.size(), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return false;
    }
    m_WidescreenReticleOverlayBase = Enabled ? Base : 0;
    return true;
}

// Call only after the live module table and renderer signature identify overlay
// 14. Accept both installed resolutions for state loads, mode changes and undo.
bool CJetForceGeminiRuntime::SetWidescreenHudBanner(uint32_t OverlayBase, bool Enabled)
{
    uint8_t Resolution = 0;
    if (Enabled && (!m_Memory.ReadU8(WidescreenHudResolutionIndexAddress, Resolution) ||
                    !IsWidescreenHudResolution(Resolution)))
    {
        return false;
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    auto AddWrite = [&](const WIDESCREEN_HUD_BANNER_WORD_PATCH & Patch)
    {
        GAME_HACK_CODE_WRITE Write = {};
        Write.Address = OverlayBase + JfgHudBuild::Offset(WidescreenHudOverlayModule, Patch.Offset);
        Write.Desired = !Enabled ? Patch.Original :
            (JfgHudBuild::VideoMode(Resolution) & 2) != 0 ? Patch.HighResolution : Patch.LowResolution;
        Write.Allowed[0] = Patch.Original;
        Write.Allowed[1] = Patch.LowResolution;
        Write.Allowed[2] = Patch.HighResolution;
        Write.AllowedCount = 3;
        Writes.push_back(Write);
    };
    // Both layouts live in overlay 14 and share its signature-checked lifetime.
    for (const WIDESCREEN_HUD_BANNER_WORD_PATCH & Patch : WidescreenHudBannerPatches)
    {
        AddWrite(Patch);
    }
    const WIDESCREEN_HUD_BANNER_WORD_PATCH * FuelPatches =
        JfgHudBuild::Current() == JfgHudBuild::BuildPal ? WidescreenHudFuelPatchesPal : WidescreenHudFuelPatches;
    for (size_t i = 0; i < sizeof(WidescreenHudFuelPatches) / sizeof(WidescreenHudFuelPatches[0]); i++)
    {
        AddWrite(FuelPatches[i]);
    }
    const CGameHackCodePatcher::Result Result = m_CodePatcher.Apply(Writes.data(), Writes.size());
    return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
           Result != CGameHackCodePatcher::Result_MemoryUnavailable;
}

bool CJetForceGeminiRuntime::SetWidescreenHudShotGauge(uint32_t OverlayBase, bool Enabled)
{
    // Called only after the live overlay-14 renderer signatures are checked.
    // The gauge wrapper marks its call chain; it leaves this game's rectangle
    // table intact. The shared rectangle hook performs the whole correction
    // under its draw-time resolution/scope guards.
    const GAME_HACK_CODE_PATCH CallPatch = {
        OverlayBase + WidescreenHudShotGaugeCallOffset,
        WidescreenHudShotGaugeCallOriginal, CallTo(WidescreenHudShotGaugeWrapperStub),
    };
    auto Succeeded = [](CGameHackCodePatcher::Result Result) {
        return Result == CGameHackCodePatcher::Result_NoChanges ||
               Result == CGameHackCodePatcher::Result_Changed;
    };
    uint32_t Call = 0, Delay = 0;
    if ((OverlayBase & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayBase, JfgHudBuild::Offset(WidescreenHudOverlayModule, 0x44C8) + 4) ||
        !m_Memory.ReadU32(CallPatch.Address, Call) ||
        !m_Memory.ReadU32(CallPatch.Address + 4, Delay))
    {
        return false;
    }
    if ((Enabled || Call == CallPatch.Replacement) &&
        ((Call != CallPatch.Original && Call != CallPatch.Replacement) ||
         Delay != WidescreenHudShotGaugeCallDelay))
    {
        return false;
    }
    // Stop this independent caller even when an unrecognized table prevents
    // migration; never leave a redirect to a cave that could be restored.
    if (!Enabled && Call == CallPatch.Replacement &&
        !Succeeded(m_CodePatcher.SetEnabled(&CallPatch, 1, false)))
    {
        return false;
    }

    // Migrate old states and installations which pretranslated all X values
    // by -45. Y and colour belong to the game: compare only the complete X
    // pattern and preserve the other halfword, including a changed Y.
    // A partially restored mixture of known original/legacy X values is safe;
    // an unknown X refuses the entire migration before any table write.
    std::vector<GAME_HACK_CODE_WRITE> Writes;
    for (const auto & Legacy : WidescreenHudShotGaugePatches)
    {
        uint32_t Current = 0;
        const uint32_t LegacyAddress = OverlayBase + JfgHudBuild::Offset(WidescreenHudOverlayModule, Legacy.Offset);
        if (!m_Memory.ReadU32(LegacyAddress, Current) ||
            ((Current >> 16) != (Legacy.Original >> 16) &&
             (Current >> 16) != (Legacy.Replacement >> 16)))
        {
            return false;
        }
        GAME_HACK_CODE_WRITE Write = {};
        Write.Address = LegacyAddress;
        Write.Desired = (Legacy.Original & 0xFFFF0000) | (Current & 0xFFFF);
        Write.Allowed[0] = Current;
        Write.AllowedCount = 1;
        Writes.push_back(Write);
    }
    if (!Succeeded(m_CodePatcher.Apply(Writes.data(), Writes.size())))
    {
        return false;
    }
    return !Enabled || Succeeded(m_CodePatcher.SetEnabled(&CallPatch, 1, true));
}

bool CJetForceGeminiRuntime::SetWidescreenHudFloyd(uint32_t OverlayBase, bool Enabled)
{
    // The live overlay-14 scope signatures are validated by the caller. Only
    // this line call belongs to the Floyd HUD; its data table remains stock.
    const GAME_HACK_CODE_PATCH CallPatch = {
        OverlayBase + JfgHudBuild::Offset(WidescreenHudOverlayModule, 0x468),
        JfgHudBuild::Word(0x0C01B4E4), CallTo(WidescreenHudFloydLineStub),
    };
    uint32_t Call = 0, Delay = 0;
    if ((OverlayBase & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayBase, 0x470) ||
        !m_Memory.ReadU32(CallPatch.Address, Call))
    {
        return false;
    }
    if (!Enabled && Call != CallPatch.Replacement)
    {
        // A foreign word is never restored as one of our installed callers.
        return true;
    }
    if ((Call != CallPatch.Original && Call != CallPatch.Replacement) ||
        !m_Memory.ReadU32(CallPatch.Address + 4, Delay) || Delay != 0xAFB90010)
    {
        return false;
    }
    const CGameHackCodePatcher::Result Result =
        m_CodePatcher.SetEnabled(&CallPatch, 1, Enabled);
    return Result == CGameHackCodePatcher::Result_NoChanges ||
           Result == CGameHackCodePatcher::Result_Changed;
}

// Remove the relocatable overlay hooks, banner, gauge and Floyd before any fixed hook
// or cave word. Once the module table points elsewhere, the old allocation
// belongs to the overlay allocator again and must be forgotten without writes.
bool CJetForceGeminiRuntime::RemoveWidescreenHudOverlayHooks(void)
{
    if (m_WidescreenHudOverlayBase == 0 &&
        m_WidescreenHudCaveOriginal.empty() && !m_WidescreenHudCaveApplied)
    {
        m_WidescreenHudOverlayHookApplied = false;
        return true;
    }

    uint32_t OverlayTable = 0;
    uint32_t LiveOverlayBase = 0;
    const uint32_t RequiredTableSize =
        (WidescreenHudOverlayModule + 1) * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) ||
        (OverlayTable & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize) ||
        !m_Memory.ReadU32(
            OverlayTable + WidescreenHudOverlayModule * OverlayHeaderSize,
            LiveOverlayBase))
    {
        // Keep the cave and all callers intact until ownership can be proven.
        return false;
    }
    if (LiveOverlayBase != m_WidescreenHudOverlayBase)
    {
        m_WidescreenHudOverlayHookApplied = false;
        m_WidescreenHudOverlayBase = 0;
    }
    if (LiveOverlayBase == 0)
    {
        return true;
    }

    // A loaded state can contain our hooks at a new address. Recognize the
    // live renderer before adopting it for cleanup; never touch the old base.
    uint32_t EnterWord = 0;
    uint32_t EnterDelay = 0;
    uint32_t ExitWord = 0;
    uint32_t ExitDelay = 0;
    if ((LiveOverlayBase & 3) != 0 ||
        !m_Memory.IsRdramAddress(LiveOverlayBase, WidescreenHudOverlayExitOffset + 8) ||
        !m_Memory.ReadU32(LiveOverlayBase + WidescreenHudOverlayEnterOffset, EnterWord) ||
        !m_Memory.ReadU32(LiveOverlayBase + WidescreenHudOverlayEnterOffset + 4, EnterDelay) ||
        !m_Memory.ReadU32(LiveOverlayBase + WidescreenHudOverlayExitOffset, ExitWord) ||
        !m_Memory.ReadU32(LiveOverlayBase + WidescreenHudOverlayExitOffset + 4, ExitDelay) ||
        (EnterWord != WidescreenHudOverlayEnterOriginal && EnterWord != CallTo(WidescreenHudScopeEnterStub)) ||
        EnterDelay != WidescreenHudOverlayEnterDelayOriginal ||
        (ExitWord != WidescreenHudOverlayExitOriginal && ExitWord != JumpTo(WidescreenHudScopeExitStub)) ||
        ExitDelay != WidescreenHudOverlayExitDelayOriginal)
    {
        return false;
    }
    m_WidescreenHudOverlayBase = LiveOverlayBase;

    const uint32_t Enter = m_WidescreenHudOverlayBase + WidescreenHudOverlayEnterOffset;
    const uint32_t Exit = m_WidescreenHudOverlayBase + WidescreenHudOverlayExitOffset;
    const GAME_HACK_CODE_PATCH EnterPatch =
    {
        Enter, WidescreenHudOverlayEnterOriginal, CallTo(WidescreenHudScopeEnterStub),
    };
    const GAME_HACK_CODE_PATCH ExitPatch =
    {
        Exit, WidescreenHudOverlayExitOriginal, JumpTo(WidescreenHudScopeExitStub),
    };

    auto RemoveReference = [this](const GAME_HACK_CODE_PATCH & Patch,
                                  uint32_t DelayOriginal) {
        uint32_t Current = 0;
        if (!m_Memory.ReadU32(Patch.Address, Current))
        {
            return false;
        }
        if (Current != Patch.Replacement)
        {
            return true;
        }

        uint32_t Delay = 0;
        if (!m_Memory.ReadU32(Patch.Address + 0x04, Delay) || Delay != DelayOriginal)
        {
            return false;
        }
        CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(&Patch, 1, false);
        return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
               Result != CGameHackCodePatcher::Result_MemoryUnavailable;
    };

    // Stop new scopes first. The exit remains available until the enter hook is
    // gone, mirroring the stub-first/entry-last install order in reverse.
    if (!RemoveReference(EnterPatch, WidescreenHudOverlayEnterDelayOriginal))
    {
        return false;
    }
    // The caller has just proved that this allocation is still overlay 14.
    // Accept either video mode so a resolution switch can restore the old one.
    const bool BannerRemoved = SetWidescreenHudBanner(m_WidescreenHudOverlayBase, false);
    const bool GaugeRemoved = SetWidescreenHudShotGauge(m_WidescreenHudOverlayBase, false);
    const bool FloydRemoved = SetWidescreenHudFloyd(m_WidescreenHudOverlayBase, false);
    if (!BannerRemoved || !GaugeRemoved || !FloydRemoved)
    {
        return false;
    }
    if (!RemoveReference(ExitPatch, WidescreenHudOverlayExitDelayOriginal))
    {
        return false;
    }

    m_WidescreenHudOverlayHookApplied = false;
    m_WidescreenHudOverlayBase = 0;
    return true;
}

bool CJetForceGeminiRuntime::PatchHudAlignment(bool Enabled, bool WidescreenCorrected)
{
    namespace Layout = JfgHudAlignment;
    namespace Code = JfgHudAlignmentCode;
    namespace Sites = JfgHudAlignmentSites;
    namespace Rdp = JfgHudAlignmentRdp;
    const bool HostOwned = m_HudAlignmentScopeOwned || !m_HudAlignmentImage.empty();
    if (!IsSupportedRom() || JfgHudBuild::Current() == JfgHudBuild::BuildNone)
    {
        return !Enabled && !HostOwned;
    }
    const uint32_t * OriginalCave = Layout::OriginalCaveWords();
    const uint32_t * OriginalGuard = Layout::OriginalGuardWords();
    auto Succeeded = [](CGameHackCodePatcher::Result Result) {
        return Result == CGameHackCodePatcher::Result_NoChanges ||
               Result == CGameHackCodePatcher::Result_Changed;
    };
    auto WordIs = [&](uint32_t Address, uint32_t Expected) {
        uint32_t Current = 0;
        return m_Memory.ReadU32(Address, Current) && Current == Expected;
    };
    auto Forget = [&]() {
        m_HudAlignmentCaveOriginal.clear();
        m_HudAlignmentImage.clear();
        m_HudAlignmentOverlay6Base = 0;
        m_HudAlignmentOverlay14Base = 0;
        m_HudAlignmentScopeOwned = false;
    };

    const size_t WordCount = (Layout::CaveEnd.Us - Layout::CaveStart.Us) / sizeof(uint32_t);
    std::vector<uint32_t> CurrentImage(WordCount);
    for (size_t i = 0; i < WordCount; i++)
    {
        if (!m_Memory.ReadU32(Layout::CaveStart + (uint32_t)(i * 4), CurrentImage[i]))
        {
            return false;
        }
    }
    const bool StockBody = std::equal(CurrentImage.begin(), CurrentImage.end(), OriginalCave);
    const bool StockGuard = WordIs(Layout::GuardAddress, OriginalGuard[0]) &&
                            WordIs(Layout::GuardAddress + 4, OriginalGuard[1]);
    const bool RetiredGuard = WordIs(Layout::GuardAddress, Layout::GuardRetired[0]) &&
                              WordIs(Layout::GuardAddress + 4, Layout::GuardRetired[1]);
    // Strong save-state adoption: every instruction, padding word and magic
    // must match. Only seven float parameters, four RDP parameters and the
    // relocated JAL to the original weapon draw routine may differ.
    std::vector<uint32_t> Signature;
    bool KnownBody = Layout::BuildImage(Signature, 0x80300000, 0, false);
    for (size_t i = 0; KnownBody && i < WordCount; i++)
    {
        const uint32_t Address = Layout::CaveStart + (uint32_t)(i * 4);
        if (!Layout::IsParameterAddress(Address) && CurrentImage[i] != Signature[i])
        {
            KnownBody = false;
        }
    }
    if (KnownBody)
    {
        const uint32_t BodyCall = CurrentImage[(Rdp::BodyCallAddress.Us - Layout::CaveStart.Us) / 4];
        const uint32_t BodyAddress = 0x80000000 | ((BodyCall & 0x03FFFFFF) << 2);
        KnownBody = (BodyCall >> 26) == 3 && m_Memory.IsRdramAddress(BodyAddress, 8);
    }
    const bool Adoptable = KnownBody && RetiredGuard;
    const bool Stock = StockBody && StockGuard;
    if (!Stock && !Adoptable && !HostOwned)
    {
        // An unrelated modification of the diagnostic routine is never storage
        // we can borrow, nor a reason to rewrite the game's padding or hooks.
        return !Enabled;
    }

    uint8_t ResolutionIndex = 0, PlayerCount = 0, ActiveKind = 0;
    if (!m_Memory.ReadU8(Code::ActiveKindAddress, ActiveKind))
    {
        return false;
    }
    if (Enabled && (!m_Memory.ReadU8(WidescreenHudResolutionIndexAddress, ResolutionIndex) ||
                    JfgHudBuild::VideoMode(ResolutionIndex) == 0xFF ||
                    !m_Memory.ReadU8(Layout::PlayerCountAddress, PlayerCount) || PlayerCount != 1))
    {
        Enabled = false;
    }
    if (!Enabled && Stock && !HostOwned)
    {
        return true;
    }
    if (Enabled && Stock && !HostOwned && ActiveKind != 0)
    {
        return false;
    }

    uint32_t Table = 0, Base6 = 0, Base14 = 0;
    const bool TableReady = m_Memory.ReadU32(OverlayTableAddress, Table) &&
                            (Table & 3) == 0 &&
                            m_Memory.IsRdramAddress(Table, 15 * OverlayHeaderSize) &&
                            m_Memory.ReadU32(Table + 6 * OverlayHeaderSize, Base6) &&
                            m_Memory.ReadU32(Table + 14 * OverlayHeaderSize, Base14);
    auto ValidBase = [&](uint32_t Base, uint32_t Size) {
        return Base != 0 && (Base & 3) == 0 && m_Memory.IsRdramAddress(Base, Size);
    };
    auto PrologueIs = [&](uint32_t Base, uint32_t Offset, const uint32_t * Words, size_t Count) {
        for (size_t i = 0; i < Count; i++)
        {
            if (!WordIs(Base + Offset + (uint32_t)(i * 4), Words[i]))
            {
                return false;
            }
        }
        return true;
    };
    const uint32_t Span6 = Sites::HealthSpan(), Span14 = Sites::WeaponSpan();
    const bool Ready6 = TableReady && ValidBase(Base6, Span6) &&
                        PrologueIs(Base6, JfgHudBuild::Offset(Sites::HealthModule, Sites::HealthFunctionOffset),
                                   Sites::HealthFunctionPrologue, 2);
    const bool Ready14 = TableReady && ValidBase(Base14, Span14) &&
                         PrologueIs(Base14, JfgHudBuild::Offset(Sites::WeaponModule, Sites::WeaponGroupFunctionOffset),
                                    Sites::WeaponGroupFunctionPrologue, 2);

    struct Reference
    {
        GAME_HACK_CODE_PATCH Patch;
        uint32_t Delay;
    };
    std::vector<Reference> References;
    auto Add = [&](uint32_t Base, uint32_t Module, const Sites::CallSite & Site, uint32_t Entry) {
        References.push_back({ { Base + JfgHudBuild::Offset(Module, Site.Offset), Site.Original, CallTo(Entry) },
                               Site.Delay });
    };
    References.push_back({ { Code::SpriteMatrixHookAddress, Code::SpriteMatrixHookOriginal,
                             CallTo(Code::SpriteMatrixEntry) }, Code::SpriteMatrixHookDelay });
    // The module table is authoritative. Never write into a cached allocation
    // after the loader has relocated/replaced that module, even on disable.
    if (TableReady && ValidBase(Base6, Span6))
    {
        Add(Base6, Sites::HealthModule, Sites::HealthSpriteCall, Code::SpriteHealthEntry);
        Add(Base6, Sites::HealthModule, Sites::HealthMatrixCall, Code::MatrixHealthEntry);
    }
    if (TableReady && ValidBase(Base14, Span14))
    {
        for (const auto & Site : Sites::WeaponSpriteCalls)
        {
            Add(Base14, Sites::WeaponModule, Site, Code::SpriteWeaponEntry);
        }
        for (const auto & Site : Sites::WeaponMatrixCalls)
        {
            Add(Base14, Sites::WeaponModule, Site, Code::MatrixWeaponEntry);
        }
        // Install the outer wrapper last, after the scoped sprite/matrix calls.
        References.push_back({ { Base14 + JfgHudBuild::Offset(Sites::WeaponModule, Sites::WeaponGroupCallOffset),
                                 CallTo(Base14 + JfgHudBuild::Offset(Sites::WeaponModule,
                                                                     Sites::WeaponGroupFunctionOffset)),
                                 CallTo(Rdp::Entry) }, Sites::WeaponGroupCallDelay });
    }
    auto RemoveReferences = [&]() {
        bool Safe = TableReady;
        for (auto i = References.rbegin(); i != References.rend(); ++i)
        {
            uint32_t Current = 0;
            if (!m_Memory.ReadU32(i->Patch.Address, Current))
            {
                Safe = false;
                continue;
            }
            if (Current != i->Patch.Replacement)
            {
                continue;
            }
            // A changed delay slot is not our instruction to repair. Keep the
            // cave available until the remaining redirect can be removed.
            if (!WordIs(i->Patch.Address + 4, i->Delay) ||
                !Succeeded(m_CodePatcher.SetEnabled(&i->Patch, 1, false)))
            {
                Safe = false;
            }
        }
        // Zero means unloaded; a nonzero invalid pointer is not proof that the
        // old module and its references have gone away.
        return Safe && (Base6 == 0 || ValidBase(Base6, Span6)) &&
               (Base14 == 0 || ValidBase(Base14, Span14));
    };
    auto Restore = [&]() {
        // Independent removal means a problem in one overlay does not leave
        // unrelated hooks installed. Code remains until every caller is gone.
        if (!RemoveReferences())
        {
            return false;
        }
        if (!Stock && !Adoptable)
        {
            return false; // Preserve an unknown body/guard, even if formerly ours.
        }
        std::vector<GAME_HACK_CODE_WRITE> Writes;
        Writes.reserve(WordCount);
        for (size_t i = 0; i < WordCount; i++)
        {
            GAME_HACK_CODE_WRITE Write = {};
            Write.Address = Layout::CaveStart + (uint32_t)(i * 4);
            Write.Desired = OriginalCave[i];
            Write.Allowed[0] = CurrentImage[i];
            Write.AllowedCount = 1;
            Writes.push_back(Write);
        }
        if (!Succeeded(m_CodePatcher.Apply(Writes.data(), Writes.size())))
        {
            return false;
        }
        const GAME_HACK_CODE_PATCH Guard[] = {
            { Layout::GuardAddress, OriginalGuard[0], Layout::GuardRetired[0] },
            { Layout::GuardAddress + 4, OriginalGuard[1], Layout::GuardRetired[1] },
        };
        if (!Succeeded(m_CodePatcher.SetEnabled(Guard, 2, false)))
        {
            return false;
        }
        if ((Adoptable || HostOwned) && !m_Memory.WriteU8(Code::ActiveKindAddress, 0))
        {
            return false;
        }
        Forget();
        return true;
    };
    if (!Enabled)
    {
        return Restore();
    }
    if (!Ready6 || !Ready14 || (!Stock && !Adoptable))
    {
        if (Adoptable || HostOwned)
        {
            Restore();
        }
        return false;
    }

    // Preflight every entry and delay before changing the guard or cave. This
    // prevents a partially supported overlay from acquiring only half a HUD.
    std::vector<GAME_HACK_CODE_PATCH> Patches;
    for (const auto & Reference : References)
    {
        uint32_t Current = 0;
        if (!m_Memory.ReadU32(Reference.Patch.Address, Current) ||
            (Current != Reference.Patch.Original && Current != Reference.Patch.Replacement) ||
            !WordIs(Reference.Patch.Address + 4, Reference.Delay))
        {
            if (Adoptable || HostOwned)
            {
                Restore();
            }
            return false;
        }
        Patches.push_back(Reference.Patch);
    }
    std::vector<uint32_t> Image;
    if (!Layout::BuildImage(Image, Base14, JfgHudBuild::VideoMode(ResolutionIndex), WidescreenCorrected))
    {
        return false;
    }
    const GAME_HACK_CODE_PATCH Guard[] = {
        { Layout::GuardAddress, OriginalGuard[0], Layout::GuardRetired[0] },
        { Layout::GuardAddress + 4, OriginalGuard[1], Layout::GuardRetired[1] },
    };
    if (!Succeeded(m_CodePatcher.SetEnabled(Guard, 2, true)))
    {
        return false;
    }
    m_HudAlignmentCaveOriginal.assign(OriginalCave, OriginalCave + WordCount);
    m_HudAlignmentScopeOwned = true;
    std::vector<GAME_HACK_CODE_WRITE> Writes;
    Writes.reserve(WordCount);
    for (size_t i = 0; i < WordCount; i++)
    {
        GAME_HACK_CODE_WRITE Write = {};
        Write.Address = Layout::CaveStart + (uint32_t)(i * 4);
        Write.Desired = Image[i];
        Write.Allowed[0] = CurrentImage[i];
        Write.AllowedCount = 1;
        Writes.push_back(Write);
    }
    if (!Succeeded(m_CodePatcher.Apply(Writes.data(), Writes.size())))
    {
        // Apply validates the whole image before writing. A stock installation
        // still has its original body here, so the guard can be returned safely.
        if (Stock && Succeeded(m_CodePatcher.SetEnabled(Guard, 2, false)))
        {
            Forget();
        }
        return false;
    }
    m_HudAlignmentImage = Image;
    m_HudAlignmentOverlay6Base = Base6;
    m_HudAlignmentOverlay14Base = Base14;
    if (Adoptable && !HostOwned && !m_Memory.WriteU8(Code::ActiveKindAddress, 0))
    {
        PatchHudAlignment(false, false);
        return false;
    }
    if (!Succeeded(m_CodePatcher.SetEnabled(Patches.data(), Patches.size(), true)))
    {
        PatchHudAlignment(false, false);
        return false;
    }
    return true;
}

bool CJetForceGeminiRuntime::PatchWidescreenHud(bool Enabled)
{
    const GAME_HACK_CODE_PATCH LegacyFixedPatches[] =
    {
        { WidescreenHudCamCopyEntry, WidescreenHudCamCopyOriginal,
          JumpTo(WidescreenHudCamCopyStub) },
        { WidescreenHudFontYEntry, WidescreenHudFontYOriginal,
          JumpTo(WidescreenHudFontYStub) },
        { WidescreenHudFontDtdyEntry, WidescreenHudFontDtdyOriginal,
          JumpTo(WidescreenHudFontDtdyStub) },
        { WidescreenHudSpriteScaleEntry, WidescreenHudSpriteScaleOriginal,
          JumpTo(WidescreenHudSpriteScaleStub) },
        { WidescreenHudSpriteScaleAltEntry, WidescreenHudSpriteScaleOriginal,
          JumpTo(WidescreenHudSpriteScaleAltStub) },
        { WidescreenHudSpritePositionEntry, WidescreenHudSpritePositionOriginal,
          JumpTo(WidescreenHudSpritePositionStub) },
        { WidescreenHudMatrixTranslateEntry, WidescreenHudMatrixTranslateOriginal,
          JumpTo(WidescreenHudMatrixTranslateStub) },
        { WidescreenHudLineEntry + 0x04, WidescreenHudLineDelayOriginal,
          0x00000000 },
        { WidescreenHudLineEntry, WidescreenHudLineOriginal,
          JumpTo(WidescreenHudLineStub) },
        { WidescreenHudRectangleEntry + 0x04, WidescreenHudRectangleDelayOriginal,
          0x00000000 },
        { WidescreenHudRectangleEntry, WidescreenHudRectangleOriginal,
          JumpTo(WidescreenHudRectangleStub) },
    };
    // Preserve the previous build's adoption signature when loading old states.
    // Append the new step loads before the entry, so removal reverses that order.
    std::vector<GAME_HACK_CODE_PATCH> FixedPatches(
        LegacyFixedPatches,
        LegacyFixedPatches + sizeof(LegacyFixedPatches) / sizeof(LegacyFixedPatches[0]));
    const GAME_HACK_CODE_PATCH AmmoPatches[] =
    {
        { JfgHudBuild::Address(0x800590D0), 0x3C140400, 0x8FB40088 }, // lw s4, 0x88(sp), significant digits
        { JfgHudBuild::Address(0x800590F4), 0x3694FC00, 0x00000000 }, // step already includes dtdy
        { JfgHudBuild::Address(0x8005921C), 0x3C140400, 0x8FB40088 }, // lw s4, 0x88(sp), leading zeroes
        { JfgHudBuild::Address(0x80059228), 0x3694FC00, 0x00000000 },
        { WidescreenHudAmmoEntry, WidescreenHudAmmoOriginal,
          JumpTo(WidescreenHudAmmoStub) },
    };
    FixedPatches.insert(FixedPatches.end(), AmmoPatches,
                        AmmoPatches + sizeof(AmmoPatches) / sizeof(AmmoPatches[0]));
    // Retire the diagnostic entry and install the Floyd rasterizer consumers
    // before exposing its overlay caller. Reverse removal disconnects the
    // consumers before restoring the diagnostic entry and cave image.
    for (const auto & Patch : JfgFloydHud::FixedPatchesForBuild())
    {
        FixedPatches.push_back({ Patch.Address, Patch.Original, Patch.Replacement });
    }
    // fxOutputLines has obtained the dimensions but has not flipped its queue.
    // The lui a1 of its line-queue index moves into the entry's delay slot and
    // the stub replays the displaced low half (0x3B90 on US, 0x35E8 on PAL).
    const JfgHudBuild::BuildWord RocketLineIndexLow = { 0x24A53B90, 0x24A535E8 };
    FixedPatches.push_back({ JfgHudBuild::Address(0x8006E1C4), RocketLineIndexLow, 0x3C058010 });
    FixedPatches.push_back({ JfgHudBuild::Address(0x8006E1C0), 0x3C058010, JumpTo(JfgRocketOverlay::Submit) });

    // Earlier builds patched the framebuffer digit renderer, one of them
    // through a cave trampoline. Both forms are returned to stock below before
    // anything else is touched, so they can never be mistaken for a live hook.
    const uint32_t WidescreenHudDigitalAdvanceLegacyJump =
        JumpTo(WidescreenHudDigitalAdvanceStub);
    auto RetireDeadDigitalPatches = [&]() {
        for (uint32_t i = 0; i < WidescreenHudDigitalRetiredCount; i++)
        {
            const GAME_HACK_CODE_PATCH & Patch = WidescreenHudDigitalRetired[i];
            GAME_HACK_CODE_PATCH Retire = Patch;
            Retire.Address = JfgHudBuild::Address(Patch.Address);
            uint32_t Current = 0;
            if (!m_Memory.ReadU32(Retire.Address, Current))
            {
                return false;
            }
            if (Current == Patch.Original)
            {
                continue;
            }
            if (Patch.Address == WidescreenHudDigitalAdvanceEntry.Us &&
                Current == WidescreenHudDigitalAdvanceLegacyJump)
            {
                Retire.Replacement = WidescreenHudDigitalAdvanceLegacyJump;
            }
            else if (Current != Patch.Replacement)
            {
                continue;
            }
            CGameHackCodePatcher::Result Result =
                m_CodePatcher.SetEnabled(&Retire, 1, false);
            if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
                Result == CGameHackCodePatcher::Result_MemoryUnavailable)
            {
                return false;
            }
        }
        return true;
    };
    auto IsOwnedFixedWord = [&](const GAME_HACK_CODE_PATCH & Patch, uint32_t Current) {
        return Current == Patch.Replacement;
    };

    // Listings are compared as the ROM in hand holds them, see HudCode; an
    // empty listing (no translation on this build) never matches.
    auto CaveCodeMatches = [this](uint32_t Address, const std::vector<uint32_t> & Code) {
        const size_t Count = Code.size();
        if (Count == 0 ||
            !m_Memory.IsRdramAddress(Address, (uint32_t)(Count * sizeof(uint32_t))))
        {
            return false;
        }
        for (size_t i = 0; i < Count; i++)
        {
            uint32_t Current = 0;
            if (!m_Memory.ReadU32(
                    Address + (uint32_t)(i * sizeof(uint32_t)), Current) ||
                Current != Code[i])
            {
                return false;
            }
        }
        return true;
    };

    auto CaptureCaveImage = [this, &CaveCodeMatches]() {
        const std::vector<uint32_t> RocketOriginal = JfgRocketOverlay::OriginalImage();
        const bool SavedRocket = CaveCodeMatches(JfgRocketOverlay::Start, HudCode(JfgRocketOverlay::Code));
        if (!SavedRocket && !CaveCodeMatches(JfgRocketOverlay::Start, RocketOriginal))
            return false;
        // A fresh runtime may adopt a snapshot containing our raster helpers.
        // Reopening the original diagnostic entry over that saved helper body
        // would be unsafe. Normalize this entire recognizable function to its
        // ROM image, while leaving unrelated captured diagnostic bytes alone.
        const std::vector<uint32_t> FloydDiagnostic = JfgFloydHud::OriginalDiagnosticImage();
        const bool SavedFloydDiagnostic =
            CaveCodeMatches(JfgFloydHud::GuardStub, HudCode(JfgFloydHud::GuardCode)) &&
            CaveCodeMatches(JfgFloydHud::InitStub, HudCode(JfgFloydHud::InitCode)) &&
            CaveCodeMatches(JfgFloydHud::StepStub, HudCode(JfgFloydHud::StepCode)) &&
            CaveCodeMatches(JfgFloydHud::StepTailStub, HudCode(JfgFloydHud::StepTailCode));
        if (!SavedFloydDiagnostic && !CaveCodeMatches(JfgFloydHud::GuardStub, FloydDiagnostic))
        {
            return false;
        }
        m_WidescreenHudCaveOriginal.resize(WidescreenHudCaveWordCount);
        for (size_t i = 0; i < WidescreenHudCaveWordCount; i++)
        {
            if (!m_Memory.ReadU32(
                    WidescreenHudCaveWordAddress(i),
                    m_WidescreenHudCaveOriginal[i]))
            {
                m_WidescreenHudCaveOriginal.clear();
                return false;
            }
            const uint32_t Address = WidescreenHudCaveWordAddress(i);
            if (Address >= JfgRocketOverlay::Start && Address < JfgRocketOverlay::End)
                m_WidescreenHudCaveOriginal[i] = RocketOriginal[
                    (Address - JfgRocketOverlay::Start) / 4];
            if (SavedFloydDiagnostic && Address >= JfgFloydHud::GuardStub &&
                Address < JfgFloydHud::GuardStub + FloydDiagnostic.size() * sizeof(uint32_t))
            {
                m_WidescreenHudCaveOriginal[i] = FloydDiagnostic[
                    (Address - JfgFloydHud::GuardStub) / sizeof(uint32_t)];
            }
        }
        return true;
    };

    auto BuildCaveImage = [this](std::vector<uint32_t> & Image) {
        if (m_WidescreenHudCaveOriginal.size() != WidescreenHudCaveWordCount)
        {
            return false;
        }
        Image = m_WidescreenHudCaveOriginal;

        // The cave is packed solid, and several stubs fill their slot exactly.
        // A stub placed a couple of words too early silently overwrote the tail
        // of its neighbour, including that neighbour's resume jump, and the
        // guest then ran off the end of a stub. Claim every word so an overlap
        // fails the build of the image instead of the emulation.
        std::vector<bool> Claimed(WidescreenHudCaveWordCount, false);
        // Each listing is placed as the ROM in hand needs it, see HudCode. An
        // empty one means a word had no translation: refuse the whole image.
        auto PlaceCode = [&Image, &Claimed](uint32_t Address, const std::vector<uint32_t> & Code) {
            const size_t Count = Code.size();
            if ((Address & 3) != 0 || Count == 0)
            {
                return false;
            }
            size_t Index = 0;
            if (Address >= WidescreenHudCaveStart && Address <= WidescreenHudCaveEnd &&
                Count <= (WidescreenHudCaveEnd - Address) / sizeof(uint32_t))
            {
                Index = (Address - WidescreenHudCaveStart) / sizeof(uint32_t);
            }
            else if (Address >= WidescreenHudReticleStub && Address <= WidescreenHudReticleCaveEnd &&
                     Count <= (WidescreenHudReticleCaveEnd - Address) / sizeof(uint32_t))
            {
                Index = WidescreenHudMainCaveWordCount +
                    (Address - WidescreenHudReticleStub) / sizeof(uint32_t);
            }
            else if (Address == JfgRocketOverlay::Start && Count == sizeof(JfgRocketOverlay::Code) / 4)
            {
                Index = WidescreenHudOldCaveWordCount;
            }
            else
            {
                // The diagnostic logger between the two segments is not ours.
                return false;
            }
            for (size_t i = 0; i < Count; i++)
            {
                if (Claimed[Index + i])
                {
                    return false;
                }
            }
            for (size_t i = 0; i < Count; i++)
            {
                Claimed[Index + i] = true;
                Image[Index + i] = Code[i];
            }
            return true;
        };

        if (!PlaceCode(JfgRocketOverlay::Start, HudCode(JfgRocketOverlay::Code)) ||
            !PlaceCode(WidescreenHudScopeEnterStub, HudCode(WidescreenHudScopeEnterCode)) ||
            !PlaceCode(WidescreenHudScopeExitStub, HudCode(WidescreenHudScopeExitCode)) ||
            !PlaceCode(WidescreenHudCamCopyStub, HudCode(WidescreenHudCamCopyCode)) ||
            !PlaceCode(WidescreenHudFontYStub, HudCode(WidescreenHudFontYCode)) ||
            !PlaceCode(WidescreenHudFontDtdyStub, HudCode(WidescreenHudFontDtdyCode)) ||
            !PlaceCode(WidescreenHudSpriteScaleStub, HudCode(WidescreenHudSpriteScaleCode)) ||
            !PlaceCode(WidescreenHudLineStub, HudCode(WidescreenHudLineCode)) ||
            !PlaceCode(WidescreenHudRectangleStub, HudCode(WidescreenHudRectangleCode)) ||
            !PlaceCode(WidescreenHudSpriteScaleAltStub, HudCode(WidescreenHudSpriteScaleAltCode)) ||
            !PlaceCode(WidescreenHudSpritePositionStub, HudCode(WidescreenHudSpritePositionCode)) ||
            !PlaceCode(WidescreenHudMatrixTranslateStub, HudCode(WidescreenHudMatrixTranslateCode)) ||
            !PlaceCode(WidescreenHudAmmoStub, HudCode(WidescreenHudAmmoCode)) ||
            !PlaceCode(WidescreenHudReticleStub, HudCode(WidescreenHudReticleCode)) ||
            !PlaceCode(WidescreenHudReticleWeaponStub, HudCode(WidescreenHudReticleWeaponCode)) ||
            !PlaceCode(WidescreenHudShotGaugeWrapperStub, HudCode(WidescreenHudShotGaugeWrapperCode)) ||
            !PlaceCode(WidescreenHudShotGaugeAnchorStub, HudCode(WidescreenHudShotGaugeAnchorCode)) ||
            !PlaceCode(WidescreenHudFloydLineStub, HudCode(WidescreenHudFloydLineCode)) ||
            !PlaceCode(JfgFloydHud::GuardStub, HudCode(JfgFloydHud::GuardCode)) ||
            !PlaceCode(JfgFloydHud::InitStub, HudCode(JfgFloydHud::InitCode)) ||
            !PlaceCode(JfgFloydHud::StepStub, HudCode(JfgFloydHud::StepCode)) ||
            !PlaceCode(JfgFloydHud::StepTailStub, HudCode(JfgFloydHud::StepTailCode)))
        {
            return false;
        }

        return true;
    };

    auto ApplyCave = [this, &BuildCaveImage, &CaveCodeMatches](bool Install) {
        std::vector<uint32_t> HookImage;
        if (!BuildCaveImage(HookImage))
        {
            return false;
        }

        const std::vector<uint32_t> SpritePositionLegacy = HudCode(WidescreenHudSpritePositionLegacyCode);
        const std::vector<uint32_t> RectangleLegacy = HudCode(WidescreenHudRectangleLegacyCode);
        const std::vector<uint32_t> ReticleLegacy = HudCode(WidescreenHudReticleLegacyCode);
        const bool LegacySpritePosition = CaveCodeMatches(WidescreenHudSpritePositionStub, SpritePositionLegacy);
        const bool LegacyRectangle = CaveCodeMatches(WidescreenHudRectangleStub, RectangleLegacy);
        const bool LegacyReticle = CaveCodeMatches(WidescreenHudReticleStub, ReticleLegacy);
        std::vector<GAME_HACK_CODE_WRITE> Writes(WidescreenHudCaveWordCount);
        for (size_t i = 0; i < Writes.size(); i++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[i];
            Write.Address = WidescreenHudCaveWordAddress(i);
            Write.Desired = Install ? HookImage[i] : m_WidescreenHudCaveOriginal[i];
            Write.Allowed[0] = m_WidescreenHudCaveOriginal[i];
            Write.AllowedCount = 1;
            // Recognize both the stock rocket exclusion and the rocket-only overlay.
            if (Write.Address == WidescreenHudReticleWeaponStub + 4)
            {
                Write.Allowed[Write.AllowedCount++] = 0x11280116;
                Write.Allowed[Write.AllowedCount++] = 0x112803DB;
            }
            if (HookImage[i] != m_WidescreenHudCaveOriginal[i])
            {
                Write.Allowed[Write.AllowedCount++] = HookImage[i];
            }
            if (LegacySpritePosition && Write.Address >= WidescreenHudSpritePositionStub &&
                Write.Address < WidescreenHudSpritePositionStub + SpritePositionLegacy.size() * sizeof(uint32_t))
            {
                Write.Allowed[Write.AllowedCount++] = SpritePositionLegacy[
                    (Write.Address - WidescreenHudSpritePositionStub) / sizeof(uint32_t)];
            }
            if (LegacyReticle && Write.Address >= WidescreenHudReticleStub &&
                Write.Address < WidescreenHudReticleStub + ReticleLegacy.size() * sizeof(uint32_t))
            {
                Write.Allowed[Write.AllowedCount++] = ReticleLegacy[
                    (Write.Address - WidescreenHudReticleStub) / sizeof(uint32_t)];
            }
            if (LegacyRectangle && Write.Address >= WidescreenHudRectangleStub &&
                Write.Address < WidescreenHudRectangleStub + RectangleLegacy.size() * sizeof(uint32_t))
            {
                Write.Allowed[Write.AllowedCount++] = RectangleLegacy[
                    (Write.Address - WidescreenHudRectangleStub) / sizeof(uint32_t)];
            }
        }

        CGameHackCodePatcher::Result Result = m_CodePatcher.Apply(Writes.data(), Writes.size());
        return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
               Result != CGameHackCodePatcher::Result_MemoryUnavailable;
    };

    // The HUD tables are spelled in US terms and translated for PAL (see
    // JetForceGeminiHudBuild.h); the Kiosk demo has no HUD support. The retired
    // US-only experiments below are only ever looked for on the US build.
    const bool HudRom = IsSupportedRom() && JfgHudBuild::Current() != JfgHudBuild::BuildNone;
    const bool ExactUsRom = HudRom && JfgHudBuild::Current() == JfgHudBuild::BuildUs;
    if (Enabled)
    {
        uint8_t Resolution = 0;
        // Keep the mode check at the mutation boundary too, so another caller
        // cannot accidentally install any part of the patch in 4:3.
        Enabled = HudRom &&
            m_Memory.ReadU8(WidescreenHudResolutionIndexAddress, Resolution) &&
            IsWidescreenHudResolution(Resolution);
    }

    if (ExactUsRom)
    {
        const auto Result = m_CodePatcher.SetEnabled(WidescreenHudReticleRasterRetired,
            sizeof(WidescreenHudReticleRasterRetired) / sizeof(WidescreenHudReticleRasterRetired[0]), false);
        if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
            Result == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
    }

    // Recover known saved hooks before handling disable as well as enable.
    // Otherwise a fresh runtime loading an experimental state in 4:3 (or with
    // the checkbox off) would leave its unguarded banner/gauge edits installed.
    // The saved cave is retained as an inert backup; disconnect all references
    // before restoring it. Unknown ROMs or arbitrary cave writers are not ours.
    const bool HasNoHudOwnership =
        m_WidescreenHudCaveOriginal.empty() &&
        !m_WidescreenHudCaveApplied &&
        !m_WidescreenHudFixedHooksApplied &&
        !m_WidescreenHudOverlayHookApplied &&
        !m_WidescreenHudScopeOwned &&
        m_WidescreenHudOverlayBase == 0 &&
        m_WidescreenReticleOverlayBase == 0;
    if (HudRom && HasNoHudOwnership)
    {
        bool AllFixedHooksInstalled = true;
        for (const GAME_HACK_CODE_PATCH & Patch : LegacyFixedPatches)
        {
            uint32_t Current = 0;
            if (!m_Memory.ReadU32(Patch.Address, Current))
            {
                return false;
            }
            if (!IsOwnedFixedWord(Patch, Current))
            {
                AllFixedHooksInstalled = false;
                break;
            }
        }

        if (AllFixedHooksInstalled)
        {
            // The font Y stub is sixteen words of this runtime's own code at a
            // fixed address: enough to prove the cave belongs to a HUD build
            // and not to some unrelated writer of the diagnostic region.
            const bool CompatibleCave = CaveCodeMatches(
                WidescreenHudFontYStub, HudCode(WidescreenHudFontYCode));
            uint32_t AmmoEntry = 0;
            const bool CompatibleAmmo = m_Memory.ReadU32(WidescreenHudAmmoEntry, AmmoEntry) &&
                (AmmoEntry == WidescreenHudAmmoOriginal ||
                 (AmmoEntry == JumpTo(WidescreenHudAmmoStub) &&
                  CaveCodeMatches(WidescreenHudAmmoStub, HudCode(WidescreenHudAmmoCode))));
            if (!CompatibleCave || !CompatibleAmmo || !CaptureCaveImage())
            {
                return false;
            }
            m_WidescreenHudScopeOriginal = 0;
            m_WidescreenHudScopeOwned = true;
            if (!m_Memory.WriteU8(WidescreenHudScopeDepthAddress, 0))
            {
                return false;
            }
        }
    }

    if (!Enabled)
    {
        // Old experiments could leave just these four constants behind after
        // removing the HUD hooks. They have independent exact-US signatures
        // and must be retired even without any cave ownership.
        const bool DigitalRemoved = !HudRom || RetireDeadDigitalPatches();
        // A disabled or unsupported ROM reaches this path on every runtime
        // tick. Only remove fixed words after ownership has been established:
        // a natural NOP must not be mistaken for a displaced delay slot.
        if (m_WidescreenHudCaveOriginal.empty() &&
            !m_WidescreenHudCaveApplied &&
            !m_WidescreenHudFixedHooksApplied &&
            !m_WidescreenHudOverlayHookApplied &&
            !m_WidescreenHudScopeOwned &&
            m_WidescreenHudOverlayBase == 0 &&
            m_WidescreenReticleOverlayBase == 0)
        {
            // The old implementation could remove every cave reference while
            // leaving translated gauge X values behind. Recognize only the
            // current overlay renderer before repairing that orphaned data.
            uint32_t Table = 0, Base = 0;
            uint32_t Enter = 0, EnterDelay = 0, Exit = 0, ExitDelay = 0;
            if (HudRom &&
                m_Memory.ReadU32(OverlayTableAddress, Table) && (Table & 3) == 0 &&
                m_Memory.IsRdramAddress(Table, (WidescreenHudOverlayModule + 1) * OverlayHeaderSize) &&
                m_Memory.ReadU32(Table + WidescreenHudOverlayModule * OverlayHeaderSize, Base) &&
                Base != 0 && (Base & 3) == 0 &&
                m_Memory.IsRdramAddress(Base, WidescreenHudOverlayExitOffset + 8) &&
                m_Memory.ReadU32(Base + WidescreenHudOverlayEnterOffset, Enter) &&
                m_Memory.ReadU32(Base + WidescreenHudOverlayEnterOffset + 4, EnterDelay) &&
                m_Memory.ReadU32(Base + WidescreenHudOverlayExitOffset, Exit) &&
                m_Memory.ReadU32(Base + WidescreenHudOverlayExitOffset + 4, ExitDelay) &&
                (Enter == WidescreenHudOverlayEnterOriginal || Enter == CallTo(WidescreenHudScopeEnterStub)) &&
                EnterDelay == WidescreenHudOverlayEnterDelayOriginal &&
                (Exit == WidescreenHudOverlayExitOriginal || Exit == JumpTo(WidescreenHudScopeExitStub)) &&
                ExitDelay == WidescreenHudOverlayExitDelayOriginal)
            {
                const bool GaugeRemoved = SetWidescreenHudShotGauge(Base, false);
                const bool FloydRemoved = SetWidescreenHudFloyd(Base, false);
                return GaugeRemoved && FloydRemoved && DigitalRemoved;
            }
            return DigitalRemoved;
        }

        // The banner/gauge and reticle are independent allocations. Attempt
        // both removals even if one signature is unavailable, but keep the
        // caves until every remaining caller has been disconnected.
        const bool HudRemoved = RemoveWidescreenHudOverlayHooks();
        const bool ReticleRemoved = SetWidescreenHudReticle(false);
        if (!HudRemoved || !ReticleRemoved || !DigitalRemoved)
        {
            return false;
        }

        // No guest path can enter the scope now. Clear a possibly interrupted
        // depth before disconnecting the fixed hooks from their cave targets.
        if (m_WidescreenHudScopeOwned &&
            !m_Memory.WriteU8(WidescreenHudScopeDepthAddress, 0))
        {
            return false;
        }

        // Remove only exact references to this cave.
        for (size_t i = FixedPatches.size(); i > 0; i--)
        {
            const GAME_HACK_CODE_PATCH & Patch = FixedPatches[i - 1];
            if (Patch.Address >= JfgFloydHud::GuardStub &&
                Patch.Address < JfgFloydHud::GuardStub + sizeof(JfgFloydHud::GuardCode))
            {
                // Reopen the diagnostic only in the same validated write set
                // as its complete original body. A foreign helper word must
                // never leave the original prologue pointing into our cave.
                continue;
            }
            uint32_t Current = 0;
            if (!m_Memory.ReadU32(Patch.Address, Current))
            {
                return false;
            }
            if (Current == Patch.Replacement)
            {
                CGameHackCodePatcher::Result Result =
                    m_CodePatcher.SetEnabled(&Patch, 1, false);
                if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
                    Result == CGameHackCodePatcher::Result_MemoryUnavailable)
                {
                    return false;
                }
            }
        }
        m_WidescreenHudFixedHooksApplied = false;

        if (!m_WidescreenHudCaveOriginal.empty())
        {
            if (!ApplyCave(false))
            {
                return false;
            }
            m_WidescreenHudCaveOriginal.clear();
        }
        m_WidescreenHudCaveApplied = false;

        if (m_WidescreenHudScopeOwned)
        {
            if (!m_Memory.WriteU8(
                    WidescreenHudScopeDepthAddress,
                    (uint8_t)m_WidescreenHudScopeOriginal))
            {
                return false;
            }
            m_WidescreenHudScopeOwned = false;
            m_WidescreenHudScopeOriginal = 0;
        }
        return true;
    }

    // Every address and instruction below is spelled for the US executable and
    // translated for PAL. Refuse the Kiosk demo, which the wider runtime
    // supports for unrelated features but whose HUD has not been mapped.
    if (!HudRom)
    {
        return false;
    }

    uint32_t AmmoDelay = 0;
    if (!m_Memory.ReadU32(WidescreenHudAmmoEntry + 4, AmmoDelay) ||
        AmmoDelay != WidescreenHudAmmoDelayOriginal)
    {
        return false;
    }

    // Those four words are no longer part of the installation. Retire exact
    // legacy edits before installing this build, and during owned cleanup above.
    if (!RetireDeadDigitalPatches())
    {
        return false;
    }

    // A virgin installation must be atomic from the game's point of view.
    // Validate every fixed call site before capturing or writing the cave: an
    // earlier prototype installed the cave first, then discovered a bad fixed
    // signature and restored the whole diagnostic page on every VI. Besides
    // being needlessly destructive, those repeated invalidations could starve
    // the boot thread. Already-owned replacements remain valid on later ticks.
    for (const GAME_HACK_CODE_PATCH & Patch : FixedPatches)
    {
        uint32_t Current = 0;
        if (!m_Memory.ReadU32(Patch.Address, Current) ||
            (Current != Patch.Original && !IsOwnedFixedWord(Patch, Current)))
        {
            return false;
        }
        if (m_WidescreenHudCaveOriginal.empty() &&
            !m_WidescreenHudCaveApplied &&
            !m_WidescreenHudFixedHooksApplied &&
            Current != Patch.Original)
        {
            // Do not adopt hooks from an unknown or older save state as stock.
            return false;
        }
    }
    if (m_WidescreenHudCaveOriginal.empty())
    {
        if (!CaptureCaveImage())
        {
            return false;
        }
    }

    if (!m_WidescreenHudScopeOwned)
    {
        uint8_t ScopeOriginal = 0;
        if (!m_Memory.ReadU8(WidescreenHudScopeDepthAddress, ScopeOriginal) ||
            ScopeOriginal != 0 ||
            !m_Memory.WriteU8(WidescreenHudScopeDepthAddress, 0))
        {
            m_WidescreenHudCaveOriginal.clear();
            return false;
        }
        m_WidescreenHudScopeOriginal = ScopeOriginal;
        m_WidescreenHudScopeOwned = true;
    }

    if (!m_WidescreenHudCaveApplied)
    {
        if (!ApplyCave(true))
        {
            PatchWidescreenHud(false);
            return false;
        }
        m_WidescreenHudCaveApplied = true;
    }

    CGameHackCodePatcher::Result FixedResult = m_CodePatcher.SetEnabled(
        FixedPatches.data(), FixedPatches.size(), true);
    if (FixedResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        FixedResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        PatchWidescreenHud(false);
        return false;
    }
    m_WidescreenHudFixedHooksApplied = true;

    uint32_t OverlayTable = 0;
    const uint32_t RequiredTableSize =
        (WidescreenHudOverlayModule + 1) * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) ||
        (OverlayTable & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize))
    {
        return true;
    }

    uint32_t OverlayBase = 0;
    const uint32_t OverlayHeader =
        OverlayTable + WidescreenHudOverlayModule * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayHeader, OverlayBase) || OverlayBase == 0)
    {
        if (m_WidescreenHudOverlayBase != 0 &&
            !RemoveWidescreenHudOverlayHooks())
        {
            return false;
        }
        return true;
    }
    if ((OverlayBase & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayBase + WidescreenHudOverlayEnterOffset,
                                 2 * sizeof(uint32_t)) ||
        !m_Memory.IsRdramAddress(OverlayBase + WidescreenHudOverlayExitOffset,
                                 2 * sizeof(uint32_t)))
    {
        return false;
    }

    if (m_WidescreenHudOverlayBase != 0 &&
        m_WidescreenHudOverlayBase != OverlayBase &&
        !RemoveWidescreenHudOverlayHooks())
    {
        return false;
    }

    const uint32_t Enter = OverlayBase + WidescreenHudOverlayEnterOffset;
    const uint32_t Exit = OverlayBase + WidescreenHudOverlayExitOffset;
    const uint32_t EnterReplacement = CallTo(WidescreenHudScopeEnterStub);
    const uint32_t ExitReplacement = JumpTo(WidescreenHudScopeExitStub);
    uint32_t EnterWord = 0;
    uint32_t EnterDelay = 0;
    uint32_t ExitWord = 0;
    uint32_t ExitDelay = 0;
    if (!m_Memory.ReadU32(Enter, EnterWord) ||
        !m_Memory.ReadU32(Enter + 0x04, EnterDelay) ||
        !m_Memory.ReadU32(Exit, ExitWord) ||
        !m_Memory.ReadU32(Exit + 0x04, ExitDelay) ||
        (EnterWord != WidescreenHudOverlayEnterOriginal &&
         EnterWord != EnterReplacement) ||
        EnterDelay != WidescreenHudOverlayEnterDelayOriginal ||
        (ExitWord != WidescreenHudOverlayExitOriginal &&
         ExitWord != ExitReplacement) ||
        ExitDelay != WidescreenHudOverlayExitDelayOriginal)
    {
        return false;
    }

    const GAME_HACK_CODE_PATCH ExitPatch =
    {
        Exit, WidescreenHudOverlayExitOriginal, ExitReplacement,
    };
    const GAME_HACK_CODE_PATCH EnterPatch =
    {
        Enter, WidescreenHudOverlayEnterOriginal, EnterReplacement,
    };
    // Remember the validated allocation before installing any local caller so
    // failure can use the normal ownership-aware cleanup, including any fixed
    // hooks and caves installed earlier in this call.
    m_WidescreenHudOverlayBase = OverlayBase;
    auto RollbackOverlay = [this]() {
        PatchWidescreenHud(false);
        return false;
    };
    // The exit goes live first, followed by the independent HUD callers. Only
    // once the exit can balance the depth counter may the entry redirect into
    // the scoped renderer.
    CGameHackCodePatcher::Result ExitResult =
        m_CodePatcher.SetEnabled(&ExitPatch, 1, true);
    if (ExitResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        ExitResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return RollbackOverlay();
    }
    if (!SetWidescreenHudShotGauge(OverlayBase, true))
    {
        return RollbackOverlay();
    }
    if (!SetWidescreenHudFloyd(OverlayBase, true))
    {
        return RollbackOverlay();
    }
    if (!SetWidescreenHudBanner(OverlayBase, true))
    {
        return RollbackOverlay();
    }
    CGameHackCodePatcher::Result EnterResult =
        m_CodePatcher.SetEnabled(&EnterPatch, 1, true);
    if (EnterResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EnterResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return RollbackOverlay();
    }

    m_WidescreenHudOverlayHookApplied = true;
    m_WidescreenHudOverlayBase = OverlayBase;
    return SetWidescreenHudReticle(true) || RollbackOverlay();
}

void CJetForceGeminiRuntime::PatchHudRaster(bool Enabled, bool TextOnly)
{
    if (!IsSupportedRom() || JfgHudBuild::Current() == JfgHudBuild::BuildNone) return;
    if (!Enabled) JfgMultiplayerHud::Update(m_Memory, m_CodePatcher, false);
    JfgHudRaster::UpdateTitleLogo(m_Memory, m_CodePatcher, Enabled);
    uint32_t table = 0, health = 0, weapon = 0;
    // An unreadable module table is not proof that the old callers are gone.
    if (!m_Memory.ReadU32(OverlayTableAddress, table) || (table & 3) != 0 ||
        !m_Memory.IsRdramAddress(table, 15 * OverlayHeaderSize) ||
        !m_Memory.ReadU32(table + 6 * OverlayHeaderSize, health) ||
        !m_Memory.ReadU32(table + 14 * OverlayHeaderSize, weapon)) return;
    const bool ready = JfgHudRaster::Update(m_Memory, m_CodePatcher, Enabled, health, weapon, TextOnly);
    if (Enabled) JfgMultiplayerHud::Update(m_Memory, m_CodePatcher, ready);
}

// True only when the live (scene, setup) is one the landing stub would act on:
// an entry in the warp table, or a story scene it routes to the front-end. Used
// to keep the stub - which sits in the shared controller-combine path - out of
// ordinary gameplay, where its $t8 clobber would otherwise forge a START press.
bool CJetForceGeminiRuntime::CurrentSceneIsCinematicSkippable(void)
{
    int16_t Scene = 0;
    int16_t Setup = 0;
    if (!m_Memory.ReadS16(CurrentSceneAddress, Scene) ||
        !m_Memory.ReadS16(CurrentSetupAddress, Setup))
    {
        return false;
    }
    const uint16_t SceneId = (uint16_t)Scene;
    const uint8_t SetupId = (uint8_t)(Setup & 0xF);

    // Story scenes the stub sends to mainFrontInit rather than the table.
    if (SceneId >= 0x142 && SceneId <= 0x147)
    {
        return true;
    }
    for (const JFG_CINEMATIC_SKIP_ENTRY & Entry : LandingCinematicSkipTable)
    {
        if (Entry.Scene == SceneId && Entry.Setup == SetupId)
        {
            return true;
        }
    }
    return false;
}

// Reads the object's model name (ObjectHeader + 0x04, NUL-terminated). Returns
// false if the header is unreadable or the name is empty.
bool CJetForceGeminiRuntime::ReadObjectName(uint32_t Object, char * Buffer, size_t Size)
{
    if (Buffer == nullptr || Size == 0)
    {
        return false;
    }
    Buffer[0] = 0;
    uint32_t Header = 0;
    if (!m_Memory.ReadU32(Object + 0x40, Header) || (Header & 3) != 0 ||
        !m_Memory.IsRdramAddress(Header + 0x04, (uint32_t)Size))
    {
        return false;
    }
    size_t i = 0;
    for (; i + 1 < Size; i++)
    {
        uint8_t Character = 0;
        if (!m_Memory.ReadU8(Header + 0x04 + (uint32_t)i, Character) || Character == 0)
        {
            break;
        }
        Buffer[i] = (char)Character;
    }
    Buffer[i] = 0;
    return i > 0;
}

// Some flying Galaxian variants move through a per-baddy mover of their own that
// the objMoveXYZ hook never sees, so they stay full speed in 60 fps mode. Halve their world movement directly, by model name: each frame the game
// has already moved the object from the halved position written last frame, so
// averaging back toward that previous value halves the applied step with no
// drift - whatever the internal mover does, and without touching guest code.
void CJetForceGeminiRuntime::HalveNamedEnemyMovement(void)
{
    static const char * const HalveNames[] = {"OctoGalaxian"};

    uint32_t ObjectList = 0;
    uint32_t ObjectCount = 0;
    if (!m_Memory.ReadU32(WaterWakeObjectListAddress, ObjectList) ||
        !m_Memory.ReadU32(WaterWakeObjectCountAddress, ObjectCount) ||
        ObjectCount > WaterWakeObjectLimit)
    {
        return;
    }

    m_HalveFrameCounter++;
    const size_t SlotCount = sizeof(m_HalvedEnemySlots) / sizeof(m_HalvedEnemySlots[0]);

    for (uint32_t Index = 0; Index < ObjectCount; Index++)
    {
        uint32_t Object = 0;
        if (!m_Memory.ReadU32(ObjectList + Index * sizeof(uint32_t), Object) || Object == 0)
        {
            continue;
        }
        char Name[16];
        if (!ReadObjectName(Object, Name, sizeof(Name)))
        {
            continue;
        }
        bool Match = false;
        for (size_t n = 0; n < sizeof(HalveNames) / sizeof(HalveNames[0]); n++)
        {
            if (strcmp(Name, HalveNames[n]) == 0)
            {
                Match = true;
                break;
            }
        }
        if (!Match)
        {
            continue;
        }

        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        if (!m_Memory.ReadF32(Object + TransformXOffset, X) ||
            !m_Memory.ReadF32(Object + TransformYOffset, Y) ||
            !m_Memory.ReadF32(Object + TransformZOffset, Z))
        {
            continue;
        }

        HALVED_ENEMY_SLOT * Slot = nullptr;
        HALVED_ENEMY_SLOT * Reusable = nullptr;
        for (size_t i = 0; i < SlotCount; i++)
        {
            HALVED_ENEMY_SLOT & Candidate = m_HalvedEnemySlots[i];
            if (Candidate.Object == Object)
            {
                Slot = &Candidate;
                break;
            }
            if (Reusable == nullptr &&
                (Candidate.Object == 0 || m_HalveFrameCounter - Candidate.LastSeenFrame > 4))
            {
                Reusable = &Candidate;
            }
        }

        if (Slot == nullptr)
        {
            // First sighting: adopt the current position, correct from next frame.
            if (Reusable == nullptr)
            {
                continue;
            }
            Slot = Reusable;
            Slot->Object = Object;
            Slot->X = X;
            Slot->Y = Y;
            Slot->Z = Z;
            Slot->LastSeenFrame = m_HalveFrameCounter;
            continue;
        }

        // The game moved from the halved position we left last frame; display the
        // midpoint so only half of this frame's step lands.
        const float CorrectedX = 0.5f * (X + Slot->X);
        const float CorrectedY = 0.5f * (Y + Slot->Y);
        const float CorrectedZ = 0.5f * (Z + Slot->Z);
        m_Memory.WriteF32(Object + TransformXOffset, CorrectedX);
        m_Memory.WriteF32(Object + TransformYOffset, CorrectedY);
        m_Memory.WriteF32(Object + TransformZOffset, CorrectedZ);
        Slot->X = CorrectedX;
        Slot->Y = CorrectedY;
        Slot->Z = CorrectedZ;
        Slot->LastSeenFrame = m_HalveFrameCounter;
    }
}

void CJetForceGeminiRuntime::RemoveLegacyIntroCinematicSkip(void)
{
    uint32_t OverlayTable = 0;
    const uint32_t RequiredTableSize = (IntroCinematicSkipOverlayModule + 1) * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) || (OverlayTable & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize))
    {
        return;
    }

    uint32_t OverlayBase = 0;
    const uint32_t OverlayHeader = OverlayTable + IntroCinematicSkipOverlayModule * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayHeader, OverlayBase) || (OverlayBase & 3) != 0)
    {
        return;
    }

    const uint32_t CurrentEntry = OverlayBase + IntroCinematicSkipEntryOffset;
    if (m_Memory.IsRdramAddress(CurrentEntry, 2 * sizeof(uint32_t)))
    {
        uint32_t Entry = 0;
        uint32_t Delay = 0;
        if (m_Memory.ReadU32(CurrentEntry, Entry) &&
            m_Memory.ReadU32(CurrentEntry + 0x04, Delay) &&
            Entry == IntroCinematicSkipJump && Delay == 0x00000000)
        {
            const GAME_HACK_CODE_PATCH CurrentEntryPatches[] =
            {
                { CurrentEntry, IntroCinematicSkipEntryOriginal, IntroCinematicSkipJump },
                { CurrentEntry + 0x04, IntroCinematicSkipEntryDelayOriginal, 0x00000000 },
            };
            m_CodePatcher.SetEnabled(
                CurrentEntryPatches,
                sizeof(CurrentEntryPatches) / sizeof(CurrentEntryPatches[0]),
                false);
        }
    }

    const uint32_t LegacyEntry = OverlayBase + LegacyIntroCinematicSkipEntryOffset;
    if (m_Memory.IsRdramAddress(LegacyEntry, 2 * sizeof(uint32_t)))
    {
        uint32_t Entry = 0;
        uint32_t Delay = 0;
        if (m_Memory.ReadU32(LegacyEntry, Entry) &&
            m_Memory.ReadU32(LegacyEntry + 0x04, Delay) &&
            (Entry == IntroCinematicSkipJump || Entry == LegacyIntroCinematicSkipCall) &&
            Delay == 0x00000000)
        {
            const GAME_HACK_CODE_PATCH LegacyEntryPatches[] =
            {
                { LegacyEntry, LegacyIntroCinematicSkipEntryOriginal, Entry },
                { LegacyEntry + 0x04, LegacyIntroCinematicSkipEntryDelayOriginal, Delay },
            };
            m_CodePatcher.SetEnabled(
                LegacyEntryPatches,
                sizeof(LegacyEntryPatches) / sizeof(LegacyEntryPatches[0]),
                false);
        }
    }

    const uint32_t LegacyFmvUpdate = OverlayBase + LegacyIntroCinematicSkipFmvUpdateOffset;
    if (m_Memory.IsRdramAddress(LegacyFmvUpdate, sizeof(uint32_t)))
    {
        uint32_t CallWord = 0;
        uint32_t FmvUpdate = 0;
        if (m_Memory.ReadU32(LegacyFmvUpdate, CallWord) &&
            CallWord == LegacyIntroCinematicSkipCall &&
            m_Memory.ReadU32(LegacyIntroCinematicSkipFmvUpdateAddress, FmvUpdate) &&
            m_Memory.IsRdramAddress(FmvUpdate, sizeof(uint32_t)))
        {
            const GAME_HACK_CODE_PATCH LegacyFmvPatch[] =
            {
                { LegacyFmvUpdate, CallTo(FmvUpdate), LegacyIntroCinematicSkipCall },
            };
            m_CodePatcher.SetEnabled(
                LegacyFmvPatch,
                sizeof(LegacyFmvPatch) / sizeof(LegacyFmvPatch[0]),
                false);
            m_Memory.WriteU32(LegacyIntroCinematicSkipFmvUpdateAddress, 0);
        }
    }
}

void CJetForceGeminiRuntime::RemoveLegacyLandingCinematicSkip(void)
{
    uint32_t Entry = 0;
    uint32_t Delay = 0;
    if (!m_Memory.ReadU32(LegacyLandingCinematicSkipEntry, Entry) ||
        !m_Memory.ReadU32(LegacyLandingCinematicSkipEntry + 0x04, Delay) ||
        (Delay != 0x00000000 && Delay != LegacyLandingCinematicSkipEntryDelayOriginal) ||
        (Entry != LegacyLandingCinematicSkipJump && Entry != LegacyLandingCinematicSkipCall))
    {
        return;
    }

    const GAME_HACK_CODE_PATCH LegacyPatches[] =
    {
        { LegacyLandingCinematicSkipEntry, LegacyLandingCinematicSkipEntryOriginal, Entry },
        { LegacyLandingCinematicSkipEntry + 0x04, LegacyLandingCinematicSkipEntryDelayOriginal, Delay },
    };
    m_CodePatcher.SetEnabled(LegacyPatches, sizeof(LegacyPatches) / sizeof(LegacyPatches[0]), false);
}

// diCpuTraceInit contains game code, not a blank scratch page. Capture every
// word the first time we install our dispatcher and restore it before a state
// is saved or the runtime is deactivated. This keeps the code cave reversible
// and avoids assuming that any executable RAM is empty.
bool CJetForceGeminiRuntime::SetObjectMoveHook(bool Enabled)
{
    const size_t PatchCount = sizeof(ObjectMovePatches) / sizeof(ObjectMovePatches[0]);
    const size_t StubCount = PatchCount - 1;
    const GAME_HACK_CODE_PATCH & Entry = ObjectMovePatches[PatchCount - 1];

    auto BuildWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[i];
            Write.Address = ObjectMovePatches[i].Address;
            Write.Desired = Install ? ObjectMovePatches[i].Replacement : m_ObjectMoveStubOriginal[i];
            Write.Allowed[0] = m_ObjectMoveStubOriginal[i];
            Write.Allowed[1] = ObjectMovePatches[i].Replacement;
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(&Entry, 1, false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        if (m_ObjectMoveStubOriginal.empty())
        {
            return true;
        }
        if (m_ObjectMoveStubOriginal.size() != StubCount)
        {
            return false;
        }

        std::vector<GAME_HACK_CODE_WRITE> Writes;
        BuildWrites(Writes, false);
        CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
        if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        m_ObjectMoveStubOriginal.clear();
        return true;
    }

    if (m_ObjectMoveStubOriginal.empty())
    {
        uint32_t EntryWord = 0;
        if (!m_Memory.ReadU32(Entry.Address, EntryWord) || EntryWord != Entry.Original)
        {
            return false;
        }
        m_ObjectMoveStubOriginal.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            if (!m_Memory.ReadU32(ObjectMovePatches[i].Address, m_ObjectMoveStubOriginal[i]))
            {
                m_ObjectMoveStubOriginal.clear();
                return false;
            }
        }
    }
    if (m_ObjectMoveStubOriginal.size() != StubCount)
    {
        return false;
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return false;
    }
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(&Entry, 1, true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        return false;
    }
    return true;
}

// Scales object movement at the final transform step. The player is identified
// through the game-owned player list, leaving sprint as the separate host-side
// position adjustment in ApplySprint.
void CJetForceGeminiRuntime::PatchObjectMove(bool HalveEnemies)
{
    // Floyd has its own, narrowly-scoped objMoveXYZ call-site hook below.
    // Keeping this global hook reserved for the enemy-speed option avoids
    // matching an unrelated object while the drone is active.
    bool Enabled = HalveEnemies;
    uint32_t Entry = 0;
    if (m_Memory.ReadU32(ObjectMoveEntry, Entry))
    {
        // A state saved by the previous enemy-speed hook still points at its
        // smaller stub. Take it down through the patcher so the recompiler sees
        // the code change, then install the expanded dispatcher below.
        if (Entry == ObjectMoveLegacyJump || Entry == ObjectMovePreviousJump)
        {
            const GAME_HACK_CODE_PATCH LegacyEntryPatch = {
                ObjectMoveEntry, Entry, 0x8FA70040};
            CGameHackCodePatcher::Result Result =
                m_CodePatcher.SetEnabled(&LegacyEntryPatch, 1, true);
            if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
                Result == CGameHackCodePatcher::Result_MemoryUnavailable)
            {
                return;
            }
            Entry = 0x8FA70040;
        }
        m_ObjectMovePatchApplied = Entry == ObjectMoveJump;
    }

    if (Enabled == m_ObjectMovePatchApplied)
    {
        return;
    }

    // lwc1 $f6, 0x48($sp) / lwc1 $f12, 0x10($a3), the reads the scaled stack
    // values feed, so the entry really is objMoveXYZ.
    uint32_t Signature[2];
    if (!m_Memory.ReadU32(ObjectMoveResume + 0x00, Signature[0]) ||
        !m_Memory.ReadU32(ObjectMoveResume + 0x04, Signature[1]) ||
        Signature[0] != 0xC7A60048 || Signature[1] != 0xC4EC0010)
    {
        return;
    }

    if (!SetObjectMoveHook(Enabled))
    {
        return;
    }
    m_ObjectMovePatchApplied = Enabled;
}

bool CJetForceGeminiRuntime::PatchSidekickPadControlProbe(bool Enabled)
{
    const size_t StubCount = sizeof(SidekickPadProbeCode) / sizeof(SidekickPadProbeCode[0]);
    auto FindEntry = [this](uint32_t & Entry) {
        Entry = 0;
        uint32_t OverlayTable = 0;
        const uint32_t RequiredTableSize = (FloydOverlayModule + 1) * OverlayHeaderSize;
        if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) || (OverlayTable & 3) != 0 ||
            !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize))
        {
            return false;
        }
        uint32_t OverlayBase = 0;
        const uint32_t OverlayHeader = OverlayTable + FloydOverlayModule * OverlayHeaderSize;
        if (!m_Memory.ReadU32(OverlayHeader, OverlayBase) || (OverlayBase & 3) != 0 ||
            !m_Memory.IsRdramAddress(OverlayBase + FloydPadControlOffset, 2 * sizeof(uint32_t)))
        {
            return false;
        }
        Entry = OverlayBase + FloydPadControlOffset;
        return true;
    };
    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        const uint32_t Resume = m_SidekickPadControlProbeEntry + 0x18;
        const uint32_t ResumeJump = 0x08000000 | ((Resume >> 2) & 0x03FFFFFF);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            uint32_t Desired = ScratchCaveWord(SidekickPadProbeCode[Index]);
            if (Index == SidekickPadProbeResumeIndex)
            {
                Desired = ResumeJump;
            }
            Write.Address = SidekickPadProbeStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? Desired : m_SidekickPadControlProbeStubOriginal[Index];
            Write.Allowed[0] = m_SidekickPadControlProbeStubOriginal[Index];
            Write.Allowed[1] = Desired;
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        if (!m_SidekickPadControlProbeApplied)
        {
            uint32_t Entry = 0;
            uint32_t EntryWord = 0;
            if (FindEntry(Entry) && m_Memory.ReadU32(Entry, EntryWord) && EntryWord == SidekickPadProbeJump)
            {
                const GAME_HACK_CODE_PATCH StalePatches[] =
                {
                    { Entry, SidekickPadProbeEntryOriginal, SidekickPadProbeJump },
                    { Entry + 0x04, SidekickPadProbeSecondOriginal, SidekickPadProbeEntryOriginal },
                };
                CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
                    StalePatches, sizeof(StalePatches) / sizeof(StalePatches[0]), false);
                return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
                       Result != CGameHackCodePatcher::Result_MemoryUnavailable;
            }
            return true;
        }

        const GAME_HACK_CODE_PATCH EntryPatches[] =
        {
            { m_SidekickPadControlProbeEntry, SidekickPadProbeEntryOriginal, SidekickPadProbeJump },
            { m_SidekickPadControlProbeEntry + 0x04, SidekickPadProbeSecondOriginal, SidekickPadProbeEntryOriginal },
        };
        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
            EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable ||
            m_SidekickPadControlProbeStubOriginal.size() != StubCount)
        {
            return false;
        }
        std::vector<GAME_HACK_CODE_WRITE> Writes;
        BuildStubWrites(Writes, false);
        CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
        if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        m_SidekickPadControlProbeStubOriginal.clear();
        m_SidekickPadControlProbeApplied = false;
        m_SidekickPadControlProbeEntry = 0;
        return true;
    }

    uint32_t Entry = 0;
    if (!FindEntry(Entry))
    {
        return false;
    }
    if (m_SidekickPadControlProbeApplied)
    {
        return Entry == m_SidekickPadControlProbeEntry;
    }

    uint32_t EntryWord = 0;
    uint32_t SecondWord = 0;
    if (!m_Memory.ReadU32(Entry, EntryWord) || !m_Memory.ReadU32(Entry + 0x04, SecondWord) ||
        EntryWord != SidekickPadProbeEntryOriginal || SecondWord != SidekickPadProbeSecondOriginal)
    {
        return false;
    }

    m_SidekickPadControlProbeEntry = Entry;
    m_SidekickPadControlProbeStubOriginal.resize(StubCount);
    for (size_t Index = 0; Index < StubCount; Index++)
    {
        if (!m_Memory.ReadU32(
                SidekickPadProbeStub + (uint32_t)(Index * sizeof(uint32_t)),
                m_SidekickPadControlProbeStubOriginal[Index]))
        {
            m_SidekickPadControlProbeStubOriginal.clear();
            m_SidekickPadControlProbeEntry = 0;
            return false;
        }
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_SidekickPadControlProbeStubOriginal.clear();
        m_SidekickPadControlProbeEntry = 0;
        return false;
    }

    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { Entry, SidekickPadProbeEntryOriginal, SidekickPadProbeJump },
        { Entry + 0x04, SidekickPadProbeSecondOriginal, SidekickPadProbeEntryOriginal },
    };
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_SidekickPadControlProbeStubOriginal.clear();
        m_SidekickPadControlProbeEntry = 0;
        return false;
    }

    m_Memory.WriteU32(DroneLateralHookHitsAddress, 0);
    m_Memory.WriteU32(SidekickPadProbeObjectAddress, 0);
    m_Memory.WriteU32(SidekickPadProbeStateAddress, 0);
    m_Memory.WriteU32(SidekickPadProbeActorAddress, 0);
    m_SidekickPadControlProbeApplied = true;
    return true;
}

// Relocate the two flight stubs' US instruction templates for the selected ROM.
// Every $t9-relative load/store belongs to the same reserved scratch region.
uint32_t SidekickFlightHookWord(uint32_t Word)
{
    if (Word == 0x3C19800A)
    {
        return WithHi(Word, DroneLateralMaxSpeedAddress);
    }
    const uint32_t Opcode = Word >> 26;
    if (((Word >> 21) & 31) == 25 &&
        (Opcode == 0x23 || Opcode == 0x2B || Opcode == 0x31 || Opcode == 0x39))
    {
        const uint32_t UsAddress = 0x800A0000 + (int16_t)(Word & 0xFFFF);
        return WithLo(Word, DroneLateralMaxSpeedAddress + (UsAddress - 0x8009FCA4));
    }
    if (Word == 0x08019BC0)
    {
        return JumpTo(SidekickVerticalStub);
    }
    if (Word == 0x0800C01A)
    {
        return SidekickStrafeResumeJump;
    }
    return Word;
}

// Move the original frame-step load into the entry's delay slot. The second
// cave restores $f8 and resumes at entry + 8, outside that delay slot.
bool CJetForceGeminiRuntime::PatchSidekickStrafe(bool Enabled)
{
    const size_t LateralCount = sizeof(SidekickStrafeHookCode) / sizeof(SidekickStrafeHookCode[0]);
    const size_t StubCount = LateralCount + sizeof(SidekickVerticalHookCode) / sizeof(SidekickVerticalHookCode[0]);
    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { SidekickStrafeEntry, SidekickStrafeEntryOriginal, SidekickStrafeJump },
        { SidekickStrafeDelay, SidekickStrafeDelayOriginal, SidekickStrafeEntryOriginal },
    };
    auto StubAddress = [LateralCount](size_t Index) {
        return Index < LateralCount ? SidekickStrafeStub + (uint32_t)(Index * sizeof(uint32_t)) :
                                     SidekickVerticalStub + (uint32_t)((Index - LateralCount) * sizeof(uint32_t));
    };
    auto BuildStubWrites = [this, StubCount, LateralCount, StubAddress](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            const uint32_t Word = SidekickFlightHookWord(Index < LateralCount ? SidekickStrafeHookCode[Index] :
                                                                              SidekickVerticalHookCode[Index - LateralCount]);
            Write.Address = StubAddress(Index);
            Write.Desired = Install ? Word : m_SidekickStrafeHookStubOriginal[Index];
            Write.Allowed[0] = m_SidekickStrafeHookStubOriginal[Index];
            Write.Allowed[1] = Word;
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        if (!m_SidekickStrafeHookApplied)
        {
            // Save states carry RDRAM. Restore a stale entry even when this
            // process has no record of having installed it.
            uint32_t Current = 0;
            if (m_Memory.ReadU32(SidekickStrafeEntry, Current) && Current == SidekickStrafeJump)
            {
                CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
                    EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
                return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
                       Result != CGameHackCodePatcher::Result_MemoryUnavailable;
            }
            return true;
        }

        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
            EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable ||
            m_SidekickStrafeHookStubOriginal.size() != StubCount)
        {
            return false;
        }
        std::vector<GAME_HACK_CODE_WRITE> Writes;
        BuildStubWrites(Writes, false);
        CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
        if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }
        m_SidekickStrafeHookStubOriginal.clear();
        m_SidekickStrafeHookApplied = false;
        return true;
    }

    if (m_SidekickStrafeHookApplied)
    {
        return true;
    }

    uint32_t Entry = 0;
    uint32_t Delay = 0;
    if (!m_Memory.ReadU32(SidekickStrafeEntry, Entry) ||
        !m_Memory.ReadU32(SidekickStrafeDelay, Delay) ||
        Entry != SidekickStrafeEntryOriginal || Delay != SidekickStrafeDelayOriginal)
    {
        return false;
    }
    m_SidekickStrafeHookStubOriginal.resize(StubCount);
    for (size_t Index = 0; Index < StubCount; Index++)
    {
        if (!m_Memory.ReadU32(
                StubAddress(Index),
                m_SidekickStrafeHookStubOriginal[Index]))
        {
            m_SidekickStrafeHookStubOriginal.clear();
            return false;
        }
    }
    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_SidekickStrafeHookStubOriginal.clear();
        return false;
    }
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_SidekickStrafeHookStubOriginal.clear();
        return false;
    }
    m_Memory.WriteU32(DroneLateralHookHitsAddress, 0);
    m_SidekickStrafeHookApplied = true;
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalVelocityAddress, 0.0f);
    return true;
}

// VI_INTR_TIME is (VI_V_SYNC_REG + 1) * viRefreshRate, so doubling the rate
// doubles the instructions the emulated CPU gets between video interrupts and
// lets a busy frame finish inside one VI period. This is an overclock of the
// emulated machine rather than a faithful patch, and it belongs in gameplay
// only: from the ROM database it also applied while the game was booting, when
// VI_V_SYNC_REG is still tiny and the resulting interrupt storm broke start up.
void CJetForceGeminiRuntime::ApplyViBudget(bool Boost)
{
    if (m_BaseViRefreshRate == 0)
    {
        if (g_GameSettings.viRefreshRate == 0)
        {
            return;
        }
        m_BaseViRefreshRate = g_GameSettings.viRefreshRate;
    }

    uint32_t Wanted = Boost ? m_BaseViRefreshRate * 2 : m_BaseViRefreshRate;
    if (g_GameSettings.viRefreshRate != Wanted)
    {
        g_GameSettings.viRefreshRate = Wanted;
    }
}

// Releases the finished graphics task on the first retrace instead of the
// second, see SchedulerReleasePatches. Split from the video sync patch because
// the two have very different reach: this one changes when the game is told the
// RCP is done, which the boot and level load paths may well be relying on.
void CJetForceGeminiRuntime::PatchSchedulerRelease(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(SchedulerFrameGateAdd, Current))
    {
        m_SchedulerReleasePatchApplied = Current == SchedulerReleasePatches[0].Replacement;
    }

    if (!Enabled && !m_SchedulerReleasePatchApplied)
    {
        return;
    }

    // lw $t0, 0x300($s2) / lw $v1, 0x2FC($s2) around the add, then the
    // sltiu $at, $t1, 2 that gates the release.
    uint32_t Signature[3];
    if (!m_Memory.ReadU32(SchedulerSignatureBase + 0x00, Signature[0]) ||
        !m_Memory.ReadU32(SchedulerSignatureBase + 0x04, Signature[1]) ||
        !m_Memory.ReadU32(SchedulerSignatureBase + 0x14, Signature[2]) ||
        Signature[0] != SchedulerSignatureWord0 || Signature[1] != 0x8E4302FC ||
        Signature[2] != SchedulerSignatureWord2)
    {
        return;
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        SchedulerReleasePatches, sizeof(SchedulerReleasePatches) / sizeof(SchedulerReleasePatches[0]), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }
    m_SchedulerReleasePatchApplied = Enabled;
}

// Reports how often the mouse camera actually updates. ProcessController runs
// once per call into the input plugin, so this is the rate the aiming code sees:
// compare it against VI/s to tell whether the camera keeps up with the render.
void CJetForceGeminiRuntime::UpdateInputRate(void)
{
    if (!g_Settings->LoadBool(Setting_JfgShowInputRate))
    {
        m_InputRateWindowValid = false;
        return;
    }

    HighResTimeStamp Now;
    Now.SetToNow();
    if (!m_InputRateWindowValid)
    {
        m_InputRateWindowValid = true;
        m_InputRateWindowStart = Now;
        m_InputRateSamples = 0;
        return;
    }

    m_InputRateSamples += 1;

    uint32_t CurrentScreen = 0;
    if (m_Memory.ReadU32(CurrentScreenAddress, CurrentScreen) && CurrentScreen != m_LastCurrentScreen)
    {
        m_LastCurrentScreen = CurrentScreen;
        m_FrameSwaps += 1;
    }

    uint64_t Elapsed = Now.GetMicroSeconds() - m_InputRateWindowStart.GetMicroSeconds();
    if (Elapsed >= 1000000)
    {
        uint32_t InputRate = (uint32_t)(((uint64_t)m_InputRateSamples * 1000000) / Elapsed);
        uint32_t FrameRate = (uint32_t)(((uint64_t)m_FrameSwaps * 1000000) / Elapsed);
        uint32_t DroneLateralFlags = 0;
        m_Memory.ReadU32(DroneLateralFlagsAddress, DroneLateralFlags);
        // Calibration readout for moving the strafe axis onto the camera. Read
        // these while flying straight with no strafe held: the heading is the
        // camera forward then, which pins down how the yaw maps to X and Z.
        float DroneForwardSpeed = 0.0f;
        float DroneHeadingX = 0.0f;
        m_Memory.ReadF32(DroneLateralForwardSpeedAddress, DroneForwardSpeed);
        m_Memory.ReadF32(DroneLateralVelocityAddress, DroneHeadingX);
        g_Notify->DisplayMessage(
            0,
            stdstr_f("input %u Hz  video %u Hz  floyd%u/%u/state%u patch%u flags%u calls%u"
                     "  fwd%.2f lat%+.2f",
                     InputRate, FrameRate, m_DroneLateralActive ? 1 : 0,
                     m_DroneLateralApplied ? 1 : 0, m_DroneLateralState,
                     m_SidekickStrafeHookApplied ? 1 : 0, DroneLateralFlags, m_DroneLateralHookHits,
                     DroneForwardSpeed, DroneHeadingX)
                .c_str());
        m_InputRateWindowStart = Now;
        m_InputRateSamples = 0;
        m_FrameSwaps = 0;
    }
}

// Lifts the one frame in two cap in viFrameSync, see FramePacing60Patches.
void CJetForceGeminiRuntime::PatchFramePacing60(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(FramePacing60Branch, Current))
    {
        m_FramePacing60PatchApplied = Current == FramePacing60Patches[0].Replacement;
    }

    if (!Enabled && !m_FramePacing60PatchApplied)
    {
        return;
    }

    // lw $t6, -0x1334($t6) / addiu $t7, $zero, 1 around the branch, then the
    // gVideoDeltaTime store the branch is skipping.
    uint32_t Signature[3];
    if (!m_Memory.ReadU32(FramePacing60SignatureBase + 0x00, Signature[0]) ||
        !m_Memory.ReadU32(FramePacing60SignatureBase + 0x04, Signature[1]) ||
        !m_Memory.ReadU32(FramePacing60SignatureBase + 0x10, Signature[2]) ||
        Signature[0] != FramePacing60SignatureWord0 || Signature[1] != 0x240F0001 ||
        Signature[2] != FramePacing60StoreWord)
    {
        return;
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        FramePacing60Patches, sizeof(FramePacing60Patches) / sizeof(FramePacing60Patches[0]), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }

    m_FramePacing60PatchApplied = Enabled;
}

// Keeps the wake's sample ring from saturating at 60fps, see
// WaterWakeRingRatePatches. Only correct while the game runs with a delta of
// one, so it is tied to the 60fps target rather than exposed on its own.
void CJetForceGeminiRuntime::PatchWaterWakeRingRate(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(WaterWakeRingRateEntry, Current))
    {
        m_WaterWakeRingRatePatchApplied = Current == WaterWakeRingRatePatches[0].Replacement;
    }

    if (!Enabled && !m_WaterWakeRingRatePatchApplied)
    {
        return;
    }

    // lh $t2, 8($s4) then beq $t2, $zero, 0x8006ABA0 bracket the first patched
    // nop, and lbu $t3, 0x3b($s4) / lbu $t4, 1($s4) / slt $at, $t3, $t4 are the
    // ring-full test whose branch is being rewritten.
    uint32_t Signature[4];
    if (!m_Memory.ReadU32(WaterWakeRingRateEntry - 0x04, Signature[0]) ||
        !m_Memory.ReadU32(WaterWakeRingRateEntry + 0x04, Signature[1]) ||
        !m_Memory.ReadU32(WaterWakeRingRateEntry + 0x10, Signature[2]) ||
        !m_Memory.ReadU32(WaterWakeRingRateEntry + 0x18, Signature[3]) ||
        Signature[0] != 0x868A0008 || Signature[1] != 0x11400034 ||
        Signature[2] != 0x928C0001 || Signature[3] != 0x016C082A)
    {
        return;
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        WaterWakeRingRatePatches, sizeof(WaterWakeRingRatePatches) / sizeof(WaterWakeRingRatePatches[0]), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }

    m_WaterWakeRingRatePatchApplied = Enabled;
}

void CJetForceGeminiRuntime::Deactivate(void)
{
    // This runs on every controller poll and video interrupt for as long as no
    // JFG source feeds port one, whatever ROM is loaded. Every patch and scratch
    // word below is an address inside a supported ROM: in any other game they
    // are someone else's memory, so only the host-side bookkeeping is reset.
    if (!IsSupportedRom())
    {
        ApplyViBudget(false);
        m_Enabled = false;
        ClearCameraState();
        ClearMovementState();
        return;
    }
    PatchLandingCinematicSkip(false);
    PatchIntroCinematicSkip(false);
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, 0);
    ApplyViBudget(false);
    PatchObjectMove(false);
    PatchSidekickStrafe(false);
    PatchSidekickPadControlProbe(false);
    m_Memory.WriteU32(DroneLateralFlagsAddress, 0);
    m_Memory.WriteF32(DroneLateralSideFactorAddress, 0.0f);
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalThrustAddress, 0.0f);
    m_Memory.WriteF32(DroneVerticalVelocityAddress, 0.0f);
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
    PatchSchedulerRelease(false);
    PatchWaterWakeRingRate(false);
    PatchFramePacing60(false);
    PatchFramePacing(false);
    SetCameraCode(false, false, false, false);
    m_Memory.WriteF32(CameraHeightOffsetAddress, 0.0f);
    for (uint8_t Player = 0; Player < CameraPlayerCount; Player++)
    {
        WriteCameraHeightOffset(Player, 0.0f);
    }
    m_Enabled = false;
    ClearCameraState();
    ClearMovementState();
}

// The host-side sprint and Floyd thruster bookkeeping, dropped whenever the
// guest side is reset: on deactivation and around a state save or load.
void CJetForceGeminiRuntime::ClearMovementState(void)
{
    m_SprintApplied = false;
    m_SprintPositionValid = false;
    m_SprintPlayerObject = 0;
    m_SprintTimeValid = false;
    m_SprintBlend = 0.0f;
    m_SprintAnimationApplied = false;
    m_SprintAnimationValid = false;
    m_SprintAnimation = 0;
    m_DroneLateralApplied = false;
}

void CJetForceGeminiRuntime::ClearCameraState(void)
{
    m_CameraPatchApplied = false;
    memset(m_Orbit, 0, sizeof(m_Orbit));
    // A state or an earlier session may leave a height behind for a player
    // nothing drives any more, and the blend adds whatever it finds there.
    if (IsSupportedRom())
    {
        for (uint8_t Player = 0; Player < CameraPlayerCount; Player++)
        {
            WriteCameraHeightOffset(Player, 0.0f);
        }
    }
    m_AimFovReference = 0.0f;
    m_AimYawCarry = 0.0f;
    m_AimPitchCarry = 0.0f;
    m_BossAimReticleX = 0;
    m_BossAimYawApplied = 0;
    m_TopDownCounterInitialized = false;
    m_TopDownCounter = 0;
    m_TopDownHoldPolls = 0;
    m_QueuedMouseWheel = 0;
    memset(&m_ScrollButtons, 0, sizeof(m_ScrollButtons));
    memset(m_SecondaryScrollButtons, 0, sizeof(m_SecondaryScrollButtons));
    memset(m_SecondaryQueuedScroll, 0, sizeof(m_SecondaryQueuedScroll));
    m_FramePacingPatchApplied = false;
    m_FramePacing60PatchApplied = false;
    m_SchedulerReleasePatchApplied = false;
    m_WaterWakeRingRatePatchApplied = false;
    m_GameplayReady = false;
    m_SprintActive = false;
    m_BaseViRefreshRate = 0;
    m_ObjectMovePatchApplied = false;
    m_LandingCinematicSkipHookApplied = false;
    m_LandingCinematicSkipStubOriginal.clear();
    m_IntroCinematicSkipHookApplied = false;
    m_IntroCinematicSkipOverlayBase = 0;
    m_IntroCinematicSkipStubOriginal.clear();
    m_Fps60ToggleDown = false;
    m_Fps30ToggleDown = false;
    m_SyncAudioEnabledState = -1;
    m_HalveFrameCounter = 0;
    memset(m_HalvedEnemySlots, 0, sizeof(m_HalvedEnemySlots));
    m_InputRateWindowValid = false;
    m_InputRateSamples = 0;
    m_FrameSwaps = 0;
    m_LastCurrentScreen = 0;
}

bool CJetForceGeminiRuntime::IsSupportedRom(void)
{
    // Selecting the table is what makes every address in this file correct for
    // the ROM in hand, so it has to happen before anything reads one. This is
    // the single funnel every entry point already passes through.
    if (!SelectAddressTable())
    {
        return false;
    }

    // Every build with an address table is accepted (US, Kiosk and PAL): every
    // address and every payload now comes from the table, so nothing here is
    // spelled in US terms any more.
    //
    // What makes opening this reasonable rather than reckless is that
    // CGameHackCodePatcher::SetEnabled checks every entry of a table before it
    // writes any of them, and gives up on the first word that is neither the
    // expected original nor an already applied replacement. A mapping mistake
    // therefore leaves the feature switched off instead of corrupting code. The
    // features that save the original words themselves and write unconditionally
    // are the ones without that net, and the ones to distrust first if the Kiosk
    // misbehaves: object movement, player velocity and the sidekick hooks.
    return true;
}

bool CJetForceGeminiRuntime::KeyDown(const KEYBOARD_MOUSE_STATE & Input, KeyboardMouseKey Key)
{
    return (uint32_t)Key < KeyboardMouseKeyCount && (Input.Keys[Key] & 0x80) != 0;
}

bool CJetForceGeminiRuntime::MouseButtonDown(const KEYBOARD_MOUSE_STATE & Input, uint32_t Button)
{
    return Button < KeyboardMouseButtonCount && (Input.MouseButtons[Button] & 0x80) != 0;
}

// StockAim hands the manual aim back to the game while the runtime stays
// installed: the reticle cursor and velocity stores, the angle helpers and the
// overlay aim patches all return to their original words, so the N64 stick
// places the reticle and turns the view exactly as the unmodified game does.
bool CJetForceGeminiRuntime::SetCameraCode(
    bool EnableFreeOrbit, bool EnableManualAim, bool InstallRuntime, bool StockAim)
{
    if (InstallRuntime)
    {
        PatchManualAimCode(!StockAim);
    }

    const size_t CameraPatchCount = sizeof(CameraCodePatches) / sizeof(CameraCodePatches[0]);
    const size_t CursorPatchCount = sizeof(ManualAimCursorPatches) / sizeof(ManualAimCursorPatches[0]);
    const size_t PatchCount = CameraPatchCount + CursorPatchCount;
    GAME_HACK_CODE_WRITE Writes[PatchCount] = {};
    for (size_t i = 0; i < PatchCount; i++)
    {
        const CAMERA_CODE_PATCH & Patch = i < CameraPatchCount
                                              ? CameraCodePatches[i]
                                              : ManualAimCursorPatches[i - CameraPatchCount];
        Writes[i].Address = Patch.Address;
        AddAllowedCodeValue(Writes[i], Patch.Original);
        if (Patch.Replacement != Patch.Original)
        {
            AddAllowedCodeValue(Writes[i], Patch.Replacement);
        }

        if (IsManualAimAnglePatch(Patch.Address))
        {
            AddAllowedCodeValue(Writes[i], 0x00A01025);
        }
        // Builds before the per-player guard window cleared the store itself
        if (Patch.Address == ManualAimCursorXStore)
        {
            AddAllowedCodeValue(Writes[i], ManualAimCursorXStoreClear);
        }
        if (Patch.Address == ManualAimCursorYStore)
        {
            AddAllowedCodeValue(Writes[i], ManualAimCursorYStoreClear);
        }
        uint32_t LegacyInstruction;
        if (GetLegacyCameraHeightInstruction(Patch.Address, LegacyInstruction) &&
            LegacyInstruction != Patch.Original && LegacyInstruction != Patch.Replacement)
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (GetPreviousCameraHeightInstruction(Patch.Address, LegacyInstruction) &&
            LegacyInstruction != Patch.Original && LegacyInstruction != Patch.Replacement)
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (GetLegacyCameraLookHelperInstruction(Patch.Address, LegacyInstruction) &&
            LegacyInstruction != Patch.Original && LegacyInstruction != Patch.Replacement)
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (GetStrictCameraLookHelperInstruction(Patch.Address, LegacyInstruction))
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (GetPlayerObjectCameraLookHelperInstruction(Patch.Address, false, LegacyInstruction))
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (GetPlayerObjectCameraLookHelperInstruction(Patch.Address, true, LegacyInstruction))
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (GetObsoleteCameraPositionBaseInstruction(Patch.Address, LegacyInstruction))
        {
            AddAllowedCodeValue(Writes[i], LegacyInstruction);
        }
        if (!InstallRuntime)
        {
            Writes[i].Desired = Patch.Original;
        }
        else if (IsManualAimAnglePatch(Patch.Address) && EnableManualAim)
        {
            Writes[i].Desired = 0x00A01025;
        }
        else if (IsManualAimVelocityPatch(Patch.Address))
        {
            Writes[i].Desired = Patch.Original;
        }
        else if (IsManualAimStorePatch(Patch.Address) && StockAim)
        {
            Writes[i].Desired = Patch.Original;
        }
        else
        {
            Writes[i].Desired = Patch.FreeCameraPatch && !EnableFreeOrbit ? Patch.Original : Patch.Replacement;
        }
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.Apply(Writes, PatchCount);
    bool Success =
        Result == CGameHackCodePatcher::Result_NoChanges ||
        Result == CGameHackCodePatcher::Result_Changed;
    m_CameraPatchApplied = InstallRuntime && Success;
    if (!InstallRuntime)
    {
        PatchManualAimCode(false);
    }
    return Success;
}

void CJetForceGeminiRuntime::PatchManualAimCode(bool Enabled)
{
    // Both modules are relocatable: take their live base from the overlay
    // table each time, and leave a module alone while it is not loaded.
    auto OverlayBase = [this](uint32_t Module, uint32_t Size, uint32_t & Base) {
        uint32_t Table = 0;
        Base = 0;
        return m_Memory.ReadU32(OverlayTableAddress, Table) && (Table & 3) == 0 &&
               m_Memory.IsRdramAddress(Table, (Module + 1) * OverlayHeaderSize) &&
               m_Memory.ReadU32(Table + Module * OverlayHeaderSize, Base) &&
               Base != 0 && (Base & 3) == 0 && m_Memory.IsRdramAddress(Base, Size);
    };

    uint32_t TargetBase = 0;
    if (Enabled && OverlayBase(TargetOverlayModule, TargetOverlayDrawOffset + 0x10, TargetBase))
    {
        const uint32_t TargetOverlayDraw = TargetBase + TargetOverlayDrawOffset;
        const uint32_t TargetOverlayCursorX = TargetBase + TargetOverlayCursorXOffset;
        const uint32_t TargetOverlayCursorY = TargetBase + TargetOverlayCursorYOffset;
        uint32_t Signature[4];
        uint32_t CursorX;
        uint32_t CursorY;
        if (m_Memory.ReadU32(TargetOverlayDraw + 0x00, Signature[0]) &&
            m_Memory.ReadU32(TargetOverlayDraw + 0x04, Signature[1]) &&
            m_Memory.ReadU32(TargetOverlayDraw + 0x08, Signature[2]) &&
            m_Memory.ReadU32(TargetOverlayDraw + 0x0C, Signature[3]) &&
            Signature[0] == 0x27BDFF68 && Signature[1] == 0xAFB4002C &&
            Signature[2] == 0xAFB30028 && Signature[3] == 0xAFB10020 &&
            m_Memory.ReadU32(TargetOverlayCursorX, CursorX) &&
            m_Memory.ReadU32(TargetOverlayCursorY, CursorY) &&
            (CursorX == 0x44053000 || CursorX == 0x240500A0) &&
            (CursorY == 0x44064000 || CursorY == 0x24060078))
        {
            GAME_HACK_CODE_WRITE OverlayWrites[2] = {};
            OverlayWrites[0].Address = TargetOverlayCursorX;
            OverlayWrites[0].Desired = 0x44053000;
            OverlayWrites[0].Allowed[0] = 0x44053000;
            OverlayWrites[0].Allowed[1] = 0x240500A0;
            OverlayWrites[0].AllowedCount = 2;
            OverlayWrites[1].Address = TargetOverlayCursorY;
            OverlayWrites[1].Desired = 0x44064000;
            OverlayWrites[1].Allowed[0] = 0x44064000;
            OverlayWrites[1].Allowed[1] = 0x24060078;
            OverlayWrites[1].AllowedCount = 2;
            m_CodePatcher.Apply(OverlayWrites, sizeof(OverlayWrites) / sizeof(OverlayWrites[0]));
        }
    }

    uint32_t BoyBase = 0;
    const uint32_t BoyAimEnd =
        (BoyAimFirstGroupOffset > BoyAimSecondGroupOffset ? BoyAimFirstGroupOffset : BoyAimSecondGroupOffset) +
        BoyAimGroupSize;
    if (!OverlayBase(BoyAimOverlayModule, BoyAimEnd, BoyBase))
    {
        return;
    }
    const uint32_t BoyAimHelper = BoyBase + BoyAimHelperOffset;
    uint32_t Signature[4];
    if (!m_Memory.ReadU32(BoyAimHelper + 0x00, Signature[0]) ||
        !m_Memory.ReadU32(BoyAimHelper + 0x04, Signature[1]) ||
        !m_Memory.ReadU32(BoyAimHelper + 0x08, Signature[2]) ||
        !m_Memory.ReadU32(BoyAimHelper + 0x0C, Signature[3]) ||
        Signature[0] != 0xAFA40000 || Signature[1] != 0x00047400 ||
        Signature[2] != 0x000E2403 || Signature[3] != 0xAFA50004)
    {
        return;
    }

    GAME_HACK_CODE_PATCH BoyAimPatches[2 * BoyAimGroupPatchCount];
    const uint32_t Groups[] = { BoyAimFirstGroupOffset, BoyAimSecondGroupOffset };
    for (size_t Group = 0; Group < 2; Group++)
    {
        for (size_t i = 0; i < BoyAimGroupPatchCount; i++)
        {
            const BOY_AIM_PATCH & Patch = BoyAimGroupPatches[i];
            BoyAimPatches[Group * BoyAimGroupPatchCount + i] = {
                BoyBase + Groups[Group] + Patch.Offset, Patch.Original, Patch.Replacement };
        }
    }
    m_CodePatcher.SetEnabled(
        BoyAimPatches, sizeof(BoyAimPatches) / sizeof(BoyAimPatches[0]), Enabled);
}

bool CJetForceGeminiRuntime::GetPlayerData(uint32_t & PlayerObject, uint32_t & PlayerData) const
{
    uint32_t PlayerCount;
    uint32_t PlayerList;
    if (!m_Memory.ReadU32(PlayerCountAddress, PlayerCount) || PlayerCount == 0 || PlayerCount > 4 ||
        !m_Memory.ReadU32(PlayerListAddress, PlayerList) || !m_Memory.IsRdramAddress(PlayerList, 4) ||
        !m_Memory.ReadU32(PlayerList, PlayerObject) ||
        !m_Memory.IsRdramAddress(PlayerObject, ObjectPlayerDataOffset + 4) ||
        !m_Memory.ReadU32(PlayerObject + ObjectPlayerDataOffset, PlayerData) ||
        !m_Memory.IsRdramAddress(PlayerData, PlayerCameraObjectOffset + 4))
    {
        return false;
    }
    return true;
}

// The player object carrying a given index, for the ports beyond the first:
// the list is in join order, so it is walked rather than indexed.
bool CJetForceGeminiRuntime::GetPlayerDataByIndex(
    uint8_t PlayerIndex, uint32_t & PlayerObject, uint32_t & PlayerData) const
{
    uint32_t PlayerCount;
    uint32_t PlayerList;
    if (!m_Memory.ReadU32(PlayerCountAddress, PlayerCount) || PlayerCount == 0 || PlayerCount > 4 ||
        !m_Memory.ReadU32(PlayerListAddress, PlayerList) ||
        !m_Memory.IsRdramAddress(PlayerList, 4 * PlayerCount))
    {
        return false;
    }
    for (uint32_t Slot = 0; Slot < PlayerCount; Slot++)
    {
        uint32_t Object;
        uint32_t Data;
        uint8_t Index;
        if (!m_Memory.ReadU32(PlayerList + 4 * Slot, Object) ||
            !m_Memory.IsRdramAddress(Object, ObjectPlayerDataOffset + 4) ||
            !m_Memory.ReadU32(Object + ObjectPlayerDataOffset, Data) ||
            !m_Memory.IsRdramAddress(Data, PlayerCameraObjectOffset + 4) ||
            !m_Memory.ReadU8(Data + PlayerIndexOffset, Index) || Index != PlayerIndex)
        {
            continue;
        }
        PlayerObject = Object;
        PlayerData = Data;
        return true;
    }
    return false;
}

// Each player's camera is the array slot of its index: controlcam is only the
// one the game processed last, which in split screen is the other player's.
bool CJetForceGeminiRuntime::GetPlayerCamera(uint8_t PlayerIndex, uint32_t & Camera) const
{
    if (PlayerIndex >= CameraCount)
    {
        return false;
    }
    Camera = CameraArrayAddress + PlayerIndex * CameraStructSize;
    return m_Memory.IsRdramAddress(Camera, CameraStructSize);
}

void CJetForceGeminiRuntime::WriteCameraHeightOffset(uint8_t PlayerIndex, float HeightOffset)
{
    if (PlayerIndex < CameraPlayerCount)
    {
        m_Memory.WriteF32(CameraHeightTableAddress + PlayerIndex * 4, HeightOffset);
    }
}

bool CJetForceGeminiRuntime::AnyFreeOrbitWanted(void) const
{
    for (uint32_t Player = 0; Player < CameraPlayerCount; Player++)
    {
        if (m_Orbit[Player].FreeOrbitWanted)
        {
            return true;
        }
    }
    return false;
}

// Drops the override and the tracking, keeping the banked input and the
// height the player had set so a resumed orbit picks them up again.
void CJetForceGeminiRuntime::ResetOrbitCamera(ORBIT_CAMERA_STATE & Orbit)
{
    Orbit.OverrideActive = false;
    Orbit.OverrideSuspended = false;
    Orbit.TrackedCamera = 0;
    Orbit.TrackedPlayerObject = 0;
    Orbit.OrbitYawInitialized = false;
    Orbit.ElevationReady = false;
}

// Turns the drone by winding its heading rather than its reticle. The camera
// yaw the player sees is only ever a render copy the game rewrites each frame
// from the player object's own facing, held negated in two synchronised words.
// Adding the mouse to those makes the drone, and so the camera, turn while the
// stick stays centred and the reticle with it. Wrapping is wanted, so the s16 is
// left to run all the way round.
void CJetForceGeminiRuntime::ApplyDroneCamera(int32_t MouseX, int32_t MouseY)
{
    uint32_t PlayerObject;
    uint32_t PlayerData;
    if (!GetPlayerData(PlayerObject, PlayerData))
    {
        return;
    }

    MouseX = MouseX > 128 ? 128 : (MouseX < -128 ? -128 : MouseX);
    MouseY = MouseY > 128 ? 128 : (MouseY < -128 ? -128 : MouseY);

    // The stored facing is the negative of the yaw on screen, so turning right
    // means winding it down. Mouse up should raise the view the same way it aims
    // on foot, and the invert option flips it for those who prefer the reverse.
    int32_t YawStep = -MouseX * DroneCameraYawSensitivity;
    int32_t PitchStep = (g_Settings->LoadBool(Setting_JfgDroneInvertY) ? MouseY : -MouseY) *
                        DroneCameraPitchSensitivity;

    const uint32_t YawOffsets[] = {DroneYawSourceOffsetA, DroneYawSourceOffsetB};
    const uint32_t PitchOffsets[] = {DronePitchSourceOffsetA, DronePitchSourceOffsetB};
    for (size_t Index = 0; Index < sizeof(YawOffsets) / sizeof(YawOffsets[0]); Index++)
    {
        int16_t Yaw;
        if (MouseX != 0 && m_Memory.ReadS16(PlayerObject + YawOffsets[Index], Yaw))
        {
            m_Memory.WriteS16(PlayerObject + YawOffsets[Index], (int16_t)((int32_t)Yaw + YawStep));
        }
        int16_t Pitch;
        if (MouseY != 0 && m_Memory.ReadS16(PlayerObject + PitchOffsets[Index], Pitch))
        {
            m_Memory.WriteS16(PlayerObject + PitchOffsets[Index],
                              (int16_t)((int32_t)Pitch + PitchStep));
        }
    }
}

// Turns the boss view once the reticle is pinned past the edge of its box, the
// second half of the stock rule described at BossAimReticleEdge. Reticle is the
// unclamped travel, so its surplus past the edge stands in for the stick past
// 0x28 and the turn follows the same quadratic ramp.
void CJetForceGeminiRuntime::ApplyBossAimCameraTurn(uint32_t PlayerObject, int32_t Reticle)
{
    int16_t Facing;
    if (!m_Memory.ReadS16(PlayerObject + ObjectYawOffset, Facing))
    {
        return;
    }

    const int32_t Edge = BossAimReticleEdge + BossAimTurnDeadBand;
    int32_t Surplus = Reticle < 0 ? -Reticle - Edge : Reticle - Edge;
    if (Surplus <= 0)
    {
        // Inside the box, or in the dead band: the view holds still. What has
        // already been turned stays turned, so the surplus decaying away leaves
        // the view where it got to rather than winding it back.
        return;
    }
    if (Surplus > BossAimTurnRange)
    {
        Surplus = BossAimTurnRange;
    }

    // n*(n+1) over twenty steps, the ramp the game keeps at 0x800A2E60, rebuilt
    // here so the rate does not depend on that table still being resident.
    const int32_t Notch = (Surplus * 19) / BossAimTurnRange;
    int32_t Rate = (Notch * (Notch + 1) * BossAimTurnRateMax) / (19 * 20);
    if (Rate <= 0)
    {
        return;
    }
    // A negative reticle offset is the mouse pushed right, and the normal aim
    // path turns right by subtracting from the yaw, so the turn follows the
    // reticle's own sign rather than opposing it.
    Rate = Reticle < 0 ? -Rate : Rate;

    int32_t Applied = m_BossAimYawApplied + Rate;
    Applied = Applied < -BossAimYawLimit ? -BossAimYawLimit : Applied;
    Applied = Applied > BossAimYawLimit ? BossAimYawLimit : Applied;
    const int32_t Step = Applied - m_BossAimYawApplied;
    if (Step == 0)
    {
        return;
    }
    m_BossAimYawApplied = Applied;
    // The object's own facing only. It is what the camera orients from, while
    // PlayerData+0x11C is the heading the strafe rail runs along: writing that
    // one as well swings the rail under the player, which the section must not
    // do. The stock code does write it, but it is driven by the same stick that
    // is also carrying the player along the rail, so the two stay agreed there.
    m_Memory.WriteS16(PlayerObject + ObjectYawOffset, (int16_t)((int32_t)Facing + Step));
}

void CJetForceGeminiRuntime::ApplyManualAimMouse(int32_t MouseX, int32_t MouseY)
{
    uint32_t PlayerObject;
    uint32_t PlayerData;
    uint8_t CameraMode;
    int16_t AimPitch;
    if (!GetPlayerData(PlayerObject, PlayerData) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraModeOffset, CameraMode) ||
        !IsManualAimCameraMode(CameraMode) ||
        !m_Memory.ReadS16(PlayerData + PlayerManualAimPitchOffset, AimPitch))
    {
        return;
    }

    MouseX = MouseX > 128 ? 128 : (MouseX < -128 ? -128 : MouseX);
    MouseY = MouseY > 128 ? 128 : (MouseY < -128 ? -128 : MouseY);

    // Scale the step by how far the view has zoomed in. The reference is the
    // widest field of view seen while aiming, which is the unzoomed one, so at
    // normal aim the scale is one and nothing changes; a sniper narrowing the
    // view shrinks the step in step with it, keeping the on screen movement, and
    // so the finest usable notch, the same at every zoom. The truncated fraction
    // is carried to the next frame so slow drags do not lose it.
    float Fov = 0.0f;
    float Scale = 1.0f;
    if (m_Memory.ReadF32(CameraFovAddress, Fov) && Fov > 0.0f)
    {
        if (Fov > m_AimFovReference)
        {
            m_AimFovReference = Fov;
        }
        if (m_AimFovReference > 0.0f)
        {
            Scale = Fov / m_AimFovReference;
        }
    }

    // Horizontal aim normally turns the player. The boss camera keeps its own
    // reticle, the X of the same pair whose Y already follows the pitch, so the
    // mouse drives that offset instead. It does not stop there in the stock
    // game: controlGetManualAim clamps the stick to +-0x28 to place the reticle,
    // then turns the view once the stick passes +-0x2D with the reticle already
    // pinned at the edge of its box. See BossAimReticleEdge for the whole rule.
    const bool BossCam = CameraMode == PlayerCameraModeBossAim;
    int16_t AimX = 0;
    if (BossCam)
    {
        float Amount = (float)MouseX * MouseAimYawSensitivity * Scale + m_AimYawCarry;
        int32_t Step = (int32_t)Amount;
        m_AimYawCarry = Amount - (float)Step;
        // The screen offset runs opposite the mouse, so moving right lands the
        // reticle right. Overshoot past the edge is kept rather than discarded:
        // it is what the stock game spends on turning the view.
        int32_t Next = m_BossAimReticleX - Step;
        Next = Next < -BossAimReticleTravel ? -BossAimReticleTravel : Next;
        Next = Next > BossAimReticleTravel ? BossAimReticleTravel : Next;
        // Only the part past the edge decays, see BossAimSurplusDecayNumerator.
        // Inside the box the reticle stays exactly where the mouse left it.
        const int32_t Sign = Next < 0 ? -1 : 1;
        const int32_t Magnitude = Next * Sign;
        if (Magnitude > BossAimReticleEdge)
        {
            const int32_t Decayed =
                ((Magnitude - BossAimReticleEdge) * BossAimSurplusDecayNumerator) /
                BossAimSurplusDecayDenominator;
            Next = (BossAimReticleEdge + Decayed) * Sign;
        }
        m_BossAimReticleX = Next;
        AimX = (int16_t)(Next < -BossAimReticleEdge ? -BossAimReticleEdge :
                         (Next > BossAimReticleEdge ? BossAimReticleEdge : Next));
        ApplyBossAimCameraTurn(PlayerObject, Next);
    }
    else
    {
        int16_t AimYaw;
        if (MouseX != 0 && m_Memory.ReadS16(PlayerData + PlayerMovementYawOffset, AimYaw))
        {
            float Amount = (float)MouseX * MouseAimYawSensitivity * Scale + m_AimYawCarry;
            int32_t Step = (int32_t)Amount;
            m_AimYawCarry = Amount - (float)Step;
            AimYaw = (int16_t)((int32_t)AimYaw - Step);
            m_Memory.WriteS16(PlayerData + PlayerMovementYawOffset, AimYaw);
        }
    }

    float PitchAmount = (float)MouseY * MouseAimPitchSensitivity * Scale + m_AimPitchCarry;
    int32_t PitchStep = (int32_t)PitchAmount;
    m_AimPitchCarry = PitchAmount - (float)PitchStep;
    int32_t NextPitch = (int32_t)AimPitch - PitchStep;
    NextPitch = NextPitch < MouseAimPitchMin ? MouseAimPitchMin : NextPitch;
    NextPitch = NextPitch > MouseAimPitchMax ? MouseAimPitchMax : NextPitch;
    AimPitch = (int16_t)NextPitch;
    m_Memory.WriteS16(PlayerData + PlayerManualAimPitchOffset, AimPitch);
    m_Memory.WriteS16(PlayerData + PlayerManualAimX1Offset, AimX);
    m_Memory.WriteS16(PlayerData + PlayerManualAimY1Offset, AimPitch);
    m_Memory.WriteS16(PlayerData + PlayerManualAimX2Offset, AimX);
    m_Memory.WriteS16(PlayerData + PlayerManualAimY2Offset, AimPitch);
    m_Memory.WriteS16(PlayerData + PlayerManualAimVerticalOffset, 0);
    m_Memory.WriteF32(PlayerData + PlayerManualAimXVelocityOffset, 0.0f);
    m_Memory.WriteF32(PlayerData + PlayerManualAimYVelocityOffset, 0.0f);
}

void CJetForceGeminiRuntime::AlignPlayerYawToOrbitCamera(
    const ORBIT_CAMERA_STATE & Orbit, uint32_t PlayerObject, uint32_t PlayerData)
{
    uint8_t PlayerType;
    if (!Orbit.OrbitYawInitialized ||
        !m_Memory.ReadU8(PlayerData + PlayerTypeOffset, PlayerType))
    {
        return;
    }

    int16_t FacingYaw = (int16_t)(0x8000 - (int32_t)Orbit.OrbitYaw);
    m_Memory.WriteS16(PlayerObject + ObjectYawOffset, FacingYaw);
    m_Memory.WriteS16(PlayerData + PlayerMovementYawOffset, FacingYaw);
    if ((PlayerType & 3) == 3)
    {
        m_Memory.WriteS16(PlayerData + PlayerAltCameraBaseYawOffset, FacingYaw);
    }
}

bool CJetForceGeminiRuntime::GetCameraBaseYaw(
    uint32_t PlayerObject, uint32_t PlayerData, int16_t & BaseYaw) const
{
    uint8_t PlayerType;
    int16_t FacingYaw;
    if (!m_Memory.ReadU8(PlayerData + PlayerTypeOffset, PlayerType))
    {
        return false;
    }
    if ((PlayerType & 3) == 3)
    {
        if (!m_Memory.ReadS16(PlayerData + PlayerAltCameraBaseYawOffset, FacingYaw))
        {
            return false;
        }
    }
    else if (!m_Memory.ReadS16(PlayerObject + ObjectYawOffset, FacingYaw))
    {
        return false;
    }
    BaseYaw = (int16_t)(0x8000 - (int32_t)FacingYaw);
    return true;
}

bool CJetForceGeminiRuntime::GetControlCamera(uint32_t & Camera) const
{
    const uint32_t CameraEnd = CameraArrayAddress + CameraStructSize * CameraCount;
    return m_Memory.ReadU32(ControlCameraAddress, Camera) &&
           Camera >= CameraArrayAddress && Camera < CameraEnd &&
           (Camera - CameraArrayAddress) % CameraStructSize == 0;
}

bool CJetForceGeminiRuntime::TopDownCameraWasUpdated(uint32_t Counter)
{
    if (!m_TopDownCounterInitialized)
    {
        m_TopDownCounterInitialized = true;
        m_TopDownCounter = Counter;
        if (Counter != 0)
        {
            m_TopDownHoldPolls = TopDownHoldPolls;
        }
    }
    else if (Counter != m_TopDownCounter)
    {
        m_TopDownCounter = Counter;
        m_TopDownHoldPolls = TopDownHoldPolls;
    }
    else if (m_TopDownHoldPolls > 0)
    {
        m_TopDownHoldPolls--;
    }
    return m_TopDownHoldPolls > 0;
}

// The top-down counter is a global the runtime advances once per video frame,
// from player one's evaluation; the other players read what it concluded.
bool CJetForceGeminiRuntime::GetNormalCameraState(
    uint32_t PlayerData, const ORBIT_CAMERA_STATE & Orbit, bool UpdateTopDown,
    bool & NormalCamera, bool & MouseCameraAllowed)
{
    uint32_t CameraObject;
    uint32_t AnimseqCamera;
    uint32_t LobbyCamera;
    uint32_t StaticCamera;
    uint32_t TopDownCounter;
    uint32_t ActiveCameraOverride;
    uint8_t PlayerIndex;
    uint8_t PlayerType;
    uint8_t CameraMode;
    uint8_t ControlFlags;
    uint8_t TransitionState;
    uint8_t CameraBehavior;
    uint8_t ForcedCamera;
    uint8_t ScriptedCamera;
    uint8_t OverrideCamera;
    int16_t CameraPath;

    if (!m_Memory.ReadU8(PlayerData + PlayerIndexOffset, PlayerIndex) || PlayerIndex >= CameraCount ||
        !m_Memory.ReadU32(PlayerData + PlayerCameraObjectOffset, CameraObject) ||
        !m_Memory.ReadU32(AnimseqCameraAddress, AnimseqCamera) ||
        !m_Memory.ReadU32(LobbyCameraInUseAddress, LobbyCamera) ||
        !m_Memory.ReadU32(StaticCameraInUseAddress, StaticCamera) ||
        !m_Memory.ReadU32(CameraTopDownCounterAddress, TopDownCounter) ||
        !m_Memory.ReadU32(
            CameraActiveOverrideBase + PlayerIndex * CameraActiveOverrideStride, ActiveCameraOverride) ||
        !m_Memory.ReadU8(PlayerData + PlayerTypeOffset, PlayerType) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraModeOffset, CameraMode) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraControlFlagsOffset, ControlFlags) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraTransitionOffset, TransitionState) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraBehaviorOffset, CameraBehavior) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraForceOffset, ForcedCamera) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraScriptOffset, ScriptedCamera) ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraOverrideOffset, OverrideCamera) ||
        !m_Memory.ReadS16(PlayerData + PlayerCameraPathOffset, CameraPath))
    {
        return false;
    }

    const bool TopDownCameraActive =
        UpdateTopDown ? TopDownCameraWasUpdated(TopDownCounter) : m_TopDownHoldPolls > 0;
    const bool PlayerCameraAvailable =
        CameraObject == 0 && AnimseqCamera == 0 && LobbyCamera == 0 && StaticCamera == 0 &&
        ActiveCameraOverride == 0 && (PlayerType & 3) != 3 &&
        ForcedCamera == 0 && ScriptedCamera == 0 && OverrideCamera == 0 && CameraPath < 0 &&
        !TopDownCameraActive;
    const bool StandardOrbitState =
        (ControlFlags & 3) == 0 && TransitionState == 0 && CameraBehavior < 2;
    NormalCamera =
        PlayerCameraAvailable && CameraMode == PlayerCameraModeNormal && StandardOrbitState;
    MouseCameraAllowed =
        PlayerCameraAvailable && (NormalCamera || (Orbit.OverrideActive && !IsManualAimCameraMode(CameraMode)));
    return true;
}

float CJetForceGeminiRuntime::ClampCameraElevation(
    uint8_t PlayerIndex, uint32_t PlayerObject, uint32_t Camera, float HeightOffset) const
{
    HeightOffset = ClampCameraHeight(HeightOffset);
    float CameraX;
    float CameraY;
    float CameraZ;
    float PlayerX;
    float PlayerZ;
    float NativeCameraY;
    int16_t CameraPitch;
    if (!m_Memory.ReadF32(Camera + TransformXOffset, CameraX) ||
        !m_Memory.ReadF32(Camera + TransformYOffset, CameraY) ||
        !m_Memory.ReadF32(Camera + TransformZOffset, CameraZ) ||
        !m_Memory.ReadF32(PlayerObject + TransformXOffset, PlayerX) ||
        !m_Memory.ReadF32(PlayerObject + TransformZOffset, PlayerZ) ||
        PlayerIndex >= CameraPlayerCount ||
        !m_Memory.ReadF32(CameraNativeYTableAddress + PlayerIndex * 4, NativeCameraY) ||
        !m_Memory.ReadS16(Camera + CameraRenderPitchOffset, CameraPitch) ||
        !IsCameraFloat(CameraX) || !IsCameraFloat(CameraY) || !IsCameraFloat(CameraZ) ||
        !IsCameraFloat(PlayerX) || !IsCameraFloat(PlayerZ) || !IsCameraFloat(NativeCameraY))
    {
        return HeightOffset;
    }

    float DeltaX = CameraX - PlayerX;
    float DeltaZ = CameraZ - PlayerZ;
    float HorizontalDistanceSquared = DeltaX * DeltaX + DeltaZ * DeltaZ;
    if (HorizontalDistanceSquared < 1.0f || !IsCameraFloat(HorizontalDistanceSquared))
    {
        return HeightOffset;
    }

    float HorizontalDistance = (float)sqrt((double)HorizontalDistanceSquared);
    float CurrentElevationTangent = (float)tan((double)CameraPitch * N64AngleToRadians);
    if (!IsCameraFloat(CurrentElevationTangent) ||
        CurrentElevationTangent < -16.0f || CurrentElevationTangent > 16.0f)
    {
        return HeightOffset;
    }

    float TargetY = CameraY - CurrentElevationTangent * HorizontalDistance;
    float MinimumOffset =
        TargetY + CameraElevationMinTangent * HorizontalDistance - NativeCameraY;
    float MaximumOffset =
        TargetY + CameraElevationMaxTangent * HorizontalDistance - NativeCameraY;
    MinimumOffset = MinimumOffset > 0.0f ? 0.0f : MinimumOffset;
    MaximumOffset = MaximumOffset < 0.0f ? 0.0f : MaximumOffset;
    HeightOffset = HeightOffset < MinimumOffset ? MinimumOffset : HeightOffset;
    HeightOffset = HeightOffset > MaximumOffset ? MaximumOffset : HeightOffset;
    return ClampCameraHeight(HeightOffset);
}

// Player one's camera pass. Its state checks and writes are the per-player
// core below; what is specific here is the mouse aim and the shared camera
// code, which is one copy for every player and therefore keeps its free orbit
// words in while any driven player wants them. The other players' wants are
// those of the previous frame, their passes running after this one.
bool CJetForceGeminiRuntime::ApplyMouseCamera(int32_t MouseX, int32_t MouseY, bool AimMode, bool StockAim)
{
    ORBIT_CAMERA_STATE & Orbit = m_Orbit[0];
    ORBIT_CAMERA_EVAL Eval;
    EvaluateOrbitCamera(0, Orbit, AimMode, Eval);
    const bool EnableManualAim = Eval.BasicStateAvailable && Eval.JoyDisabled == 0 &&
                                 IsManualAimCameraMode(Eval.CameraMode) &&
                                 (AimMode || Eval.CameraMode == PlayerCameraModeBossAim) && !StockAim;
    Orbit.FreeOrbitWanted = Eval.EnableFreeOrbit;
    SetCameraCode(AnyFreeOrbitWanted(), EnableManualAim, true, StockAim);
    return ApplyOrbitCamera(0, Orbit, Eval, MouseX, MouseY, AimMode);
}

// The state checks that decide whether a player's camera may be orbited this
// frame, for player one from the first entry of the player list and for the
// others from the entry carrying their index.
void CJetForceGeminiRuntime::EvaluateOrbitCamera(
    uint8_t PlayerIndex, const ORBIT_CAMERA_STATE & Orbit, bool AimMode, ORBIT_CAMERA_EVAL & Eval)
{
    memset(&Eval, 0, sizeof(Eval));
    const bool PlayerFound = PlayerIndex == 0 ? GetPlayerData(Eval.PlayerObject, Eval.PlayerData)
                                              : GetPlayerDataByIndex(PlayerIndex, Eval.PlayerObject, Eval.PlayerData);
    Eval.BasicStateAvailable =
        PlayerFound && GetPlayerCamera(PlayerIndex, Eval.Camera) &&
        m_Memory.ReadU32(DisableJoyAddress, Eval.JoyDisabled) &&
        m_Memory.ReadU8(Eval.PlayerData + PlayerCameraModeOffset, Eval.CameraMode);
    Eval.PreserveConstrainedCameras = g_Settings->LoadBool(Setting_JfgPreserveCameraInGameLimits);
    const bool FreeCameraInJump = g_Settings->LoadBool(Setting_JfgFreeCameraInJump);
    Eval.JumpCameraMode = Eval.BasicStateAvailable && Eval.CameraMode == PlayerCameraModeJump;
    Eval.FreeJumpCameraAllowed = Eval.JumpCameraMode && FreeCameraInJump;
    Eval.FreeCameraBlockedByJump = Eval.JumpCameraMode && !FreeCameraInJump;
    Eval.ConstrainedStateAvailable =
        Eval.BasicStateAvailable &&
        GetNormalCameraState(Eval.PlayerData, Orbit, PlayerIndex == 0, Eval.NormalCamera, Eval.MouseCameraAllowed);
    Eval.FreeCameraStateAllowed =
        Eval.JoyDisabled == 0 && !Eval.FreeCameraBlockedByJump &&
        (Eval.FreeJumpCameraAllowed ||
         (Eval.PreserveConstrainedCameras ? (Eval.ConstrainedStateAvailable && Eval.NormalCamera)
                                          : Eval.BasicStateAvailable));
    Eval.EnableFreeOrbit = Eval.FreeCameraStateAllowed && !AimMode;
}

// The orbit itself: spends the banked mouse counts on the player's orbit yaw
// and camera height and writes them where the patched camera code reads them.
// The height word is written on every path, since the blend adds whatever it
// finds there for as long as the code is in for any player: it carries the
// player's height only while the orbit is active and zero otherwise.
bool CJetForceGeminiRuntime::ApplyOrbitCamera(
    uint8_t PlayerIndex, ORBIT_CAMERA_STATE & Orbit, const ORBIT_CAMERA_EVAL & Eval,
    int32_t MouseX, int32_t MouseY, bool AimMode)
{
    const bool CameraInputEnabled = Eval.FreeCameraStateAllowed;
    float HeightToApply = 0.0f;
    bool Result = CameraInputEnabled;

    if (!m_CameraPatchApplied || !Eval.BasicStateAvailable)
    {
        ResetOrbitCamera(Orbit);
    }
    else
    {
        if (Eval.Camera != Orbit.TrackedCamera || Eval.PlayerObject != Orbit.TrackedPlayerObject)
        {
            ResetOrbitCamera(Orbit);
            Orbit.TrackedCamera = Eval.Camera;
            Orbit.TrackedPlayerObject = Eval.PlayerObject;
            Orbit.HeightOffset = 0.0f;
            Orbit.OrbitYaw = 0;
        }

        if (Eval.PreserveConstrainedCameras && !Eval.FreeJumpCameraAllowed &&
            (!Eval.ConstrainedStateAvailable || Eval.JoyDisabled != 0 || !Eval.MouseCameraAllowed))
        {
            Orbit.OverrideActive = false;
            Orbit.OverrideSuspended = false;
            Orbit.OrbitYawInitialized = false;
            Orbit.ElevationReady = false;
            Result = false;
        }
        else if (AimMode || Eval.JoyDisabled != 0 || Eval.FreeCameraBlockedByJump ||
                 (Eval.PreserveConstrainedCameras && !Eval.FreeJumpCameraAllowed &&
                  Eval.CameraMode != PlayerCameraModeNormal))
        {
            if (Orbit.OverrideActive)
            {
                if (AimMode)
                {
                    AlignPlayerYawToOrbitCamera(Orbit, Eval.PlayerObject, Eval.PlayerData);
                }
                m_Memory.WriteS16(Eval.Camera + CameraPitchOffset, 0);
                m_Memory.WriteS16(Eval.PlayerData + PlayerCameraYawOffset, 0);
                Orbit.OverrideActive = false;
                Orbit.OverrideSuspended = true;
                Orbit.OrbitYawInitialized = false;
            }
            Orbit.ElevationReady = false;
        }
        else
        {
            Orbit.OverrideSuspended = false;
            Orbit.OverrideActive = true;
            bool Ready = true;
            if (!Orbit.OrbitYawInitialized)
            {
                Ready = m_Memory.ReadS16(Eval.PlayerData + PlayerCameraOrbitYawOffset, Orbit.OrbitYaw);
                Orbit.OrbitYawInitialized = Ready;
            }

            int16_t CameraBaseYaw = 0;
            if (Ready && GetCameraBaseYaw(Eval.PlayerObject, Eval.PlayerData, CameraBaseYaw))
            {
                Orbit.OrbitYaw = (int16_t)((int64_t)Orbit.OrbitYaw + (int64_t)MouseX * MouseCameraYawSensitivity);
                m_Memory.WriteS16(Eval.PlayerData + PlayerCameraOrbitYawOffset, Orbit.OrbitYaw);
                m_Memory.WriteS16(
                    Eval.PlayerData + PlayerCameraYawOffset,
                    (int16_t)((int32_t)Orbit.OrbitYaw - (int32_t)CameraBaseYaw));
                m_Memory.WriteU8(Eval.PlayerData + PlayerCameraCenterOffset, 0);

                if (Eval.JumpCameraMode)
                {
                    Orbit.HeightOffset = 0.0f;
                    Orbit.ElevationReady = false;
                }
                else
                {
                    Orbit.HeightOffset = ClampCameraHeight(
                        Orbit.HeightOffset + (float)MouseY * MouseCameraHeightSensitivity);
                    if (Orbit.ElevationReady)
                    {
                        Orbit.HeightOffset = ClampCameraElevation(
                            PlayerIndex, Eval.PlayerObject, Eval.Camera, Orbit.HeightOffset);
                    }
                    else
                    {
                        Orbit.ElevationReady = true;
                    }
                    HeightToApply = Orbit.HeightOffset;
                    m_Memory.WriteS16(Eval.Camera + CameraPitchOffset, 0);
                }
            }
        }
    }

    WriteCameraHeightOffset(PlayerIndex, HeightToApply);
    return Result;
}

// The video-frame pass of a port beyond the first: its right stick orbits the
// camera of the player on that port exactly as port one's does, through the
// same state and writes. The shared camera code is installed by port one's
// pass, which runs first each frame and honours FreeOrbitWanted from here, so
// a port with no source has to say so and clear its height word. Mouse travel
// routed to these ports is still ignored, and the trigger's aim being the
// game's own here (see MapSecondaryPort) the stick does not feed the camera
// while it is held.
void CJetForceGeminiRuntime::ProcessSecondaryVideoFrame(int32_t Control, const JFG_PORT_INPUT & Input)
{
    if (Control < 1 || Control >= (int32_t)CameraPlayerCount)
    {
        return;
    }
    ORBIT_CAMERA_STATE & Orbit = m_Orbit[Control];
    const uint8_t PlayerIndex = (uint8_t)Control;

    uint32_t RobotMission = 0;
    const bool Driven = Input.HasSource() && IsSupportedRom() && m_Enabled && m_GameplayReady &&
                        m_CameraPatchApplied &&
                        m_Memory.ReadU32(RobotMissionAddress, RobotMission) && RobotMission == 0;
    if (!Driven)
    {
        ResetOrbitCamera(Orbit);
        Orbit.MouseDeltaX = 0;
        Orbit.MouseDeltaY = 0;
        Orbit.StickCarryX = 0.0f;
        Orbit.StickCarryY = 0.0f;
        Orbit.FreeOrbitWanted = false;
        if (IsSupportedRom())
        {
            WriteCameraHeightOffset(PlayerIndex, 0.0f);
        }
        return;
    }

    JFG_CONTROLS Controls;
    ReadControls(Input, Controls);
    if (Controls.AimPad)
    {
        Orbit.StickCarryX = 0.0f;
        Orbit.StickCarryY = 0.0f;
    }
    else
    {
        BankStickCamera(Orbit, Input);
    }
    const int32_t MouseX = Orbit.MouseDeltaX;
    const int32_t MouseY = Orbit.MouseDeltaY;
    Orbit.MouseDeltaX = 0;
    Orbit.MouseDeltaY = 0;

    ORBIT_CAMERA_EVAL Eval;
    EvaluateOrbitCamera(PlayerIndex, Orbit, Controls.Aim, Eval);
    Orbit.FreeOrbitWanted = Eval.EnableFreeOrbit;
    ApplyOrbitCamera(PlayerIndex, Orbit, Eval, MouseX, MouseY, Controls.Aim);
}

// How hard the stick gets pushed for a frame of mouse travel, clamped to the
// same limit the keyboard uses so the drone never turns faster than the pad
// could ask for.
int8_t DroneStickAxis(int32_t MouseDelta)
{
    int32_t Axis = MouseDelta * DroneStickSensitivity;
    Axis = Axis > JfgStickLimit ? JfgStickLimit : Axis;
    Axis = Axis < -JfgStickLimit ? -JfgStickLimit : Axis;
    return (int8_t)Axis;
}

void CJetForceGeminiRuntime::MapController(
    const JFG_CONTROLS & Controls, BUTTONS & Buttons, bool ApplyCamera)
{
    // The scheme replaces controller one rather than adding to the active
    // plugin mapping. Clear it first so an empty custom-input frame is also a
    // neutral N64 controller state.
    Buttons.Value = 0;

    const bool Forward = Controls.Forward;
    const bool Backward = Controls.Backward;
    const bool Left = Controls.Left;
    const bool Right = Controls.Right;
    const bool CUp = Controls.CUp;
    const bool CDown = Controls.CDown;
    const bool Sprint = Controls.Sprint;
    const bool A = Controls.A;
    const bool B = Controls.B;
    // Weapon cycling uses the same A/B buttons in all on-foot gameplay states.
    // Keep the notches out of drone mode below, where A/B are the throttle.
    const bool ScrollUp = Controls.Scroll > 0;
    const bool ScrollDown = Controls.Scroll < 0;
    const bool Start = Controls.Start;
    const bool Fire = Controls.Fire;
    bool AimMode = Controls.Aim;
    // With the option on, aiming from the trigger leaves the game's own aim
    // in place: the right stick goes to the N64 stick, the reticle travels its
    // box and the view turns once it is pinned at the edge, as the stock game
    // does. The mouse button keeps the mouse scheme even with the trigger held,
    // and the boss section keeps its mouse-driven copy of that same rule.
    const bool StockAim = AimMode && Controls.AimPad && !Controls.AimMouse &&
                          g_Settings->LoadBool(Setting_JfgGamepadStockAim);
    // Read here rather than relying on ApplyMouseCamera having run, since the
    // boss camera path below returns before reaching it.
    uint32_t PlayerObject = 0;
    uint32_t PlayerData = 0;
    uint8_t CameraMode = 0;
    bool PlayerStanding = false;
    if (GetPlayerData(PlayerObject, PlayerData) &&
        m_Memory.ReadU8(PlayerData + PlayerCameraModeOffset, CameraMode))
    {
        PlayerStanding = CameraMode == PlayerCameraModeNormal;
    }
    const bool BossCam = CameraMode == PlayerCameraModeBossAim;
    if (!BossCam)
    {
        // Cleared here rather than in ApplyManualAimMouse, which returns early
        // outside the manual aim modes and so would leave the reticle and the
        // turn already spent to be inherited by the next boss section.
        m_BossAimReticleX = 0;
        m_BossAimYawApplied = 0;
    }

    // Flying the drone leaves the camera mode and the player object exactly as
    // they are on foot, so the mission state is what tells the two apart.
    uint32_t RobotMission = 0;
    m_Memory.ReadU32(RobotMissionAddress, RobotMission);
    const bool DroneMode = RobotMission != 0;

    // The boss section is always an aiming state as far as the game is concerned,
    // so R is not needed and is dropped. Clearing it also keeps the code out of
    // the aim branch below, which would freeze the stick, leaving it free to run
    // the player along the rail while the mouse aims the reticle.
    if (BossCam)
    {
        AimMode = false;
    }

    const bool DroneCameraDirect = DroneMode && g_Settings->LoadBool(Setting_JfgDroneCameraDirect);
    const bool DroneOnStick = DroneMode && !DroneCameraDirect;
    const bool DroneThrusters = DroneMode && g_Settings->LoadBool(Setting_JfgDroneLateralMovement);
    const bool DroneLateralMovement = DroneThrusters && Left != Right;
    const bool DroneVerticalMovement = DroneThrusters && CUp != CDown;
    m_DroneLateralActive = DroneLateralMovement || DroneVerticalMovement;
    uint32_t DroneLateralFlags = DroneThrusters ? DroneLateralActive | (Right ? DroneLateralRight : 0) : 0;
    // The stub stays armed for the whole drone section rather than only while a
    // strafe key is down, so the lateral velocity it holds can decay through
    // drag after the key is released instead of being frozen mid-drift.
    float DroneLateralSideFactor = 0.0f;
    const bool PalRate = JfgAddresses() == &JfgPalAddresses;
    const float SideThrust = PalRate ? PalDroneLateralSideThrust : DroneLateralSideThrust;
    const float DroneVerticalThrust = DroneVerticalMovement ? (CUp ? 1.0f : -1.0f) * SideThrust : 0.0f;
    float DroneLateralRightX = 0.0f;
    float DroneLateralRightZ = 0.0f;
    int16_t DroneCameraYaw = 0;
    if (DroneThrusters &&
        m_Memory.ReadS16(PlayerObject + DroneYawSourceOffsetA, DroneCameraYaw))
    {
        const float Angle = (float)DroneCameraYaw * DroneLateralAngleScale;
        DroneLateralRightX = cosf(Angle);
        DroneLateralRightZ = -sinf(Angle);
        if (DroneLateralMovement)
        {
            DroneLateralSideFactor = (Right ? -1.0f : 1.0f) * SideThrust;
        }
    }
    // The video pass runs independently from the controller poll. Only the
    // latter is consumed by Floyd's game logic; writing here from both passes
    // could clear a held D between the poll and objMoveXYZ.
    if (!ApplyCamera)
    {
        m_Memory.WriteU32(DroneLateralFlagsAddress, DroneLateralFlags);
        m_Memory.WriteF32(DroneLateralSideFactorAddress, DroneLateralSideFactor);
        m_Memory.WriteF32(DroneVerticalThrustAddress, DroneVerticalThrust);
        m_Memory.WriteF32(DroneLateralDragAddress, PalRate ? PalDroneLateralDrag : DroneLateralDrag);
        m_Memory.WriteF32(DroneLateralMaxSpeedAddress, PalRate ? PalDroneLateralMaxSpeed : DroneLateralMaxSpeed);
        m_Memory.WriteF32(DroneLateralRightXAddress, DroneLateralRightX);
        m_Memory.WriteF32(DroneLateralRightZAddress, DroneLateralRightZ);
        if (!DroneThrusters)
        {
            // Do not carry drift into a new mission or an option re-enable.
            m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
            m_Memory.WriteF32(DroneVerticalVelocityAddress, 0.0f);
        }
        if (!m_DroneLateralActive)
        {
            m_Memory.WriteU32(DroneLateralHookHitsAddress, 0);
        }
    }
    m_SprintActive = g_Settings->LoadBool(Setting_JfgEnableSprint) && Sprint && PlayerStanding && !AimMode && !DroneMode &&
                     (Forward || Backward || Left || Right);

    // The banked delta is spent on exactly one of the two passes. Whichever pass
    // does the work has to be the one that spends it: writing memory happens on
    // the video interrupt, while a stick only reaches the game through the poll
    // the game performs itself. Steering the drone with the stick is the one
    // case that needs the button pass.
    const bool SpendMouse = DroneOnStick ? !ApplyCamera : ApplyCamera;
    int32_t MouseX = SpendMouse ? m_Orbit[0].MouseDeltaX : 0;
    int32_t MouseY = SpendMouse ? m_Orbit[0].MouseDeltaY : 0;
    if (SpendMouse)
    {
        m_Orbit[0].MouseDeltaX = 0;
        m_Orbit[0].MouseDeltaY = 0;
    }

    if (ApplyCamera)
    {
        if (DroneMode)
        {
            // The drone owns its own camera, so the on foot orbit has nothing to
            // say here. Dropping the tracked yaw makes it read the game again on
            // the way out rather than snapping to where it left off.
            m_Orbit[0].OrbitYawInitialized = false;

            if (DroneCameraDirect)
            {
                ApplyDroneCamera(MouseX, MouseY);
            }
        }
        else if (BossCam)
        {
            // The game owns the boss camera and draws its own reticle, so aim the
            // reticle on screen and leave the camera to the game.
            ApplyManualAimMouse(MouseX, MouseY);
        }
        else
        {
            ApplyMouseCamera(MouseX, MouseY, AimMode, StockAim);
        }
    }

    const bool DpadActive =
        Controls.DpadUp || Controls.DpadDown || Controls.DpadLeft || Controls.DpadRight;
    bool InputActive =
        Forward || Backward || Left || Right || CUp || CDown || Controls.CLeft || Controls.CRight ||
        A || B || Start || Fire || Controls.Aim || MouseX != 0 || MouseY != 0 ||
        Controls.Scroll != 0 || Controls.StickX != 0 || Controls.StickY != 0 || DpadActive;
    if (!InputActive)
    {
        return;
    }

    // The game leaves the D pad alone on foot, so it passes straight through
    Buttons.U_DPAD = Controls.DpadUp;
    Buttons.D_DPAD = Controls.DpadDown;
    Buttons.L_DPAD = Controls.DpadLeft;
    Buttons.R_DPAD = Controls.DpadRight;

    // The drone flies with the stick steering its reticle and the camera turning
    // to follow, so the mouse pushes the stick rather than writing an angle, and
    // A and B become the throttle.
    if (DroneMode)
    {
        Buttons.Z_TRIG = Fire;
        Buttons.START_BUTTON = Start;
        // Strafing deliberately does not request the throttle. It did while the
        // hack rotated the velocity the engine produced, since there had to be
        // one to rotate, but the lateral thruster now carries its own velocity
        // and asking for A on top only adds forward thrust.
        Buttons.A_BUTTON = Forward || A;
        Buttons.B_BUTTON = Backward || B;
        Buttons.L_CBUTTON = false;
        Buttons.R_CBUTTON = false;
        // Jump/crouch feed the vertical thruster above, not native C buttons.
        Buttons.U_CBUTTON = false;
        Buttons.D_CBUTTON = false;
        // Leaving the stick at rest is what keeps the reticle centred, since the
        // game walks it back to the middle by itself once nothing is pushing it.
        if (!DroneCameraDirect)
        {
            Buttons.X_AXIS = DroneStickAxis(MouseX);
            Buttons.Y_AXIS =
                DroneStickAxis(g_Settings->LoadBool(Setting_JfgDroneInvertY) ? MouseY : -MouseY);
        }
        return;
    }

    Buttons.Z_TRIG = Fire;
    Buttons.R_TRIG = AimMode;
    Buttons.A_BUTTON = A || ScrollDown;
    Buttons.B_BUTTON = B || ScrollUp;
    Buttons.START_BUTTON = Start;
    // The mouse takes the stick for aiming, so lateral movement moves to the C
    // buttons instead. The boss section runs the player along its rail on those
    // same C buttons in the unmodified game, so Q and D map there too while the
    // mouse aims the reticle.
    // Crouching always shuffles sideways on the C buttons. While prone, the
    // setting explicitly routes Q and D to those same C buttons; otherwise the
    // game receives its own stick movement while the player is lying down.
    bool PostureStrafe =
        CameraMode == PlayerCameraModeCrouch ||
        (CameraMode == PlayerCameraModeProne &&
         g_Settings->LoadBool(Setting_JfgCrouchProneStickStrafe));
    bool StrafeOnCButtons = AimMode || BossCam || PostureStrafe;
    // The pad's shoulder buttons sidestep on those same C buttons in any state
    Buttons.L_CBUTTON = Controls.CLeft || (Left && StrafeOnCButtons);
    Buttons.R_CBUTTON = Controls.CRight || (Right && StrafeOnCButtons);

    if (AimMode)
    {
        if (StockAim)
        {
            // controlGetManualAim pins the reticle at half deflection and
            // starts turning just past it, so the stick goes through as it is.
            // Its Y runs the other way from the camera's, so the stick is not
            // flipped into N64 up-positive here: pushing up moves the reticle
            // up. The camera bank outside the aim keeps its own sign.
            Buttons.X_AXIS = StickToN64(Controls.CameraX);
            Buttons.Y_AXIS = StickToN64(Controls.CameraY);
        }
        else
        {
            if (ApplyCamera)
            {
                ApplyManualAimMouse(MouseX, MouseY);
            }
            Buttons.X_AXIS = 0;
            Buttons.Y_AXIS = 0;
        }
        Buttons.U_CBUTTON = Forward || CUp;
        Buttons.D_CBUTTON = Backward || CDown;
        return;
    }

    Buttons.U_CBUTTON = CUp;
    Buttons.D_CBUTTON = CDown;
    Buttons.Y_AXIS = Controls.StickY;
    if (!StrafeOnCButtons)
    {
        Buttons.X_AXIS = Controls.StickX;
    }
}

// Blend in real time rather than by a fixed amount per VI: this keeps the
// half-second sprint transition identical at 30 and 60fps. A large elapsed
// time is capped so unpausing the emulator cannot complete the transition in a
// single frame.
void CJetForceGeminiRuntime::UpdateSprintBlend(void)
{
    if (!g_Settings->LoadBool(Setting_JfgEnableSprint))
    {
        m_SprintActive = false;
        m_SprintBlend = 0.0f;
        m_SprintTimeValid = false;
        return;
    }

    HighResTimeStamp Now;
    Now.SetToNow();
    if (!m_SprintTimeValid)
    {
        m_SprintLastUpdate = Now;
        m_SprintTimeValid = true;
        return;
    }

    uint64_t NowMicroseconds = Now.GetMicroSeconds();
    uint64_t LastMicroseconds = m_SprintLastUpdate.GetMicroSeconds();
    m_SprintLastUpdate = Now;
    if (NowMicroseconds < LastMicroseconds)
    {
        return;
    }

    uint64_t ElapsedMicroseconds = NowMicroseconds - LastMicroseconds;
    if (ElapsedMicroseconds > SprintMaximumElapsedMicroseconds)
    {
        ElapsedMicroseconds = SprintMaximumElapsedMicroseconds;
    }

    float BlendStep = (float)ElapsedMicroseconds / (float)SprintRampMicroseconds;
    if (m_SprintActive)
    {
        m_SprintBlend += BlendStep;
        if (m_SprintBlend > 1.0f)
        {
            m_SprintBlend = 1.0f;
        }
    }
    else
    {
        m_SprintBlend -= BlendStep;
        if (m_SprintBlend < 0.0f)
        {
            m_SprintBlend = 0.0f;
        }
    }
}

// The retail player controller does not use the generic velocity helper that
// enemies use, so there is no single speed value to patch safely. Instead,
// observe the player object's actual horizontal movement each video interrupt
// and add the missing half only while sprinting. The position cache keeps this
// independent of the game's own acceleration, collision and slopes.
void CJetForceGeminiRuntime::ApplySprint(uint32_t PlayerObject)
{
    const float SprintMovementExtraMultiplier = m_SprintBlend * (SprintMovementMultiplier - 1.0f);
    float PositionX = 0.0f;
    float PositionZ = 0.0f;
    if (!m_Memory.ReadF32(PlayerObject + TransformXOffset, PositionX) ||
        !m_Memory.ReadF32(PlayerObject + TransformZOffset, PositionZ) ||
        !IsCameraFloat(PositionX) || !IsCameraFloat(PositionZ))
    {
        m_SprintApplied = false;
        m_SprintPositionValid = false;
        return;
    }

    if (!m_SprintPositionValid || m_SprintPlayerObject != PlayerObject)
    {
        m_SprintPositionValid = true;
        m_SprintPlayerObject = PlayerObject;
        m_SprintPreviousX = PositionX;
        m_SprintPreviousZ = PositionZ;
        m_SprintApplied = false;
        return;
    }

    float DeltaX = PositionX - m_SprintPreviousX;
    float DeltaZ = PositionZ - m_SprintPreviousZ;
    m_SprintApplied = false;
    if (SprintMovementExtraMultiplier > 0.0f && fabs(DeltaX) <= SprintMaximumStep &&
        fabs(DeltaZ) <= SprintMaximumStep)
    {
        float SprintX = PositionX + DeltaX * SprintMovementExtraMultiplier;
        float SprintZ = PositionZ + DeltaZ * SprintMovementExtraMultiplier;
        if (IsCameraFloat(SprintX) && IsCameraFloat(SprintZ) &&
            m_Memory.WriteF32(PlayerObject + TransformXOffset, SprintX) &&
            m_Memory.WriteF32(PlayerObject + TransformZOffset, SprintZ))
        {
            PositionX = SprintX;
            PositionZ = SprintZ;
            m_SprintApplied = DeltaX != 0.0f || DeltaZ != 0.0f;
        }
    }

    m_SprintPreviousX = PositionX;
    m_SprintPreviousZ = PositionZ;

    // objAnimDframe keeps the active animation cursor in the selected entry of
    // the player object's animation list. Walking animations are looped, so a
    // separate, gentler animation multiplier lets the run feel faster without
    // making the footstep events occur at the full movement-speed multiplier.
    uint8_t AnimationIndex = 0;
    uint32_t AnimationList = 0;
    uint32_t Animation = 0;
    uint8_t AnimationLoops = 0;
    float AnimationFrame = 0.0f;
    if (!m_Memory.ReadU8(PlayerObject + PlayerAnimationIndexOffset, AnimationIndex) ||
        AnimationIndex >= 16 || !m_Memory.ReadU32(PlayerObject + PlayerAnimationListOffset, AnimationList) ||
        !m_Memory.IsRdramAddress(AnimationList + AnimationIndex * sizeof(uint32_t), sizeof(uint32_t)) ||
        !m_Memory.ReadU32(AnimationList + AnimationIndex * sizeof(uint32_t), Animation) ||
        !m_Memory.ReadU8(Animation + AnimationLoopFlagOffset, AnimationLoops) || AnimationLoops == 0 ||
        !m_Memory.ReadF32(PlayerObject + AnimationFrameOffset, AnimationFrame) || AnimationFrame < 0.0f ||
        AnimationFrame > 1.0f)
    {
        m_SprintAnimationApplied = false;
        m_SprintAnimationValid = false;
        return;
    }

    if (!m_SprintAnimationValid || m_SprintAnimation != Animation)
    {
        m_SprintAnimationValid = true;
        m_SprintAnimation = Animation;
        m_SprintPreviousAnimationFrame = AnimationFrame;
        m_SprintAnimationApplied = false;
        return;
    }

    float AnimationDelta = AnimationFrame - m_SprintPreviousAnimationFrame;
    if (AnimationDelta < -0.5f)
    {
        AnimationDelta += 1.0f;
    }
    else if (AnimationDelta > 0.5f)
    {
        AnimationDelta -= 1.0f;
    }

    m_SprintAnimationApplied = false;
    const float SprintAnimationExtraMultiplier = m_SprintBlend * (SprintAnimationMultiplier - 1.0f);
    if (SprintAnimationExtraMultiplier > 0.0f && fabs(AnimationDelta) <= 0.5f)
    {
        float SprintAnimationFrame = AnimationFrame + AnimationDelta * SprintAnimationExtraMultiplier;
        if (SprintAnimationFrame >= 1.0f)
        {
            SprintAnimationFrame -= 1.0f;
        }
        else if (SprintAnimationFrame < 0.0f)
        {
            SprintAnimationFrame += 1.0f;
        }

        if (m_Memory.WriteF32(PlayerObject + AnimationFrameOffset, SprintAnimationFrame))
        {
            AnimationFrame = SprintAnimationFrame;
            m_SprintAnimationApplied = AnimationDelta != 0.0f;
        }
    }
    m_SprintPreviousAnimationFrame = AnimationFrame;
}
