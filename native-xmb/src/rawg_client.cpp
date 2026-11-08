#include "rawg_client.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace xmb {
namespace {

constexpr int RequestDeadlineMs = 12000;
constexpr qsizetype MaxMetadataBytes = 2 * 1024 * 1024;
constexpr qsizetype MaxImageBytes = 8 * 1024 * 1024;
constexpr int MaxRedirects = 2;
constexpr int MaxGames = 12;
constexpr int MaxDimension = 8192;
constexpr quint64 MaxPixels = 32ULL * 1024ULL * 1024ULL;
constexpr QSize MaxArtworkSize(3840, 2160);
constexpr QSize MaxPreviewSize(640, 360);

bool validApiKey(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{16,128}$"));
    return pattern.match(value).hasMatch();
}

QString configuredApiKey() {
    const QString environment = QString::fromUtf8(qgetenv("RAWG_API_KEY"));
    if (validApiKey(environment)) return environment;
    QFile file(RawgClient::apiKeyFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.read(257);
    if (bytes.isEmpty() || bytes.size() > 256 || file.error() != QFileDevice::NoError) return {};
    const QString candidate = QString::fromUtf8(bytes);
    return validApiKey(candidate) ? candidate : QString{};
}

bool hasUserInfo(const QUrl& url) {
    return !url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@'));
}

bool secure443(const QUrl& url) {
    return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           !hasUserInfo(url) && (url.port(-1) == -1 || url.port(-1) == 443);
}

bool validMetadataUrl(const QUrl& url) {
    return secure443(url) && url.host().compare(QStringLiteral("api.rawg.io"), Qt::CaseInsensitive) == 0;
}

bool validImageUrl(const QUrl& url) {
    const QString host = url.host().toLower();
    return secure443(url) && host == QStringLiteral("media.rawg.io");
}

QString boundedString(const QJsonValue& value, int maxLength) {
    if (!value.isString()) return {};
    return value.toString().trimmed().left(maxLength);
}

QStringList parseNames(const QJsonValue& value, int countLimit, bool nestedPlatform = false) {
    QStringList result;
    if (!value.isArray()) return result;
    const QJsonArray entries = value.toArray();
    for (const QJsonValue& item : entries) {
        const QJsonObject object = nestedPlatform ? item.toObject().value("platform").toObject() : item.toObject();
        const QString name = boundedString(object.value(QStringLiteral("name")), 64);
        if (!name.isEmpty() && !result.contains(name, Qt::CaseInsensitive)) result.append(name);
        if (result.size() >= countLimit) break;
    }
    return result;
}

QVector<RawgGame> parseGames(const QJsonArray& results) {
    QVector<RawgGame> games;
    for (const QJsonValue& value : results) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        if (!idValue.isDouble()) continue;
        const double rawId = idValue.toDouble();
        if (rawId < 1 || rawId > 2147483647 || rawId != qFloor(rawId)) continue;
        RawgGame game;
        game.id = static_cast<int>(rawId);
        game.name = boundedString(object.value(QStringLiteral("name")), 120);
        game.slug = boundedString(object.value(QStringLiteral("slug")), 128);
        if (game.name.isEmpty() || game.slug.isEmpty()) continue;
        static const QRegularExpression slugPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$"));
        if (!slugPattern.match(game.slug).hasMatch()) continue;
        game.released = boundedString(object.value(QStringLiteral("released")), 20);
        game.platforms = parseNames(object.value(QStringLiteral("platforms")), 8, true);
        game.genres = parseNames(object.value(QStringLiteral("genres")), 8);
        const QString image = boundedString(object.value(QStringLiteral("background_image")), 2048);
        if (!image.isEmpty()) {
            const QUrl imageUrl(image, QUrl::StrictMode);
            if (validImageUrl(imageUrl)) game.imageUrl = imageUrl;
        }
        game.pageUrl = QUrl(QStringLiteral("https://rawg.io/games/%1").arg(game.slug));
        games.append(std::move(game));
        if (games.size() >= MaxGames) break;
    }
    return games;
}

struct DecodedArtwork {
    QImage image;
    QByteArray cacheBytes;
};

DecodedArtwork decodeArtwork(QByteArray bytes) {
    DecodedArtwork result;
    if (bytes.isEmpty() || bytes.size() > MaxImageBytes) return result;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return result;
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() > MaxDimension || sourceSize.height() > MaxDimension ||
        quint64(sourceSize.width()) * quint64(sourceSize.height()) > MaxPixels) return result;
    const QSize requestedSize = sourceSize.scaled(MaxArtworkSize, Qt::KeepAspectRatio);
    if (sourceSize.width() > MaxArtworkSize.width() || sourceSize.height() > MaxArtworkSize.height())
        reader.setScaledSize(requestedSize);
    QImage image = reader.read();
    if (image.isNull() || image.width() > MaxDimension || image.height() > MaxDimension ||
        quint64(image.width()) * quint64(image.height()) > MaxPixels) return result;
    if (image.width() > MaxArtworkSize.width() || image.height() > MaxArtworkSize.height())
        image = image.scaled(MaxArtworkSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (image.isNull()) return result;
    QByteArray encoded;
    QBuffer out(&encoded);
    if (!out.open(QIODevice::WriteOnly)) return result;
    QImageWriter writer(&out, "jpg");
    writer.setQuality(84);
    if (!writer.write(image) || encoded.isEmpty() || encoded.size() > MaxImageBytes) return result;
    result.image = std::move(image);
    result.cacheBytes = std::move(encoded);
    return result;
}

QImage readCachedPreview(const QString& path) {
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > MaxArtworkSize.width() || size.height() > MaxArtworkSize.height() ||
        quint64(size.width()) * quint64(size.height()) > MaxPixels) return {};
    const QSize requested = size.scaled(MaxPreviewSize, Qt::KeepAspectRatio);
    if (requested.isValid() && requested != size) reader.setScaledSize(requested);
    QImage image = reader.read();
    if (image.isNull()) return {};
    if (image.width() > MaxPreviewSize.width() || image.height() > MaxPreviewSize.height())
        image = image.scaled(MaxPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace

RawgClient::RawgClient(QString cacheDirectory, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), cacheDirectory_(QDir::cleanPath(QDir(cacheDirectory).absolutePath())),
      network_(network ? network : new QNetworkAccessManager(this)), deadline_(new QTimer(this)) {
    deadline_->setSingleShot(true);
    connect(deadline_, &QTimer::timeout, this, [this] {
        if (reply_) reply_->abort();
    });
}

RawgClient::~RawgClient() { cancel(); }

QString RawgClient::apiKeyFilePath() {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) return {};
    return QDir(appData).filePath(QStringLiteral("rawg_api_key"));
}

bool RawgClient::hasApiKey() { return !configuredApiKey().isEmpty(); }
bool RawgClient::hasEnvironmentKey() {
    return validApiKey(QString::fromUtf8(qgetenv("RAWG_API_KEY")));
}

bool RawgClient::saveApiKey(const QString& apiKey, QString& error) {
    error.clear();
    if (!validApiKey(apiKey)) {
        error = QStringLiteral("Enter a valid RAWG API key (16–128 letters, digits, _ or -).");
        return false;
    }
    const QString path = apiKeyFilePath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QStringLiteral("Could not access the private application settings folder.");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(apiKey.toUtf8()) != apiKey.size() || !file.commit()) {
        file.cancelWriting();
        error = QStringLiteral("Could not save the RAWG API key securely.");
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void RawgClient::search(const QString& rawQuery) {
    cancel();
    const quint64 generation = generation_;
    const QString queryText = rawQuery.trimmed().left(120);
    if (queryText.isEmpty()) {
        emit searchReady({});
        return;
    }
    const QString key = configuredApiKey();
    if (key.isEmpty()) {
        emit failed(QStringLiteral("Add a RAWG API key in settings to search RAWG. Cached artwork remains available."));
        return;
    }
    QUrl url(QStringLiteral("https://api.rawg.io/api/games"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    query.addQueryItem(QStringLiteral("search"), queryText);
    query.addQueryItem(QStringLiteral("page_size"), QString::number(MaxGames));
    query.addQueryItem(QStringLiteral("search_precise"), QStringLiteral("true"));
    url.setQuery(query);
    beginRequest(url, RequestRole::Metadata, generation);
}

void RawgClient::downloadArtwork(const RawgGame& game) {
    cancel();
    const quint64 generation = generation_;
    const QString path = artworkCachePath(game);
    if (game.id <= 0 || path.isEmpty() || !validImageUrl(game.imageUrl)) {
        failIfCurrent(generation, QStringLiteral("This RAWG result has no usable artwork image."));
        return;
    }
    artworkGame_ = game;
    const QFileInfo info(path);
    if (info.isFile() && info.size() > 0 && info.size() <= MaxImageBytes) {
        auto* watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, game, path, generation] {
            const QImage preview = watcher->result();
            watcher->deleteLater();
            if (generation != generation_) return;
            if (!preview.isNull()) emit artworkReady(game, path, preview);
            else {
                QFile::remove(path);
                beginRequest(game.imageUrl, RequestRole::Image, generation);
            }
        });
        watcher->setFuture(QtConcurrent::run([path] { return readCachedPreview(path); }));
        return;
    }
    beginRequest(game.imageUrl, RequestRole::Image, generation);
}

void RawgClient::cancel() {
    ++generation_;
    deadline_->stop();
    response_.clear();
    QNetworkReply* active = reply_;
    reply_ = nullptr;
    if (active) {
        active->abort();
        active->deleteLater();
    }
}

void RawgClient::beginRequest(const QUrl& url, RequestRole role, quint64 generation, int redirects) {
    if (generation != generation_) return;
    const bool allowed = role == RequestRole::Metadata ? validMetadataUrl(url) : validImageUrl(url);
    if (!allowed || redirects > MaxRedirects) {
        failIfCurrent(generation, QStringLiteral("RAWG returned an unsupported secure address."));
        return;
    }
    if (reply_) {
        QNetworkReply* old = reply_;
        reply_ = nullptr;
        old->abort();
        old->deleteLater();
    }
    response_.clear();
    QNetworkRequest request(url);
    request.setTransferTimeout(RequestDeadlineMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", role == RequestRole::Metadata ? "application/json" : "image/avif,image/webp,image/*");
    request.setRawHeader("User-Agent", "XMB RAWG artwork client");
    reply_ = network_->get(request);
    QNetworkReply* current = reply_;
    current->setReadBufferSize((role == RequestRole::Metadata ? MaxMetadataBytes : MaxImageBytes) + 1);
    response_.reserve(role == RequestRole::Metadata ? 16 * 1024 : 128 * 1024);
    deadline_->start(RequestDeadlineMs);
    connect(current, &QIODevice::readyRead, this, [this, current, role, generation] {
        if (current != reply_ || generation != generation_) return;
        const qsizetype limit = role == RequestRole::Metadata ? MaxMetadataBytes : MaxImageBytes;
        const qint64 remaining = limit + 1 - response_.size();
        if (remaining <= 0) { current->abort(); return; }
        response_.append(current->read(remaining));
        if (response_.size() > limit) current->abort();
    });
    connect(current, &QNetworkReply::finished, this, [this, current, role, generation, redirects, url] {
        finishRequest(current, role, generation, redirects, url);
    });
}

void RawgClient::finishRequest(QNetworkReply* reply, RequestRole role, quint64 generation,
                               int redirects, const QUrl& requestUrl) {
    if (reply != reply_ || generation != generation_) { reply->deleteLater(); return; }
    deadline_->stop();
    const qsizetype limit = role == RequestRole::Metadata ? MaxMetadataBytes : MaxImageBytes;
    const qint64 remaining = limit + 1 - response_.size();
    if (remaining > 0) response_.append(reply->read(remaining));
    const QByteArray bytes = response_;
    response_.clear();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QVariant redirectAttribute = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply_ = nullptr;
    reply->deleteLater();
    if (bytes.size() > limit) {
        failIfCurrent(generation, QStringLiteral("RAWG response exceeded the allowed size."));
        return;
    }
    if (redirectAttribute.isValid()) {
        if (redirects >= MaxRedirects) {
            failIfCurrent(generation, QStringLiteral("RAWG returned too many redirects."));
            return;
        }
        QUrl target = requestUrl.resolved(redirectAttribute.toUrl());
        if (role == RequestRole::Metadata) {
            QUrlQuery query(target);
            const auto items = query.queryItems(QUrl::FullyDecoded);
            query.clear();
            for (const auto& item : items)
                if (item.first.compare(QStringLiteral("key"), Qt::CaseInsensitive) != 0)
                    query.addQueryItem(item.first, item.second);
            target.setQuery(query);
        }
        beginRequest(target, role, generation, redirects + 1); // API key is never forwarded.
        return;
    }
    if (status == 401 || status == 403) {
        failIfCurrent(generation, QStringLiteral("RAWG denied access. Check your RAWG API key."));
        return;
    }
    if (status == 429) {
        failIfCurrent(generation, QStringLiteral("RAWG's request limit was reached. Try again later."));
        return;
    }
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300 || bytes.isEmpty()) {
        failIfCurrent(generation, role == RequestRole::Metadata
            ? QStringLiteral("RAWG search failed. Check the API key and try again.")
            : QStringLiteral("RAWG artwork could not be downloaded."));
        return;
    }
    if (role == RequestRole::Metadata) finishMetadata(bytes, generation);
    else finishImage(bytes, generation);
}

void RawgClient::finishMetadata(const QByteArray& bytes, quint64 generation) {
    if (generation != generation_) return;
    const auto results = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("results"));
    if (!results.isArray()) {
        failIfCurrent(generation, QStringLiteral("RAWG returned an unreadable game list."));
        return;
    }
    emit searchReady(parseGames(results.toArray()));
}

void RawgClient::finishImage(const QByteArray& bytes, quint64 generation) {
    if (generation != generation_) return;
    const RawgGame game = artworkGame_;
    const QString path = artworkCachePath(game);
    auto* watcher = new QFutureWatcher<DecodedArtwork>(this);
    connect(watcher, &QFutureWatcher<DecodedArtwork>::finished, this, [this, watcher, game, path, generation] {
        const DecodedArtwork decoded = watcher->result();
        watcher->deleteLater();
        if (generation != generation_) return;
        if (decoded.image.isNull() || decoded.cacheBytes.isEmpty()) {
            failIfCurrent(generation, QStringLiteral("RAWG returned an invalid artwork image."));
            return;
        }
        const QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath())) {
            failIfCurrent(generation, QStringLiteral("Could not create the RAWG artwork cache."));
            return;
        }
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(decoded.cacheBytes) != decoded.cacheBytes.size() ||
            !file.commit()) {
            file.cancelWriting();
            failIfCurrent(generation, QStringLiteral("Could not save RAWG artwork to the cache."));
            return;
        }
        QImage preview = decoded.image.scaled(MaxPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        emit artworkReady(game, path, preview);
    });
    watcher->setFuture(QtConcurrent::run([bytes] { return decodeArtwork(bytes); }));
}

QString RawgClient::artworkCachePath(const RawgGame& game) const {
    if (game.id <= 0 || cacheDirectory_.isEmpty() || !validImageUrl(game.imageUrl)) return {};
    const QByteArray digest = QCryptographicHash::hash(game.imageUrl.toEncoded(QUrl::FullyEncoded),
                                                        QCryptographicHash::Sha256).toHex().left(20);
    return QDir(cacheDirectory_).filePath(QStringLiteral("rawg/%1_%2.jpg").arg(game.id).arg(QString::fromLatin1(digest)));
}

void RawgClient::failIfCurrent(quint64 generation, const QString& message) {
    if (generation == generation_) emit failed(message);
}

} // namespace xmb
