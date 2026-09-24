#include "stdafx.h"

#include "JetForceGeminiAddresses.h"

#include <Project64-core/Settings/SettingType/SettingsType-Application.h>

// The US and Kiosk tables are generated from the two decompilation symbol maps
// rather than transcribed, and every Kiosk entry is verified: globals by
// symbol, code sites by matching the instruction shape with register allocation
// canonicalised, and the scratch areas by locating the equivalent padding.
// Docs/JFG_KIOSK_PORT.md records how each one was established and what
// confirms it. The PAL build has no symbol map; its table was derived by
// aligning its code with the US image, see Docs/JFG_PAL_PORT.md.
//
// The order of the fields is the US address order. It has no meaning to the
// code, but it keeps the two tables readable side by side and makes an
// out-of-place entry visible.

const JFG_ADDRESSES JfgUsAddresses =
{
    "8A6009B6-94ACE150-C:45",

    /* ObjectMoveEntry                     */ 0x80009A24,
    /* ObjectMoveResume                    */ 0x80009A28,
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
    /* SidekickStrafeEntry                 */ 0x80030060,
    /* SidekickStrafeDelay                 */ 0x80030064,
    /* CameraAngleHelper                   */ 0x80033FA4,
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
    /* SidekickStrafeStub                  */ 0x80066C00,
    /* SidekickVerticalStub                */ 0x80066F00,
    /* SidekickPadProbeStub                */ 0x80066D80,
    /* ObjectMoveStub                      */ 0x80066E00,
    /* LandingCinematicSkipStub            */ 0x80067000,
    /* WaterWakeRingRateEntry              */ 0x8006AAC8,
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
    /* LandingCinematicSkipInputAddress    */ 0x8009FCBC,
    /* DroneLateralDragAddress             */ 0x8009FCC0,
    /* DroneLateralForwardSpeedAddress     */ 0x8009FCC4,
    /* DroneLateralRightXAddress           */ 0x8009FCC8,
    /* SidekickPadProbeStateAddress        */ 0x8009FCCC,
    /* DroneLateralPreviousObjectAddress   */ 0x8009FCD8,
    /* SidekickPadProbeObjectAddress       */ 0x8009FCDC,
    /* EnemyHalveFlagAddress               */ 0x8009FCE0,
    /* DroneLateralFlagsAddress            */ 0x8009FCE4,
    /* DroneLateralHookHitsAddress         */ 0x8009FCE8,
    /* SidekickPadProbeActorAddress        */ 0x8009FCFC,
    /* RobotMissionAddress                 */ 0x800A3208,
    /* MultiplayerGameAddress              */ 0x800A4FC4,
    /* CooperativeGameAddress              */ 0x800A4FC8,
    /* WaterWakeObjectListAddress          */ 0x800F2CA4,
    /* WaterWakeObjectCountAddress         */ 0x800F2CA8,
    /* PlayerListAddress                   */ 0x800F2D0C,
    /* PlayerCountAddress                  */ 0x800F2D10,
    /* DisableJoyAddress                   */ 0x800F6DBC,
    /* ControlCameraAddress                */ 0x800F6DC0,
    /* CameraActiveOverrideBase            */ 0x800F6E58,
    /* CameraArrayAddress                  */ 0x800FA4D0,
    /* CameraFovAddress                    */ 0x800FB078,
    /* LobbyCameraInUseAddress             */ 0x800FB080,
    /* StaticCameraInUseAddress            */ 0x800FB084,
    /* OverlayTableAddress                 */ 0x800FEAA0,
    /* CurrentScreenAddress                */ 0x800FECB0,
    /* AnimseqCameraAddress                */ 0x801045B8,

    /* IntroCinematicSkipStub              */ 0x80067200,
    /* CurrentSceneAddress                 */ 0x800A323C,
    /* CurrentSetupAddress                 */ 0x800A3248,
    /* NextCharacterAddress                */ 0x800A3260,
    /* LoadingAddress                      */ 0x800A3294,
    /* MainFrontInitFunction               */ 0x80047608,
    /* FrontCharSelectSetQuitModeFunction  */ 0x8005AAE8,
    /* FrontGetModeFunction                */ 0x80058A5C,
    /* MainChangeLevelFunction             */ 0x8004665C,

    /* FloydPadControlOffset               */ 0x000002A4,
    /* IntroCinematicSkipEntryOffset       */ 0x000000D0,
    /* LegacyIntroCinematicSkipEntryOffset */ 0x000000C0,
    /* LegacyIntroCinematicSkipFmvUpdateOffset */ 0x0000037C,
    /* TargetOverlayCursorXOffset          */ 0x0000041C,
    /* TargetOverlayCursorYOffset          */ 0x00000444,
    /* TargetOverlayDrawOffset             */ 0x000004A8,
    /* BoyAimHelperOffset                  */ 0x00003DB0,
    /* BoyAimFirstGroupOffset              */ 0x00004198,
    /* BoyAimSecondGroupOffset             */ 0x00004738,

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
    /* FramePacingEscalateStoreWord        */ 0xA22D0000,
    /* FramePacing60StoreWord              */ 0xA22F0000,
};

// The Kiosk demo, internal name "J F G DISPLAY".
const JFG_ADDRESSES JfgKioskAddresses =
{
    "DFD8AB47-3CDBEB89-C:45",

    /* ObjectMoveEntry                     */ 0x800099FC,
    /* ObjectMoveResume                    */ 0x80009A00,
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
    /* SidekickStrafeEntry                 */ 0x8003006C,
    /* SidekickStrafeDelay                 */ 0x80030070,
    /* CameraAngleHelper                   */ 0x80033FB8,
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
    /* SidekickStrafeStub                  */ 0x800675B0,
    /* SidekickVerticalStub                */ 0x800677A0,
    /* SidekickPadProbeStub                */ 0x80067680,
    /* ObjectMoveStub                      */ 0x800676C0,
    /* LandingCinematicSkipStub            */ 0x00000000,
    /* WaterWakeRingRateEntry              */ 0x8006AE0C,
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
    /* LandingCinematicSkipInputAddress    */ 0x800A054C,
    /* DroneLateralDragAddress             */ 0x800A0550,
    /* DroneLateralForwardSpeedAddress     */ 0x800A0554,
    /* DroneLateralRightXAddress           */ 0x800A0558,
    /* SidekickPadProbeStateAddress        */ 0x800A055C,
    /* DroneLateralPreviousObjectAddress   */ 0x800A0568,
    /* SidekickPadProbeObjectAddress       */ 0x800A056C,
    /* EnemyHalveFlagAddress               */ 0x800A0570,
    /* DroneLateralFlagsAddress            */ 0x800A0574,
    /* DroneLateralHookHitsAddress         */ 0x800A0578,
    /* SidekickPadProbeActorAddress        */ 0x800A058C,
    /* RobotMissionAddress                 */ 0x800A3A58,
    /* MultiplayerGameAddress              */ 0x800A5994,
    /* CooperativeGameAddress              */ 0x800A5998,
    /* WaterWakeObjectListAddress          */ 0x800F38A4,
    /* WaterWakeObjectCountAddress         */ 0x800F38A8,
    /* PlayerListAddress                   */ 0x800F390C,
    /* PlayerCountAddress                  */ 0x800F3910,
    /* DisableJoyAddress                   */ 0x800F787C,
    /* ControlCameraAddress                */ 0x800F7880,
    /* CameraActiveOverrideBase            */ 0x800F7918,
    /* CameraArrayAddress                  */ 0x800FAF90,
    /* CameraFovAddress                    */ 0x800FBB38,
    /* LobbyCameraInUseAddress             */ 0x800FBB40,
    /* StaticCameraInUseAddress            */ 0x800FBB44,
    /* OverlayTableAddress                 */ 0x800FF780,
    /* CurrentScreenAddress                */ 0x800FF990,
    /* AnimseqCameraAddress                */ 0x80105288,

    // The intro stub keeps the address the Kiosk has always used.
    /* IntroCinematicSkipStub              */ 0x80067200,
    /* CurrentSceneAddress                 */ 0x800A3A8C,
    /* CurrentSetupAddress                 */ 0x800A3A98,
    /* NextCharacterAddress                */ 0x800A3AB0,
    /* LoadingAddress                      */ 0x800A3AE0,
    /* MainFrontInitFunction               */ 0x80046BA4,
    /* FrontCharSelectSetQuitModeFunction  */ 0x00000000,
    /* FrontGetModeFunction                */ 0x8005950C,
    /* MainChangeLevelFunction             */ 0x80045E50,

    /* FloydPadControlOffset               */ 0x000002A0,
    /* IntroCinematicSkipEntryOffset       */ 0x000000D0,
    /* LegacyIntroCinematicSkipEntryOffset */ 0x000000C0,
    /* LegacyIntroCinematicSkipFmvUpdateOffset */ 0x0000037C,
    /* TargetOverlayCursorXOffset          */ 0x0000041C,
    /* TargetOverlayCursorYOffset          */ 0x00000444,
    /* TargetOverlayDrawOffset             */ 0x000004A8,
    /* BoyAimHelperOffset                  */ 0x0000392C,
    /* BoyAimFirstGroupOffset              */ 0x00003D14,
    /* BoyAimSecondGroupOffset             */ 0x000042B4,

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
    /* FramePacingEscalateStoreWord        */ 0xA22D0000,
    /* FramePacing60StoreWord              */ 0xA22F0000,
};

// The European release, "JET FORCE GEMINI" / NJFP, a 50 Hz build. Its main
// image is the US code with small, uniform shifts (0 to +0x270 in text, -0x5A0
// in most of bss), so every entry below was mapped from its US counterpart and
// then checked word for word against the PAL image. Docs/JFG_PAL_PORT.md has
// the method and the verification.
const JFG_ADDRESSES JfgPalAddresses =
{
    "68D7A1DE-0079834A-C:50",

    /* ObjectMoveEntry                     */ 0x80009A04,
    /* ObjectMoveResume                    */ 0x80009A08,
    /* CameraClampBranch                   */ 0x8002D1A0,
    /* CameraCenterBranch                  */ 0x8002D1CC,
    /* CameraOrbitGateBranch               */ 0x8002DF40,
    /* CameraOrbitCenterBranch             */ 0x8002DF50,
    /* CameraOrbitBranch                   */ 0x8002E00C,
    /* CameraPositionXBaseCall             */ 0x8002E120,
    /* CameraPositionZBaseCall             */ 0x8002E158,
    /* CameraHeightBlendBase               */ 0x8002E594,
    /* CameraLookHelperCall                */ 0x8002E88C,
    /* CameraYawHelperCall                 */ 0x8002EAAC,
    /* CameraPitchHelperCall               */ 0x8002EAD4,
    /* CameraTopDownEntry                  */ 0x8002EBE4,
    /* SidekickStrafeEntry                 */ 0x800300F0,
    /* SidekickStrafeDelay                 */ 0x800300F4,
    /* CameraAngleHelper                   */ 0x8003403C,
    /* ManualAimXVelocityStore             */ 0x8003B010,
    /* ManualAimYVelocityStore             */ 0x8003B028,
    /* ManualAimCursorXStore               */ 0x8003B110,
    /* ManualAimCursorYStore               */ 0x8003B154,
    /* LandingCinematicSkipEntry           */ 0x800460F0,
    /* LegacyLandingCinematicSkipEntry     */ 0x80046118,
    /* SchedulerSignatureBase              */ 0x800508F0,
    /* SchedulerFrameGateAdd               */ 0x800508F8,
    /* FramePacingSignatureBase            */ 0x800552B8,
    /* FramePacingEscalateStore            */ 0x800552C4,
    /* FramePacing60SignatureBase          */ 0x800552CC,
    /* FramePacing60Branch                 */ 0x800552D4,
    // The diagnostic block the stubs borrow is byte-identical to the US one,
    // 0x210 bytes further on, so the US layout carries over unchanged.
    /* SidekickStrafeStub                  */ 0x80066E10,
    /* SidekickVerticalStub                */ 0x80067110,
    /* SidekickPadProbeStub                */ 0x80066F90,
    /* ObjectMoveStub                      */ 0x80067010,
    /* LandingCinematicSkipStub            */ 0x80067210,
    /* WaterWakeRingRateEntry              */ 0x8006ACD8,
    // The tail of the text segment, with both zero gaps, moved by +0x270.
    /* CameraHelperBase                    */ 0x80096B3C,
    /* CameraTopDownHelperBase             */ 0x80098F78,
    /* CameraNativeYAddress                */ 0x8009F4B4,
    /* CameraHeightOffsetAddress           */ 0x8009F4B8,
    /* CameraTopDownCounterAddress         */ 0x8009F4BC,
    /* SidekickControlObjectAddress        */ 0x8009FF10,
    /* DroneLateralMaxSpeedAddress         */ 0x8009FF14,
    /* DroneLateralSideFactorAddress       */ 0x8009FF18,
    /* DroneLateralVelocityAddress         */ 0x8009FF1C,
    /* DroneVerticalThrustAddress          */ 0x8009FF40,
    /* DroneVerticalVelocityAddress        */ 0x8009FF44,
    /* DroneLateralRightZAddress           */ 0x8009FF20,
    /* LandingCinematicSkipInputAddress    */ 0x8009FF2C,
    /* DroneLateralDragAddress             */ 0x8009FF30,
    /* DroneLateralForwardSpeedAddress     */ 0x8009FF34,
    /* DroneLateralRightXAddress           */ 0x8009FF38,
    /* SidekickPadProbeStateAddress        */ 0x8009FF3C,
    /* DroneLateralPreviousObjectAddress   */ 0x8009FF48,
    /* SidekickPadProbeObjectAddress       */ 0x8009FF4C,
    /* EnemyHalveFlagAddress               */ 0x8009FF50,
    /* DroneLateralFlagsAddress            */ 0x8009FF54,
    /* DroneLateralHookHitsAddress         */ 0x8009FF58,
    /* SidekickPadProbeActorAddress        */ 0x8009FF6C,
    /* RobotMissionAddress                 */ 0x800A3478,
    /* MultiplayerGameAddress              */ 0x800A5244,
    /* CooperativeGameAddress              */ 0x800A5248,
    /* WaterWakeObjectListAddress          */ 0x800F2704,
    /* WaterWakeObjectCountAddress         */ 0x800F2708,
    /* PlayerListAddress                   */ 0x800F276C,
    /* PlayerCountAddress                  */ 0x800F2770,
    /* DisableJoyAddress                   */ 0x800F681C,
    /* ControlCameraAddress                */ 0x800F6820,
    /* CameraActiveOverrideBase            */ 0x800F68B8,
    /* CameraArrayAddress                  */ 0x800F9F30,
    /* CameraFovAddress                    */ 0x800FAAD8,
    /* LobbyCameraInUseAddress             */ 0x800FAAE0,
    /* StaticCameraInUseAddress            */ 0x800FAAE4,
    /* OverlayTableAddress                 */ 0x800FE500,
    /* CurrentScreenAddress                */ 0x800FE710,
    /* AnimseqCameraAddress                */ 0x80104008,

    /* IntroCinematicSkipStub              */ 0x80067410,
    /* CurrentSceneAddress                 */ 0x800A34AC,
    /* CurrentSetupAddress                 */ 0x800A34B8,
    /* NextCharacterAddress                */ 0x800A34D0,
    /* LoadingAddress                      */ 0x800A3504,
    /* MainFrontInitFunction               */ 0x80047758,
    /* FrontCharSelectSetQuitModeFunction  */ 0x8005ACF8,
    /* FrontGetModeFunction                */ 0x80058C6C,
    /* MainChangeLevelFunction             */ 0x8004675C,

    /* FloydPadControlOffset               */ 0x000002A4,
    /* IntroCinematicSkipEntryOffset       */ 0x000000D0,
    /* LegacyIntroCinematicSkipEntryOffset */ 0x000000C0,
    /* LegacyIntroCinematicSkipFmvUpdateOffset */ 0x0000037C,
    /* TargetOverlayCursorXOffset          */ 0x00000418,
    /* TargetOverlayCursorYOffset          */ 0x00000440,
    /* TargetOverlayDrawOffset             */ 0x000004A4,
    /* BoyAimHelperOffset                  */ 0x00003ECC,
    /* BoyAimFirstGroupOffset              */ 0x000042B4,
    /* BoyAimSecondGroupOffset             */ 0x00004874,

    /* CameraHelperCallWord                */ 0x0C00D00F,
    /* FramePacing60SignatureWord0         */ 0x8DCEE72C,
    /* FramePacingSignatureWord1           */ 0x2442E70D,
    /* ManualAimCursorXGuardWord           */ 0x15E10002,
    /* ManualAimCursorYGuardWord           */ 0x15610002,
    /* ManualAimCursorXStoreWord           */ 0xA7190000,
    /* ManualAimCursorYStoreWord           */ 0xA58D0000,
    /* SchedulerFrameGateAddWord           */ 0x25090001,
    /* SchedulerSignatureWord0             */ 0x8E480300,
    /* SchedulerSignatureWord2             */ 0x2D210002,
    /* FramePacingEscalateStoreWord        */ 0xA24D0000,
    /* FramePacing60StoreWord              */ 0xA24F0000,
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
        else if (Rom == JfgPalAddresses.RomIdentifier)
        {
            ResolvedAddresses = &JfgPalAddresses;
        }
        ResolvedAddressesValid = true;
    }
    return ResolvedAddresses;
}
