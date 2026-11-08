#include "ps_watcher.h"
#include "pad.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QOpenGLWidget>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace xmb {
namespace {
constexpr int kPollMs=20;
// The hold threshold. The console's hold burst (PARITY-SCORECARD 16.3, 206.6 fps) shows the frame going
// fully black 1052 ms after the press and the entries at 1279..1326 ms, but that is the *menu's*
// appearance, not the firmware's tap/hold decision: the burst starts at the press and nothing local
// shows a press that was released just before the menu began. The 1052 ms black onset is therefore an
// upper bound on the threshold, and this 1000 ms rule is consistent with it - View holds the black frame
// for the remaining 52 ms (psHoldBlackMs), which reproduces the measured onset exactly. No local artifact
// gives a better threshold (out/transitions has the PS-hold only as a native-only endpoint), so the rule
// stays. Do not "fix" this to 1052: that number is the envelope, not the trigger (BEHAVIOUR-GAP-AUDIT A43).
constexpr qint64 kHoldMs=1000;
constexpr qint64 kStaleMs=500;
constexpr quint16 kPsButton=0x400;   // XInput wButtons layout, as pad.cpp reports the PS button

QString logTarget() {
    const QString value=qEnvironmentVariable("XMB_PS_LOG");
    return value.isEmpty() || value=="0" || value=="1"?QString{}:value;
}
}

bool psLoggingEnabled() {
    static const bool enabled=[]{
        const QString value=qEnvironmentVariable("XMB_PS_LOG");
        return !value.isEmpty() && value!="0";
    }();
    return enabled;
}

void psLog(const QString& message) {
    if(!psLoggingEnabled())return;
    qInfo().noquote()<<"ps:"<<message;
    const QString path=logTarget();
    if(path.isEmpty())return;
    QFile file(path);
    if(!file.open(QIODevice::Append|QIODevice::Text))return;
    file.write((message+'\n').toUtf8());
}

PsWatcher::PsWatcher(QObject* parent):QObject(parent) {}

PsWatcher::~PsWatcher()=default;

void PsWatcher::start() {
    if(timer_)return;
    clock_.start();
    timer_=new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(kPollMs);
    connect(timer_,&QTimer::timeout,this,&PsWatcher::poll);
    psLog(QString("watcher started: poll=%1 ms hold=%2 ms (HID reads are independent of window focus)")
        .arg(kPollMs).arg(kHoldMs));
    poll();
    timer_->start();
}

void PsWatcher::replayPress() {
    if(!timer_)return;
    if(simulated_ && down_)return;
    simulated_=true;
    psLog("script: PS press injected");
    poll();
}

void PsWatcher::replayRelease() {
    if(!timer_)return;
    if(!simulated_)return;
    simulated_=false;
    psLog("script: PS release injected");
    poll();
}

void PsWatcher::poll() {
    if(!timer_)return;
    pad::State state;
    const bool sample=pad::latest(state);
    const qint64 now=clock_.elapsed();
    if(sample)lastSampleAt_=now;
    // A missing HID sample is not a release: the controller reader may simply be between reports.
    // Hold the current state for a short grace period, then cancel the press rather than turn a
    // controller dropout into a tap.
    if(down_ && !simulated_ && !sample && lastSampleAt_>=0 && now-lastSampleAt_>kStaleMs) {
        down_=false;holdFired_=false;
        psLog(QString("press cancelled: no HID sample for %1 ms").arg(now-lastSampleAt_));
        emit cancelled();
        return;
    }
    bool pressedNow=simulated_;
    if(sample)pressedNow=pressedNow||(state.buttons&kPsButton)!=0;
    else if(down_)pressedNow=true;
    if(pressedNow && !down_) {
        down_=true;downAt_=now;holdFired_=false;
        psLog(QString("press source=%1 hid-sample=%2 raw-ps=%3")
            .arg(simulated_?"script":"hid").arg(sample?"1":"0")
            .arg(sample?QString::number((state.buttons&kPsButton)?1:0):QStringLiteral("-")));
        emit pressed();
    } else if(pressedNow && down_ && !holdFired_ && now-downAt_>=kHoldMs) {
        holdFired_=true;
        psLog(QString("hold after %1 ms").arg(now-downAt_));
        emit held();
    } else if(!pressedNow && down_) {
        const qint64 duration=now-downAt_;
        const bool wasHold=holdFired_;
        down_=false;holdFired_=false;
        psLog(QString("release after %1 ms%2").arg(duration).arg(wasHold?" (hold)":""));
        if(wasHold)emit released(duration);
        else {psLog(QString("tap after %1 ms").arg(duration));emit tapped(duration);}
    }
}
}

// ---- Launched-title session (PHASES H5) ----
namespace xmb {
namespace {
bool g_renderPaused=false;
qint64 wallMs(){return QDateTime::currentMSecsSinceEpoch();}
#ifdef Q_OS_WIN
QString foregroundName() {
    const HWND window=GetForegroundWindow();
    if(!window)return QStringLiteral("none");
    wchar_t title[128]{};GetWindowTextW(window,title,127);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    return QString("pid=%1 title=\"%2\"").arg(pid).arg(QString::fromWCharArray(title));
}
#else
QString foregroundName(){return QStringLiteral("n/a");}
#endif
}

bool renderPaused(){return g_renderPaused;}

TitleSession::TitleSession(QWidget* window,QWidget* view,Hooks hooks,QObject* parent)
    :QObject(parent),window_(window),view_(view),hooks_(std::move(hooks)) {
    clock_.start();
    poll_=new QTimer(this);poll_->setInterval(250);
    connect(poll_,&QTimer::timeout,this,&TitleSession::evaluate);
    // Foreground changes (a PS tap bringing the XMB forward, Alt-Tab) resume or pause without waiting for the poll.
    connect(qApp,&QGuiApplication::applicationStateChanged,this,[this]{evaluate();});
    connect(qApp,&QGuiApplication::focusWindowChanged,this,[this]{evaluate();});
    if(auto* gl=qobject_cast<QOpenGLWidget*>(view_))connect(gl,&QOpenGLWidget::frameSwapped,this,&TitleSession::onFrame);
}

bool TitleSession::xmbForeground() const {
#ifdef Q_OS_WIN
    return GetForegroundWindow()==reinterpret_cast<HWND>(window_->winId());
#else
    return window_->isActiveWindow();
#endif
}

void TitleSession::onFrame() {
    ++framesTotal_;
    if(active_)++framesSession_;
    if(paused_)++framesWhilePaused_;
}

void TitleSession::evaluate() {
    const bool running=hooks_.titleRunning && hooks_.titleRunning();
    if(running && !active_)begin();
    else if(!running && active_)end();
    else if(active_)updatePause();
}

void TitleSession::begin() {
    active_=true;
    launchAt_=clock_.elapsed();framesSession_=0;framesWhilePaused_=0;pausedMs_=0;
    lastReportAt_=launchAt_;lastReportFrames_=0;
    // windowState() keeps FullScreen while minimised, so a title that minimises the XMB still restores to fullscreen.
    wasFullScreen_=window_->windowState().testFlag(Qt::WindowFullScreen);
    wasMaximized_=window_->windowState().testFlag(Qt::WindowMaximized);
    launchSelection_=hooks_.selection?hooks_.selection():QString{};
    ++restoreGeneration_;   // a restore still retrying from the previous title must not fight this one
    qInfo().noquote()<<QString("title: session begin at=%1 fullscreen=%2 maximized=%3 selection=%4 foreground=%5")
        .arg(wallMs()).arg(wasFullScreen_?1:0).arg(wasMaximized_?1:0).arg(launchSelection_,foregroundName());
    poll_->start();
    updatePause();
}

void TitleSession::updatePause() {
    static const bool disabled=qEnvironmentVariable("XMB_TITLE_NO_PAUSE")==QStringLiteral("1");
    const bool want=active_ && !disabled && !xmbForeground();
    const qint64 now=clock_.elapsed();
    if(want==paused_) {
        // While a title runs, a frame count every ~2 s is the idle evidence (should stay 0 while paused).
        if(active_ && now-lastReportAt_>=2000) {
            qInfo().noquote()<<QString("title: idle frames=%1 in last %2 ms paused=%3 foreground=%4")
                .arg(framesSession_-lastReportFrames_).arg(now-lastReportAt_).arg(paused_?1:0).arg(foregroundName());
            lastReportAt_=now;lastReportFrames_=framesSession_;
        }
        return;
    }
    paused_=want;g_renderPaused=want;
    if(want) {
        pauseStartedAt_=now;
        qInfo().noquote()<<QString("title: render paused (XMB not in front) foreground=%1").arg(foregroundName());
    } else {
        pausedMs_+=now-pauseStartedAt_;
        qInfo().noquote()<<QString("title: render resumed after %1 ms paused (%2)")
            .arg(now-pauseStartedAt_).arg(active_?QStringLiteral("XMB in front while the title runs"):QStringLiteral("title ended"));
        view_->update();   // re-arm the frameSwapped -> update loop
    }
}

void TitleSession::end() {
    const qint64 endedAt=clock_.elapsed();
    const qint64 endedWall=wallMs();
    active_=false;poll_->stop();
    updatePause();   // resumes rendering
    const qint64 sessionMs=endedAt-launchAt_;
    qInfo().noquote()<<QString("title: session end at=%1 duration=%2 ms frames=%3 framesWhilePaused=%4 pausedMs=%5 fps=%6 foreground=%7")
        .arg(endedWall).arg(sessionMs).arg(framesSession_).arg(framesWhilePaused_).arg(pausedMs_)
        .arg(sessionMs>0?QString::number(framesSession_*1000.0/sessionMs,'f',2):QStringLiteral("-")).arg(foregroundName());
    restoreWindow(QStringLiteral("title ended"));
    // Measure the time to foreground and keep re-asserting for a short while: the dead title's windows are
    // torn down asynchronously and Windows may hand the foreground to another window first.
    const int generation=++restoreGeneration_;
    auto* probe=new QTimer(this);probe->setInterval(20);
    auto attempts=std::make_shared<int>(0);
    connect(probe,&QTimer::timeout,this,[this,probe,generation,endedAt,endedWall,attempts]{
        const qint64 elapsed=clock_.elapsed()-endedAt;
        if(generation!=restoreGeneration_ || active_){probe->stop();probe->deleteLater();return;}
        if(xmbForeground() && QApplication::focusWidget()==view_) {
            qInfo().noquote()<<QString("title: XMB foreground+focus after %1 ms (at=%2) fullscreen=%3 wanted=%4 minimized=%5")
                .arg(elapsed).arg(endedWall+elapsed).arg(window_->isFullScreen()?1:0).arg(wasFullScreen_?1:0).arg(window_->isMinimized()?1:0);
            probe->stop();probe->deleteLater();return;
        }
        if(elapsed>=3000) {
            qWarning().noquote()<<QString("title: XMB did not regain the foreground within 3000 ms (foreground=%1)").arg(foregroundName());
            probe->stop();probe->deleteLater();return;
        }
        // Retry the restore at ~150 ms, ~500 ms and ~1500 ms.
        if((elapsed>=150 && *attempts==0) || (elapsed>=500 && *attempts==1) || (elapsed>=1500 && *attempts==2)) {
            ++*attempts;restoreWindow(QString("retry %1 after %2 ms").arg(*attempts).arg(elapsed));
        }
    });
    probe->start();
    const QString now=hooks_.selection?hooks_.selection():QString{};
    qInfo().noquote()<<QString("title: selection at return=%1 launch=%2 kept=%3").arg(now,launchSelection_).arg(now==launchSelection_?1:0);
    // The shell rescans the library on return; check the selection again once that has had time to land.
    QTimer::singleShot(2000,this,[this,generation]{
        if(generation!=restoreGeneration_ || active_)return;
        const QString later=hooks_.selection?hooks_.selection():QString{};
        qInfo().noquote()<<QString("title: selection after rescan=%1 launch=%2 kept=%3").arg(later,launchSelection_).arg(later==launchSelection_?1:0);
    });
}

void TitleSession::restoreWindow(const QString& reason) {
    qInfo().noquote()<<QString("title: restore begin (%1) minimized=%2 fullscreen=%3 wanted=%4 foreground=%5")
        .arg(reason).arg(window_->isMinimized()?1:0).arg(window_->isFullScreen()?1:0).arg(wasFullScreen_?1:0).arg(foregroundName());
    if(window_->isMinimized()) {
        if(wasFullScreen_)window_->showFullScreen();
        else if(wasMaximized_)window_->showMaximized();
        else window_->showNormal();
    }
    if(wasFullScreen_) {
        // A title that changed the display mode can leave the fullscreen window at the old size: re-fit it.
        QScreen* screen=window_->screen();
        if(!window_->isFullScreen())window_->showFullScreen();
        else if(screen && window_->geometry()!=screen->geometry()) {
            qInfo().noquote()<<QString("title: fullscreen geometry %1x%2 does not match the screen %3x%4; re-fitting")
                .arg(window_->width()).arg(window_->height()).arg(screen->geometry().width()).arg(screen->geometry().height());
            window_->setGeometry(screen->geometry());window_->showFullScreen();
        }
    }
    window_->raise();
    window_->activateWindow();
    if(auto* handle=window_->windowHandle())handle->requestActivate();
#ifdef Q_OS_WIN
    const HWND window=reinterpret_cast<HWND>(window_->winId());
    const HWND foreground=GetForegroundWindow();
    if(window && foreground!=window) {
        // Same documented route as View::bringXmbForward: attach to the foreground thread's input queue for the
        // one call. With the title gone there is often no foreground window at all, which SetForegroundWindow
        // accepts without the attach.
        const DWORD foregroundThread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
        const DWORD thisThread=GetCurrentThreadId();
        const bool attached=foregroundThread && foregroundThread!=thisThread && AttachThreadInput(thisThread,foregroundThread,TRUE);
        SetForegroundWindow(window);
        BringWindowToTop(window);
        SetActiveWindow(window);
        if(attached)AttachThreadInput(thisThread,foregroundThread,FALSE);
    }
#endif
    view_->setFocus(Qt::ActiveWindowFocusReason);
    qInfo().noquote()<<QString("title: restore end xmbForeground=%1 foreground=%2").arg(xmbForeground()?1:0).arg(foregroundName());
}
}
