#include "interaction_audio.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QThread>
#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace xmb {
namespace {

// Format references (independent bounded implementation):
// https://github.com/kakaroto/RCOMage/blob/master/src/rcofile.h
// https://github.com/kakaroto/RCOMage/blob/master/src/rcoreader.c
// https://github.com/kakaroto/RCOMage/blob/master/src/vaghandler.c
// Channel byte layout and VAG version/header were inspected in local firmware.

constexpr std::array<const char*, 4> Labels{"snd_cursor", "snd_decide", "snd_cancel", "snd_error"};
constexpr qsizetype MaximumFile = 64 * 1024 * 1024;
constexpr quint32 MaximumTables = 8 * 1024 * 1024;

void checkedRange(const QByteArray& data, quint64 at, quint64 length) {
    if (at > quint64(data.size()) || length > quint64(data.size()) - at)
        throw std::runtime_error("Sound resource exceeds its container");
}

quint32 be32(const QByteArray& data, quint64 at) {
    checkedRange(data, at, 4);
    return qFromBigEndian<quint32>(data.constData() + at);
}

quint16 be16(const QByteArray& data, quint64 at) {
    checkedRange(data, at, 2);
    return qFromBigEndian<quint16>(data.constData() + at);
}

QByteArray tables(const QByteArray& file) {
    checkedRange(file, 0, 0xa4);
    if (be32(file, 0) != 0x46525000)
        throw std::runtime_error("Expected a PS3 big-endian RCO sound container");
    const auto compression = be32(file, 12) >> 4;
    if (compression == 0) return file;
    if (compression != 1) throw std::runtime_error("Unsupported RCO sound table compression");
    const auto packed = be32(file, 0xa4), expanded = be32(file, 0xa8);
    if (expanded == 0 || expanded > MaximumTables)
        throw std::runtime_error("RCO sound table expansion exceeds limit");
    checkedRange(file, 0xb0, packed);
    QByteArray framed(4, Qt::Uninitialized);
    qToBigEndian(expanded, framed.data());
    framed.append(file.constData() + 0xb0, packed);
    auto result = qUncompress(framed);
    if (result.size() != expanded) throw std::runtime_error("RCO sound table zlib size mismatch");
    return file.left(0xa4) + result;
}

struct Channel {
    int rate = 0;
    QVector<qint16> samples;
};

Channel decodeVag(const QByteArray& file, quint64 start, quint32 length) {
    checkedRange(file, start, length);
    if (length < 80 || std::memcmp(file.constData() + start, "VAGp", 4) != 0)
        throw std::runtime_error("Sound channel is not a supported VAGp stream");
    // Observed system_plugin.rco: version 6, 64-byte header. Its dataSize is
    // measured from 0x30 and includes the additional 16 reserved header bytes.
    // RCOMage vaghandler.c also advances past a 64-byte VagHeader.
    if (be32(file, start + 4) != 6)
        throw std::runtime_error("Unsupported VAG version (only observed version 6 is supported)");
    const auto dataSize = be32(file, start + 12), rate = be32(file, start + 16);
    if (dataSize != length - 48 || (length - 64) % 16 != 0 || rate < 8000 || rate > 192000)
        throw std::runtime_error("Invalid VAG channel length or sample rate");
    for (quint64 p = start + 48; p < start + 64; ++p)
        if (file[qsizetype(p)] != 0) throw std::runtime_error("Unsupported VAG extended header");
    const auto blocks = (length - 64) / 16;
    if (quint64(blocks) * 28 > quint64(rate) * 10)
        throw std::runtime_error("Interaction sound exceeds ten-second decode limit");
    Channel channel;
    channel.rate = int(rate);
    channel.samples.reserve(qsizetype(blocks) * 28);
    constexpr int coefficients[5][2]{{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    int previous = 0, beforePrevious = 0;
    bool ended = false;
    for (quint32 block = 0; block < blocks; ++block) {
        const auto* bytes = reinterpret_cast<const uchar*>(file.constData() + start + 64 + quint64(block) * 16);
        const int predictor = bytes[0] >> 4, shift = bytes[0] & 15, flags = bytes[1];
        if (flags == 7) { ended = true; break; } // Silent VAG terminator, not sample data.
        if (predictor > 4 || shift > 12 || (flags & ~7) != 0 || (flags & 2) != 0)
            throw std::runtime_error("Unsupported VAG predictor, shift or looping interaction sound");
        for (int index = 0; index < 28; ++index) {
            int nibble = (bytes[2 + index / 2] >> ((index & 1) * 4)) & 15;
            if (nibble >= 8) nibble -= 16;
            const int prediction = (previous * coefficients[predictor][0] + beforePrevious * coefficients[predictor][1] + 32) >> 6;
            const int sample = std::clamp(((nibble * 4096) >> shift) + prediction, -32768, 32767);
            beforePrevious = previous;
            previous = sample;
            channel.samples.push_back(qint16(sample));
        }
        if ((flags & 1) != 0) { ended = true; break; }
    }
    if (!ended || channel.samples.isEmpty()) throw std::runtime_error("VAG interaction sound has no valid ending or samples");
    return channel;
}

struct Clip {
    QAudioFormat format;
    QByteArray pcm;
};

Clip decodeClip(const QByteArray& file, const QByteArray& tree, quint64 extra,
                quint32 soundBase, quint32 soundLength) {
    checkedRange(tree, extra, 28);
    if (be16(tree, extra) != 1) throw std::runtime_error("Unsupported RCO sound codec (expected VAG)");
    // The local PS3 entries store channel count as 02 00 / 01 00. Treat these
    // as the observed byte count plus reserved zero; do not guess other layouts.
    const int count = uchar(tree[qsizetype(extra + 2)]);
    if ((count != 1 && count != 2) || tree[qsizetype(extra + 3)] != 0)
        throw std::runtime_error("Unsupported RCO sound channel layout");
    const auto total = be32(tree, extra + 4), entryOffset = be32(tree, extra + 8);
    if (entryOffset > soundLength || total > soundLength - entryOffset)
        throw std::runtime_error("RCO sound entry exceeds its data section");
    std::array<Channel, 2> channels;
    for (int i = 0; i < count; ++i) {
        const auto length = be32(tree, extra + 12 + i * 8), offset = be32(tree, extra + 16 + i * 8);
        // Per-channel offsets are relative to the whole RCO sound-data section,
        // rather than to entryOffset. This matches RCOMage's offset adjustment.
        if (offset < entryOffset || offset - entryOffset > total || length > total - (offset - entryOffset))
            throw std::runtime_error("RCO sound channel exceeds its entry");
        channels[size_t(i)] = decodeVag(file, quint64(soundBase) + offset, length);
        if (i && (channels[size_t(i)].rate != channels[0].rate || channels[size_t(i)].samples.size() != channels[0].samples.size()))
            throw std::runtime_error("VAG stereo channels have different rates or sample counts");
    }
    Clip clip;
    clip.format.setSampleRate(channels[0].rate);
    clip.format.setChannelCount(count);
    clip.format.setSampleFormat(QAudioFormat::Int16);
    clip.pcm.resize(channels[0].samples.size() * count * qsizetype(sizeof(qint16)));
    for (qsizetype frame = 0; frame < channels[0].samples.size(); ++frame)
        for (int channel = 0; channel < count; ++channel) {
            const qint16 sample = channels[size_t(channel)].samples[frame];
            // QAudioSink Int16 uses native-endian PCM.
            std::memcpy(clip.pcm.data() + (frame * count + channel) * sizeof(sample), &sample, sizeof(sample));
        }
    return clip;
}

std::array<Clip, 4> loadClips(const QString& flashRoot) {
    std::array<Clip, 4> clips;
    QFile file(QDir(flashRoot).filePath("vsh/resource/system_plugin.rco"));
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    if (file.size() < 0xa4 || file.size() > MaximumFile) throw std::runtime_error("RCO sound file exceeds size limits");
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) throw std::runtime_error(file.errorString().toStdString());
    const auto tree = tables(bytes);
    const auto soundRoot = be32(bytes, 0x1c), labels = be32(bytes, 0x40), labelsLength = be32(bytes, 0x44);
    const auto soundBase = be32(bytes, 0x88), soundLength = be32(bytes, 0x8c);
    checkedRange(tree, labels, labelsLength);
    checkedRange(bytes, soundBase, soundLength);
    if (soundRoot == 0xffffffff || be16(tree, soundRoot) != 0x0600)
        throw std::runtime_error("Original RCO sound table is absent or unsupported");
    const auto count = be32(tree, quint64(soundRoot) + 16);
    if (count > 256) throw std::runtime_error("RCO sound entry count exceeds limit");
    quint64 at = quint64(soundRoot) + 40;
    for (quint32 index = 0; index < count; ++index) {
        checkedRange(tree, at, 68);
        if (be16(tree, at) != 0x0601) throw std::runtime_error("Unsupported RCO sound entry type");
        const auto offset = be32(tree, at + 4);
        if (offset >= labelsLength) throw std::runtime_error("RCO sound label is outside its table");
        const auto start = quint64(labels) + offset;
        const auto end = tree.indexOf('\0', qsizetype(start));
        if (end < 0 || quint64(end) >= quint64(labels) + labelsLength || quint64(end) - start > 256)
            throw std::runtime_error("RCO sound label is unterminated or too long");
        const auto label = tree.mid(qsizetype(start), end - qsizetype(start));
        for (size_t cue = 0; cue < Labels.size(); ++cue) if (label == Labels[cue]) {
            try {
                clips[cue] = decodeClip(bytes, tree, at + 40, soundBase, soundLength);
                qInfo().noquote() << "interaction audio: decoded original" << label
                    << clips[cue].format.sampleRate() << "Hz" << clips[cue].format.channelCount()
                    << "channels," << clips[cue].pcm.size() << "PCM bytes";
                if (std::all_of(clips[cue].pcm.cbegin(), clips[cue].pcm.cend(), [](char byte) { return byte == 0; }))
                    qInfo().noquote() << "interaction audio: original" << label << "contains silent PCM; preserving the source";
            } catch (const std::exception& error) {
                qWarning().noquote() << "interaction audio:" << label << error.what();
            }
        }
        if (index + 1 < count) {
            const auto next = be32(tree, at + 20);
            if (next < 68) throw std::runtime_error("Invalid RCO sound sibling offset");
            at += next;
        }
    }
    for (size_t cue = 0; cue < Labels.size(); ++cue)
        if (clips[cue].pcm.isEmpty()) qWarning() << "interaction audio: original cue unavailable" << Labels[cue];
    return clips;
}

}

struct InteractionAudio::Impl {
    std::array<Clip, 4> clips;
    std::unique_ptr<QAudioSink> sink;
    QBuffer buffer;
    QElapsedTimer clock;
    qint64 lastMove = -1000;
    bool enabled = true;
    bool outputWarningLogged = false;
    float volume = 0.6f;

    void stop() {
        if (sink) sink->stop();
        buffer.close();
    }
};

InteractionAudio::InteractionAudio(QString flashRoot, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    try { impl_->clips = loadClips(flashRoot); }
    catch (const std::exception& error) { qWarning().noquote() << "interaction audio: original resources unavailable:" << error.what(); }
    impl_->clock.start();
}

InteractionAudio::~InteractionAudio() { impl_->stop(); }

bool InteractionAudio::available(Cue cue) const {
    const auto index = size_t(cue);
    return index < impl_->clips.size() && !impl_->clips[index].pcm.isEmpty();
}

void InteractionAudio::setEnabled(bool enabled) {
    Q_ASSERT(QThread::currentThread() == thread());
    impl_->enabled = enabled;
    if (!enabled) impl_->stop();
}

void InteractionAudio::setVolume(float volume) {
    Q_ASSERT(QThread::currentThread() == thread());
    impl_->volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
    if (impl_->sink) impl_->sink->setVolume(impl_->volume);
}

void InteractionAudio::play(Cue cue) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!impl_->enabled || impl_->volume <= 0 || !available(cue)) return;
    const auto now = impl_->clock.elapsed();
    if (cue == Cue::Move && now - impl_->lastMove < 45) return;
    if (cue == Cue::Move) impl_->lastMove = now;
    const auto& clip = impl_->clips[size_t(cue)];
    if (!impl_->sink || impl_->sink->format() != clip.format) {
        impl_->stop();
        impl_->sink.reset();
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(clip.format)) {
            if (!impl_->outputWarningLogged)
                qWarning() << "interaction audio: output device cannot play original PCM format" << clip.format;
            impl_->outputWarningLogged = true;
            return;
        }
        impl_->outputWarningLogged = false;
        impl_->sink = std::make_unique<QAudioSink>(device, clip.format);
        impl_->sink->setVolume(impl_->volume);
        auto* sink = impl_->sink.get();
        connect(sink, &QAudioSink::stateChanged, sink, [sink](QtAudio::State) {
            if (sink->error() != QtAudio::NoError && sink->error() != QtAudio::UnderrunError)
                qWarning() << "interaction audio: output failure" << sink->error();
        });
    }
    impl_->stop();
    impl_->buffer.setData(clip.pcm);
    if (!impl_->buffer.open(QIODevice::ReadOnly)) {
        qWarning() << "interaction audio: could not open decoded PCM buffer";
        return;
    }
    impl_->sink->start(&impl_->buffer);
}

}
