#include "ps_watcher.h"
#include "pad.h"
#include <QDebug>
#include <QFile>
#include <QTimer>

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
