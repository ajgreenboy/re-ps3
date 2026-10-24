#include "media_library.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStringList>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

namespace xmb {
namespace {

struct ScanResult {
    QString path;
    MediaEntries entries;
    QString error;
    bool truncated = false;
};

std::filesystem::path filesystemPath(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    const auto bytes = path.toUtf8();
    return std::filesystem::u8path(bytes.constData(), bytes.constData() + bytes.size());
#endif
}

QString qtPath(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(QString::fromStdWString(path.native()));
#else
    const auto bytes = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
#endif
}

bool accepts(const QFileInfo& info, MediaKind kind) {
    static const QStringList photos{"png", "jpg", "jpeg", "bmp", "webp", "gif", "tif", "tiff"};
    static const QStringList music{"mp3", "flac", "wav", "m4a", "ogg", "aac", "wma", "opus"};
    static const QStringList videos{"mp4", "mkv", "avi", "mov", "webm", "m4v", "mpg", "mpeg", "wmv", "ts", "m2ts"};
    const auto extension = info.suffix().toLower();
    switch (kind) {
    case MediaKind::Photo:
        return photos.contains(extension);
    case MediaKind::Music:
        return music.contains(extension);
    case MediaKind::Video:
        return videos.contains(extension);
    }
    return false;
}

QString failure(const QString& path, const std::error_code& error) {
    return QStringLiteral("Unable to read %1: %2").arg(path, QString::fromStdString(error.message()));
}

ScanResult scanDirectory(const QString& requestedPath, MediaKind kind,
                         const std::shared_ptr<std::atomic_bool>& cancelled) {
    ScanResult result;
    result.path = requestedPath;
    try {
        if (cancelled->load(std::memory_order_relaxed)) return result;
        if (requestedPath.trimmed().isEmpty()) {
            result.error = QStringLiteral("No media folder was selected.");
            return result;
        }
        const QFileInfo root(requestedPath);
        result.path = root.canonicalFilePath();
        if (result.path.isEmpty()) result.path = QDir::cleanPath(root.absoluteFilePath());
        std::error_code error;
        std::filesystem::directory_iterator iterator(filesystemPath(result.path), error);
        if (error) {
            result.error = failure(result.path, error);
            return result;
        }
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            if (cancelled->load(std::memory_order_relaxed)) return result;
            const auto entryPath = qtPath(iterator->path());
            const QFileInfo info(entryPath);
            const bool directory = iterator->is_directory(error);
            if (error) {
                if (result.error.isEmpty()) result.error = failure(entryPath, error);
                error.clear();
            } else {
                const bool regular = directory || iterator->is_regular_file(error);
                if (error) {
                    if (result.error.isEmpty()) result.error = failure(entryPath, error);
                    error.clear();
                } else if (regular && !info.isHidden() && (directory || accepts(info, kind))) {
                    if (result.entries.size() == MediaLibrary::MaximumEntries) {
                        result.truncated = true;
                        break;
                    }
                    // Resolve directory links so Back/history can use a stable
                    // target. Keep file names as displayed, including symlinks.
                    auto path = directory ? info.canonicalFilePath() : QString{};
                    if (path.isEmpty()) path = QDir::cleanPath(info.absoluteFilePath());
                    result.entries.push_back({std::move(path), info.fileName(), directory,
                                              directory ? 0 : info.size(), info.lastModified()});
                }
            }
            iterator.increment(error);
            if (error) {
                if (result.error.isEmpty()) result.error = failure(result.path, error);
                break;
            }
        }
        if (cancelled->load(std::memory_order_relaxed)) return result;
        std::sort(result.entries.begin(), result.entries.end(), [](const MediaEntry& left, const MediaEntry& right) {
            if (left.isDirectory != right.isDirectory) return left.isDirectory;
            const int order = QString::compare(left.title, right.title, Qt::CaseInsensitive);
            if (order != 0) return order < 0;
            return left.path < right.path;
        });
    } catch (const std::exception& error) {
        result.error = QStringLiteral("Unable to read %1: %2").arg(result.path, QString::fromUtf8(error.what()));
    } catch (...) {
        result.error = QStringLiteral("Unable to read %1: unexpected directory scan failure.").arg(result.path);
    }
    return result;
}

}

MediaLibrary::MediaLibrary(QObject* parent) : QObject(parent) {
    qRegisterMetaType<MediaKind>();
    qRegisterMetaType<MediaEntry>();
    qRegisterMetaType<MediaEntries>();
}

MediaLibrary::~MediaLibrary() {
    if (cancellation_) cancellation_->store(true, std::memory_order_relaxed);
    // Watchers are QObject children. Destroying them disconnects callbacks; the
    // worker owns only its value arguments/token and never dereferences this.
}

void MediaLibrary::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (cancellation_) cancellation_->store(true, std::memory_order_relaxed);
    cancellation_.reset();
    activeRequestId_ = 0;
}

quint64 MediaLibrary::scan(QString path, MediaKind kind) {
    Q_ASSERT(QThread::currentThread() == thread());
    cancel();
    if (nextRequestId_ == std::numeric_limits<quint64>::max()) nextRequestId_ = 0;
    const quint64 requestId = ++nextRequestId_;
    activeRequestId_ = requestId;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    cancellation_ = cancellation;
    auto* watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this,
            [this, watcher, cancellation, requestId] {
        watcher->deleteLater();
        if (cancellation->load(std::memory_order_relaxed) ||
            cancellation_ != cancellation || activeRequestId_ != requestId) return;
        auto result = watcher->result();
        // Clear ownership before emitting: a consumer may synchronously start a
        // new scan (or destroy the service) in its loaded handler.
        cancellation_.reset();
        activeRequestId_ = 0;
        emit loaded(requestId, std::move(result.path), std::move(result.entries),
                    std::move(result.error), result.truncated);
    });
    watcher->setFuture(QtConcurrent::run([path = std::move(path), kind, cancellation] {
        return scanDirectory(path, kind, cancellation);
    }));
    return requestId;
}

}
