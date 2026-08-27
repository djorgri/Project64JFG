#pragma once
#include "resource.h"

class CAboutDlg :
    public CDialogImpl<CAboutDlg>
{
public:
    CAboutDlg() = default;

    BEGIN_MSG_MAP_EX(CAboutDlg)
    {
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog);
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnColorStatic);
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground);
        COMMAND_ID_HANDLER(IDC_ABOUT_SUPPORT, OnSupportProject64);
        COMMAND_ID_HANDLER(IDC_ABOUT_WEBSITE, OnWebsite);
        COMMAND_ID_HANDLER(IDC_ABOUT_DISCORD, OnDiscord);
        COMMAND_ID_HANDLER(IDOK, OnOkCmd);
        COMMAND_ID_HANDLER(IDCANCEL, OnOkCmd);
    }
    END_MSG_MAP()

    enum
    {
        IDD = IDD_About
    };

private:
    CAboutDlg(const CAboutDlg &);
    CAboutDlg & operator=(const CAboutDlg &);

    void SetWindowDetais(int nIDDlgItem, int nAboveIDDlgItem, const wchar_t * Text, const HFONT & font);
    void PlaceControlBelow(int nIDDlgItem, int nAboveIDDlgItem, int spacing = 8);

    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL & bHandled);
    LRESULT OnColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL & bHandled);
    LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL & bHandled);
    LRESULT OnSupportProject64(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);
    LRESULT OnWebsite(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);
    LRESULT OnDiscord(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);
    LRESULT OnOkCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL & bHandled);

    CFont m_BoldFont;
    CFont m_TextFont;
};
