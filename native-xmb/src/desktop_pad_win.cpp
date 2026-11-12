// Delivery half of the controller-driven desktop (desktop_pad.h): pad reading, gates, SendInput, the on-screen keyboard,
// and the two verification helpers (a sink window that logs what it receives, and a guarded injector).
#include "desktop_pad.h"
#include "pad.h"
#include "ps_watcher.h"
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace xmb::desktoppad {
#ifdef Q_OS_WIN
namespace {
qint64 monotonicMs() {
    static QElapsedTimer clock=[]{QElapsedTimer t;t.start();return t;}();
    return clock.elapsed();
}

// ---- SendInput -------------------------------------------------------------------------------------------
void mouseAbsolute(INPUT& in,int x,int y) {
    const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw=std::max(1,GetSystemMetrics(SM_CXVIRTUALSCREEN)),vh=std::max(1,GetSystemMetrics(SM_CYVIRTUALSCREEN));
    x=std::clamp(x,vx,vx+vw-1);y=std::clamp(y,vy,vy+vh-1);
    // Absolute coordinates bypass "Enhance pointer precision", so the pointer moves exactly as many pixels as the mapper
    // asked for. The centre of the target pixel is used so the rounding lands on it.
    in.type=INPUT_MOUSE;
    in.mi.dx=LONG((qint64(x-vx)*65536+32768)/vw);
    in.mi.dy=LONG((qint64(y-vy)*65536+32768)/vh);
    in.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
}

// Builds the INPUT list for the actions (ToggleKeyboard excluded) and sends it in order. Returns how many events Windows took.
int sendActions(const std::vector<Action>& actions,qint64& moved) {
    std::vector<INPUT> inputs;
    POINT cursor{0,0};
    GetCursorPos(&cursor);
    for(const auto& a:actions) {
        INPUT in{};
        switch(a.kind) {
        case Kind::Move:
            cursor.x+=a.dx;cursor.y+=a.dy;
            mouseAbsolute(in,cursor.x,cursor.y);
            moved+=std::abs(a.dx)+std::abs(a.dy);
            break;
        case Kind::Button:
            in.type=INPUT_MOUSE;
            in.mi.dwFlags=a.button==0?(a.down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP):(a.down?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_RIGHTUP);
            break;
        case Kind::Wheel: {
            if(a.wheelV) {
                in.type=INPUT_MOUSE;in.mi.dwFlags=MOUSEEVENTF_WHEEL;in.mi.mouseData=DWORD(a.wheelV);
                inputs.push_back(in);in={};
            }
            if(a.wheelH) {
                in.type=INPUT_MOUSE;in.mi.dwFlags=MOUSEEVENTF_HWHEEL;in.mi.mouseData=DWORD(a.wheelH);
            } else continue;
            break;
        }
        case Kind::Key:
            in.type=INPUT_KEYBOARD;
            in.ki.wVk=WORD(a.vk);
            in.ki.wScan=WORD(MapVirtualKeyW(UINT(a.vk),MAPVK_VK_TO_VSC));
            in.ki.dwFlags=(a.down?0:KEYEVENTF_KEYUP)|(a.extended?KEYEVENTF_EXTENDEDKEY:0);
            break;
        case Kind::ToggleKeyboard:continue;
        }
        inputs.push_back(in);
    }
    if(inputs.empty())return 0;
    return int(SendInput(UINT(inputs.size()),inputs.data(),sizeof(INPUT)));
}

DWORD windowPid(HWND hwnd) {
    DWORD pid=0;
    if(hwnd)GetWindowThreadProcessId(hwnd,&pid);
    return pid;
}

// ---- on-screen keyboard -----------------------------------------------------------------------------------
// The touch keyboard's COM entry point (the same one the taskbar button uses). Undocumented but stable since Windows 8.
struct ITipInvocation : IUnknown { virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd)=0; };
const CLSID kUIHostNoLaunch={0x4CE576FA,0x83DC,0x4F88,{0x95,0x1C,0x9D,0x07,0x82,0xB4,0xE3,0x76}};
const IID kITipInvocation={0x37C994E7,0x432B,0x4834,{0xA2,0xF7,0xDC,0xE1,0xF1,0x3B,0x83,0x4B}};

bool toggleTouchKeyboard() {
    ITipInvocation* tip=nullptr;
    const HRESULT created=CoCreateInstance(kUIHostNoLaunch,nullptr,CLSCTX_INPROC_HANDLER|CLSCTX_LOCAL_SERVER,kITipInvocation,reinterpret_cast<void**>(&tip));
    if(FAILED(created)||!tip){qInfo().noquote()<<QString("desktop-pad: ITipInvocation not available (0x%1)").arg(quint32(created),0,16);return false;}
    const HRESULT result=tip->Toggle(GetDesktopWindow());
    tip->Release();
    qInfo().noquote()<<QString("desktop-pad: ITipInvocation::Toggle -> 0x%1").arg(quint32(result),0,16);
    return SUCCEEDED(result);
}
bool visibleWindowOfClass(const wchar_t* className,DWORD* pid=nullptr) {
    HWND window=FindWindowW(className,nullptr);
    while(window) {
        RECT r{};GetWindowRect(window,&r);
        int cloaked=0;
        // DWMWA_CLOAKED (14): a Store-style window can be "visible" yet cloaked.
        using DwmGet=HRESULT(WINAPI*)(HWND,DWORD,PVOID,DWORD);
        static const auto dwmGet=reinterpret_cast<DwmGet>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"),"DwmGetWindowAttribute"));
        if(dwmGet)dwmGet(window,14,&cloaked,sizeof(cloaked));
        if(IsWindowVisible(window) && !cloaked && r.right-r.left>40 && r.bottom-r.top>20){if(pid)*pid=windowPid(window);return true;}
        window=FindWindowExW(nullptr,window,className,nullptr);
    }
    return false;
}
bool touchKeyboardVisible() { return visibleWindowOfClass(L"IPTip_Main_Window"); }

struct OskProcess { HANDLE handle=nullptr; DWORD pid=0; };
OskProcess* startOsk() {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system,MAX_PATH);
    std::wstring path=std::wstring(system)+L"\\osk.exe";
    // osk.exe carries a manifest that makes CreateProcess fail with ERROR_ELEVATION_REQUIRED (740); the shell starts it the way
    // the Start menu does. SEE_MASK_NOCLOSEPROCESS gives back the handle, so the process this driver started is the only one it closes.
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
    info.lpVerb=L"open";
    info.lpFile=path.c_str();
    info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info) || !info.hProcess){
        qWarning().noquote()<<QString("desktop-pad: starting osk.exe failed, error %1").arg(GetLastError());
        return nullptr;
    }
    return new OskProcess{info.hProcess,GetProcessId(info.hProcess)};
}
// Closes the osk.exe this driver started, and only that one: WM_CLOSE to its own windows, then TerminateProcess on the
// handle it was created with if it has not gone within a second.
void closeOsk(OskProcess* osk) {
    struct Find { DWORD pid; std::vector<HWND> windows; } find{osk->pid,{}};
    EnumWindows([](HWND hwnd,LPARAM param)->BOOL{
        auto* f=reinterpret_cast<Find*>(param);
        if(windowPid(hwnd)==f->pid)f->windows.push_back(hwnd);
        return TRUE;
    },reinterpret_cast<LPARAM>(&find));
    for(HWND w:find.windows)PostMessageW(w,WM_CLOSE,0,0);
    if(WaitForSingleObject(osk->handle,1000)!=WAIT_OBJECT_0) {
        qInfo().noquote()<<QString("desktop-pad: osk.exe pid %1 did not close; terminating that process").arg(osk->pid);
        TerminateProcess(osk->handle,0);
        WaitForSingleObject(osk->handle,1000);
    }
    CloseHandle(osk->handle);
    qInfo().noquote()<<QString("desktop-pad: closed osk.exe pid %1 (started by the XMB)").arg(osk->pid);
    delete osk;
}
bool oskAlive(OskProcess* osk) { return osk && WaitForSingleObject(osk->handle,0)==WAIT_TIMEOUT; }

bool processRunning(const wchar_t* exeName) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    bool found=false;
    for(BOOL ok=Process32FirstW(snapshot,&entry);ok;ok=Process32NextW(snapshot,&entry))
        if(_wcsicmp(entry.szExeFile,exeName)==0){found=true;break;}
    CloseHandle(snapshot);
    return found;
}
}

DesktopPadDriver::DesktopPadDriver(Hooks hooks,QObject* parent):QObject(parent),hooks_(std::move(hooks)) {
    timer_=new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(10);
    connect(timer_,&QTimer::timeout,this,&DesktopPadDriver::tick);
}
DesktopPadDriver::~DesktopPadDriver() {
    setActive(false);
    if(oskProcess_){auto* osk=static_cast<OskProcess*>(oskProcess_);oskProcess_=nullptr;if(oskAlive(osk))closeOsk(osk);else{CloseHandle(osk->handle);delete osk;}}
}

void DesktopPadDriver::setActive(bool on) {
    if(on==active_)return;
    active_=on;
    if(on) {
        activeSince_=monotonicMs();injected_=0;
        lastGateAt_=lastYieldAt_=-1000;conflictLogged_=false;primed_=false;
        reason_=QStringLiteral("starting");
        timer_->start();
        qInfo().noquote()<<"desktop-pad: active (left stick pointer, right stick scroll; gates: preference, XMB hidden, no title running, no fullscreen game)";
    } else {
        timer_->stop();
        // Release before the XMB reappears, so nothing stays pressed on the desktop (a held Cross, Alt from the switcher).
        deliver(mapper_.releaseAll(monotonicMs()));
        qInfo().noquote()<<QString("desktop-pad: inactive after %1 ms, %2 events delivered").arg(monotonicMs()-activeSince_).arg(injected_);
        reason_.clear();
    }
}

void DesktopPadDriver::deliver(const std::vector<Action>& actions) {
    if(actions.empty())return;
    qint64 moved=0;
    const int sent=sendActions(actions,moved);
    injected_+=sent;
    for(const auto& a:actions) {
        if(a.kind==Kind::Move || a.kind==Kind::Wheel)continue;   // too frequent to log
        if(a.kind==Kind::Key && a.repeat)continue;
        qInfo().noquote()<<QString("desktop-pad: %1").arg(a.why.isEmpty()?QStringLiteral("input"):a.why);
        if(a.kind==Kind::ToggleKeyboard)toggleKeyboard();
    }
    if(sent==0 && std::any_of(actions.begin(),actions.end(),[](const Action& a){return a.kind!=Kind::ToggleKeyboard;}))
        qWarning().noquote()<<QString("desktop-pad: SendInput delivered nothing (error %1); the foreground window may be elevated").arg(GetLastError());
}

bool DesktopPadDriver::yieldToFullscreenApp() {
    // A fullscreen game owns the pad. SHQueryUserNotificationState is the check Windows itself uses to hold back
    // notifications: QUNS_RUNNING_D3D_FULL_SCREEN (3) is an exclusive-fullscreen Direct3D app, QUNS_PRESENTATION_MODE (4) a
    // presentation. A fullscreen video in a browser (QUNS_BUSY) is not a game and keeps the pad.
    QUERY_USER_NOTIFICATION_STATE state=QUNS_ACCEPTS_NOTIFICATIONS;
    if(SUCCEEDED(SHQueryUserNotificationState(&state)) && (state==QUNS_RUNNING_D3D_FULL_SCREEN || state==QUNS_PRESENTATION_MODE)) {
        yieldDetail_=QStringLiteral("a fullscreen game is running");
        return true;
    }
    // Steam's Big Picture / desktop controller configuration: Steam Input maps the pad itself there.
    struct Search { bool found=false; } search;
    EnumWindows([](HWND hwnd,LPARAM param)->BOOL{
        if(!IsWindowVisible(hwnd))return TRUE;
        wchar_t title[64]{};
        GetWindowTextW(hwnd,title,64);
        if(wcsncmp(title,L"Steam Big Picture",17)==0){reinterpret_cast<Search*>(param)->found=true;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    if(search.found){yieldDetail_=QStringLiteral("Steam Big Picture is open");return true;}
    return false;
}

void DesktopPadDriver::checkConflicts() {
    if(conflictLogged_)return;
    conflictLogged_=true;
    if(processRunning(L"DS4Windows.exe"))
        qWarning().noquote()<<"desktop-pad: DS4Windows is running; if it maps this pad to the mouse and keyboard as well, each action happens twice. Turn off Settings > Power Saving > Desktop Mode - Controller Mouse, or its mapping (audit/DESKTOP-MODE.md)";
    if(processRunning(L"steam.exe"))
        qInfo().noquote()<<"desktop-pad: Steam is running; if its Steam Input desktop configuration is enabled for this pad it maps the pad too. Steam Big Picture is detected and the driver steps aside there, the desktop configuration cannot be detected (audit/DESKTOP-MODE.md)";
}

QString DesktopPadDriver::gate() {
    const qint64 now=monotonicMs();
    if(now-lastGateAt_<100)return gateCache_;
    lastGateAt_=now;
    QString reason;
    if(!hooks_.enabled())reason=QStringLiteral("off (desktop/controllerMouse)");
    else if(hooks_.xmbVisible())reason=QStringLiteral("the XMB is visible");
    else if(hooks_.blocked())reason=QStringLiteral("a title or web app is running");
    else {
        if(now-lastYieldAt_>=500){lastYieldAt_=now;yieldNow_=yieldToFullscreenApp();}
        if(yieldNow_)reason=yieldDetail_;
        mapper_.setConfig([&]{Config c=mapper_.config();c.pointerSpeed=speedMultiplier(hooks_.pointerSpeed());return c;}());
    }
    gateCache_=reason;
    return reason;
}

void DesktopPadDriver::tick() {
    const qint64 now=monotonicMs();
    Sample sample;
    pad::State hid;pad::XInputState x;
    const bool haveHid=pad::latest(hid),haveX=pad::xinputLatest(x);
    if(haveHid||haveX) {
        sample.buttons=quint16((haveHid?hid.buttons:0)|(haveX?x.buttons:0));
        const auto stronger=[](float ax,float ay,float bx,float by,float& ox,float& oy){
            if(std::max(std::abs(bx),std::abs(by))>std::max(std::abs(ax),std::abs(ay))){ox=bx;oy=by;} else {ox=ax;oy=ay;}
        };
        stronger(haveHid?hid.lx:0,haveHid?hid.ly:0,haveX?x.lx:0,haveX?x.ly:0,sample.lx,sample.ly);
        stronger(haveHid?hid.rx:0,haveHid?hid.ry:0,haveX?x.rx:0,haveX?x.ry:0,sample.rx,sample.ry);
    }
    const QString reason=gate();
    if(reason!=reason_) {
        reason_=reason;
        qInfo().noquote()<<(reason.isEmpty()?QStringLiteral("desktop-pad: live"):QString("desktop-pad: paused, %1").arg(reason));
        emit idleReasonChanged(reason);
        primed_=false;
    }
    if(!reason.isEmpty()) {
        deliver(mapper_.suspend(sample,now));
        return;
    }
    if(!primed_) {
        // First live tick (Desktop Mode just entered, or a gate just opened): anything held on the pad is ignored until released.
        primed_=true;
        deliver(mapper_.suspend(sample,now));
        checkConflicts();
        return;
    }
    deliver(mapper_.update(sample,now));
}

quint64 DesktopPadDriver::oskPid() const { return oskProcess_?static_cast<OskProcess*>(oskProcess_)->pid:0; }

void DesktopPadDriver::toggleKeyboard() {
    auto* osk=static_cast<OskProcess*>(oskProcess_);
    if(osk) {
        oskProcess_=nullptr;
        if(oskAlive(osk)){closeOsk(osk);return;}
        CloseHandle(osk->handle);delete osk;   // it was closed by hand; this press opens it again below
    }
    if(touchKeyboardVisible()) {
        qInfo()<<"desktop-pad: touch keyboard is showing; toggling it closed";
        toggleTouchKeyboard();
        return;
    }
    const bool asked=toggleTouchKeyboard();
    // The toggle can succeed and show nothing (no touch keyboard on this edition or setting); osk.exe is the fallback.
    QTimer::singleShot(asked?800:0,this,[this,asked]{
        if(asked && touchKeyboardVisible()){qInfo()<<"desktop-pad: touch keyboard shown";return;}
        auto* started=startOsk();
        if(!started){qWarning()<<"desktop-pad: osk.exe could not be started";return;}
        oskProcess_=started;
        qInfo().noquote()<<QString("desktop-pad: started osk.exe pid %1 (%2)").arg(started->pid).arg(asked?"the touch keyboard did not appear":"touch keyboard unavailable");
    });
}

// ---- verification: the sink window -----------------------------------------------------------------------------
namespace {
// Qt reports a horizontal wheel with the opposite sign to Windows (WM_MOUSEHWHEEL: right is positive); the sink logs the Windows sign.
class SinkWindow final : public QWidget {
public:
    SinkWindow(const QString& logFile):log_(logFile) {
        setWindowTitle(QStringLiteral("xmb desktop-pad sink"));
        setWindowFlag(Qt::WindowStaysOnTopHint,true);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_AlwaysShowToolTips,false);
        resize(640,400);
        clock_.start();
        if(!log_.open(QIODevice::WriteOnly|QIODevice::Truncate))qWarning()<<"desktop pad sink: cannot open the log:"<<log_.errorString();
    }
    void write(QJsonObject o) {
        o.insert("ms",double(clock_.elapsed()));
        log_.write(QJsonDocument(o).toJson(QJsonDocument::Compact)+"\n");
        log_.flush();
    }
    QRect clientGlobal() const { return QRect(mapToGlobal(QPoint(0,0)),size()); }
protected:
    void mouseMoveEvent(QMouseEvent* e) override { write({{"e","move"},{"x",e->globalPosition().toPoint().x()},{"y",e->globalPosition().toPoint().y()}}); }
    void mousePressEvent(QMouseEvent* e) override { button("press",e); }
    void mouseReleaseEvent(QMouseEvent* e) override { button("release",e); }
    void wheelEvent(QWheelEvent* e) override {
        write({{"e","wheel"},{"dx",-e->angleDelta().x()},{"dy",e->angleDelta().y()},{"x",e->globalPosition().toPoint().x()},{"y",e->globalPosition().toPoint().y()}});
    }
    void keyPressEvent(QKeyEvent* e) override { keyEvent("keydown",e); }
    void keyReleaseEvent(QKeyEvent* e) override { keyEvent("keyup",e); }
    bool focusNextPrevChild(bool) override { return false; }   // Tab/arrows are events to log, not focus moves
private:
    void button(const char* kind,QMouseEvent* e) {
        write({{"e",kind},{"btn",e->button()==Qt::LeftButton?"left":e->button()==Qt::RightButton?"right":"other"},
            {"x",e->globalPosition().toPoint().x()},{"y",e->globalPosition().toPoint().y()}});
    }
    void keyEvent(const char* kind,QKeyEvent* e) {
        write({{"e",kind},{"vk",int(e->nativeVirtualKey())},{"repeat",e->isAutoRepeat()}});
    }
    QFile log_;
    QElapsedTimer clock_;
};
}

int runPadSink(const QString& logFile,int seconds) {
    SinkWindow window(logFile);
    window.move(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(120,120));
    window.show();
    window.raise();
    window.activateWindow();
    bringWindowForward(&window,&window);
    const auto ready=[&]{
        const QRect r=window.clientGlobal();
        window.write({{"e","ready"},{"pid",double(GetCurrentProcessId())},{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()},
            {"foreground",GetForegroundWindow()==reinterpret_cast<HWND>(window.winId())}});
    };
    QTimer wait;wait.setInterval(100);
    int tries=0;
    QObject::connect(&wait,&QTimer::timeout,&window,[&]{
        const bool front=GetForegroundWindow()==reinterpret_cast<HWND>(window.winId());
        if(front || ++tries>=30){wait.stop();ready();}
        else bringWindowForward(&window,&window);
    });
    wait.start();
    QTimer::singleShot(std::max(1,seconds)*1000,&window,[]{QApplication::quit();});
    return QApplication::exec();
}

// ---- verification: the guarded injector -------------------------------------------------------------------------
int runPadInject(const QString& input,const QString& output) {
    QFile in(input);
    if(!in.open(QIODevice::ReadOnly)){qCritical().noquote()<<"desktop-pad-inject: cannot read"<<input;return 2;}
    const auto root=QJsonDocument::fromJson(in.readAll()).object();
    const DWORD sinkPid=DWORD(root.value("sinkPid").toDouble());
    if(!sinkPid){qCritical()<<"desktop-pad-inject: sinkPid is required (the injector only ever acts on a sink window)";return 2;}
    const Config config=configFromJson(root.value("config").toObject());
    const int tick=std::max(1,root.value("tickMs").toInt(10));
    struct Timed { qint64 t; Sample s; };
    std::vector<Timed> samples;
    for(const auto& v:root.value("samples").toArray()) {
        const auto o=v.toObject();
        samples.push_back({qint64(o.value("t").toDouble()),Sample{quint16(o.value("buttons").toInt()),float(o.value("lx").toDouble()),
            float(o.value("ly").toDouble()),float(o.value("rx").toDouble()),float(o.value("ry").toDouble())}});
    }
    std::stable_sort(samples.begin(),samples.end(),[](const Timed& a,const Timed& b){return a.t<b.t;});
    const qint64 end=qint64(root.value("endMs").toDouble(samples.empty()?0:double(samples.back().t)));

    // Every delivery is checked: the foreground window and the window under the pointer must both belong to the sink process.
    const auto ownedBySink=[&](QString* why){
        if(windowPid(GetForegroundWindow())!=sinkPid){if(why)*why=QStringLiteral("the foreground window is not the sink");return false;}
        POINT p{};GetCursorPos(&p);
        HWND under=WindowFromPoint(p);
        if(windowPid(under)!=sinkPid){if(why)*why=QStringLiteral("the window under the pointer is not the sink");return false;}
        return true;
    };
    if(root.contains("startPointer")) {
        const auto sp=root.value("startPointer").toArray();
        const POINT target{sp.at(0).toInt(),sp.at(1).toInt()};
        if(windowPid(WindowFromPoint(target))!=sinkPid){qCritical()<<"desktop-pad-inject: the start point is not over the sink window; not moving the pointer";return 3;}
        SetCursorPos(target.x,target.y);
    }
    POINT startCursor{};GetCursorPos(&startCursor);
    Mapper mapper(config);
    QJsonObject counts{{"move",0},{"button",0},{"wheel",0},{"key",0},{"toggleKeyboard",0}};
    qint64 moveSum[2]{0,0},wheelSum[2]{0,0};
    QString abortReason;
    QElapsedTimer clock;clock.start();
    size_t next=0;Sample current;
    qint64 injected=0;
    POINT expectedCursor=startCursor;
    int interference=0;   // times the pointer was somewhere other than where this injector left it (a person or another program moved it)
    const auto run=[&](const std::vector<Action>& actions)->bool{
        if(actions.empty())return true;
        QString why;
        POINT seen{};GetCursorPos(&seen);
        if(seen.x!=expectedCursor.x || seen.y!=expectedCursor.y){++interference;expectedCursor=seen;}
        if(!ownedBySink(&why)){abortReason=why;return false;}
        for(const auto& a:actions) {
            const char* name=a.kind==Kind::Move?"move":a.kind==Kind::Button?"button":a.kind==Kind::Wheel?"wheel":a.kind==Kind::Key?"key":"toggleKeyboard";
            counts[name]=counts[name].toInt()+1;
            if(a.kind==Kind::Move){moveSum[0]+=a.dx;moveSum[1]+=a.dy;expectedCursor.x+=a.dx;expectedCursor.y+=a.dy;}
            if(a.kind==Kind::Wheel){wheelSum[0]+=a.wheelH;wheelSum[1]+=a.wheelV;}
        }
        qint64 moved=0;
        injected+=sendActions(actions,moved);
        return true;
    };
    for(qint64 t=0;t<=end && abortReason.isEmpty();t+=tick) {
        while(clock.elapsed()<t)QThread::msleep(1);
        while(next<samples.size() && samples[next].t<=t)current=samples[next++].s;
        if(!run(mapper.update(current,t)))break;
    }
    if(abortReason.isEmpty())run(mapper.releaseAll(end));
    QThread::msleep(200);   // let the sink drain its queue
    POINT endCursor{};GetCursorPos(&endCursor);
    QJsonObject out{{"aborted",!abortReason.isEmpty()},{"abortReason",abortReason},{"counts",counts},{"interference",interference},{"sendInputAccepted",double(injected)},
        {"mapperMoveSum",QJsonArray{double(moveSum[0]),double(moveSum[1])}},{"mapperWheelSum",QJsonArray{double(wheelSum[0]),double(wheelSum[1])}},
        {"cursorStart",QJsonArray{int(startCursor.x),int(startCursor.y)}},{"cursorEnd",QJsonArray{int(endCursor.x),int(endCursor.y)}}};
    QFile file(output);
    const auto bytes=QJsonDocument(out).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
    qInfo().noquote()<<QString("desktop-pad-inject: %1, %2 events accepted").arg(abortReason.isEmpty()?QStringLiteral("completed"):QString("aborted (%1)").arg(abortReason)).arg(injected);
    return abortReason.isEmpty()?0:3;
}

int runKeyboardCheck(const QString& output) {
    // Opens the on-screen keyboard once and closes it again, reporting which one appeared. Uses the driver's own toggle, so an
    // osk.exe is only ever closed if the driver started it. The touch keyboard is closed by toggling it back.
    DesktopPadDriver driver({[]{return true;},[]{return 3;},[]{return false;},[]{return false;}});
    QJsonObject out;
    const auto state=[&](const QString& label){
        DWORD tipPid=0;
        const bool tip=visibleWindowOfClass(L"IPTip_Main_Window",&tipPid);
        const bool osk=visibleWindowOfClass(L"OSKMainClass");
        out.insert(label,QJsonObject{{"touchKeyboardVisible",tip},{"oskWindowVisible",osk},{"oskStartedByDriver",driver.oskPid()!=0},{"oskPid",double(driver.oskPid())}});
    };
    state("before");
    driver.toggleKeyboard();
    QEventLoop first;QTimer::singleShot(2500,&first,&QEventLoop::quit);first.exec();
    state("afterFirstPress");
    driver.toggleKeyboard();
    QEventLoop second;QTimer::singleShot(2000,&second,&QEventLoop::quit);second.exec();
    state("afterSecondPress");
    QFile file(output);
    const auto bytes=QJsonDocument(out).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size())return 2;
    return 0;
}
#else
DesktopPadDriver::DesktopPadDriver(Hooks hooks,QObject* parent):QObject(parent),hooks_(std::move(hooks)) {}
DesktopPadDriver::~DesktopPadDriver() = default;
void DesktopPadDriver::setActive(bool) {}
quint64 DesktopPadDriver::oskPid() const { return 0; }
void DesktopPadDriver::toggleKeyboard() {}
void DesktopPadDriver::deliver(const std::vector<Action>&) {}
int runPadSink(const QString&,int) { return 2; }
int runPadInject(const QString&,const QString&) { return 2; }
int runKeyboardCheck(const QString&) { return 2; }
#endif
}
