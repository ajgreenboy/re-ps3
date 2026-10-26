#include "steam_artwork.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace xmb {
namespace {

constexpr int RequestDeadlineMs = 8000;
constexpr qsizetype MaximumMetadataBytes = 1024 * 1024;
constexpr qsizetype MaximumImageBytes = 5 * 1024 * 1024;
constexpr int MaximumRedirects = 3;
constexpr int MaximumAppIds = 32;
constexpr int MaximumImageDimension = 8192;
constexpr quint64 MaximumImagePixels = 64ULL * 1024ULL * 1024ULL;
constexpr QSize ArtworkSize(512, 288);

struct DecodeResult {
    QImage image;
};

bool validAppId(const QString& appId) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,12}$"));
    if (!digits.match(appId).hasMatch()) return false;
    bool valid = false;
    const qulonglong numeric = appId.toULongLong(&valid);
    return valid && numeric != 0;
}

bool hasUserInfo(const QUrl& url) {
    return !url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@'));
}

bool validMetadataUrl(const QUrl& url) {
    return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           url.host().compare(QStringLiteral("store.steampowered.com"), Qt::CaseInsensitive) == 0 &&
           !hasUserInfo(url) && (url.port(-1) == -1 || url.port(-1) == 443);
}

bool validImageUrl(const QUrl& url) {
    const QString host = url.host().toLower();
    const bool steamStaticHost = host == QStringLiteral("steamstatic.com") ||
        host.endsWith(QStringLiteral(".steamstatic.com"));
    return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           steamStaticHost && !hasUserInfo(url) && (url.port(-1) == -1 || url.port(-1) == 443);
}

bool allowedUrl(const QUrl& url, bool imageRequest) {
    return imageRequest ? validImageUrl(url) : validMetadataUrl(url);
}

DecodeResult decodeArtwork(const QByteArray& bytes, const QString& path,
                            quint64 generation, const std::shared_ptr<std::atomic<quint64>>& activeGeneration) {
    DecodeResult result;
    if (bytes.isEmpty() || bytes.size() > MaximumImageBytes) return result;

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return result;
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() > MaximumImageDimension ||
        sourceSize.height() > MaximumImageDimension ||
        quint64(sourceSize.width()) * quint64(sourceSize.height()) > MaximumImagePixels) return result;

    const QSize requestedSize = sourceSize.scaled(ArtworkSize, Qt::KeepAspectRatio);
    if (requestedSize.isValid() && requestedSize != sourceSize) reader.setScaledSize(requestedSize);
    QImage image = reader.read();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        image.width() > MaximumImageDimension || image.height() > MaximumImageDimension ||
        quint64(image.width()) * quint64(image.height()) > MaximumImagePixels) return result;
    if (image.width() > ArtworkSize.width() || image.height() > ArtworkSize.height())
        image = image.scaled(ArtworkSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (image.isNull()) return result;

    if (activeGeneration->load(std::memory_order_relaxed) != generation || path.isEmpty()) return result;
    const QFileInfo cacheInfo(path);
    if (!QDir().mkpath(cacheInfo.absolutePath())) {
        result.image = std::move(image);
        return result;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.image = std::move(image);
        return result;
    }
    if (file.write(bytes) != bytes.size() || activeGeneration->load(std::memory_order_relaxed) != generation ||
        !file.commit()) {
        file.cancelWriting();
        result.image = std::move(image);
        return result;
    }
    result.image = std::move(image);
    return result;
}

} // namespace

SteamArtwork::SteamArtwork(QString cacheDirectory, QObject* parent)
    : QObject(parent), cacheDirectory_(QDir::cleanPath(cacheDirectory.trimmed())),
      network_(new QNetworkAccessManager(this)),
      workerGeneration_(std::make_shared<std::atomic<quint64>>(generation_)) {}

SteamArtwork::~SteamArtwork() {
    ++generation_;
    workerGeneration_->store(generation_, std::memory_order_relaxed);
    pendingAppIds_.clear();
    cancelRequests();
}

QString SteamArtwork::cachePath(QString cacheDirectory, QString appId) {
    if (!validAppId(appId) || cacheDirectory.trimmed().isEmpty()) return {};
    return QDir::cleanPath(QDir(cacheDirectory).absoluteFilePath(appId + QStringLiteral(".image")));
}

bool SteamArtwork::isCurrent(quint64 generation) const {
    return generation == generation_;
}

void SteamArtwork::cancelRequests() {
    const auto pending = replies_;
    replies_.clear();
    for (auto* reply : pending)
        if (reply) reply->abort();
}

void SteamArtwork::refresh(QStringList appIds) {
    ++generation_;
    const quint64 generation = generation_;
    workerGeneration_->store(generation, std::memory_order_relaxed);
    cancelRequests();
    pendingAppIds_.clear();
    activeJobs_ = 0;

    QSet<QString> seen;
    for (const auto& candidate : appIds) {
        const QString appId = candidate.trimmed();
        if (!validAppId(appId) || seen.contains(appId)) continue;
        seen.insert(appId);
        pendingAppIds_.append(appId);
        if (pendingAppIds_.size() >= MaximumAppIds) break;
    }
    pump(generation);
}

void SteamArtwork::pump(quint64 generation) {
    if (!isCurrent(generation)) return;
    while (activeJobs_ < 2 && !pendingAppIds_.isEmpty()) {
        const QString appId = pendingAppIds_.takeFirst();
        ++activeJobs_;
        requestMetadata(appId, generation);
    }
}

void SteamArtwork::requestMetadata(const QString& appId, quint64 generation) {
    QUrl url(QStringLiteral("https://store.steampowered.com/api/appdetails"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("appids"), appId);
    query.addQueryItem(QStringLiteral("filters"), QStringLiteral("basic"));
    url.setQuery(query);
    get(appId, generation, url, false, 0, [this, appId, generation](const QByteArray& bytes, int status, int error) {
        if (!isCurrent(generation)) return;
        if (error != static_cast<int>(QNetworkReply::NoError) || status < 200 || status >= 300 || bytes.isEmpty()) {
            finishJob(appId, generation);
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            finishJob(appId, generation);
            return;
        }
        const QJsonObject app = document.object().value(appId).toObject();
        const QJsonObject data = app.value(QStringLiteral("data")).toObject();
        const QUrl imageUrl(data.value(QStringLiteral("header_image")).toString());
        if (!app.value(QStringLiteral("success")).toBool() || !validImageUrl(imageUrl)) {
            finishJob(appId, generation);
            return;
        }
        requestImage(appId, generation, imageUrl);
    });
}

void SteamArtwork::requestImage(const QString& appId, quint64 generation, const QUrl& url) {
    if (!validImageUrl(url)) {
        finishJob(appId, generation);
        return;
    }
    get(appId, generation, url, true, 0, [this, appId, generation](const QByteArray& bytes, int status, int error) {
        if (!isCurrent(generation)) return;
        if (error != static_cast<int>(QNetworkReply::NoError) || status < 200 || status >= 300 ||
            bytes.isEmpty() || bytes.size() > MaximumImageBytes) {
            finishJob(appId, generation);
            return;
        }
        decodeAndCache(appId, generation, bytes);
    });
}

void SteamArtwork::get(const QString& appId, quint64 generation, const QUrl& url, bool imageRequest,
                       int redirectCount, ResponseHandler handler) {
    if (!isCurrent(generation) || !allowedUrl(url, imageRequest)) {
        if (isCurrent(generation)) handler({}, 0, static_cast<int>(QNetworkReply::ProtocolFailure));
        return;
    }
    const qsizetype maximumBytes = imageRequest ? MaximumImageBytes : MaximumMetadataBytes;
    QNetworkRequest request(url);
    request.setTransferTimeout(RequestDeadlineMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    QNetworkReply* reply = network_->get(request);
    replies_.append(reply);
    reply->setReadBufferSize(maximumBytes + 1);
    auto responseBytes = std::make_shared<QByteArray>();
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(RequestDeadlineMs);
    connect(reply, &QNetworkReply::readyRead, this, [reply, responseBytes, maximumBytes] {
        const qint64 remaining = qint64(maximumBytes) + 1 - responseBytes->size();
        if (remaining <= 0) {
            reply->abort();
            return;
        }
        responseBytes->append(reply->read(remaining));
        if (responseBytes->size() > maximumBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, maximumBytes] {
        bool validLength = false;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&validLength);
        if (validLength && length > maximumBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, responseBytes, appId, generation, url, imageRequest, redirectCount,
             maximumBytes, handler = std::move(handler)]() mutable {
        replies_.removeAll(reply);
        const QByteArray tail = reply->readAll();
        const bool tooLarge = responseBytes->size() > maximumBytes ||
            tail.size() > qint64(maximumBytes) - responseBytes->size();
        if (!tooLarge) responseBytes->append(tail);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const int error = static_cast<int>(reply->error());
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        reply->deleteLater();
        if (!isCurrent(generation)) return;
        if (tooLarge) {
            handler({}, status, static_cast<int>(QNetworkReply::UnknownNetworkError));
            return;
        }
        if (redirect.isValid() && !redirect.isEmpty()) {
            const QUrl target = url.resolved(redirect);
            if (redirectCount >= MaximumRedirects || !allowedUrl(target, imageRequest)) {
                handler({}, status, static_cast<int>(QNetworkReply::ProtocolFailure));
                return;
            }
            get(appId, generation, target, imageRequest, redirectCount + 1, std::move(handler));
            return;
        }
        handler(*responseBytes, status, error);
    });
}

void SteamArtwork::decodeAndCache(const QString& appId, quint64 generation, QByteArray bytes) {
    const QString path = cachePath(cacheDirectory_, appId);
    const auto activeGeneration = workerGeneration_;
    auto* watcher = new QFutureWatcher<DecodeResult>(this);
    connect(watcher, &QFutureWatcher<DecodeResult>::finished, this, [this, watcher, appId, generation] {
        const DecodeResult result = watcher->result();
        watcher->deleteLater();
        if (!isCurrent(generation)) return;
        finishJob(appId, generation, result.image);
    });
    watcher->setFuture(QtConcurrent::run([bytes = std::move(bytes), path, generation, activeGeneration] {
        return decodeArtwork(bytes, path, generation, activeGeneration);
    }));
}

void SteamArtwork::finishJob(const QString& appId, quint64 generation, QImage image) {
    if (!isCurrent(generation)) return;
    if (activeJobs_ > 0) --activeJobs_;
    if (!image.isNull()) emit loaded(appId, std::move(image));
    if (isCurrent(generation)) pump(generation);
}

} // namespace xmb
