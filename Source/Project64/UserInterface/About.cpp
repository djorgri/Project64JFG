#include "stdafx.h"

#include <Project64\UserInterface\About.h>

LRESULT CAboutDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL & /*bHandled*/)
{
    const wchar_t * AboutMsg =
        L"Project64JFG is a custom version of the official Project64 emulator, created with the assistance of AI. "
        L"It is a non-commercial fun project, developed independently in free time.";
    const wchar_t * CreditsMsg =
        L"The original Project64 developers and contributors deserve the credit for the emulator. "
        L"Please support the official Project64 project.";

    CDC hDC = GetDC();
    float DPIScale = hDC.GetDeviceCaps(LOGPIXELSX) / 96.0f;
    LOGFONT lf = {0};
    CFontHandle(GetDlgItem(IDC_VERSION).GetFont()).GetLogFont(&lf);
    lf.lfHeight = (int)(16 * DPIScale);
    m_TextFont.CreateFontIndirect(&lf);
    lf.lfHeight = (int)(18 * DPIScale);
    lf.lfWeight += 200;
    m_BoldFont.CreateFontIndirect(&lf);

    CWindow VersionWnd = GetDlgItem(IDC_VERSION);
    VersionWnd.SetWindowText(stdstr_f("Version: %s", VER_FILE_VERSION_STR).ToUTF16().c_str());
    VersionWnd.SetFont(m_BoldFont);
    SetWindowDetais(IDC_ABOUT_PROJECT, IDC_VERSION, AboutMsg, m_TextFont);
    SetWindowDetais(IDC_THANKS_CORE, IDC_ABOUT_PROJECT, L"Credits and support", m_BoldFont);
    SetWindowDetais(IDC_CORE_THANK_LIST, IDC_THANKS_CORE, CreditsMsg, m_TextFont);
    PlaceControlBelow(IDC_ABOUT_SUPPORT, IDC_CORE_THANK_LIST, 24);
    PlaceControlBelow(IDC_ABOUT_WEBSITE, IDC_ABOUT_SUPPORT);
    PlaceControlBelow(IDC_ABOUT_DISCORD, IDC_ABOUT_WEBSITE);
    PlaceControlBelow(IDOK, IDC_ABOUT_DISCORD);

    return TRUE;
}

void CAboutDlg::PlaceControlBelow(int nIDDlgItem, int nAboveIDDlgItem, int spacing)
{
    CWindow Wnd = GetDlgItem(nIDDlgItem);
    CWindow AboveWnd = GetDlgItem(nAboveIDDlgItem);

    CRect rcAbove;
    AboveWnd.GetWindowRect(&rcAbove);
    ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rcAbove, 2);

    CRect rcWnd;
    Wnd.GetWindowRect(&rcWnd);
    ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rcWnd, 2);

    CDC hDC = GetDC();
    const float DPIScale = hDC.GetDeviceCaps(LOGPIXELSX) / 96.0f;
    Wnd.SetWindowPos(nullptr, rcWnd.left, rcAbove.bottom + (LONG)(spacing * DPIScale),
                     0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOOWNERZORDER);
}

void CAboutDlg::SetWindowDetais(int nIDDlgItem, int nAboveIDDlgItem, const wchar_t * Text, const HFONT & font)
{
    CWindow Wnd = GetDlgItem(nIDDlgItem);
    Wnd.SetWindowText(Text);
    Wnd.SetFont(font);

    CDC hDC = GetDC();
    float DPIScale = hDC.GetDeviceCaps(LOGPIXELSX) / 96.0f;
    hDC.SelectFont(font);

    CRect rcWin;
    Wnd.GetWindowRect(&rcWin);
    ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rcWin, 2);
    if (hDC.DrawText(Text, -1, &rcWin, DT_LEFT | DT_CALCRECT | DT_WORDBREAK | DT_NOCLIP) > 0)
    {
        Wnd.SetWindowPos(nullptr, 0, 0, rcWin.Width(), rcWin.Height(), SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER);
    }

    CWindow AboveWnd = GetDlgItem(nAboveIDDlgItem);
    AboveWnd.GetWindowRect(&rcWin);
    ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rcWin, 2);
    LONG Top = rcWin.bottom + (LONG)(8 * DPIScale);

    Wnd.GetWindowRect(&rcWin);
    ::MapWindowPoints(nullptr, m_hWnd, (LPPOINT)&rcWin, 2);
    Wnd.SetWindowPos(nullptr, rcWin.left, Top, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOOWNERZORDER);
}

LRESULT CAboutDlg::OnColorStatic(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL & /*bHandled*/)
{
    HDC hdcStatic = (HDC)wParam;
    SetTextColor(hdcStatic, RGB(0, 0, 0));
    SetBkMode(hdcStatic, TRANSPARENT);
    return (LONG)(LRESULT)((HBRUSH)GetStockObject(NULL_BRUSH));
}

LRESULT CAboutDlg::OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL & /*bHandled*/)
{
    static HPEN outline = CreatePen(PS_SOLID, 1, 0x00FFFFFF);
    static HBRUSH fill = CreateSolidBrush(0x00FFFFFF);
    SelectObject((HDC)wParam, outline);
    SelectObject((HDC)wParam, fill);

    RECT rect;
    GetClientRect(&rect);

    Rectangle((HDC)wParam, rect.left, rect.top, rect.right, rect.bottom);
    return TRUE;
}

LRESULT CAboutDlg::OnSupportProject64(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    ShellExecuteW(m_hWnd, L"open", L"https://www.pj64-emu.com/support-project64.html", nullptr, nullptr, SW_SHOWNORMAL);
    return TRUE;
}

LRESULT CAboutDlg::OnWebsite(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    ShellExecuteW(m_hWnd, L"open", L"https://www.pj64-emu.com", nullptr, nullptr, SW_SHOWNORMAL);
    return TRUE;
}

LRESULT CAboutDlg::OnDiscord(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    ShellExecuteW(m_hWnd, L"open", L"https://discord.gg/Cg3zquF", nullptr, nullptr, SW_SHOWNORMAL);
    return TRUE;
}

LRESULT CAboutDlg::OnOkCmd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/)
{
    EndDialog(0);
    return TRUE;
}
