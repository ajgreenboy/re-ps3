#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#endif
#include "live_overlay.h"

namespace xmb {
#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;
namespace {
bool checked(HRESULT hr, const char* operation, QString* error) {
    if(SUCCEEDED(hr))return true;
    if(error)*error=QStringLiteral("%1: 0x%2").arg(QLatin1String(operation)).arg(quint32(hr),8,16,QLatin1Char('0'));
    return false;
}
LRESULT CALLBACK overlayProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if(message==WM_MOUSEACTIVATE) {
        SetForegroundWindow(reinterpret_cast<HWND>(GetWindowLongPtrW(window,GWLP_USERDATA)));
        return MA_NOACTIVATE;
    }
    // Keep mouse input off the title; keyboard and controller navigation belong to the Qt owner.
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,w,l);
}
}
struct LiveOverlay::Impl {
    HWND window=nullptr,owner=nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGISwapChain1> chain;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    QSize size;
    QRect monitor;
    ~Impl() { if(window)DestroyWindow(window); }
};
#else
struct LiveOverlay::Impl {};
#endif
LiveOverlay::LiveOverlay():impl_(std::make_unique<Impl>()) {}
LiveOverlay::~LiveOverlay()=default;

bool LiveOverlay::create(quintptr owner, const QRect& monitor, QString* error) {
#ifdef Q_OS_WIN
    auto& s=*impl_;
    s.owner=reinterpret_cast<HWND>(owner);
    s.monitor=monitor;
    if(monitor.isEmpty()) { if(error)*error=QStringLiteral("empty game monitor rectangle");return false; }
    WNDCLASSW wc{};
    wc.lpfnWndProc=overlayProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XmbLiveComposition";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
        if(error)*error=QStringLiteral("RegisterClass failed: %1").arg(GetLastError());return false;
    }
    s.window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        wc.lpszClassName,L"XMB live overlay",WS_POPUP,monitor.x(),monitor.y(),monitor.width(),monitor.height(),nullptr,nullptr,wc.hInstance,nullptr);
    if(!s.window) { if(error)*error=QStringLiteral("CreateWindowEx failed: %1").arg(GetLastError());return false; }
    SetWindowLongPtrW(s.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s.owner));
    if(!checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&s.device,nullptr,&s.context),"D3D11CreateDevice",error))return false;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    if(!checked(s.device.As(&dxgi),"IDXGIDevice",error) ||
       !checked(dxgi->GetAdapter(&adapter),"GetAdapter",error) ||
       !checked(adapter->GetParent(IID_PPV_ARGS(&s.factory)),"IDXGIFactory2",error) ||
       !checked(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&s.composition)),"DCompositionCreateDevice",error) ||
       !checked(s.composition->CreateTargetForHwnd(s.window,TRUE,&s.target),"CreateTargetForHwnd",error) ||
       !checked(s.composition->CreateVisual(&s.visual),"CreateVisual",error))return false;
    return true;
#else
    Q_UNUSED(owner);Q_UNUSED(monitor);if(error)*error=QStringLiteral("DirectComposition requires Windows");return false;
#endif
}
bool LiveOverlay::present(const QImage& image, QString* error) {
#ifdef Q_OS_WIN
    auto& s=*impl_;
    if(!IsWindow(s.window) || !IsWindow(s.owner) || image.isNull() || image.format()!=QImage::Format_ARGB32_Premultiplied) {
        if(error)*error=QStringLiteral("invalid overlay window or premultiplied framebuffer");return false;
    }
    if(s.size!=image.size()) {
        s.context->ClearState();
        if(s.chain) {
            if(!checked(s.chain->ResizeBuffers(2,UINT(image.width()),UINT(image.height()),DXGI_FORMAT_B8G8R8A8_UNORM,0),"ResizeBuffers",error))return false;
        } else {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width=UINT(image.width());desc.Height=UINT(image.height());
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
            desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
            desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
            if(!checked(s.factory->CreateSwapChainForComposition(s.device.Get(),&desc,nullptr,&s.chain),"CreateSwapChainForComposition",error) ||
               !checked(s.visual->SetContent(s.chain.Get()),"SetContent",error) ||
               !checked(s.target->SetRoot(s.visual.Get()),"SetRoot",error))return false;
        }
        const D2D_MATRIX_3X2_F scale{float(s.monitor.width())/image.width(),0,0,float(s.monitor.height())/image.height(),0,0};
        if(!checked(s.visual->SetTransform(scale),"SetTransform",error) ||
           !checked(s.composition->Commit(),"Commit",error))return false;
        s.size=image.size();
    }
    ComPtr<ID3D11Texture2D> buffer;
    if(!checked(s.chain->GetBuffer(0,IID_PPV_ARGS(&buffer)),"GetBuffer",error))return false;
    s.context->UpdateSubresource(buffer.Get(),0,nullptr,image.constBits(),UINT(image.bytesPerLine()),0);
    // Qt already paces this loop; do not add a second blocking vsync wait.
    if(!checked(s.chain->Present(0,0),"Present",error) ||
       !checked(s.device->GetDeviceRemovedReason(),"D3D device",error))return false;
    if(!IsWindowVisible(s.window)) {
        if(!SetWindowPos(s.window,HWND_TOPMOST,s.monitor.x(),s.monitor.y(),s.monitor.width(),s.monitor.height(),SWP_NOACTIVATE|SWP_SHOWWINDOW)) {
            if(error)*error=QStringLiteral("show overlay failed: %1").arg(GetLastError());return false;
        }
    }
    return true;
#else
    Q_UNUSED(image);Q_UNUSED(error);return false;
#endif
}
} // namespace xmb
