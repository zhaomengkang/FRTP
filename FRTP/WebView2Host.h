#pragma once

// Thin wrapper around a WebView2 controller hosted inside an existing window.
class CWebView2Host
{
public:
    CWebView2Host() = default;
    ~CWebView2Host() { Close(); }

    // Starts asynchronous creation. `zoom` is the content zoom on a 100 % display; the current
    // monitor scaling is cancelled on top of it. A non-empty `userAgent` replaces the browser
    // user agent (used to receive the same pages the IE control gets).
    // onReady(true/false) is invoked on the UI thread.
    void Init(HWND parent, const CRect& bounds, const std::wstring& userDataFolder, double zoom,
              const std::wstring& userAgent = std::wstring(),
              std::function<void(bool)> onReady = nullptr);
    void Navigate(const std::wstring& url);
    void SetBounds(const CRect& bounds);
    void SetZoom(double zoom);
    void SetVisible(bool visible);
    // Shrinks the zoom when the page is wider than the viewport, so a page designed for one
    // display scaling is never clipped on another.
    void FitPageWidth();
    bool IsReady() const { return m_controller != nullptr; }
    void Close();

private:
    HRESULT ApplyUserAgent(const std::wstring& userAgent);
    HRESULT ApplyContentScale();
    void ApplyViewportFit();
    void LogFit(const std::wstring& text);

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_env;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_view;
    std::wstring m_pendingUrl;
    CRect m_bounds;
    bool m_visible = true;
    double m_zoom = 1.0;
    double m_baseZoom = 1.0;           // requested content zoom (independent of the display DPI)
    int m_dpi = 96;
    bool m_devicePixelScale = false;   // true when the controller renders 1 CSS px per device px
    int m_fitPass = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};
