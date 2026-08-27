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
        L"Routes controller port 1 exclusively through the Jet Force Gemini keyboard/mouse mapping. Regular input-plugin bindings are ignored.");
    add_tooltip(tooltip, dialog, IDC_GSH_LATERAL_MOVEMENT,
        L"During Floyd missions, Q/D use the drone's native acceleration while redirecting its travelled distance to the left or right.");
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
        L"Skips known JFG cinematics in the US ROM. Press E (A) or Enter (Start) while a cinematic/logo screen plays. Press P to append a scene/setup probe line to JfgCinematicProbe.log. Requires the keyboard/mouse mapping.");
    add_tooltip(tooltip, dialog, IDC_GSH_ENABLE_SPRINT,
        L"Holding Left Shift increases standing movement speed in normal gameplay. It is disabled while aiming, crouching, prone, or in boss modes.");
    add_tooltip(tooltip, dialog, IDC_GSH_WIDESCREEN_HUD,
        L"Corrects the horizontal proportions of the gameplay HUD when Jet Force Gemini's native widescreen mode is selected. US ROM only.");
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

    LoadSettings();
    ::EnableWindow(FrameRate, Jfg60FpsAvailable ? TRUE : FALSE);
    UpdateControlState();
    initialize_tooltips(m_hWnd);
    return TRUE;
}

void CGameSpecificHacksDialog::LoadSettings(void)
{
    CheckDlgButton(IDC_GSH_KEYBOARD_MOUSE, g_Settings->LoadBool(Setting_JfgKeyboardMouse) ? BST_CHECKED : BST_UNCHECKED);
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
    bool KeyboardMouse = IsDlgButtonChecked(IDC_GSH_KEYBOARD_MOUSE) == BST_CHECKED;
    // IDC_GSH_LATERAL_MOVEMENT and IDC_GSH_DRONE_DIRECT are greyed out (WS_DISABLED in
    // the resource), so they are intentionally left out here to stay disabled.
    const int KeyboardMouseOptions[] = {
        IDC_GSH_PRESERVE_CAMERA,
        IDC_GSH_FREE_CAMERA_JUMP,
        IDC_GSH_PRONE_CBUTTONS,
        IDC_GSH_DRONE_INVERT_Y,
        IDC_GSH_FAST_CUTSCENES,
        IDC_GSH_ENABLE_SPRINT,
    };
    for (size_t i = 0; i < sizeof(KeyboardMouseOptions) / sizeof(KeyboardMouseOptions[0]); i++)
    {
        ::EnableWindow(GetDlgItem(KeyboardMouseOptions[i]), KeyboardMouse ? TRUE : FALSE);
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

LRESULT CGameSpecificHacksDialog::OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    EndDialog(IDOK);
    return 0;
}
