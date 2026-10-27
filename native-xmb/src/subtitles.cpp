#include "subtitles.h"
#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>

namespace xmb::subtitles {
namespace {
// Bounds. A hand-written .srt is a few kilobytes and a two-hour film's is around 100 KB; the cap is
// here so a wrong file (a video renamed to .srt, a log file) is refused instead of read into memory.
constexpr qint64 maximumFileBytes=4*1024*1024;
constexpr int maximumCandidates=16;
constexpr int maximumCues=20000;
constexpr int maximumCueCharacters=1200;
constexpr int maximumCueLines=6;

// A sidecar's bytes: UTF-8 with or without a BOM, or UTF-16 in either byte order. Everything else is
// read as UTF-8 with replacement characters, which is what a mis-encoded .srt should degrade to
// rather than refusing to play.
QString decodeBytes(const QByteArray& raw) {
    if(raw.startsWith(QByteArray::fromHex("EFBBBF")))return QString::fromUtf8(raw.mid(3));
    if(raw.startsWith(QByteArray::fromHex("FFFE"))) {
        const QByteArray payload=raw.mid(2);QStringDecoder decoder(QStringConverter::Utf16LE);
        return decoder.decode(payload);
    }
    if(raw.startsWith(QByteArray::fromHex("FEFF"))) {
        const QByteArray payload=raw.mid(2);QStringDecoder decoder(QStringConverter::Utf16BE);
        return decoder.decode(payload);
    }
    return QString::fromUtf8(raw);
}

// 00:00:01,000 / 00:00:01.000 / 00:01.500 / 90 (seconds, as some tools write it). Hours are
// optional and the fraction may be 1-3 digits, because both spellings appear in the wild.
bool parseTimestamp(const QString& raw,qint64& milliseconds) {
    QString value=raw.trimmed();
    if(value.isEmpty() || value.size()>32)return false;
    value.replace(',','.');
    const auto parts=value.split(':');
    if(parts.isEmpty() || parts.size()>3)return false;
    double seconds=0;
    for(const auto& part:parts) {
        bool ok=false;const double field=part.trimmed().toDouble(&ok);
        if(!ok || field<0 || field>100000)return false;
        seconds=seconds*60+field;
    }
    if(!std::isfinite(seconds) || seconds>10*3600.0)return false;
    milliseconds=qint64(std::llround(seconds*1000));
    return true;
}

// Strips the inline markup a cue may carry: <i>, <b>, <font color=...>, WebVTT's <v Bob>/<c.class>
// and ASS override blocks like {\an8}. The five named entities are decoded afterwards.
QString cleanCueLine(const QString& line) {
    QString out;out.reserve(line.size());
    bool inTag=false,inBrace=false;
    for(const QChar c:line) {
        if(inTag){if(c=='>')inTag=false;continue;}
        if(inBrace){if(c=='}')inBrace=false;continue;}
        if(c=='<'){inTag=true;continue;}
        if(c=='{'){inBrace=true;continue;}
        out+=c;
    }
    out.replace("&amp;","&");
    out.replace("&lt;","<");
    out.replace("&gt;",">");
    out.replace("&quot;","\"");
    out.replace("&nbsp;"," ");
    out.replace("&#39;","'");
    return out.trimmed();
}

struct ParsedCues {
    QVector<Cue> cues;
    int blocks=0;
    int skipped=0;
    QString error;
};

// One block per cue for SRT, and the same shape for WebVTT once its headers are dropped: find the
// timing line, read the two timestamps, take everything after it as the cue's text.
ParsedCues parseCueText(const QString& text,const QString& format) {
    ParsedCues result;
    QString body=text;
    if(!body.isEmpty() && body.at(0)==QChar(0xFEFF))body.remove(0,1);
    auto lines=body.split('\n');
    for(auto& line:lines)if(line.endsWith('\r'))line.chop(1);
    QVector<QStringList> blocks;QStringList block;
    for(const auto& line:lines) {
        // A blank line ends the block, exactly as both formats define a cue boundary. Leading and
        // trailing blank lines therefore produce no empty block.
        if(line.trimmed().isEmpty()) {
            if(!block.isEmpty()){blocks.push_back(block);block.clear();}
            continue;
        }
        block.push_back(line);
        if(blocks.size()>maximumCues+64)break; // a pathological file cannot grow the block list
    }
    if(!block.isEmpty())blocks.push_back(block);
    bool first=true;
    for(const auto& lines2:blocks) {
        if(lines2.isEmpty())continue;
        const QString head=lines2.first().trimmed();
        // WebVTT's own header and its comment/metadata blocks are not cues.
        if(format=="vtt" && first && head.startsWith("WEBVTT",Qt::CaseInsensitive)){first=false;continue;}
        first=false;
        if(format=="vtt" && head.startsWith("NOTE",Qt::CaseInsensitive))continue;
        if(format=="vtt" && (head.startsWith("STYLE",Qt::CaseInsensitive) || head.startsWith("REGION",Qt::CaseInsensitive)
            || head.startsWith("X-TIMESTAMP-MAP",Qt::CaseInsensitive)))continue;
        ++result.blocks;
        int timing=-1;
        for(int i=0;i<lines2.size();++i)if(lines2[i].contains("-->")){timing=i;break;}
        if(timing<0){++result.skipped;continue;}
        const QString timingLine=lines2[timing];
        const int arrow=timingLine.indexOf("-->");
        const QString left=timingLine.left(arrow).trimmed();
        const QString right=timingLine.mid(arrow+3).trimmed();
        // Text before the arrow may be an SRT index on the same line ("12 00:00:01,000"); the
        // last space-separated field is the timestamp. After the arrow the timestamp ends at the
        // first space (WebVTT settings: "align:start position:10%"; SRT coordinates: "X1:.. ").
        const int lastSpace=left.lastIndexOf(' ');
        const QString startText=lastSpace<0?left:left.mid(lastSpace+1);
        const int space=right.indexOf(' ');
        const QString endText=space<0?right:right.left(space);
        qint64 start=0,end=0;
        if(!parseTimestamp(startText,start) || !parseTimestamp(endText,end)){++result.skipped;continue;}
        if(end<=start){++result.skipped;continue;}
        QStringList textLines;
        for(int i=timing+1;i<lines2.size();++i) {
            const QString cleaned=cleanCueLine(lines2[i]);
            textLines.push_back(cleaned);
            if(textLines.size()>=maximumCueLines)break;
        }
        while(!textLines.isEmpty() && textLines.first().isEmpty())textLines.removeFirst();
        while(!textLines.isEmpty() && textLines.last().isEmpty())textLines.removeLast();
        QString cueText=textLines.join('\n');
        if(cueText.size()>maximumCueCharacters)cueText.truncate(maximumCueCharacters);
        if(cueText.isEmpty()){++result.skipped;continue;}
        if(result.cues.size()>=maximumCues){++result.skipped;continue;}
        result.cues.push_back({start,end,cueText});
    }
    // A file whose cues are out of order still plays: the search below assumes sorted starts.
    std::stable_sort(result.cues.begin(),result.cues.end(),[](const Cue& a,const Cue& b){return a.start<b.start;});
    if(result.cues.isEmpty())
        result.error=QString("no usable cue in %1 block(s)").arg(result.blocks);
    return result;
}

ParsedCues parseFile(const QString& path) {
    ParsedCues result;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {
        result.error=QString("cannot be opened (%1)").arg(file.errorString());
        return result;
    }
    const qint64 size=file.size();
    if(size<=0){result.error="file is empty";return result;}
    if(size>maximumFileBytes) {
        result.error=QString("file is %1 bytes, above the %2-byte limit").arg(size).arg(maximumFileBytes);
        return result;
    }
    const auto raw=file.read(maximumFileBytes+1);
    file.close();
    if(raw.size()>maximumFileBytes){result.error="file grew past the size limit while reading";return result;}
    const QString format=QFileInfo(path).suffix().toLower();
    return parseCueText(decodeBytes(raw),format);
}

bool supportedFormat(const QString& extension){return extension=="srt" || extension=="vtt";}
// Subtitle formats that exist and that this build cannot read. They are named in the log rather
// than silently skipped, because "I put my .ass file next to the film and nothing happened" is
// exactly the report this reason answers.
bool otherSubtitleFormat(const QString& extension) {
    static const QStringList others{"ass","ssa","sub","idx","smi","sami","ttml","dfxp","sbv","lrc","mpl"};
    return others.contains(extension);
}
// The tag in "<media>.<tag>.srt": letters, digits, '.', '_' and '-' up to 24 characters. That
// accepts en, eng, en.forced, english and part2, and refuses a name that is not a tag rather than
// pretending it is a language.
bool plausibleTag(const QString& tag) {
    if(tag.isEmpty() || tag.size()>24)return false;
    for(int i=0;i<tag.size();++i) {
        const QChar c=tag.at(i);
        if(!(c.isLetterOrNumber() || c=='.' || c=='_' || c=='-'))return false;
    }
    return tag.at(0).isLetter() || tag.at(0).isDigit();
}
// Splits "<base>[.<tag>].<ext>" into the format extension and the language tag (empty when the file
// carries none). `name` and `base` differ only after base.size(), so the slice is case-insensitive.
void splitCandidate(const QString& name,const QString& base,QString& extension,QString& tag) {
    const QString rest=name.mid(base.size()+1);
    const int dot=rest.lastIndexOf('.');
    if(dot<0){extension=rest.toLower();tag.clear();return;}
    extension=rest.mid(dot+1).toLower();
    tag=rest.left(dot);
}

const QHash<QString,QString>& languageNames() {
    static const QHash<QString,QString> names{
        {"en","English"},{"eng","English"},{"fr","French"},{"fra","French"},{"fre","French"},
        {"de","German"},{"deu","German"},{"ger","German"},{"es","Spanish"},{"spa","Spanish"},
        {"it","Italian"},{"ita","Italian"},{"ja","Japanese"},{"jpn","Japanese"},
        {"ko","Korean"},{"kor","Korean"},{"pt","Portuguese"},{"por","Portuguese"},
        {"ru","Russian"},{"rus","Russian"},{"zh","Chinese"},{"zho","Chinese"},{"chi","Chinese"},
        {"nl","Dutch"},{"nld","Dutch"},{"dut","Dutch"},{"sv","Swedish"},{"swe","Swedish"},
        {"no","Norwegian"},{"nor","Norwegian"},{"da","Danish"},{"dan","Danish"},
        {"fi","Finnish"},{"fin","Finnish"},{"pl","Polish"},{"pol","Polish"},
        {"tr","Turkish"},{"tur","Turkish"},{"ar","Arabic"},{"ara","Arabic"},
        {"he","Hebrew"},{"heb","Hebrew"},{"hi","Hindi"},{"hin","Hindi"},
        {"cs","Czech"},{"ces","Czech"},{"el","Greek"},{"ell","Greek"},
        {"hu","Hungarian"},{"hun","Hungarian"},{"ro","Romanian"},{"ron","Romanian"},
        {"th","Thai"},{"tha","Thai"},{"uk","Ukrainian"},{"ukr","Ukrainian"},
        {"vi","Vietnamese"},{"vie","Vietnamese"},{"id","Indonesian"},{"ind","Indonesian"}};
    return names;
}
}

Discovery discover(const QString& mediaPath) {
    Discovery result;
    if(mediaPath.trimmed().isEmpty())return result;
    const QFileInfo media(mediaPath);
    const QString base=media.completeBaseName();
    if(base.isEmpty())return result;
    const QDir directory(media.absolutePath());
    if(!directory.exists())return result;
    const auto names=directory.entryList(QDir::Files|QDir::Readable,QDir::Name);
    const QString lowerBase=base.toLower();
    QStringList candidates;
    for(const auto& name:names) {
        if(candidates.size()>=maximumCandidates)break;
        const QString lower=name.toLower();
        if(lower==lowerBase)continue;
        if(!lower.startsWith(lowerBase+'.'))continue;
        QString extension,tag;
        splitCandidate(name,base,extension,tag);
        if(supportedFormat(extension)) {
            if(tag.isEmpty())candidates.push_back(name);
            else if(plausibleTag(tag))candidates.push_back(name);
            else result.rejected.push_back(QString("%1: the part after the title is not a language tag").arg(name));
            continue;
        }
        if(otherSubtitleFormat(extension))
            result.rejected.push_back(QString("%1: %2 subtitles are not supported (SRT and WebVTT only)")
                .arg(name,extension.toUpper()));
    }
    for(const auto& name:candidates) {
        const QString path=directory.filePath(name);
        QString extension,tag;
        splitCandidate(name,base,extension,tag);
        auto parsed=parseFile(path);
        if(!parsed.error.isEmpty()) {
            result.rejected.push_back(QString("%1: %2").arg(name,parsed.error));
            continue;
        }
        Track track;
        track.path=path;track.language=tag;track.format=extension;
        track.bytes=QFileInfo(path).size();track.skipped=parsed.skipped;
        track.cues=std::move(parsed.cues);
        track.name=displayName(track);
        result.tracks.push_back(std::move(track));
    }
    // The untagged file is the player's default, so it comes first; the rest follow by label.
    std::stable_sort(result.tracks.begin(),result.tracks.end(),[](const Track& a,const Track& b){
        if(a.language.isEmpty()!=b.language.isEmpty())return a.language.isEmpty();
        return QString::compare(a.name,b.name,Qt::CaseInsensitive)<0;
    });
    return result;
}

QString displayName(const Track& track) {
    const QString format=track.format.toUpper();
    if(track.language.isEmpty())return QString("On (%1)").arg(format.isEmpty()?"file":format);
    QString head=track.language,tail;
    for(int i=0;i<track.language.size();++i) {
        const QChar c=track.language.at(i);
        if(c=='.' || c=='_' || c=='-'){head=track.language.left(i);tail=track.language.mid(i+1);break;}
    }
    const QString name=languageNames().value(head.toLower());
    if(name.isEmpty())return track.language; // an unknown tag is shown exactly as the file spells it
    return tail.isEmpty()?name:QString("%1 (%2)").arg(name,tail);
}

int cueAt(const QVector<Cue>& cues,qint64 positionMs) {
    if(cues.isEmpty() || positionMs<0)return -1;
    // The last cue that has started; overlapping cues resolve to the latest start, which is what a
    // player shows. A short backwards walk then finds the one whose end still covers the position.
    int low=0,high=int(cues.size())-1,started=-1;
    while(low<=high) {
        const int middle=(low+high)/2;
        if(cues[middle].start<=positionMs){started=middle;low=middle+1;}
        else high=middle-1;
    }
    if(started<0)return -1;
    const int lowest=std::max(0,started-4);
    for(int i=started;i>=lowest;--i)
        if(positionMs>=cues[i].start && positionMs<cues[i].end)return i;
    return -1;
}
}
