#pragma once
// ZF4: XMB_BOOT_PROFILE=1 logs how long each start-up span takes as "boot-profile: <span> N ms" (Qt log, so XMB_LOG=<file> captures it).
// Off by default: the check is one cached environment read and a span costs nothing else. Spans are RAII, may nest and may run on any thread.
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

namespace xmb::bootprof {

inline bool enabled() {
    static const bool on=qEnvironmentVariable("XMB_BOOT_PROFILE")==QLatin1String("1");
    return on;
}

class Span {
public:
    explicit Span(const char* name):name_(name) { if(enabled())timer_.start(); }
    ~Span() { if(enabled())qInfo().noquote()<<QString("boot-profile: %1 %2 ms").arg(QLatin1String(name_)).arg(double(timer_.nsecsElapsed())/1e6,0,'f',1); }
    Span(const Span&)=delete;
    Span& operator=(const Span&)=delete;
private:
    const char* name_;
    QElapsedTimer timer_;
};

// Sequential marks on the main thread: logs the time since the previous mark under the new name (the first call only starts the clock).
inline void mark(const char* name) {
    if(!enabled())return;
    static QElapsedTimer last;
    if(!last.isValid()){last.start();return;}
    qInfo().noquote()<<QString("boot-profile: %1 %2 ms").arg(QLatin1String(name)).arg(double(last.nsecsElapsed())/1e6,0,'f',1);
    last.restart();
}

// Marks for code that runs every frame: only until main reports the first buffer swap.
inline bool& firstFrameDone() { static bool done=false; return done; }
inline void markEarly(const char* name) { if(!firstFrameDone())mark(name); }

} // namespace xmb::bootprof
