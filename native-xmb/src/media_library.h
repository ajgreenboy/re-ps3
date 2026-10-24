#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>

namespace xmb {

enum class MediaKind { Photo, Music, Video };

struct MediaEntry {
    QString path;
    QString title;
    bool isDirectory = false;
    qint64 size = 0;
    QDateTime modified;
};

using MediaEntries = QVector<MediaEntry>;

// Call scan()/cancel() on this object's thread. Directory I/O, metadata reads,
// filtering and sorting run on the global Qt thread pool. No recursive scans.
class MediaLibrary final : public QObject {
    Q_OBJECT
public:
    static constexpr qsizetype MaximumEntries = 5000;

    explicit MediaLibrary(QObject* parent = nullptr);
    ~MediaLibrary() override;

    // Supersedes the preceding request. Results are delivered on this object's
    // thread. path is canonical when it exists, otherwise a cleaned absolute path.
    quint64 scan(QString path, MediaKind kind);
    void cancel();

signals:
    // Empty entries plus empty error means a successfully scanned empty folder.
    // A nonempty error can accompany partial entries if enumeration failed midway.
    // truncated means more than MaximumEntries matching entries were encountered.
    void loaded(quint64 requestId, QString path, xmb::MediaEntries entries,
                QString error, bool truncated);

private:
    quint64 nextRequestId_ = 0;
    quint64 activeRequestId_ = 0;
    std::shared_ptr<std::atomic_bool> cancellation_;
};

}

Q_DECLARE_METATYPE(xmb::MediaKind)
Q_DECLARE_METATYPE(xmb::MediaEntry)
Q_DECLARE_METATYPE(xmb::MediaEntries)
