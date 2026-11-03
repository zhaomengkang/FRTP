#include "pch.h"
#include "Resource.h"
#include "Util.h"
#include "DataRes.h"

namespace DataRes
{
    namespace
    {
        // Bump the folder name whenever an embedded file changes, so a machine that already
        // unpacked an older copy picks up the new one.
        constexpr wchar_t kCacheFolder[] = L"FRTP\\data\\26.10.22";

        struct Entry
        {
            UINT id;
            const wchar_t* relative;
        };

        const Entry kEntries[] = {
            { IDR_DATA_HELP,           L"res\\Helper\\help.html" },
            { IDR_DATA_KAREL_7DA,      L"KAREL\\7DA\\a_mkweb.pc" },
            { IDR_DATA_KAREL_7DC,      L"KAREL\\7DC\\a_mkweb.pc" },
            { IDR_DATA_KAREL_7DF,      L"KAREL\\7DF\\a_mkweb.pc" },
            { IDR_DATA_KAREL_7DH,      L"KAREL\\7DH\\a_mkweb.pc" },
            { IDR_DATA_WEBVIEW2LOADER, L"runtimes\\win-x86\\native\\WebView2Loader.dll" },
        };

        HMODULE g_webView2Loader = nullptr;

        bool LocalAppData(std::wstring& path)
        {
            PWSTR raw = nullptr;
            if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw)) || !raw)
            {
                if (raw) ::CoTaskMemFree(raw);
                return false;
            }
            path = raw;
            ::CoTaskMemFree(raw);
            return !path.empty();
        }

        bool EnsureDirectory(const std::wstring& dir)
        {
            if (dir.empty()) return false;
            if (::CreateDirectoryW(dir.c_str(), nullptr)) return true;
            if (::GetLastError() == ERROR_ALREADY_EXISTS) return true;
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return false;
            return EnsureDirectory(dir.substr(0, slash)) && ::CreateDirectoryW(dir.c_str(), nullptr);
        }

        bool CachedFileSize(const std::wstring& path, unsigned long long& size)
        {
            WIN32_FILE_ATTRIBUTE_DATA info = {};
            if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
                return false;
            size = (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            return true;
        }

        // Writes one embedded resource to `destination`. An already unpacked copy of the same
        // size is kept as it is, which keeps startup cheap on the second run.
        bool Unpack(UINT id, const std::wstring& destination, std::wstring* error)
        {
            HRSRC resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
            if (!resource)
            {
                if (error) *error = L"Embedded resource " + std::to_wstring(id) + L" is missing.";
                return false;
            }
            HGLOBAL loaded = ::LoadResource(nullptr, resource);
            const void* data = loaded ? ::LockResource(loaded) : nullptr;
            const DWORD size = ::SizeofResource(nullptr, resource);
            if (!data || size == 0)
            {
                if (error) *error = L"Embedded resource " + std::to_wstring(id) + L" cannot be read.";
                return false;
            }

            unsigned long long cached = 0;
            if (CachedFileSize(destination, cached) && cached == size)
                return true;

            const size_t slash = destination.find_last_of(L"\\/");
            if (slash == std::wstring::npos || !EnsureDirectory(destination.substr(0, slash)))
            {
                if (error) *error = L"Cannot create the folder for '" + destination + L"'.";
                return false;
            }

            HANDLE file = ::CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                if (error) *error = L"Cannot write '" + destination + L"'.";
                return false;
            }
            DWORD written = 0;
            const BOOL ok = ::WriteFile(file, data, size, &written, nullptr);
            ::CloseHandle(file);
            if (!ok || written != size)
            {
                if (error) *error = L"Cannot write '" + destination + L"'.";
                return false;
            }
            return true;
        }

        std::wstring Destination(const wchar_t* relative)
        {
            const std::wstring root = CacheRoot();
            if (root.empty()) return std::wstring();
            return PathCombine2(root, relative);
        }

        std::wstring FindEntry(const wchar_t* relative)
        {
            std::wstring path = Destination(relative);
            if (!path.empty() && FileExists(path)) return path;
            return std::wstring();
        }
    }

    std::wstring CacheRoot()
    {
        std::wstring base;
        if (!LocalAppData(base)) return std::wstring();
        return PathCombine2(base, kCacheFolder);
    }

    bool Prepare(std::wstring* error)
    {
        for (const Entry& entry : kEntries)
        {
            const std::wstring destination = Destination(entry.relative);
            if (destination.empty())
            {
                if (error) *error = L"The local application data folder is not available.";
                return false;
            }
            if (!Unpack(entry.id, destination, error))
                return false;
        }
        return true;
    }

    bool LoadEmbeddedWebView2Loader()
    {
        if (g_webView2Loader) return true;
        const std::wstring dll = Destination(L"runtimes\\win-x86\\native\\WebView2Loader.dll");
        if (dll.empty()) return false;
        if (!FileExists(dll))
        {
            std::wstring ignored;
            Prepare(&ignored);
        }
        g_webView2Loader = ::LoadLibraryW(dll.c_str());
        return g_webView2Loader != nullptr;
    }

    std::wstring KarelProgram(int controllerType)
    {
        const wchar_t* folder = nullptr;
        switch (controllerType)
        {
        case 1: folder = L"KAREL\\7DA\\a_mkweb.pc"; break;
        case 2: folder = L"KAREL\\7DC\\a_mkweb.pc"; break;
        case 3: folder = L"KAREL\\7DF\\a_mkweb.pc"; break;
        case 4: folder = L"KAREL\\7DH\\a_mkweb.pc"; break;
        default: return std::wstring();
        }

        std::wstring path = FindEntry(folder);
        if (!path.empty()) return path;

        // Not unpacked yet (or the cache was cleared while running): try again once.
        std::wstring ignored;
        Prepare(&ignored);
        return FindEntry(folder);
    }

    std::wstring HelpPage()
    {
        std::wstring path = FindEntry(L"res\\Helper\\help.html");
        if (!path.empty()) return path;
        std::wstring ignored;
        Prepare(&ignored);
        return FindEntry(L"res\\Helper\\help.html");
    }

    void LogLine(const std::wstring& text)
    {
        const std::wstring root = CacheRoot();
        if (root.empty()) return;
        const std::wstring path = PathCombine2(root, L"webview2.log");
        std::wstring log = ReadTextFile(path);
        if (log.size() > 64 * 1024)
            log.clear();
        WriteTextFile(path, log + text + L"\r\n");
    }
}
