#include "render_gate.h"
#include "host.h"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <atomic>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"wtsapi32.lib")
#endif

namespace xmb {
namespace {
std::atomic_bool g_closed{false};
bool gateEnabled() {
    static const bool enabled=qEnvironmentVariable("XMB_RENDER_GATE")!=QStringLiteral("0");
    return enabled;
}
// XMB_RENDER_GATE=observe: decide and log as usual but never stop the frame loop (measurement runs use it to see whether
// a window ever covered the one being measured).
bool observeOnly() {
    static const bool observe=qEnvironmentVariable("XMB_RENDER_GATE")==QStringLiteral("observe");
    return observe;
}

#ifdef Q_OS_WIN
bool cloaked(HWND window) {
    DWORD value=0;
    return SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&value,sizeof value)) && value!=0;
}

// True when `self` is shown but every pixel of its client area lies under an opaque window above it.
bool coveredByOthers(HWND self) {
    RECT client{};
    if(!GetClientRect(self,&client))return false;
    POINT origin{0,0};
    ClientToScreen(self,&origin);
    OffsetRect(&client,origin.x,origin.y);
    if(IsRectEmpty(&client))return false;
    HRGN visible=CreateRectRgnIndirect(&client);
    HRGN cover=CreateRectRgn(0,0,0,0);
    bool covered=false;
    const DWORD ownProcess=GetCurrentProcessId();
    int examined=0;
    for(HWND other=GetWindow(self,GW_HWNDPREV);other && examined<512;other=GetWindow(other,GW_HWNDPREV),++examined) {
        if(!IsWindowVisible(other) || IsIconic(other) || cloaked(other))continue;
        DWORD process=0;GetWindowThreadProcessId(other,&process);
        if(process==ownProcess)continue;
        const LONG_PTR style=GetWindowLongPtrW(other,GWL_EXSTYLE);
        if(style&(WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP))continue; // may show what is behind it
        RECT frame{};
        if(FAILED(DwmGetWindowAttribute(other,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof frame)) && !GetWindowRect(other,&frame))continue;
        RECT overlap{};
        if(!IntersectRect(&overlap,&frame,&client))continue;
        SetRectRgn(cover,overlap.left,overlap.top,overlap.right,overlap.bottom);
        // A window with its own region (rounded or shaped) covers less than its rectangle; skip rather than guess.
        HRGN shape=CreateRectRgn(0,0,0,0);
        const int hasShape=GetWindowRgn(other,shape);
        DeleteObject(shape);
        if(hasShape!=ERROR && hasShape!=NULLREGION)continue;
        if(CombineRgn(visible,visible,cover,RGN_DIFF)==NULLREGION){covered=true;break;}
    }
    DeleteObject(cover);
    DeleteObject(visible);
    return covered;
}

bool inputDesktopReachable() {
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_SWITCHDESKTOP);
    if(!desktop)return false; // the secure desktop (lock screen, UAC) or a disconnected session owns the input
    CloseDesktop(desktop);
    return true;
}
#endif
}

bool renderGateClosed() { return g_closed.load(std::memory_order_relaxed); }

#ifdef Q_OS_WIN
// WM_WTSSESSION_CHANGE arrives at the registered window; Qt hands it to the application's native event filters.
class SessionFilter final:public QAbstractNativeEventFilter {
public:
    explicit SessionFilter(RenderGate* gate):gate_(gate) {}
    bool nativeEventFilter(const QByteArray& type,void* message,qintptr*) override {
        if(type!="windows_generic_MSG" && type!="windows_dispatcher_MSG")return false;
        const MSG* msg=static_cast<const MSG*>(message);
        if(msg->message!=WM_WTSSESSION_CHANGE)return false;
        switch(msg->wParam) {
        case WTS_SESSION_LOCK: case WTS_CONSOLE_DISCONNECT: case WTS_REMOTE_DISCONNECT:
            qInfo().noquote()<<QString("render gate: session event %1 (lock/disconnect)").arg(qulonglong(msg->wParam));
            gate_->sessionAway_=true;break;
        case WTS_SESSION_UNLOCK: case WTS_CONSOLE_CONNECT: case WTS_REMOTE_CONNECT:
            qInfo().noquote()<<QString("render gate: session event %1 (unlock/connect)").arg(qulonglong(msg->wParam));
            gate_->sessionAway_=false;break;
        default: return false;
        }
        QMetaObject::invokeMethod(gate_,[gate=gate_]{gate->evaluateNow();},Qt::QueuedConnection);
        return false;
    }
private:
    RenderGate* gate_;
};
#else
class SessionFilter {};
#endif

RenderGate::RenderGate(QWidget* window,QWidget* view,QObject* parent):QObject(parent),window_(window),view_(view) {
    if(!gateEnabled()){qInfo()<<"render gate: disabled (XMB_RENDER_GATE=0)";return;}
#ifdef Q_OS_WIN
    // Display state notifications are shared with Auto-Off (host.cpp); subscribing is idempotent.
    host::observeDisplayState();
    filter_=new SessionFilter(this);
    qApp->installNativeEventFilter(filter_);
    if(window_ && window_->windowHandle()==nullptr)window_->winId(); // make sure there is a native handle to register
    if(window_)WTSRegisterSessionNotification(reinterpret_cast<HWND>(window_->winId()),NOTIFY_FOR_THIS_SESSION);
#endif
    // The input-desktop probe is only a backstop for a missed session notification. If it already fails while the session
    // is plainly in use (no permission to open the desktop, a service-style launch), it would shut the gate for good, so
    // it is used only where it passes at start-up.
#ifdef Q_OS_WIN
    inputDesktopProbe_=inputDesktopReachable();
    if(!inputDesktopProbe_)qInfo()<<"render gate: input desktop cannot be opened at start-up; relying on session notifications only";
#endif
    timer_.setInterval(500);
    connect(&timer_,&QTimer::timeout,this,&RenderGate::evaluate);
    timer_.start();
}

RenderGate::~RenderGate() {
#ifdef Q_OS_WIN
    if(filter_){
        qApp->removeNativeEventFilter(filter_);
        if(window_)WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(window_->winId()));
        delete filter_;
    }
#endif
    g_closed.store(false);
}

void RenderGate::evaluateNow() { evaluate(); }

void RenderGate::evaluate() {
    QString why;
    static const QString forced=qEnvironmentVariable("XMB_RENDER_GATE_FORCE");
    if(!forced.isEmpty())why=forced;
#ifdef Q_OS_WIN
    if(why.isEmpty() && host::displayState()==0)why=QStringLiteral("display off");
    if(why.isEmpty() && (sessionAway_ || (inputDesktopProbe_ && !inputDesktopReachable())))why=QStringLiteral("session locked or disconnected");
    if(why.isEmpty() && window_ && window_->isVisible()) {
        const HWND handle=reinterpret_cast<HWND>(window_->winId());
        if(IsIconic(handle))why=QStringLiteral("window minimised");
        else if(cloaked(handle))why=QStringLiteral("window cloaked");
        else if(coveredByOthers(handle))why=QStringLiteral("window fully covered");
    }
#endif
    const bool closed=!why.isEmpty();
    if(observeOnly()) {
        static bool observed=false;
        if(closed!=observed){observed=closed;qInfo().noquote()<<QString("render gate: observe only, would be %1 (%2)").arg(closed?"closed":"open").arg(why);}
        return;
    }
    if(closed==g_closed.load())return;
    g_closed.store(closed);
    if(closed) {
        reason_=why;closedAt_=QDateTime::currentMSecsSinceEpoch();
        qInfo().noquote()<<QString("render gate: closed, frame loop stopped (%1)").arg(why);
    } else {
        qInfo().noquote()<<QString("render gate: open again after %1 ms (%2); frame loop restarted")
            .arg(QDateTime::currentMSecsSinceEpoch()-closedAt_).arg(reason_);
        if(view_)view_->update();   // re-arm the frameSwapped -> update chain
    }
}
}
