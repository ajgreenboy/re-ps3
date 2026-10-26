#pragma once

#include <QString>
#include <QtGlobal>

namespace xmb {

// Native playback-control presentation state. This class does not draw controls
// or operate a media player; the view supplies timestamps and renders State.
class MediaControls final {
public:
    struct State {
        bool visible = false;
        double opacity = 0.0;
    };

    void opened(qint64 nowMs);
    void activity(qint64 nowMs);
    void setPaused(bool paused, qint64 nowMs);
    State state(qint64 nowMs) const;

    static QString formatTime(qint64 milliseconds);
    static qint64 seekTarget(qint64 position, qint64 duration, qint64 delta);

private:
    qint64 monotonicNow(qint64 nowMs) const;

    mutable qint64 lastNowMs_ = 0;
    qint64 lastActivityMs_ = 0;
    mutable bool hasTimestamp_ = false;
    bool opened_ = false;
    bool paused_ = false;
};

} // namespace xmb
