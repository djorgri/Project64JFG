#pragma once

#include <stdint.h>

// Per-build address table for the Jet Force Gemini hacks.
//
// Everything the hacks patch or read lives at a fixed address, and those
// addresses move between builds. The game *structures* do not: the field
// offsets the hacks use appear at the same offsets in every image, which is why
// only this table is needed rather than a second implementation. The evidence,
// the method and the per-entry provenance are in Docs/JFG_KIOSK_PORT.md and
// Docs/JFG_PAL_PORT.md.
//
// A zero means the feature has no target on that build. The Kiosk demo has no
// landing cinematic, so its skip has nothing to hook; callers must treat zero as
// "unavailable" rather than patching address zero.
struct JFG_ADDRESSES
{
    // Which build this table describes, as the Project64 game identifier.
    const char * RomIdentifier;

    uint32_t ObjectMoveEntry;
    uint32_t ObjectMoveResume;
    uint32_t CameraClampBranch;
    uint32_t CameraCenterBranch;
    uint32_t CameraOrbitGateBranch;
    uint32_t CameraOrbitCenterBranch;
    uint32_t CameraOrbitBranch;
    uint32_t CameraPositionXBaseCall;
    uint32_t CameraPositionZBaseCall;
    uint32_t CameraHeightBlendBase;
    uint32_t CameraLookHelperCall;
    uint32_t CameraYawHelperCall;
    uint32_t CameraPitchHelperCall;
    uint32_t CameraTopDownEntry;
    uint32_t SidekickStrafeEntry;
    uint32_t SidekickStrafeDelay;
    uint32_t CameraAngleHelper;
    uint32_t ManualAimXVelocityStore;
    uint32_t ManualAimYVelocityStore;
    uint32_t ManualAimCursorXStore;
    uint32_t ManualAimCursorYStore;
    uint32_t LandingCinematicSkipEntry;
    uint32_t LegacyLandingCinematicSkipEntry;
    uint32_t SchedulerSignatureBase;
    uint32_t SchedulerFrameGateAdd;
    uint32_t FramePacingSignatureBase;
    uint32_t FramePacingEscalateStore;
    uint32_t FramePacing60SignatureBase;
    uint32_t FramePacing60Branch;
    uint32_t SidekickStrafeStub;
    uint32_t SidekickVerticalStub;
    uint32_t SidekickPadProbeStub;
    uint32_t ObjectMoveStub;
    uint32_t LandingCinematicSkipStub;
    uint32_t WaterWakeRingRateEntry;
    uint32_t CameraHelperBase;
    uint32_t CameraTopDownHelperBase;
    uint32_t CameraNativeYAddress;
    uint32_t CameraHeightOffsetAddress;
    uint32_t CameraTopDownCounterAddress;
    uint32_t SidekickControlObjectAddress;
    uint32_t DroneLateralMaxSpeedAddress;
    uint32_t DroneLateralSideFactorAddress;
    uint32_t DroneLateralVelocityAddress;
    uint32_t DroneVerticalThrustAddress;
    uint32_t DroneVerticalVelocityAddress;
    uint32_t DroneLateralRightZAddress;
    uint32_t LandingCinematicSkipInputAddress;
    uint32_t DroneLateralDragAddress;
    uint32_t DroneLateralForwardSpeedAddress;
    uint32_t DroneLateralRightXAddress;
    uint32_t SidekickPadProbeStateAddress;
    uint32_t DroneLateralPreviousObjectAddress;
    uint32_t SidekickPadProbeObjectAddress;
    uint32_t EnemyHalveFlagAddress;
    uint32_t DroneLateralFlagsAddress;
    uint32_t DroneLateralHookHitsAddress;
    uint32_t SidekickPadProbeActorAddress;
    uint32_t RobotMissionAddress;
    uint32_t MultiplayerGameAddress;
    uint32_t CooperativeGameAddress;
    uint32_t WaterWakeObjectListAddress;
    uint32_t WaterWakeObjectCountAddress;
    uint32_t PlayerListAddress;
    uint32_t PlayerCountAddress;
    uint32_t DisableJoyAddress;
    uint32_t ControlCameraAddress;
    uint32_t CameraActiveOverrideBase;
    uint32_t CameraArrayAddress;
    uint32_t CameraFovAddress;
    uint32_t LobbyCameraInUseAddress;
    uint32_t StaticCameraInUseAddress;
    uint32_t OverlayTableAddress;
    uint32_t CurrentScreenAddress;
    uint32_t AnimseqCameraAddress;

    // Added with the PAL build: addresses the runtime used to spell as US
    // literals because the Kiosk port did not need them to move.
    uint32_t IntroCinematicSkipStub;
    uint32_t CurrentSceneAddress;
    uint32_t CurrentSetupAddress;
    uint32_t NextCharacterAddress;
    uint32_t LoadingAddress;
    uint32_t MainFrontInitFunction;
    uint32_t FrontCharSelectSetQuitModeFunction;
    uint32_t FrontGetModeFunction;
    uint32_t MainChangeLevelFunction;

    // Offsets inside relocatable overlays. The module is always resolved from
    // the live overlay table; only the position of the code inside it moves
    // between builds. Each anchor stands for a group of words whose spacing is
    // identical in every build, see the users in JetForceGemini.cpp.
    uint32_t FloydPadControlOffset;             // module 22
    uint32_t IntroCinematicSkipEntryOffset;     // module 57
    uint32_t LegacyIntroCinematicSkipEntryOffset;
    uint32_t LegacyIntroCinematicSkipFmvUpdateOffset;
    uint32_t TargetOverlayCursorXOffset;        // module 13
    uint32_t TargetOverlayCursorYOffset;        // module 13
    uint32_t TargetOverlayDrawOffset;           // module 13
    uint32_t BoyAimHelperOffset;                // module 16
    uint32_t BoyAimFirstGroupOffset;            // module 16
    uint32_t BoyAimSecondGroupOffset;           // module 16

    // Instruction words that differ between the builds: relocated call
    // targets, moved globals, and sites where the two compiles simply
    // chose different registers. Everything else the hacks verify or
    // write is identical in both images.
    uint32_t CameraHelperCallWord;
    uint32_t FramePacing60SignatureWord0;
    uint32_t FramePacingSignatureWord1;
    // The reticle cursor stores of controlGetManualAim and, ten words before
    // each, the `bne $tX, $at` of the division guard the runtime rewrites in
    // place; see FillManualAimCursorPatches.
    uint32_t ManualAimCursorXGuardWord;
    uint32_t ManualAimCursorYGuardWord;
    uint32_t ManualAimCursorXStoreWord;
    uint32_t ManualAimCursorYStoreWord;
    uint32_t SchedulerFrameGateAddWord;
    uint32_t SchedulerSignatureWord0;
    uint32_t SchedulerSignatureWord2;
    // viFrameSync keeps gVideoDeltaTime's address in $s1 on US and Kiosk and
    // in $s2 on PAL, so the two stores to it are spelled differently.
    uint32_t FramePacingEscalateStoreWord;
    uint32_t FramePacing60StoreWord;
};

extern const JFG_ADDRESSES JfgUsAddresses;
extern const JFG_ADDRESSES JfgKioskAddresses;
extern const JFG_ADDRESSES JfgPalAddresses;

// The table for the ROM currently loaded, or nullptr when it is not one of the
// builds above. Valid only while a ROM is loaded.
const JFG_ADDRESSES * JfgAddresses(void);
