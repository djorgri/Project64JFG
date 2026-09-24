#pragma once

#include "GameHackMemory.h"
#include <Common/HighResTimeStamp.h>
#include <Project64-plugin-spec/Input.h>
#include <vector>

class CMipsMemoryVM;
class CRecompiler;

// The sources feeding one N64 controller port. A null entry is a source that
// is switched off, absent, or routed to another port; CControl_Plugin fills
// this from the port settings before every controller poll.
struct JFG_PORT_INPUT
{
    const KEYBOARD_MOUSE_STATE * KeyboardMouse;
    const GAMEPAD_STATE * Gamepads[2];

    bool HasSource(void) const
    {
        return KeyboardMouse != nullptr || Gamepads[0] != nullptr || Gamepads[1] != nullptr;
    }
};

class CJetForceGeminiRuntime
{
public:
    CJetForceGeminiRuntime(CMipsMemoryVM & MMU, CRecompiler *& Recompiler);
    ~CJetForceGeminiRuntime();

    void Reset(void);
    void StateSaving(void);
    void StateLoaded(void);
    void ProcessRuntimeFrame(void);
    void ProcessController(int32_t Control, const JFG_PORT_INPUT & Input, BUTTONS & Buttons);
    void ProcessVideoFrame(const JFG_PORT_INPUT & Input, BUTTONS & Buttons);
    void ProcessSecondaryVideoFrame(int32_t Control, const JFG_PORT_INPUT & Input);
    void QueueSecondaryScroll(int32_t Control, const JFG_PORT_INPUT & Input);
    bool IsEnabled(void) const;
    bool UsesExclusiveInput(const JFG_PORT_INPUT & Input) const;
    bool UsesKeyboardMouse(void) const;
    bool SupportsCurrentRom(void) const;

private:
    // What the scheme reads once keyboard, mouse and gamepads routed to the
    // same port have been merged; see ReadControls. Forward/Backward/Left/
    // Right are the held-key view of movement, StickX/StickY the analogue one
    // in N64 units, so a stick and the keys can share a port.
    struct JFG_CONTROLS
    {
        bool Forward;
        bool Backward;
        bool Left;
        bool Right;
        int8_t StickX;
        int8_t StickY;
        // The C buttons as the game's control setup reads them: up jumps,
        // down crouches, left and right sidestep. A and B are the N64 buttons,
        // which that setup spends on weapon cycling; see MapController.
        bool CUp;
        bool CDown;
        bool CLeft;
        bool CRight;
        bool Sprint;
        bool A;
        bool B;
        bool Fire;
        // Aim is either source; the two below say which, since the trigger
        // can hand the aim to the game's own code while the mouse button
        // keeps the mouse scheme, see MapController
        bool Aim;
        bool AimMouse;
        bool AimPad;
        bool Start;
        bool SkipCinematic;
        bool DpadUp;
        bool DpadDown;
        bool DpadLeft;
        bool DpadRight;
        // +1 next weapon (a B impulse), -1 previous weapon (an A impulse)
        int32_t Scroll;
        // Right stick, -1..1 with SDL's signs (up and left negative), the
        // pad pushed furthest winning each axis
        float CameraX;
        float CameraY;
    };

    // Rising-edge bookkeeping for the X/Y weapon buttons, one slot per gamepad
    struct SCROLL_BUTTON_STATE
    {
        bool PreviousDown[2];
        bool NextDown[2];
    };

    // The free orbit camera, one per player. Player one's is fed by the mouse
    // and the right stick of port one, the others by the right stick of the
    // port routed to them; the camera code itself is shared, see
    // ApplyOrbitCamera and SetCameraCode.
    struct ORBIT_CAMERA_STATE
    {
        bool OverrideActive;
        bool OverrideSuspended;
        uint32_t TrackedCamera;
        uint32_t TrackedPlayerObject;
        bool OrbitYawInitialized;
        bool ElevationReady;
        float HeightOffset;
        int16_t OrbitYaw;
        // Mouse counts banked between the two sampling paths, see BankMouseDelta
        int32_t MouseDeltaX;
        int32_t MouseDeltaY;
        // Right stick camera: the sub-count fraction left over each video
        // frame, see BankStickCamera
        float StickCarryX;
        float StickCarryY;
        // Whether this player asked for the free orbit code on its last video
        // frame; the shared patches stay in while any player does
        bool FreeOrbitWanted;
    };

    // What the state checks concluded for one player this video frame, the
    // first half of what ApplyMouseCamera used to do; see EvaluateOrbitCamera
    struct ORBIT_CAMERA_EVAL
    {
        uint32_t PlayerObject;
        uint32_t PlayerData;
        uint32_t Camera;
        uint32_t JoyDisabled;
        uint8_t CameraMode;
        bool BasicStateAvailable;
        bool ConstrainedStateAvailable;
        bool NormalCamera;
        bool MouseCameraAllowed;
        bool JumpCameraMode;
        bool FreeJumpCameraAllowed;
        bool FreeCameraBlockedByJump;
        bool PreserveConstrainedCameras;
        bool FreeCameraStateAllowed;
        bool EnableFreeOrbit;
    };

    static bool IsSupportedRom(void);
    void PatchHudRaster(bool Enabled, bool TextOnly = false);
    static bool KeyDown(const KEYBOARD_MOUSE_STATE & Input, KeyboardMouseKey Key);
    static bool MouseButtonDown(const KEYBOARD_MOUSE_STATE & Input, uint32_t Button);
    static void ReadControls(const JFG_PORT_INPUT & Input, JFG_CONTROLS & Controls);
    static int32_t ReadScrollButtons(const JFG_PORT_INPUT & Input, SCROLL_BUTTON_STATE & State);
    void MapController(const JFG_CONTROLS & Controls, BUTTONS & Buttons, bool ApplyCamera);
    void MapSecondaryPort(int32_t Control, const JFG_PORT_INPUT & Input, BUTTONS & Buttons);
    bool UpdateEnabledState(const JFG_PORT_INPUT & Input);
    void BankMouseDelta(const KEYBOARD_MOUSE_STATE & Input);
    void BankStickCamera(ORBIT_CAMERA_STATE & Orbit, const JFG_PORT_INPUT & Input);
    void QueueMouseWheel(const KEYBOARD_MOUSE_STATE & Input);
    void QueueGamepadScroll(const JFG_PORT_INPUT & Input);
    void Deactivate(void);
    void ClearCameraState(void);
    void ClearMovementState(void);
    bool SetCameraCode(bool EnableFreeOrbit, bool EnableManualAim, bool InstallRuntime, bool StockAim);
    void PatchManualAimCode(bool Enabled);
    bool GetPlayerData(uint32_t & PlayerObject, uint32_t & PlayerData) const;
    bool GetPlayerDataByIndex(uint8_t PlayerIndex, uint32_t & PlayerObject, uint32_t & PlayerData) const;
    bool GetPlayerCamera(uint8_t PlayerIndex, uint32_t & Camera) const;
    bool GetCameraBaseYaw(uint32_t PlayerObject, uint32_t PlayerData, int16_t & BaseYaw) const;
    bool GetControlCamera(uint32_t & Camera) const;
    bool TopDownCameraWasUpdated(uint32_t Counter);
    bool GetNormalCameraState(
        uint32_t PlayerData, const ORBIT_CAMERA_STATE & Orbit, bool UpdateTopDown,
        bool & NormalCamera, bool & MouseCameraAllowed);
    float ClampCameraElevation(
        uint8_t PlayerIndex, uint32_t PlayerObject, uint32_t Camera, float HeightOffset) const;
    bool ApplyMouseCamera(int32_t MouseX, int32_t MouseY, bool AimMode, bool StockAim);
    void EvaluateOrbitCamera(
        uint8_t PlayerIndex, const ORBIT_CAMERA_STATE & Orbit, bool AimMode, ORBIT_CAMERA_EVAL & Eval);
    bool ApplyOrbitCamera(
        uint8_t PlayerIndex, ORBIT_CAMERA_STATE & Orbit, const ORBIT_CAMERA_EVAL & Eval,
        int32_t MouseX, int32_t MouseY, bool AimMode);
    void ResetOrbitCamera(ORBIT_CAMERA_STATE & Orbit);
    void WriteCameraHeightOffset(uint8_t PlayerIndex, float HeightOffset);
    bool AnyFreeOrbitWanted(void) const;
    void ApplyManualAimMouse(int32_t MouseX, int32_t MouseY);
    void ApplyBossAimCameraTurn(uint32_t PlayerObject, int32_t Reticle);
    void ApplyDroneCamera(int32_t MouseX, int32_t MouseY);
    void AlignPlayerYawToOrbitCamera(
        const ORBIT_CAMERA_STATE & Orbit, uint32_t PlayerObject, uint32_t PlayerData);
    void UpdateSprintBlend(void);
    void ApplySprint(uint32_t PlayerObject);
    void PatchFramePacing(bool Enabled);
    void PatchFramePacing60(bool Enabled);
    void PatchSchedulerRelease(bool Enabled);
    void PatchWaterWakeRingRate(bool Enabled);
    void ApplyViBudget(bool Boost);
    bool PatchLandingCinematicSkip(bool Enabled);
    bool PatchIntroCinematicSkip(bool Enabled);
    bool PatchWidescreenHud(bool Enabled);
    bool PatchHudAlignment(bool Enabled, bool WidescreenCorrected);
    bool SetWidescreenHudBanner(uint32_t OverlayBase, bool Enabled);
    bool SetWidescreenHudShotGauge(uint32_t OverlayBase, bool Enabled);
    bool SetWidescreenHudFloyd(uint32_t OverlayBase, bool Enabled);
    bool SetWidescreenHudReticle(bool Enabled);
    bool RemoveWidescreenHudOverlayHooks(void);
    bool CurrentSceneIsCinematicSkippable(void);
    bool ReadObjectName(uint32_t Object, char * Buffer, size_t Size);
    void HalveNamedEnemyMovement(void);
    void RemoveLegacyLandingCinematicSkip(void);
    void RemoveLegacyIntroCinematicSkip(void);
    bool SetObjectMoveHook(bool Enabled);
    void PatchObjectMove(bool HalveEnemies);
    bool PatchSidekickStrafe(bool Enabled);
    bool PatchSidekickPadControlProbe(bool Enabled);
    void UpdateInputRate(void);

    CGameHackMemory m_Memory;
    CGameHackCodePatcher m_CodePatcher;
    bool m_Enabled;
    bool m_CameraPatchApplied;
    // Indexed by player, which is the port for the secondary ports; see
    // ProcessSecondaryVideoFrame
    ORBIT_CAMERA_STATE m_Orbit[4];

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

    int32_t m_QueuedMouseWheel;

    // The scroll states track the X/Y weapon buttons for port one and for the
    // three secondary ports respectively; the secondary queue is those ports'
    // counterpart of m_QueuedMouseWheel, see QueueSecondaryScroll.
    SCROLL_BUTTON_STATE m_ScrollButtons;
    SCROLL_BUTTON_STATE m_SecondaryScrollButtons[3];
    int32_t m_SecondaryQueuedScroll[3];

    bool m_FramePacingPatchApplied;
    bool m_FramePacing60PatchApplied;
    bool m_SchedulerReleasePatchApplied;
    bool m_WaterWakeRingRatePatchApplied;
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
    bool m_DroneLateralApplied;
    uint32_t m_DroneLateralState;
    uint32_t m_DroneLateralHookHits;
    bool m_SidekickStrafeHookApplied;
    std::vector<uint32_t> m_SidekickStrafeHookStubOriginal;
    bool m_SidekickPadControlProbeApplied;
    uint32_t m_SidekickPadControlProbeEntry;
    std::vector<uint32_t> m_SidekickPadControlProbeStubOriginal;
    uint32_t m_BaseViRefreshRate;
    bool m_ObjectMovePatchApplied;
    std::vector<uint32_t> m_ObjectMoveStubOriginal;


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
    uint32_t m_WidescreenReticleOverlayBase;
    uint32_t m_WidescreenHudScopeOriginal;
    std::vector<uint32_t> m_WidescreenHudCaveOriginal;
    std::vector<uint32_t> m_HudAlignmentCaveOriginal;
    std::vector<uint32_t> m_HudAlignmentImage;
    uint32_t m_HudAlignmentOverlay6Base;
    uint32_t m_HudAlignmentOverlay14Base;
    bool m_HudAlignmentScopeOwned;

    // Live FPS switch edge state, see Fps60ToggleKey / Fps30ToggleKey
    bool m_Fps60ToggleDown;
    bool m_Fps30ToggleDown;
    // Last sync-to-audio state pushed to the core (-1 = not yet set). Keeps
    // audio from pacing the frame in 60 fps mode; see ProcessVideoFrame.
    int8_t m_SyncAudioEnabledState;

    // Per-object tracking for HalveNamedEnemyMovement: the halved world position
    // written last frame, so this frame's game-applied move can be halved by
    // averaging back toward it. Species whose per-baddy mover the objMoveXYZ hook
    // never sees (e.g. OctoGalaxian) are slowed here instead.
    struct HALVED_ENEMY_SLOT
    {
        uint32_t Object;
        float X, Y, Z;
        uint32_t LastSeenFrame;
    };
    HALVED_ENEMY_SLOT m_HalvedEnemySlots[16];
    uint32_t m_HalveFrameCounter;


    // Input rate diagnostic, see UpdateInputRate
    bool m_InputRateWindowValid;
    uint32_t m_InputRateSamples;
    uint32_t m_FrameSwaps;
    uint32_t m_LastCurrentScreen;
    HighResTimeStamp m_InputRateWindowStart;
};
