#include "stdafx.h"

#include <Project64\UserInterface\GameSpecificHacks.h>

#include <commctrl.h>

namespace
{
// The 60 fps patch set remains experimental, but is available for testing.
constexpr bool Jfg60FpsAvailable = true;

void add_tooltip(HWND tooltip, HWND dialog, int id, const wchar_t *text)
{
    const auto control = GetDlgItem(dialog, id);
    if (!control)
        return;

    TTTOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = dialog;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<wchar_t *>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

void initialize_tooltips(HWND dialog)
{
    const auto tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, dialog, nullptr, nullptr, nullptr);
    if (!tooltip)
        return;

    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 340);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 350);

    add_tooltip(tooltip, dialog, IDC_GSH_KEYBOARD_MOUSE,
        L"Feeds the selected controller port exclusively from the Jet Force Gemini keyboard/mouse mapping. Regular input-plugin bindings for that port are ignored. Mouse look is available on player 1 only.");
    add_tooltip(tooltip, dialog, IDC_GSH_KEYBOARD_MOUSE_PORT,
        L"N64 controller port fed by the keyboard and mouse. Sources sharing a port are merged.");
    add_tooltip(tooltip, dialog, IDC_GSH_GAMEPAD1,
        L"Feeds the selected port from the first connected gamepad: left stick moves, right stick looks, A jumps, B crouches, X/Y cycle weapons, right trigger fires, left trigger aims. Xbox, PlayStation and Switch layouts are recognised.");
    add_tooltip(tooltip, dialog, IDC_GSH_GAMEPAD1_PORT,
        L"N64 controller port fed by gamepad 1. Right stick camera control is available on player 1 only.");
    add_tooltip(tooltip, dialog, IDC_GSH_GAMEPAD2,
        L"Feeds the selected port from the second connected gamepad, with the same layout as gamepad 1.");
    add_tooltip(tooltip, dialog, IDC_GSH_GAMEPAD2_PORT,
        L"N64 controller port fed by gamepad 2. Right stick camera control is available on player 1 only.");
    add_tooltip(tooltip, dialog, IDC_GSH_GAMEPAD_STOCK_AIM,
        L"While aiming with the left trigger, keeps the game's own aiming: the right stick moves the reticle inside its box and the view turns once the reticle reaches the edge. Aiming with the right mouse button keeps the mouse behaviour.");
    add_tooltip(tooltip, dialog, IDC_GSH_STICK_CAMERA_SPEED,
        L"How fast the right stick turns the camera and the aim, from 1 (slowest) to 10 (fastest).");
    add_tooltip(tooltip, dialog, IDC_GSH_LATERAL_MOVEMENT,
        L"During Floyd missions, Q/D move sideways and Jump/Crouch move up/down, with gradual acceleration and braking. Keyboard: Space to rise, Ctrl to descend. Gamepad: A to rise, B to descend.");
    add_tooltip(tooltip, dialog, IDC_GSH_PRESERVE_CAMERA,
        L"Keeps the game's normal camera pitch and yaw limits while using mouse look.");
    add_tooltip(tooltip, dialog, IDC_GSH_FREE_CAMERA_JUMP,
        L"Allows mouse camera control while jumping. This is experimental.");
    add_tooltip(tooltip, dialog, IDC_GSH_PRONE_CBUTTONS,
        L"While prone, maps Q/D to N64 C-Left/C-Right instead of the joystick.");
    add_tooltip(tooltip, dialog, IDC_GSH_DRONE_INVERT_Y,
        L"Reverses vertical mouse input while controlling Floyd.");
    add_tooltip(tooltip, dialog, IDC_GSH_DRONE_DIRECT,
        L"Moves Floyd's aiming reticle directly with the mouse instead of rotating the camera.");

    add_tooltip(tooltip, dialog, IDC_GSH_FRAME_RATE,
        L"Enables the experimental 60 fps patch set. Use 30 fps if the game does not start or a compatibility problem appears.");
    add_tooltip(tooltip, dialog, IDC_GSH_KEEP_30FPS,
        L"Keeps original 30 fps pacing during demanding scenes. Available only in 30 fps mode.");
    add_tooltip(tooltip, dialog, IDC_GSH_SCHEDULER_RELEASE,
        L"Releases each graphics task at the next retrace in 60 fps mode to improve task pacing.");
	add_tooltip(tooltip, dialog, IDC_GSH_CPU_BUDGET,
		L"Doubles the CPU time available for each frame. Helps prevent slowdowns in busy scenes; in 30 fps mode, enable audio synchronization if sound stutters.");
	add_tooltip(tooltip, dialog, IDC_GSH_SYNC_AUDIO,
		L"Paces 30 fps emulation from the audio buffer to prevent sound stutter caused by the CPU-budget overclock. Restart the ROM after changing it.");
    add_tooltip(tooltip, dialog, IDC_GSH_HALVE_ENEMY_SPEED,
        L"Halves patched enemy movement updates in 60 fps mode to compensate for doubled game speed.");

    add_tooltip(tooltip, dialog, IDC_GSH_FAST_CUTSCENES,
        L"Skips known JFG cinematics in the US ROM. Press E or Enter, or A or Start on a gamepad, while a cinematic/logo screen plays. Requires a JFG input source on player 1.");
    add_tooltip(tooltip, dialog, IDC_GSH_ENABLE_SPRINT,
        L"Holding Left Shift, or clicking the left stick, increases standing movement speed in normal gameplay. It is disabled while aiming, crouching, prone, or in boss modes.");
    add_tooltip(tooltip, dialog, IDC_GSH_WIDESCREEN_HUD,
        L"Experimental correction for gameplay HUD proportions, including ammunition digits. Requires the game's native widescreen mode and the US retail ROM.");
    add_tooltip(tooltip, dialog, IDC_GSH_ALIGN_HUD,
        L"Aligns the weapon panel and health arc using a 13-unit left margin, with a slight visual adjustment for the arc in widescreen. Centres the health icon inside its arc. Works in 4:3 and widescreen.");
    add_tooltip(tooltip, dialog, IDC_GSH_SHOW_INPUT_RATE,
        L"Shows input and video rates plus Jet Force Gemini diagnostic values at the bottom of the emulator window.");
}
} // namespace

LRESULT CGameSpecificHacksDialog::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL & /*bHandled*/)
{
    TCITEMW TabItem = {0};
    TabItem.mask = TCIF_TEXT;
    TabItem.pszText = const_cast<wchar_t *>(L"Jet Force Gemini");
    TabCtrl_InsertItem(GetDlgItem(IDC_GSH_GAME_TAB), 0, &TabItem);

    HWND FrameRate = GetDlgItem(IDC_GSH_FRAME_RATE);
    ::SendMessageW(FrameRate, CB_ADDSTRING, 0, (LPARAM)L"30 fps");
    ::SendMessageW(FrameRate, CB_ADDSTRING, 0, (LPARAM)L"60 fps");

    HWND StickSpeed = GetDlgItem(IDC_GSH_STICK_CAMERA_SPEED);
    ::SendMessageW(StickSpeed, TBM_SETRANGE, TRUE, MAKELONG(1, 10));
    ::SendMessageW(StickSpeed, TBM_SETTICFREQ, 1, 0);

    LoadSettings();
    ::EnableWindow(FrameRate, Jfg60FpsAvailable ? TRUE : FALSE);
    UpdateControlState();
    initialize_tooltips(m_hWnd);
    return TRUE;
}

// The port lists are 0 based like the settings, shown to the user as players
void CGameSpecificHacksDialog::FillPortList(int ControlId, SettingID Setting)
{
    HWND List = GetDlgItem(ControlId);
    ::SendMessageW(List, CB_RESETCONTENT, 0, 0);
    static const wchar_t * Players[] = {L"Player 1", L"Player 2", L"Player 3", L"Player 4"};
    for (size_t i = 0; i < sizeof(Players) / sizeof(Players[0]); i++)
    {
        ::SendMessageW(List, CB_ADDSTRING, 0, (LPARAM)Players[i]);
    }
    uint32_t Port = g_Settings->LoadDword(Setting);
    if (Port >= sizeof(Players) / sizeof(Players[0]))
    {
        Port = 0;
        g_Settings->SaveDword(Setting, Port);
    }
    ::SendMessageW(List, CB_SETCURSEL, Port, 0);
}

void CGameSpecificHacksDialog::LoadSettings(void)
{
    CheckDlgButton(IDC_GSH_KEYBOARD_MOUSE, g_Settings->LoadBool(Setting_JfgKeyboardMouse) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_GAMEPAD1, g_Settings->LoadBool(Setting_JfgGamepad1) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_GAMEPAD2, g_Settings->LoadBool(Setting_JfgGamepad2) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_GAMEPAD_STOCK_AIM, g_Settings->LoadBool(Setting_JfgGamepadStockAim) ? BST_CHECKED : BST_UNCHECKED);
    FillPortList(IDC_GSH_KEYBOARD_MOUSE_PORT, Setting_JfgKeyboardMousePort);
    FillPortList(IDC_GSH_GAMEPAD1_PORT, Setting_JfgGamepad1Port);
    FillPortList(IDC_GSH_GAMEPAD2_PORT, Setting_JfgGamepad2Port);
    uint32_t StickSpeed = g_Settings->LoadDword(Setting_JfgGamepadCameraSpeed);
    StickSpeed = StickSpeed < 1 ? 1 : (StickSpeed > 10 ? 10 : StickSpeed);
    ::SendMessageW(GetDlgItem(IDC_GSH_STICK_CAMERA_SPEED), TBM_SETPOS, TRUE, StickSpeed);
    CheckDlgButton(IDC_GSH_LATERAL_MOVEMENT,
                   g_Settings->LoadBool(Setting_JfgDroneLateralMovement) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_PRESERVE_CAMERA, g_Settings->LoadBool(Setting_JfgPreserveCameraInGameLimits) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_FREE_CAMERA_JUMP, g_Settings->LoadBool(Setting_JfgFreeCameraInJump) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_PRONE_CBUTTONS, g_Settings->LoadBool(Setting_JfgCrouchProneStickStrafe) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_DRONE_INVERT_Y, g_Settings->LoadBool(Setting_JfgDroneInvertY) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_DRONE_DIRECT, g_Settings->LoadBool(Setting_JfgDroneCameraDirect) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_KEEP_30FPS, g_Settings->LoadBool(Setting_JfgUncapFramePacing) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_SCHEDULER_RELEASE, g_Settings->LoadBool(Setting_JfgSchedulerRelease) ? BST_CHECKED : BST_UNCHECKED);
    const bool Target60Fps = Jfg60FpsAvailable && g_Settings->LoadBool(Setting_JfgTarget60Fps);
    if (!Target60Fps)
    {
        g_Settings->SaveBool(Setting_JfgTarget60Fps, false);
    }
    CheckDlgButton(IDC_GSH_CPU_BUDGET,
                   g_Settings->LoadBool(Target60Fps ? Setting_JfgBoostViBudget : Setting_JfgBoostViBudget30)
                       ? BST_CHECKED
                       : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_SYNC_AUDIO, g_Settings->LoadBool(Setting_JfgSyncAudio) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_HALVE_ENEMY_SPEED, g_Settings->LoadBool(Setting_JfgHalveEnemySpeed) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_FAST_CUTSCENES, g_Settings->LoadBool(Setting_JfgFastCutscenes) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_ENABLE_SPRINT, g_Settings->LoadBool(Setting_JfgEnableSprint) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_WIDESCREEN_HUD, g_Settings->LoadBool(Setting_JfgWidescreenHud) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_ALIGN_HUD, g_Settings->LoadBool(Setting_JfgAlignHud) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_GSH_SHOW_INPUT_RATE, g_Settings->LoadBool(Setting_JfgShowInputRate) ? BST_CHECKED : BST_UNCHECKED);

    ::SendMessage(GetDlgItem(IDC_GSH_FRAME_RATE), CB_SETCURSEL,
                  Target60Fps ? 1 : 0, 0);
}

void CGameSpecificHacksDialog::SaveCheckBox(int ControlId, SettingID Setting)
{
    g_Settings->SaveBool(Setting, IsDlgButtonChecked(ControlId) == BST_CHECKED);
}

void CGameSpecificHacksDialog::UpdateControlState(void)
{
    const bool KeyboardMouse = IsDlgButtonChecked(IDC_GSH_KEYBOARD_MOUSE) == BST_CHECKED;
    const bool Gamepad1 = IsDlgButtonChecked(IDC_GSH_GAMEPAD1) == BST_CHECKED;
    const bool Gamepad2 = IsDlgButtonChecked(IDC_GSH_GAMEPAD2) == BST_CHECKED;
    const bool AnySource = KeyboardMouse || Gamepad1 || Gamepad2;
    const bool AnyGamepad = Gamepad1 || Gamepad2;
    ::EnableWindow(GetDlgItem(IDC_GSH_KEYBOARD_MOUSE_PORT), KeyboardMouse ? TRUE : FALSE);
    ::EnableWindow(GetDlgItem(IDC_GSH_GAMEPAD1_PORT), Gamepad1 ? TRUE : FALSE);
    ::EnableWindow(GetDlgItem(IDC_GSH_GAMEPAD2_PORT), Gamepad2 ? TRUE : FALSE);
    ::EnableWindow(GetDlgItem(IDC_GSH_STICK_CAMERA_SPEED), AnyGamepad ? TRUE : FALSE);
    ::EnableWindow(GetDlgItem(IDC_GSH_STICK_CAMERA_LABEL), AnyGamepad ? TRUE : FALSE);
    ::EnableWindow(GetDlgItem(IDC_GSH_GAMEPAD_STOCK_AIM), AnyGamepad ? TRUE : FALSE);

    // IDC_GSH_DRONE_DIRECT stays disabled in the dialog resource.
    const int SchemeOptions[] = {
        IDC_GSH_LATERAL_MOVEMENT,
        IDC_GSH_PRESERVE_CAMERA,
        IDC_GSH_FREE_CAMERA_JUMP,
        IDC_GSH_PRONE_CBUTTONS,
        IDC_GSH_DRONE_INVERT_Y,
        IDC_GSH_FAST_CUTSCENES,
        IDC_GSH_ENABLE_SPRINT,
    };
    for (size_t i = 0; i < sizeof(SchemeOptions) / sizeof(SchemeOptions[0]); i++)
    {
        ::EnableWindow(GetDlgItem(SchemeOptions[i]), AnySource ? TRUE : FALSE);
    }

    bool Target60Fps = ::SendMessage(GetDlgItem(IDC_GSH_FRAME_RATE), CB_GETCURSEL, 0, 0) == 1;
    ::ShowWindow(GetDlgItem(IDC_GSH_KEEP_30FPS), Target60Fps ? SW_HIDE : SW_SHOW);
    ::ShowWindow(GetDlgItem(IDC_GSH_SYNC_AUDIO), Target60Fps ? SW_HIDE : SW_SHOW);

    const int Target60FpsOptions[] = {
        IDC_GSH_SCHEDULER_RELEASE,
        IDC_GSH_HALVE_ENEMY_SPEED,
    };
    for (size_t i = 0; i < sizeof(Target60FpsOptions) / sizeof(Target60FpsOptions[0]); i++)
    {
        ::ShowWindow(GetDlgItem(Target60FpsOptions[i]), Target60Fps ? SW_SHOW : SW_HIDE);
    }
}

LRESULT CGameSpecificHacksDialog::OnCheckBoxClicked(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    switch (wID)
    {
    case IDC_GSH_KEYBOARD_MOUSE:
        SaveCheckBox(wID, Setting_JfgKeyboardMouse);
        UpdateControlState();
        break;
    case IDC_GSH_GAMEPAD1:
        SaveCheckBox(wID, Setting_JfgGamepad1);
        UpdateControlState();
        break;
    case IDC_GSH_GAMEPAD2:
        SaveCheckBox(wID, Setting_JfgGamepad2);
        UpdateControlState();
        break;
    case IDC_GSH_GAMEPAD_STOCK_AIM: SaveCheckBox(wID, Setting_JfgGamepadStockAim); break;
    case IDC_GSH_LATERAL_MOVEMENT: SaveCheckBox(wID, Setting_JfgDroneLateralMovement); break;
    case IDC_GSH_PRESERVE_CAMERA: SaveCheckBox(wID, Setting_JfgPreserveCameraInGameLimits); break;
    case IDC_GSH_FREE_CAMERA_JUMP: SaveCheckBox(wID, Setting_JfgFreeCameraInJump); break;
    case IDC_GSH_PRONE_CBUTTONS: SaveCheckBox(wID, Setting_JfgCrouchProneStickStrafe); break;
    case IDC_GSH_DRONE_INVERT_Y: SaveCheckBox(wID, Setting_JfgDroneInvertY); break;
    case IDC_GSH_DRONE_DIRECT: SaveCheckBox(wID, Setting_JfgDroneCameraDirect); break;
    case IDC_GSH_KEEP_30FPS: SaveCheckBox(wID, Setting_JfgUncapFramePacing); break;
    case IDC_GSH_SCHEDULER_RELEASE: SaveCheckBox(wID, Setting_JfgSchedulerRelease); break;
    case IDC_GSH_CPU_BUDGET:
        SaveCheckBox(wID, ::SendMessage(GetDlgItem(IDC_GSH_FRAME_RATE), CB_GETCURSEL, 0, 0) == 1
                               ? Setting_JfgBoostViBudget
                               : Setting_JfgBoostViBudget30);
        break;
    case IDC_GSH_SYNC_AUDIO: SaveCheckBox(wID, Setting_JfgSyncAudio); break;
    case IDC_GSH_HALVE_ENEMY_SPEED: SaveCheckBox(wID, Setting_JfgHalveEnemySpeed); break;
    case IDC_GSH_FAST_CUTSCENES: SaveCheckBox(wID, Setting_JfgFastCutscenes); break;
    case IDC_GSH_ENABLE_SPRINT: SaveCheckBox(wID, Setting_JfgEnableSprint); break;
    case IDC_GSH_WIDESCREEN_HUD: SaveCheckBox(wID, Setting_JfgWidescreenHud); break;
    case IDC_GSH_ALIGN_HUD: SaveCheckBox(wID, Setting_JfgAlignHud); break;
    case IDC_GSH_SHOW_INPUT_RATE:
        SaveCheckBox(wID, Setting_JfgShowInputRate);
        if (IsDlgButtonChecked(wID) != BST_CHECKED)
        {
            g_Notify->DisplayMessage(0, EMPTY_STRING);
        }
        break;
    }
    return 0;
}

LRESULT CGameSpecificHacksDialog::OnFrameRateChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    bool Target60Fps = Jfg60FpsAvailable &&
        ::SendMessage(GetDlgItem(IDC_GSH_FRAME_RATE), CB_GETCURSEL, 0, 0) == 1;
    if (!Target60Fps)
    {
        ::SendMessage(GetDlgItem(IDC_GSH_FRAME_RATE), CB_SETCURSEL, 0, 0);
    }
    g_Settings->SaveBool(Setting_JfgTarget60Fps, Target60Fps);
    CheckDlgButton(IDC_GSH_CPU_BUDGET,
                   g_Settings->LoadBool(Target60Fps ? Setting_JfgBoostViBudget : Setting_JfgBoostViBudget30)
                       ? BST_CHECKED
                       : BST_UNCHECKED);
    UpdateControlState();
    return 0;
}

LRESULT CGameSpecificHacksDialog::OnPortChanged(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    SettingID Setting;
    switch (wID)
    {
    case IDC_GSH_KEYBOARD_MOUSE_PORT: Setting = Setting_JfgKeyboardMousePort; break;
    case IDC_GSH_GAMEPAD1_PORT: Setting = Setting_JfgGamepad1Port; break;
    case IDC_GSH_GAMEPAD2_PORT: Setting = Setting_JfgGamepad2Port; break;
    default: return 0;
    }
    const LRESULT Port = ::SendMessage(GetDlgItem(wID), CB_GETCURSEL, 0, 0);
    if (Port >= 0 && Port < 4)
    {
        g_Settings->SaveDword(Setting, (uint32_t)Port);
    }
    return 0;
}

LRESULT CGameSpecificHacksDialog::OnStickCameraSpeedChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL & bHandled)
{
    if ((HWND)lParam != GetDlgItem(IDC_GSH_STICK_CAMERA_SPEED))
    {
        bHandled = FALSE;
        return 0;
    }
    const LRESULT Speed = ::SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
    if (Speed >= 1 && Speed <= 10)
    {
        g_Settings->SaveDword(Setting_JfgGamepadCameraSpeed, (uint32_t)Speed);
    }
    return 0;
}

LRESULT CGameSpecificHacksDialog::OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    EndDialog(IDOK);
    return 0;
}
