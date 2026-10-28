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

// Every original XMB cue in vsh/resource/system_plugin.rco. The table is
// 0x0600/0x0601, nine entries; each entry carries a 28-byte VAG descriptor and
// no gain or pan field. snd_error is a real entry, but its PCM is silent; see
// audit/INTERACTION-AUDIO.md for the raw bytes.
enum class System : size_t {
    Cursor, Decide, CategoryDecide, Cancel, Option, Error, SystemOk, SystemNg, Trophy, Count
};
constexpr size_t SystemCount = size_t(System::Count);
constexpr std::array<const char*, SystemCount> Labels{
    "snd_cursor", "snd_decide", "snd_category_decide", "snd_cancel", "snd_option",
    "snd_error", "snd_system_ok", "snd_system_ng", "snd_trophy"};
constexpr size_t CueCount = 7;
constexpr std::array<const char*, CueCount> CueNames{
    "Move", "Confirm", "Back", "Error", "Options", "Accept", "Trophy"};
constexpr std::array<const char*, 7> DecisionNames{
    "play", "retrigger-skip", "disabled", "silent", "source-unavailable", "output-unavailable", "rejected"};
// Measured, not fitted: the firmware's playback wrapper takes one float gain
// for every cue and it is 1.0 (0x3F800000) in the local 4.93 modules.
//   explore_plugin.prx VA 0x272000: FUN_001fdf4c does
//     lis r9, 0x27 / lfs f1, 0x2000(r9) / bl <paf sound play>
//   sysconf_plugin.prx VA 0x14ab70: same 1.0 loaded by its own sound wrapper.
// No RCO entry carries a per-cue gain or pan, so the cue mix is the PCM itself.
// The XMB volume preference is a separate user scale on top of this constant.
constexpr float kFirmwareCueGain = 1.0f;
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
    QString source;
    qsizetype sourceBytes = 0;
    qint64 frames = 0;
    double peak = 0;
    double rms = 0;

    qint64 milliseconds() const {
        // Round up: a retrigger guard must never open before the decoded sample
        // has actually finished (39.1 ms would otherwise round down to 39).
        return format.sampleRate() > 0 ? (frames * 1000 + format.sampleRate() - 1) / format.sampleRate() : 0;
    }
};

// Peak and RMS of the interleaved PCM that will actually reach QAudioSink.
// This is an unweighted full-scale measurement (1.0 == 32767), not a listening
// level: the firmware's own sample data carries the per-cue mix.
void measure(Clip& clip) {
    const int channels = clip.format.channelCount();
    const qsizetype values = clip.pcm.size() / qsizetype(sizeof(qint16));
    clip.frames = channels > 0 ? values / channels : 0;
    double peak = 0, sum = 0;
    const auto* pcm = reinterpret_cast<const qint16*>(clip.pcm.constData());
    for (qsizetype i = 0; i < values; ++i) {
        const double value = double(pcm[i]) / 32768.0;
        peak = std::max(peak, std::abs(value));
        sum += value * value;
    }
    clip.peak = peak;
    clip.rms = values > 0 ? std::sqrt(sum / double(values)) : 0;
}

Clip themeWav(const QByteArray& wav) {
    if(wav.size()<44 || wav.size()>8*1024*1024 || !wav.startsWith("RIFF") || wav.mid(8,4)!="WAVE")
        throw std::runtime_error("Theme cue is not a bounded PCM WAV");
    const auto u16=[&](qsizetype p){checkedRange(wav,quint64(p),2);return qFromLittleEndian<quint16>(wav.constData()+p);};
    const auto u32=[&](qsizetype p){checkedRange(wav,quint64(p),4);return qFromLittleEndian<quint32>(wav.constData()+p);};
    const quint64 end=quint64(u32(4))+8;
    if(end>quint64(wav.size()) || end<12)throw std::runtime_error("Theme WAV length is invalid");
    int channels=0,rate=0;qsizetype dataAt=0,dataBytes=0;
    for(quint64 p=12;p<end;) {
        if(end-p<8)throw std::runtime_error("Truncated theme WAV chunk");
        const auto size=u32(qsizetype(p+4));const quint64 payload=p+8;
        if(size>end-payload)throw std::runtime_error("Theme WAV chunk exceeds its RIFF container");
        const auto id=wav.mid(qsizetype(p),4);
        if(id=="fmt ") {
            if(channels || size<16 || u16(qsizetype(payload))!=1 || u16(qsizetype(payload+14))!=16)
                throw std::runtime_error("Theme cue must use signed 16-bit PCM");
            channels=u16(qsizetype(payload+2));const quint32 hz=u32(qsizetype(payload+4));
            if((channels!=1 && channels!=2) || hz<8000 || hz>192000 ||
               u16(qsizetype(payload+12))!=channels*2 || u32(qsizetype(payload+8))!=hz*channels*2)
                throw std::runtime_error("Unsupported theme cue channel layout or sample rate");
            rate=int(hz);
        } else if(id=="data") {
            if(dataAt)throw std::runtime_error("Multiple theme WAV data chunks");
            dataAt=qsizetype(payload);dataBytes=size;
        }
        p=payload+size+(size&1u);
        if(p>end)throw std::runtime_error("Missing theme WAV chunk padding");
    }
    if(!channels || !dataAt || !dataBytes || dataBytes%(channels*2))throw std::runtime_error("Theme cue has no complete PCM frames");
    const qsizetype frames=dataBytes/(channels*2);
    if(frames>qsizetype(rate)*10)throw std::runtime_error("Theme cue exceeds ten seconds");
    Clip clip;clip.format.setSampleRate(48000);clip.format.setChannelCount(2);clip.format.setSampleFormat(QAudioFormat::Int16);
    const qsizetype outFrames=std::max<qsizetype>(1,(frames*48000+rate-1)/rate);
    clip.pcm.resize(outFrames*4);
    const auto sample=[&](qsizetype frame,int channel){return qFromLittleEndian<qint16>(wav.constData()+dataAt+(frame*channels+std::min(channel,channels-1))*2);};
    for(qsizetype i=0;i<outFrames;++i) {
        const double at=double(i)*rate/48000.;const qsizetype first=std::min(qsizetype(at),frames-1),next=std::min(first+1,frames-1);
        const double fraction=at-std::floor(at);
        for(int c=0;c<2;++c) {
            const qint16 value=qint16(std::clamp(std::lround(sample(first,c)*(1-fraction)+sample(next,c)*fraction),-32768L,32767L));
            std::memcpy(clip.pcm.data()+i*4+c*2,&value,2);
        }
    }
    return clip;
}

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
    qsizetype encodedBytes = 0;
    for (int i = 0; i < count; ++i) {
        const auto length = be32(tree, extra + 12 + i * 8), offset = be32(tree, extra + 16 + i * 8);
        // Per-channel offsets are relative to the whole RCO sound-data section,
        // rather than to entryOffset. This matches RCOMage's offset adjustment.
        if (offset < entryOffset || offset - entryOffset > total || length > total - (offset - entryOffset))
            throw std::runtime_error("RCO sound channel exceeds its entry");
        channels[size_t(i)] = decodeVag(file, quint64(soundBase) + offset, length);
        encodedBytes += qsizetype(length);
        if (i && (channels[size_t(i)].rate != channels[0].rate || channels[size_t(i)].samples.size() != channels[0].samples.size()))
            throw std::runtime_error("VAG stereo channels have different rates or sample counts");
    }
    Clip clip;
    clip.format.setSampleRate(channels[0].rate);
    clip.format.setChannelCount(count);
    clip.format.setSampleFormat(QAudioFormat::Int16);
    clip.sourceBytes = encodedBytes;
    clip.pcm.resize(channels[0].samples.size() * count * qsizetype(sizeof(qint16)));
    for (qsizetype frame = 0; frame < channels[0].samples.size(); ++frame)
        for (int channel = 0; channel < count; ++channel) {
            const qint16 sample = channels[size_t(channel)].samples[frame];
            // QAudioSink Int16 uses native-endian PCM.
            std::memcpy(clip.pcm.data() + (frame * count + channel) * sizeof(sample), &sample, sizeof(sample));
        }
    return clip;
}

std::array<Clip, SystemCount> loadClips(const QString& flashRoot) {
    std::array<Clip, SystemCount> clips;
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
        for (size_t cue = 0; cue < SystemCount; ++cue) if (label == Labels[cue]) {
            try {
                clips[cue] = decodeClip(bytes, tree, at + 40, soundBase, soundLength);
                clips[cue].source = QString::fromLatin1(Labels[cue]);
                measure(clips[cue]);
                qInfo().noquote() << "interaction audio: decoded original" << label
                    << clips[cue].format.sampleRate() << "Hz" << clips[cue].format.channelCount()
                    << "channels," << clips[cue].pcm.size() << "PCM bytes from" << clips[cue].sourceBytes
                    << "VAG bytes, peak" << QString::number(clips[cue].peak, 'f', 6)
                    << "rms" << QString::number(clips[cue].rms, 'f', 6);
                if (clips[cue].peak == 0)
                    qInfo().noquote() << "interaction audio: original" << label
                        << "contains genuinely silent PCM; preserving the source entry rather than substituting a cue";
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
    for (size_t cue = 0; cue < SystemCount; ++cue)
        if (clips[cue].pcm.isEmpty()) qWarning() << "interaction audio: original cue unavailable" << Labels[cue];
    return clips;
}

bool loggingEnabled() {
    // Both names accept only "1": XMB_AUDIO_TRACE=0 must not switch tracing on.
    return qEnvironmentVariable("XMB_AUDIO_LOG") == "1" || qEnvironmentVariable("XMB_AUDIO_TRACE") == "1";
}

} // namespace

struct InteractionAudio::Impl {
    std::array<Clip, SystemCount> clips;
    std::array<Clip, CueCount> theme;
    std::array<qint64, CueCount> lastPlay = [] {
        std::array<qint64, CueCount> history{};
        history.fill(-1000000);
        return history;
    }();
    std::unique_ptr<QAudioSink> sink;
    QBuffer buffer;
    QElapsedTimer clock;
    QString device;
    bool enabled = true;
    bool outputWarningLogged = false;
    bool trace = loggingEnabled();
    float volume = 0.6f;

    float gain() const { return std::clamp(volume * kFirmwareCueGain, 0.0f, 1.0f); }

    // Cue-to-source map. snd_error is deliberately not used for Cue::Error: the
    // original entry is a 1.8 ms silent stub, while snd_system_ng is the
    // negative cue in the same table.
    size_t slot(Cue cue) const {
        switch (cue) {
        case Cue::Move: return size_t(System::Cursor);
        case Cue::Confirm: return size_t(System::Decide);
        case Cue::Back: return size_t(System::Cancel);
        case Cue::Error: return size_t(System::SystemNg);
        case Cue::Options: return size_t(System::Option);
        case Cue::Accept: return size_t(System::SystemOk);
        case Cue::Trophy: return size_t(System::Trophy);
        }
        return SystemCount;
    }

    const Clip* selected(Cue cue) const {
        const auto index = size_t(cue);
        if (index < theme.size() && !theme[index].pcm.isEmpty()) return &theme[index];
        auto system = slot(cue);
        if (cue == Cue::Accept && (system >= clips.size() || clips[system].pcm.isEmpty()))
            system = size_t(System::Decide);
        if (cue == Cue::Options && (system >= clips.size() || clips[system].pcm.isEmpty()))
            system = size_t(System::Decide);
        return system < clips.size() && !clips[system].pcm.isEmpty() ? &clips[system] : nullptr;
    }

    void stop() {
        if (sink) sink->stop();
        buffer.close();
    }

    void forgetHistory() {
        lastPlay.fill(-1000000);
    }
};

InteractionAudio::InteractionAudio(QString flashRoot, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    try { impl_->clips = loadClips(flashRoot); }
    catch (const std::exception& error) { qWarning().noquote() << "interaction audio: original resources unavailable:" << error.what(); }
    impl_->clock.start();
    qInfo().noquote() << "interaction audio: firmware cue gain" << QString::number(kFirmwareCueGain, 'f', 6)
        << "(measured: explore_plugin.prx VA 0x272000, sysconf_plugin.prx VA 0x14ab70), user volume"
        << QString::number(impl_->volume, 'f', 6) << "-> sink gain" << QString::number(impl_->gain(), 'f', 6);
}

InteractionAudio::~InteractionAudio() { impl_->stop(); }

bool InteractionAudio::available(Cue cue) const {
    return impl_->selected(cue)!=nullptr;
}

int InteractionAudio::setThemeSounds(const QHash<QString,QByteArray>& sounds) {
    Q_ASSERT(QThread::currentThread()==thread());
    // Role mapping/fallback follows MaXMB xmb.js _soundFiles/_applyThemeSounds
    // and audio.js setThemeSounds (MIT); decoding/output remain native C++.
    constexpr std::array<std::pair<const char*,Cue>,5> roles{{{"se_cursor",Cue::Move},{"se_decide",Cue::Confirm},
        {"se_cancel",Cue::Back},{"se_optionmenu",Cue::Options},{"se_system_ok",Cue::Accept}}};
    std::array<Clip,CueCount> next;int count=0;
    for(const auto& [id,cue]:roles) {
        const auto found=sounds.constFind(QString::fromLatin1(id));if(found==sounds.cend())continue;
        try {
            next[size_t(cue)]=themeWav(*found);
            next[size_t(cue)].source=QStringLiteral("theme:")+QString::fromLatin1(id);
            next[size_t(cue)].sourceBytes=found->size();
            measure(next[size_t(cue)]);
            ++count;
        }
        catch(const std::exception& error){qWarning().noquote()<<"interaction audio: rejected theme cue"<<id<<error.what();}
    }
    impl_->stop();impl_->forgetHistory();impl_->theme=std::move(next);
    qInfo()<<"interaction audio: theme overrides"<<count;
    return count;
}

void InteractionAudio::setEnabled(bool enabled) {
    Q_ASSERT(QThread::currentThread() == thread());
    impl_->enabled = enabled;
    if (!enabled) impl_->stop();
}

void InteractionAudio::setVolume(float volume) {
    Q_ASSERT(QThread::currentThread() == thread());
    impl_->volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
    if (impl_->sink) impl_->sink->setVolume(impl_->gain());
}

const char* InteractionAudio::cueName(Cue cue) {
    const auto index = size_t(cue);
    return index < CueCount ? CueNames[index] : "unknown";
}

const char* InteractionAudio::decisionName(Decision decision) {
    const auto index = size_t(decision);
    return index < DecisionNames.size() ? DecisionNames[index] : "unknown";
}

void InteractionAudio::play(Cue cue) {
    playAt(cue, impl_->clock.elapsed());
}

InteractionAudio::Decision InteractionAudio::playAt(Cue cue, qint64 atMs) {
    Q_ASSERT(QThread::currentThread() == thread());
    auto& state = *impl_;
    const auto index = size_t(cue);
    const char* name = cueName(cue);
    const auto trace = [&](Decision decision, const QString& detail = QString()) {
        if (!state.trace) return;
        QString line = QStringLiteral("cue %1 t=%2ms decision=%3")
                           .arg(QString::fromLatin1(name)).arg(atMs).arg(QString::fromLatin1(decisionName(decision)));
        if (!detail.isEmpty()) line += QLatin1Char(' ') + detail;
        qInfo().noquote() << "interaction audio:" << line;
    };
    // Bounded inputs only. An out-of-range timestamp or a cast made-up enum
    // value is refused before it can touch the retrigger history.
    if (index >= CueCount || atMs < 0 || atMs > kMaximumTimeMs) {
        trace(Decision::Rejected, QStringLiteral("reason=%1")
              .arg(index >= CueCount ? QStringLiteral("invalid-cue") : QStringLiteral("time-out-of-bounds")));
        return Decision::Rejected;
    }
    if (!state.enabled) { trace(Decision::Disabled); return Decision::Disabled; }
    if (state.gain() <= 0) {
        trace(Decision::Silent, QStringLiteral("volume=%1").arg(QString::number(state.gain(), 'f', 6)));
        return Decision::Silent;
    }
    const Clip* clip = state.selected(cue);
    if (!clip) { trace(Decision::SourceUnavailable); return Decision::SourceUnavailable; }
    // The firmware does not stack a second copy of a cue on top of itself.
    // Until this cue's own sample has finished, retriggers are dropped; a
    // different cue still interrupts it. Move additionally has a 45 ms floor
    // because its 39 ms system sample is shorter than the 67 ms repeat interval.
    // The floor is a fit: no firmware debounce value is traced.
    const qint64 guard = cue == Cue::Move ? std::max<qint64>(45, clip->milliseconds())
                                          : std::max<qint64>(1, clip->milliseconds());
    // Time is monotonic; a backwards timestamp reads as age 0 so it can only be
    // suppressed, never used to bypass the guard.
    const qint64 age = std::max<qint64>(0, atMs - state.lastPlay[index]);
    if (age < guard) {
        trace(Decision::RetriggerSkipped,
              QStringLiteral("age=%1ms guard=%2ms source=%3").arg(age).arg(guard).arg(clip->source));
        return Decision::RetriggerSkipped;
    }
    if (!state.sink || state.sink->format() != clip->format) {
        state.stop();
        state.sink.reset();
        state.device.clear();
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(clip->format)) {
            if (!state.outputWarningLogged)
                qWarning() << "interaction audio: output device cannot play original PCM format" << clip->format;
            state.outputWarningLogged = true;
            trace(Decision::OutputUnavailable,
                  QStringLiteral("reason=%1").arg(device.isNull() ? QStringLiteral("no-device")
                                                                  : QStringLiteral("unsupported-format")));
            return Decision::OutputUnavailable;
        }
        state.outputWarningLogged = false;
        state.sink = std::make_unique<QAudioSink>(device, clip->format);
        state.sink->setVolume(state.gain());
        state.device = device.description();
        auto* sink = state.sink.get();
        connect(sink, &QAudioSink::stateChanged, sink, [sink](QtAudio::State) {
            if (sink->error() != QtAudio::NoError && sink->error() != QtAudio::UnderrunError)
                qWarning() << "interaction audio: output failure" << sink->error();
        });
    }
    state.stop();
    state.buffer.setData(clip->pcm);
    if (!state.buffer.open(QIODevice::ReadOnly)) {
        qWarning() << "interaction audio: could not open decoded PCM buffer";
        trace(Decision::Rejected, QStringLiteral("reason=buffer"));
        return Decision::Rejected;
    }
    state.sink->start(&state.buffer);
    state.lastPlay[index] = atMs;
    trace(Decision::Played,
          QStringLiteral("source=%1 rate=%2 channels=%3 bytes=%4 peak=%5 rms=%6 duration=%7ms guard=%8ms output=%9")
              .arg(clip->source).arg(clip->format.sampleRate()).arg(clip->format.channelCount())
              .arg(clip->pcm.size()).arg(QString::number(clip->peak, 'f', 6))
              .arg(QString::number(clip->rms, 'f', 6)).arg(clip->milliseconds()).arg(guard)
              .arg(state.device.isEmpty() ? QStringLiteral("none") : state.device));
    return Decision::Played;
}

}
