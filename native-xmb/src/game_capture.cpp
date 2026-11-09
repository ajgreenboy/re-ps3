// See game_capture.h. WinRT headers must precede Qt's (macro hygiene).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>
#include <inspectable.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include "game_capture.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace xmb {
namespace {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wgd11 = winrt::Windows::Graphics::DirectX::Direct3D11;

// IDirect3DDxgiInterfaceAccess (windows.graphics.directx.direct3d11.interop.h declares it too in
// recent SDKs; a local copy keeps this file independent of the SDK revision).
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDxgiAccessLocal : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID id, void** object) = 0;
};

QString hrText(HRESULT hr) {
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

QRect toQRect(const RECT& r) { return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top); }

QString windowText(HWND h) {
    wchar_t buf[256] = {};
    const int n = GetWindowTextW(h, buf, 255);
    return QString::fromWCharArray(buf, n);
}

QString windowClass(HWND h) {
    wchar_t buf[256] = {};
    const int n = GetClassNameW(h, buf, 255);
    return QString::fromWCharArray(buf, n);
}

// DeviceName of the IDXGIOutput whose HMONITOR matches.
QString dxgiOutputName(HMONITOR mon) {
    QString name;
    if (!mon) return name;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return name;
    for (UINT a = 0; name.isEmpty(); ++a) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0; name.isEmpty(); ++o) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(output->GetDesc(&d)) && d.Monitor == mon)
                name = QString::fromWCharArray(d.DeviceName);
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return name;
}

bool looksBlack(const QImage& img, QString* summary) {
    if (img.isNull()) return true;
    int maxChannel = 0;
    const int stepY = std::max(1, img.height() / 64), stepX = std::max(1, img.width() / 64);
    for (int y = 0; y < img.height(); y += stepY) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += stepX) {
            const QRgb p = line[x];
            maxChannel = std::max({maxChannel, qRed(p), qGreen(p), qBlue(p)});
        }
    }
    if (summary) *summary = QStringLiteral("maxChannel=%1").arg(maxChannel);
    return maxChannel <= 3;
}

struct Shared {
    std::mutex m;
    std::condition_variable cv;
    bool arrived = false;
};

GameCaptureResult captureImpl(HWND hwnd, int timeoutMs) {
    GameCaptureResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    };
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        if (!wgc::GraphicsCaptureSession::IsSupported()) { res.detail = QStringLiteral("WGC not supported"); return res; }

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{nullptr};
        HRESULT hr = interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(hr) || !item) { res.detail = QStringLiteral("CreateForWindow ") + hrText(hr); return res; }
        const auto size = item.Size();
        if (size.Width <= 0 || size.Height <= 0) { res.detail = QStringLiteral("item size empty"); return res; }

        winrt::com_ptr<ID3D11Device> dev;
        winrt::com_ptr<ID3D11DeviceContext> ctx;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                               nullptr, 0, D3D11_SDK_VERSION, dev.put(), nullptr, ctx.put());
        if (FAILED(hr)) { res.detail = QStringLiteral("D3D11CreateDevice ") + hrText(hr); return res; }
        auto dxgi = dev.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> insp;
        hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put());
        if (FAILED(hr)) { res.detail = QStringLiteral("CreateDirect3D11DeviceFromDXGIDevice ") + hrText(hr); return res; }
        auto winrtDevice = insp.as<wgd11::IDirect3DDevice>();

        auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        auto session = pool.CreateCaptureSession(item);

        // Windows 11 / recent 10: no border, no cursor. Each is optional and may be refused.
        using winrt::Windows::Foundation::Metadata::ApiInformation;
        if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled")) {
            try { session.IsCursorCaptureEnabled(false); res.cursorOff = true; } catch (...) {}
        }
        if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
            try { session.IsBorderRequired(false); res.borderOff = true; } catch (...) {}
        }

        Shared sh;
        auto token = pool.FrameArrived(winrt::auto_revoke, [&](auto&&, auto&&) {
            std::lock_guard<std::mutex> l(sh.m);
            sh.arrived = true;
            sh.cv.notify_all();
        });
        session.StartCapture();

        {
            std::unique_lock<std::mutex> l(sh.m);
            sh.cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return sh.arrived; });
            if (!sh.arrived) {
                l.unlock();
                token.revoke();
                session.Close(); pool.Close();
                res.elapsedMs = elapsed();
                res.detail = QStringLiteral("timeout: no frame within %1 ms").arg(timeoutMs);
                return res;
            }
        }
        auto frame = pool.TryGetNextFrame();
        if (!frame) { token.revoke(); session.Close(); pool.Close(); res.detail = QStringLiteral("TryGetNextFrame null"); return res; }
        const auto content = frame.ContentSize();

        auto access = frame.Surface().as<IDxgiAccessLocal>();
        winrt::com_ptr<ID3D11Texture2D> tex;
        hr = access->GetInterface(__uuidof(ID3D11Texture2D), tex.put_void());
        if (FAILED(hr)) { token.revoke(); session.Close(); pool.Close(); res.detail = QStringLiteral("GetInterface ") + hrText(hr); return res; }

        D3D11_TEXTURE2D_DESC desc{};
        tex->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::com_ptr<ID3D11Texture2D> staging;
        hr = dev->CreateTexture2D(&desc, nullptr, staging.put());
        if (FAILED(hr)) { token.revoke(); session.Close(); pool.Close(); res.detail = QStringLiteral("CreateTexture2D ") + hrText(hr); return res; }
        ctx->CopyResource(staging.get(), tex.get());
        D3D11_MAPPED_SUBRESOURCE map{};
        hr = ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map);
        if (FAILED(hr)) { token.revoke(); session.Close(); pool.Close(); res.detail = QStringLiteral("Map ") + hrText(hr); return res; }
        const int w = std::min<int>(content.Width, int(desc.Width)), h = std::min<int>(content.Height, int(desc.Height));
        QImage img(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) {
            const uchar* src = static_cast<const uchar*>(map.pData) + size_t(y) * map.RowPitch;
            uchar* dst = img.scanLine(y);
            memcpy(dst, src, size_t(w) * 4);
            for (int x = 0; x < w; ++x) dst[x * 4 + 3] = 0xFF;  // BGRA -> opaque RGB32
        }
        ctx->Unmap(staging.get(), 0);
        frame.Close();
        token.revoke();
        session.Close();
        pool.Close();

        QString summary;
        res.image = img;
        res.status = looksBlack(img, &summary) ? GameCaptureStatus::Black : GameCaptureStatus::Ok;
        res.detail = QStringLiteral("%1x%2 %3").arg(w).arg(h).arg(summary);
    } catch (const winrt::hresult_error& e) {
        res.status = GameCaptureStatus::Failed;
        res.image = QImage();
        res.detail = QStringLiteral("winrt error ") + hrText(e.code()) + QLatin1Char(' ')
                   + QString::fromWCharArray(e.message().c_str());
    } catch (...) {
        res.status = GameCaptureStatus::Failed;
        res.image = QImage();
        res.detail = QStringLiteral("unknown exception");
    }
    res.elapsedMs = elapsed();
    return res;
}

}  // namespace

const char* gameWindowModeName(GameWindowMode m) {
    switch (m) {
    case GameWindowMode::Minimized: return "minimized";
    case GameWindowMode::Windowed: return "windowed";
    case GameWindowMode::Borderless: return "borderless";
    case GameWindowMode::Exclusive: return "exclusive";
    default: return "invalid";
    }
}

const char* gameCaptureStatusName(GameCaptureStatus s) {
    switch (s) {
    case GameCaptureStatus::Ok: return "ok";
    case GameCaptureStatus::Black: return "black";
    default: return "failed";
    }
}

GameWindowInfo classifyGameWindow(quintptr handle) {
    GameWindowInfo info;
    info.hwnd = handle;
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!hwnd || !IsWindow(hwnd)) { info.reason = QStringLiteral("not a window"); return info; }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    info.pid = pid;
    info.title = windowText(hwnd);
    info.className = windowClass(hwnd);
    info.foreground = GetForegroundWindow() == hwnd;

    DWORD cloakReason = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakReason, sizeof(cloakReason))))
        info.cloaked = cloakReason != 0;

    RECT wr{};
    GetWindowRect(hwnd, &wr);
    info.windowRect = toQRect(wr);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    info.popupStyle = (style & WS_POPUP) != 0;
    info.captionStyle = (style & WS_CAPTION) == WS_CAPTION;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) info.monitorRect = toQRect(mi.rcMonitor);
    info.outputName = dxgiOutputName(mon);
    info.coversMonitor = !info.monitorRect.isEmpty() && info.windowRect.contains(info.monitorRect);

    QUERY_USER_NOTIFICATION_STATE qs;
    if (SUCCEEDED(SHQueryUserNotificationState(&qs))) info.notificationState = int(qs);

    if (!IsWindowVisible(hwnd)) { info.reason = QStringLiteral("window not visible"); return info; }
    if (info.cloaked) { info.reason = QStringLiteral("cloaked by DWM (reason %1)").arg(cloakReason); return info; }
    if (IsIconic(hwnd)) { info.mode = GameWindowMode::Minimized; info.reason = QStringLiteral("iconic"); return info; }

    const QString kind = info.popupStyle ? QStringLiteral("popup") : QStringLiteral("captionless window");
    if (info.coversMonitor && !info.captionStyle) {
        // QUNS_RUNNING_D3D_FULL_SCREEN is session-wide, so only trust it for the foreground window.
        if (info.notificationState == QUNS_RUNNING_D3D_FULL_SCREEN && info.foreground) {
            info.mode = GameWindowMode::Exclusive;
            info.reason = QStringLiteral("monitor-sized %1, foreground, SHQueryUserNotificationState=D3D_FULL_SCREEN").arg(kind);
        } else {
            info.mode = GameWindowMode::Borderless;
            info.reason = QStringLiteral("monitor-sized %1, notification state %2").arg(kind).arg(info.notificationState);
        }
    } else {
        info.mode = GameWindowMode::Windowed;
        info.reason = info.coversMonitor ? QStringLiteral("covers monitor but has a caption (maximized)")
                                         : QStringLiteral("smaller than its monitor");
    }
    return info;
}

quintptr findMainWindow(quint32 pid) {
    struct Ctx { DWORD pid; HWND best; qint64 area; } c{pid, nullptr, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        if (p != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        DWORD cl = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cl, sizeof(cl))) && cl) return TRUE;
        RECT r{};
        GetWindowRect(h, &r);
        const qint64 a = qint64(r.right - r.left) * (r.bottom - r.top);
        if (a > c->area) { c->area = a; c->best = h; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&c));
    return reinterpret_cast<quintptr>(c.best);
}

GameCaptureResult captureGameWindow(quintptr handle, int timeoutMs) {
    GameCaptureResult out;
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!hwnd || !IsWindow(hwnd)) { out.detail = QStringLiteral("not a window"); return out; }
    // Own MTA thread: independent of the caller's apartment. Every wait inside is bounded by timeoutMs.
    std::thread worker([&] { out = captureImpl(hwnd, timeoutMs); });
    worker.join();
    return out;
}

namespace {
// pid -> parent pid for every process, from one Toolhelp snapshot.
QHash<quint32, quint32> parentMap() {
    QHash<quint32, quint32> parents;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return parents;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        parents.insert(e.th32ProcessID, e.th32ParentProcessID);
    CloseHandle(snap);
    return parents;
}
bool inTree(const QHash<quint32, quint32>& parents, quint32 pid, quint32 root) {
    // Bounded walk: a recycled parent id can make a cycle.
    for (int depth = 0; pid && depth < 32; ++depth) {
        if (pid == root) return true;
        pid = parents.value(pid, 0);
    }
    return false;
}
}  // namespace

bool pidInTree(quint32 pid, quint32 rootPid) {
    if (!pid || !rootPid) return false;
    return inTree(parentMap(), pid, rootPid);
}

int postCloseToProcessTree(quint32 rootPid) {
    if (!rootPid) return 0;
    struct Ctx { QHash<quint32, quint32> parents; quint32 root; int posted; } c{parentMap(), rootPid, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || !inTree(c->parents, p, c->root)) return TRUE;
        if (PostMessageW(h, WM_CLOSE, 0, 0)) ++c->posted;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&c));
    return c.posted;
}

QString gameCaptureLogLine(const GameWindowInfo& i, const GameCaptureResult& r) {
    return QStringLiteral("ingame: window pid=%1 hwnd=0x%2 mode=%3 capture=%4 (%5 ms) title=\"%6\" class=%7 "
                          "rect=%8,%9 %10x%11 monitor=%12x%13 output=%14 qUNS=%15 borderOff=%16 cursorOff=%17 | %18 | %19")
        .arg(i.pid).arg(i.hwnd, 0, 16).arg(QLatin1String(gameWindowModeName(i.mode)))
        .arg(QLatin1String(gameCaptureStatusName(r.status))).arg(r.elapsedMs).arg(i.title).arg(i.className)
        .arg(i.windowRect.x()).arg(i.windowRect.y()).arg(i.windowRect.width()).arg(i.windowRect.height())
        .arg(i.monitorRect.width()).arg(i.monitorRect.height()).arg(i.outputName).arg(i.notificationState)
        .arg(r.borderOff).arg(r.cursorOff).arg(i.reason).arg(r.detail);
}

}  // namespace xmb
