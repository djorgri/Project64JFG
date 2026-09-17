#pragma once

#include <stdint.h>

// Per-build address table for the Jet Force Gemini hacks.
//
// Everything the hacks patch or read lives at a fixed address, and those
// addresses move between builds. The game *structures* do not: the field
// offsets the hacks use appear at the same offsets in both images, which is why
// only this table is needed rather than a second implementation. The evidence,
// the method and the per-entry provenance are in Docs/JFG_KIOSK_PORT.md.
//
// A zero means the feature has no target on that build. The Kiosk demo has no
// landing cinematic, so its skip has nothing to hook; callers must treat zero as
// "unavailable" rather than patching address zero.
struct JFG_ADDRESSES
{
    // Which build this table describes, as the Project64 game identifier.
    const char * RomIdentifier;

    uint32_t WaterWakeLegacyCallSite;
    uint32_t ObjectMoveEntry;
    uint32_t ObjectMoveResume;
    uint32_t WaterWakeCullingEntry;
    uint32_t WaterWakeStockDrawEntry;
    uint32_t WaterWakeDrawFallbackEntry;
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
    uint32_t SidekickControlEntry;
    uint32_t SidekickControlProbeEntry;
    uint32_t SidekickVelocityLateralEntry;
    uint32_t SidekickVelocityLateralDelay;
    uint32_t SidekickVelocityLateralResume;
    uint32_t SidekickLateralMoveInputLegacyEntry;
    uint32_t SidekickStrafeEntry;
    uint32_t SidekickStrafeDelay;
    uint32_t SidekickLateralMoveOldTailEntry;
    uint32_t SidekickLateralMoveEntry;
    uint32_t SidekickControlEnd;
    uint32_t CameraAngleHelper;
    uint32_t PlayerVelocityEntry;
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
    uint32_t TripleBufferRequest;
    uint32_t PlayerVelocityStub;
    uint32_t FloydMoveHookStub;
    uint32_t SidekickStrafeStub;
    uint32_t SidekickVerticalStub;
    uint32_t SidekickControlProbeStub;
    uint32_t SidekickPadProbeStub;
    uint32_t ObjectMoveStub;
    uint32_t FloydCameraLateralStub;
    uint32_t SidekickLateralMoveStub;
    uint32_t SidekickVelocityLateralStub;
    uint32_t LandingCinematicSkipStub;
    uint32_t WaterWakeRingRateEntry;
    uint32_t WaterWakeUpdate;
    uint32_t WaterWakeFrameRateEntry;
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
    uint32_t FloydCameraPreviousXAddress;
    uint32_t FloydCameraPreviousZAddress;
    uint32_t FloydCameraPreviousObjectAddress;
    uint32_t LandingCinematicSkipInputAddress;
    uint32_t DroneLateralDragAddress;
    uint32_t DroneLateralForwardSpeedAddress;
    uint32_t DroneLateralRightXAddress;
    uint32_t SidekickPadProbeStateAddress;
    uint32_t DroneLateralPreviousXAddress;
    uint32_t DroneLateralPreviousZAddress;
    uint32_t DroneLateralPreviousObjectAddress;
    uint32_t SidekickPadProbeObjectAddress;
    uint32_t EnemyHalveFlagAddress;
    uint32_t DroneLateralFlagsAddress;
    uint32_t DroneLateralHookHitsAddress;
    uint32_t WaterWakeGateCounter;
    uint32_t DroneLateralHookFlagsAddress;
    uint32_t WaterWakeDrawFallbackCalledAddress;
    uint32_t WaterWakeStockDrawTargetAddress;
    uint32_t WaterWakeStockDrawCalledAddress;
    uint32_t SidekickPadProbeActorAddress;
    uint32_t WaterWakeGateStub;
    uint32_t WaterWakeDrawFallbackStub;
    uint32_t SquaddieXStub;
    uint32_t SquaddieZStub;
    uint32_t RobotMissionAddress;
    uint32_t MultiplayerGameAddress;
    uint32_t CooperativeGameAddress;
    uint32_t WaterWakeGlobalFadeAddress;
    uint32_t WaterWakeObjectListAddress;
    uint32_t WaterWakeObjectCountAddress;
    uint32_t PlayerListAddress;
    uint32_t PlayerCountAddress;
    uint32_t GeneralRenderListAddress;
    uint32_t DisableJoyAddress;
    uint32_t ControlCameraAddress;
    uint32_t CameraActiveOverrideBase;
    uint32_t CameraArrayAddress;
    uint32_t CameraFovAddress;
    uint32_t LobbyCameraInUseAddress;
    uint32_t StaticCameraInUseAddress;
    uint32_t OverlayTableAddress;
    uint32_t TripleBufferActive;
    uint32_t CurrentScreenAddress;
    uint32_t AnimseqCameraAddress;

    // Instruction words that differ between the builds: relocated call
    // targets, moved globals, and sites where the two compiles simply
    // chose different registers. Everything else the hacks verify or
    // write is identical in both images.
    uint32_t CameraHelperCallWord;
    uint32_t FramePacing60SignatureWord0;
    uint32_t FramePacingSignatureWord1;
    uint32_t ManualAimCursorXStoreWord;
    uint32_t ManualAimCursorYStoreWord;
    uint32_t SchedulerFrameGateAddWord;
    uint32_t SchedulerSignatureWord0;
    uint32_t SchedulerSignatureWord2;
};

extern const JFG_ADDRESSES JfgUsAddresses;
extern const JFG_ADDRESSES JfgKioskAddresses;

// The table for the ROM currently loaded, or nullptr when it is not one of the
// builds above. Valid only while a ROM is loaded.
const JFG_ADDRESSES * JfgAddresses(void);
