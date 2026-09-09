#include "stdafx.h"

#include "JetForceGemini.h"
#include "JetForceGeminiAddresses.h"
#include "JetForceGeminiHudAlignment.h"
#include <Common/DateTime.h>
#include <Common/path.h>
#include <math.h>
#include <algorithm>
#include <cstdio>

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
const KeyboardMouseKey CinematicProbeKey = (KeyboardMouseKey)19; // USB HID P
const bool CinematicProbeEnabled = false; // Kept for internal diagnostics, disabled in public builds.
// Diagnostic only: hold the instrument scope open so every scope-gated HUD hook
// behaves like the font hook, which is resolution-gated and therefore global.
// The ammunition counter ignores all of them while the weapon frame beside it
// compresses, which is what a draw outside the +0xC00/+0x10D0 window would look
// like. This tells those two cases apart without hooking another renderer.
const KeyboardMouseKey WidescreenHudScopeForceKey = (KeyboardMouseKey)18; // USB HID O
// Live 30/60 FPS switch for testing: numpad + selects 60, numpad - selects 30.
// Both flip Setting_JfgTarget60Fps, which ProcessRuntimeFrame re-reads and
// re-applies every frame, so the mode changes on the spot.
const KeyboardMouseKey Fps60ToggleKey = (KeyboardMouseKey)87; // USB HID Keypad +
const KeyboardMouseKey Fps30ToggleKey = (KeyboardMouseKey)86; // USB HID Keypad -
const char * CinematicProbeLogFileName = "JfgCinematicProbe.log";

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
const uint32_t TargetOverlayCursorX = 0x8034853C;
const uint32_t TargetOverlayCursorY = 0x80348564;
const uint32_t TargetOverlayDraw = 0x803485C8;
const uint32_t BoyAimHelper = 0x8035C3A0;
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

// JFG only requests a third framebuffer for widescreen modes. At 60fps the
// scheduler releases each graphics task on the next retrace, so the normal two
// buffers can be reused while dynamic effects are still being consumed. Force
// the engine's existing triple-buffer request before it allocates the level
// framebuffers.
uint32_t TripleBufferRequest = 0x800551E8;
uint32_t TripleBufferActive = 0x800FECA6;

// The wake’s element lifetimes already use the game's delta time. Its two
// opacity envelopes, however, advance by fixed ±32/±64 steps per update. They
// therefore complete twice as fast at 60fps and leave the wake invisible.
// Halving just those fixed steps retains every game update and its timing.
// objObjectsTick calls wakeUpdateRipple once per logical game frame. At 30fps
// that means thirty calls with a delta of two; at 60fps it becomes sixty calls
// with a delta of one. The wake mixes delta-scaled motion with per-call state,
// so neither rate produces the same result. Its small local gate reproduces
// the original thirty calls with a delta of two while the rest of the game
// stays at 60fps.
uint32_t WaterWakeUpdate = 0x8006B090;
uint32_t WaterWakeUpdateCall = 0x0C000000 | ((WaterWakeUpdate >> 2) & 0x03FFFFFF);
uint32_t WaterWakeGateStub = 0x8009FD00;
uint32_t WaterWakeGateJump = 0x0C000000 | ((WaterWakeGateStub >> 2) & 0x03FFFFFF);
uint32_t WaterWakeGateCounter = 0x8009FCEC;
uint32_t WaterWakeLegacyCallSite = 0x80009278;
uint32_t WaterWakeObjectListAddress = 0x800F2CA4;
uint32_t WaterWakeObjectCountAddress = 0x800F2CA8;
const uint32_t WaterWakeObjectLimit = 1024;
uint32_t GeneralRenderListAddress = 0x800F2FB0;
const uint32_t GeneralRenderListLimit = 134;
uint32_t WaterWakeGlobalFadeAddress = 0x800A6950;

// At 60fps, a live wake object no longer passes the general-render-list cull
// (and therefore wakeDraw never sees it). Keep the stock cull for every other
// object, but include objects whose +0x58 payload is a wake block (marker 0x40).
uint32_t WaterWakeCullingEntry = 0x800146E4;
uint32_t WaterWakeCullingStub = WaterWakeGateStub;
uint32_t WaterWakeCullingJump = 0x08027F40; // j WaterWakeCullingStub

// The fallback hooks the point immediately after the stock wake pass. It draws
// the one active wake selected by the host, then resumes the stock renderer.
uint32_t WaterWakeDrawTargetAddress = WaterWakeGateCounter;
uint32_t WaterWakeDrawFallbackEntry = 0x80014CA0;
uint32_t WaterWakeDrawFallbackStub = 0x8009FD00;
uint32_t WaterWakeDrawFallbackJump = 0x0C027F40; // jal WaterWakeDrawFallbackStub
uint32_t WaterWakeDrawFallbackCalledAddress = 0x8009FCF0;

// wakeUpdateRipple advances this ring-buffer index once per call, not once per
// delta. At 60fps that reuses the vertex frame twice as quickly as the 30fps
// renderer. Preserve its original decrement on alternate frames only.
uint32_t WaterWakeFrameRateEntry = 0x8006B1A0;
uint32_t WaterWakeFrameRateStub = WaterWakeGateStub;
uint32_t WaterWakeFrameRateJump = 0x08027F40; // j WaterWakeFrameRateStub

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

uint32_t WaterWakeStockDrawEntry = 0x80014C44;
uint32_t WaterWakeStockDrawStub = WaterWakeGateStub;
uint32_t WaterWakeStockDrawJump = 0x0C027F40; // jal WaterWakeStockDrawStub
uint32_t WaterWakeStockDrawTargetAddress = 0x8009FCF4;
uint32_t WaterWakeStockDrawCalledAddress = 0x8009FCF8;

// Enemy movement has two common paths. objMoveXYZ below catches movers that add
// a step directly to their transform. Most regular enemies instead run through
// SquaddieControl in module 3, whose shared heading helper writes X/Z velocity.
// Module 3 is a relocatable overlay, so its hook addresses are resolved from the
// live overlay table rather than treated as fixed RAM addresses.

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
// Hook the function epilogue: the original routine has already calculated the
// player's velocity vector when this code runs.
uint32_t PlayerVelocityEntry = 0x800341A4;
// 0x8009FDxx is reused by RSP microcode. Keep the hook next to the proven
// objMoveXYZ code cave instead, after its 0x64-byte dispatcher.
uint32_t PlayerVelocityStub = 0x80066C00;
uint32_t PlayerVelocityJump = 0x08019B00; // j PlayerVelocityStub
uint32_t DroneLateralFlagsAddress = 0x8009FCE4;
uint32_t DroneLateralHookHitsAddress = 0x8009FCE8;
uint32_t DroneLateralHookFlagsAddress = 0x8009FCEC;
uint32_t EnemyHalveFlagAddress = 0x8009FCE0;
uint32_t DroneLateralPreviousXAddress = 0x8009FCD0;
uint32_t DroneLateralPreviousZAddress = 0x8009FCD4;
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
// Also the cap on the forward and lateral speeds combined, matching the value
// the engine sets for its own forward axis at 0x8003045C.
const float DroneLateralMaxSpeed = 8.0f;

// The first word of each 0x20-byte overlay table entry is its relocated RAM
// base. SquaddieControl is module 3; the shared heading helper starts at AD38
// and writes its horizontal velocity at AE38/AE3C and AE68/AE6C.
uint32_t OverlayTableAddress = 0x800FEAA0;
const uint32_t FrontModeAddress = 0x800A51B0;
const uint32_t CurrentSceneAddress = 0x800A323C;
const uint32_t CurrentSetupAddress = 0x800A3248;
const uint32_t NextLevelAddress = 0x800A3250;
const uint32_t NextSetupAddress = 0x800A3258;
const uint32_t NextCharacterAddress = 0x800A3260;
const uint32_t NextFrontModeAddress = 0x800A3264;
const uint32_t LoadingAddress = 0x800A3294;
const uint32_t OverlayHeaderSize = 0x20;
const uint32_t SquaddieOverlayModule = 3;
const uint32_t FloydOverlayModule = 22;
const uint32_t FloydMovePlayerOffset = 0x2F38;
const uint32_t FloydMovePlayerSize = 0x218;
const uint32_t FloydPadControlOffset = 0x2A4;
uint32_t FloydMoveHookStub = 0x80066C00;
uint32_t FloydMoveHookJump = 0x08019B00;
const uint32_t FloydMoveObjMoveCallOffset = 0x200;
uint32_t FloydMoveObjMoveCallOriginal = 0x0C002681; // jal objMoveXYZ
const uint32_t FloydMoveObjMoveDelayOriginal = 0x02002025; // or a0, s0, zero
// The trampoline calls objMoveXYZ at word 24, keeps its native delay slot at
// word 25, then jumps back to the instruction after the displaced call.
const uint32_t FloydMoveCallHookResumeIndex = 26;
const uint32_t FloydCameraLateralCallOffset = 0x2E80;
const uint32_t FloydCameraLateralCallOriginal = 0x0C012721;
const uint32_t FloydCameraLateralCallDelayOriginal = 0xE6040014;
uint32_t FloydCameraLateralStub = 0x80066F00;
uint32_t FloydCameraLateralJump = 0x0C019BC0; // jal FloydCameraLateralStub
uint32_t FloydCameraPreviousXAddress = 0x8009FCB0;
uint32_t FloydCameraPreviousZAddress = 0x8009FCB4;
uint32_t FloydCameraPreviousObjectAddress = 0x8009FCB8;
const uint32_t FloydCameraLateralTail = 0x08012721; // j 0x80049C84
uint32_t SidekickControlEntry = 0x8002F728;
uint32_t SidekickControlEnd = 0x8003109C;
uint32_t SidekickLateralMoveEntry = 0x80031088;
uint32_t SidekickLateralMoveStub = 0x80066F00;
uint32_t SidekickLateralMoveJump = 0x08019BC0; // j SidekickLateralMoveStub
uint32_t SidekickLateralMovePreviousJump = 0x08019B40; // j old 0x80066D00 stub
const uint32_t SidekickLateralMoveEntryOriginal = 0x03E00008; // jr ra
const uint32_t SidekickLateralMoveDelayOriginal = 0x27BD0120; // addiu sp, sp, 0x120
// Previous tests hooked the input vector and the shared sidekickControl
// prologue. Remove either entry when a save state restores it before using the
// final transform hook.
uint32_t SidekickLateralMoveInputLegacyEntry = 0x8002F8F4;
const uint32_t SidekickLateralMoveInputLegacyOriginal = 0x86020090; // lh v0, 0x90(s0)
uint32_t SidekickLateralMoveOldTailEntry = 0x80031080;
const uint32_t SidekickLateralMoveOldTailOriginal = 0x8FBF003C; // lw ra, 0x3c(sp)
const uint32_t SidekickLateralMoveOldTailDelayOriginal = 0x8FB00038; // lw s0, 0x38(sp)
uint32_t SidekickControlProbeEntry = 0x8002F8AC;
uint32_t SidekickControlProbeStub = 0x80066D00;
uint32_t SidekickControlProbeJump = 0x08019B40; // j SidekickControlProbeStub
const uint32_t SidekickControlProbeEntryOriginal = 0xE6080014; // swc1 f8, 0x14(s0)
const uint32_t SidekickControlProbeDelayOriginal = 0x8FAD00E8; // lw t5, 0xE8(sp)
uint32_t SidekickControlProbeResumeJump = 0x0800BE2D; // j 0x8002F8B4
uint32_t SidekickControlObjectAddress = 0x8009FCA0;
uint32_t SidekickVelocityLateralEntry = 0x8002F8AC;
uint32_t SidekickVelocityLateralDelay = 0x8002F8B0;
uint32_t SidekickVelocityLateralResume = 0x8002F8B4;
uint32_t SidekickVelocityLateralStub = 0x80066F00;
uint32_t SidekickVelocityLateralJump = 0x08019BC0; // j SidekickVelocityLateralStub
const uint32_t SidekickVelocityLateralEntryOriginal = 0xE6080014; // swc1 f8, 0x14(s0)
const uint32_t SidekickVelocityLateralDelayOriginal = 0x8FAD00E8; // lw t5, 0xE8(sp)
uint32_t SidekickVelocityLateralResumeJump = 0x0800BE2D; // j 0x8002F8B4
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
// Reuses the address the retired sidekickpadMovePlayer stub occupied. That one
// ran over a hundred times per drone session without faulting, so this is the
// one spot in diCpuTraceInit proven dead by execution rather than by arithmetic
// on neighbouring stub sizes. It is 28 words of proven room and this stub is
// 27. PlayerVelocityStub shares the address but is never installed.
uint32_t SidekickStrafeStub = 0x80066C00;
uint32_t SidekickStrafeJump = 0x08019B00; // j SidekickStrafeStub
uint32_t SidekickStrafeResumeJump = 0x0800C01A; // j 0x80030068
uint32_t SidekickPadProbeStub = 0x80066D80;
uint32_t SidekickPadProbeJump = 0x08019B60; // j SidekickPadProbeStub
uint32_t SidekickPadProbeObjectAddress = 0x8009FCDC;
uint32_t SidekickPadProbeStateAddress = 0x8009FCCC;
uint32_t SidekickPadProbeActorAddress = 0x8009FCFC;
const uint32_t SidekickPadProbeEntryOriginal = 0x27BDFF98; // addiu sp, sp, -0x68
const uint32_t SidekickPadProbeSecondOriginal = 0xAFBF0034; // sw ra, 0x34(sp)
// The squads module publishes one time step, computed by both of its
// controllers from the per-frame tick count they are handed. That tick count is
// the game's own frame delta - measured live it is 2 at 30fps and 1 at 60fps,
// giving a step of 2.0 vs 1.0 - so the module already scales everything it
// drives by it and is frame-rate-correct on its own. Halving the step was a
// mistake, born of reading it as a constant 1.0: it does not slow movement
// alone, it halves the delta itself, so all seven readers - the Squaddies'
// action timers, their interpolation, and their animation advance - run at half
// wall-clock speed at 60fps. That half-speed animation is the enemy slow-motion
// this reverts. Enemy *movement* that genuinely needed slowing is handled off
// the delta (PatchObjectMove for objMoveXYZ movers, HalveNamedEnemyMovement for
// named fliers, PatchSquaddieMove's direct velocity halve), so the step is left
// at its native value: PatchSquadsTimeStep stays disabled. Restoring the delta
// makes the module match 30fps exactly and, because 30fps already runs a step
// of 2.0, can never make anything move faster than 30fps does.
const uint32_t SquadsTimeStepOffset = 0x1A1C8;
// The int copy SquadronControl writes beside the step. Its high half is what
// that jump's delay slot leaves in $at, so the stub restores it from here.
const uint32_t SquadsTickCountAddress = 0x800FE9F8;
// A second module global the SquaddieControl delay slot addresses. Its own high
// half is what that slot leaves in $at, so the stub restores it from here.
const uint32_t SquaddieStepDelayTargetOffset = 0x1A140;
const uint32_t SquadronStepCvtOffset = 0x14D74;
const uint32_t SquadronStepEntryOffset = 0x14D7C;
const uint32_t SquaddieStepCvtOffset = 0x1064C;
const uint32_t SquaddieStepEntryOffset = 0x10658;
const uint32_t SquadronStepCvtOriginal = 0x468021A0;   // cvt.s.w $f6, $f4
const uint32_t SquaddieStepCvtOriginal = 0x468042A0;   // cvt.s.w $f10, $f8
// The step-store entry words (swc1 $f6/$f10, %lo(Step)($at)) are relocation
// dependent, so PatchSquadsTimeStep reconstructs them from the live Step rather
// than from a fixed constant here.
// Past every other scratch stub. Eight words each, which is what the halve,
// store and $at restore need.
uint32_t SquadronStepStub = 0x8009FDA0;
uint32_t SquaddieStepStub = 0x8009FDC0;

const uint32_t SquaddieXEntryOffset = 0xAE38;
const uint32_t SquaddieXDelayOffset = 0xAE3C;
const uint32_t SquaddieXResumeOffset = 0xAE40;
const uint32_t SquaddieZEntryOffset = 0xAE68;
const uint32_t SquaddieZDelayOffset = 0xAE6C;
const uint32_t SquaddieZResumeOffset = 0xAE70;
uint32_t SquaddieXStub = 0x8009FD60;
uint32_t SquaddieZStub = 0x8009FD80;

uint32_t JalInstruction(uint32_t Address)
{
    return 0x0C000000 | ((Address >> 2) & 0x03FFFFFF);
}

void AppendCinematicProbeLog(const stdstr & Message)
{
    CPath ProbePath(CPath::MODULE_DIRECTORY, CinematicProbeLogFileName);
    FILE * ProbeFile = fopen(ProbePath, "a");
    if (ProbeFile != nullptr)
    {
        fprintf(ProbeFile, "%s\n", Message.c_str());
        fclose(ProbeFile);
    }
}

// Cataloguing every cinematic in the game takes more than one sitting, so the
// probe log is never truncated: each emulator run appends to what the previous
// runs collected. A dated banner opens each run because the P counter in the
// probe lines restarts from one every time the process does.
void OpenCinematicProbeLogSession(void)
{
    static bool SessionOpened = false;
    if (SessionOpened)
    {
        return;
    }
    SessionOpened = true;

    AppendCinematicProbeLog(stdstr_f(
        "---- session %s ----",
        CDateTime().SetToNow().Format("%Y-%m-%d %H:%M:%S").c_str()));
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
const uint32_t IntroCinematicSkipEntryOffset = 0xD0;
const uint32_t IntroCinematicSkipResumeOffset = 0xD8;
const uint32_t IntroCinematicSkipEntryOriginal = 0x8E0E0000; // lw $t6, 0($s0)
const uint32_t IntroCinematicSkipEntryDelayOriginal = 0xAFBF0024; // sw $ra, 0x24($sp)
const uint32_t IntroCinematicSkipStub = 0x80067200;
uint32_t IntroCinematicSkipJump = 0x08019C80; // j IntroCinematicSkipStub
const size_t IntroCinematicSkipResumeJumpIndex = 25;
const uint32_t LegacyIntroCinematicSkipEntryOffset = 0xC0;
const uint32_t LegacyIntroCinematicSkipEntryOriginal = 0x27BDFF90; // addiu $sp, $sp, -0x70
const uint32_t LegacyIntroCinematicSkipEntryDelayOriginal = 0xAFB00020; // sw $s0, 0x20($sp)
const uint32_t LegacyIntroCinematicSkipFmvUpdateOffset = 0x37C;
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
// same uncorrected horizontal scale. Keep this US-only prototype scoped to the
// body of overlay 14's frontSingleInstruments: its call at +0xC00 establishes
// the first HUD orthographic matrix after the reticle, and every return path
// converges on the epilogue at +0x10D0.
//
// Reuse the same retired diagnostic region as the other US JFG trampolines,
// but only after gameplay and overlay 14 are live. Writing this while the boot
// thread is still running diCpuTraceInit prevents a cold boot, so the runtime
// readiness gate below is part of the cave's safety contract.
//
// The scope counter deliberately lives on a different page: storing it twice
// per HUD pass on the code page would continually invalidate every stub under
// the recompiler's self-modifying-code tracking.
const uint32_t WidescreenHudCaveStart = 0x80067280;
const uint32_t WidescreenHudCaveEnd = 0x80067690;
const size_t WidescreenHudMainCaveWordCount =
    (WidescreenHudCaveEnd - WidescreenHudCaveStart) / sizeof(uint32_t);
// A second, disjoint segment reuses the retired diagnostic ring-buffer display.
// Its sole caller at 0x800674BC is already replaced by SpriteScaleAltCode.
// Keep the intervening diCpuLogMessage code entirely outside our memory image.
const uint32_t WidescreenHudReticleStub = 0x80067790;
const uint32_t WidescreenHudReticleCaveEnd = 0x80067844;
const size_t WidescreenHudCaveWordCount = WidescreenHudMainCaveWordCount +
    (WidescreenHudReticleCaveEnd - WidescreenHudReticleStub) / sizeof(uint32_t);

uint32_t WidescreenHudCaveWordAddress(size_t Index)
{
    return Index < WidescreenHudMainCaveWordCount ?
        WidescreenHudCaveStart + (uint32_t)(Index * sizeof(uint32_t)) :
        WidescreenHudReticleStub + (uint32_t)((Index - WidescreenHudMainCaveWordCount) * sizeof(uint32_t));
}
const uint32_t WidescreenHudScopeEnterStub = 0x80067280;
const uint32_t WidescreenHudScopeExitStub = 0x800672A0;
const uint32_t WidescreenHudCamCopyStub = 0x800672C0;
const uint32_t WidescreenHudFontYStub = 0x80067300;
const uint32_t WidescreenHudDigitalAdvanceStub = 0x80067340;
const uint32_t WidescreenHudFontDtdyStub = 0x80067360;
const uint32_t WidescreenHudSpriteScaleStub = 0x800673A0;
const uint32_t WidescreenHudLineStub = 0x800673E0;
const uint32_t WidescreenHudRectangleStub = 0x80067440;
const uint32_t WidescreenHudShotGaugeWrapperStub = 0x800677F4;
const uint32_t WidescreenHudShotGaugeAnchorStub = 0x80067810;
const uint32_t WidescreenHudShotGaugeCallOffset = 0x2B28;
const uint32_t WidescreenHudShotGaugeCallOriginal = 0x0C01657D;
const uint32_t WidescreenHudShotGaugeCallDelay = 0x00003825;
const uint32_t WidescreenHudSpriteScaleAltStub = 0x800674A0;
const uint32_t WidescreenHudSpritePositionStub = 0x800674E0;
const uint32_t WidescreenHudMatrixTranslateStub = 0x80067560;
// Extend into the first 0x80 bytes of the unused diCpuReportWatchpoint.
// The next original function starts at 0x800676B4; no other stub occupies it.
const uint32_t WidescreenHudAmmoStub = 0x80067610;
const uint32_t WidescreenHudAmmoEntry = 0x8005900C;
const uint32_t WidescreenHudAmmoOriginal = 0x00135080;
const uint32_t WidescreenHudAmmoDelayOriginal = 0x030A4021;
// cpuTraceTrackBufStatus is the byte at 0x80102550; the following three bytes
// are alignment padding before the pointer at 0x80102554. Claim only the last
// padding byte so a non-zero diagnostic status can never make the HUD hooks
// appear active outside their scope.
const uint32_t WidescreenHudScopeDepthAddress = 0x80102553;
const uint32_t WidescreenHudResolutionIndexAddress = 0x800FECA8;

bool IsWidescreenHudResolution(uint8_t Resolution)
{
    // Active US gameplay modes: 0/2 are 4:3, 1/3 are widescreen.
    // Do not treat an odd boot/reset mode or the graphics plugin's aspect
    // ratio as the game's widescreen setting.
    return Resolution == 1 || Resolution == 3;
}

const uint32_t WidescreenHudOverlayModule = 14;
const uint32_t WidescreenHudOverlayEnterOffset = 0xC00;
const uint32_t WidescreenHudOverlayExitOffset = 0x10D0;
const uint32_t WidescreenHudOverlayEnterOriginal = 0x0C010359; // jal camStandardOrtho
const uint32_t WidescreenHudOverlayEnterDelayOriginal = 0x02002025; // or a0, s0, zero
const uint32_t WidescreenHudOverlayExitOriginal = 0x03E00008; // jr ra
const uint32_t WidescreenHudOverlayExitDelayOriginal = 0x27BD00B0; // addiu sp, sp, 0xB0
const uint32_t WidescreenHudReticleOverlayModule = 13;
const uint32_t WidescreenHudReticleDrawOffset = 0x4A8;
const uint32_t WidescreenHudReticleLineCallOriginal = 0x0C01B563; // jal fxDrawLineInWindow
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

// frontDrawTarget passes already projected/rotated endpoints, with its aim
// centre in s4. Compress each X offset around that centre BEFORE clipping and
// CPU line rasterization; never rescale the centre or thin a bitmap afterwards.
// Round 3*dx/4 to nearest, ties away from zero, to keep mirrored lines symmetric.
// The original call delay has run; this tail call preserves ra, stack arguments,
// Y, colour, all saved registers, HI/LO and the FPU. t0/t1 are caller-saved.
// Normal reticles run outside the HUD scope. If a diagnostic forces that scope
// open, leave the existing fxDrawLine correction to act once, not twice.
const uint32_t WidescreenHudReticleCode[] =
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
static_assert(WidescreenHudReticleStub + sizeof(WidescreenHudReticleCode) == WidescreenHudShotGaugeWrapperStub,
              "Reticle and shot-gauge stubs must not overlap");
static_assert(WidescreenHudReticleStub >= 0x80067790 && WidescreenHudReticleCaveEnd <= 0x800678C4,
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

const uint32_t WidescreenHudCamCopyEntry = 0x80042158;
const uint32_t WidescreenHudCamCopyOriginal = 0xC4243128; // lwc1 f4, 0x3128(at)
const uint32_t WidescreenHudFontYEntry = 0x80070500;
const uint32_t WidescreenHudFontYOriginal = 0x31AE0FFF; // andi t6, t5, 0x0FFF
const uint32_t WidescreenHudFontDtdyEntry = 0x80070550;
const uint32_t WidescreenHudFontDtdyOriginal = 0x3C0E0400; // lui t6, 0x0400
const uint32_t WidescreenHudSpriteScaleEntry = 0x80041850;
const uint32_t WidescreenHudSpriteScaleOriginal = 0x44050000; // mfc1 a1, f0
const uint32_t WidescreenHudSpriteScaleAltEntry = 0x8004189C;
const uint32_t WidescreenHudSpritePositionEntry = 0x8004171C;
const uint32_t WidescreenHudSpritePositionOriginal = 0x44183000; // mfc1 t8, f6
const uint32_t WidescreenHudMatrixTranslateEntry = 0x800498E8;
const uint32_t WidescreenHudMatrixTranslateOriginal = 0xC4E00000; // lwc1 f0, 0(a3)
const uint32_t WidescreenHudLineEntry = 0x8006D390;
const uint32_t WidescreenHudLineOriginal = 0x3C0E8010; // lui t6, 0x8010
const uint32_t WidescreenHudLineDelayOriginal = 0x8DCE3B90; // lw t6, 0x3B90(t6)
const uint32_t WidescreenHudRectangleEntry = 0x80059790;
const uint32_t WidescreenHudRectangleOriginal = 0x8E020000; // lw v0, 0(s0)
const uint32_t WidescreenHudRectangleDelayOriginal = 0x0018CB80; // sll t9, t8, 14
const uint32_t WidescreenHudDigitalColumnAEntry = 0x8006DE64;
const uint32_t WidescreenHudDigitalColumnBEntry = 0x8006DF14;
const uint32_t WidescreenHudDigitalColumnOriginal = 0x24E70002; // addiu a3, a3, 2
const uint32_t WidescreenHudDigitalColumnCompressed = 0x24E70000; // merge one source column
const uint32_t WidescreenHudDigitalRowAdvanceEntry = 0x8006DF70;
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
const uint32_t WidescreenHudDigitalAdvanceEntry = 0x8006E088;
const uint32_t WidescreenHudDigitalAdvanceOriginal = 0x2673000A; // addiu s3, s3, 10
const uint32_t WidescreenHudDigitalAdvanceCompressed = 0x26730008; // addiu s3, s3, 8

// The four patches above are retired. The probe proved this renderer never
// runs for the visible counter: no item of its queue type was produced across
// two sessions. The panel actually uses frontPrintNum's own TextureRectangles,
// confirmed by a runtime trace of its yellow/white 96/100 arguments.
// They are still recognised here so a save state that
// carries them can be returned to stock.
const uint32_t WidescreenHudDigitalRetiredCount = 4;
const GAME_HACK_CODE_PATCH WidescreenHudDigitalRetired[] =
{
    { WidescreenHudDigitalColumnAEntry, WidescreenHudDigitalColumnOriginal,
      WidescreenHudDigitalColumnCompressed },
    { WidescreenHudDigitalColumnBEntry, WidescreenHudDigitalColumnOriginal,
      WidescreenHudDigitalColumnCompressed },
    { WidescreenHudDigitalRowAdvanceEntry,
      WidescreenHudDigitalRowAdvanceOriginal,
      WidescreenHudDigitalRowAdvanceCompressed },
    { WidescreenHudDigitalAdvanceEntry, WidescreenHudDigitalAdvanceOriginal,
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
static_assert(WidescreenHudAmmoStub + sizeof(WidescreenHudAmmoCode) <= WidescreenHudCaveEnd,
              "Ammo stub exceeds its reserved cave slot");
static_assert(WidescreenHudCaveEnd <= 0x800676B4,
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

// WidescreenHudDigitalAdvanceStub is no longer written: the digit stride is a
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
static_assert(WidescreenHudShotGaugeWrapperStub + sizeof(WidescreenHudShotGaugeWrapperCode) == WidescreenHudShotGaugeAnchorStub,
              "Shot-gauge wrapper must end before its rectangle anchor helper");
static_assert(WidescreenHudShotGaugeAnchorStub + sizeof(WidescreenHudShotGaugeAnchorCode) == WidescreenHudReticleCaveEnd,
              "Shot-gauge helper must fit within the retired ring-display function");

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
const uint32_t LandingCinematicSkipStubCode[] =
{
    0x3C01800A, // lui   $at, 0x800A
    0x8C28FCBC, // lw    $t0, 0xFCBC($at)  ; direct E / Return request
    0x11000003, // beq   $t0, $zero, resume
    0x00000000, // nop
    0x10000005, // b     trySkip
    0x00000000, // nop
    0x3C1F8004, // resume: lui $ra, 0x8004
    0x37FF5FF4, // ori   $ra, $ra, 0x5FF4
    0x03E00008, // jr    $ra
    0x00000000, // nop

    0x3C0E800A, // trySkip: lui $t6, 0x800A
    0x81CE3294, // lb    $t6, 0x3294($t6)  ; loading
    0x27BDFFD8, // addiu $sp, $sp, -0x28
    0x15C00044, // bne   $t6, $zero, return
    0x00000000, // nop

    0x3C04800A, // lui   $a0, 0x800A
    0x8484323C, // lh    $a0, 0x323C($a0)  ; currentScene
    0x3C06800A, // lui   $a2, 0x800A
    0x24C63248, // addiu $a2, $a2, 0x3248  ; currentSetup
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
    0x0C011D82, // jal   mainFrontInit(5, 0x7F, 0)
    0x00003025, // or    $a2, $zero, $zero
    0x0C016ABA, // jal   frontCharSelectSetQuitMode(0)
    0x00002025, // or    $a0, $zero, $zero
    0x10000025, // b     return
    0x00000000, // nop

    0x3C028006, // landing_check: lui $v0, 0x8006
    0x24437160, // addiu $v1, $v0, 0x7160  ; LandingCinematicSkipStubTable
    0x846F0002, // loop: lh $t7, 2($v1)    ; destination
    0x2405FFFF, // addiu $a1, $zero, -1
    0x10AF001F, // beq   $a1, $t7, return
    0x3C04800A, // lui   $a0, 0x800A
    0x3C06800A, // lui   $a2, 0x800A
    0x8484323C, // lh    $a0, 0x323C($a0)  ; currentScene
    0x24C63248, // addiu $a2, $a2, 0x3248  ; currentSetup
    0x94620000, // lhu   $v0, 0($v1)       ; scene << 4 | setup
    0x00000000, // nop
    0x0002C102, // srl   $t8, $v0, 4
    0x14980013, // bne   $a0, $t8, next
    0x00000000, // nop
    0x84D90000, // lh    $t9, 0($a2)
    0x3048000F, // andi  $t0, $v0, 0x000F
    0x1728000F, // bne   $t9, $t0, next
    0x00000000, // nop
    0x0C016297, // jal   frontGetMode
    0xAFA30024, // sw    $v1, 0x24($sp)
    0x8FA30024, // lw    $v1, 0x24($sp)
    0x3C05800A, // lui   $a1, 0x800A
    0x84640002, // lh    $a0, 2($v1)       ; destination scene
    0x84A53260, // lh    $a1, 0x3260($a1)  ; nextCharacter
    0x240A0001, // addiu $t2, $zero, 1
    0xAFAA0010, // sw    $t2, 0x10($sp)
    0xAFA00014, // sw    $zero, 0x14($sp)
    0x00003025, // or    $a2, $zero, $zero
    0x0C011997, // jal   mainChangeLevel
    0x00403825, // or    $a3, $v0, $zero   ; frontGetMode result
    0x10000005, // b     return
    0x00000000, // nop
    0x846B0006, // next: lh $t3, 6($v1)
    0x24630004, // addiu $v1, $v1, 4
    0x14ABFFDF, // bne   $a1, $t3, loop
    0x00000000, // nop
    0x3C1F8004, // return: lui $ra, 0x8004
    0x37FF5FF4, // ori   $ra, $ra, 0x5FF4
    0x27BD0028, // addiu $sp, $sp, 0x28
    0x03E00008, // jr    $ra
    0x00000000, // nop
};

// The stub above walks a scene-to-destination table that begins 0x160 bytes
// into the cave (LandingCinematicSkipStub + 0x160, just past the 87 code words).
// A held E/Return plus a match on (currentScene, currentSetup) warps past the
// cinematic through mainChangeLevel. Keeping the table as data - rather than
// hand-encoded words - lets the catalogue recovered from JfgCinematicProbe.log
// grow by editing a list. BuildLandingCinematicSkipImage stitches the code, the
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
    sizeof(LandingCinematicSkipStubCode) / sizeof(uint32_t) <=
        LandingCinematicSkipTableWordOffset,
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
    std::vector<uint32_t> Image(
        LandingCinematicSkipStubCode,
        LandingCinematicSkipStubCode +
            sizeof(LandingCinematicSkipStubCode) / sizeof(uint32_t));
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
uint32_t ManualAimCursorXStoreWord = 0xA7190000;
uint32_t ManualAimCursorYStoreWord = 0xA58D0000;
uint32_t SchedulerSignatureWord0 = 0x8E480300;
uint32_t SchedulerSignatureWord2 = 0x2D210002;
uint32_t FramePacing60SignatureWord0 = 0x8DCEECCC;
uint32_t FramePacingSignatureWord1 = 0x2442ECAB;
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
    { ManualAimCursorXStore, ManualAimCursorXStoreWord, ManualAimCursorXStoreClear, false },
    { ManualAimCursorYStore, ManualAimCursorYStoreWord, ManualAimCursorYStoreClear, false },
    { ManualAimXVelocityStore, 0xE60401E4, 0xE60801E4, false },
    { ManualAimYVelocityStore, 0xE60401E8, 0xE60801E8, false },

    { CameraHeightBlendBase + 0x00, 0x8D230000, 0x3C01800A, true },
    { CameraHeightBlendBase + 0x04, 0xC7A40090, 0xC7A40090, true },
    { CameraHeightBlendBase + 0x08, 0xC46C0010, 0xE424F244, true },
    { CameraHeightBlendBase + 0x0C, 0xC7A800A8, 0xC426F248, true },
    { CameraHeightBlendBase + 0x10, 0x460C2181, 0x46062100, true },
    { CameraHeightBlendBase + 0x14, 0x46083482, 0xC46C0010, true },
    { CameraHeightBlendBase + 0x18, 0x460C9280, 0x460C2181, true },
    { CameraHeightBlendBase + 0x1C, 0xE46A0010, 0x46083482, true },
    { CameraHeightBlendBase + 0x20, 0x8D230000, 0x460C9280, true },
    { CameraHeightBlendBase + 0x24, 0xC7A40094, 0xE46A0010, true },
    { CameraHeightBlendBase + 0x28, 0xC4620014, 0xC7A40094, true },
    { CameraHeightBlendBase + 0x2C, 0xC7A800A8, 0xC4620014, true },
    { CameraHeightBlendBase + 0x30, 0x46022181, 0x46022181, true },
    { CameraHeightBlendBase + 0x34, 0x46083482, 0x46083482, true },
    { CameraHeightBlendBase + 0x38, 0x46029280, 0x46029280, true },
    { CameraHeightBlendBase + 0x3C, 0xE46A0014, 0xE46A0014, true },

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

// Left as US addresses on purpose. These live in an overlay resident in
// expansion RAM, not in the base segment the address table covers, so they
// cannot be mapped the same way. The feature is disabled, and its table is
// verified before any write, so on another build it simply declines.
GAME_HACK_CODE_PATCH BoyAimPatches[] =
{
    { 0x8035C7B8, 0x87A50056, 0x24050000 },
    { 0x8035C7BC, 0x860401CE, 0x00002025 },
    { 0x8035C7D0, 0x87A50054, 0x860501E2 },
    { 0x8035C7D4, 0x860401D0, 0x00A02025 },
    { 0x8035CD58, 0x87A50056, 0x24050000 },
    { 0x8035CD5C, 0x860401CE, 0x00002025 },
    { 0x8035CD70, 0x87A50054, 0x860501E2 },
    { 0x8035CD74, 0x860401D0, 0x00A02025 },
    { 0x8035C788, 0x860401DC, 0x00002025 },
    { 0x8035C78C, 0x87A50056, 0x24050000 },
    { 0x8035C7A0, 0x87A50054, 0x860501E2 },
    { 0x8035C7A4, 0x860401DE, 0x00A02025 },
    { 0x8035CD28, 0x860401DC, 0x00002025 },
    { 0x8035CD2C, 0x87A50056, 0x24050000 },
    { 0x8035CD40, 0x87A50054, 0x860501E2 },
    { 0x8035CD44, 0x860401DE, 0x00A02025 },
};

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

GAME_HACK_CODE_PATCH TripleBufferPatches[] =
{
    { TripleBufferRequest, 0x308E0001, 0x240E0001 }, // addiu $t6, $zero, 1
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

// An old save state may still contain the first wake gate. Restore its call and
// release its former stub before installing the new, delta-correct version.
GAME_HACK_CODE_PATCH WaterWakeLegacyGatePatches[] =
{
    { WaterWakeGateStub + 0x00, 0x00000000, 0x3C01800A }, // lui  $at, 0x800A
    { WaterWakeGateStub + 0x04, 0x00000000, 0x8C28FCE4 }, // lw   $t0, -0x31C($at)
    { WaterWakeGateStub + 0x08, 0x00000000, 0x31080004 }, // andi $t0, $t0, 4
    { WaterWakeGateStub + 0x0C, 0x00000000, 0x15000003 }, // bne  $t0, $zero, update
    { WaterWakeGateStub + 0x10, 0x00000000, 0x00000000 }, // nop
    { WaterWakeGateStub + 0x14, 0x00000000, 0x03E00008 }, // jr   $ra
    { WaterWakeGateStub + 0x18, 0x00000000, 0x00000000 }, // nop
    { WaterWakeGateStub + 0x1C, 0x00000000, 0x0801AC24 }, // j    wakeUpdateRipple
    { WaterWakeGateStub + 0x20, 0x00000000, 0x00000000 }, // nop
};

// Restores the direct opacity experiment if it is present in an old save state.
GAME_HACK_CODE_PATCH WaterWakeLegacyRatePatches[] =
{
    { WaterWakeRingRateEntry - 0x8C, 0x24580040, 0x24580020 }, // +64 to +32
    { WaterWakeRingRateEntry - 0x3C, 0x25CFFFC0, 0x25CFFFE0 }, // -64 to -32
    { WaterWakeUpdate + 0x38, 0x25F80020, 0x25F80010 }, // +32 to +16
    { WaterWakeUpdate + 0x64, 0x254BFFE0, 0x254BFFF0 }, // -32 to -16
};

// On alternate calls, return immediately. The retained call keeps its native
// delta of one: this deliberately slows both fixed-step and delta-scaled wake
// lifetimes to the original 30Hz cadence. The final call patch is intentionally
// last.
GAME_HACK_CODE_PATCH WaterWakeGatePatches[] =
{
    { WaterWakeGateStub + 0x00, 0x00000000, 0x3C018010 }, // lui   $at, 0x8010
    { WaterWakeGateStub + 0x04, 0x00000000, 0x8C28D7C0 }, // lw    $t0, -0x2840($at)
    { WaterWakeGateStub + 0x08, 0x00000000, 0x31080001 }, // andi  $t0, $t0, 1
    { WaterWakeGateStub + 0x0C, 0x00000000, 0x15000004 }, // bne   $t0, $zero, return
    { WaterWakeGateStub + 0x10, 0x00000000, 0x00000000 }, // nop
    { WaterWakeGateStub + 0x14, 0x00000000, 0x00000000 }, // preserve caller's $a1
    { WaterWakeGateStub + 0x18, 0x00000000, 0x0801AC24 }, // j     wakeUpdateRipple
    { WaterWakeGateStub + 0x1C, 0x00000000, 0x00000000 }, // nop
    { WaterWakeGateStub + 0x20, 0x00000000, 0x03E00008 }, // jr    $ra
    { WaterWakeGateStub + 0x24, 0x00000000, 0x00000000 }, // nop
    { WaterWakeLegacyCallSite, WaterWakeUpdateCall, WaterWakeGateJump },
};

// fxDrawLevelEffects runs immediately after the wake pass. This temporary
// probe removes it to determine whether it overwrites the wake at 60fps.
GAME_HACK_CODE_PATCH WaterWakeDrawProbePatches[] =
{
    { WaterWakeDrawFallbackEntry, 0x0C01A2D5, 0x00000000 }, // jal fxDrawLevelEffects -> nop
};

// The original path culls the wake before the wake-draw pass builds its input
// list. The hook preserves that path byte-for-byte for normal objects, while
// the small marker check sends only water wakes to the existing list append.
// The stub ends before the Squaddie hooks at 0x8009FD60.
GAME_HACK_CODE_PATCH WaterWakeCullingPatches[] =
{
    { WaterWakeCullingStub + 0x00, 0x00000000, 0x8C480058 }, // lw    $t0, 0x58($v0)
    { WaterWakeCullingStub + 0x04, 0x00000000, 0x11000008 }, // beq   $t0, $zero, normal
    { WaterWakeCullingStub + 0x08, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingStub + 0x0C, 0x00000000, 0x91090000 }, // lbu   $t1, 0($t0)
    { WaterWakeCullingStub + 0x10, 0x00000000, 0x24010040 }, // addiu $at, $zero, 0x40
    { WaterWakeCullingStub + 0x14, 0x00000000, 0x15210004 }, // bne   $t1, $at, normal
    { WaterWakeCullingStub + 0x18, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingStub + 0x1C, 0x00000000, 0x8FAE0064 }, // lw    $t6, 0x64($sp)
    { WaterWakeCullingStub + 0x20, 0x00000000, 0x080051C6 }, // j     0x80014718
    { WaterWakeCullingStub + 0x24, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingStub + 0x28, 0x00000000, 0x11A00008 }, // beq   $t5, $zero, skip
    { WaterWakeCullingStub + 0x2C, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingStub + 0x30, 0x00000000, 0x0C0057B6 }, // jal   0x80015ED8
    { WaterWakeCullingStub + 0x34, 0x00000000, 0x00402021 }, // addu  $a0, $v0, $zero
    { WaterWakeCullingStub + 0x38, 0x00000000, 0x10400004 }, // beq   $v0, $zero, skip
    { WaterWakeCullingStub + 0x3C, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingStub + 0x40, 0x00000000, 0x8FAE0064 }, // lw    $t6, 0x64($sp)
    { WaterWakeCullingStub + 0x44, 0x00000000, 0x080051C6 }, // j     0x80014718
    { WaterWakeCullingStub + 0x48, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingStub + 0x4C, 0x00000000, 0x080051BF }, // j     0x800146FC
    { WaterWakeCullingStub + 0x50, 0x00000000, 0x00000000 }, // nop
    { WaterWakeCullingEntry, 0x11A00005, WaterWakeCullingJump },
};

// Save the original return address, reset the texture-display-list cache, then
// draw the selected wake. The reset is local to this effect and avoids carrying
// a stale texture state from the preceding 60fps graphics task.
// This stays before the optional Squaddie hooks at 0x8009FD60.
GAME_HACK_CODE_PATCH WaterWakeDrawFallbackPatches[] =
{
    { WaterWakeDrawFallbackStub + 0x00, 0x00000000, 0xAFBF0040 }, // sw    $ra, 0x40($sp)
    { WaterWakeDrawFallbackStub + 0x04, 0x00000000, 0x02C02021 }, // addu  $a0, $s6, $zero
    { WaterWakeDrawFallbackStub + 0x08, 0x00000000, 0x0C0156E0 }, // jal   texDPInit
    { WaterWakeDrawFallbackStub + 0x0C, 0x00000000, 0x00000000 }, // nop
    { WaterWakeDrawFallbackStub + 0x10, 0x00000000, 0x3C08800A }, // lui   $t0, 0x800A
    { WaterWakeDrawFallbackStub + 0x14, 0x00000000, 0x8D04FCEC }, // lw    $a0, -0x314($t0)
    { WaterWakeDrawFallbackStub + 0x18, 0x00000000, 0x24090001 }, // addiu $t1, $zero, 1
    { WaterWakeDrawFallbackStub + 0x1C, 0x00000000, 0xAD09FCF0 }, // sw    $t1, -0x310($t0)
    { WaterWakeDrawFallbackStub + 0x20, 0x00000000, 0x10800004 }, // beq   $a0, $zero, resume
    { WaterWakeDrawFallbackStub + 0x24, 0x00000000, 0x00000000 }, // nop
    { WaterWakeDrawFallbackStub + 0x28, 0x00000000, 0x02C02821 }, // addu  $a1, $s6, $zero
    { WaterWakeDrawFallbackStub + 0x2C, 0x00000000, 0x0C01ADA7 }, // jal   wakeDrawRipple
    { WaterWakeDrawFallbackStub + 0x30, 0x00000000, 0x00000000 }, // nop
    { WaterWakeDrawFallbackStub + 0x34, 0x00000000, 0x8FBF0040 }, // lw    $ra, 0x40($sp)
    { WaterWakeDrawFallbackStub + 0x38, 0x00000000, 0x0800532A }, // j     0x80014CA8
    { WaterWakeDrawFallbackStub + 0x3C, 0x00000000, 0x00000000 }, // nop
    { WaterWakeDrawFallbackStub + 0x44, 0x00000000, 0x00000000 }, // nop
    { WaterWakeDrawFallbackEntry, 0x0C01A2D5, WaterWakeDrawFallbackJump },
};

GAME_HACK_CODE_PATCH WaterWakeFrameRatePatches[] =
{
    { WaterWakeFrameRateStub + 0x00, 0x00000000, 0x314C0001 }, // andi  $t4, $t2, 1
    { WaterWakeFrameRateStub + 0x04, 0x00000000, 0x11800004 }, // beq   $t4, $zero, decrement
    { WaterWakeFrameRateStub + 0x08, 0x00000000, 0x00000000 }, // nop
    { WaterWakeFrameRateStub + 0x0C, 0x00000000, 0x01406021 }, // addu  $t4, $t2, $zero
    { WaterWakeFrameRateStub + 0x10, 0x00000000, 0x0801AC69 }, // j     0x8006B1A4
    { WaterWakeFrameRateStub + 0x14, 0x00000000, 0x00000000 }, // nop
    { WaterWakeFrameRateStub + 0x18, 0x00000000, 0x016A6023 }, // subu  $t4, $t3, $t2
    { WaterWakeFrameRateStub + 0x1C, 0x00000000, 0x0801AC69 }, // j     0x8006B1A4
    { WaterWakeFrameRateStub + 0x20, 0x00000000, 0x00000000 }, // nop
    { WaterWakeFrameRateEntry, 0x016A6023, WaterWakeFrameRateJump },
};

// The stock call already has a1=s6 in its delay slot. Record its exact wake
// pointer and tail-call the original routine, preserving its original return.
GAME_HACK_CODE_PATCH WaterWakeStockDrawProbePatches[] =
{
    { WaterWakeStockDrawStub + 0x00, 0x00000000, 0x3C08800A }, // lui   $t0, 0x800A
    { WaterWakeStockDrawStub + 0x04, 0x00000000, 0xAD04FCF4 }, // sw    $a0, -0x30C($t0)
    { WaterWakeStockDrawStub + 0x08, 0x00000000, 0x24090001 }, // addiu $t1, $zero, 1
    { WaterWakeStockDrawStub + 0x0C, 0x00000000, 0xAD09FCF8 }, // sw    $t1, -0x308($t0)
    { WaterWakeStockDrawStub + 0x10, 0x00000000, 0x0801ADA7 }, // j     wakeDrawRipple
    { WaterWakeStockDrawStub + 0x14, 0x00000000, 0x00000000 }, // nop
    { WaterWakeStockDrawEntry, 0x0C01ADA7, WaterWakeStockDrawJump },
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

// controlFSUvels produces a controlled character's world-space X/Y/Z velocity
// at +0x18. Floyd is a separate character object from the on-foot player, so
// the hook deliberately applies to the helper's current object whenever the
// robot-only lateral flag is set.
// The hook leaves the native calculation intact, then turns its horizontal
// vector by 90 degrees when Floyd's C-button strafe flag is active. The game's
// own thrust curve therefore supplies both acceleration and top speed.
GAME_HACK_CODE_PATCH PlayerVelocityPatches[] =
{
    { PlayerVelocityStub + 0x00, 0x00000000, 0x3C01800A },  // lui   $at, 0x800A
    { PlayerVelocityStub + 0x04, 0x00000000, 0x8C29FCE4 },  // lw    $t1, 0xFCE4($at)
    { PlayerVelocityStub + 0x08, 0x00000000, 0x312A0001 },  // andi  $t2, $t1, 1
    { PlayerVelocityStub + 0x0C, 0x00000000, 0x11400010 },  // beq   $t2, $zero, return
    { PlayerVelocityStub + 0x10, 0x00000000, 0x8FA80024 },  // lw    $t0, 0x24($sp)
    { PlayerVelocityStub + 0x14, 0x00000000, 0x8C2BFCE8 },  // lw    $t3, 0xFCE8($at)
    { PlayerVelocityStub + 0x18, 0x00000000, 0x256B0001 },  // addiu $t3, $t3, 1
    { PlayerVelocityStub + 0x1C, 0x00000000, 0xAC2BFCE8 },  // sw    $t3, 0xFCE8($at)
    { PlayerVelocityStub + 0x20, 0x00000000, 0xC5000018 },  // lwc1  $f0, 0x18($t0)
    { PlayerVelocityStub + 0x24, 0x00000000, 0x312A0002 },  // andi  $t2, $t1, 2
    { PlayerVelocityStub + 0x28, 0x00000000, 0x11400006 },  // beq   $t2, $zero, left
    { PlayerVelocityStub + 0x2C, 0x00000000, 0xC5020020 },  // lwc1  $f2, 0x20($t0)
    { PlayerVelocityStub + 0x30, 0x00000000, 0x46001107 },  // neg.s $f4, $f2
    { PlayerVelocityStub + 0x34, 0x00000000, 0xE5040018 },  // swc1  $f4, 0x18($t0)
    { PlayerVelocityStub + 0x38, 0x00000000, 0xE5000020 },  // swc1  $f0, 0x20($t0)
    { PlayerVelocityStub + 0x3C, 0x00000000, 0x10000004 },  // b     return
    { PlayerVelocityStub + 0x40, 0x00000000, 0x00000000 },  // nop
    { PlayerVelocityStub + 0x44, 0x00000000, 0xE5020018 },  // left: swc1 $f2, 0x18($t0)
    { PlayerVelocityStub + 0x48, 0x00000000, 0x46000107 },  // neg.s $f4, $f0
    { PlayerVelocityStub + 0x4C, 0x00000000, 0xE5040020 },  // swc1  $f4, 0x20($t0)
    { PlayerVelocityStub + 0x50, 0x00000000, 0x8FBF0014 },  // return: lw $ra, 0x14($sp)
    { PlayerVelocityStub + 0x54, 0x00000000, 0x27BD0020 },  // addiu $sp, $sp, 0x20
    { PlayerVelocityStub + 0x58, 0x00000000, 0x03E00008 },  // jr    $ra
    { PlayerVelocityStub + 0x5C, 0x00000000, 0x00000000 },  // nop
    { PlayerVelocityEntry + 0x00, 0x8FBF0014, PlayerVelocityJump },
    { PlayerVelocityEntry + 0x04, 0x27BD0020, 0x00000000 },
};

// The dedicated Floyd controller ends with s0 still pointing to Floyd's
// object. This trampoline runs after its native movement calculation and
// changes only the resulting X/Z displacement while Q/D is held. It also keeps
// a per-object previous position, so no correction is applied on the first
// call after an object change.
const uint32_t FloydMoveHookCode[] =
{
    0x3C01800A, // lui   $at, 0x800A
    0xC600000C, // lwc1  $f0, 0x0C($s0)
    0xC6020014, // lwc1  $f2, 0x14($s0)
    0x8C2BFCD8, // lw    $t3, 0xFCD8($at)
    0x160B0021, // bne   $s0, $t3, initialise
    0x00000000, // nop
    0xC424FCD0, // lwc1  $f4, 0xFCD0($at)
    0xC426FCD4, // lwc1  $f6, 0xFCD4($at)
    0x46040201, // sub.s $f8, $f0, $f4
    0x46061281, // sub.s $f10, $f2, $f6
    0x8C29FCE4, // lw    $t1, 0xFCE4($at)
    0x312A0001, // andi  $t2, $t1, 1
    0x11400015, // beq   $t2, $zero, keep native movement
    0x00000000, // nop
    0x312A0002, // andi  $t2, $t1, 2
    0x1140000A, // beq   $t2, $zero, move left
    0x00000000, // nop
    0x46005307, // neg.s $f12, $f10
    0x460C2100, // add.s $f4, $f4, $f12
    0xE604000C, // swc1  $f4, 0x0C($s0)
    0x46083180, // add.s $f6, $f6, $f8
    0xE6060014, // swc1  $f6, 0x14($s0)
    0xE424FCD0, // swc1  $f4, 0xFCD0($at)
    0xE426FCD4, // swc1  $f6, 0xFCD4($at)
    0x1000000F, // b     finish
    0x00000000, // nop
    0x460A2100, // move left: add.s $f4, $f4, $f10
    0xE604000C, // swc1  $f4, 0x0C($s0)
    0x46083181, // sub.s $f6, $f6, $f8
    0xE6060014, // swc1  $f6, 0x14($s0)
    0xE424FCD0, // swc1  $f4, 0xFCD0($at)
    0xE426FCD4, // swc1  $f6, 0xFCD4($at)
    0x10000007, // b     finish
    0x00000000, // nop
    0xE420FCD0, // keep native movement: swc1 $f0, 0xFCD0($at)
    0xE422FCD4, // swc1  $f2, 0xFCD4($at)
    0x10000003, // b     finish
    0x00000000, // nop
    0xE420FCD0, // initialise: swc1 $f0, 0xFCD0($at)
    0xE422FCD4, // swc1  $f2, 0xFCD4($at)
    0xAC30FCD8, // finish: sw $s0, 0xFCD8($at)
    0x02002025, // or    $a0, $s0, $zero
    0x8FBF001C, // lw    $ra, 0x1C($sp)
    0x8FB00018, // lw    $s0, 0x18($sp)
    0x03E00008, // jr    $ra
    0x27BD0050, // addiu $sp, $sp, 0x50
};

// sidekickpadMovePlayer prepares objMoveXYZ(Floyd, dx, dy, dz). Redirecting
// only this call leaves its collision and all surrounding mission logic intact
// while rotating the final horizontal delta for Q/D.
const uint32_t FloydMoveCallHookCode[] =
{
    0x3C01800A, // lui   $at, 0x800A
    0x8C2BFCE8, // lw    $t3, 0xFCE8($at)
    0x256B0001, // addiu $t3, $t3, 1
    0xAC2BFCE8, // sw    $t3, 0xFCE8($at)
    0x8C29FCE4, // lw    $t1, 0xFCE4($at)
    0xAC29FCEC, // sw    $t1, 0xFCEC($at)
    0x312A0001, // andi  $t2, $t1, 1
    0x11400011, // beq   $t2, $zero, call objMoveXYZ
    0x00000000, // nop
    0x312A0002, // andi  $t2, $t1, 2
    0x11400009, // beq   $t2, $zero, move left
    0x00000000, // nop
    0x00A04025, // move right: or $t0, $a1, $zero
    0x44870000, // mtc1  $a3, $f0
    0x46000087, // neg.s $f2, $f0
    0x44051000, // mfc1  $a1, $f2
    0x01003825, // or    $a3, $t0, $zero
    0x10000006, // b     call objMoveXYZ
    0x00000000, // nop
    0x00A04025, // move left: or $t0, $a1, $zero
    0x44850000, // mtc1  $a1, $f0
    0x46000087, // neg.s $f2, $f0
    0x00E02825, // or    $a1, $a3, $zero
    0x44071000, // mfc1  $a3, $f2
    FloydMoveObjMoveCallOriginal, // call objMoveXYZ
    FloydMoveObjMoveDelayOriginal,
    0x00000000, // j sidekickpadMovePlayer + 0x208 (filled at install time)
    0x00000000, // nop
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
    0x11400028, // beq   $t2, $zero, done
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
    // Bound the sum, not each axis: turning hard at full speed otherwise lets
    // the lateral drift ride on top of the forward speed.
    0x46063282, // mul.s $f10, $f6, $f6
    0x46021382, // mul.s $f14, $f2, $f2
    0x460E5380, // add.s $f14, $f10, $f14
    0x46007384, // sqrt.s $f14, $f14         combined horizontal speed
    0xC730FCA4, // lwc1  $f16, 0xFCA4($t9)
    0x4610703E, // c.le.s $f14, $f16
    0x00000000, // nop
    0x45010004, // bc1t  within the cap
    0x00000000, // nop
    0x460E8403, // div.s $f16, $f16, $f14    cap / magnitude, magnitude > cap > 0
    0x46103182, // mul.s $f6, $f6, $f16
    0x46101082, // mul.s $f2, $f2, $f16
    0xE706001C, // swc1  $f6, 0x1C($t8)
    0xE7020024, // swc1  $f2, 0x24($t8)
    SidekickStrafeDelayOriginal, // done: replay the overwritten lwc1 $f8, 0x0($s0)
    SidekickStrafeResumeJump,
    0x00000000, // nop
};

// sidekickpadCamera writes the mission camera position in the delay slot of
// this call. The trampoline therefore runs after its native X/Z calculation,
// turns just the frame displacement while Q/D is held, then tail-calls the
// original routine so the caller resumes exactly as before.
const uint32_t FloydCameraLateralHookCode[] =
{
    0x3C01800A, // lui   $at, 0x800A
    0xC600000C, // lwc1  $f0, 0x0C($s0)
    0xC6020014, // lwc1  $f2, 0x14($s0)
    0x8C2BFCB8, // lw    $t3, 0xFCB8($at)
    0x160B0026, // bne   $s0, $t3, initialise
    0x00000000, // nop
    0xC424FCB0, // lwc1  $f4, 0xFCB0($at)
    0xC426FCB4, // lwc1  $f6, 0xFCB4($at)
    0x46040201, // sub.s $f8, $f0, $f4
    0x46061281, // sub.s $f10, $f2, $f6
    0x8C29FCE4, // lw    $t1, 0xFCE4($at)
    0x312A0001, // andi  $t2, $t1, 1
    0x11400019, // beq   $t2, $zero, keep native movement
    0x00000000, // nop
    0x8C2BFCE8, // lw    $t3, 0xFCE8($at)
    0x256B0001, // addiu $t3, $t3, 1
    0xAC2BFCE8, // sw    $t3, 0xFCE8($at)
    0x312A0002, // andi  $t2, $t1, 2
    0x11400008, // beq   $t2, $zero, move left
    0x00000000, // nop
    0x46005307, // neg.s $f12, $f10
    0x460C2100, // add.s $f4, $f4, $f12
    0xE604000C, // swc1  $f4, 0x0C($s0)
    0x46083180, // add.s $f6, $f6, $f8
    0xE6060014, // swc1  $f6, 0x14($s0)
    0x10000007, // b     store rotated movement
    0x00000000, // nop
    0x460A2100, // move left: add.s $f4, $f4, $f10
    0xE604000C, // swc1  $f4, 0x0C($s0)
    0x46083181, // sub.s $f6, $f6, $f8
    0xE6060014, // swc1  $f6, 0x14($s0)
    0x10000001, // b     store rotated movement
    0x00000000, // nop
    0xE424FCB0, // store rotated: swc1 $f4, 0xFCB0($at)
    0xE426FCB4, // swc1  $f6, 0xFCB4($at)
    0xAC30FCB8, // sw    $s0, 0xFCB8($at)
    FloydCameraLateralTail,
    0x00000000, // nop
    0xE420FCB0, // keep native: swc1 $f0, 0xFCB0($at)
    0xE422FCB4, // swc1  $f2, 0xFCB4($at)
    0xAC30FCB8, // sw    $s0, 0xFCB8($at)
    FloydCameraLateralTail,
    0x00000000, // nop
    0xE420FCB0, // initialise: swc1 $f0, 0xFCB0($at)
    0xE422FCB4, // swc1  $f2, 0xFCB4($at)
    0xAC30FCB8, // sw    $s0, 0xFCB8($at)
    FloydCameraLateralTail,
    0x00000000, // nop
};

// The state-7 branch advances Floyd's object before sidekickControl returns.
// Turn that final transform delta here, where no later game update in the
// frame can overwrite it. The delay slot preserves the original ra restore;
// the stub performs the remaining epilogue itself.
const uint32_t SidekickLateralMoveHookCode[] =
{
    0x3C01800A, // lui   $at, 0x800A
    0x8C28FCA0, // lw    $t0, 0xFCA0($at)
    0x11000027, // beq   $t0, $zero, return
    0x00000000, // nop
    0x8C29FCE4, // lw    $t1, 0xFCE4($at)
    0x312A0001, // andi  $t2, $t1, 1
    0x11400025, // beq   $t2, $zero, track native position
    0xC500000C, // lwc1  $f0, 0x0C($t0)
    0xC5020014, // lwc1  $f2, 0x14($t0)
    0x8C2BFCD8, // lw    $t3, 0xFCD8($at)
    0x150B0021, // bne   $t0, $t3, initialise
    0x00000000, // nop
    0xC424FCD0, // lwc1  $f4, 0xFCD0($at)
    0xC426FCD4, // lwc1  $f6, 0xFCD4($at)
    0x46040201, // sub.s $f8, $f0, $f4
    0x46061281, // sub.s $f10, $f2, $f6
    0x312A0002, // andi  $t2, $t1, 2
    0x1140000A, // beq   $t2, $zero, move left
    0x00000000, // nop
    0x46005307, // move right: neg.s $f12, $f10
    0x460C2100, // add.s $f4, $f4, $f12
    0xE504000C, // swc1  $f4, 0x0C($t0)
    0x46083180, // add.s $f6, $f6, $f8
    0xE5060014, // swc1  $f6, 0x14($t0)
    0xE424FCD0, // swc1  $f4, 0xFCD0($at)
    0xE426FCD4, // swc1  $f6, 0xFCD4($at)
    0x10000009, // b     finish
    0x00000000, // nop
    0x460A2100, // move left: add.s $f4, $f4, $f10
    0xE504000C, // swc1  $f4, 0x0C($t0)
    0x46083181, // sub.s $f6, $f6, $f8
    0xE5060014, // swc1  $f6, 0x14($t0)
    0xE424FCD0, // swc1  $f4, 0xFCD0($at)
    0xE426FCD4, // swc1  $f6, 0xFCD4($at)
    0x10000001, // b     finish
    0x00000000, // nop
    0x8C2BFCE8, // finish: lw $t3, 0xFCE8($at)
    0x256B0001, // addiu $t3, $t3, 1
    0xAC2BFCE8, // sw    $t3, 0xFCE8($at)
    0xAC28FCD8, // sw    $t0, 0xFCD8($at)
    0x03E00008, // jr    $ra
    0x00000000, // nop
    0x03E00008, // return: jr $ra
    0x00000000, // nop
    0xE420FCD0, // track native position: swc1 $f0, 0xFCD0($at)
    0xE422FCD4, // swc1  $f2, 0xFCD4($at)
    0xAC28FCD8, // sw    $t0, 0xFCD8($at)
    0x03E00008, // jr    $ra
    0x00000000, // nop
};

// Capture sidekickControl's Object argument before its state dispatch. The
// tail hook uses this saved pointer after the original epilogue has restored
// the stack, so this stub deliberately changes no game state.
const uint32_t SidekickControlProbeCode[] =
{
    0x8FAD00E8, // lw    $t5, 0xE8($sp)
    0x3C01800A, // lui   $at, 0x800A
    0x8FA80120, // lw    $t0, 0x120($sp)
    0xAC28FCA0, // sw    $t0, 0xFCA0($at)
    SidekickControlProbeResumeJump,
    0x00000000, // nop
};

// sidekickControl writes its world-space X/Y/Z movement vector at +0x0C.
// Its final Z store is replaced by a trampoline whose delay slot preserves
// that store; the trampoline then turns only X/Z and resumes the original
// control routine before the game integrates the result.
const uint32_t SidekickVelocityLateralHookCode[] =
{
    0x8FAD00E8, // lw    $t5, 0xE8($sp)
    0x3C01800A, // lui   $at, 0x800A
    0x8C29FCE4, // lw    $t1, 0xFCE4($at)
    0x312A0001, // andi  $t2, $t1, 1
    0x11400013, // beq   $t2, $zero, resume
    0x00000000, // nop
    0x8C2BFCE8, // lw    $t3, 0xFCE8($at)
    0x256B0001, // addiu $t3, $t3, 1
    0xAC2BFCE8, // sw    $t3, 0xFCE8($at)
    0xC600000C, // lwc1  $f0, 0x0C($s0)
    0xC6020014, // lwc1  $f2, 0x14($s0)
    0x312A0002, // andi  $t2, $t1, 2
    0x11400006, // beq   $t2, $zero, move left
    0x00000000, // nop
    0x46001107, // neg.s $f4, $f2
    0xE604000C, // swc1  $f4, 0x0C($s0)
    0xE6000014, // swc1  $f0, 0x14($s0)
    SidekickVelocityLateralResumeJump,
    0x00000000, // nop
    0xE602000C, // move left: swc1 $f2, 0x0C($s0)
    0x46000107, // neg.s $f4, $f0
    0xE6040014, // swc1  $f4, 0x14($s0)
    SidekickVelocityLateralResumeJump,
    0x00000000, // nop
    SidekickVelocityLateralResumeJump,
    0x00000000, // nop
};

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
    WaterWakeLegacyCallSite = A.WaterWakeLegacyCallSite;
    ObjectMoveEntry = A.ObjectMoveEntry;
    ObjectMoveResume = A.ObjectMoveResume;
    WaterWakeCullingEntry = A.WaterWakeCullingEntry;
    WaterWakeStockDrawEntry = A.WaterWakeStockDrawEntry;
    WaterWakeDrawFallbackEntry = A.WaterWakeDrawFallbackEntry;
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
    SidekickControlEntry = A.SidekickControlEntry;
    SidekickControlProbeEntry = A.SidekickControlProbeEntry;
    SidekickVelocityLateralEntry = A.SidekickVelocityLateralEntry;
    SidekickVelocityLateralDelay = A.SidekickVelocityLateralDelay;
    SidekickVelocityLateralResume = A.SidekickVelocityLateralResume;
    SidekickLateralMoveInputLegacyEntry = A.SidekickLateralMoveInputLegacyEntry;
    SidekickStrafeEntry = A.SidekickStrafeEntry;
    SidekickStrafeDelay = A.SidekickStrafeDelay;
    SidekickLateralMoveOldTailEntry = A.SidekickLateralMoveOldTailEntry;
    SidekickLateralMoveEntry = A.SidekickLateralMoveEntry;
    SidekickControlEnd = A.SidekickControlEnd;
    PlayerVelocityEntry = A.PlayerVelocityEntry;
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
    TripleBufferRequest = A.TripleBufferRequest;
    PlayerVelocityStub = A.PlayerVelocityStub;
    FloydMoveHookStub = A.FloydMoveHookStub;
    SidekickStrafeStub = A.SidekickStrafeStub;
    SidekickControlProbeStub = A.SidekickControlProbeStub;
    SidekickPadProbeStub = A.SidekickPadProbeStub;
    ObjectMoveStub = A.ObjectMoveStub;
    FloydCameraLateralStub = A.FloydCameraLateralStub;
    SidekickLateralMoveStub = A.SidekickLateralMoveStub;
    SidekickVelocityLateralStub = A.SidekickVelocityLateralStub;
    LandingCinematicSkipStub = A.LandingCinematicSkipStub;
    WaterWakeRingRateEntry = A.WaterWakeRingRateEntry;
    WaterWakeUpdate = A.WaterWakeUpdate;
    WaterWakeFrameRateEntry = A.WaterWakeFrameRateEntry;
    CameraAngleHelper = A.CameraAngleHelper;
    CameraHelperBase = A.CameraHelperBase;
    CameraTopDownHelperBase = A.CameraTopDownHelperBase;
    CameraNativeYAddress = A.CameraNativeYAddress;
    CameraHeightOffsetAddress = A.CameraHeightOffsetAddress;
    CameraTopDownCounterAddress = A.CameraTopDownCounterAddress;
    SidekickControlObjectAddress = A.SidekickControlObjectAddress;
    DroneLateralMaxSpeedAddress = A.DroneLateralMaxSpeedAddress;
    DroneLateralSideFactorAddress = A.DroneLateralSideFactorAddress;
    DroneLateralVelocityAddress = A.DroneLateralVelocityAddress;
    DroneLateralRightZAddress = A.DroneLateralRightZAddress;
    FloydCameraPreviousXAddress = A.FloydCameraPreviousXAddress;
    FloydCameraPreviousZAddress = A.FloydCameraPreviousZAddress;
    FloydCameraPreviousObjectAddress = A.FloydCameraPreviousObjectAddress;
    LandingCinematicSkipInputAddress = A.LandingCinematicSkipInputAddress;
    DroneLateralDragAddress = A.DroneLateralDragAddress;
    DroneLateralForwardSpeedAddress = A.DroneLateralForwardSpeedAddress;
    DroneLateralRightXAddress = A.DroneLateralRightXAddress;
    SidekickPadProbeStateAddress = A.SidekickPadProbeStateAddress;
    DroneLateralPreviousXAddress = A.DroneLateralPreviousXAddress;
    DroneLateralPreviousZAddress = A.DroneLateralPreviousZAddress;
    DroneLateralPreviousObjectAddress = A.DroneLateralPreviousObjectAddress;
    SidekickPadProbeObjectAddress = A.SidekickPadProbeObjectAddress;
    EnemyHalveFlagAddress = A.EnemyHalveFlagAddress;
    DroneLateralFlagsAddress = A.DroneLateralFlagsAddress;
    DroneLateralHookHitsAddress = A.DroneLateralHookHitsAddress;
    WaterWakeGateCounter = A.WaterWakeGateCounter;
    DroneLateralHookFlagsAddress = A.DroneLateralHookFlagsAddress;
    WaterWakeDrawFallbackCalledAddress = A.WaterWakeDrawFallbackCalledAddress;
    WaterWakeStockDrawTargetAddress = A.WaterWakeStockDrawTargetAddress;
    WaterWakeStockDrawCalledAddress = A.WaterWakeStockDrawCalledAddress;
    SidekickPadProbeActorAddress = A.SidekickPadProbeActorAddress;
    WaterWakeGateStub = A.WaterWakeGateStub;
    WaterWakeDrawFallbackStub = A.WaterWakeDrawFallbackStub;
    SquaddieXStub = A.SquaddieXStub;
    SquaddieZStub = A.SquaddieZStub;
    RobotMissionAddress = A.RobotMissionAddress;
    WaterWakeGlobalFadeAddress = A.WaterWakeGlobalFadeAddress;
    WaterWakeObjectListAddress = A.WaterWakeObjectListAddress;
    WaterWakeObjectCountAddress = A.WaterWakeObjectCountAddress;
    PlayerListAddress = A.PlayerListAddress;
    PlayerCountAddress = A.PlayerCountAddress;
    GeneralRenderListAddress = A.GeneralRenderListAddress;
    DisableJoyAddress = A.DisableJoyAddress;
    ControlCameraAddress = A.ControlCameraAddress;
    CameraActiveOverrideBase = A.CameraActiveOverrideBase;
    CameraArrayAddress = A.CameraArrayAddress;
    CameraFovAddress = A.CameraFovAddress;
    LobbyCameraInUseAddress = A.LobbyCameraInUseAddress;
    StaticCameraInUseAddress = A.StaticCameraInUseAddress;
    OverlayTableAddress = A.OverlayTableAddress;
    TripleBufferActive = A.TripleBufferActive;
    CurrentScreenAddress = A.CurrentScreenAddress;
    AnimseqCameraAddress = A.AnimseqCameraAddress;

    // Recomputed rather than tabulated, so they cannot drift from the addresses.
    WaterWakeUpdateCall = 0x0C000000 | ((WaterWakeUpdate >> 2) & 0x03FFFFFF);
    WaterWakeGateJump = 0x0C000000 | ((WaterWakeGateStub >> 2) & 0x03FFFFFF);
    WaterWakeCullingStub = WaterWakeGateStub;
    WaterWakeDrawTargetAddress = WaterWakeGateCounter;
    WaterWakeFrameRateStub = WaterWakeGateStub;
    WaterWakeStockDrawStub = WaterWakeGateStub;
    LandingCinematicSkipStubTable = LandingCinematicSkipStub + 0x160;
    IntroCinematicSkipJump = JumpTo(IntroCinematicSkipStub);

    // Build-specific instruction words, and the three replacements derived from
    // them so they follow whichever registers the compile happened to choose.
    CameraHelperCallWord = A.CameraHelperCallWord;
    ManualAimCursorXStoreWord = A.ManualAimCursorXStoreWord;
    ManualAimCursorYStoreWord = A.ManualAimCursorYStoreWord;
    SchedulerSignatureWord0 = A.SchedulerSignatureWord0;
    SchedulerSignatureWord2 = A.SchedulerSignatureWord2;
    FramePacing60SignatureWord0 = A.FramePacing60SignatureWord0;
    FramePacingSignatureWord1 = A.FramePacingSignatureWord1;
    CameraHelperCallReplacement = 0x0C000000 | ((CameraHelperBase >> 2) & 0x03FFFFFF);
    ManualAimCursorXStoreClear = ManualAimCursorXStoreWord & ~(0x1Fu << 16);
    ManualAimCursorYStoreClear = ManualAimCursorYStoreWord & ~(0x1Fu << 16);

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
    CameraCodePatches[10] = { ManualAimCursorXStore, ManualAimCursorXStoreWord, ManualAimCursorXStoreClear, false };
    CameraCodePatches[11] = { ManualAimCursorYStore, ManualAimCursorYStoreWord, ManualAimCursorYStoreClear, false };
    CameraCodePatches[12] = { ManualAimXVelocityStore, 0xE60401E4, 0xE60801E4, false };
    CameraCodePatches[13] = { ManualAimYVelocityStore, 0xE60401E8, 0xE60801E8, false };
    CameraCodePatches[14] = { CameraHeightBlendBase + 0x00, 0x8D230000, WithHi(0x3C010000, CameraHeightOffsetAddress), true };
    CameraCodePatches[15] = { CameraHeightBlendBase + 0x04, 0xC7A40090, 0xC7A40090, true };
    CameraCodePatches[16] = { CameraHeightBlendBase + 0x08, 0xC46C0010, WithLo(0xE4240000, CameraNativeYAddress), true };
    CameraCodePatches[17] = { CameraHeightBlendBase + 0x0C, 0xC7A800A8, WithLo(0xC4260000, CameraHeightOffsetAddress), true };
    CameraCodePatches[18] = { CameraHeightBlendBase + 0x10, 0x460C2181, 0x46062100, true };
    CameraCodePatches[19] = { CameraHeightBlendBase + 0x14, 0x46083482, 0xC46C0010, true };
    CameraCodePatches[20] = { CameraHeightBlendBase + 0x18, 0x460C9280, 0x460C2181, true };
    CameraCodePatches[21] = { CameraHeightBlendBase + 0x1C, 0xE46A0010, 0x46083482, true };
    CameraCodePatches[22] = { CameraHeightBlendBase + 0x20, 0x8D230000, 0x460C9280, true };
    CameraCodePatches[23] = { CameraHeightBlendBase + 0x24, 0xC7A40094, 0xE46A0010, true };
    CameraCodePatches[24] = { CameraHeightBlendBase + 0x28, 0xC4620014, 0xC7A40094, true };
    CameraCodePatches[25] = { CameraHeightBlendBase + 0x2C, 0xC7A800A8, 0xC4620014, true };
    CameraCodePatches[26] = { CameraHeightBlendBase + 0x30, 0x46022181, 0x46022181, true };
    CameraCodePatches[27] = { CameraHeightBlendBase + 0x34, 0x46083482, 0x46083482, true };
    CameraCodePatches[28] = { CameraHeightBlendBase + 0x38, 0x46029280, 0x46029280, true };
    CameraCodePatches[29] = { CameraHeightBlendBase + 0x3C, 0xE46A0014, 0xE46A0014, true };
    CameraCodePatches[30] = { CameraLookHelperCall + 0x0C, 0x44802000, 0xC7A40098, true };
    CameraCodePatches[31] = { CameraLookHelperCall + 0x38, 0x44805000, 0xC7AA00A0, true };
    CameraCodePatches[32] = { CameraYawHelperCall + 0x14, 0xA5C50000, 0xA5C20000, true };
    CameraCodePatches[33] = { CameraTopDownEntry + 0x00, 0x44866000, JumpTo(CameraTopDownHelperBase), false };
    CameraCodePatches[34] = { CameraTopDownEntry + 0x04, 0x3C06800F, 0x44866000, false };
    CameraCodePatches[35] = { CameraTopDownHelperBase + 0x00, 0x00000000, WithHi(0x3C080000, CameraTopDownCounterAddress), false };
    CameraCodePatches[36] = { CameraTopDownHelperBase + 0x04, 0x00000000, WithLo(0x8D090000, CameraTopDownCounterAddress), false };
    // A lone lui: its low half is supplied by the game code jumped to below, so
    // there is no pair here to name the global. Both builds keep their 0x800F
    // globals in the same 64K page, so the upper half is the same either way.
    CameraCodePatches[37] = { CameraTopDownHelperBase + 0x08, 0x00000000, 0x3C06800F, false };
    CameraCodePatches[38] = { CameraTopDownHelperBase + 0x0C, 0x00000000, 0x25290001, false };
    CameraCodePatches[39] = { CameraTopDownHelperBase + 0x10, 0x00000000, JumpTo(CameraTopDownEntry + 0x08), false };
    CameraCodePatches[40] = { CameraTopDownHelperBase + 0x14, 0x00000000, WithLo(0xAD090000, CameraTopDownCounterAddress), false };
    CameraCodePatches[41] = { CameraHelperBase + 0x00, 0x00000000, 0x92080568, false };
    CameraCodePatches[42] = { CameraHelperBase + 0x04, 0x00000000, 0x3108FFFC, false };
    CameraCodePatches[43] = { CameraHelperBase + 0x08, 0x00000000, 0x00000000, false };
    CameraCodePatches[44] = { CameraHelperBase + 0x0C, 0x00000000, 0x15000003, false };
    CameraCodePatches[45] = { CameraHelperBase + 0x10, 0x00000000, 0x00A01025, false };
    CameraCodePatches[46] = { CameraHelperBase + 0x14, 0x00000000, 0x03E00008, false };
    CameraCodePatches[47] = { CameraHelperBase + 0x1C, 0x00000000, JumpTo(CameraAngleHelper), false };
    CameraCodePatches[48] = { CameraHelperBase + 0x20, 0x00000000, 0x92080568, false };
    CameraCodePatches[49] = { CameraHelperBase + 0x24, 0x00000000, 0x3108FFFC, false };
    CameraCodePatches[50] = { CameraHelperBase + 0x28, 0x00000000, 0x00000000, false };
    CameraCodePatches[51] = { CameraHelperBase + 0x2C, 0x00000000, 0x15000003, false };
    CameraCodePatches[52] = { CameraHelperBase + 0x30, 0x00000000, 0x8FA200F0, false };
    CameraCodePatches[53] = { CameraHelperBase + 0x34, 0x00000000, 0xAFA00098, false };
    CameraCodePatches[54] = { CameraHelperBase + 0x38, 0x00000000, 0xAFA000A0, false };
    CameraCodePatches[55] = { CameraHelperBase + 0x3C, 0x00000000, 0x03E00008, false };

    // Every jump word that encodes one of our own stub addresses, or a game
    // routine a stub returns to. These were the last thing still spelled in
    // US terms: an instruction word starts 0x08 or 0x0C, so the migration
    // that looked for 0x8xxxxxxx addresses walked straight past them.
    WaterWakeCullingJump = JumpTo(WaterWakeCullingStub);
    WaterWakeDrawFallbackJump = CallTo(WaterWakeDrawFallbackStub);
    WaterWakeFrameRateJump = JumpTo(WaterWakeFrameRateStub);
    WaterWakeStockDrawJump = CallTo(WaterWakeStockDrawStub);
    ObjectMoveJump = JumpTo(ObjectMoveStub);
    PlayerVelocityJump = JumpTo(PlayerVelocityStub);
    FloydMoveHookJump = JumpTo(FloydMoveHookStub);
    SidekickStrafeJump = JumpTo(SidekickStrafeStub);
    FloydCameraLateralJump = CallTo(FloydCameraLateralStub);
    SidekickLateralMoveJump = JumpTo(SidekickLateralMoveStub);
    SidekickVelocityLateralJump = JumpTo(SidekickVelocityLateralStub);
    SidekickControlProbeJump = JumpTo(SidekickControlProbeStub);
    SidekickPadProbeJump = JumpTo(SidekickPadProbeStub);
    SidekickLateralMovePreviousJump = JumpTo(SidekickControlProbeStub);
    LandingCinematicSkipJump = CallTo(LandingCinematicSkipStub);
    LegacyLandingCinematicSkipJump = JumpTo(FloydCameraLateralStub);
    LegacyLandingCinematicSkipCall = CallTo(FloydCameraLateralStub);
    LegacyIntroCinematicSkipCall = CallTo(IntroCinematicSkipStub);
    FloydMoveObjMoveCallOriginal = CallTo(ObjectMoveEntry - 0x20);
    SidekickControlProbeResumeJump = JumpTo(SidekickVelocityLateralResume);
    SidekickVelocityLateralResumeJump = JumpTo(SidekickVelocityLateralResume);
    SidekickStrafeResumeJump = JumpTo(SidekickStrafeDelay + 0x04);

    // The patch tables embed addresses, so they are rebuilt from the same
    // initialisers rather than duplicated here by hand.
    BoyAimPatches[0] = { 0x8035C7B8, 0x87A50056, 0x24050000 };
    BoyAimPatches[1] = { 0x8035C7BC, 0x860401CE, 0x00002025 };
    BoyAimPatches[2] = { 0x8035C7D0, 0x87A50054, 0x860501E2 };
    BoyAimPatches[3] = { 0x8035C7D4, 0x860401D0, 0x00A02025 };
    BoyAimPatches[4] = { 0x8035CD58, 0x87A50056, 0x24050000 };
    BoyAimPatches[5] = { 0x8035CD5C, 0x860401CE, 0x00002025 };
    BoyAimPatches[6] = { 0x8035CD70, 0x87A50054, 0x860501E2 };
    BoyAimPatches[7] = { 0x8035CD74, 0x860401D0, 0x00A02025 };
    BoyAimPatches[8] = { 0x8035C788, 0x860401DC, 0x00002025 };
    BoyAimPatches[9] = { 0x8035C78C, 0x87A50056, 0x24050000 };
    BoyAimPatches[10] = { 0x8035C7A0, 0x87A50054, 0x860501E2 };
    BoyAimPatches[11] = { 0x8035C7A4, 0x860401DE, 0x00A02025 };
    BoyAimPatches[12] = { 0x8035CD28, 0x860401DC, 0x00002025 };
    BoyAimPatches[13] = { 0x8035CD2C, 0x87A50056, 0x24050000 };
    BoyAimPatches[14] = { 0x8035CD40, 0x87A50054, 0x860501E2 };
    BoyAimPatches[15] = { 0x8035CD44, 0x860401DE, 0x00A02025 };
    FramePacingPatches[0] = { FramePacingEscalateStore, 0xA22D0000, 0x00000000 };
    FramePacing60Patches[0] = { FramePacing60Branch, 0x11C00002, 0x00000000 };
    SchedulerReleasePatches[0] = { SchedulerFrameGateAdd, A.SchedulerFrameGateAddWord, (A.SchedulerFrameGateAddWord + 1) };
    TripleBufferPatches[0] = { TripleBufferRequest, 0x308E0001, 0x240E0001 };
    WaterWakeRingRatePatches[0] = { WaterWakeRingRateEntry + 0x00, 0x00000000, 0x928D0002 };
    WaterWakeRingRatePatches[1] = { WaterWakeRingRateEntry + 0x08, 0x00000000, 0x01A36825 };
    WaterWakeRingRatePatches[2] = { WaterWakeRingRateEntry + 0x1C, 0x1020002E, 0x002D0824 };
    WaterWakeRingRatePatches[3] = { WaterWakeRingRateEntry + 0x20, 0x00000000, 0x1020002D };
    WaterWakeLegacyGatePatches[0] = { WaterWakeGateStub + 0x00, 0x00000000, 0x3C01800A };
    WaterWakeLegacyGatePatches[1] = { WaterWakeGateStub + 0x04, 0x00000000, WithLo(0x8C280000, DroneLateralFlagsAddress) };
    WaterWakeLegacyGatePatches[2] = { WaterWakeGateStub + 0x08, 0x00000000, 0x31080004 };
    WaterWakeLegacyGatePatches[3] = { WaterWakeGateStub + 0x0C, 0x00000000, 0x15000003 };
    WaterWakeLegacyGatePatches[4] = { WaterWakeGateStub + 0x10, 0x00000000, 0x00000000 };
    WaterWakeLegacyGatePatches[5] = { WaterWakeGateStub + 0x14, 0x00000000, 0x03E00008 };
    WaterWakeLegacyGatePatches[6] = { WaterWakeGateStub + 0x18, 0x00000000, 0x00000000 };
    WaterWakeLegacyGatePatches[7] = { WaterWakeGateStub + 0x1C, 0x00000000, 0x0801AC24 };
    WaterWakeLegacyGatePatches[8] = { WaterWakeGateStub + 0x20, 0x00000000, 0x00000000 };
    WaterWakeLegacyRatePatches[0] = { WaterWakeRingRateEntry - 0x8C, 0x24580040, 0x24580020 };
    WaterWakeLegacyRatePatches[1] = { WaterWakeRingRateEntry - 0x3C, 0x25CFFFC0, 0x25CFFFE0 };
    WaterWakeLegacyRatePatches[2] = { WaterWakeUpdate + 0x38, 0x25F80020, 0x25F80010 };
    WaterWakeLegacyRatePatches[3] = { WaterWakeUpdate + 0x64, 0x254BFFE0, 0x254BFFF0 };
    WaterWakeGatePatches[0] = { WaterWakeGateStub + 0x00, 0x00000000, 0x3C018010 };
    WaterWakeGatePatches[1] = { WaterWakeGateStub + 0x04, 0x00000000, 0x8C28D7C0 };
    WaterWakeGatePatches[2] = { WaterWakeGateStub + 0x08, 0x00000000, 0x31080001 };
    WaterWakeGatePatches[3] = { WaterWakeGateStub + 0x0C, 0x00000000, 0x15000004 };
    WaterWakeGatePatches[4] = { WaterWakeGateStub + 0x10, 0x00000000, 0x00000000 };
    WaterWakeGatePatches[5] = { WaterWakeGateStub + 0x14, 0x00000000, 0x00000000 };
    WaterWakeGatePatches[6] = { WaterWakeGateStub + 0x18, 0x00000000, 0x0801AC24 };
    WaterWakeGatePatches[7] = { WaterWakeGateStub + 0x1C, 0x00000000, 0x00000000 };
    WaterWakeGatePatches[8] = { WaterWakeGateStub + 0x20, 0x00000000, 0x03E00008 };
    WaterWakeGatePatches[9] = { WaterWakeGateStub + 0x24, 0x00000000, 0x00000000 };
    WaterWakeGatePatches[10] = { WaterWakeLegacyCallSite, WaterWakeUpdateCall, WaterWakeGateJump };
    WaterWakeDrawProbePatches[0] = { WaterWakeDrawFallbackEntry, 0x0C01A2D5, 0x00000000 };
    WaterWakeCullingPatches[0] = { WaterWakeCullingStub + 0x00, 0x00000000, 0x8C480058 };
    WaterWakeCullingPatches[1] = { WaterWakeCullingStub + 0x04, 0x00000000, 0x11000008 };
    WaterWakeCullingPatches[2] = { WaterWakeCullingStub + 0x08, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[3] = { WaterWakeCullingStub + 0x0C, 0x00000000, 0x91090000 };
    WaterWakeCullingPatches[4] = { WaterWakeCullingStub + 0x10, 0x00000000, 0x24010040 };
    WaterWakeCullingPatches[5] = { WaterWakeCullingStub + 0x14, 0x00000000, 0x15210004 };
    WaterWakeCullingPatches[6] = { WaterWakeCullingStub + 0x18, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[7] = { WaterWakeCullingStub + 0x1C, 0x00000000, 0x8FAE0064 };
    WaterWakeCullingPatches[8] = { WaterWakeCullingStub + 0x20, 0x00000000, 0x080051C6 };
    WaterWakeCullingPatches[9] = { WaterWakeCullingStub + 0x24, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[10] = { WaterWakeCullingStub + 0x28, 0x00000000, 0x11A00008 };
    WaterWakeCullingPatches[11] = { WaterWakeCullingStub + 0x2C, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[12] = { WaterWakeCullingStub + 0x30, 0x00000000, 0x0C0057B6 };
    WaterWakeCullingPatches[13] = { WaterWakeCullingStub + 0x34, 0x00000000, 0x00402021 };
    WaterWakeCullingPatches[14] = { WaterWakeCullingStub + 0x38, 0x00000000, 0x10400004 };
    WaterWakeCullingPatches[15] = { WaterWakeCullingStub + 0x3C, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[16] = { WaterWakeCullingStub + 0x40, 0x00000000, 0x8FAE0064 };
    WaterWakeCullingPatches[17] = { WaterWakeCullingStub + 0x44, 0x00000000, 0x080051C6 };
    WaterWakeCullingPatches[18] = { WaterWakeCullingStub + 0x48, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[19] = { WaterWakeCullingStub + 0x4C, 0x00000000, 0x080051BF };
    WaterWakeCullingPatches[20] = { WaterWakeCullingStub + 0x50, 0x00000000, 0x00000000 };
    WaterWakeCullingPatches[21] = { WaterWakeCullingEntry, 0x11A00005, WaterWakeCullingJump };
    WaterWakeDrawFallbackPatches[0] = { WaterWakeDrawFallbackStub + 0x00, 0x00000000, 0xAFBF0040 };
    WaterWakeDrawFallbackPatches[1] = { WaterWakeDrawFallbackStub + 0x04, 0x00000000, 0x02C02021 };
    WaterWakeDrawFallbackPatches[2] = { WaterWakeDrawFallbackStub + 0x08, 0x00000000, 0x0C0156E0 };
    WaterWakeDrawFallbackPatches[3] = { WaterWakeDrawFallbackStub + 0x0C, 0x00000000, 0x00000000 };
    WaterWakeDrawFallbackPatches[4] = { WaterWakeDrawFallbackStub + 0x10, 0x00000000, 0x3C08800A };
    WaterWakeDrawFallbackPatches[5] = { WaterWakeDrawFallbackStub + 0x14, 0x00000000, WithLo(0x8D040000, DroneLateralHookFlagsAddress) };
    WaterWakeDrawFallbackPatches[6] = { WaterWakeDrawFallbackStub + 0x18, 0x00000000, 0x24090001 };
    WaterWakeDrawFallbackPatches[7] = { WaterWakeDrawFallbackStub + 0x1C, 0x00000000, WithLo(0xAD090000, WaterWakeDrawFallbackCalledAddress) };
    WaterWakeDrawFallbackPatches[8] = { WaterWakeDrawFallbackStub + 0x20, 0x00000000, 0x10800004 };
    WaterWakeDrawFallbackPatches[9] = { WaterWakeDrawFallbackStub + 0x24, 0x00000000, 0x00000000 };
    WaterWakeDrawFallbackPatches[10] = { WaterWakeDrawFallbackStub + 0x28, 0x00000000, 0x02C02821 };
    WaterWakeDrawFallbackPatches[11] = { WaterWakeDrawFallbackStub + 0x2C, 0x00000000, 0x0C01ADA7 };
    WaterWakeDrawFallbackPatches[12] = { WaterWakeDrawFallbackStub + 0x30, 0x00000000, 0x00000000 };
    WaterWakeDrawFallbackPatches[13] = { WaterWakeDrawFallbackStub + 0x34, 0x00000000, 0x8FBF0040 };
    WaterWakeDrawFallbackPatches[14] = { WaterWakeDrawFallbackStub + 0x38, 0x00000000, 0x0800532A };
    WaterWakeDrawFallbackPatches[15] = { WaterWakeDrawFallbackStub + 0x3C, 0x00000000, 0x00000000 };
    WaterWakeDrawFallbackPatches[16] = { WaterWakeDrawFallbackStub + 0x44, 0x00000000, 0x00000000 };
    WaterWakeDrawFallbackPatches[17] = { WaterWakeDrawFallbackEntry, 0x0C01A2D5, WaterWakeDrawFallbackJump };
    WaterWakeFrameRatePatches[0] = { WaterWakeFrameRateStub + 0x00, 0x00000000, 0x314C0001 };
    WaterWakeFrameRatePatches[1] = { WaterWakeFrameRateStub + 0x04, 0x00000000, 0x11800004 };
    WaterWakeFrameRatePatches[2] = { WaterWakeFrameRateStub + 0x08, 0x00000000, 0x00000000 };
    WaterWakeFrameRatePatches[3] = { WaterWakeFrameRateStub + 0x0C, 0x00000000, 0x01406021 };
    WaterWakeFrameRatePatches[4] = { WaterWakeFrameRateStub + 0x10, 0x00000000, 0x0801AC69 };
    WaterWakeFrameRatePatches[5] = { WaterWakeFrameRateStub + 0x14, 0x00000000, 0x00000000 };
    WaterWakeFrameRatePatches[6] = { WaterWakeFrameRateStub + 0x18, 0x00000000, 0x016A6023 };
    WaterWakeFrameRatePatches[7] = { WaterWakeFrameRateStub + 0x1C, 0x00000000, 0x0801AC69 };
    WaterWakeFrameRatePatches[8] = { WaterWakeFrameRateStub + 0x20, 0x00000000, 0x00000000 };
    WaterWakeFrameRatePatches[9] = { WaterWakeFrameRateEntry, 0x016A6023, WaterWakeFrameRateJump };
    WaterWakeStockDrawProbePatches[0] = { WaterWakeStockDrawStub + 0x00, 0x00000000, 0x3C08800A };
    WaterWakeStockDrawProbePatches[1] = { WaterWakeStockDrawStub + 0x04, 0x00000000, WithLo(0xAD040000, WaterWakeStockDrawTargetAddress) };
    WaterWakeStockDrawProbePatches[2] = { WaterWakeStockDrawStub + 0x08, 0x00000000, 0x24090001 };
    WaterWakeStockDrawProbePatches[3] = { WaterWakeStockDrawStub + 0x0C, 0x00000000, WithLo(0xAD090000, WaterWakeStockDrawCalledAddress) };
    WaterWakeStockDrawProbePatches[4] = { WaterWakeStockDrawStub + 0x10, 0x00000000, 0x0801ADA7 };
    WaterWakeStockDrawProbePatches[5] = { WaterWakeStockDrawStub + 0x14, 0x00000000, 0x00000000 };
    WaterWakeStockDrawProbePatches[6] = { WaterWakeStockDrawEntry, 0x0C01ADA7, WaterWakeStockDrawJump };
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
    PlayerVelocityPatches[0] = { PlayerVelocityStub + 0x00, 0x00000000, WithHi(0x3C010000, DroneLateralFlagsAddress) };
    PlayerVelocityPatches[1] = { PlayerVelocityStub + 0x04, 0x00000000, WithLo(0x8C290000, DroneLateralFlagsAddress) };
    PlayerVelocityPatches[2] = { PlayerVelocityStub + 0x08, 0x00000000, 0x312A0001 };
    PlayerVelocityPatches[3] = { PlayerVelocityStub + 0x0C, 0x00000000, 0x11400010 };
    PlayerVelocityPatches[4] = { PlayerVelocityStub + 0x10, 0x00000000, 0x8FA80024 };
    PlayerVelocityPatches[5] = { PlayerVelocityStub + 0x14, 0x00000000, WithLo(0x8C2B0000, DroneLateralHookHitsAddress) };
    PlayerVelocityPatches[6] = { PlayerVelocityStub + 0x18, 0x00000000, 0x256B0001 };
    PlayerVelocityPatches[7] = { PlayerVelocityStub + 0x1C, 0x00000000, WithLo(0xAC2B0000, DroneLateralHookHitsAddress) };
    PlayerVelocityPatches[8] = { PlayerVelocityStub + 0x20, 0x00000000, 0xC5000018 };
    PlayerVelocityPatches[9] = { PlayerVelocityStub + 0x24, 0x00000000, 0x312A0002 };
    PlayerVelocityPatches[10] = { PlayerVelocityStub + 0x28, 0x00000000, 0x11400006 };
    PlayerVelocityPatches[11] = { PlayerVelocityStub + 0x2C, 0x00000000, 0xC5020020 };
    PlayerVelocityPatches[12] = { PlayerVelocityStub + 0x30, 0x00000000, 0x46001107 };
    PlayerVelocityPatches[13] = { PlayerVelocityStub + 0x34, 0x00000000, 0xE5040018 };
    PlayerVelocityPatches[14] = { PlayerVelocityStub + 0x38, 0x00000000, 0xE5000020 };
    PlayerVelocityPatches[15] = { PlayerVelocityStub + 0x3C, 0x00000000, 0x10000004 };
    PlayerVelocityPatches[16] = { PlayerVelocityStub + 0x40, 0x00000000, 0x00000000 };
    PlayerVelocityPatches[17] = { PlayerVelocityStub + 0x44, 0x00000000, 0xE5020018 };
    PlayerVelocityPatches[18] = { PlayerVelocityStub + 0x48, 0x00000000, 0x46000107 };
    PlayerVelocityPatches[19] = { PlayerVelocityStub + 0x4C, 0x00000000, 0xE5040020 };
    PlayerVelocityPatches[20] = { PlayerVelocityStub + 0x50, 0x00000000, 0x8FBF0014 };
    PlayerVelocityPatches[21] = { PlayerVelocityStub + 0x54, 0x00000000, 0x27BD0020 };
    PlayerVelocityPatches[22] = { PlayerVelocityStub + 0x58, 0x00000000, 0x03E00008 };
    PlayerVelocityPatches[23] = { PlayerVelocityStub + 0x5C, 0x00000000, 0x00000000 };
    PlayerVelocityPatches[24] = { PlayerVelocityEntry + 0x00, 0x8FBF0014, PlayerVelocityJump };
    PlayerVelocityPatches[25] = { PlayerVelocityEntry + 0x04, 0x27BD0020, 0x00000000 };
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
    m_CameraOverrideActive(false),
    m_CameraOverrideSuspended(false),
    m_TrackedCamera(0),
    m_TrackedPlayerObject(0),
    m_OrbitYawInitialized(false),
    m_CameraElevationReady(false),
    m_CameraHeightOffset(0.0f),
    m_OrbitYaw(0),
    m_AimFovReference(0.0f),
    m_AimYawCarry(0.0f),
    m_AimPitchCarry(0.0f),
    m_BossAimReticleX(0),
    m_BossAimYawApplied(0),
    m_TopDownCounterInitialized(false),
    m_TopDownCounter(0),
    m_TopDownHoldPolls(0),
    m_MouseDeltaX(0),
    m_MouseDeltaY(0),
    m_QueuedMouseWheel(0),
    m_FramePacingPatchApplied(false),
    m_FramePacing60PatchApplied(false),
    m_SchedulerReleasePatchApplied(false),
    m_TripleBufferPatchApplied(false),
    m_WaterWakeRatePatchApplied(false),
    m_WaterWakeRingRatePatchApplied(false),
    m_WaterWakeDrawProbeApplied(false),
    m_WaterWakeCullingPatchApplied(false),
    m_WaterWakeDrawFallbackPatchApplied(false),
    m_WaterWakeFrameRatePatchApplied(false),
    m_WaterWakeStockDrawProbeApplied(false),
    m_WaterWakeRatePatchStatus(0),
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
    m_DroneLateralRight(false),
    m_DroneLateralApplied(false),
    m_DroneLateralState(0),
    m_DroneLateralDebugStatus(0),
    m_DroneLateralPositionValid(false),
    m_DroneLateralObject(0),
    m_DroneLateralPreviousX(0.0f),
    m_DroneLateralPreviousZ(0.0f),
    m_DroneLateralHookHits(0),
    m_DroneLateralControllerEntry(0),
    m_DroneLateralControllerWord0(0),
    m_DroneLateralControllerWord1(0),
    m_DroneLateralControllerWord2(0),
    m_DroneLateralControllerWord3(0),
    m_DroneLateralControllerReturn(0),
    m_DroneLateralControllerReturnWord0(0),
    m_DroneLateralControllerReturnWord1(0),
    m_DroneLateralControllerReturnWord2(0),
    m_DroneLateralControllerReturnWord3(0),
    m_DroneLateralControllerReturnWord4(0),
    m_DroneLateralMoveHookApplied(false),
    m_DroneLateralMoveHookEntry(0),
    m_FloydCameraLateralHookApplied(false),
    m_FloydCameraLateralHookEntry(0),
    m_SidekickVelocityLateralHookApplied(false),
    m_SidekickLateralMoveHookApplied(false),
    m_SidekickStrafeHookApplied(false),
    m_SidekickPadControlProbeApplied(false),
    m_SidekickPadControlProbeEntry(0),
    m_BaseViRefreshRate(0),
    m_ObjectMovePatchApplied(false),
    m_PlayerVelocityPatchApplied(false),
    m_SquaddieMovePatchApplied(false),
    m_SquaddieOverlayBase(0),
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
    m_CinematicProbeDown(false),
    m_Fps60ToggleDown(false),
    m_Fps30ToggleDown(false),
    m_SyncAudioEnabledState(-1),
    m_HalveFrameCounter(0),
    m_WidescreenHudScopeForceDown(false),
    m_WidescreenHudScopeForced(false),
    m_InputRateWindowValid(false),
    m_InputRateSamples(0),
    m_FrameSwaps(0),
    m_LastCurrentScreen(0)
{
    memset(m_HalvedEnemySlots, 0, sizeof(m_HalvedEnemySlots));
    if (CinematicProbeEnabled)
    {
        OpenCinematicProbeLogSession();
    }
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
    PatchHudAlignment(false, false);
    PatchWidescreenHud(false);
    PatchLandingCinematicSkip(false);
    PatchIntroCinematicSkip(false);
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, 0);
    PatchTripleBuffer(false);
    PatchWaterWakeRate(false);
    PatchWaterWakeRingRate(false);
    PatchWaterWakeDrawProbe(false);
    PatchWaterWakeCulling(false);
    PatchWaterWakeDrawFallback(false);
    PatchWaterWakeFrameRate(false);
    PatchWaterWakeStockDrawProbe(false);
    PatchObjectMove(false);
    PatchDroneLateralMove(false);
    PatchFloydCameraLateralMove(false);
    PatchSidekickVelocityLateralMove(false);
    PatchSidekickLateralMove(false);
    PatchSidekickStrafe(false);
    PatchSidekickPadControlProbe(false);
    PatchPlayerVelocity(false);
    PatchSquaddieMove(false);
    PatchSquadsTimeStep(false);
    m_Memory.WriteU32(DroneLateralFlagsAddress, 0);
    m_Memory.WriteF32(DroneLateralSideFactorAddress, 0.0f);
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);

    m_SprintApplied = false;
    m_SprintPositionValid = false;
    m_SprintPlayerObject = 0;
    m_SprintTimeValid = false;
    m_SprintBlend = 0.0f;
    m_SprintAnimationApplied = false;
    m_SprintAnimationValid = false;
    m_SprintAnimation = 0;
    m_DroneLateralApplied = false;
    m_DroneLateralPositionValid = false;
    m_DroneLateralObject = 0;
    m_DroneLateralCandidates.clear();
}

void CJetForceGeminiRuntime::StateLoaded(void)
{
    PatchHudAlignment(false, false);
    PatchWidescreenHud(false);
    // States made by the first HUD prototype may contain an interrupted scope.
    // This byte is reserved alignment padding, so normalise it even when the
    // host-side ownership bookkeeping was reset before loading the state.
    if (IsSupportedRom() && JfgAddresses() == &JfgUsAddresses)
    {
        m_Memory.WriteU8(WidescreenHudScopeDepthAddress, 0);
    }
    PatchLandingCinematicSkip(false);
    PatchIntroCinematicSkip(false);
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, 0);
    RemoveLegacyLandingCinematicSkip();
    RemoveLegacyIntroCinematicSkip();
    ApplyViBudget(false);
    PatchTripleBuffer(false);
    PatchWaterWakeRate(false);
    PatchWaterWakeRingRate(false);
    PatchWaterWakeDrawProbe(false);
    PatchWaterWakeCulling(false);
    PatchWaterWakeDrawFallback(false);
    PatchWaterWakeFrameRate(false);
    PatchWaterWakeStockDrawProbe(false);
    PatchSquaddieMove(false);
    PatchSquadsTimeStep(false);
    ClearCameraState();
    m_MouseDeltaX = 0;
    m_MouseDeltaY = 0;
    m_SprintApplied = false;
    m_SprintPositionValid = false;
    m_SprintPlayerObject = 0;
    m_SprintTimeValid = false;
    m_SprintBlend = 0.0f;
    m_SprintAnimationApplied = false;
    m_SprintAnimationValid = false;
    m_SprintAnimation = 0;

    PatchObjectMove(false);
    PatchDroneLateralMove(false);
    PatchFloydCameraLateralMove(false);
    PatchSidekickVelocityLateralMove(false);
    PatchSidekickLateralMove(false);
    PatchSidekickStrafe(false);
    PatchSidekickPadControlProbe(false);
    PatchPlayerVelocity(false);
    m_Memory.WriteU32(DroneLateralFlagsAddress, 0);
    m_Memory.WriteF32(DroneLateralSideFactorAddress, 0.0f);
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
    m_DroneLateralApplied = false;
    m_DroneLateralPositionValid = false;
    m_DroneLateralObject = 0;
    m_DroneLateralCandidates.clear();
}

bool CJetForceGeminiRuntime::IsEnabled(void) const
{
    return m_Enabled;
}

// The mouse/keyboard option owns controller one completely. Query the setting
// directly instead of m_Enabled so the input plugin is suppressed from the
// first poll, before a level has made the runtime active.
bool CJetForceGeminiRuntime::UsesExclusiveInput(void) const
{
    return g_Settings->LoadBool(Setting_JfgKeyboardMouse) && IsSupportedRom();
}

bool CJetForceGeminiRuntime::SupportsCurrentRom(void) const
{
    return IsSupportedRom();
}

// Enhancements which do not consume keyboard/mouse state run on their own VI
// path. In particular, do not put this behind UpdateEnabledState(): that helper
// deliberately requires Setting_JfgKeyboardMouse and would uninstall an
// otherwise independent HUD option every frame. The prototype is intentionally
// exact-US-only until its overlay signatures have been established elsewhere.
void CJetForceGeminiRuntime::ProcessRuntimeFrame(void)
{
    const bool Supported = IsSupportedRom() && JfgAddresses() == &JfgUsAddresses;
    const bool WidescreenRequested = Supported && g_Settings->LoadBool(Setting_JfgWidescreenHud);
    const bool AlignmentRequested = Supported && g_Settings->LoadBool(Setting_JfgAlignHud);
    if (!WidescreenRequested && !AlignmentRequested)
    {
        PatchHudAlignment(false, false);
        PatchWidescreenHud(false);
        return;
    }

    // Never install the fixed hooks during the boot/front-end sequence. Their
    // trampoline storage is in the retired tail of JFG's CPU diagnostic code,
    // which is still executing during a cold boot. A loaded gameplay state
    // skips that initializer, which is why the old eager installation appeared
    // to work only after loading a state.
    //
    // Keep Options and boot screens stock. Once gameplay resumes, require the
    // live overlay and its four-word signature before touching either cave.
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
        ResolutionIndex <= 3 &&
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

    const bool EnableWidescreen = GameplayHudReady && WidescreenRequested &&
                                  IsWidescreenHudResolution(ResolutionIndex);
    const bool HudPatched = PatchWidescreenHud(EnableWidescreen);
    PatchHudAlignment(GameplayHudReady && AlignmentRequested && HudPatched,
                      EnableWidescreen && HudPatched);

    // Republish the override every frame: the scope-exit stub decrements this
    // byte on every HUD pass. Seed it well above zero so no legitimate exit can
    // reach zero mid-frame and briefly close the scope again.
    if (EnableWidescreen && HudPatched &&
        m_WidescreenHudScopeForced && m_WidescreenHudScopeOwned)
    {
        m_Memory.WriteU8(WidescreenHudScopeDepthAddress, 0x20);
    }
}

// Called when the game reads the controller, so the buttons it gets are live
// rather than a snapshot: joyRead detects a jump on the rising edge between two
// completed reads, and quantising the state to the video interrupt made two
// consecutive reads land on the same value often enough to swallow the edge.
// The mouse delta is banked here because reading it also consumes it.
void CJetForceGeminiRuntime::ProcessController(
    int32_t Control, const KEYBOARD_MOUSE_STATE & Input, BUTTONS & Buttons)
{
    if (!UpdateEnabledState())
    {
        return;
    }
    if (Control == 0 && Input.Size >= sizeof(KEYBOARD_MOUSE_STATE))
    {
        // Arm the guest hook in the same controller-read path that maps E and
        // Return, and publish those physical keys for the MIPS stub before the
        // game reaches its next joyGetButtons call.
        const bool CinematicSkipRequested =
            g_Settings->LoadBool(Setting_JfgFastCutscenes) &&
            (KeyDown(Input, KeyboardMouseKey_E) || KeyDown(Input, KeyboardMouseKey_Return));
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

        if (CinematicProbeEnabled)
        {
            const bool CinematicProbeDown = KeyDown(Input, CinematicProbeKey);
            if (CinematicProbeDown && !m_CinematicProbeDown)
            {
                DisplayCinematicProbe();
            }
            m_CinematicProbeDown = CinematicProbeDown;
        }

        const bool ScopeForceDown = KeyDown(Input, WidescreenHudScopeForceKey);
        if (ScopeForceDown && !m_WidescreenHudScopeForceDown)
        {
            m_WidescreenHudScopeForced = !m_WidescreenHudScopeForced;
            g_Notify->DisplayMessage(
                0, m_WidescreenHudScopeForced ? "JFG HUD scope forced open"
                                              : "JFG HUD scope restored");
        }
        m_WidescreenHudScopeForceDown = ScopeForceDown;

        // Numpad +/- switch the frame-rate target live. Flip the setting only on
        // the key's rising edge, and only when it actually changes, so a held
        // key does not spam saves; the notification still confirms every press.
        const bool Fps60ToggleDown = KeyDown(Input, Fps60ToggleKey);
        if (Fps60ToggleDown && !m_Fps60ToggleDown)
        {
            if (!g_Settings->LoadBool(Setting_JfgTarget60Fps))
            {
                g_Settings->SaveBool(Setting_JfgTarget60Fps, true);
            }
            g_Notify->DisplayMessage(0, "JFG 60 FPS");
        }
        m_Fps60ToggleDown = Fps60ToggleDown;

        const bool Fps30ToggleDown = KeyDown(Input, Fps30ToggleKey);
        if (Fps30ToggleDown && !m_Fps30ToggleDown)
        {
            if (g_Settings->LoadBool(Setting_JfgTarget60Fps))
            {
                g_Settings->SaveBool(Setting_JfgTarget60Fps, false);
            }
            g_Notify->DisplayMessage(0, "JFG 30 FPS");
        }
        m_Fps30ToggleDown = Fps30ToggleDown;

        BankMouseDelta(Input);
        QueueMouseWheel(Input);

        KEYBOARD_MOUSE_STATE ControllerInput = Input;
        ControllerInput.MouseWheel = m_QueuedMouseWheel;
        m_QueuedMouseWheel = 0;
        MapController(ControllerInput, Buttons, false);
    }
}

// Called once per video interrupt, so the mouse camera runs at the render rate
// even while the game polls the controller at its own logic rate.
void CJetForceGeminiRuntime::ProcessVideoFrame(const KEYBOARD_MOUSE_STATE & Input, BUTTONS & Buttons)
{
    if (!UpdateEnabledState())
    {
        return;
    }
    if (Input.Size < sizeof(KEYBOARD_MOUSE_STATE))
    {
        return;
    }

    BankMouseDelta(Input);
    QueueMouseWheel(Input);
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
    PatchTripleBuffer(false);

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

    // A wheel notch is a one-poll A/B impulse. The video path can run before
    // that poll, so it queues the event but never consumes it itself.
    KEYBOARD_MOUSE_STATE VideoInput = Input;
    VideoInput.MouseWheel = 0;
    MapController(VideoInput, Buttons, true);
    UpdateSprintBlend();
    ApplySprint(PlayerObject);
    bool HalveEnemySpeed = Target60Fps && g_Settings->LoadBool(Setting_JfgHalveEnemySpeed);
    bool BoostViBudget = Target60Fps ? g_Settings->LoadBool(Setting_JfgBoostViBudget) :
                                      g_Settings->LoadBool(Setting_JfgBoostViBudget30);
    ApplyViBudget(BoostViBudget);
    m_Memory.WriteU32(EnemyHalveFlagAddress, HalveEnemySpeed ? 1 : 0);
    PatchObjectMove(HalveEnemySpeed);
    // The squads time step is the game's frame delta (2 at 30fps, 1 at 60fps),
    // so halving it ran the Squaddies' animation and timers in slow motion at
    // 60fps. Keep it native; movement slowing is handled off the delta above.
    PatchSquadsTimeStep(false);
    if (HalveEnemySpeed)
    {
        HalveNamedEnemyMovement();
    }
    PatchPlayerVelocity(false);
    UpdateDroneLateralControllerProbe();
    // sidekickpadMovePlayer moves the player towards the hover pad, not Floyd,
    // so its call-site hook is retired in favour of the sidekickControl one.
    PatchDroneLateralMove(false);
    PatchFloydCameraLateralMove(false);
    PatchSidekickVelocityLateralMove(false);
    PatchSidekickLateralMove(false);
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
    PatchSquaddieMove(false);
    PatchFramePacing(Target60Fps || g_Settings->LoadBool(Setting_JfgUncapFramePacing));
    PatchFramePacing60(Target60Fps);
    PatchSchedulerRelease(SchedulerRelease);
    PatchWaterWakeRate(false);
    PatchWaterWakeRingRate(Target60Fps);
    PatchWaterWakeStockDrawProbe(false);
    PatchWaterWakeDrawProbe(false);
    PatchWaterWakeCulling(false);
    PatchWaterWakeDrawFallback(false);
    PatchWaterWakeFrameRate(false);
    UpdateWaterWakeDrawTarget();
}

bool CJetForceGeminiRuntime::UpdateEnabledState(void)
{
    if (!g_Settings->LoadBool(Setting_JfgKeyboardMouse) || !IsSupportedRom())
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
    m_MouseDeltaX += Input.MouseX;
    m_MouseDeltaY += Input.MouseY;
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

// Applies a stub plus its hook entry, the entry being the last patch in the
// table. A save state can restore a stub built by an earlier build of this code,
// matching neither the original nor the current replacement, which makes the
// patcher refuse the whole table: the hook then stays live while we no longer
// feed it, and the game hangs. Take the entry down first, since a vanilla entry
// leaves the stub as dead code whatever it holds, and clear anything unexpected
// out of the stub before writing it back.
bool CJetForceGeminiRuntime::SetHookEnabled(const GAME_HACK_CODE_PATCH * Patches, size_t Count, bool Enabled)
{
    if (Count < 2)
    {
        return false;
    }
    const GAME_HACK_CODE_PATCH & Entry = Patches[Count - 1];

    if (!Enabled)
    {
        m_CodePatcher.SetEnabled(&Entry, 1, false);
        m_CodePatcher.SetEnabled(Patches, Count - 1, false);
        return true;
    }

    // Only overwrite unexpected stub words when the entry says the hook is
    // already ours, which means the stub is a stale build of it. Free memory was
    // only ever sampled at one instant, so forcing writes into it when the hook
    // is not installed could land on live game data.
    uint32_t EntryWord = 0;
    if (m_Memory.ReadU32(Entry.Address, EntryWord) && EntryWord == Entry.Replacement)
    {
        for (size_t i = 0; i + 1 < Count; i++)
        {
            uint32_t Current = 0;
            if (m_Memory.ReadU32(Patches[i].Address, Current) && Current != Patches[i].Original &&
                Current != Patches[i].Replacement)
            {
                m_Memory.WriteU32(Patches[i].Address, Patches[i].Original);
            }
        }
    }
    if (m_CodePatcher.SetEnabled(Patches, Count - 1, true) != CGameHackCodePatcher::Result_SignatureMismatch)
    {
        return m_CodePatcher.SetEnabled(&Entry, 1, true) != CGameHackCodePatcher::Result_SignatureMismatch;
    }
    return false;
}

// The landing-skip code cave belongs to a completed retail diagnostic routine,
// not a blank RDRAM page. Capture and restore it exactly, with the live entry
// always removed first, so save states can never contain this trampoline.
bool CJetForceGeminiRuntime::PatchLandingCinematicSkip(bool Enabled)
{
    const std::vector<uint32_t> StubImage = BuildLandingCinematicSkipImage();
    const size_t StubCount = StubImage.size();
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
            uint32_t HookWord = IntroCinematicSkipHookCode[i];
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
        if (!m_Memory.ReadU32(Base + Call.Offset, Current) ||
            !m_Memory.ReadU32(Base + Call.Offset + 4, Delay) || Delay != Call.Delay ||
            (Current != WidescreenHudReticleLineCallOriginal && Current != Replacement))
        {
            return false;
        }
        Patches.push_back({ Base + Call.Offset, WidescreenHudReticleLineCallOriginal, Replacement });
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
    for (const WIDESCREEN_HUD_BANNER_WORD_PATCH & Patch : WidescreenHudBannerPatches)
    {
        GAME_HACK_CODE_WRITE Write = {};
        Write.Address = OverlayBase + Patch.Offset;
        Write.Desired = !Enabled ? Patch.Original :
            (Resolution & 2) != 0 ? Patch.HighResolution : Patch.LowResolution;
        Write.Allowed[0] = Patch.Original;
        Write.Allowed[1] = Patch.LowResolution;
        Write.Allowed[2] = Patch.HighResolution;
        Write.AllowedCount = 3;
        Writes.push_back(Write);
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
        !m_Memory.IsRdramAddress(OverlayBase, 0x44CC) ||
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
        if (!m_Memory.ReadU32(OverlayBase + Legacy.Offset, Current) ||
            ((Current >> 16) != (Legacy.Original >> 16) &&
             (Current >> 16) != (Legacy.Replacement >> 16)))
        {
            return false;
        }
        GAME_HACK_CODE_WRITE Write = {};
        Write.Address = OverlayBase + Legacy.Offset;
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

// Remove the relocatable overlay hooks, banner and gauge before any fixed hook
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
    if (!SetWidescreenHudBanner(m_WidescreenHudOverlayBase, false))
    {
        return false;
    }
    if (!SetWidescreenHudShotGauge(m_WidescreenHudOverlayBase, false))
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
    if (!IsSupportedRom() || JfgAddresses() != &JfgUsAddresses)
    {
        return !Enabled && !HostOwned;
    }
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

    const size_t WordCount = (Layout::CaveEnd - Layout::CaveStart) / sizeof(uint32_t);
    std::vector<uint32_t> CurrentImage(WordCount);
    for (size_t i = 0; i < WordCount; i++)
    {
        if (!m_Memory.ReadU32(Layout::CaveStart + (uint32_t)(i * 4), CurrentImage[i]))
        {
            return false;
        }
    }
    const bool StockBody = std::equal(CurrentImage.begin(), CurrentImage.end(),
                                      JfgHudAlignmentOriginal::CaveWords);
    const bool StockGuard = WordIs(Layout::GuardAddress, JfgHudAlignmentOriginal::GuardWords[0]) &&
                            WordIs(Layout::GuardAddress + 4, JfgHudAlignmentOriginal::GuardWords[1]);
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
        const uint32_t BodyCall = CurrentImage[(Rdp::BodyCallAddress - Layout::CaveStart) / 4];
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
                    ResolutionIndex > 3 ||
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
    const bool Ready6 = TableReady && ValidBase(Base6, 0x0C3C) &&
                        PrologueIs(Base6, Sites::HealthFunctionOffset,
                                   Sites::HealthFunctionPrologue, 2);
    const bool Ready14 = TableReady && ValidBase(Base14, 0x2BA8) &&
                         PrologueIs(Base14, Sites::WeaponGroupFunctionOffset,
                                    Sites::WeaponGroupFunctionPrologue, 2);

    struct Reference
    {
        GAME_HACK_CODE_PATCH Patch;
        uint32_t Delay;
    };
    std::vector<Reference> References;
    auto Add = [&](uint32_t Base, const Sites::CallSite & Site, uint32_t Entry) {
        References.push_back({ { Base + Site.Offset, Site.Original, CallTo(Entry) }, Site.Delay });
    };
    References.push_back({ { Code::SpriteMatrixHookAddress, Code::SpriteMatrixHookOriginal,
                             CallTo(Code::SpriteMatrixEntry) }, Code::SpriteMatrixHookDelay });
    // The module table is authoritative. Never write into a cached allocation
    // after the loader has relocated/replaced that module, even on disable.
    if (TableReady && ValidBase(Base6, 0x0C3C))
    {
        Add(Base6, Sites::HealthSpriteCall, Code::SpriteHealthEntry);
        Add(Base6, Sites::HealthMatrixCall, Code::MatrixHealthEntry);
    }
    if (TableReady && ValidBase(Base14, 0x2BA8))
    {
        for (const auto & Site : Sites::WeaponSpriteCalls)
        {
            Add(Base14, Site, Code::SpriteWeaponEntry);
        }
        for (const auto & Site : Sites::WeaponMatrixCalls)
        {
            Add(Base14, Site, Code::MatrixWeaponEntry);
        }
        // Install the outer wrapper last, after the scoped sprite/matrix calls.
        References.push_back({ { Base14 + Sites::WeaponGroupCallOffset,
                                 CallTo(Base14 + Sites::WeaponGroupFunctionOffset),
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
        return Safe && (Base6 == 0 || ValidBase(Base6, 0x0C3C)) &&
               (Base14 == 0 || ValidBase(Base14, 0x2BA8));
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
            Write.Desired = JfgHudAlignmentOriginal::CaveWords[i];
            Write.Allowed[0] = CurrentImage[i];
            Write.AllowedCount = 1;
            Writes.push_back(Write);
        }
        if (!Succeeded(m_CodePatcher.Apply(Writes.data(), Writes.size())))
        {
            return false;
        }
        const GAME_HACK_CODE_PATCH Guard[] = {
            { Layout::GuardAddress, JfgHudAlignmentOriginal::GuardWords[0], Layout::GuardRetired[0] },
            { Layout::GuardAddress + 4, JfgHudAlignmentOriginal::GuardWords[1], Layout::GuardRetired[1] },
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
    if (!Layout::BuildImage(Image, Base14, ResolutionIndex, WidescreenCorrected))
    {
        return false;
    }
    const GAME_HACK_CODE_PATCH Guard[] = {
        { Layout::GuardAddress, JfgHudAlignmentOriginal::GuardWords[0], Layout::GuardRetired[0] },
        { Layout::GuardAddress + 4, JfgHudAlignmentOriginal::GuardWords[1], Layout::GuardRetired[1] },
    };
    if (!Succeeded(m_CodePatcher.SetEnabled(Guard, 2, true)))
    {
        return false;
    }
    m_HudAlignmentCaveOriginal.assign(JfgHudAlignmentOriginal::CaveWords,
                                     JfgHudAlignmentOriginal::CaveWords + WordCount);
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
        { 0x800590D0, 0x3C140400, 0x8FB40088 }, // lw s4, 0x88(sp), significant digits
        { 0x800590F4, 0x3694FC00, 0x00000000 }, // step already includes dtdy
        { 0x8005921C, 0x3C140400, 0x8FB40088 }, // lw s4, 0x88(sp), leading zeroes
        { 0x80059228, 0x3694FC00, 0x00000000 },
        { WidescreenHudAmmoEntry, WidescreenHudAmmoOriginal,
          JumpTo(WidescreenHudAmmoStub) },
    };
    FixedPatches.insert(FixedPatches.end(), AmmoPatches,
                        AmmoPatches + sizeof(AmmoPatches) / sizeof(AmmoPatches[0]));

    // Earlier builds patched the framebuffer digit renderer, one of them
    // through a cave trampoline. Both forms are returned to stock below before
    // anything else is touched, so they can never be mistaken for a live hook.
    const uint32_t WidescreenHudDigitalAdvanceLegacyJump =
        JumpTo(WidescreenHudDigitalAdvanceStub);
    auto RetireDeadDigitalPatches = [&]() {
        for (uint32_t i = 0; i < WidescreenHudDigitalRetiredCount; i++)
        {
            const GAME_HACK_CODE_PATCH & Patch = WidescreenHudDigitalRetired[i];
            uint32_t Current = 0;
            if (!m_Memory.ReadU32(Patch.Address, Current))
            {
                return false;
            }
            if (Current == Patch.Original)
            {
                continue;
            }
            GAME_HACK_CODE_PATCH Retire = Patch;
            if (Patch.Address == WidescreenHudDigitalAdvanceEntry &&
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

    auto CaveCodeMatches = [this](uint32_t Address, const uint32_t * Code, size_t Count) {
        if (Code == nullptr || Count == 0 ||
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

    auto CaptureCaveImage = [this]() {
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
        auto PlaceCode = [&Image, &Claimed](
                             uint32_t Address, const uint32_t * Code, size_t Count) {
            if ((Address & 3) != 0)
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

        if (!PlaceCode(WidescreenHudScopeEnterStub, WidescreenHudScopeEnterCode,
                       sizeof(WidescreenHudScopeEnterCode) / sizeof(WidescreenHudScopeEnterCode[0])) ||
            !PlaceCode(WidescreenHudScopeExitStub, WidescreenHudScopeExitCode,
                       sizeof(WidescreenHudScopeExitCode) / sizeof(WidescreenHudScopeExitCode[0])) ||
            !PlaceCode(WidescreenHudCamCopyStub, WidescreenHudCamCopyCode,
                       sizeof(WidescreenHudCamCopyCode) / sizeof(WidescreenHudCamCopyCode[0])) ||
            !PlaceCode(WidescreenHudFontYStub, WidescreenHudFontYCode,
                       sizeof(WidescreenHudFontYCode) / sizeof(WidescreenHudFontYCode[0])) ||
            !PlaceCode(WidescreenHudFontDtdyStub, WidescreenHudFontDtdyCode,
                       sizeof(WidescreenHudFontDtdyCode) / sizeof(WidescreenHudFontDtdyCode[0])) ||
            !PlaceCode(WidescreenHudSpriteScaleStub, WidescreenHudSpriteScaleCode,
                       sizeof(WidescreenHudSpriteScaleCode) / sizeof(WidescreenHudSpriteScaleCode[0])) ||
            !PlaceCode(WidescreenHudLineStub, WidescreenHudLineCode,
                       sizeof(WidescreenHudLineCode) / sizeof(WidescreenHudLineCode[0])) ||
            !PlaceCode(WidescreenHudRectangleStub, WidescreenHudRectangleCode,
                       sizeof(WidescreenHudRectangleCode) / sizeof(WidescreenHudRectangleCode[0])) ||
            !PlaceCode(WidescreenHudSpriteScaleAltStub, WidescreenHudSpriteScaleAltCode,
                       sizeof(WidescreenHudSpriteScaleAltCode) / sizeof(WidescreenHudSpriteScaleAltCode[0])) ||
            !PlaceCode(WidescreenHudSpritePositionStub, WidescreenHudSpritePositionCode,
                       sizeof(WidescreenHudSpritePositionCode) / sizeof(WidescreenHudSpritePositionCode[0])) ||
            !PlaceCode(WidescreenHudMatrixTranslateStub, WidescreenHudMatrixTranslateCode,
                       sizeof(WidescreenHudMatrixTranslateCode) / sizeof(WidescreenHudMatrixTranslateCode[0])) ||
            !PlaceCode(WidescreenHudAmmoStub, WidescreenHudAmmoCode,
                       sizeof(WidescreenHudAmmoCode) / sizeof(WidescreenHudAmmoCode[0])) ||
            !PlaceCode(WidescreenHudReticleStub, WidescreenHudReticleCode,
                       sizeof(WidescreenHudReticleCode) / sizeof(WidescreenHudReticleCode[0])) ||
            !PlaceCode(WidescreenHudShotGaugeWrapperStub, WidescreenHudShotGaugeWrapperCode,
                       sizeof(WidescreenHudShotGaugeWrapperCode) / sizeof(WidescreenHudShotGaugeWrapperCode[0])) ||
            !PlaceCode(WidescreenHudShotGaugeAnchorStub, WidescreenHudShotGaugeAnchorCode,
                       sizeof(WidescreenHudShotGaugeAnchorCode) / sizeof(WidescreenHudShotGaugeAnchorCode[0])))
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

        const bool LegacySpritePosition = CaveCodeMatches(
            WidescreenHudSpritePositionStub, WidescreenHudSpritePositionLegacyCode,
            sizeof(WidescreenHudSpritePositionLegacyCode) /
                sizeof(WidescreenHudSpritePositionLegacyCode[0]));
        const bool LegacyRectangle = CaveCodeMatches(
            WidescreenHudRectangleStub, WidescreenHudRectangleLegacyCode,
            sizeof(WidescreenHudRectangleLegacyCode) /
                sizeof(WidescreenHudRectangleLegacyCode[0]));
        std::vector<GAME_HACK_CODE_WRITE> Writes(WidescreenHudCaveWordCount);
        for (size_t i = 0; i < Writes.size(); i++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[i];
            Write.Address = WidescreenHudCaveWordAddress(i);
            Write.Desired = Install ? HookImage[i] : m_WidescreenHudCaveOriginal[i];
            Write.Allowed[0] = m_WidescreenHudCaveOriginal[i];
            Write.AllowedCount = 1;
            if (HookImage[i] != m_WidescreenHudCaveOriginal[i])
            {
                Write.Allowed[Write.AllowedCount++] = HookImage[i];
            }
            if (LegacySpritePosition && Write.Address >= WidescreenHudSpritePositionStub &&
                Write.Address < WidescreenHudSpritePositionStub + sizeof(WidescreenHudSpritePositionLegacyCode))
            {
                Write.Allowed[Write.AllowedCount++] = WidescreenHudSpritePositionLegacyCode[
                    (Write.Address - WidescreenHudSpritePositionStub) / sizeof(uint32_t)];
            }
            if (LegacyRectangle && Write.Address >= WidescreenHudRectangleStub &&
                Write.Address < WidescreenHudRectangleStub + sizeof(WidescreenHudRectangleLegacyCode))
            {
                Write.Allowed[Write.AllowedCount++] = WidescreenHudRectangleLegacyCode[
                    (Write.Address - WidescreenHudRectangleStub) / sizeof(uint32_t)];
            }
        }

        CGameHackCodePatcher::Result Result = m_CodePatcher.Apply(Writes.data(), Writes.size());
        return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
               Result != CGameHackCodePatcher::Result_MemoryUnavailable;
    };

    const bool ExactUsRom = IsSupportedRom() && JfgAddresses() == &JfgUsAddresses;
    if (Enabled)
    {
        uint8_t Resolution = 0;
        // Keep the mode check at the mutation boundary too, so another caller
        // cannot accidentally install any part of the patch in 4:3.
        Enabled = ExactUsRom &&
            m_Memory.ReadU8(WidescreenHudResolutionIndexAddress, Resolution) &&
            IsWidescreenHudResolution(Resolution);
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
    if (ExactUsRom && HasNoHudOwnership)
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
                WidescreenHudFontYStub, WidescreenHudFontYCode,
                sizeof(WidescreenHudFontYCode) / sizeof(WidescreenHudFontYCode[0]));
            uint32_t AmmoEntry = 0;
            const bool CompatibleAmmo = m_Memory.ReadU32(WidescreenHudAmmoEntry, AmmoEntry) &&
                (AmmoEntry == WidescreenHudAmmoOriginal ||
                 (AmmoEntry == JumpTo(WidescreenHudAmmoStub) &&
                  CaveCodeMatches(WidescreenHudAmmoStub, WidescreenHudAmmoCode,
                                  sizeof(WidescreenHudAmmoCode) / sizeof(WidescreenHudAmmoCode[0]))));
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
        const bool DigitalRemoved = !ExactUsRom || RetireDeadDigitalPatches();
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
            // current US overlay renderer before repairing that orphaned data.
            uint32_t Table = 0, Base = 0;
            uint32_t Enter = 0, EnterDelay = 0, Exit = 0, ExitDelay = 0;
            if (ExactUsRom &&
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
                return SetWidescreenHudShotGauge(Base, false) && DigitalRemoved;
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

    // Every address and instruction below is from the exact US executable.
    // Refuse Kiosk (and any later PAL/JP table) even though the wider runtime
    // supports it for unrelated features.
    if (JfgAddresses() != &JfgUsAddresses)
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
    // The exit goes live first, followed by the independent gauge caller. Only
    // once the exit can balance the depth counter may the entry redirect into
    // the scoped renderer.
    CGameHackCodePatcher::Result ExitResult =
        m_CodePatcher.SetEnabled(&ExitPatch, 1, true);
    if (ExitResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        ExitResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return false;
    }
    if (!SetWidescreenHudShotGauge(OverlayBase, true))
    {
        m_CodePatcher.SetEnabled(&ExitPatch, 1, false);
        return false;
    }
    if (!SetWidescreenHudBanner(OverlayBase, true))
    {
        SetWidescreenHudShotGauge(OverlayBase, false);
        m_CodePatcher.SetEnabled(&ExitPatch, 1, false);
        return false;
    }
    CGameHackCodePatcher::Result EnterResult =
        m_CodePatcher.SetEnabled(&EnterPatch, 1, true);
    if (EnterResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EnterResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        SetWidescreenHudBanner(OverlayBase, false);
        SetWidescreenHudShotGauge(OverlayBase, false);
        m_CodePatcher.SetEnabled(&ExitPatch, 1, false);
        return false;
    }

    m_WidescreenHudOverlayHookApplied = true;
    m_WidescreenHudOverlayBase = OverlayBase;
    return SetWidescreenHudReticle(true);
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

// Some flying Galaxian variants move through a per-baddy mover that the
// SquaddieControl step hook never reaches, so they stay full speed in 60 fps
// mode. Halve their world movement directly, by model name: each frame the game
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

// Records where the game stands at the instant the key is pressed. The scene
// and setup name the level, the next/character words say what a transition is
// heading for, and the animseq camera pointer is what separates the individual
// shots of a cinematic from each other and from ordinary play. Pressing once
// per shot and once more on the way out is what makes a run of these lines
// describe a whole cinematic.
void CJetForceGeminiRuntime::DisplayCinematicProbe(void)
{
    int16_t CurrentScene = 0;
    int16_t CurrentSetup = 0;
    int16_t NextLevel = 0;
    int16_t NextSetup = 0;
    int16_t NextCharacter = 0;
    int16_t NextFrontMode = 0;
    uint8_t Loading = 0;
    uint8_t FrontMode = 0;
    uint32_t AnimseqCamera = 0;

    m_Memory.ReadS16(CurrentSceneAddress, CurrentScene);
    m_Memory.ReadS16(CurrentSetupAddress, CurrentSetup);
    m_Memory.ReadS16(NextLevelAddress, NextLevel);
    m_Memory.ReadS16(NextSetupAddress, NextSetup);
    m_Memory.ReadS16(NextCharacterAddress, NextCharacter);
    m_Memory.ReadS16(NextFrontModeAddress, NextFrontMode);
    m_Memory.ReadU8(LoadingAddress, Loading);
    m_Memory.ReadU8(FrontModeAddress, FrontMode);
    m_Memory.ReadU32(AnimseqCameraAddress, AnimseqCamera);

    static uint32_t ProbeCount = 0;
    ProbeCount += 1;

    stdstr Message =
        stdstr_f("JFG P%03u scene%04X setup%04X next%04X nset%04X char%04X f%02X nf%04X load%u anim%08X",
                 ProbeCount, (uint16_t)CurrentScene, (uint16_t)CurrentSetup, (uint16_t)NextLevel,
                 (uint16_t)NextSetup, (uint16_t)NextCharacter, FrontMode,
                 (uint16_t)NextFrontMode, Loading, AnimseqCamera);

    g_Notify->DisplayMessage(
        0,
        Message.c_str());

    AppendCinematicProbeLog(Message);
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

// Like the object-movement dispatcher, the Floyd velocity hook occupies a
// dormant piece of game code rather than blank RAM. Preserve its original words
// so disabling the option, saving a state, or leaving the ROM restores it
// exactly. This also keeps it independent from the RSP microcode scratch page.
bool CJetForceGeminiRuntime::SetPlayerVelocityHook(bool Enabled)
{
    const size_t PatchCount = sizeof(PlayerVelocityPatches) / sizeof(PlayerVelocityPatches[0]);
    const size_t StubCount = PatchCount - 1;
    const GAME_HACK_CODE_PATCH & Entry = PlayerVelocityPatches[PatchCount - 1];

    auto BuildWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[i];
            Write.Address = PlayerVelocityPatches[i].Address;
            Write.Desired = Install ? PlayerVelocityPatches[i].Replacement : m_PlayerVelocityStubOriginal[i];
            Write.Allowed[0] = m_PlayerVelocityStubOriginal[i];
            Write.Allowed[1] = PlayerVelocityPatches[i].Replacement;
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
        if (m_PlayerVelocityStubOriginal.empty())
        {
            return true;
        }
        if (m_PlayerVelocityStubOriginal.size() != StubCount)
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
        m_PlayerVelocityStubOriginal.clear();
        return true;
    }

    if (m_PlayerVelocityStubOriginal.empty())
    {
        uint32_t EntryWord = 0;
        if (!m_Memory.ReadU32(Entry.Address, EntryWord) || EntryWord != Entry.Original)
        {
            return false;
        }
        m_PlayerVelocityStubOriginal.resize(StubCount);
        for (size_t i = 0; i < StubCount; i++)
        {
            if (!m_Memory.ReadU32(PlayerVelocityPatches[i].Address, m_PlayerVelocityStubOriginal[i]))
            {
                m_PlayerVelocityStubOriginal.clear();
                return false;
            }
        }
    }
    if (m_PlayerVelocityStubOriginal.size() != StubCount)
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

void CJetForceGeminiRuntime::PatchPlayerVelocity(bool Enabled)
{
    uint32_t Entry = 0;
    if (m_Memory.ReadU32(PlayerVelocityEntry, Entry))
    {
        m_PlayerVelocityPatchApplied = Entry == PlayerVelocityJump;
    }

    if (!Enabled && !m_PlayerVelocityPatchApplied)
    {
        return;
    }

    if (!SetPlayerVelocityHook(Enabled))
    {
        return;
    }
    m_PlayerVelocityPatchApplied = Enabled;

}

bool CJetForceGeminiRuntime::GetSquaddieOverlayBase(uint32_t & OverlayBase) const
{
    OverlayBase = 0;
    uint32_t OverlayTable = 0;
    const uint32_t RequiredTableSize = (SquaddieOverlayModule + 1) * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) ||
        (OverlayTable & 3) != 0 || !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize))
    {
        return false;
    }

    uint32_t OverlayHeader = OverlayTable + SquaddieOverlayModule * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayHeader, OverlayBase) || (OverlayBase & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayBase + SquaddieZResumeOffset, sizeof(uint32_t)))
    {
        OverlayBase = 0;
        return false;
    }
    return true;
}

// sidekickpadMovePlayer is module 22's dedicated Floyd movement controller.
// Read its live relocated entry for diagnostics before altering it: overlays
// move in RDRAM, so their static ROM offsets alone cannot be patched safely.
void CJetForceGeminiRuntime::UpdateDroneLateralControllerProbe(void)
{
    m_DroneLateralControllerEntry = 0;
    m_DroneLateralControllerWord0 = 0;
    m_DroneLateralControllerWord1 = 0;
    m_DroneLateralControllerWord2 = 0;
    m_DroneLateralControllerWord3 = 0;
    m_DroneLateralControllerReturn = 0;
    m_DroneLateralControllerReturnWord0 = 0;
    m_DroneLateralControllerReturnWord1 = 0;
    m_DroneLateralControllerReturnWord2 = 0;
    m_DroneLateralControllerReturnWord3 = 0;
    m_DroneLateralControllerReturnWord4 = 0;

    uint32_t OverlayTable = 0;
    const uint32_t RequiredTableSize = (FloydOverlayModule + 1) * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayTableAddress, OverlayTable) || (OverlayTable & 3) != 0 ||
        !m_Memory.IsRdramAddress(OverlayTable, RequiredTableSize))
    {
        return;
    }

    uint32_t OverlayBase = 0;
    const uint32_t OverlayHeader = OverlayTable + FloydOverlayModule * OverlayHeaderSize;
    if (!m_Memory.ReadU32(OverlayHeader, OverlayBase) || (OverlayBase & 3) != 0)
    {
        return;
    }

    const uint32_t Entry = OverlayBase + FloydMovePlayerOffset;
    if (!m_Memory.IsRdramAddress(Entry, 5 * sizeof(uint32_t)) ||
        !m_Memory.ReadU32(Entry + 0x00, m_DroneLateralControllerWord0) ||
        !m_Memory.ReadU32(Entry + 0x04, m_DroneLateralControllerWord1) ||
        !m_Memory.ReadU32(Entry + 0x08, m_DroneLateralControllerWord2) ||
        !m_Memory.ReadU32(Entry + 0x0C, m_DroneLateralControllerWord3))
    {
        return;
    }
    m_DroneLateralControllerEntry = Entry;

    for (uint32_t Offset = 0; Offset < FloydMovePlayerSize; Offset += sizeof(uint32_t))
    {
        uint32_t Instruction = 0;
        const uint32_t Address = Entry + Offset;
        if (!m_Memory.ReadU32(Address, Instruction) || Instruction != 0x03E00008)
        {
            continue;
        }

        if (Offset < 3 * sizeof(uint32_t) ||
            !m_Memory.ReadU32(Address - 0x0C, m_DroneLateralControllerReturnWord0) ||
            !m_Memory.ReadU32(Address - 0x08, m_DroneLateralControllerReturnWord1) ||
            !m_Memory.ReadU32(Address - 0x04, m_DroneLateralControllerReturnWord2) ||
            !m_Memory.ReadU32(Address + 0x00, m_DroneLateralControllerReturnWord3) ||
            !m_Memory.ReadU32(Address + 0x04, m_DroneLateralControllerReturnWord4))
        {
            return;
        }
        m_DroneLateralControllerReturn = Address;
        return;
    }
}

bool CJetForceGeminiRuntime::PatchSidekickLateralMove(bool Enabled)
{
    const size_t TailStubCount = sizeof(SidekickLateralMoveHookCode) / sizeof(SidekickLateralMoveHookCode[0]);
    const size_t ProbeStubCount = sizeof(SidekickControlProbeCode) / sizeof(SidekickControlProbeCode[0]);
    auto Failed = [](CGameHackCodePatcher::Result Result) {
        return Result == CGameHackCodePatcher::Result_SignatureMismatch ||
               Result == CGameHackCodePatcher::Result_MemoryUnavailable;
    };
    auto BuildTailWrites = [this, TailStubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(TailStubCount);
        for (size_t Index = 0; Index < TailStubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = SidekickLateralMoveStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? SidekickLateralMoveHookCode[Index] : m_SidekickLateralMoveHookStubOriginal[Index];
            Write.Allowed[0] = m_SidekickLateralMoveHookStubOriginal[Index];
            Write.Allowed[1] = SidekickLateralMoveHookCode[Index];
            Write.AllowedCount = 2;
        }
    };
    auto BuildProbeWrites = [this, ProbeStubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(ProbeStubCount);
        for (size_t Index = 0; Index < ProbeStubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = SidekickControlProbeStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? SidekickControlProbeCode[Index] : m_SidekickControlProbeStubOriginal[Index];
            Write.Allowed[0] = m_SidekickControlProbeStubOriginal[Index];
            Write.Allowed[1] = SidekickControlProbeCode[Index];
            Write.AllowedCount = 2;
        }
    };

    const GAME_HACK_CODE_PATCH TailEntryPatches[] =
    {
        { SidekickLateralMoveEntry, SidekickLateralMoveEntryOriginal, SidekickLateralMoveJump },
    };
    const GAME_HACK_CODE_PATCH ProbeEntryPatches[] =
    {
        { SidekickControlProbeEntry, SidekickControlProbeEntryOriginal, SidekickControlProbeJump },
        { SidekickControlProbeEntry + 0x04, SidekickControlProbeDelayOriginal, SidekickControlProbeEntryOriginal },
    };
    const GAME_HACK_CODE_PATCH InputLegacyPatches[] =
    {
        { SidekickLateralMoveInputLegacyEntry, SidekickLateralMoveInputLegacyOriginal, SidekickLateralMovePreviousJump },
    };
    const GAME_HACK_CODE_PATCH OldTailPreviousPatches[] =
    {
        { SidekickLateralMoveOldTailEntry, SidekickLateralMoveOldTailOriginal, SidekickLateralMovePreviousJump },
        { SidekickLateralMoveOldTailEntry + 0x04, SidekickLateralMoveOldTailDelayOriginal, 0x00000000 },
    };
    const GAME_HACK_CODE_PATCH OldTailCurrentPatches[] =
    {
        { SidekickLateralMoveOldTailEntry, SidekickLateralMoveOldTailOriginal, SidekickLateralMoveJump },
        { SidekickLateralMoveOldTailEntry + 0x04, SidekickLateralMoveOldTailDelayOriginal,
          SidekickLateralMoveOldTailOriginal },
    };

    if (Failed(m_CodePatcher.SetEnabled(InputLegacyPatches, sizeof(InputLegacyPatches) / sizeof(InputLegacyPatches[0]), false)))
    {
        return false;
    }

    uint32_t OldTailEntry = 0;
    if (!m_Memory.ReadU32(SidekickLateralMoveOldTailEntry, OldTailEntry))
    {
        return false;
    }
    if (OldTailEntry == SidekickLateralMovePreviousJump &&
        Failed(m_CodePatcher.SetEnabled(OldTailPreviousPatches, sizeof(OldTailPreviousPatches) / sizeof(OldTailPreviousPatches[0]), false)))
    {
        return false;
    }
    if (OldTailEntry == SidekickLateralMoveJump &&
        Failed(m_CodePatcher.SetEnabled(OldTailCurrentPatches, sizeof(OldTailCurrentPatches) / sizeof(OldTailCurrentPatches[0]), false)))
    {
        return false;
    }
    if (OldTailEntry != SidekickLateralMoveOldTailOriginal && OldTailEntry != SidekickLateralMovePreviousJump &&
        OldTailEntry != SidekickLateralMoveJump)
    {
        return false;
    }

    if (!Enabled)
    {
        if (Failed(m_CodePatcher.SetEnabled(TailEntryPatches, sizeof(TailEntryPatches) / sizeof(TailEntryPatches[0]), false)) ||
            Failed(m_CodePatcher.SetEnabled(ProbeEntryPatches, sizeof(ProbeEntryPatches) / sizeof(ProbeEntryPatches[0]), false)))
        {
            return false;
        }
        if (!m_SidekickLateralMoveHookApplied)
        {
            return true;
        }
        if (m_SidekickLateralMoveHookStubOriginal.size() != TailStubCount ||
            m_SidekickControlProbeStubOriginal.size() != ProbeStubCount)
        {
            return false;
        }
        std::vector<GAME_HACK_CODE_WRITE> TailWrites;
        std::vector<GAME_HACK_CODE_WRITE> ProbeWrites;
        BuildTailWrites(TailWrites, false);
        BuildProbeWrites(ProbeWrites, false);
        if (Failed(m_CodePatcher.Apply(TailWrites.data(), TailWrites.size())) ||
            Failed(m_CodePatcher.Apply(ProbeWrites.data(), ProbeWrites.size())))
        {
            return false;
        }
        m_SidekickLateralMoveHookStubOriginal.clear();
        m_SidekickControlProbeStubOriginal.clear();
        m_SidekickLateralMoveHookApplied = false;
        return true;
    }

    if (m_SidekickLateralMoveHookApplied)
    {
        return true;
    }
    if (Failed(m_CodePatcher.SetEnabled(ProbeEntryPatches, sizeof(ProbeEntryPatches) / sizeof(ProbeEntryPatches[0]), false)))
    {
        return false;
    }

    uint32_t TailEntry = 0;
    uint32_t ProbeEntry = 0;
    uint32_t ProbeDelay = 0;
    if (!m_Memory.ReadU32(SidekickLateralMoveEntry, TailEntry) ||
        !m_Memory.ReadU32(SidekickControlProbeEntry, ProbeEntry) ||
        !m_Memory.ReadU32(SidekickControlProbeEntry + 0x04, ProbeDelay) ||
        TailEntry != SidekickLateralMoveEntryOriginal || ProbeEntry != SidekickControlProbeEntryOriginal ||
        ProbeDelay != SidekickControlProbeDelayOriginal)
    {
        return false;
    }

    m_SidekickLateralMoveHookStubOriginal.resize(TailStubCount);
    m_SidekickControlProbeStubOriginal.resize(ProbeStubCount);
    for (size_t Index = 0; Index < TailStubCount; Index++)
    {
        if (!m_Memory.ReadU32(SidekickLateralMoveStub + (uint32_t)(Index * sizeof(uint32_t)),
                              m_SidekickLateralMoveHookStubOriginal[Index]))
        {
            m_SidekickLateralMoveHookStubOriginal.clear();
            m_SidekickControlProbeStubOriginal.clear();
            return false;
        }
    }
    for (size_t Index = 0; Index < ProbeStubCount; Index++)
    {
        if (!m_Memory.ReadU32(SidekickControlProbeStub + (uint32_t)(Index * sizeof(uint32_t)),
                              m_SidekickControlProbeStubOriginal[Index]))
        {
            m_SidekickLateralMoveHookStubOriginal.clear();
            m_SidekickControlProbeStubOriginal.clear();
            return false;
        }
    }

    std::vector<GAME_HACK_CODE_WRITE> TailWrites;
    std::vector<GAME_HACK_CODE_WRITE> ProbeWrites;
    BuildTailWrites(TailWrites, true);
    BuildProbeWrites(ProbeWrites, true);
    if (Failed(m_CodePatcher.Apply(TailWrites.data(), TailWrites.size())) ||
        Failed(m_CodePatcher.Apply(ProbeWrites.data(), ProbeWrites.size())) ||
        Failed(m_CodePatcher.SetEnabled(TailEntryPatches, sizeof(TailEntryPatches) / sizeof(TailEntryPatches[0]), true)) ||
        Failed(m_CodePatcher.SetEnabled(ProbeEntryPatches, sizeof(ProbeEntryPatches) / sizeof(ProbeEntryPatches[0]), true)))
    {
        m_CodePatcher.SetEnabled(TailEntryPatches, sizeof(TailEntryPatches) / sizeof(TailEntryPatches[0]), false);
        m_CodePatcher.SetEnabled(ProbeEntryPatches, sizeof(ProbeEntryPatches) / sizeof(ProbeEntryPatches[0]), false);
        BuildTailWrites(TailWrites, false);
        BuildProbeWrites(ProbeWrites, false);
        m_CodePatcher.Apply(TailWrites.data(), TailWrites.size());
        m_CodePatcher.Apply(ProbeWrites.data(), ProbeWrites.size());
        m_SidekickLateralMoveHookStubOriginal.clear();
        m_SidekickControlProbeStubOriginal.clear();
        return false;
    }

    m_SidekickLateralMoveHookApplied = true;
    return true;
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
            uint32_t Desired = SidekickPadProbeCode[Index];
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

#if 0 // Replaced by the smaller objMoveXYZ call-site hook below.
bool CJetForceGeminiRuntime::PatchDroneLateralMove(bool Enabled)
{
    const size_t StubCount = sizeof(FloydMoveHookCode) / sizeof(FloydMoveHookCode[0]);

    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = FloydMoveHookStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? FloydMoveHookCode[Index] : m_DroneLateralMoveHookStubOriginal[Index];
            Write.Allowed[0] = m_DroneLateralMoveHookStubOriginal[Index];
            Write.Allowed[1] = FloydMoveHookCode[Index];
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        if (!m_DroneLateralMoveHookApplied)
        {
            // A state created by an experimental build can restore the hook
            // without restoring this host-side flag. Detect that exact entry
            // pattern and take it down before the emulation thread resumes.
            UpdateDroneLateralControllerProbe();
            if (m_DroneLateralControllerReturn >= 0x0C)
            {
                const uint32_t StaleEntry = m_DroneLateralControllerReturn - 0x0C;
                uint32_t StaleWord = 0;
                if (m_Memory.ReadU32(StaleEntry, StaleWord) && StaleWord == FloydMoveHookJump)
                {
                    const GAME_HACK_CODE_PATCH StaleEntryPatches[] =
                    {
                        { StaleEntry, FloydMoveHookJump, 0x02002025 },
                        { StaleEntry + 0x04, 0x00000000, 0x8FBF001C },
                    };
                    m_CodePatcher.SetEnabled(
                        StaleEntryPatches,
                        sizeof(StaleEntryPatches) / sizeof(StaleEntryPatches[0]), true);
                }
            }
            m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
            return true;
        }

        const GAME_HACK_CODE_PATCH EntryPatches[] =
        {
            { m_DroneLateralMoveHookEntry, 0x02002025, FloydMoveHookJump },
            { m_DroneLateralMoveHookEntry + 0x04, 0x8FBF001C, 0x00000000 },
        };
        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
            EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }

        if (!m_DroneLateralMoveHookStubOriginal.empty())
        {
            if (m_DroneLateralMoveHookStubOriginal.size() != StubCount)
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
        }

        m_DroneLateralMoveHookStubOriginal.clear();
        m_DroneLateralMoveHookApplied = false;
        m_DroneLateralMoveHookEntry = 0;
        m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
        return true;
    }

    if (m_DroneLateralControllerReturn < 0x0C)
    {
        return false;
    }
    const uint32_t HookEntry = m_DroneLateralControllerReturn - 0x0C;
    if (m_DroneLateralMoveHookApplied)
    {
        if (m_DroneLateralMoveHookEntry == HookEntry)
        {
            return true;
        }
        if (!PatchDroneLateralMove(false))
        {
            return false;
        }
    }

    uint32_t EntryWord = 0;
    uint32_t DelayWord = 0;
    if (!m_Memory.ReadU32(HookEntry, EntryWord) || !m_Memory.ReadU32(HookEntry + 0x04, DelayWord) ||
        EntryWord != 0x02002025 || DelayWord != 0x8FBF001C)
    {
        return false;
    }

    m_DroneLateralMoveHookStubOriginal.resize(StubCount);
    for (size_t Index = 0; Index < StubCount; Index++)
    {
        if (!m_Memory.ReadU32(
                FloydMoveHookStub + (uint32_t)(Index * sizeof(uint32_t)),
                m_DroneLateralMoveHookStubOriginal[Index]))
        {
            m_DroneLateralMoveHookStubOriginal.clear();
            return false;
        }
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_DroneLateralMoveHookStubOriginal.clear();
        return false;
    }

    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { HookEntry, 0x02002025, FloydMoveHookJump },
        { HookEntry + 0x04, 0x8FBF001C, 0x00000000 },
    };
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_DroneLateralMoveHookStubOriginal.clear();
        return false;
    }

    m_DroneLateralMoveHookApplied = true;
    m_DroneLateralMoveHookEntry = HookEntry;
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
    return true;
}

#endif

bool CJetForceGeminiRuntime::PatchDroneLateralMove(bool Enabled)
{
    const size_t StubCount = sizeof(FloydMoveCallHookCode) / sizeof(FloydMoveCallHookCode[0]);

    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        const uint32_t Resume = m_DroneLateralMoveHookEntry + 0x08;
        const uint32_t ResumeJump = 0x08000000 | ((Resume >> 2) & 0x03FFFFFF);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            uint32_t Desired = FloydMoveCallHookCode[Index];
            if (Index == FloydMoveCallHookResumeIndex)
            {
                Desired = ResumeJump;
            }

            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = FloydMoveHookStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? Desired : m_DroneLateralMoveHookStubOriginal[Index];
            Write.Allowed[0] = m_DroneLateralMoveHookStubOriginal[Index];
            Write.Allowed[1] = Desired;
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        if (!m_DroneLateralMoveHookApplied)
        {
            // Save states carry RDRAM. Restore a stale call-site hook even if
            // the new process has no record of having installed it.
            UpdateDroneLateralControllerProbe();
            if (m_DroneLateralControllerEntry != 0)
            {
                const uint32_t StaleEntry = m_DroneLateralControllerEntry + FloydMoveObjMoveCallOffset;
                uint32_t StaleWord = 0;
                if (m_Memory.ReadU32(StaleEntry, StaleWord) && StaleWord == FloydMoveHookJump)
                {
                    const GAME_HACK_CODE_PATCH StalePatches[] =
                    {
                        { StaleEntry, FloydMoveHookJump, FloydMoveObjMoveCallOriginal },
                        { StaleEntry + 0x04, 0x00000000, FloydMoveObjMoveDelayOriginal },
                    };
                    m_CodePatcher.SetEnabled(StalePatches, sizeof(StalePatches) / sizeof(StalePatches[0]), true);
                }
            }
            return true;
        }

        const GAME_HACK_CODE_PATCH EntryPatches[] =
        {
            { m_DroneLateralMoveHookEntry, FloydMoveObjMoveCallOriginal, FloydMoveHookJump },
            { m_DroneLateralMoveHookEntry + 0x04, FloydMoveObjMoveDelayOriginal, 0x00000000 },
        };
        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
            EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return false;
        }

        if (m_DroneLateralMoveHookStubOriginal.size() != StubCount)
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

        m_DroneLateralMoveHookStubOriginal.clear();
        m_DroneLateralMoveHookApplied = false;
        m_DroneLateralMoveHookEntry = 0;
        return true;
    }

    if (m_DroneLateralControllerEntry == 0)
    {
        return false;
    }
    const uint32_t HookEntry = m_DroneLateralControllerEntry + FloydMoveObjMoveCallOffset;
    if (m_DroneLateralMoveHookApplied)
    {
        return m_DroneLateralMoveHookEntry == HookEntry;
    }

    uint32_t EntryWord = 0;
    uint32_t DelayWord = 0;
    if (!m_Memory.ReadU32(HookEntry, EntryWord) || !m_Memory.ReadU32(HookEntry + 0x04, DelayWord) ||
        EntryWord != FloydMoveObjMoveCallOriginal || DelayWord != FloydMoveObjMoveDelayOriginal)
    {
        return false;
    }

    m_DroneLateralMoveHookEntry = HookEntry;
    m_DroneLateralMoveHookStubOriginal.resize(StubCount);
    for (size_t Index = 0; Index < StubCount; Index++)
    {
        if (!m_Memory.ReadU32(
                FloydMoveHookStub + (uint32_t)(Index * sizeof(uint32_t)),
                m_DroneLateralMoveHookStubOriginal[Index]))
        {
            m_DroneLateralMoveHookStubOriginal.clear();
            m_DroneLateralMoveHookEntry = 0;
            return false;
        }
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_DroneLateralMoveHookStubOriginal.clear();
        m_DroneLateralMoveHookEntry = 0;
        return false;
    }

    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { HookEntry, FloydMoveObjMoveCallOriginal, FloydMoveHookJump },
        { HookEntry + 0x04, FloydMoveObjMoveDelayOriginal, 0x00000000 },
    };
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_DroneLateralMoveHookStubOriginal.clear();
        m_DroneLateralMoveHookEntry = 0;
        return false;
    }

    m_DroneLateralMoveHookApplied = true;
    return true;
}

bool CJetForceGeminiRuntime::PatchFloydCameraLateralMove(bool Enabled)
{
    const size_t StubCount = sizeof(FloydCameraLateralHookCode) / sizeof(FloydCameraLateralHookCode[0]);
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
            !m_Memory.IsRdramAddress(OverlayBase + FloydCameraLateralCallOffset, 2 * sizeof(uint32_t)))
        {
            return false;
        }
        Entry = OverlayBase + FloydCameraLateralCallOffset;
        return true;
    };
    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = FloydCameraLateralStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? FloydCameraLateralHookCode[Index] :
                                      m_FloydCameraLateralHookStubOriginal[Index];
            Write.Allowed[0] = m_FloydCameraLateralHookStubOriginal[Index];
            Write.Allowed[1] = FloydCameraLateralHookCode[Index];
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        if (!m_FloydCameraLateralHookApplied)
        {
            uint32_t Entry = 0;
            uint32_t Current = 0;
            if (FindEntry(Entry) && m_Memory.ReadU32(Entry, Current) && Current == FloydCameraLateralJump)
            {
                const GAME_HACK_CODE_PATCH StalePatch =
                    { Entry, FloydCameraLateralCallOriginal, FloydCameraLateralJump };
                CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(&StalePatch, 1, false);
                return Result != CGameHackCodePatcher::Result_SignatureMismatch &&
                       Result != CGameHackCodePatcher::Result_MemoryUnavailable;
            }
            return true;
        }

        const GAME_HACK_CODE_PATCH EntryPatch =
            { m_FloydCameraLateralHookEntry, FloydCameraLateralCallOriginal, FloydCameraLateralJump };
        CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(&EntryPatch, 1, false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable ||
            m_FloydCameraLateralHookStubOriginal.size() != StubCount)
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
        m_FloydCameraLateralHookStubOriginal.clear();
        m_FloydCameraLateralHookApplied = false;
        m_FloydCameraLateralHookEntry = 0;
        m_Memory.WriteU32(FloydCameraPreviousObjectAddress, 0);
        return true;
    }

    uint32_t Entry = 0;
    uint32_t Delay = 0;
    if (!FindEntry(Entry) || !m_Memory.ReadU32(Entry, Delay) ||
        !m_Memory.ReadU32(Entry + sizeof(uint32_t), Delay) || Delay != FloydCameraLateralCallDelayOriginal)
    {
        return false;
    }
    if (m_FloydCameraLateralHookApplied)
    {
        return Entry == m_FloydCameraLateralHookEntry;
    }

    uint32_t Current = 0;
    if (!m_Memory.ReadU32(Entry, Current) || Current != FloydCameraLateralCallOriginal)
    {
        return false;
    }
    m_FloydCameraLateralHookEntry = Entry;
    m_FloydCameraLateralHookStubOriginal.resize(StubCount);
    for (size_t Index = 0; Index < StubCount; Index++)
    {
        if (!m_Memory.ReadU32(
                FloydCameraLateralStub + (uint32_t)(Index * sizeof(uint32_t)),
                m_FloydCameraLateralHookStubOriginal[Index]))
        {
            m_FloydCameraLateralHookStubOriginal.clear();
            m_FloydCameraLateralHookEntry = 0;
            return false;
        }
    }

    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_FloydCameraLateralHookStubOriginal.clear();
        m_FloydCameraLateralHookEntry = 0;
        return false;
    }

    const GAME_HACK_CODE_PATCH EntryPatch =
        { Entry, FloydCameraLateralCallOriginal, FloydCameraLateralJump };
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(&EntryPatch, 1, true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_FloydCameraLateralHookStubOriginal.clear();
        m_FloydCameraLateralHookEntry = 0;
        return false;
    }

    m_Memory.WriteU32(FloydCameraPreviousObjectAddress, 0);
    m_Memory.WriteU32(DroneLateralHookHitsAddress, 0);
    m_FloydCameraLateralHookApplied = true;
    return true;
}

bool CJetForceGeminiRuntime::PatchSidekickVelocityLateralMove(bool Enabled)
{
    const size_t StubCount = sizeof(SidekickVelocityLateralHookCode) / sizeof(SidekickVelocityLateralHookCode[0]);
    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { SidekickVelocityLateralEntry, SidekickVelocityLateralEntryOriginal, SidekickVelocityLateralJump },
        { SidekickVelocityLateralDelay, SidekickVelocityLateralDelayOriginal, SidekickVelocityLateralEntryOriginal },
    };
    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = SidekickVelocityLateralStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? SidekickVelocityLateralHookCode[Index] :
                                      m_SidekickVelocityLateralHookStubOriginal[Index];
            Write.Allowed[0] = m_SidekickVelocityLateralHookStubOriginal[Index];
            Write.Allowed[1] = SidekickVelocityLateralHookCode[Index];
            Write.AllowedCount = 2;
        }
    };

    if (!Enabled)
    {
        if (!m_SidekickVelocityLateralHookApplied)
        {
            uint32_t Current = 0;
            if (m_Memory.ReadU32(SidekickVelocityLateralEntry, Current) && Current == SidekickVelocityLateralJump)
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
            m_SidekickVelocityLateralHookStubOriginal.size() != StubCount)
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
        m_SidekickVelocityLateralHookStubOriginal.clear();
        m_SidekickVelocityLateralHookApplied = false;
        return true;
    }

    if (m_SidekickVelocityLateralHookApplied)
    {
        return true;
    }

    uint32_t Entry = 0;
    uint32_t Delay = 0;
    if (!m_Memory.ReadU32(SidekickVelocityLateralEntry, Entry) ||
        !m_Memory.ReadU32(SidekickVelocityLateralDelay, Delay) ||
        Entry != SidekickVelocityLateralEntryOriginal || Delay != SidekickVelocityLateralDelayOriginal)
    {
        return false;
    }
    m_SidekickVelocityLateralHookStubOriginal.resize(StubCount);
    for (size_t Index = 0; Index < StubCount; Index++)
    {
        if (!m_Memory.ReadU32(
                SidekickVelocityLateralStub + (uint32_t)(Index * sizeof(uint32_t)),
                m_SidekickVelocityLateralHookStubOriginal[Index]))
        {
            m_SidekickVelocityLateralHookStubOriginal.clear();
            return false;
        }
    }
    std::vector<GAME_HACK_CODE_WRITE> Writes;
    BuildStubWrites(Writes, true);
    CGameHackCodePatcher::Result StubResult = m_CodePatcher.Apply(Writes.data(), Writes.size());
    if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_SidekickVelocityLateralHookStubOriginal.clear();
        return false;
    }
    CGameHackCodePatcher::Result EntryResult = m_CodePatcher.SetEnabled(
        EntryPatches, sizeof(EntryPatches) / sizeof(EntryPatches[0]), true);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        BuildStubWrites(Writes, false);
        m_CodePatcher.Apply(Writes.data(), Writes.size());
        m_SidekickVelocityLateralHookStubOriginal.clear();
        return false;
    }
    m_Memory.WriteU32(DroneLateralHookHitsAddress, 0);
    m_SidekickVelocityLateralHookApplied = true;
    return true;
}

// Only the entry word is displaced. The delay slot at 0x80030064 reloads $f8
// from Floyd's position and the stub resumes on it, so letting it run twice is
// harmless and one patched instruction is enough.
bool CJetForceGeminiRuntime::PatchSidekickStrafe(bool Enabled)
{
    const size_t StubCount = sizeof(SidekickStrafeHookCode) / sizeof(SidekickStrafeHookCode[0]);
    const GAME_HACK_CODE_PATCH EntryPatches[] =
    {
        { SidekickStrafeEntry, SidekickStrafeEntryOriginal, SidekickStrafeJump },
        { SidekickStrafeDelay, SidekickStrafeDelayOriginal, SidekickStrafeEntryOriginal },
    };
    auto BuildStubWrites = [this, StubCount](std::vector<GAME_HACK_CODE_WRITE> & Writes, bool Install) {
        Writes.resize(StubCount);
        for (size_t Index = 0; Index < StubCount; Index++)
        {
            GAME_HACK_CODE_WRITE & Write = Writes[Index];
            Write.Address = SidekickStrafeStub + (uint32_t)(Index * sizeof(uint32_t));
            Write.Desired = Install ? SidekickStrafeHookCode[Index] :
                                      m_SidekickStrafeHookStubOriginal[Index];
            Write.Allowed[0] = m_SidekickStrafeHookStubOriginal[Index];
            Write.Allowed[1] = SidekickStrafeHookCode[Index];
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
                SidekickStrafeStub + (uint32_t)(Index * sizeof(uint32_t)),
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
    return true;
}

// SquaddieControl's common heading helper converts speed and yaw into X/Z
// velocity. Each hook moves the original multiply into the jump delay slot,
// multiplies its result by 0.5 in free RAM, stores it, then resumes the overlay.
// Halves the squads module's published time step. Both controllers convert the
// tick count they are given and store it, so the store is what gets displaced:
// the stub multiplies the converted value by a half and writes it instead. The
// displaced instruction addressed the step through $at, and the jump's delay
// slot reloads $at for the instruction that follows, so the stub rebuilds the
// address itself and hands $at back exactly as the delay slot left it. The
// scratch registers are the two the conversions read, dead from the conversion
// onwards.
void CJetForceGeminiRuntime::PatchSquadsTimeStep(bool Enabled)
{
    uint32_t OverlayBase = 0;
    if (!GetSquaddieOverlayBase(OverlayBase))
    {
        m_SquadsTimeStepPatchApplied = false;
        return;
    }

    const uint32_t Step = OverlayBase + SquadsTimeStepOffset;
    const uint32_t SquaddieDelayTarget = OverlayBase + SquaddieStepDelayTargetOffset;
    const uint32_t SquadronEntry = OverlayBase + SquadronStepEntryOffset;
    const uint32_t SquaddieEntry = OverlayBase + SquaddieStepEntryOffset;
    const uint32_t SquadronJump = JalInstruction(SquadronStepStub);
    const uint32_t SquaddieJump = JalInstruction(SquaddieStepStub);

    // The instruction each stub replaces is the store of the freshly computed
    // step, `swc1 $fN, %lo(Step)($at)`. Its low 16 bits are %lo(Step), so the
    // whole word changes every time overlay 3 is relocated to a new address.
    // Reconstruct it from the live Step instead of comparing against a fixed
    // constant: the old constant only matched the single load address it was
    // captured at, so the signature check failed - and the entire enemy-speed
    // halve silently never installed - in every other scene.
    const uint32_t SquadronStepEntryOriginal = 0xE4260000 | (Step & 0xFFFF); // swc1 $f6, %lo(Step)($at)
    const uint32_t SquaddieStepEntryOriginal = 0xE42A0000 | (Step & 0xFFFF); // swc1 $f10, %lo(Step)($at)

    // Stub words first, live jump entries last, so a half written stub is never
    // reachable.
    GAME_HACK_CODE_PATCH Patches[] = {
        {SquadronStepStub + 0x00, 0x00000000, 0x3C013F00},                    // lui   $at, 0x3F00
        {SquadronStepStub + 0x04, 0x00000000, 0x44812000},                    // mtc1  $at, $f4
        {SquadronStepStub + 0x08, 0x00000000, WithHi(0x3C010000, Step)},      // lui   $at, hi
        {SquadronStepStub + 0x0C, 0x00000000, 0x46043182},                    // mul.s $f6,$f6,$f4
        {SquadronStepStub + 0x10, 0x00000000, WithLo(0xE4260000, Step)},      // swc1  $f6, lo($at)
        {SquadronStepStub + 0x14, 0x00000000,
         WithHi(0x3C010000, SquadsTickCountAddress)},                    // lui   $at, restore
        {SquadronStepStub + 0x18, 0x00000000, 0x03E00008},                    // jr    $ra
        {SquadronStepStub + 0x1C, 0x00000000, 0x00000000},                    // nop
        {SquaddieStepStub + 0x00, 0x00000000, 0x3C013F00},                    // lui   $at, 0x3F00
        {SquaddieStepStub + 0x04, 0x00000000, 0x44814000},                    // mtc1  $at, $f8
        {SquaddieStepStub + 0x08, 0x00000000, WithHi(0x3C010000, Step)},      // lui   $at, hi
        {SquaddieStepStub + 0x0C, 0x00000000, 0x46085282},                    // mul.s $f10,$f10,$f8
        {SquaddieStepStub + 0x10, 0x00000000, WithLo(0xE42A0000, Step)},      // swc1  $f10, lo($at)
        {SquaddieStepStub + 0x14, 0x00000000,
         WithHi(0x3C010000, SquaddieDelayTarget)},                            // lui   $at, restore
        {SquaddieStepStub + 0x18, 0x00000000, 0x03E00008},                    // jr    $ra
        {SquaddieStepStub + 0x1C, 0x00000000, 0x00000000},                    // nop
        {SquadronEntry, SquadronStepEntryOriginal, SquadronJump},
        {SquaddieEntry, SquaddieStepEntryOriginal, SquaddieJump},
    };
    const size_t StubCount = 16;
    const size_t EntryCount = 2;

    // The conversions feeding each store, which reject a different build or a
    // different module sitting in the slot even if one target word coincides.
    uint32_t Signature[2];
    if (!m_Memory.ReadU32(OverlayBase + SquadronStepCvtOffset, Signature[0]) ||
        !m_Memory.ReadU32(OverlayBase + SquaddieStepCvtOffset, Signature[1]) ||
        Signature[0] != SquadronStepCvtOriginal || Signature[1] != SquaddieStepCvtOriginal)
    {
        m_SquadsTimeStepPatchApplied = false;
        return;
    }

    uint32_t SquadronWord = 0;
    uint32_t SquaddieWord = 0;
    if (!m_Memory.ReadU32(SquadronEntry, SquadronWord) ||
        !m_Memory.ReadU32(SquaddieEntry, SquaddieWord))
    {
        m_SquadsTimeStepPatchApplied = false;
        return;
    }
    m_SquadsTimeStepPatchApplied = SquadronWord == SquadronJump && SquaddieWord == SquaddieJump;
    if (Enabled == m_SquadsTimeStepPatchApplied)
    {
        return;
    }

    if (Enabled)
    {
        CGameHackCodePatcher::Result StubResult =
            m_CodePatcher.SetEnabled(Patches, StubCount, true);
        if (StubResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            StubResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return;
        }
    }

    CGameHackCodePatcher::Result EntryResult =
        m_CodePatcher.SetEnabled(Patches + StubCount, EntryCount, Enabled);
    if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
        EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }
    m_SquadsTimeStepPatchApplied = Enabled;

    // The stub is only reclaimed once nothing can reach it.
    if (!Enabled)
    {
        m_CodePatcher.SetEnabled(Patches, StubCount, false);
    }
}

void CJetForceGeminiRuntime::PatchSquaddieMove(bool Enabled)
{
    uint32_t OverlayBase = 0;
    if (!GetSquaddieOverlayBase(OverlayBase))
    {
        if (Enabled || m_SquaddieOverlayBase == 0 ||
            !m_Memory.IsRdramAddress(
                m_SquaddieOverlayBase + SquaddieZResumeOffset, sizeof(uint32_t)))
        {
            m_SquaddieMovePatchApplied = false;
            m_SquaddieOverlayBase = 0;
            return;
        }
        OverlayBase = m_SquaddieOverlayBase;
    }

    const uint32_t XEntry = OverlayBase + SquaddieXEntryOffset;
    const uint32_t XDelay = OverlayBase + SquaddieXDelayOffset;
    const uint32_t XResume = OverlayBase + SquaddieXResumeOffset;
    const uint32_t ZEntry = OverlayBase + SquaddieZEntryOffset;
    const uint32_t ZDelay = OverlayBase + SquaddieZDelayOffset;
    const uint32_t ZResume = OverlayBase + SquaddieZResumeOffset;
    const uint32_t XJump = JalInstruction(SquaddieXStub);
    const uint32_t ZJump = JalInstruction(SquaddieZStub);

    // Stub words first, displaced delay slots next, live jump entries last.
    GAME_HACK_CODE_PATCH Patches[] =
        {
            {SquaddieXStub + 0x00, 0x00000000, 0x3C013F00}, // lui   $at, 0x3F00
            {SquaddieXStub + 0x04, 0x00000000, 0x44815000}, // mtc1  $at, $f10
            {SquaddieXStub + 0x08, 0x00000000, 0x00000000}, // nop
            {SquaddieXStub + 0x0C, 0x00000000, 0x00000000}, // nop
            {SquaddieXStub + 0x10, 0x00000000, 0x460A8402}, // mul.s $f16, $f16, $f10
            {SquaddieXStub + 0x14, 0x00000000, 0x03E00008}, // jr    $ra
            {SquaddieXStub + 0x18, 0x00000000, 0xE510001C}, // swc1  $f16, 0x1C($t0)
            {SquaddieZStub + 0x00, 0x00000000, 0x3C013F00}, // lui   $at, 0x3F00
            {SquaddieZStub + 0x04, 0x00000000, 0x44819000}, // mtc1  $at, $f18
            {SquaddieZStub + 0x08, 0x00000000, 0x00000000}, // nop
            {SquaddieZStub + 0x0C, 0x00000000, 0x00000000}, // nop
            {SquaddieZStub + 0x10, 0x00000000, 0x46122102}, // mul.s $f4, $f4, $f18
            {SquaddieZStub + 0x14, 0x00000000, 0x03E00008}, // jr    $ra
            {SquaddieZStub + 0x18, 0x00000000, 0xE5640024}, // swc1  $f4, 0x24($t3)
            {XDelay, 0xE510001C, 0x460A0402},
            {ZDelay, 0xE5640024, 0x46120102},
            {XEntry, 0x460A0402, XJump},
            {ZEntry, 0x46120102, ZJump},
        };
    const size_t StubCount = 14;
    const size_t DelayCount = 2;
    const size_t EntryCount = 2;
    const size_t PatchCount = sizeof(Patches) / sizeof(Patches[0]);

    // These instructions bracket the two patched pairs and reject a different
    // module, build or overlay generation even if one target word coincides.
    uint32_t Signature[4];
    if (!m_Memory.ReadU32(XResume, Signature[0]) ||
        !m_Memory.ReadU32(OverlayBase + 0xAE48, Signature[1]) ||
        !m_Memory.ReadU32(OverlayBase + 0xAE60, Signature[2]) ||
        !m_Memory.ReadU32(ZResume, Signature[3]) ||
        Signature[0] != 0x8C490000 || Signature[1] != 0x8524001C ||
        Signature[2] != 0xC5520020 || Signature[3] != 0x8FBF0014)
    {
        m_SquaddieMovePatchApplied = false;
        return;
    }

    uint32_t XEntryWord = 0;
    uint32_t ZEntryWord = 0;
    if (!m_Memory.ReadU32(XEntry, XEntryWord) || !m_Memory.ReadU32(ZEntry, ZEntryWord))
    {
        m_SquaddieMovePatchApplied = false;
        return;
    }
    bool HookPresent = XEntryWord == XJump || ZEntryWord == ZJump;
    m_SquaddieMovePatchApplied = XEntryWord == XJump && ZEntryWord == ZJump;
    m_SquaddieOverlayBase = OverlayBase;

    if (!Enabled)
    {
        if (!HookPresent)
        {
            m_SquaddieMovePatchApplied = false;
            m_SquaddieOverlayBase = 0;
            return;
        }
        CGameHackCodePatcher::Result EntryResult =
            m_CodePatcher.SetEnabled(Patches + StubCount + DelayCount, EntryCount, false);
        if (EntryResult == CGameHackCodePatcher::Result_SignatureMismatch ||
            EntryResult == CGameHackCodePatcher::Result_MemoryUnavailable)
        {
            return;
        }
        m_SquaddieMovePatchApplied = false;
        m_CodePatcher.SetEnabled(Patches + StubCount, DelayCount, false);
        m_CodePatcher.SetEnabled(Patches, StubCount, false);
        m_SquaddieOverlayBase = 0;
        return;
    }

    // A loaded state can contain an older version of our stub. Only reclaim
    // unexpected words when an entry proves the region belongs to this hook.
    if (HookPresent)
    {
        for (size_t i = 0; i < StubCount + DelayCount; i++)
        {
            uint32_t Current = 0;
            if (m_Memory.ReadU32(Patches[i].Address, Current) &&
                Current != Patches[i].Original && Current != Patches[i].Replacement)
            {
                m_Memory.WriteU32(Patches[i].Address, Patches[i].Original);
            }
        }
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(Patches, PatchCount, true);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_SquaddieMovePatchApplied = false;
        return;
    }
    m_SquaddieMovePatchApplied = true;
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

// viSetTrippleBuffer normally only requests a third framebuffer for widescreen
// resolutions. Applying this before the level's video setup makes viChangeMode
// allocate the same third buffer for the 60fps scheduler path.
void CJetForceGeminiRuntime::PatchTripleBuffer(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(TripleBufferRequest, Current))
    {
        m_TripleBufferPatchApplied = Current == TripleBufferPatches[0].Replacement;
    }

    if (!Enabled && !m_TripleBufferPatchApplied)
    {
        return;
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        TripleBufferPatches, sizeof(TripleBufferPatches) / sizeof(TripleBufferPatches[0]), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }
    m_TripleBufferPatchApplied = Enabled;
    if (Enabled)
    {
        // The game normally reaches viSetTrippleBuffer during video setup. The
        // hack is already active before that call, but set the backing flag as
        // well so an earlier setup path cannot leave the allocation at two
        // buffers.
        m_Memory.WriteU8(TripleBufferActive, 1);
    }
}

// Preserve the 30fps update cadence of the wake while the main game loop runs
// at 60fps. The retained update deliberately keeps its native delta of one,
// which gives its short-lived mesh segments their 30fps lifetime again.
void CJetForceGeminiRuntime::PatchWaterWakeRate(bool Enabled)
{
    // Migrate either of the two earlier experiments out of a loaded save state.
    uint32_t LegacyStubWord = 0;
    if (m_Memory.ReadU32(WaterWakeGateStub + 0x04, LegacyStubWord) &&
        LegacyStubWord == WaterWakeLegacyGatePatches[1].Replacement)
    {
        uint32_t LegacyCall = 0;
        if (m_Memory.ReadU32(WaterWakeLegacyCallSite, LegacyCall) && LegacyCall == WaterWakeGateJump)
        {
            const GAME_HACK_CODE_PATCH LegacyCallPatch = {
                WaterWakeLegacyCallSite, WaterWakeUpdateCall, WaterWakeGateJump};
            m_CodePatcher.SetEnabled(&LegacyCallPatch, 1, false);
        }
        m_CodePatcher.SetEnabled(WaterWakeLegacyGatePatches,
                                 sizeof(WaterWakeLegacyGatePatches) / sizeof(WaterWakeLegacyGatePatches[0]), false);
    }

    uint32_t LegacyRate = 0;
    if (m_Memory.ReadU32(WaterWakeLegacyRatePatches[0].Address, LegacyRate) &&
        LegacyRate == WaterWakeLegacyRatePatches[0].Replacement)
    {
        m_CodePatcher.SetEnabled(
            WaterWakeLegacyRatePatches, sizeof(WaterWakeLegacyRatePatches) / sizeof(WaterWakeLegacyRatePatches[0]), false);
    }

    uint32_t Current = 0;
    uint32_t GateStubWord = 0;
    if (m_Memory.ReadU32(WaterWakeLegacyCallSite, Current) &&
        m_Memory.ReadU32(WaterWakeGateStub + 0x04, GateStubWord))
    {
        m_WaterWakeRatePatchApplied = Current == WaterWakeGateJump && GateStubWord == WaterWakeGatePatches[1].Replacement;
    }

    if (!Enabled)
    {
        if (m_WaterWakeRatePatchApplied)
        {
            m_CodePatcher.SetEnabled(
                WaterWakeGatePatches, sizeof(WaterWakeGatePatches) / sizeof(WaterWakeGatePatches[0]), false);
        }
        m_Memory.WriteU32(WaterWakeGateCounter, 0);
        m_WaterWakeRatePatchApplied = false;
        m_WaterWakeRatePatchStatus = 0;
        return;
    }

    if (m_WaterWakeRatePatchApplied)
    {
        return;
    }

    m_Memory.WriteU32(WaterWakeGateCounter, 0);
    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        WaterWakeGatePatches, sizeof(WaterWakeGatePatches) / sizeof(WaterWakeGatePatches[0]), true);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        m_WaterWakeRatePatchStatus = 1;
        return;
    }

    m_WaterWakeRatePatchApplied = true;
    m_WaterWakeRatePatchStatus = 16;
}

void CJetForceGeminiRuntime::PatchWaterWakeDrawProbe(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(WaterWakeDrawProbePatches[0].Address, Current))
    {
        m_WaterWakeDrawProbeApplied = Current == WaterWakeDrawProbePatches[0].Replacement;
    }

    if (!Enabled && !m_WaterWakeDrawProbeApplied)
    {
        return;
    }

    CGameHackCodePatcher::Result Result = m_CodePatcher.SetEnabled(
        WaterWakeDrawProbePatches,
        sizeof(WaterWakeDrawProbePatches) / sizeof(WaterWakeDrawProbePatches[0]), Enabled);
    if (Result == CGameHackCodePatcher::Result_SignatureMismatch ||
        Result == CGameHackCodePatcher::Result_MemoryUnavailable)
    {
        return;
    }
    m_WaterWakeDrawProbeApplied = Enabled;
    if (Enabled)
    {
        m_WaterWakeRatePatchStatus = 9;
    }
}

void CJetForceGeminiRuntime::PatchWaterWakeCulling(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(WaterWakeCullingEntry, Current))
    {
        m_WaterWakeCullingPatchApplied = Current == WaterWakeCullingJump;
    }

    if (Enabled == m_WaterWakeCullingPatchApplied)
    {
        if (Enabled)
        {
            m_WaterWakeRatePatchStatus = 10;
        }
        return;
    }

    if (!SetHookEnabled(
            WaterWakeCullingPatches,
            sizeof(WaterWakeCullingPatches) / sizeof(WaterWakeCullingPatches[0]), Enabled))
    {
        m_WaterWakeRatePatchStatus = 1;
        return;
    }
    m_WaterWakeCullingPatchApplied = Enabled;
    m_WaterWakeRatePatchStatus = Enabled ? 10 : 0;
}

void CJetForceGeminiRuntime::PatchWaterWakeDrawFallback(bool Enabled)
{
    uint32_t Current = 0;
    uint32_t StubTail = 0;
    if (m_Memory.ReadU32(WaterWakeDrawFallbackEntry, Current) &&
        m_Memory.ReadU32(WaterWakeDrawFallbackStub + 0x38, StubTail))
    {
        m_WaterWakeDrawFallbackPatchApplied = Current == WaterWakeDrawFallbackJump &&
                                              StubTail == WaterWakeDrawFallbackPatches[14].Replacement;
    }

    if (Enabled == m_WaterWakeDrawFallbackPatchApplied)
    {
        if (Enabled)
        {
            m_WaterWakeRatePatchStatus = 13;
        }
        else
        {
            m_Memory.WriteU32(WaterWakeDrawTargetAddress, 0);
            m_Memory.WriteU32(WaterWakeDrawFallbackCalledAddress, 0);
        }
        return;
    }

    if (!SetHookEnabled(
            WaterWakeDrawFallbackPatches,
            sizeof(WaterWakeDrawFallbackPatches) / sizeof(WaterWakeDrawFallbackPatches[0]), Enabled))
    {
        m_WaterWakeRatePatchStatus = 1;
        return;
    }
    m_WaterWakeDrawFallbackPatchApplied = Enabled;
    m_WaterWakeRatePatchStatus = Enabled ? 13 : 0;
    if (!Enabled)
    {
        m_Memory.WriteU32(WaterWakeDrawTargetAddress, 0);
        m_Memory.WriteU32(WaterWakeDrawFallbackCalledAddress, 0);
    }
}

void CJetForceGeminiRuntime::PatchWaterWakeFrameRate(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(WaterWakeFrameRateEntry, Current))
    {
        m_WaterWakeFrameRatePatchApplied = Current == WaterWakeFrameRateJump;
    }

    if (Enabled == m_WaterWakeFrameRatePatchApplied)
    {
        if (Enabled)
        {
            m_WaterWakeRatePatchStatus = 14;
        }
        return;
    }

    if (!SetHookEnabled(
            WaterWakeFrameRatePatches,
            sizeof(WaterWakeFrameRatePatches) / sizeof(WaterWakeFrameRatePatches[0]), Enabled))
    {
        m_WaterWakeRatePatchStatus = 1;
        return;
    }
    m_WaterWakeFrameRatePatchApplied = Enabled;
    m_WaterWakeRatePatchStatus = Enabled ? 14 : 0;
}

void CJetForceGeminiRuntime::PatchWaterWakeStockDrawProbe(bool Enabled)
{
    uint32_t Current = 0;
    if (m_Memory.ReadU32(WaterWakeStockDrawEntry, Current))
    {
        m_WaterWakeStockDrawProbeApplied = Current == WaterWakeStockDrawJump;
    }

    if (Enabled == m_WaterWakeStockDrawProbeApplied)
    {
        if (Enabled)
        {
            m_WaterWakeRatePatchStatus = 15;
        }
        return;
    }

    if (!SetHookEnabled(
            WaterWakeStockDrawProbePatches,
            sizeof(WaterWakeStockDrawProbePatches) / sizeof(WaterWakeStockDrawProbePatches[0]), Enabled))
    {
        m_WaterWakeRatePatchStatus = 1;
        return;
    }
    m_WaterWakeStockDrawProbeApplied = Enabled;
    m_WaterWakeRatePatchStatus = Enabled ? 15 : 0;
    if (!Enabled)
    {
        m_Memory.WriteU32(WaterWakeStockDrawTargetAddress, 0);
        m_Memory.WriteU32(WaterWakeStockDrawCalledAddress, 0);
    }
}

void CJetForceGeminiRuntime::UpdateWaterWakeDrawTarget(void)
{
    uint32_t Target = 0;
    uint32_t ObjectList = 0;
    uint32_t ObjectCount = 0;
    if (m_Memory.ReadU32(WaterWakeObjectListAddress, ObjectList) &&
        m_Memory.ReadU32(WaterWakeObjectCountAddress, ObjectCount) && ObjectCount <= WaterWakeObjectLimit)
    {
        for (uint32_t Index = 0; Index < ObjectCount; Index++)
        {
            uint32_t Object = 0;
            uint32_t Wake = 0;
            uint32_t DrawSetup = 0;
            uint8_t Marker = 0;
            int16_t Alpha = 0;
            if (!m_Memory.ReadU32(ObjectList + Index * sizeof(uint32_t), Object) ||
                !m_Memory.ReadU32(Object + 0x58, Wake) || Wake == 0 ||
                !m_Memory.ReadU8(Wake, Marker) || Marker != 0x40 ||
                !m_Memory.ReadU32(Wake + 0x70, DrawSetup) || DrawSetup == 0 ||
                !m_Memory.ReadS16(Wake + 0x76, Alpha) || Alpha == 0)
            {
                continue;
            }
            Target = Wake;
            break;
        }
    }
    m_Memory.WriteU32(WaterWakeDrawTargetAddress, Target);
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
        uint8_t TripleBuffer = 0;
        m_Memory.ReadU8(TripleBufferActive, TripleBuffer);
        uint32_t ObjectMoveEntryWord = 0;
        uint32_t ObjectMoveResumeWord = 0;
        uint32_t ObjectMoveStubWord = 0;
        m_Memory.ReadU32(ObjectMoveEntry, ObjectMoveEntryWord);
        m_Memory.ReadU32(ObjectMoveResume, ObjectMoveResumeWord);
        m_Memory.ReadU32(ObjectMoveStub, ObjectMoveStubWord);
        uint32_t WakeGlobalFadeRaw = 0;
        m_Memory.ReadU32(WaterWakeGlobalFadeAddress, WakeGlobalFadeRaw);
        int32_t WakeGlobalFade = (int32_t)WakeGlobalFadeRaw;
        uint32_t WakeFallbackTarget = 0;
        m_Memory.ReadU32(WaterWakeDrawTargetAddress, WakeFallbackTarget);
        uint32_t WakeStockTarget = 0;
        uint32_t WakeStockCalled = 0;
        m_Memory.ReadU32(WaterWakeStockDrawTargetAddress, WakeStockTarget);
        m_Memory.ReadU32(WaterWakeStockDrawCalledAddress, WakeStockCalled);
        uint8_t WakeDirection = 0;
        uint8_t WakeSegments = 0;
        uint8_t WakeCapacity = 0;
        uint8_t WakeFrame = 0;
        int16_t WakeAlpha = 0;
        int16_t WakeIntensity = 0;
        int16_t WakeTimer = 0;
        int16_t WakeChildAlpha = 0;
        int16_t WakeSegmentLifetime = 0;
        uint8_t WakeChildDirection = 0;
        uint8_t WakeDrawCount = 0;
        uint8_t WakeDrawActive = 0;
        bool WakeChildValid = false;
        bool WakeChildIsSelf = false;
        bool WakeMesh = false;
        uint32_t WakeVertex = 0;
        int16_t WakeVertexX = 0;
        int16_t WakeVertexY = 0;
        int16_t WakeVertexZ = 0;
        uint32_t WakeCount = 0;
        uint32_t WakeObject = 0;
        uint32_t WakeRenderListIndex = 0;
        bool WakeValid = false;

        // wakeUpdateRipple is driven by the generic effects-object list, not
        // the player list. A wake block starts with its fixed 0x40 capacity;
        // scan that exact list and report the instance with most segments.
        uint32_t WakeObjectList = 0;
        uint32_t WakeObjectCount = 0;
        if (m_Memory.ReadU32(WaterWakeObjectListAddress, WakeObjectList) &&
            m_Memory.ReadU32(WaterWakeObjectCountAddress, WakeObjectCount) &&
            WakeObjectCount <= WaterWakeObjectLimit)
        {
            for (uint32_t Index = 0; Index < WakeObjectCount; Index++)
            {
                uint32_t Object = 0;
                uint32_t Wake = 0;
                uint8_t WakeMarker = 0;
                uint8_t Direction = 0;
                uint8_t Segments = 0;
                uint8_t Capacity = 0;
                uint8_t Frame = 0;
                int16_t Alpha = 0;
                int16_t Intensity = 0;
                int16_t Timer = 0;
                int16_t ChildAlpha = 0;
                uint32_t DrawBuffer = 0;
                uint32_t Child = 0;
                uint8_t ChildDirection = 0;
                bool ChildValid = false;
                uint8_t DrawCount = 0;
                uint8_t DrawActive = 0;
                int16_t DrawSpan = 0;
                uint32_t Vertex = 0;
                int16_t VertexX = 0;
                int16_t VertexY = 0;
                int16_t VertexZ = 0;
                if (!m_Memory.ReadU32(WakeObjectList + Index * sizeof(uint32_t), Object) ||
                    !m_Memory.ReadU32(Object + 0x58, Wake) || Wake == 0 ||
                    !m_Memory.ReadU8(Wake, WakeMarker) || WakeMarker != 0x40 ||
                    !m_Memory.ReadU8(Wake + 0x75, Direction) ||
                    !m_Memory.ReadU8(Wake + 0x3B, Segments) ||
                    !m_Memory.ReadU8(Wake + 0x01, Capacity) ||
                    !m_Memory.ReadU8(Wake + 0x74, Frame) ||
                    !m_Memory.ReadS16(Wake + 0x76, Alpha) ||
                    !m_Memory.ReadS16(Wake + 0x08, Intensity) ||
                    !m_Memory.ReadS16(Wake + 0x78, Timer) || !m_Memory.ReadU32(Wake + 0x10, DrawBuffer))
                {
                    continue;
                }

                ChildValid = m_Memory.ReadU32(Wake + 0x58, Child) && Child != 0 &&
                             m_Memory.IsRdramAddress(Child, 0x78) &&
                             m_Memory.ReadU8(Child + 0x75, ChildDirection) &&
                             m_Memory.ReadS16(Child + 0x76, ChildAlpha);

                if (DrawBuffer != 0 && m_Memory.ReadU8(Wake + 0x38, DrawCount))
                {
                    // wakeDraw treats +0x0E in each 16-byte entry as its
                    // non-zero vertex span. The entries themselves start at
                    // wake + 0x10, not at the texture/render object + 0x84.
                    const uint8_t SegmentCount = DrawCount > 64 ? 64 : DrawCount;
                    for (uint8_t SegmentIndex = 0; SegmentIndex < SegmentCount; SegmentIndex++)
                    {
                        int16_t Span = 0;
                        if (!m_Memory.ReadS16(DrawBuffer + SegmentIndex * 0x10 + 0x0E, Span))
                        {
                            break;
                        }
                        if (Span > 0)
                        {
                            DrawActive += 1;
                            if (Span > DrawSpan)
                            {
                                DrawSpan = Span;
                            }
                        }
                    }

                    if (DrawCount != 0 && m_Memory.ReadU32(DrawBuffer, Vertex) && Vertex != 0)
                    {
                        uint32_t VertexAddress = Vertex | 0x80000000;
                        m_Memory.ReadS16(VertexAddress + 0x00, VertexX);
                        m_Memory.ReadS16(VertexAddress + 0x02, VertexY);
                        m_Memory.ReadS16(VertexAddress + 0x04, VertexZ);
                    }
                }

                WakeCount += 1;
                if (!WakeValid || (DrawBuffer != 0 && !WakeMesh) ||
                    (DrawBuffer != 0 && WakeMesh && DrawActive > WakeDrawActive) ||
                    ((DrawBuffer != 0 || !WakeMesh) && DrawActive == WakeDrawActive && Segments > WakeSegments) ||
                    ((DrawBuffer != 0 || !WakeMesh) && DrawActive == WakeDrawActive && Segments == WakeSegments &&
                     Intensity > WakeIntensity))
                {
                    WakeValid = true;
                    WakeDirection = Direction;
                    WakeSegments = Segments;
                    WakeCapacity = Capacity;
                    WakeFrame = Frame;
                    WakeAlpha = Alpha;
                    WakeIntensity = Intensity;
                    WakeTimer = Timer;
                    WakeChildAlpha = ChildAlpha;
                    WakeSegmentLifetime = DrawSpan;
                    WakeChildDirection = ChildDirection;
                    WakeDrawCount = DrawCount;
                    WakeDrawActive = DrawActive;
                    WakeChildValid = ChildValid;
                    WakeChildIsSelf = ChildValid && Child == Wake;
                    WakeMesh = DrawBuffer != 0;
                    WakeVertex = Vertex;
                    WakeVertexX = VertexX;
                    WakeVertexY = VertexY;
                    WakeVertexZ = VertexZ;
                    WakeObject = Object;
                }
            }
        }

        if (WakeObject != 0)
        {
            for (uint32_t Index = 0; Index < GeneralRenderListLimit; Index++)
            {
                uint32_t RenderObject = 0;
                if (!m_Memory.ReadU32(GeneralRenderListAddress + Index * sizeof(uint32_t), RenderObject))
                {
                    break;
                }
                if (RenderObject == WakeObject)
                {
                    WakeRenderListIndex = Index + 1;
                    break;
                }
            }
        }
        uint8_t WakeOutputAlpha = 0;
        if (WakeValid)
        {
            WakeOutputAlpha = (uint8_t)(((uint32_t)(255 - (WakeGlobalFade >> 1)) * (uint16_t)WakeAlpha) >> 8);
        }
        uint32_t DroneLateralFlags = 0;
        uint32_t DroneLateralHookFlags = 0;
        m_Memory.ReadU32(DroneLateralFlagsAddress, DroneLateralFlags);
        m_Memory.ReadU32(DroneLateralHookFlagsAddress, DroneLateralHookFlags);
        // Calibration readout for moving the strafe axis onto the camera. Read
        // these while flying straight with no strafe held: the heading is the
        // camera forward then, which pins down how the yaw maps to X and Z.
        float DroneForwardSpeed = 0.0f;
        float DroneHeadingX = 0.0f;
        m_Memory.ReadF32(DroneLateralForwardSpeedAddress, DroneForwardSpeed);
        m_Memory.ReadF32(DroneLateralVelocityAddress, DroneHeadingX);
        g_Notify->DisplayMessage(
            0,
            stdstr_f("input %u Hz  video %u Hz  floyd%u/%u/state%u patch%u flags%u calls%u seen%u"
                     "  fwd%.2f lat%+.2f",
                     InputRate, FrameRate, m_DroneLateralActive ? 1 : 0,
                     m_DroneLateralApplied ? 1 : 0, m_DroneLateralState,
                     m_SidekickStrafeHookApplied ? 1 : 0, DroneLateralFlags, m_DroneLateralHookHits,
                     DroneLateralHookFlags, DroneForwardSpeed, DroneHeadingX)
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
        Signature[2] != 0xA22F0000)
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
    PatchLandingCinematicSkip(false);
    PatchIntroCinematicSkip(false);
    m_Memory.WriteU32(LandingCinematicSkipInputAddress, 0);
    ApplyViBudget(false);
    PatchSquaddieMove(false);
    PatchSquadsTimeStep(false);
    PatchObjectMove(false);
    PatchDroneLateralMove(false);
    PatchFloydCameraLateralMove(false);
    PatchSidekickVelocityLateralMove(false);
    PatchSidekickLateralMove(false);
    PatchSidekickStrafe(false);
    PatchSidekickPadControlProbe(false);
    PatchPlayerVelocity(false);
    m_Memory.WriteU32(DroneLateralFlagsAddress, 0);
    m_Memory.WriteF32(DroneLateralSideFactorAddress, 0.0f);
    m_Memory.WriteF32(DroneLateralVelocityAddress, 0.0f);
    m_Memory.WriteU32(DroneLateralPreviousObjectAddress, 0);
    PatchSchedulerRelease(false);
    PatchTripleBuffer(false);
    PatchWaterWakeRate(false);
    PatchWaterWakeRingRate(false);
    PatchWaterWakeDrawProbe(false);
    PatchWaterWakeCulling(false);
    PatchWaterWakeDrawFallback(false);
    PatchWaterWakeFrameRate(false);
    PatchWaterWakeStockDrawProbe(false);
    PatchFramePacing60(false);
    PatchFramePacing(false);
    if (m_Enabled || m_CameraPatchApplied || IsSupportedRom())
    {
        SetCameraCode(false, false, false);
        m_Memory.WriteF32(CameraHeightOffsetAddress, 0.0f);
    }
    m_Enabled = false;
    ClearCameraState();
    m_SprintApplied = false;
    m_SprintPositionValid = false;
    m_SprintPlayerObject = 0;
    m_SprintTimeValid = false;
    m_SprintBlend = 0.0f;
    m_SprintAnimationApplied = false;
    m_SprintAnimationValid = false;
    m_SprintAnimation = 0;
    m_DroneLateralApplied = false;
    m_DroneLateralPositionValid = false;
    m_DroneLateralObject = 0;
    m_DroneLateralCandidates.clear();
}

void CJetForceGeminiRuntime::ClearCameraState(void)
{
    m_CameraPatchApplied = false;
    m_CameraOverrideActive = false;
    m_CameraOverrideSuspended = false;
    m_TrackedCamera = 0;
    m_TrackedPlayerObject = 0;
    m_OrbitYawInitialized = false;
    m_CameraElevationReady = false;
    m_CameraHeightOffset = 0.0f;
    m_OrbitYaw = 0;
    m_AimFovReference = 0.0f;
    m_AimYawCarry = 0.0f;
    m_AimPitchCarry = 0.0f;
    m_BossAimReticleX = 0;
    m_BossAimYawApplied = 0;
    m_TopDownCounterInitialized = false;
    m_TopDownCounter = 0;
    m_TopDownHoldPolls = 0;
    m_MouseDeltaX = 0;
    m_MouseDeltaY = 0;
    m_QueuedMouseWheel = 0;
    m_FramePacingPatchApplied = false;
    m_FramePacing60PatchApplied = false;
    m_SchedulerReleasePatchApplied = false;
    m_TripleBufferPatchApplied = false;
    m_WaterWakeRatePatchApplied = false;
    m_WaterWakeRingRatePatchApplied = false;
    m_WaterWakeDrawProbeApplied = false;
    m_WaterWakeCullingPatchApplied = false;
    m_WaterWakeDrawFallbackPatchApplied = false;
    m_WaterWakeFrameRatePatchApplied = false;
    m_WaterWakeStockDrawProbeApplied = false;
    m_WaterWakeRatePatchStatus = 0;
    m_GameplayReady = false;
    m_SprintActive = false;
    m_BaseViRefreshRate = 0;
    m_ObjectMovePatchApplied = false;
    m_PlayerVelocityPatchApplied = false;
    m_SquaddieMovePatchApplied = false;
    m_SquaddieOverlayBase = 0;
    m_LandingCinematicSkipHookApplied = false;
    m_LandingCinematicSkipStubOriginal.clear();
    m_IntroCinematicSkipHookApplied = false;
    m_IntroCinematicSkipOverlayBase = 0;
    m_IntroCinematicSkipStubOriginal.clear();
    m_CinematicProbeDown = false;
    m_Fps60ToggleDown = false;
    m_Fps30ToggleDown = false;
    m_SyncAudioEnabledState = -1;
    m_HalveFrameCounter = 0;
    memset(m_HalvedEnemySlots, 0, sizeof(m_HalvedEnemySlots));
    m_WidescreenHudScopeForceDown = false;
    m_WidescreenHudScopeForced = false;
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

    // Both builds are accepted: every address and every payload now comes from
    // the table, so nothing here is spelled in US terms any more.
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

bool CJetForceGeminiRuntime::SetCameraCode(
    bool EnableFreeOrbit, bool EnableManualAim, bool InstallRuntime)
{
    if (InstallRuntime)
    {
        PatchManualAimCode(true);
    }

    const size_t PatchCount = sizeof(CameraCodePatches) / sizeof(CameraCodePatches[0]);
    GAME_HACK_CODE_WRITE Writes[PatchCount] = {};
    for (size_t i = 0; i < PatchCount; i++)
    {
        const CAMERA_CODE_PATCH & Patch = CameraCodePatches[i];
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
        uint32_t LegacyInstruction;
        if (GetLegacyCameraHeightInstruction(Patch.Address, LegacyInstruction) &&
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
    if (Enabled)
    {
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
    uint32_t PlayerObject, uint32_t PlayerData)
{
    uint8_t PlayerType;
    if (!m_OrbitYawInitialized ||
        !m_Memory.ReadU8(PlayerData + PlayerTypeOffset, PlayerType))
    {
        return;
    }

    int16_t FacingYaw = (int16_t)(0x8000 - (int32_t)m_OrbitYaw);
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

bool CJetForceGeminiRuntime::GetNormalCameraState(
    uint32_t PlayerData, bool & NormalCamera, bool & MouseCameraAllowed)
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

    const bool TopDownCameraActive = TopDownCameraWasUpdated(TopDownCounter);
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
        PlayerCameraAvailable && (NormalCamera || (m_CameraOverrideActive && !IsManualAimCameraMode(CameraMode)));
    return true;
}

float CJetForceGeminiRuntime::ClampCameraElevation(
    uint32_t PlayerObject, uint32_t Camera, float HeightOffset) const
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
        !m_Memory.ReadF32(CameraNativeYAddress, NativeCameraY) ||
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

bool CJetForceGeminiRuntime::ApplyMouseCamera(int32_t MouseX, int32_t MouseY, bool AimMode)
{
    uint32_t PlayerObject = 0;
    uint32_t PlayerData = 0;
    uint32_t Camera = 0;
    uint32_t JoyDisabled = 0;
    uint8_t CameraMode = 0;
    bool NormalCamera = false;
    bool MouseCameraAllowed = false;
    bool BasicCameraStateAvailable =
        GetPlayerData(PlayerObject, PlayerData) && GetControlCamera(Camera) &&
        m_Memory.ReadU32(DisableJoyAddress, JoyDisabled) &&
        m_Memory.ReadU8(PlayerData + PlayerCameraModeOffset, CameraMode);
    bool PreserveConstrainedCameras =
        g_Settings->LoadBool(Setting_JfgPreserveCameraInGameLimits);
    bool FreeCameraInJump =
        g_Settings->LoadBool(Setting_JfgFreeCameraInJump);
    bool JumpCameraMode =
        BasicCameraStateAvailable && CameraMode == PlayerCameraModeJump;
    bool FreeJumpCameraAllowed =
        JumpCameraMode && FreeCameraInJump;
    bool FreeCameraBlockedByJump =
        JumpCameraMode && !FreeCameraInJump;
    bool ConstrainedCameraStateAvailable =
        BasicCameraStateAvailable && GetNormalCameraState(PlayerData, NormalCamera, MouseCameraAllowed);
    bool FreeCameraStateAllowed =
        JoyDisabled == 0 && !FreeCameraBlockedByJump &&
        (FreeJumpCameraAllowed ||
         (PreserveConstrainedCameras ? (ConstrainedCameraStateAvailable && NormalCamera) : BasicCameraStateAvailable));
    bool EnableFreeOrbit =
        FreeCameraStateAllowed && !AimMode;
    bool EnableManualAim = BasicCameraStateAvailable && JoyDisabled == 0 && IsManualAimCameraMode(CameraMode) &&
                           (AimMode || CameraMode == PlayerCameraModeBossAim);
    bool CameraInputEnabled =
        FreeCameraStateAllowed;
    SetCameraCode(EnableFreeOrbit, EnableManualAim, true);

    if (!m_CameraPatchApplied || !BasicCameraStateAvailable)
    {
        m_CameraOverrideActive = false;
        m_CameraOverrideSuspended = false;
        m_TrackedCamera = 0;
        m_TrackedPlayerObject = 0;
        m_OrbitYawInitialized = false;
        m_CameraElevationReady = false;
        return CameraInputEnabled;
    }

    if (Camera != m_TrackedCamera || PlayerObject != m_TrackedPlayerObject)
    {
        m_TrackedCamera = Camera;
        m_TrackedPlayerObject = PlayerObject;
        m_CameraOverrideActive = false;
        m_CameraOverrideSuspended = false;
        m_OrbitYawInitialized = false;
        m_CameraElevationReady = false;
        m_CameraHeightOffset = 0.0f;
        m_Memory.WriteF32(CameraHeightOffsetAddress, 0.0f);
        m_OrbitYaw = 0;
    }

    if (PreserveConstrainedCameras && !FreeJumpCameraAllowed &&
        (!ConstrainedCameraStateAvailable || JoyDisabled != 0 || !MouseCameraAllowed))
    {
        m_CameraOverrideActive = false;
        m_CameraOverrideSuspended = false;
        m_OrbitYawInitialized = false;
        m_CameraElevationReady = false;
        return false;
    }

    if (AimMode || JoyDisabled != 0 || FreeCameraBlockedByJump ||
        (PreserveConstrainedCameras && !FreeJumpCameraAllowed &&
         CameraMode != PlayerCameraModeNormal))
    {
        if (m_CameraOverrideActive)
        {
            if (AimMode)
            {
                AlignPlayerYawToOrbitCamera(PlayerObject, PlayerData);
            }
            m_Memory.WriteS16(Camera + CameraPitchOffset, 0);
            m_Memory.WriteS16(PlayerData + PlayerCameraYawOffset, 0);
            m_CameraOverrideActive = false;
            m_CameraOverrideSuspended = true;
            m_OrbitYawInitialized = false;
        }
        m_CameraElevationReady = false;
        return CameraInputEnabled;
    }

    m_CameraOverrideSuspended = false;
    m_CameraOverrideActive = true;
    if (!m_OrbitYawInitialized)
    {
        if (!m_Memory.ReadS16(PlayerData + PlayerCameraOrbitYawOffset, m_OrbitYaw))
        {
            return CameraInputEnabled;
        }
        m_OrbitYawInitialized = true;
    }

    int16_t CameraBaseYaw;
    if (!GetCameraBaseYaw(PlayerObject, PlayerData, CameraBaseYaw))
    {
        return CameraInputEnabled;
    }

    m_OrbitYaw = (int16_t)((int64_t)m_OrbitYaw + (int64_t)MouseX * MouseCameraYawSensitivity);
    m_Memory.WriteS16(PlayerData + PlayerCameraOrbitYawOffset, m_OrbitYaw);
    m_Memory.WriteS16(
        PlayerData + PlayerCameraYawOffset, (int16_t)((int32_t)m_OrbitYaw - (int32_t)CameraBaseYaw));
    m_Memory.WriteU8(PlayerData + PlayerCameraCenterOffset, 0);

    if (JumpCameraMode)
    {
        m_CameraHeightOffset = 0.0f;
        m_Memory.WriteF32(CameraHeightOffsetAddress, m_CameraHeightOffset);
        m_CameraElevationReady = false;
        return CameraInputEnabled;
    }

    m_CameraHeightOffset = ClampCameraHeight(
        m_CameraHeightOffset + (float)MouseY * MouseCameraHeightSensitivity);
    if (m_CameraElevationReady)
    {
        m_CameraHeightOffset =
            ClampCameraElevation(PlayerObject, Camera, m_CameraHeightOffset);
    }
    else
    {
        m_CameraElevationReady = true;
    }
    m_Memory.WriteF32(CameraHeightOffsetAddress, m_CameraHeightOffset);
    m_Memory.WriteS16(Camera + CameraPitchOffset, 0);
    return CameraInputEnabled;
}

void CJetForceGeminiRuntime::ApplyCameraRelativeStrafe(void)
{
    uint32_t PlayerObject;
    uint32_t PlayerData;
    uint32_t JoyDisabled;
    uint8_t CameraMode;
    if (!m_CameraOverrideActive || !m_OrbitYawInitialized ||
        !GetPlayerData(PlayerObject, PlayerData) ||
        !m_Memory.ReadU32(DisableJoyAddress, JoyDisabled) || JoyDisabled != 0 ||
        !m_Memory.ReadU8(PlayerData + PlayerCameraModeOffset, CameraMode) ||
        CameraMode != PlayerCameraModeNormal)
    {
        return;
    }

    AlignPlayerYawToOrbitCamera(PlayerObject, PlayerData);
    m_Memory.WriteS16(PlayerData + PlayerCameraYawOffset, 0);
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
    const KEYBOARD_MOUSE_STATE & Input, BUTTONS & Buttons, bool ApplyCamera)
{
    // Keyboard/mouse mode replaces controller one rather than adding to the
    // active plugin mapping. Clear it first so an empty custom-input frame is
    // also a neutral N64 controller state.
    Buttons.Value = 0;

    // The input backend exposes physical keys. Keep the usual ZQSD and WASD
    // positions, then accept their printed-letter counterparts too so either
    // layout stays usable when Windows and the physical keyboard differ.
    bool Forward = KeyDown(Input, KeyboardMouseKey_W) || KeyDown(Input, KeyboardMouseKey_Z);
    bool Backward = KeyDown(Input, KeyboardMouseKey_S);
    bool Left = KeyDown(Input, KeyboardMouseKey_A) || KeyDown(Input, KeyboardMouseKey_Q);
    bool Right = KeyDown(Input, KeyboardMouseKey_D);
    bool CUp = KeyDown(Input, KeyboardMouseKey_Space);
    bool CDown = KeyDown(Input, KeyboardMouseKey_LeftControl) ||
                 KeyDown(Input, KeyboardMouseKey_RightControl);
    bool Sprint = KeyDown(Input, KeyboardMouseKey_LeftShift);
    bool A = KeyDown(Input, KeyboardMouseKey_E);
    bool B = KeyDown(Input, KeyboardMouseKey_F);
    // Weapon cycling uses the same A/B buttons in all on-foot gameplay states.
    // Keep the wheel out of drone mode below, where A/B are the throttle.
    bool ScrollUp = Input.MouseWheel > 0;
    bool ScrollDown = Input.MouseWheel < 0;
    bool Start = KeyDown(Input, KeyboardMouseKey_Return);
    bool MouseLeft = MouseButtonDown(Input, MouseButtonLeft);
    bool MouseRight = MouseButtonDown(Input, MouseButtonRight);
    bool AimMode = MouseRight;
    bool CameraRelativeLateralMovement =
        false;

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
    const bool DroneLateralMovement =
        DroneMode && g_Settings->LoadBool(Setting_JfgDroneLateralMovement) && Left != Right;
    m_DroneLateralActive = DroneLateralMovement;
    m_DroneLateralRight = Right;
    uint32_t DroneLateralFlags = 0;
    // The stub stays armed for the whole drone section rather than only while a
    // strafe key is down, so the lateral velocity it holds can decay through
    // drag after the key is released instead of being frozen mid-drift.
    float DroneLateralSideFactor = 0.0f;
    float DroneLateralRightX = 0.0f;
    float DroneLateralRightZ = 0.0f;
    int16_t DroneCameraYaw = 0;
    if (DroneMode && g_Settings->LoadBool(Setting_JfgDroneLateralMovement) &&
        m_Memory.ReadS16(PlayerObject + DroneYawSourceOffsetA, DroneCameraYaw))
    {
        DroneLateralFlags = DroneLateralActive | (Right ? DroneLateralRight : 0);
        const float Angle = (float)DroneCameraYaw * DroneLateralAngleScale;
        DroneLateralRightX = cosf(Angle);
        DroneLateralRightZ = -sinf(Angle);
        if (DroneLateralMovement)
        {
            DroneLateralSideFactor = (Right ? -1.0f : 1.0f) * DroneLateralSideThrust;
        }
    }
    // The video pass runs independently from the controller poll. Only the
    // latter is consumed by Floyd's game logic; writing here from both passes
    // could clear a held D between the poll and objMoveXYZ.
    if (!ApplyCamera)
    {
        m_Memory.WriteU32(DroneLateralFlagsAddress, DroneLateralFlags);
        m_Memory.WriteF32(DroneLateralSideFactorAddress, DroneLateralSideFactor);
        m_Memory.WriteF32(DroneLateralDragAddress, DroneLateralDrag);
        m_Memory.WriteF32(DroneLateralMaxSpeedAddress, DroneLateralMaxSpeed);
        m_Memory.WriteF32(DroneLateralRightXAddress, DroneLateralRightX);
        m_Memory.WriteF32(DroneLateralRightZAddress, DroneLateralRightZ);
        if (!DroneLateralMovement)
        {
            m_Memory.WriteU32(DroneLateralHookHitsAddress, 0);
            m_Memory.WriteU32(DroneLateralHookFlagsAddress, 0);
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
    int32_t MouseX = SpendMouse ? m_MouseDeltaX : 0;
    int32_t MouseY = SpendMouse ? m_MouseDeltaY : 0;
    if (SpendMouse)
    {
        m_MouseDeltaX = 0;
        m_MouseDeltaY = 0;
    }

    if (ApplyCamera)
    {
        if (DroneMode)
        {
            // The drone owns its own camera, so the on foot orbit has nothing to
            // say here. Dropping the tracked yaw makes it read the game again on
            // the way out rather than snapping to where it left off.
            m_OrbitYawInitialized = false;

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
            bool CameraInputEnabled = ApplyMouseCamera(MouseX, MouseY, AimMode);
            if (CameraInputEnabled && !AimMode && CameraRelativeLateralMovement && Left != Right)
            {
                ApplyCameraRelativeStrafe();
            }
        }
    }

    bool InputActive =
        Forward || Backward || Left || Right || CUp || CDown || A || B || Start ||
        MouseLeft || MouseRight || MouseX != 0 || MouseY != 0 || Input.MouseWheel != 0;
    if (!InputActive)
    {
        return;
    }

    // The drone flies with the stick steering its reticle and the camera turning
    // to follow, so the mouse pushes the stick rather than writing an angle, and
    // A and B become the throttle.
    if (DroneMode)
    {
        Buttons.Value = 0;
        Buttons.Z_TRIG = MouseLeft;
        Buttons.START_BUTTON = Start;
        // Strafing deliberately does not request the throttle. It did while the
        // hack rotated the velocity the engine produced, since there had to be
        // one to rotate, but the lateral thruster now carries its own velocity
        // and asking for A on top only adds forward thrust.
        Buttons.A_BUTTON = Forward || A;
        Buttons.B_BUTTON = Backward || B;
        Buttons.L_CBUTTON = false;
        Buttons.R_CBUTTON = false;
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

    Buttons.Z_TRIG = MouseLeft;
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
    bool StrafeOnCButtons =
        AimMode || CameraRelativeLateralMovement || BossCam || PostureStrafe;
    if (Left && StrafeOnCButtons)
    {
        Buttons.L_CBUTTON = true;
    }
    if (Right && StrafeOnCButtons)
    {
        Buttons.R_CBUTTON = true;
    }

    if (AimMode)
    {
        if (ApplyCamera)
        {
            ApplyManualAimMouse(MouseX, MouseY);
        }
        Buttons.U_CBUTTON = Forward || CUp;
        Buttons.D_CBUTTON = Backward || CDown;
        Buttons.X_AXIS = 0;
        Buttons.Y_AXIS = 0;
        return;
    }

    Buttons.U_CBUTTON = CUp;
    Buttons.D_CBUTTON = CDown;
    Buttons.Y_AXIS = (Forward ? JfgStickLimit : 0) - (Backward ? JfgStickLimit : 0);
    if (!StrafeOnCButtons)
    {
        Buttons.X_AXIS = (Right ? JfgStickLimit : 0) - (Left ? JfgStickLimit : 0);
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

// Floyd has no retail strafe control. Q/D therefore hold the game's forward
// thrust, then this pass turns the resulting displacement of the exact object
// that sidekickpadControl is controlling. The probe supplies that object;
// using the control camera here moved only the camera, not Floyd.
void CJetForceGeminiRuntime::ApplyDroneLateralMovement(void)
{
    m_DroneLateralApplied = false;
    m_DroneLateralDebugStatus = 0;

    uint32_t Object = 0;
    float PositionX = 0.0f;
    float PositionZ = 0.0f;
    if (!m_Memory.ReadU32(SidekickPadProbeObjectAddress, Object) || Object == 0 ||
        !m_Memory.IsRdramAddress(Object, TransformZOffset + sizeof(float)))
    {
        m_DroneLateralDebugStatus = 1;
        m_DroneLateralPositionValid = false;
        m_DroneLateralObject = 0;
        return;
    }
    if (!m_Memory.ReadF32(Object + TransformXOffset, PositionX) ||
        !m_Memory.ReadF32(Object + TransformZOffset, PositionZ) ||
        !IsCameraFloat(PositionX) || !IsCameraFloat(PositionZ))
    {
        m_DroneLateralDebugStatus = 2;
        m_DroneLateralPositionValid = false;
        m_DroneLateralObject = Object;
        return;
    }
    if (!m_DroneLateralPositionValid || m_DroneLateralObject != Object)
    {
        m_DroneLateralDebugStatus = 3;
        m_DroneLateralPositionValid = true;
        m_DroneLateralObject = Object;
        m_DroneLateralPreviousX = PositionX;
        m_DroneLateralPreviousZ = PositionZ;
        return;
    }

    const float DeltaX = PositionX - m_DroneLateralPreviousX;
    const float DeltaZ = PositionZ - m_DroneLateralPreviousZ;
    m_DroneLateralPreviousX = PositionX;
    m_DroneLateralPreviousZ = PositionZ;
    if (!m_DroneLateralActive)
    {
        m_DroneLateralDebugStatus = 4;
        return;
    }
    if (fabs(DeltaX) > SprintMaximumStep || fabs(DeltaZ) > SprintMaximumStep)
    {
        m_DroneLateralDebugStatus = 5;
        return;
    }
    if (DeltaX == 0.0f && DeltaZ == 0.0f)
    {
        m_DroneLateralDebugStatus = 6;
        return;
    }

    const float LateralX = m_DroneLateralRight ? -DeltaZ : DeltaZ;
    const float LateralZ = m_DroneLateralRight ? DeltaX : -DeltaX;
    const float StrafeX = PositionX - DeltaX + LateralX;
    const float StrafeZ = PositionZ - DeltaZ + LateralZ;
    if (!IsCameraFloat(StrafeX) || !IsCameraFloat(StrafeZ))
    {
        m_DroneLateralDebugStatus = 7;
        return;
    }
    if (m_Memory.WriteF32(Object + TransformXOffset, StrafeX) &&
        m_Memory.WriteF32(Object + TransformZOffset, StrafeZ))
    {
        m_DroneLateralDebugStatus = 8;
        m_DroneLateralPreviousX = StrafeX;
        m_DroneLateralPreviousZ = StrafeZ;
        m_DroneLateralApplied = true;
        return;
    }
    m_DroneLateralDebugStatus = 9;
}
