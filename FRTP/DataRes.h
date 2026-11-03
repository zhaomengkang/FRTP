#pragma once
#include <string>

// Data files that used to ship next to the executable (the KAREL programs, the help page and
// the WebView2 loader) are compiled into the executable as RCDATA and unpacked into a per-user
// cache folder on first use, so the application runs from a single file.
namespace DataRes
{
    // Unpacks every embedded file into the cache folder. Returns false and fills `error` when
    // a resource could not be written.
    bool Prepare(std::wstring* error = nullptr);

    // Loads the embedded WebView2Loader.dll into the process. Only needed when the WebView2
    // environment cannot be created from the statically linked loader; the handle is kept for
    // the lifetime of the process.
    bool LoadEmbeddedWebView2Loader();

    // Cache folder that holds the unpacked copies.
    std::wstring CacheRoot();

    // Path of a_mkweb.pc for the given controller type (1=7DA, 2=7DC, 3=7DF, 4=7DH).
    // Empty when the resource is unavailable.
    std::wstring KarelProgram(int controllerType);

    // Path of the unpacked help page.
    std::wstring HelpPage();

    // Appends one line to the diagnostic log in the cache folder (<cache>\webview2.log).
    void LogLine(const std::wstring& text);
}
