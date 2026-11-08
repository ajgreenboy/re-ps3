#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <functional>
class QTimer;
class QWidget;

namespace xmb {
// Global PS-button watcher. It polls the HID controller layer (pad.cpp) on its own timer, so it keeps
// running while the XMB window is not the active window or is minimised: Windows HID reads are not
// tied to window focus, and no keyboard hook, game-process injection or elevation is involved.
//
// The state machine is the console's: a press that is released before 1000 ms is a tap; a press held
// for 1000 ms is a hold (reported once, while the button is still down), and its later release is not
// also reported as a tap. A controller that stops reporting for 500 ms cancels an unfinished press
// instead of turning a disconnect into a phantom tap.
//
// Scripted verification drives the same machine through replayPress()/replayRelease().
class PsWatcher final : public QObject {
    Q_OBJECT
public:
    explicit PsWatcher(QObject* parent=nullptr);
    ~PsWatcher() override;
    // Starts polling once; called by the view when it is constructed.
    void start();
    // Scripted PS press/release, as if the HID reader had reported the bit (local verification only).
    void replayPress();
    void replayRelease();
    bool down() const { return down_; }
signals:
    void pressed();
    void held();
    void tapped(qint64 durationMs);
    void released(qint64 durationMs);
    void cancelled();
private:
    void poll();
    QTimer* timer_=nullptr;
    QElapsedTimer clock_;
    bool down_=false;
    bool holdFired_=false;
    bool simulated_=false;
    qint64 downAt_=0;
    qint64 lastSampleAt_=-1;
};

// ---- Launched-title session (PHASES H5: launch and return polish) ----
// While a launched title runs and the XMB is not the foreground window, the XMB idles: the view's
// self-perpetuating frame loop (frameSwapped -> update) checks renderPaused() and stops re-arming, so an
// occluded window no longer renders at the swap rate. When the title ends (normal exit, crash, Alt-F4,
// a kill, or Steam reporting the app stopped) the session restores the XMB: un-minimise, re-apply the
// fullscreen state recorded at launch, raise and take the Win32 foreground, give the view keyboard focus,
// resume the frame loop, and log whether the column/row recorded at launch is still the selection.
//
// It knows nothing about Shell: main.cpp supplies "is a title running" and "describe the selection"
// callbacks, so this file stays free of shell.h. Log lines carry the "title:" prefix (Qt log, so
// XMB_LOG=<file> captures them). XMB_TITLE_NO_PAUSE=1 disables only the render pause (A/B measurement).
bool renderPaused();

class TitleSession final : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<bool()> titleRunning;
        std::function<QString()> selection;   // e.g. "game/1/custom:id"; compared at launch and return
    };
    // `window` is the XMB's top-level window, `view` the widget that should own keyboard focus and whose
    // frameSwapped signal is counted (a QOpenGLWidget).
    TitleSession(QWidget* window,QWidget* view,Hooks hooks,QObject* parent=nullptr);
    // Re-evaluate now (connected to Shell::changed by main.cpp); also polled every 250 ms while a title runs.
    void evaluate();
    bool active() const { return active_; }
private:
    void begin();
    void end();
    void updatePause();
    void restoreWindow(const QString& reason);
    bool xmbForeground() const;
    void onFrame();
    QWidget* window_;
    QWidget* view_;
    Hooks hooks_;
    QTimer* poll_=nullptr;
    QElapsedTimer clock_;
    bool active_=false;
    bool paused_=false;
    bool wasFullScreen_=false;
    bool wasMaximized_=false;
    QString launchSelection_;
    qint64 launchAt_=0,pauseStartedAt_=0,pausedMs_=0;
    qint64 framesTotal_=0,framesSession_=0,framesWhilePaused_=0;
    qint64 lastReportAt_=0,lastReportFrames_=0;
    int restoreGeneration_=0;
};

// XMB_PS_LOG=1 turns on the watcher's state-transition log (lines carry the ps: prefix and go through
// Qt's message handler, so XMB_LOG=<file> captures them). When the value is a file path instead, the
// lines are also appended there directly.
bool psLoggingEnabled();
void psLog(const QString& message);
}
