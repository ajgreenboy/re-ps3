#pragma once
// Image-based subtitles for the native video player: Blu-ray PGS (.sup) and DVD VobSub (.idx + .sub).
//
// Text subtitles (SRT/WebVTT, subtitles.h) are drawn through the XMB's glyph atlas. A PGS or VobSub
// stream carries no text at all: every cue is a palettised bitmap with a position on the video frame,
// so the decoder's job is to turn the stream into a timeline of ready-made pictures. Nothing here
// opens a codec or touches the network or the disk; the caller hands in bytes (a Jellyfin stream
// fetched with its original codec, or a sidecar file) and gets the cues back.
//
// What is decoded:
//   - PGS: PCS/WDS/PDS/ODS/END segments, fragmented objects, object cropping, window clipping,
//     epoch starts, acquisition points and palette-only updates. Palettes are BT.709 for HD streams
//     (height > 576) and BT.601 for SD, in studio ("limited", 16-235/16-240) range as the BD spec
//     defines it; values outside that range are clamped rather than wrapped. A cue ends at the next
//     display set that changes the picture, or at an empty composition (the stream's "clear").
//   - VobSub: the .idx (size, org, palette, custom colours, time offset, per-language delay,
//     timestamp/filepos), MPEG-1 and MPEG-2 program-stream packs in the .sub, SPUs spread over
//     several packets, the control sequences (forced/start/stop display, colours, contrast, display
//     area, field offsets, CHG_COLCON skipped) and the interlaced 2-bit RLE.
//
// What comes out: one ImageCue per distinct picture, sorted by start. `image` is QImage::Format_Indexed8
// with a non-premultiplied ARGB colour table (1 byte per pixel, so a two-hour film stays in the
// low hundreds of MB at worst), already trimmed to its visible pixels; `rect` is where that image
// sits on a frame of `videoSize`. A renderer scales rect from videoSize to the on-screen video
// rectangle and uploads image.convertToFormat(QImage::Format_ARGB32_Premultiplied) when the cue
// becomes current. Identical consecutive pictures (PGS acquisition points) share one cue.
//
// Robustness: input is capped at 64 MB per buffer, every length field is checked against the bytes
// that are actually there, images are capped at 4096x4096, the timeline at 20000 cues and 512 MB of
// pixels, the total decode/compose work at 1.5 G pixels (about 1.5 s worst case: a stream of huge
// empty objects stops there with a note), and every loop advances or stops. Malformed segments/packets are skipped and counted; only
// a stream with no usable cue at all is a failure.
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace xmb {
// One picture on the half-open interval [startMs, endMs).
struct ImageCue {
    qint64 startMs=0;
    qint64 endMs=0;
    QImage image;     // Format_Indexed8, colour table is non-premultiplied ARGB, trimmed to visible pixels
    QRect rect;       // where `image` goes, in videoSize coordinates (same size as image)
    QSize videoSize;  // the frame the stream was authored for (PCS size / .idx "size:")
    bool forced=false;// PGS forced-on object, or VobSub FSTA_DSP: shown even with subtitles "off"
};

// Decodes a whole .sup / Jellyfin PGS stream. Returns false with `reason` set when nothing usable was
// found; on success `reason` (when given) gets a one-line note about anything skipped, or is cleared.
bool parsePgs(const QByteArray& sup,QVector<ImageCue>& out,QString* reason=nullptr);

// Decodes one language of a VobSub pair. `track` is the .idx "index:" value of the wanted language
// (falling back to its ordinal position); a negative track means the .idx "langidx:" default. When
// the chosen language has no timestamp lines the .sub is scanned for that substream (0x20 + index)
// and its packet PTS are used instead. Same reason/return contract as parsePgs.
bool parseVobSub(const QByteArray& idx,const QByteArray& sub,int track,QVector<ImageCue>& out,QString* reason=nullptr);

// The cue on screen at `ms`, or nullptr. `cues` must be sorted by startMs (both parsers guarantee it);
// when cues overlap the latest one that has started wins, as with text subtitles.
const ImageCue* cueAt(const QVector<ImageCue>& cues,qint64 ms);
}
