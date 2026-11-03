#include "pch.h"
#include "Resource.h"
#include "Util.h"
#include "DataRes.h"
#include "HelpDlg.h"

CHelpDlg* CHelpDlg::s_instance = nullptr;

BEGIN_MESSAGE_MAP(CHelpDlg, CDialog)
    ON_WM_CLOSE()
END_MESSAGE_MAP()

CHelpDlg::CHelpDlg(CWnd* parent) : CDialog(IDD_HELP_DIALOG, parent) {}

void CHelpDlg::ShowHelp(CWnd* parent)
{
    if (s_instance && ::IsWindow(s_instance->GetSafeHwnd()))
    {
        s_instance->ShowWindow(SW_RESTORE);
        s_instance->SetForegroundWindow();
        return;
    }
    s_instance = new CHelpDlg(parent);
    if (!s_instance->Create(IDD_HELP_DIALOG, parent))
    {
        delete s_instance;
        s_instance = nullptr;
        return;
    }
    s_instance->ShowWindow(SW_SHOW);
}

BOOL CHelpDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    CRect rc;
    GetDlgItem(IDC_WEBVIEW_HELP)->GetWindowRect(rc);
    ScreenToClient(rc);

    std::wstring folder = WebView2DataFolder();
    // The help window itself grows with the display scaling, so the help page keeps the same
    // zoom: its text stays proportional to the window on every scaling setting.
    m_web.Init(GetSafeHwnd(), rc, folder, 0.7, std::wstring(), [this](bool ok)
    {
        if (!ok)
        {
            ::MessageBoxW(GetSafeHwnd(), L"WebView2 runtime is not available.", L"WebView2 Init failed", MB_OK | MB_ICONERROR);
            return;
        }
        // help.html is compiled into the executable and unpacked on demand.
        std::wstring html = DataRes::HelpPage();
        if (!html.empty())
        {
            std::wstring url = L"file:///" + html;
            std::replace(url.begin(), url.end(), L'\\', L'/');
            m_web.Navigate(url);
        }
        else
        {
            ::MessageBoxW(GetSafeHwnd(), L"Unable to unpack the embedded help page.", L"Help", MB_OK | MB_ICONERROR);
        }
    });
    return TRUE;
}

void CHelpDlg::OnClose() { DestroyWindow(); }

void CHelpDlg::PostNcDestroy()
{
    m_web.Close();
    s_instance = nullptr;
    delete this;
}
