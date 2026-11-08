#include "gallery_thumbs.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRunnable>
#include <QSaveFile>
#include <QThread>
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace xmb {
namespace {
QString cacheKey(const QString& path, int side) { return QStringLiteral("%1|%2").arg(side).arg(path); }

// <directory>/<side>/<xx>/<sha1>; the hash covers path, size and modification time so an edited photo gets a new file.
QString cacheStem(const QString& directory, const QString& path, int side, const QFileInfo& info) {
    const QByteArray id = QStringLiteral("%1|%2|%3|%4").arg(path).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()).arg(side).toUtf8();
    const QString hex = QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex());
    return QDir(directory).filePath(QStringLiteral("%1/%2/%3").arg(side).arg(hex.left(2), hex));
}

bool validCacheFile(const QString& file) {
    QImageReader reader(file);
    const QSize size = reader.size();
    return size.isValid() && size.width() > 0 && size.height() > 0;
}
}

class ThumbJob final : public QRunnable {
public:
    ThumbJob(GalleryThumbs* owner, const std::atomic<quint64>* generation, quint64 mine, QString path,
             GalleryThumbs::Level level, int side, QString directory, bool deliver)
        : owner_(owner), generation_(generation), mine_(mine), path_(std::move(path)), level_(level), side_(side),
          directory_(std::move(directory)), deliver_(deliver) {}
    void run() override {
        GalleryThumbs::Result result{cacheKey(path_, side_), path_, level_, GalleryThumbs::Outcome::Cancelled, 0, {}, deliver_};
        if (generation_->load() == mine_) {
            QImage image;
            qint64 bytes = 0;
            result.outcome = GalleryThumbs::produce(path_, side_, directory_, deliver_ ? &image : nullptr, &bytes);
            result.bytes = bytes;
            result.image = image;
        }
        QMetaObject::invokeMethod(owner_, [owner = owner_, result = std::move(result)]() mutable { owner->deliver(std::move(result)); },
                                  Qt::QueuedConnection);
    }
private:
    GalleryThumbs* owner_;
    const std::atomic<quint64>* generation_;
    quint64 mine_;
    QString path_;
    GalleryThumbs::Level level_;
    int side_;
    QString directory_;
    bool deliver_;
};

bool GalleryThumbs::policyFromParams(const GalleryParams& params, Policy* policy, QString* error) {
    static const QStringList names{"LOD_PHOTO_DIST_1", "LOD_PHOTO_DIST_2", "LOD_PHOTO_DIST_3",
        "REQUEST_LOD_PHOTO_DIST_1", "REQUEST_LOD_PHOTO_DIST_2", "REQUEST_LOD_PHOTO_DIST_3",
        "CACHE_THUMBNAIL_LEVEL1", "CACHE_THUMBNAIL_LEVEL2", "CACHE_THUMBNAIL_LEVEL3"};
    if (const QString missing = params.require(names); !missing.isEmpty()) {
        if (error) *error = missing;
        return false;
    }
    Policy p;
    p.lodDistance[0] = params.number("LOD_PHOTO_DIST_1");
    p.lodDistance[1] = params.number("LOD_PHOTO_DIST_2");
    p.lodDistance[2] = params.number("LOD_PHOTO_DIST_3");
    p.requestRatio[0] = params.number("REQUEST_LOD_PHOTO_DIST_1");
    p.requestRatio[1] = params.number("REQUEST_LOD_PHOTO_DIST_2");
    p.requestRatio[2] = params.number("REQUEST_LOD_PHOTO_DIST_3");
    // Level n's side is the comment on its CACHE_THUMBNAIL_LEVELn line: Small(1)=LEVEL1 ... Large(3)=LEVEL3.
    p.side[1] = params.commentInteger("CACHE_THUMBNAIL_LEVEL1");
    p.side[2] = params.commentInteger("CACHE_THUMBNAIL_LEVEL2");
    p.side[3] = params.commentInteger("CACHE_THUMBNAIL_LEVEL3");
    p.memoryEntries[1] = params.integer("CACHE_THUMBNAIL_LEVEL1");
    p.memoryEntries[2] = params.integer("CACHE_THUMBNAIL_LEVEL2");
    p.memoryEntries[3] = params.integer("CACHE_THUMBNAIL_LEVEL3");
    for (int level = 1; level <= 3; ++level) {
        if (p.side[level] < 16 || p.side[level] > 4096) {
            if (error) *error = QStringLiteral("Photo Gallery parameters give no thumbnail size for CACHE_THUMBNAIL_LEVEL%1").arg(level);
            return false;
        }
        p.memoryEntries[level] = std::max(p.memoryEntries[level], MemoryFloor);
    }
    if (!(p.lodDistance[0] > 0 && p.lodDistance[0] <= p.lodDistance[1] && p.lodDistance[1] <= p.lodDistance[2])) {
        if (error) *error = QStringLiteral("Photo Gallery parameters: LOD_PHOTO_DIST_1..3 are not increasing");
        return false;
    }
    *policy = p;
    return true;
}

GalleryThumbs::GalleryThumbs(const Config& config, QObject* parent) : QObject(parent), config_(config) {
    int threads = config.threads > 0 ? config.threads : std::clamp(QThread::idealThreadCount() / 3, 1, 3);
    pool_.setMaxThreadCount(threads);
    pool_.setThreadPriority(QThread::LowPriority);
    QDir().mkpath(config_.directory);
}

GalleryThumbs::~GalleryThumbs() {
    generation_.fetch_add(1);
    pool_.clear();
    pool_.waitForDone();
}

GalleryThumbs::Level GalleryThumbs::levelForDistance(double d) const {
    if (d <= config_.policy.lodDistance[0]) return Large;
    if (d <= config_.policy.lodDistance[1]) return Medium;
    if (d <= config_.policy.lodDistance[2]) return Small;
    return Placeholder;
}

double GalleryThumbs::requestRadius(Level level) const {
    if (level < Small || level > Large) return 0;
    const int slot = 3 - int(level);  // Large = LOD_1 ... Small = LOD_3
    return config_.policy.requestRatio[slot] * config_.policy.lodDistance[slot];
}

QVector<GalleryThumbs::Request> GalleryThumbs::plan(double focusX, double focusZ, double cameraDistance,
                                                     const QVector<Item>& items, int maxRequests) const {
    const Level ceiling = levelForDistance(cameraDistance);
    struct Candidate { double distance; int index; Level level; };
    QVector<Candidate> candidates;
    if (ceiling == Placeholder || maxRequests <= 0) return {};
    for (const Item& item : items) {
        const double distance = std::hypot(item.x - focusX, item.z - focusZ);
        Level level = Placeholder;
        for (int candidate = Large; candidate >= Small; --candidate)
            if (distance <= requestRadius(Level(candidate))) { level = Level(candidate); break; }
        level = std::min(level, ceiling);
        if (level != Placeholder) candidates.push_back({distance, item.index, level});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.index < b.index; });
    QVector<Request> requests;
    for (const Candidate& candidate : candidates) {
        if (requests.size() >= maxRequests) break;
        requests.push_back({candidate.index, candidate.level});
    }
    return requests;
}

void GalleryThumbs::request(const QString& path, Level level, bool deliver) {
    const int side = sideOf(level);
    if (side <= 0 || path.isEmpty()) return;
    const QString key = cacheKey(path, side);
    if (deliver) {
        if (auto found = memory_.find(key); found != memory_.end()) {
            found->tick = ++tick_;
            ++stats_.memoryHits;
            ready_.push_back({path, level, found->image});
            emit readyChanged();
            return;
        }
    }
    if (flying_.contains(key)) return;
    flying_.insert(key, true);
    ++inFlight_;
    pool_.start(new ThumbJob(this, &generation_, generation_.load(), path, level, side, config_.directory, deliver));
}

void GalleryThumbs::cancelPending() { generation_.fetch_add(1); }

QImage GalleryThumbs::cached(const QString& path, Level level) {
    const auto found = memory_.find(cacheKey(path, sideOf(level)));
    if (found == memory_.end()) return {};
    found->tick = ++tick_;
    return found->image;
}

QVector<GalleryThumbs::Ready> GalleryThumbs::takeReady(int budget) {
    QVector<Ready> out;
    const int count = std::clamp(budget, 0, int(ready_.size()));
    out = ready_.mid(0, count);
    ready_.remove(0, count);
    return out;
}

void GalleryThumbs::remember(const QString& key, Level level, const QImage& image) {
    memory_.insert(key, {image, ++tick_, level});
    int inLevel = 0;
    for (const Cached& entry : std::as_const(memory_)) inLevel += entry.level == level;
    const int cap = config_.policy.memoryEntries[level];
    while (inLevel > cap) {
        auto oldest = memory_.end();
        for (auto it = memory_.begin(); it != memory_.end(); ++it)
            if (it->level == level && (oldest == memory_.end() || it->tick < oldest->tick)) oldest = it;
        if (oldest == memory_.end()) break;
        memory_.erase(oldest);
        --inLevel;
    }
}

void GalleryThumbs::deliver(Result result) {
    flying_.remove(result.key);
    --inFlight_;
    switch (result.outcome) {
    case Outcome::DiskHit: ++stats_.diskHits; break;
    case Outcome::Generated: ++stats_.generated; stats_.bytesWritten += result.bytes; break;
    case Outcome::Failed: ++stats_.failed; break;
    case Outcome::Cancelled: ++stats_.cancelled; break;
    }
    if (result.deliver && !result.image.isNull()) {
        remember(result.key, result.level, result.image);
        ready_.push_back({result.path, result.level, result.image});
        emit readyChanged();
    }
}

void GalleryThumbs::waitIdle() {
    while (inFlight_ > 0) {
        pool_.waitForDone();
        QCoreApplication::sendPostedEvents(this, 0);
    }
}

qint64 GalleryThumbs::diskBytes() const {
    qint64 total = 0;
    QDirIterator it(config_.directory, {QStringLiteral("*.jpg"), QStringLiteral("*.png")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

qint64 GalleryThumbs::pruneDisk() const {
    struct Entry { QString path; qint64 size; qint64 time; };
    QVector<Entry> entries;
    qint64 total = 0;
    QDirIterator it(config_.directory, {QStringLiteral("*.jpg"), QStringLiteral("*.png")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        entries.push_back({info.filePath(), info.size(), info.lastModified().toMSecsSinceEpoch()});
        total += info.size();
    }
    if (total <= config_.maxDiskBytes) return total;
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time != b.time ? a.time < b.time : a.path < b.path; });
    for (const Entry& entry : entries) {
        if (total <= config_.maxDiskBytes) break;
        if (QFile::remove(entry.path)) total -= entry.size;
    }
    return total;
}

GalleryThumbs::Outcome GalleryThumbs::produce(const QString& path, int side, const QString& directory, QImage* image, qint64* bytesWritten) {
    if (bytesWritten) *bytesWritten = 0;
    const QFileInfo source(path);
    if (!source.isFile() || source.size() <= 0 || source.size() > MaximumSourceBytes || side <= 0) return Outcome::Failed;
    const QString stem = cacheStem(directory, path, side, source);
    for (const char* extension : {".jpg", ".png"}) {
        const QString file = stem + QLatin1String(extension);
        if (!QFileInfo::exists(file)) continue;
        if (!validCacheFile(file)) { QFile::remove(file); continue; }
        QFile(file).setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        if (image) {
            QImageReader reader(file);
            reader.setAllocationLimit(128);
            *image = reader.read();
            if (image->isNull()) { QFile::remove(file); continue; }
        }
        return Outcome::DiskHit;
    }

    QImageReader reader(path);
    reader.setAllocationLimit(128);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid() && std::max(full.width(), full.height()) > side) {
        const double scale = double(side) / std::max(full.width(), full.height());
        reader.setScaledSize(QSize(std::max(1, int(std::lround(full.width() * scale))), std::max(1, int(std::lround(full.height() * scale)))));
    }
    QImage decoded = reader.read();
    if (decoded.isNull()) return Outcome::Failed;
    if (std::max(decoded.width(), decoded.height()) > side)
        decoded = decoded.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (decoded.isNull()) return Outcome::Failed;

    const bool alpha = decoded.hasAlphaChannel();
    const QString file = stem + (alpha ? QLatin1String(".png") : QLatin1String(".jpg"));
    // Several workers create the same <side>/<xx> folder at once; a lost race shows as a failed open, so retry.
    QSaveFile out(file);
    bool opened = false;
    for (int attempt = 0; attempt < 4 && !opened; ++attempt) {
        QDir().mkpath(QFileInfo(file).absolutePath());
        opened = out.open(QIODevice::WriteOnly);
        if (!opened) QThread::msleep(2);
    }
    if (!opened) return Outcome::Failed;
    const QImage toStore = alpha ? decoded : decoded.convertToFormat(QImage::Format_RGB32);
    if (!toStore.save(&out, alpha ? "PNG" : "JPG", 88) || !out.commit()) return Outcome::Failed;
    if (bytesWritten) *bytesWritten = QFileInfo(file).size();
    if (image) *image = decoded;
    return Outcome::Generated;
}
}
