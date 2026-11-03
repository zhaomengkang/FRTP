#include "pch.h"
#include "Util.h"
#include "FtpClient.h"
#include "MkWebClient.h"
#include "DataRes.h"
#include "FRLoadUserPC.h"

namespace FRLoadUserPC
{
    bool LoadMKWebServer(const std::wstring& host, int controllerType, const std::wstring& user,
                         const std::wstring& pass, const std::wstring& appTitle)
    {
        std::wstring serverVersion = MkWebClient::GetLibVersion(host);
        if (!serverVersion.empty() && !appTitle.empty() &&
            ToLower(appTitle).find(ToLower(serverVersion)) != std::wstring::npos)
            return true;

        // The KAREL program of the matching controller type is shipped inside the executable.
        std::wstring local = DataRes::KarelProgram(controllerType);
        if (local.empty())
            throw AppError(L"Embedded KAREL program is not available for this controller type.\r\nCache folder: '" +
                           DataRes::CacheRoot() + L"'");
        if (!FileExists(local))
            throw AppError(L"Local file not found.\r\nFile name: '" + local + L"'");

        DeleteSimKey(host, user, pass);

        try
        {
            FtpClient ftp(host, user, pass);
            ftp.Connect();
            ftp.SetServerPath(L"MDB:");
            ftp.Upload(local, L"A_MKWEB.PC");
            return true;
        }
        catch (...)
        {
            throw AppError(L"Failed to upload server program.\r\n1. Enter valid INSTALL username and password\r\n2. Verify that the R632(KAREL) option is installed on the robot controller");
        }
    }

    bool DeleteSimKey(const std::wstring& host, const std::wstring& user, const std::wstring& pass)
    {
        try
        {
            FtpClient ftp(host, user, pass);
            ftp.Connect();
            ftp.SetServerPath(L"MDB:");
            ftp.DeleteFile(L"A_MKWEB.PC");
            ftp.DeleteFile(L"A_MKWEB.VR");
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
