#include "rawg_artwork.h"
#include "rawg_client.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace xmb {
namespace {

constexpr int MissRetrySeconds = 7 * 24 * 60 * 60;
constexpr int FailureRetrySeconds = 5 * 60;
constexpr int RequestIntervalMs = 150;

QString normalized(QString name) {
    static const QRegularExpression marks(QStringLiteral("[™®©'’]"));
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{M}\\p{N}]+"));
    return name.remove(marks).normalized(QString::NormalizationForm_C).toCaseFolded()
        .replace(separators, QStringLiteral(" ")).simplified();
}

QString steamBaseTitle(const QString& title) {
    static const QRegularExpression modeSuffix(
        QStringLiteral(R"((?:\s*[-:–—]\s*|\s*\(\s*|\s+)(?:multi[- ]?player|single[- ]?player|zombies)\s*\)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = modeSuffix.match(title);
    return match.hasMatch() ? title.left(match.capturedStart()).trimmed() : QString{};
}

// Distinct RAWG ids can use the same name. Never break a tie using result order or artwork availability.
const RawgGame* uniqueMatch(const RawgGameList& games, const QString& name, bool& ambiguous) {
    const QString wanted = normalized(name);
    if (wanted.isEmpty()) return nullptr;
    static const QRegularExpression editionSuffix(
        QStringLiteral(R"(^ (?:19[0-9]{2}|20[0-9]{2}|remake|remastered|remaster)$)"));
    const RawgGame* found = nullptr;
    for (const RawgGame& game : games) {
        const QString candidate = normalized(game.name);
        if (candidate != wanted) {
            // A bare title such as "Prey" does not identify which same-name release is installed.
            if (candidate.startsWith(wanted) && editionSuffix.match(candidate.mid(wanted.size())).hasMatch())
                ambiguous = true;
            continue;
        }
        if (found && found->id != game.id) {
            ambiguous = true;
            return nullptr;
        }
        found = &game;
    }
    return ambiguous ? nullptr : found;
}

// Store only a client-generated image basename. A cache entry cannot select an arbitrary local file.
QString rawgDirectory(const QString& cacheDirectory, bool create = false) {
    if (cacheDirectory.isEmpty()) return {};
    const QDir root(cacheDirectory);
    const QFileInfo info(root.filePath(QStringLiteral("rawg")));
    if (info.isSymLink() || info.isJunction()) return {};
    if (!info.exists() && create && !root.mkpath(QStringLiteral("rawg"))) return {};
    const QString path = QFileInfo(info.absoluteFilePath()).canonicalFilePath();
    if (path.isEmpty() || QFileInfo(path).absolutePath() != root.canonicalPath()) return {};
    return path;
}

QString associationPath(const QString& directory, const QString& id) {
    const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(directory).filePath(QStringLiteral("target-%1.json").arg(QString::fromLatin1(hash)));
}

QJsonObject association(const QString& directory, const QString& id, const QString& title) {
    if (directory.isEmpty()) return {};
    QFile file(associationPath(directory, id));
    const QFileInfo info(file);
    if (!info.isFile() || info.isSymLink() || info.size() > 8192 || !file.open(QIODevice::ReadOnly)) return {};
    const auto entry = QJsonDocument::fromJson(file.read(8192)).object();
    return entry.value(QStringLiteral("title")).toString() == title ? entry : QJsonObject{};
}

bool saveAssociation(const QString& directory, const QString& id, const QJsonObject& entry) {
    if (directory.isEmpty()) return false;
    QSaveFile file(associationPath(directory, id));
    const QByteArray data = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QString safeImage(const QString& directory, const QString& name) {
    static const QRegularExpression filename(QStringLiteral("\\A[1-9][0-9]{0,9}_[0-9a-f]{20}\\.jpg\\z"));
    if (directory.isEmpty() || !filename.match(name).hasMatch()) return {};
    const QFileInfo info(QDir(directory).filePath(name));
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > 8 * 1024 * 1024) return {};
    const QString path = info.canonicalFilePath();
    if (path.isEmpty() || QFileInfo(path).absolutePath() != directory) return {};
    QImageReader reader(path, "jpeg");
    const QSize size = reader.size();
    return size.width() > 0 && size.height() > 0 && size.width() <= 3840 && size.height() <= 2160
        ? path : QString{};
}

} // namespace

RawgArtwork::RawgArtwork(QString cacheDirectory, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), cacheDirectory_(std::move(cacheDirectory)),
      client_(new RawgClient(cacheDirectory_, this, network)), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &RawgArtwork::next);
    connect(client_, &RawgClient::searchReady, this, [this](const RawgGameList& games) {
        if (active_.first.isEmpty()) return;
        bool ambiguous = false;
        const RawgGame* game = uniqueMatch(games, query_, ambiguous);
        if (!game && !ambiguous && query_ == active_.second && active_.first.startsWith(QStringLiteral("steam:"))) {
            const QString base = steamBaseTitle(query_);
            if (!base.isEmpty()) {
                game = uniqueMatch(games, base, ambiguous);
                if (!game && !ambiguous) {
                    query_ = base;
                    client_->search(query_);
                    return;
                }
            }
        }
        if (game && !game->imageUrl.isEmpty()) client_->downloadArtwork(*game);
        else deferTarget(MissRetrySeconds);
    });
    connect(client_, &RawgClient::artworkReady, this,
            [this](const RawgGame&, const QString& path, const QImage& preview) {
        if (active_.first.isEmpty()) return;
        const QString directory = rawgDirectory(cacheDirectory_);
        const QString name = QFileInfo(path).fileName();
        const QString trustedPath = safeImage(directory, name);
        if (trustedPath.isEmpty() || trustedPath != QFileInfo(path).canonicalFilePath() ||
            !saveAssociation(directory, active_.first, {{QStringLiteral("title"), active_.second},
                                                        {QStringLiteral("image"), name}})) {
            deferTarget(FailureRetrySeconds);
            return;
        }
        const Target completed = active_;
        active_ = {};
        delivered_.insert(completed);
        retryAfter_.remove(completed);
        timer_->start(RequestIntervalMs);
        emit ready(completed.first, completed.second, trustedPath, preview);
    });
    connect(client_, &RawgClient::failed, this, [this](const QString& safeError) {
        if (active_.first.isEmpty()) return;
        // A broken connection or invalid key affects every queued entry.
        networkRetryAfter_ = QDateTime::currentSecsSinceEpoch() + FailureRetrySeconds;
        needsNetwork_.append(active_);
        active_ = {};
        timer_->start(RequestIntervalMs);
        report(safeError);
    });
}

QString RawgArtwork::cachedArtwork(QString cacheDirectory, QString id, QString title) {
    const QString directory = rawgDirectory(cacheDirectory);
    const auto entry = association(directory, id, title);
    return safeImage(directory, entry.value(QStringLiteral("image")).toString());
}

void RawgArtwork::refresh(const QVector<Target>& targets) {
    pending_.clear();
    needsNetwork_.clear();
    QSet<QString> seen;
    bool keepActive = false;
    for (const Target& target : targets) {
        if (target.first.isEmpty() || target.second.trimmed().isEmpty() || seen.contains(target.first)) continue;
        seen.insert(target.first);
        if (target == active_) keepActive = true;
        else pending_.append(target);
    }
    if (!keepActive && !active_.first.isEmpty()) {
        ++generation_;
        active_ = {};
        client_->cancel();
    }
    if (!keepActive && !pending_.isEmpty() && (!timer_->isActive() || timer_->remainingTime() > RequestIntervalMs))
        timer_->start(0);
}

void RawgArtwork::next() {
    if (!active_.first.isEmpty()) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString directory = rawgDirectory(cacheDirectory_);
    QElapsedTimer slice;
    slice.start();
    while (!pending_.isEmpty()) {
        if (slice.elapsed() >= 3) {
            timer_->start(1);
            return;
        }
        const Target target = pending_.takeFirst();
        if (retryAfter_.value(target) > now) continue;
        const auto entry = association(directory, target.first, target.second);
        if (entry.value(QStringLiteral("retryAfter")).toInteger() > now) continue;
        const QString path = safeImage(directory, entry.value(QStringLiteral("image")).toString());
        if (path.isEmpty()) {
            delivered_.remove(target);
            needsNetwork_.append(target);
        } else if (!delivered_.contains(target)) {
            readPreview(target, path);
            return;
        }
    }
    if (networkRetryAfter_ > now) {
        if (!needsNetwork_.isEmpty())
            timer_->start(int(qMin<qint64>(FailureRetrySeconds, networkRetryAfter_ - now)) * 1000);
        return;
    }
    if (needsNetwork_.isEmpty()) {
        report(tr("Artwork scan complete."));
        return;
    }
    if (!RawgClient::hasApiKey()) {
        report(tr("Add a RAWG API key to fetch missing artwork."));
        return;
    }
    if (rawgDirectory(cacheDirectory_, true).isEmpty()) {
        report(tr("Could not access the RAWG artwork cache."));
        return;
    }
    active_ = needsNetwork_.takeFirst();
    query_ = active_.second;
    client_->search(query_);
}

void RawgArtwork::deferTarget(qint64 seconds) {
    const qint64 until = QDateTime::currentSecsSinceEpoch() + seconds;
    retryAfter_.insert(active_, until);
    if (seconds == MissRetrySeconds)
        saveAssociation(rawgDirectory(cacheDirectory_, true), active_.first,
                        {{QStringLiteral("title"), active_.second}, {QStringLiteral("retryAfter"), until}});
    active_ = {};
    timer_->start(RequestIntervalMs);
}

void RawgArtwork::readPreview(const Target& target, const QString& path) {
    active_ = target;
    const quint64 generation = generation_;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, generation, watcher, target, path] {
        const QImage preview = watcher->result();
        watcher->deleteLater();
        if (generation != generation_) return;
        active_ = {};
        timer_->start(0);
        if (preview.isNull()) {
            needsNetwork_.append(target); // RawgClient verifies and repairs corrupt cached images.
            return;
        }
        delivered_.insert(target);
        emit ready(target.first, target.second, path, preview);
    });
    watcher->setFuture(QtConcurrent::run([path] {
        QImageReader reader(path, "jpeg");
        const QSize size = reader.size();
        if (size.isEmpty() || size.width() > 3840 || size.height() > 2160) return QImage{};
        reader.setScaledSize(size.scaled(QSize(640, 360), Qt::KeepAspectRatio));
        return reader.read();
    }));
}

void RawgArtwork::resetBackoff() {
    networkRetryAfter_ = 0;
    retryAfter_.clear();
    lastStatus_.clear();
    if (active_.first.isEmpty()) timer_->start(0);
}

void RawgArtwork::report(const QString& message) {
    if (lastStatus_ == message) return;
    lastStatus_ = message;
    emit status(message);
}

} // namespace xmb
