#include "pch.h"
#include <cmath>
#include "Util.h"
#include "DataRes.h"
#include "WebView2Host.h"

using namespace Microsoft::WRL;

namespace
{
    // WebView2 accepts zoom factors in this range only; anything outside is rejected.
    double ClampZoom(double zoom)
    {
        if (zoom < 0.25) return 0.25;
        if (zoom > 5.0) return 5.0;
        return zoom;
    }

    std::wstring ToHex(long value)
    {
        wchar_t buffer[16] = {};
        swprintf_s(buffer, L"%08lX", static_cast<unsigned long>(value));
        return buffer;
    }
}

void CWebView2Host::Init(HWND parent, const CRect& bounds, const std::wstring& userDataFolder, double zoom,
                         const std::wstring& userAgent, std::function<void(bool)> onReady)
{
    m_bounds = bounds;
    m_zoom = ClampZoom(zoom);   // zoom on a 100 % display; the display scaling is cancelled on top
    m_baseZoom = m_zoom;
    m_dpi = SystemDpi();
    auto alive = m_alive;

    // The WebView2 loader is linked statically, so the call normally works without any DLL next
    // to the executable. If it still fails the embedded copy is loaded and the call retried once.
    auto createEnvironment = [&]() -> HRESULT
    {
        return CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userDataFolder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, alive, parent, onReady, userAgent](HRESULT res, ICoreWebView2Environment* env) -> HRESULT
                {
                    if (!*alive) return S_OK;
                    if (FAILED(res) || !env)
                    {
                        LogFit(L"environment creation failed, hr=0x" + ToHex(res));
                        if (onReady) onReady(false);
                        return S_OK;
                    }
                    m_env = env;
                    HRESULT h2 = env->CreateCoreWebView2Controller(
                        parent,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, alive, onReady, userAgent](HRESULT r2, ICoreWebView2Controller* ctl) -> HRESULT
                        {
                            if (!*alive) return S_OK;
                            if (FAILED(r2) || !ctl)
                            {
                                if (onReady) onReady(false);
                                return S_OK;
                            }
                            m_controller = ctl;
                            HRESULT setupResult = m_controller->get_CoreWebView2(&m_view);
                            if (SUCCEEDED(setupResult) && !m_view)
                                setupResult = E_FAIL;
                            if (SUCCEEDED(setupResult) && !userAgent.empty())
                                setupResult = ApplyUserAgent(userAgent);
                            if (SUCCEEDED(setupResult))
                                setupResult = m_controller->put_Bounds(m_bounds);
                            if (SUCCEEDED(setupResult))
                                setupResult = ApplyContentScale();
                            if (SUCCEEDED(setupResult))
                                setupResult = m_controller->put_IsVisible(m_visible ? TRUE : FALSE);
                            if (FAILED(setupResult))
                            {
                                m_controller->Close();
                                m_controller.Reset();
                                m_view.Reset();
                                if (onReady) onReady(false);
                                return S_OK;
                            }
                            {
                                // Every finished navigation is checked once: a page authored for a
                                // different display scaling must not be cut off by the viewport.
                                EventRegistrationToken token = {};
                                m_view->add_NavigationCompleted(
                                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [this, alive](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT
                                        {
                                            if (*alive) FitPageWidth();
                                            return S_OK;
                                        }).Get(), &token);
                            }
                            if (!m_pendingUrl.empty() && m_view)
                            {
                                setupResult = m_view->Navigate(m_pendingUrl.c_str());
                                if (FAILED(setupResult))
                                {
                                    m_controller->Close();
                                    m_controller.Reset();
                                    m_view.Reset();
                                    if (onReady) onReady(false);
                                    return S_OK;
                                }
                            }
                            if (onReady) onReady(true);
                            return S_OK;
                        }).Get());
                if (FAILED(h2) && onReady) onReady(false);
                return S_OK;
            }).Get());
    };

    HRESULT hr = createEnvironment();
    if (FAILED(hr))
    {
        LogFit(L"environment call failed, hr=0x" + ToHex(hr));
        if (DataRes::LoadEmbeddedWebView2Loader())
        {
            hr = createEnvironment();
            LogFit(L"retry with the embedded loader, hr=0x" + ToHex(hr));
        }
    }
    if (FAILED(hr) && onReady)
        onReady(false);
}

HRESULT CWebView2Host::ApplyUserAgent(const std::wstring& userAgent)
{
    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
    Microsoft::WRL::ComPtr<ICoreWebView2Settings2> settings2;
    if (FAILED(m_view->get_Settings(&settings)) || !settings)
        return E_FAIL;
    if (FAILED(settings.As(&settings2)) || !settings2)
        return E_FAIL;
    const HRESULT hr = settings2->put_UserAgent(userAgent.c_str());
    LogFit(L"user agent: " + userAgent);
    return hr;
}

// The page must be laid out at one CSS pixel per device pixel, exactly like the IE control does,
// whatever display scaling Windows is set to. ZoomFactor alone is not reliable here: WebView2
// keeps honouring the monitor DPI for its layout, so the explicit 1:1 rasterization scale is
// used when the runtime offers it and a DPI-compensated zoom factor is the fallback.
HRESULT CWebView2Host::ApplyContentScale()
{
    const double baseZoom = m_zoom;   // zoom on a 100 % display
    Microsoft::WRL::ComPtr<ICoreWebView2Controller3> controller3;
    if (SUCCEEDED(m_controller.As(&controller3)) && controller3)
    {
        if (SUCCEEDED(controller3->put_RasterizationScale(1.0)))
        {
            m_devicePixelScale = true;
            m_zoom = ClampZoom(baseZoom);
            LogFit(L"content scale: rasterization 1.0, zoom " + std::to_wstring(m_zoom) +
                   L" (1 CSS px = 1 device px)");
            return m_controller->put_ZoomFactor(m_zoom);
        }
    }
    m_devicePixelScale = false;
    const int dpi = m_dpi > 0 ? m_dpi : 96;
    m_zoom = ClampZoom(baseZoom * 96.0 / static_cast<double>(dpi));
    LogFit(L"content scale: zoom " + std::to_wstring(m_zoom) + L" (DPI compensated, dpi " +
           std::to_wstring(dpi) + L")");
    return m_controller->put_ZoomFactor(m_zoom);
}

void CWebView2Host::FitPageWidth()
{    if (!m_view || !m_controller) return;
    m_fitPass = 0;
    ApplyViewportFit();
}

void CWebView2Host::ApplyViewportFit()
{
    if (!m_view || !m_controller) return;
    auto alive = m_alive;
    const int pass = m_fitPass;

    // The robot pages are laid out for a fixed screen. Chromium keeps that width instead of
    // shrinking it like the IE engine does, so the layout is measured and the zoom is lowered
    // until the page fits the viewport exactly.
    HRESULT hr = m_view->ExecuteScript(
        L"(function(){"
        L"function fixMargins(doc){try{var d=doc.documentElement,b=doc.body;"
        L"if(d){d.style.margin='0';d.style.padding='0';}"
        L"if(b){b.style.margin='0';b.style.padding='0';}}catch(e){}}"
        L"fixMargins(document);"
        L"function dims(doc){if(!doc){return [0,0];}var d=doc.documentElement,b=doc.body;"
        L"return [Math.max(d?d.scrollWidth:0,b?b.scrollWidth:0),"
        L"Math.max(d?d.scrollHeight:0,b?b.scrollHeight:0)];}"
        L"var m=dims(document),w=m[0],h=m[1];"
        L"try{for(var i=0;i<window.frames.length;i++){var fr=window.frames[i];"
        L"var el=fr.frameElement;var box=el?el.getBoundingClientRect():null;"
        L"fixMargins(fr.document);var f=dims(fr.document);"
        L"if(box){w=Math.max(w,box.left+(f[0]||box.width));h=Math.max(h,box.top+(f[1]||box.height));}"
        L"else{w=Math.max(w,f[0]);h=Math.max(h,f[1]);}}}catch(e){}"
        // scrollWidth misses content clipped by an overflow:hidden ancestor, so the right and
        // bottom edges of every element are measured as well.
        L"var mr=0,mb=0,n=0;"
        L"try{var all=document.querySelectorAll('*');"
        L"for(var i=0;i<all.length&&i<5000;i++){var r=all[i].getBoundingClientRect();"
        L"if(r.width>0||r.height>0){if(r.right>mr)mr=r.right;if(r.bottom>mb)mb=r.bottom;}}}catch(e){}"
        L"var vw=window.innerWidth||0,vh=window.innerHeight||0;"
        L"if(mr>vw*2||mb>vh*2){mr=0;mb=0;}"   // ignore elements parked far outside the viewport
        L"return Math.round(Math.max(w,mr))+','+Math.round(Math.max(h,mb))+','+Math.round(vw)+','"
        L"+Math.round(vh)+','+Math.round((window.devicePixelRatio||1)*100);"
        L"})()",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, alive, pass](HRESULT res, LPCWSTR result) -> HRESULT
            {
                if (!*alive || FAILED(res) || !result || !m_controller) return S_OK;
                int pageWidth = 0, pageHeight = 0, viewWidth = 0, viewHeight = 0, dpr100 = 100;
                if (swscanf_s(result, L"\"%d,%d,%d,%d,%d\"", &pageWidth, &pageHeight,
                              &viewWidth, &viewHeight, &dpr100) != 5)
                {
                    LogFit(std::wstring(L"unparsable measurement: ") + result);
                    return S_OK;
                }
                if (m_fitPass != pass) return S_OK;   // a newer navigation owns the fit now

                const double before = m_zoom;
                double after = before;

                // devicePixelRatio is the factor that maps a CSS pixel to a device pixel. It must
                // equal the requested content zoom; when it does not, the 1:1 rasterization scale
                // was ignored by the runtime (for example on 175 % displays) and the zoom factor
                // has to cancel the monitor scaling instead.
                const double dpr = dpr100 > 0 ? dpr100 / 100.0 : 1.0;
                if (m_baseZoom > 0 && std::fabs(dpr - m_baseZoom) > m_baseZoom * 0.05)
                {
                    after = ClampZoom(before * (m_baseZoom / dpr));
                    if (std::fabs(after - before) > 0.002)
                        SetZoom(after);
                    LogFit(L"scale mismatch (dpr=" + std::to_wstring(dpr100) + L" expected " +
                           std::to_wstring(static_cast<int>(m_baseZoom * 100 + 0.5)) + L"), zoom " +
                           std::to_wstring(before) + L"->" + std::to_wstring(after));
                }
                else
                {
                    // Only the width is fitted: a page that is merely taller than the viewport is a
                    // scrolling document, while a robot screen is a fixed layout that IE used to
                    // shrink horizontally - and the height follows the same factor.
                    double factor = 1.0;
                    if (pageWidth > viewWidth && viewWidth > 0)
                        factor = min(factor, static_cast<double>(viewWidth) / pageWidth);
                    if (factor < 0.995)
                    {
                        after = ClampZoom(before * factor);
                        if (after < before - 0.001)
                            SetZoom(after);
                    }
                }
                LogFit(L"fit pass " + std::to_wstring(pass) + L": page=" + std::to_wstring(pageWidth) +
                       L"x" + std::to_wstring(pageHeight) + L" view=" + std::to_wstring(viewWidth) +
                       L"x" + std::to_wstring(viewHeight) + L" dpr=" + std::to_wstring(dpr100) +
                       L" zoom=" + std::to_wstring(before) + L"->" + std::to_wstring(after));

                // One refinement: font metrics and table cells can reflow after the zoom change.
                if (std::fabs(after - before) > 0.002 && pass < 1)
                {
                    m_fitPass = pass + 1;
                    ApplyViewportFit();
                }
                return S_OK;
            }).Get());
    (void)hr;
}

void CWebView2Host::LogFit(const std::wstring& text)
{
    DataRes::LogLine(text);
}

void CWebView2Host::Navigate(const std::wstring& url)
{
    m_pendingUrl = url;
    if (m_view)
        m_view->Navigate(url.c_str());
}

void CWebView2Host::SetBounds(const CRect& bounds)
{
    m_bounds = bounds;
    if (m_controller)
        m_controller->put_Bounds(bounds);
}

void CWebView2Host::SetZoom(double zoom)
{
    m_zoom = ClampZoom(zoom);
    if (m_controller)
        m_controller->put_ZoomFactor(m_zoom);
}

void CWebView2Host::SetVisible(bool visible){
    m_visible = visible;
    if (m_controller)
        m_controller->put_IsVisible(visible ? TRUE : FALSE);
}

void CWebView2Host::Close()
{
    *m_alive = false;
    m_alive = std::make_shared<bool>(true);
    if (m_controller)
    {
        m_controller->Close();
        m_controller.Reset();
    }
    m_view.Reset();
    m_env.Reset();
}
