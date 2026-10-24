#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace xmb {

// Loads only the user's original system_plugin.rco. No generated substitutes.
// Construct and call on the GUI thread; all firmware reads finish in construction.
class InteractionAudio final : public QObject {
    Q_OBJECT
public:
    enum class Cue { Move, Confirm, Back, Error };
    Q_ENUM(Cue)

    explicit InteractionAudio(QString flashRoot, QObject* parent = nullptr);
    ~InteractionAudio() override;
    void play(Cue cue);
    void setEnabled(bool enabled);
    void setVolume(float volume);
    // Reports decoded source availability, not speaker/device audibility.
    bool available(Cue cue) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
