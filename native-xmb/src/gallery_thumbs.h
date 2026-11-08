#pragma once
#include "gallery_params.h"
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVector>
#include <atomic>

namespace xmb {
// Photo Gallery thumbnail LOD cache (audit/PHOTO-GALLERY-DESIGN.md step G3).
//
// The module picks a photo's resolution from the camera distance (LOD_PHOTO_DIST_1..3) and asks for thumbnails
// around the focused photo (REQUEST_LOD_PHOTO_DIST_1..3). Three sizes are cached (CACHE_THUMBNAIL_LEVEL1..3; the
// side in pixels is the number in the comment on each of those lines, read from parameters.txt at run time), level 0
// is "tiny, always 0" and draws as the placeholder colour only.
//
// Everything that touches pixels runs on a private thread pool, never on the GUI thread: decode with
// QImageReader::setScaledSize (JPEG decodes at reduced size), EXIF orientation applied, JPEG (PNG when the picture
// has alpha) written under <directory>/<side>/<xx>/<sha1>.jpg|png. The GUI thread only receives finished QImages
// through takeReady(budget) - the per-frame upload budget the GL scene spends - and the memory LRU. Both disk and
// memory are bounded (maxDiskBytes, CACHE_THUMBNAIL_LEVELn entries).
class GalleryThumbs final : public QObject {
    Q_OBJECT
public:
    enum Level { Placeholder = 0, Small = 1, Medium = 2, Large = 3 };
    struct Policy {
        double lodDistance[3] = {};   // LOD_PHOTO_DIST_1..3: Large up to [0], Medium up to [1], Small up to [2]
        double requestRatio[3] = {};  // REQUEST_LOD_PHOTO_DIST_1..3
        int side[4] = {};             // pixels per level (comments on CACHE_THUMBNAIL_LEVELn), [0] unused
        int memoryEntries[4] = {};    // CACHE_THUMBNAIL_LEVELn; 0 = the firmware keeps none: a small floor is used
    };
    struct Config {
        Policy policy;
        QString directory;            // e.g. <data>/cache/gallery/thumbs
        qint64 maxDiskBytes = 512ll * 1024 * 1024;
        int threads = 0;              // 0 = a third of the cores, 1..3
    };
    struct Item { int index = -1; double x = 0, z = 0; };
    struct Request { int index = -1; Level level = Placeholder; };
    struct Ready { QString path; Level level = Placeholder; QImage image; };
    struct Stats {
        quint64 memoryHits = 0, diskHits = 0, generated = 0, failed = 0, cancelled = 0;
        qint64 bytesWritten = 0;
    };
    enum class Outcome { DiskHit, Generated, Failed, Cancelled };

    static constexpr int MemoryFloor = 64;
    static constexpr qint64 MaximumSourceBytes = 1024ll * 1024 * 1024;

    // Fails (with the missing names) when parameters.txt lacks any value or level size.
    static bool policyFromParams(const GalleryParams& params, Policy* policy, QString* error);

    explicit GalleryThumbs(const Config& config, QObject* parent = nullptr);
    ~GalleryThumbs() override;

    // ---- pure policy ----
    Level levelForDistance(double cameraDistance) const;
    int sideOf(Level level) const { return level >= Small && level <= Large ? config_.policy.side[level] : 0; }
    // World-space radius around the focus inside which a level is requested: REQUEST_LOD_PHOTO_DIST_n x
    // LOD_PHOTO_DIST_n (INFERRED reading of "a range of dist around the focused photo").
    double requestRadius(Level level) const;
    // Which photo wants which level: each item gets the best level whose request radius holds it, never above what
    // the camera distance shows. Nearest first, at most maxRequests, placeholders omitted.
    QVector<Request> plan(double focusX, double focusZ, double cameraDistance, const QVector<Item>& items,
                          int maxRequests) const;

    // ---- asynchronous use (GL scene) ----
    // Queues one thumbnail (a no-op while it is in flight). deliver=false only fills the disk cache.
    void request(const QString& path, Level level, bool deliver = true);
    // Jobs that have not started are dropped; running ones finish.
    void cancelPending();
    QImage cached(const QString& path, Level level);
    // Up to budget finished images for the GUI thread to upload; the same images stay in the memory LRU.
    QVector<Ready> takeReady(int budget);
    int inFlight() const { return inFlight_; }
    int readyCount() const { return ready_.size(); }
    Stats stats() const { return stats_; }
    // Blocks until every queued job has finished and its result has been delivered (CLI warm-up and tests).
    void waitIdle();

    // ---- disk cache ----
    QString cacheDirectory() const { return config_.directory; }
    // Total bytes of thumbnails on disk (walks the cache).
    qint64 diskBytes() const;
    // Deletes the least recently used files (modification time, refreshed on every hit) until the cache fits
    // maxDiskBytes. Returns the bytes left.
    qint64 pruneDisk() const;

    // The worker body, public so tests can call it without the pool: decode path at side pixels, apply EXIF
    // orientation, store in directory. *image is filled when non-null.
    static Outcome produce(const QString& path, int side, const QString& directory, QImage* image, qint64* bytesWritten);

signals:
    void readyChanged();

private:
    friend class ThumbJob;
    struct Result {
        QString key, path;
        Level level;
        Outcome outcome;
        qint64 bytes;
        QImage image;
        bool deliver;
    };
    void deliver(Result result);
    void remember(const QString& key, Level level, const QImage& image);

    Config config_;
    QThreadPool pool_;
    std::atomic<quint64> generation_{0};
    int inFlight_ = 0;
    QHash<QString, bool> flying_;
    struct Cached { QImage image; quint64 tick = 0; Level level = Placeholder; };
    QHash<QString, Cached> memory_;
    quint64 tick_ = 0;
    QVector<Ready> ready_;
    Stats stats_;
};
}
