#include "release_index.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <limits>

namespace xmb {
QStringList asStringArray(const QJsonValue& value) {
    if(value.isString())return {value.toString()};
    QStringList result;
    for(const auto& item:value.toArray())if(item.isString())result.append(item.toString());
    return result;
}
QString classifyUri(const QString& uri) {
    if(uri.startsWith(QStringLiteral("magnet:"),Qt::CaseInsensitive))return QStringLiteral("magnet");
    static const QRegularExpression torrent(QStringLiteral("\\.torrent(\\?|$)"),QRegularExpression::CaseInsensitiveOption);
    if(torrent.match(uri).hasMatch())return QStringLiteral("torrent");
    static const QRegularExpression http(QStringLiteral("^https?://"),QRegularExpression::CaseInsensitiveOption);
    return http.match(uri).hasMatch()?QStringLiteral("direct"):QStringLiteral("unknown");
}
Download parseMagnet(const QString& uri) {
    Download result; result.kind=QStringLiteral("magnet"); result.uri=uri;
    QString text=uri;
    text.remove(QRegularExpression(QStringLiteral("^magnet:\\??"),QRegularExpression::CaseInsensitiveOption));
    // URLSearchParams decodes '+' as space; QUrlQuery deliberately does not.
    text.replace(QLatin1Char('+'),QStringLiteral("%20"));
    const QUrlQuery query(text);
    static const QRegularExpression v1(QStringLiteral("^urn:btih:([0-9a-f]+)$"),QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression v2(QStringLiteral("^urn:btmh:(.+)$"),QRegularExpression::CaseInsensitiveOption);
    for(const auto& xt:query.allQueryItemValues(QStringLiteral("xt"),QUrl::FullyDecoded)) {
        const auto first=v1.match(xt),second=v2.match(xt);
        if(first.hasMatch()){result.infohash=first.captured(1).toLower();break;}
        if(second.hasMatch() && result.infohash.isEmpty())result.infohash=second.captured(1);
    }
    result.displayName=query.queryItemValue(QStringLiteral("dn"),QUrl::FullyDecoded);
    result.trackers=query.allQueryItemValues(QStringLiteral("tr"),QUrl::FullyDecoded);
    return result;
}
Download normalizeDownload(const QString& uri) {
    if(classifyUri(uri)==QStringLiteral("magnet"))return parseMagnet(uri);
    Download result; result.kind=classifyUri(uri); result.uri=uri; return result;
}
ReleaseSize parseSize(const QJsonValue& label) {
    ReleaseSize result;
    if(!label.isString())return result;
    result.sizeLabel=label.toString();
    QString text=result.sizeLabel.trimmed();
    text.remove(QRegularExpression(QStringLiteral("\\(?selective\\s+download\\)?\\s*$"),QRegularExpression::CaseInsensitiveOption));
    text.replace(QLatin1Char(','),QLatin1Char('.'));
    // Sources use non-breaking spaces (and &nbsp; entities) inside size strings: normalise both to a space.
    text.replace(QStringLiteral("&nbsp;"),QStringLiteral(" "));
    text.replace(QChar(0x00A0),QLatin1Char(' '));
    static const QRegularExpression token(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*(TB|GB|MB|KB|B)?"),QRegularExpression::CaseInsensitiveOption);
    QVector<QPair<double,QString>> values;
    QString fallback;
    auto matches=token.globalMatch(text);
    while(matches.hasNext()) {
        const auto match=matches.next(); const QString unit=match.captured(2).toUpper();
        if(fallback.isEmpty() && !unit.isEmpty())fallback=unit;
        values.append({match.captured(1).toDouble(),unit});
    }
    if(fallback.isEmpty())return result;
    qint64 low=std::numeric_limits<qint64>::max(),high=0;
    for(const auto& value:values) {
        const QString unit=value.second.isEmpty()?fallback:value.second;
        const int power=QStringList{QStringLiteral("B"),QStringLiteral("KB"),QStringLiteral("MB"),QStringLiteral("GB"),QStringLiteral("TB")}.indexOf(unit);
        const double bytes=value.first*std::pow(1024.0,power);
        if(!std::isfinite(bytes) || bytes>=double(std::numeric_limits<qint64>::max()))return result;
        const qint64 rounded=qint64(std::round(bytes)); low=std::min(low,rounded); high=std::max(high,rounded);
    }
    if(!values.isEmpty())result.sizeBytes=low;
    if(values.size()>1)result.sizeBytesMax=high;
    return result;
}
QDateTime parseDate(const QJsonValue& value) {
    if(value.isDouble()) {
        const double ms=value.toDouble();
        if(!std::isfinite(ms) || std::abs(ms)>8.64e15)return {};
        return QDateTime::fromMSecsSinceEpoch(qint64(ms),QTimeZone::UTC);
    }
    if(!value.isString())return {};
    const QString text=value.toString();
    auto date=QDateTime::fromString(text,Qt::ISODateWithMs);
    if(!date.isValid())date=QDateTime::fromString(text,Qt::ISODate);
    if(!date.isValid())date=QDateTime::fromString(text,Qt::RFC2822Date);
    return date;
}
QVector<MirrorGroup> groupToMirrors(const QJsonValue& value) {
    QVector<MirrorGroup> result;
    static const QRegularExpression part(QStringLiteral("\\.part(\\d+)\\.rar$"),QRegularExpression::CaseInsensitiveOption);
    for(const auto& group:value.toArray()) {
        MirrorGroup mirror; QVector<Download> numbered;
        for(const auto& uri:asStringArray(group)) {
            const auto download=normalizeDownload(uri); mirror.parts.append(download);
            if(part.match(uri).hasMatch())numbered.append(download);
        }
        std::stable_sort(numbered.begin(),numbered.end(),[&](const Download& a,const Download& b) {
            return part.match(a.uri).captured(1).toULongLong()<part.match(b.uri).captured(1).toULongLong();
        });
        qsizetype index=0;
        for(auto& download:mirror.parts)if(part.match(download.uri).hasMatch())download=numbered[index++];
        if(mirror.parts.isEmpty())continue;
        mirror.host=QUrl(mirror.parts.first().uri).host(); result.append(mirror);
    }
    return result;
}
namespace {
QVector<QJsonValue> children(const QJsonValue& value) {
    QVector<QJsonValue> result;
    if(value.isArray())for(const auto& child:value.toArray())result.append(child);
    else if(value.isObject())for(const auto& child:value.toObject())result.append(child);
    return result;
}
bool uriLike(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^(?:magnet:|https?://)|\\.torrent$"),QRegularExpression::CaseInsensitiveOption);
    return pattern.match(text).hasMatch();
}
QStringList collectUris(const QJsonValue& root) {
    QStringList result; QVector<QJsonValue> pending{root};
    while(!pending.isEmpty()) {
        const auto value=pending.takeLast();
        if(value.isString() && uriLike(value.toString())) {
            if(!result.contains(value.toString()))result.append(value.toString());
        } else {
            const auto nested=children(value);
            for(auto it=nested.crbegin();it!=nested.crend();++it)pending.append(*it);
        }
    }
    return result;
}
QJsonArray downloadsToJson(const QVector<Download>& downloads) {
    QJsonArray result;
    for(const auto& download:downloads)result.append(QJsonObject{
        {QStringLiteral("kind"),download.kind},{QStringLiteral("uri"),download.uri},
        {QStringLiteral("infohash"),download.infohash},{QStringLiteral("displayName"),download.displayName},
        {QStringLiteral("trackers"),QJsonArray::fromStringList(download.trackers)}});
    return result;
}
QVector<Download> downloadsFromJson(const QJsonArray& array) {
    QVector<Download> result;
    for(const auto& value:array)result.append(normalizeDownload(value.toObject().value(QStringLiteral("uri")).toString()));
    return result;
}
}
QVector<IndexedRelease> parseReleaseIndex(const QJsonDocument& document,QString* error) {
    if(error)error->clear();
    if(document.isNull()) { if(error)*error=QStringLiteral("Invalid release index JSON"); return {}; }
    const auto object=document.object();
    const bool fitgirl=object.value(QStringLiteral("downloads")).isArray();
    QJsonArray records;
    if(fitgirl)records=object.value(QStringLiteral("downloads")).toArray();
    else {
        QVector<QJsonValue> pending{document.isArray()?QJsonValue(document.array()):QJsonValue(object)};
        while(!pending.isEmpty()) {
            const auto value=pending.takeLast();
            if(value.isArray() && value.toArray().size()>records.size())records=value.toArray();
            pending.append(children(value));
        }
    }
    QVector<IndexedRelease> result;
    for(const auto& value:records) {
        if(!value.isObject())continue;
        IndexedRelease release; release.raw=value.toObject();
        release.source=fitgirl?QStringLiteral("fitgirl"):QStringLiteral("heuristic");
        QJsonValue size(QJsonValue::Undefined),date(QJsonValue::Undefined); QStringList uris;
        if(fitgirl) {
            release.title=release.raw.value(QStringLiteral("title")).toString();
            uris=asStringArray(release.raw.value(QStringLiteral("uris")));
            release.mirrors=groupToMirrors(release.raw.value(QStringLiteral("uriGroups")));
            size=release.raw.value(QStringLiteral("fileSize")); date=release.raw.value(QStringLiteral("uploadDate"));
        } else {
            QString fallback;
            for(auto it=release.raw.constBegin();it!=release.raw.constEnd();++it) {
                const QString key=it.key().toLower(); const auto item=it.value();
                if((key==QStringLiteral("title") || key==QStringLiteral("name")) && item.isString() && release.title.isNull())release.title=item.toString();
                if(fallback.isNull() && item.isString() && item.toString().size()>=8 && !uriLike(item.toString()))fallback=item.toString();
                if(size.isUndefined() && (key==QStringLiteral("size") || key==QStringLiteral("filesize")))size=item;
                if(date.isUndefined() && (key==QStringLiteral("date") || key==QStringLiteral("uploaddate") || key==QStringLiteral("publishedat")))date=item;
            }
            if(release.title.isNull())release.title=fallback;
            uris=collectUris(value);
        }
        for(const auto& uri:uris)release.downloads.append(normalizeDownload(uri));
        const auto parsed=parseSize(size); release.sizeBytes=parsed.sizeBytes; release.sizeBytesMax=parsed.sizeBytesMax; release.sizeLabel=parsed.sizeLabel;
        release.publishedAt=parseDate(date); result.append(release);
    }
    return result;
}
QJsonObject releaseToJson(const IndexedRelease& release) {
    QJsonArray mirrors;
    for(const auto& mirror:release.mirrors)mirrors.append(QJsonObject{{QStringLiteral("host"),mirror.host},{QStringLiteral("parts"),downloadsToJson(mirror.parts)}});
    return {{QStringLiteral("source"),release.source},{QStringLiteral("title"),release.title},
        {QStringLiteral("downloads"),downloadsToJson(release.downloads)},{QStringLiteral("mirrors"),mirrors},
        {QStringLiteral("publishedAt"),release.publishedAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("sizeBytes"),release.sizeBytes},{QStringLiteral("sizeBytesMax"),release.sizeBytesMax},
        {QStringLiteral("sizeLabel"),release.sizeLabel},{QStringLiteral("raw"),release.raw}};
}
IndexedRelease releaseFromJson(const QJsonObject& object) {
    IndexedRelease release; release.source=object.value(QStringLiteral("source")).toString(); release.title=object.value(QStringLiteral("title")).toString();
    release.downloads=downloadsFromJson(object.value(QStringLiteral("downloads")).toArray());
    for(const auto& value:object.value(QStringLiteral("mirrors")).toArray()) {
        const auto mirror=value.toObject(); release.mirrors.append({mirror.value(QStringLiteral("host")).toString(),downloadsFromJson(mirror.value(QStringLiteral("parts")).toArray())});
    }
    release.publishedAt=parseDate(object.value(QStringLiteral("publishedAt")));
    release.sizeBytes=object.value(QStringLiteral("sizeBytes")).toInteger(-1); release.sizeBytesMax=object.value(QStringLiteral("sizeBytesMax")).toInteger(-1);
    release.sizeLabel=object.value(QStringLiteral("sizeLabel")).toString(); release.raw=object.value(QStringLiteral("raw")).toObject(); return release;
}
}
