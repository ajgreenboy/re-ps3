#pragma once
// PCM tap for the music page's Visual Player (audit/MUSIC-PLAYER.md "Visualizer"). A QAudioBufferOutput on the music
// QMediaPlayer hands decoded buffers to a worker thread that keeps a 2048-point Hann FFT and a short, bounded
// history of log-spaced band levels. The GUI thread only reads that history; nothing here allocates per frame.
#include <QObject>
#include <QAudioBuffer>
#include <QMutex>
#include <QThread>
#include <array>
#include <atomic>
#include <deque>

class QMediaPlayer;
class QAudioBufferOutput;

namespace xmb {

struct MusicSpectrum {
    static constexpr int bands=40;
    std::array<float,bands> band{};   // 0..1 per log-spaced band, 50 Hz .. 16 kHz
    float rms=0.f;                    // 0..1 loudness of the last hop
    qint64 timeMs=0;                  // media time the window is centred on
    bool valid=false;
};

class MusicTap : public QObject {
    Q_OBJECT
public:
    explicit MusicTap(QMediaPlayer* player,QObject* parent=nullptr);
    ~MusicTap() override;
    // The page calls this every frame it wants levels; the tap analyses only while a request is under 500 ms old.
    void request();
    // The analysed window closest to positionMs (the newest when none is within 400 ms). False when none exists.
    bool sample(qint64 positionMs,MusicSpectrum& out) const;
    // Forget the history (new track, seek).
    void reset();
    // Counters for the performance log.
    quint64 buffersAnalysed() const { return analysed_.load(); }
    quint64 buffersDropped() const { return dropped_.load(); }
private:
    class Worker;
    void push(const MusicSpectrum& spectrum);
    bool wanted() const;
    QMediaPlayer* player_;
    QAudioBufferOutput* output_=nullptr;
    QThread thread_;
    Worker* worker_=nullptr;
    mutable QMutex mutex_;
    std::deque<MusicSpectrum> history_;       // newest last, capped at maxHistory
    std::atomic<qint64> requestedAt_{0};
    std::atomic<int> pending_{0};
    std::atomic<quint64> analysed_{0},dropped_{0};
    static constexpr size_t maxHistory=96;    // ~1.1 s at one window per 512 frames
    friend class Worker;
};

}
