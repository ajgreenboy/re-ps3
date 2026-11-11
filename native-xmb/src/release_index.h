#pragma once
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace xmb {
struct Download {
    QString kind;
    QString uri;
    QString infohash;
    QString displayName;
    QStringList trackers;
};
struct MirrorGroup { QString host; QVector<Download> parts; };
namespace release_index {
struct Release {
    QString source;
    QString title;
    QVector<Download> downloads;
    QVector<MirrorGroup> mirrors;
    QDateTime publishedAt;
    qint64 sizeBytes=-1;
    qint64 sizeBytesMax=-1;
    QString sizeLabel;
    QJsonObject raw;
};
}
using IndexedRelease=release_index::Release;
struct ReleaseSize { qint64 sizeBytes=-1; qint64 sizeBytesMax=-1; QString sizeLabel; };
QStringList asStringArray(const QJsonValue& value);
QString classifyUri(const QString& uri);
Download parseMagnet(const QString& uri);
Download normalizeDownload(const QString& uri);
ReleaseSize parseSize(const QJsonValue& label);
QDateTime parseDate(const QJsonValue& value);
QVector<MirrorGroup> groupToMirrors(const QJsonValue& value);
QVector<IndexedRelease> parseReleaseIndex(const QJsonDocument& document, QString* error=nullptr);
// Versioned source storage uses these lossless normalized records (raw is retained).
QJsonObject releaseToJson(const IndexedRelease& release);
IndexedRelease releaseFromJson(const QJsonObject& object);
}
