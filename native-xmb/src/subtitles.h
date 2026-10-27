#pragma once
// Sidecar subtitles for the native video player (audit/SUBTITLES.md).
//
// This is not a media-backend feature: it discovers the subtitle files that sit next to the video
// being played, parses SRT and WebVTT into timed cues, and hands the cue that covers the player's
// own position to the view, which draws it through the XMB's GL glyph atlas. Nothing here opens a
// codec, nothing here is written next to the user's media, and no embedded (in-container) subtitle
// stream is claimed: those stay with QMediaPlayer's own subtitle tracks, which this build does not
// switch yet.
//
// Parsing is deliberately tolerant, because a hand-written .srt is usually slightly broken:
//   - the file's bytes are bounded (4 MB) before they are read;
//   - UTF-8, UTF-8 with a BOM, UTF-16LE and UTF-16BE are decoded, CRLF and CR both become LF;
//   - SRT's index lines, WebVTT's WEBVTT/NOTE/STYLE/REGION headers and both timestamp spellings
//     ("00:00:01,000" and "00:00:01.000", hours optional) are accepted;
//   - inline markup (<i>, <b>, <font ...>), ASS override blocks ({\an8}) and the five named
//     entities are handled;
//   - a block without a usable timing line, with end <= start, or with no text is dropped and
//     counted rather than treated as a fatal error, and the file is still usable;
//   - a file with no usable cue at all is reported with a reason and is not offered as a track.
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb::subtitles {
// One cue: the half-open interval [start, end) in milliseconds plus its text. Multi-line cues keep
// their '\n' so the view can draw the file's own line breaks.
struct Cue {
    qint64 start=0;
    qint64 end=0;
    QString text;
};
// One usable sidecar file. `language` is the tag from "<media>.<tag>.srt" and is empty for a bare
// "<media>.srt"; `name` is the label the player panel shows for it.
struct Track {
    QString path;      // the sidecar file itself
    QString name;      // display label: "English", "en.forced", "On (SRT)"
    QString language;  // the tag as written, e.g. "en" or "en.forced"; empty when untagged
    QString format;    // "srt" or "vtt"
    qint64 bytes=0;
    int skipped=0;     // blocks that were dropped (malformed, empty, or end <= start)
    QVector<Cue> cues;
};
// The result of looking beside a media file: what can be used, and what was ignored and why. The
// reasons are one line each so the shell can log them verbatim.
struct Discovery {
    QVector<Track> tracks;   // usable tracks, the untagged one first, then by label
    QStringList rejected;    // "<file>: <reason>"
};
// Looks beside `mediaPath` for "<base>.srt"/"<base>.vtt" and "<base>.<tag>.srt"/".vtt", parses each
// candidate and returns the usable ones. Never throws, never writes, and reads at most 4 MB per file
// from at most 16 candidates.
Discovery discover(const QString& mediaPath);
// 0-based index of the cue covering `positionMs`, or -1 when none does. Overlapping cues resolve to
// the latest one that has started, which is what a player shows when two overlap.
int cueAt(const QVector<Cue>& cues,qint64 positionMs);
// The panel label for a track: a known ISO-639 tag becomes its English name, an unknown tag is shown
// verbatim, and an untagged file is "On (SRT)" / "On (WebVTT)".
QString displayName(const Track& track);
}
