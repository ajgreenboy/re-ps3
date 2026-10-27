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
// Accept=snd_system_ok and Error=snd_system_ng. snd_error is decoded and
// reported but never played: its original PCM is a silent stub.
// Construct and call on the GUI thread; all firmware reads finish in construction.
class InteractionAudio final : public QObject {
    Q_OBJECT
public:
    enum class Cue { Move, Confirm, Back, Error, Options, Accept };
    Q_ENUM(Cue)

    explicit InteractionAudio(QString flashRoot, QObject* parent = nullptr);
    ~InteractionAudio() override;
    void play(Cue cue);
    void setEnabled(bool enabled);
    void setVolume(float volume);
    // P3t::sounds(): se_cursor/decide/cancel/optionmenu/system_ok -> PCM WAV.
    // Missing or rejected entries retain system cues; an empty map restores them all.
    int setThemeSounds(const QHash<QString,QByteArray>& sounds);
    // Reports decoded source availability, not speaker/device audibility.
    bool available(Cue cue) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
