#include "media_controls.h"

#include <algorithm>
#include <limits>

namespace xmb {
namespace {
constexpr qint64 ShowDurationMs = 3000;
constexpr qint64 FadeDurationMs = 250;
}

qint64 MediaControls::monotonicNow(qint64 nowMs) const {
    if (!hasTimestamp_) {
        lastNowMs_ = nowMs;
        hasTimestamp_ = true;
    } else if (nowMs > lastNowMs_) {
        lastNowMs_ = nowMs;
    }
    return lastNowMs_;
}

void MediaControls::opened(qint64 nowMs) {
    const qint64 now = monotonicNow(nowMs);
    opened_ = true;
    paused_ = false;
    lastActivityMs_ = now;
}

void MediaControls::activity(qint64 nowMs) {
    const qint64 now = monotonicNow(nowMs);
    opened_ = true;
    lastActivityMs_ = now;
}

void MediaControls::setPaused(bool paused, qint64 nowMs) {
    const qint64 now = monotonicNow(nowMs);
    opened_ = true;
    if (paused_ && !paused)
        lastActivityMs_ = now;
    paused_ = paused;
}

MediaControls::State MediaControls::state(qint64 nowMs) const {
    if (!opened_)
        return {};

    const qint64 now = monotonicNow(nowMs);
    qint64 elapsed = 0;
    if (now > lastActivityMs_) {
        constexpr qint64 Max = std::numeric_limits<qint64>::max();
        elapsed = lastActivityMs_ < 0 && now > Max + lastActivityMs_
            ? Max
            : now - lastActivityMs_;
    }
    if (paused_ || elapsed <= ShowDurationMs)
        return {true, 1.0};

    const qint64 fadeElapsed = elapsed - ShowDurationMs;
    if (fadeElapsed >= FadeDurationMs)
        return {false, 0.0};

    const double opacity = 1.0 - static_cast<double>(fadeElapsed) / FadeDurationMs;
    return {true, std::clamp(opacity, 0.0, 1.0)};
}

QString MediaControls::formatTime(qint64 milliseconds) {
    if (milliseconds < 0)
        return QStringLiteral("--:--");

    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 seconds = totalSeconds % 60;
    const qint64 totalMinutes = totalSeconds / 60;
    const qint64 minutes = totalMinutes % 60;
    const qint64 hours = totalMinutes / 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(totalMinutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

qint64 MediaControls::seekTarget(qint64 position, qint64 duration, qint64 delta) {
    const bool durationKnown = duration >= 0;
    const qint64 upperBound = durationKnown ? duration : std::numeric_limits<qint64>::max();
    const qint64 boundedPosition = std::clamp(position, qint64(0), upperBound);

    if (delta > 0) {
        if (delta > upperBound || boundedPosition > upperBound - delta)
            return upperBound;
    }
    if (delta < 0 && delta < -boundedPosition)
        return 0;
    return boundedPosition + delta;
}

} // namespace xmb
