#include "stdafx.h"

#include "JetForceGeminiAddresses.h"

#include <Project64-core/Settings/SettingType/SettingsType-Application.h>

// Both tables are generated from the two decompilation symbol maps rather than
// transcribed, and every Kiosk entry is verified: globals by symbol, code sites
// by matching the instruction shape with register allocation canonicalised, and
// the scratch areas by locating the equivalent padding. Docs/JFG_KIOSK_PORT.md
// records how each one was established and what confirms it.
//
// The order of the fields is the US address order. It has no meaning to the
// code, but it keeps the two tables readable side by side and makes an
// out-of-place entry visible.

const JFG_ADDRESSES JfgUsAddresses =
{
    "8A6009B6-94ACE150-C:45",

    /* WaterWakeLegacyCallSite             */ 0x80009278,
    /* ObjectMoveEntry                     */ 0x80009A24,
    /* ObjectMoveResume                    */ 0x80009A28,
    /* WaterWakeCullingEntry               */ 0x800146E4,
    /* WaterWakeStockDrawEntry             */ 0x80014C44,
    /* WaterWakeDrawFallbackEntry          */ 0x80014CA0,
    /* CameraClampBranch                   */ 0x8002D128,
    /* CameraCenterBranch                  */ 0x8002D154,
    /* CameraOrbitGateBranch               */ 0x8002DEC8,
    /* CameraOrbitCenterBranch             */ 0x8002DED8,
    /* CameraOrbitBranch                   */ 0x8002DF94,
    /* CameraPositionXBaseCall             */ 0x8002E0A8,
    /* CameraPositionZBaseCall             */ 0x8002E0E0,
    /* CameraHeightBlendBase               */ 0x8002E51C,
    /* CameraLookHelperCall                */ 0x8002E814,
    /* CameraYawHelperCall                 */ 0x8002EA34,
    /* CameraPitchHelperCall               */ 0x8002EA5C,
    /* CameraTopDownEntry                  */ 0x8002EB6C,
    /* SidekickControlEntry                */ 0x8002F728,
    /* SidekickControlProbeEntry           */ 0x8002F8AC,
    /* SidekickVelocityLateralEntry        */ 0x8002F8AC,
    /* SidekickVelocityLateralDelay        */ 0x8002F8B0,
    /* SidekickVelocityLateralResume       */ 0x8002F8B4,
    /* SidekickLateralMoveInputLegacyEntry */ 0x8002F8F4,
    /* SidekickStrafeEntry                 */ 0x80030060,
    /* SidekickStrafeDelay                 */ 0x80030064,
    /* SidekickLateralMoveOldTailEntry     */ 0x80031080,
    /* SidekickLateralMoveEntry            */ 0x80031088,
    /* SidekickControlEnd                  */ 0x8003109C,
    /* CameraAngleHelper                   */ 0x80033FA4,
    /* PlayerVelocityEntry                 */ 0x800341A4,
    /* ManualAimXVelocityStore             */ 0x8003AF14,
    /* ManualAimYVelocityStore             */ 0x8003AF2C,
    /* ManualAimCursorXStore               */ 0x8003B014,
    /* ManualAimCursorYStore               */ 0x8003B058,
    /* LandingCinematicSkipEntry           */ 0x80045FF0,
    /* LegacyLandingCinematicSkipEntry     */ 0x80046018,
    /* SchedulerSignatureBase              */ 0x800506D0,
    /* SchedulerFrameGateAdd               */ 0x800506D8,
    /* FramePacingSignatureBase            */ 0x800550DC,
    /* FramePacingEscalateStore            */ 0x800550E8,
    /* FramePacing60SignatureBase          */ 0x800550F0,
    /* FramePacing60Branch                 */ 0x800550F8,
    /* TripleBufferRequest                 */ 0x800551E8,
    /* PlayerVelocityStub                  */ 0x80066C00,
    /* FloydMoveHookStub                   */ 0x80066C00,
    /* SidekickStrafeStub                  */ 0x80066C00,
    /* SidekickVerticalStub                */ 0x80066F00,
    /* SidekickControlProbeStub            */ 0x80066D00,
    /* SidekickPadProbeStub                */ 0x80066D80,
    /* ObjectMoveStub                      */ 0x80066E00,
    /* FloydCameraLateralStub              */ 0x80066F00,
    /* SidekickLateralMoveStub             */ 0x80066F00,
    /* SidekickVelocityLateralStub         */ 0x80066F00,
    /* LandingCinematicSkipStub            */ 0x80067000,
    /* WaterWakeRingRateEntry              */ 0x8006AAC8,
    /* WaterWakeUpdate                     */ 0x8006B090,
    /* WaterWakeFrameRateEntry             */ 0x8006B1A0,
    /* CameraHelperBase                    */ 0x800968CC,
    /* CameraTopDownHelperBase             */ 0x80098D08,
    /* CameraNativeYAddress                */ 0x8009F244,
    /* CameraHeightOffsetAddress           */ 0x8009F248,
    /* CameraTopDownCounterAddress         */ 0x8009F24C,
    /* SidekickControlObjectAddress        */ 0x8009FCA0,
    /* DroneLateralMaxSpeedAddress         */ 0x8009FCA4,
    /* DroneLateralSideFactorAddress       */ 0x8009FCA8,
    /* DroneLateralVelocityAddress         */ 0x8009FCAC,
    /* DroneVerticalThrustAddress          */ 0x8009FCD0,
    /* DroneVerticalVelocityAddress        */ 0x8009FCD4,
    /* DroneLateralRightZAddress           */ 0x8009FCB0,
    /* FloydCameraPreviousXAddress         */ 0x8009FCB0,
    /* FloydCameraPreviousZAddress         */ 0x8009FCB4,
    /* FloydCameraPreviousObjectAddress    */ 0x8009FCB8,
    /* LandingCinematicSkipInputAddress    */ 0x8009FCBC,
    /* DroneLateralDragAddress             */ 0x8009FCC0,
    /* DroneLateralForwardSpeedAddress     */ 0x8009FCC4,
    /* DroneLateralRightXAddress           */ 0x8009FCC8,
    /* SidekickPadProbeStateAddress        */ 0x8009FCCC,
    /* DroneLateralPreviousXAddress        */ 0x8009FCD0,
    /* DroneLateralPreviousZAddress        */ 0x8009FCD4,
    /* DroneLateralPreviousObjectAddress   */ 0x8009FCD8,
    /* SidekickPadProbeObjectAddress       */ 0x8009FCDC,
    /* EnemyHalveFlagAddress               */ 0x8009FCE0,
    /* DroneLateralFlagsAddress            */ 0x8009FCE4,
    /* DroneLateralHookHitsAddress         */ 0x8009FCE8,
    /* WaterWakeGateCounter                */ 0x8009FCEC,
    /* DroneLateralHookFlagsAddress        */ 0x8009FCEC,
    /* WaterWakeDrawFallbackCalledAddress  */ 0x8009FCF0,
    /* WaterWakeStockDrawTargetAddress     */ 0x8009FCF4,
    /* WaterWakeStockDrawCalledAddress     */ 0x8009FCF8,
    /* SidekickPadProbeActorAddress        */ 0x8009FCFC,
    /* WaterWakeGateStub                   */ 0x8009FD00,
    /* WaterWakeDrawFallbackStub           */ 0x8009FD00,
    /* SquaddieXStub                       */ 0x8009FD60,
    /* SquaddieZStub                       */ 0x8009FD80,
    /* RobotMissionAddress                 */ 0x800A3208,
    /* MultiplayerGameAddress              */ 0x800A4FC4,
    /* CooperativeGameAddress              */ 0x800A4FC8,
    /* WaterWakeGlobalFadeAddress          */ 0x800A6950,
    /* WaterWakeObjectListAddress          */ 0x800F2CA4,
    /* WaterWakeObjectCountAddress         */ 0x800F2CA8,
    /* PlayerListAddress                   */ 0x800F2D0C,
    /* PlayerCountAddress                  */ 0x800F2D10,
    /* GeneralRenderListAddress            */ 0x800F2FB0,
    /* DisableJoyAddress                   */ 0x800F6DBC,
    /* ControlCameraAddress                */ 0x800F6DC0,
    /* CameraActiveOverrideBase            */ 0x800F6E58,
    /* CameraArrayAddress                  */ 0x800FA4D0,
    /* CameraFovAddress                    */ 0x800FB078,
    /* LobbyCameraInUseAddress             */ 0x800FB080,
    /* StaticCameraInUseAddress            */ 0x800FB084,
    /* OverlayTableAddress                 */ 0x800FEAA0,
    /* TripleBufferActive                  */ 0x800FECA6,
    /* CurrentScreenAddress                */ 0x800FECB0,
    /* AnimseqCameraAddress                */ 0x801045B8,

    /* CameraHelperCallWord                */ 0x0C00CFE9,
    /* FramePacing60SignatureWord0         */ 0x8DCEECCC,
    /* FramePacingSignatureWord1           */ 0x2442ECAB,
    /* ManualAimCursorXGuardWord           */ 0x15E10002,
    /* ManualAimCursorYGuardWord           */ 0x15610002,
    /* ManualAimCursorXStoreWord           */ 0xA7190000,
    /* ManualAimCursorYStoreWord           */ 0xA58D0000,
    /* SchedulerFrameGateAddWord           */ 0x25090001,
    /* SchedulerSignatureWord0             */ 0x8E480300,
    /* SchedulerSignatureWord2             */ 0x2D210002,
};

// The Kiosk demo, internal name "J F G DISPLAY".
const JFG_ADDRESSES JfgKioskAddresses =
{
    "DFD8AB47-3CDBEB89-C:45",

    /* WaterWakeLegacyCallSite             */ 0x80009260,
    /* ObjectMoveEntry                     */ 0x800099FC,
    /* ObjectMoveResume                    */ 0x80009A00,
    /* WaterWakeCullingEntry               */ 0x800144A8,
    /* WaterWakeStockDrawEntry             */ 0x80014908,
    /* WaterWakeDrawFallbackEntry          */ 0x80014964,
    /* CameraClampBranch                   */ 0x8002D134,
    /* CameraCenterBranch                  */ 0x8002D160,
    /* CameraOrbitGateBranch               */ 0x8002DED4,
    /* CameraOrbitCenterBranch             */ 0x8002DEE4,
    /* CameraOrbitBranch                   */ 0x8002DFA0,
    /* CameraPositionXBaseCall             */ 0x8002E0B4,
    /* CameraPositionZBaseCall             */ 0x8002E0EC,
    /* CameraHeightBlendBase               */ 0x8002E528,
    /* CameraLookHelperCall                */ 0x8002E820,
    /* CameraYawHelperCall                 */ 0x8002EA40,
    /* CameraPitchHelperCall               */ 0x8002EA68,
    /* CameraTopDownEntry                  */ 0x8002EB78,
    /* SidekickControlEntry                */ 0x8002F734,
    /* SidekickControlProbeEntry           */ 0x8002F8B8,
    /* SidekickVelocityLateralEntry        */ 0x8002F8B8,
    /* SidekickVelocityLateralDelay        */ 0x8002F8BC,
    /* SidekickVelocityLateralResume       */ 0x8002F8C0,
    /* SidekickLateralMoveInputLegacyEntry */ 0x8002F900,
    /* SidekickStrafeEntry                 */ 0x8003006C,
    /* SidekickStrafeDelay                 */ 0x80030070,
    /* SidekickLateralMoveOldTailEntry     */ 0x8003108C,
    /* SidekickLateralMoveEntry            */ 0x80031094,
    /* SidekickControlEnd                  */ 0x800310A8,
    /* CameraAngleHelper                   */ 0x80033FB8,
    /* PlayerVelocityEntry                 */ 0x800341B8,
    /* ManualAimXVelocityStore             */ 0x8003ACDC,
    /* ManualAimYVelocityStore             */ 0x8003ACF4,
    /* ManualAimCursorXStore               */ 0x8003ADDC,
    /* ManualAimCursorYStore               */ 0x8003AE20,
    /* LandingCinematicSkipEntry           */ 0x00000000,
    /* LegacyLandingCinematicSkipEntry     */ 0x00000000,
    /* SchedulerSignatureBase              */ 0x8005124C,
    /* SchedulerFrameGateAdd               */ 0x80051254,
    /* FramePacingSignatureBase            */ 0x80055BC0,
    /* FramePacingEscalateStore            */ 0x80055BCC,
    /* FramePacing60SignatureBase          */ 0x80055BD4,
    /* FramePacing60Branch                 */ 0x80055BDC,
    /* TripleBufferRequest                 */ 0x80055CCC,
    /* PlayerVelocityStub                  */ 0x800675B0,
    /* FloydMoveHookStub                   */ 0x800675B0,
    /* SidekickStrafeStub                  */ 0x800675B0,
    /* SidekickVerticalStub                */ 0x800677A0,
    /* SidekickControlProbeStub            */ 0x800676D0,
    /* SidekickPadProbeStub                */ 0x80067680,
    /* ObjectMoveStub                      */ 0x800676C0,
    /* FloydCameraLateralStub              */ 0x800677A0,
    /* SidekickLateralMoveStub             */ 0x800677A0,
    /* SidekickVelocityLateralStub         */ 0x800677A0,
    /* LandingCinematicSkipStub            */ 0x00000000,
    /* WaterWakeRingRateEntry              */ 0x8006AE0C,
    /* WaterWakeUpdate                     */ 0x8006B3D4,
    /* WaterWakeFrameRateEntry             */ 0x8006B4E4,
    /* CameraHelperBase                    */ 0x8009350C,
    /* CameraTopDownHelperBase             */ 0x80098CC8,
    /* CameraNativeYAddress                */ 0x8009FAD4,
    /* CameraHeightOffsetAddress           */ 0x8009FAD8,
    /* CameraTopDownCounterAddress         */ 0x8009FADC,
    /* SidekickControlObjectAddress        */ 0x800A0530,
    /* DroneLateralMaxSpeedAddress         */ 0x800A0534,
    /* DroneLateralSideFactorAddress       */ 0x800A0538,
    /* DroneLateralVelocityAddress         */ 0x800A053C,
    /* DroneVerticalThrustAddress          */ 0x800A0560,
    /* DroneVerticalVelocityAddress        */ 0x800A0564,
    /* DroneLateralRightZAddress           */ 0x800A0540,
    /* FloydCameraPreviousXAddress         */ 0x800A0540,
    /* FloydCameraPreviousZAddress         */ 0x800A0544,
    /* FloydCameraPreviousObjectAddress    */ 0x800A0548,
    /* LandingCinematicSkipInputAddress    */ 0x800A054C,
    /* DroneLateralDragAddress             */ 0x800A0550,
    /* DroneLateralForwardSpeedAddress     */ 0x800A0554,
    /* DroneLateralRightXAddress           */ 0x800A0558,
    /* SidekickPadProbeStateAddress        */ 0x800A055C,
    /* DroneLateralPreviousXAddress        */ 0x800A0560,
    /* DroneLateralPreviousZAddress        */ 0x800A0564,
    /* DroneLateralPreviousObjectAddress   */ 0x800A0568,
    /* SidekickPadProbeObjectAddress       */ 0x800A056C,
    /* EnemyHalveFlagAddress               */ 0x800A0570,
    /* DroneLateralFlagsAddress            */ 0x800A0574,
    /* DroneLateralHookHitsAddress         */ 0x800A0578,
    /* WaterWakeGateCounter                */ 0x800A057C,
    /* DroneLateralHookFlagsAddress        */ 0x800A057C,
    /* WaterWakeDrawFallbackCalledAddress  */ 0x800A0580,
    /* WaterWakeStockDrawTargetAddress     */ 0x800A0584,
    /* WaterWakeStockDrawCalledAddress     */ 0x800A0588,
    /* SidekickPadProbeActorAddress        */ 0x800A058C,
    /* WaterWakeGateStub                   */ 0x800A0590,
    /* WaterWakeDrawFallbackStub           */ 0x800A0590,
    /* SquaddieXStub                       */ 0x800A05F0,
    /* SquaddieZStub                       */ 0x800A0610,
    /* RobotMissionAddress                 */ 0x800A3A58,
    /* MultiplayerGameAddress              */ 0x800A5994,
    /* CooperativeGameAddress              */ 0x800A5998,
    /* WaterWakeGlobalFadeAddress          */ 0x800A7310,
    /* WaterWakeObjectListAddress          */ 0x800F38A4,
    /* WaterWakeObjectCountAddress         */ 0x800F38A8,
    /* PlayerListAddress                   */ 0x800F390C,
    /* PlayerCountAddress                  */ 0x800F3910,
    /* GeneralRenderListAddress            */ 0x800F3A70,
    /* DisableJoyAddress                   */ 0x800F787C,
    /* ControlCameraAddress                */ 0x800F7880,
    /* CameraActiveOverrideBase            */ 0x800F7918,
    /* CameraArrayAddress                  */ 0x800FAF90,
    /* CameraFovAddress                    */ 0x800FBB38,
    /* LobbyCameraInUseAddress             */ 0x800FBB40,
    /* StaticCameraInUseAddress            */ 0x800FBB44,
    /* OverlayTableAddress                 */ 0x800FF780,
    /* TripleBufferActive                  */ 0x800FF986,
    /* CurrentScreenAddress                */ 0x800FF990,
    /* AnimseqCameraAddress                */ 0x80105288,

    /* CameraHelperCallWord                */ 0x0C00CFEE,
    /* FramePacing60SignatureWord0         */ 0x8DCEF9AC,
    /* FramePacingSignatureWord1           */ 0x2442F98B,
    /* ManualAimCursorXGuardWord           */ 0x15C10002,
    /* ManualAimCursorYGuardWord           */ 0x17210002,
    /* ManualAimCursorXStoreWord           */ 0xA5F80000,
    /* ManualAimCursorYStoreWord           */ 0xA56C0000,
    /* SchedulerFrameGateAddWord           */ 0x254B0001,
    /* SchedulerSignatureWord0             */ 0x8E4A0300,
    /* SchedulerSignatureWord2             */ 0x2D610002,
};

namespace
{
// Every entry point of the runtime funnels through IsSupportedRom(), so this is
// asked several times per controller poll and per video interrupt. The ROM
// identifier is a string load and two comparisons; resolve it once per ROM and
// drop the answer when Game_IniKey changes, which is what a ROM load writes.
const JFG_ADDRESSES * ResolvedAddresses = nullptr;
bool ResolvedAddressesValid = false;
bool RomChangeCallbackRegistered = false;

void RomChanged(void *)
{
    ResolvedAddressesValid = false;
}
} // namespace

const JFG_ADDRESSES * JfgAddresses(void)
{
    if (!RomChangeCallbackRegistered)
    {
        g_Settings->RegisterChangeCB(Game_IniKey, nullptr, RomChanged);
        RomChangeCallbackRegistered = true;
        ResolvedAddressesValid = false;
    }
    if (!ResolvedAddressesValid)
    {
        const stdstr Rom = g_Settings->LoadStringVal(Game_IniKey);
        ResolvedAddresses = nullptr;
        if (Rom == JfgUsAddresses.RomIdentifier)
        {
            ResolvedAddresses = &JfgUsAddresses;
        }
        else if (Rom == JfgKioskAddresses.RomIdentifier)
        {
            ResolvedAddresses = &JfgKioskAddresses;
        }
        ResolvedAddressesValid = true;
    }
    return ResolvedAddresses;
}
