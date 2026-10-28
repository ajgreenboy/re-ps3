#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QByteArray>
#include <memory>

namespace xmb {

// Original system_plugin.rco cues, optionally replaced by decoded local PS3 theme sounds.
// All nine entries of the RCO sound table are decoded. The native cue map is
// Move=snd_cursor, Confirm=snd_decide, Back=snd_cancel, Options=snd_option,
// Accept=snd_system_ok, Error=snd_system_ng and Trophy=snd_trophy. snd_error is
// decoded and reported but never played: its original PCM is a silent stub.
//
// The firmware applies exactly one gain to every cue: a float 1.0 (unity) read
// from explore_plugin.prx VA 0x272000 (the wrapper loads it with
// `lfs f1, 0x2000(r9)`, r9=0x270000) and the same value in sysconf_plugin.prx VA
// 0x14ab70. No cue carries a per-entry gain or pan in the RCO. The native path
// keeps that unity gain as a named constant and applies the user's XMB volume on
// top of it, so the cue mix is the firmware's and only the user scale is ours.
//
// Construct and call on the GUI thread; all firmware reads finish in construction.
class InteractionAudio final : public QObject {
    Q_OBJECT
public:
    // Six original slots plus Trophy (appended, so existing indices keep their
    // meaning). Trophy is wired only where a trophy notification is shown; the
    // firmware names "snd_trophy" in np_trophy_util.prx's notification setup.
    enum class Cue { Move, Confirm, Back, Error, Options, Accept, Trophy };
    Q_ENUM(Cue)

    // What playAt() decided. Only Played means an output device started, so the
    // decision path is assertable on a machine with no audio hardware.
    enum class Decision { Played, RetriggerSkipped, Disabled, Silent, SourceUnavailable, OutputUnavailable, Rejected };
    Q_ENUM(Decision)

    // playAt() only accepts a timestamp in this window; anything else is
    // Rejected without touching the retrigger history.
    static constexpr qint64 kMaximumTimeMs = qint64(24) * 60 * 60 * 1000;

    explicit InteractionAudio(QString flashRoot, QObject* parent = nullptr);
    ~InteractionAudio() override;
    // play() is playAt(cue, elapsed-ms-since-construction).
    void play(Cue cue);
    // Play this cue as if the clock read `atMs`. Bounded by kMaximumTimeMs and
    // never accepts a negative timestamp. Time is expected to be monotonic; a
    // timestamp before the cue's last play is treated as a retrigger (age 0) so
    // the guard can never be bypassed by moving the clock backwards.
    Decision playAt(Cue cue, qint64 atMs);
    void setEnabled(bool enabled);
    void setVolume(float volume);
    // P3t::sounds(): se_cursor/decide/cancel/optionmenu/system_ok -> PCM WAV.
    // Missing or rejected entries retain system cues; an empty map restores them all.
    int setThemeSounds(const QHash<QString,QByteArray>& sounds);
    // Reports decoded source availability, not speaker/device audibility.
    bool available(Cue cue) const;
    // Stable names for the trace and for a gate that only reads stderr/stdout.
    static const char* cueName(Cue cue);
    static const char* decisionName(Decision decision);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
