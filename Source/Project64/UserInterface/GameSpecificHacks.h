#pragma once

#include "resource.h"
#include <Project64-core/Settings/SettingsID.h>

class CGameSpecificHacksDialog :
    public CDialogImpl<CGameSpecificHacksDialog>
{
public:
    BEGIN_MSG_MAP_EX(CGameSpecificHacksDialog)
    {
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog);
        COMMAND_HANDLER(IDC_GSH_KEYBOARD_MOUSE, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_GAMEPAD1, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_GAMEPAD2, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_GAMEPAD_STOCK_AIM, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_KEYBOARD_MOUSE_PORT, CBN_SELCHANGE, OnPortChanged);
        COMMAND_HANDLER(IDC_GSH_GAMEPAD1_PORT, CBN_SELCHANGE, OnPortChanged);
        COMMAND_HANDLER(IDC_GSH_GAMEPAD2_PORT, CBN_SELCHANGE, OnPortChanged);
        MESSAGE_HANDLER(WM_HSCROLL, OnStickCameraSpeedChanged);
        COMMAND_HANDLER(IDC_GSH_LATERAL_MOVEMENT, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_PRESERVE_CAMERA, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_FREE_CAMERA_JUMP, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_SNAP_CAMERA_AIM, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_PRONE_CBUTTONS, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_DRONE_INVERT_Y, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_DRONE_DIRECT, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_KEEP_30FPS, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_SCHEDULER_RELEASE, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_CPU_BUDGET, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_HALVE_ENEMY_SPEED, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_FAST_CUTSCENES, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_SYNC_AUDIO, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_FAST_CUTSCENES, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_ENABLE_SPRINT, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_WIDESCREEN_HUD, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_ALIGN_HUD, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_SHOW_INPUT_RATE, BN_CLICKED, OnCheckBoxClicked);
        COMMAND_HANDLER(IDC_GSH_FRAME_RATE, CBN_SELCHANGE, OnFrameRateChanged);
        COMMAND_ID_HANDLER(IDOK, OnClose);
        COMMAND_ID_HANDLER(IDCANCEL, OnClose);
    }
    END_MSG_MAP()

    enum
    {
        IDD = IDD_GameSpecificHacks
    };

private:
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL & bHandled);
    LRESULT OnCheckBoxClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);
    LRESULT OnFrameRateChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);
    LRESULT OnPortChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);
    LRESULT OnStickCameraSpeedChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL & bHandled);
    LRESULT OnClose(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);

    void LoadSettings(void);
    void SaveCheckBox(int ControlId, SettingID Setting);
    void FillPortList(int ControlId, SettingID Setting);
    void UpdateControlState(void);
};
