#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
class QTimer;

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

// XMB_PS_LOG=1 turns on the watcher's state-transition log (lines carry the ps: prefix and go through
// Qt's message handler, so XMB_LOG=<file> captures them). When the value is a file path instead, the
// lines are also appended there directly.
bool psLoggingEnabled();
void psLog(const QString& message);
}
