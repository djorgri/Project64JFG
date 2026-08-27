#pragma once

#include "GameHackMemory.h"
#include <Common/HighResTimeStamp.h>
#include <Project64-plugin-spec/Input.h>
#include <vector>

class CMipsMemoryVM;
class CRecompiler;

class CJetForceGeminiRuntime
{
public:
    CJetForceGeminiRuntime(CMipsMemoryVM & MMU, CRecompiler *& Recompiler);
    ~CJetForceGeminiRuntime();

    void Reset(void);
    void StateSaving(void);
    void StateLoaded(void);
    void ProcessRuntimeFrame(void);
    void ProcessController(int32_t Control, const KEYBOARD_MOUSE_STATE & Input, BUTTONS & Buttons);
    void ProcessVideoFrame(const KEYBOARD_MOUSE_STATE & Input, BUTTONS & Buttons);
    bool IsEnabled(void) const;
    bool UsesExclusiveInput(void) const;
    bool SupportsCurrentRom(void) const;

private:
    static bool IsSupportedRom(void);
    static bool KeyDown(const KEYBOARD_MOUSE_STATE & Input, KeyboardMouseKey Key);
    static bool MouseButtonDown(const KEYBOARD_MOUSE_STATE & Input, uint32_t Button);
    void MapController(const KEYBOARD_MOUSE_STATE & Input, BUTTONS & Buttons, bool ApplyCamera);
    bool UpdateEnabledState(void);
    void BankMouseDelta(const KEYBOARD_MOUSE_STATE & Input);
    void QueueMouseWheel(const KEYBOARD_MOUSE_STATE & Input);
    void Deactivate(void);
    void ClearCameraState(void);
    bool SetCameraCode(bool EnableFreeOrbit, bool EnableManualAim, bool InstallRuntime);
    void PatchManualAimCode(bool Enabled);
    bool GetPlayerData(uint32_t & PlayerObject, uint32_t & PlayerData) const;
    bool GetCameraBaseYaw(uint32_t PlayerObject, uint32_t PlayerData, int16_t & BaseYaw) const;
    bool GetControlCamera(uint32_t & Camera) const;
    bool TopDownCameraWasUpdated(uint32_t Counter);
    bool GetNormalCameraState(
        uint32_t PlayerData, bool & NormalCamera, bool & MouseCameraAllowed);
    float ClampCameraElevation(uint32_t PlayerObject, uint32_t Camera, float HeightOffset) const;
    bool ApplyMouseCamera(int32_t MouseX, int32_t MouseY, bool AimMode);
    void ApplyManualAimMouse(int32_t MouseX, int32_t MouseY);
    void ApplyBossAimCameraTurn(uint32_t PlayerObject, int32_t Reticle);
    void ApplyDroneCamera(int32_t MouseX, int32_t MouseY);
    void AlignPlayerYawToOrbitCamera(uint32_t PlayerObject, uint32_t PlayerData);
    void ApplyCameraRelativeStrafe(void);
    void UpdateSprintBlend(void);
    void ApplySprint(uint32_t PlayerObject);
    void ApplyDroneLateralMovement(void);
    void PatchFramePacing(bool Enabled);
    void PatchFramePacing60(bool Enabled);
    void PatchSchedulerRelease(bool Enabled);
    void PatchTripleBuffer(bool Enabled);
    void PatchWaterWakeRate(bool Enabled);
    void PatchWaterWakeRingRate(bool Enabled);
    void PatchWaterWakeDrawProbe(bool Enabled);
    void PatchWaterWakeCulling(bool Enabled);
    void PatchWaterWakeDrawFallback(bool Enabled);
    void PatchWaterWakeFrameRate(bool Enabled);
    void PatchWaterWakeStockDrawProbe(bool Enabled);
    void UpdateWaterWakeDrawTarget(void);
    void ApplyViBudget(bool Boost);
    bool PatchLandingCinematicSkip(bool Enabled);
    bool PatchIntroCinematicSkip(bool Enabled);
    bool PatchWidescreenHud(bool Enabled);
    bool RemoveWidescreenHudOverlayHooks(void);
    void DisplayCinematicProbe(void);
    bool CurrentSceneIsCinematicSkippable(void);
    bool ReadObjectName(uint32_t Object, char * Buffer, size_t Size);
    void HalveNamedEnemyMovement(void);
    void RemoveLegacyLandingCinematicSkip(void);
    void RemoveLegacyIntroCinematicSkip(void);
    bool SetHookEnabled(const GAME_HACK_CODE_PATCH * Patches, size_t Count, bool Enabled);
    bool SetObjectMoveHook(bool Enabled);
    bool SetPlayerVelocityHook(bool Enabled);
    bool GetSquaddieOverlayBase(uint32_t & OverlayBase) const;
    void PatchObjectMove(bool HalveEnemies);
    void PatchPlayerVelocity(bool Enabled);
    void PatchSquaddieMove(bool Enabled);
    void PatchSquadsTimeStep(bool Enabled);
    bool PatchDroneLateralMove(bool Enabled);
    bool PatchFloydCameraLateralMove(bool Enabled);
    bool PatchSidekickVelocityLateralMove(bool Enabled);
    bool PatchSidekickLateralMove(bool Enabled);
    bool PatchSidekickStrafe(bool Enabled);
    bool PatchSidekickPadControlProbe(bool Enabled);
    void UpdateDroneLateralControllerProbe(void);
    void UpdateInputRate(void);

    CGameHackMemory m_Memory;
    CGameHackCodePatcher m_CodePatcher;
    bool m_Enabled;
    bool m_CameraPatchApplied;
    bool m_CameraOverrideActive;
    bool m_CameraOverrideSuspended;
    uint32_t m_TrackedCamera;
    uint32_t m_TrackedPlayerObject;
    bool m_OrbitYawInitialized;
    bool m_CameraElevationReady;
    float m_CameraHeightOffset;
    int16_t m_OrbitYaw;

    // Sniper zoom aware aim, see ApplyManualAimMouse. The reference is the widest
    // field of view seen while aiming; the carries hold the sub unit fraction of
    // the scaled step between frames.
    float m_AimFovReference;
    float m_AimYawCarry;
    float m_AimPitchCarry;

    // Boss reticle horizontal offset, see ApplyManualAimMouse. It travels past
    // the edge of the on screen box on purpose: the surplus is what turns the
    // view, and m_BossAimYawApplied tracks how much of that turn has been spent
    // so it can be held inside the range the boss camera will follow.
    int32_t m_BossAimReticleX;
    int32_t m_BossAimYawApplied;

    bool m_TopDownCounterInitialized;
    uint32_t m_TopDownCounter;
    int32_t m_TopDownHoldPolls;

    // Mouse movement banked between the two sampling paths, see BankMouseDelta
    int32_t m_MouseDeltaX;
    int32_t m_MouseDeltaY;
    int32_t m_QueuedMouseWheel;

    bool m_FramePacingPatchApplied;
    bool m_FramePacing60PatchApplied;
    bool m_SchedulerReleasePatchApplied;
    bool m_TripleBufferPatchApplied;
    bool m_WaterWakeRatePatchApplied;
    bool m_WaterWakeRingRatePatchApplied;
    bool m_WaterWakeDrawProbeApplied;
    bool m_WaterWakeCullingPatchApplied;
    bool m_WaterWakeDrawFallbackPatchApplied;
    bool m_WaterWakeFrameRatePatchApplied;
    bool m_WaterWakeStockDrawProbeApplied;
    uint32_t m_WaterWakeRatePatchStatus;
    bool m_GameplayReady;
    bool m_SprintActive;
    bool m_SprintTimeValid;
    float m_SprintBlend;
    HighResTimeStamp m_SprintLastUpdate;
    bool m_SprintApplied;
    bool m_SprintPositionValid;
    uint32_t m_SprintPlayerObject;
    float m_SprintPreviousX;
    float m_SprintPreviousZ;
    bool m_SprintAnimationApplied;
    bool m_SprintAnimationValid;
    uint32_t m_SprintAnimation;
    float m_SprintPreviousAnimationFrame;
    bool m_DroneLateralActive;
    bool m_DroneLateralRight;
    bool m_DroneLateralApplied;
    uint32_t m_DroneLateralState;
    uint32_t m_DroneLateralDebugStatus;
    struct DroneLateralCandidate
    {
        uint32_t Object;
        float PreviousX;
        float PreviousZ;
    };
    bool m_DroneLateralPositionValid;
    uint32_t m_DroneLateralObject;
    float m_DroneLateralPreviousX;
    float m_DroneLateralPreviousZ;
    std::vector<DroneLateralCandidate> m_DroneLateralCandidates;
    uint32_t m_DroneLateralHookHits;
    uint32_t m_DroneLateralControllerEntry;
    uint32_t m_DroneLateralControllerWord0;
    uint32_t m_DroneLateralControllerWord1;
    uint32_t m_DroneLateralControllerWord2;
    uint32_t m_DroneLateralControllerWord3;
    uint32_t m_DroneLateralControllerReturn;
    uint32_t m_DroneLateralControllerReturnWord0;
    uint32_t m_DroneLateralControllerReturnWord1;
    uint32_t m_DroneLateralControllerReturnWord2;
    uint32_t m_DroneLateralControllerReturnWord3;
    uint32_t m_DroneLateralControllerReturnWord4;
    bool m_DroneLateralMoveHookApplied;
    uint32_t m_DroneLateralMoveHookEntry;
    std::vector<uint32_t> m_DroneLateralMoveHookStubOriginal;
    bool m_FloydCameraLateralHookApplied;
    uint32_t m_FloydCameraLateralHookEntry;
    std::vector<uint32_t> m_FloydCameraLateralHookStubOriginal;
    bool m_SidekickVelocityLateralHookApplied;
    std::vector<uint32_t> m_SidekickVelocityLateralHookStubOriginal;
    bool m_SidekickLateralMoveHookApplied;
    std::vector<uint32_t> m_SidekickLateralMoveHookStubOriginal;
    bool m_SidekickStrafeHookApplied;
    std::vector<uint32_t> m_SidekickStrafeHookStubOriginal;
    std::vector<uint32_t> m_SidekickControlProbeStubOriginal;
    bool m_SidekickPadControlProbeApplied;
    uint32_t m_SidekickPadControlProbeEntry;
    std::vector<uint32_t> m_SidekickPadControlProbeStubOriginal;
    uint32_t m_BaseViRefreshRate;
    bool m_ObjectMovePatchApplied;
    std::vector<uint32_t> m_ObjectMoveStubOriginal;
    bool m_PlayerVelocityPatchApplied;
    std::vector<uint32_t> m_PlayerVelocityStubOriginal;
    bool m_SquaddieMovePatchApplied;
    uint32_t m_SquaddieOverlayBase;
    bool m_SquadsTimeStepPatchApplied;


    bool m_LandingCinematicSkipHookApplied;
    std::vector<uint32_t> m_LandingCinematicSkipStubOriginal;
    bool m_IntroCinematicSkipHookApplied;
    uint32_t m_IntroCinematicSkipOverlayBase;
    std::vector<uint32_t> m_IntroCinematicSkipStubOriginal;
    bool m_WidescreenHudCaveApplied;
    bool m_WidescreenHudFixedHooksApplied;
    bool m_WidescreenHudOverlayHookApplied;
    bool m_WidescreenHudScopeOwned;
    uint32_t m_WidescreenHudOverlayBase;
    uint32_t m_WidescreenHudScopeOriginal;
    std::vector<uint32_t> m_WidescreenHudCaveOriginal;
    bool m_CinematicProbeDown;

    // Live FPS switch edge state, see Fps60ToggleKey / Fps30ToggleKey
    bool m_Fps60ToggleDown;
    bool m_Fps30ToggleDown;
    // Last sync-to-audio state pushed to the core (-1 = not yet set). Keeps
    // audio from pacing the frame in 60 fps mode; see ProcessVideoFrame.
    int8_t m_SyncAudioEnabledState;

    // Per-object tracking for HalveNamedEnemyMovement: the halved world position
    // written last frame, so this frame's game-applied move can be halved by
    // averaging back toward it. Species whose per-baddy mover the SquaddieControl
    // step hook never reaches (e.g. OctoGalaxian) are slowed here instead.
    struct HALVED_ENEMY_SLOT
    {
        uint32_t Object;
        float X, Y, Z;
        uint32_t LastSeenFrame;
    };
    HALVED_ENEMY_SLOT m_HalvedEnemySlots[16];
    uint32_t m_HalveFrameCounter;

    // Diagnostic scope override, see WidescreenHudScopeForceKey
    bool m_WidescreenHudScopeForceDown;
    bool m_WidescreenHudScopeForced;

    // Input rate diagnostic, see UpdateInputRate
    bool m_InputRateWindowValid;
    uint32_t m_InputRateSamples;
    uint32_t m_FrameSwaps;
    uint32_t m_LastCurrentScreen;
    HighResTimeStamp m_InputRateWindowStart;
};
