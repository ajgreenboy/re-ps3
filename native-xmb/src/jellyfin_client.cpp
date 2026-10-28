#include "jellyfin_client.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dpapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "crypt32.lib")
#endif
#endif

namespace xmb {
namespace {
constexpr int RequestTimeoutMs = 15000;
constexpr qsizetype MaxResponseBytes = 4 * 1024 * 1024;
constexpr qsizetype MaxImageBytes = 4 * 1024 * 1024;
constexpr int PageLimit = 200;
// Artwork is bounded on both sides of the network: the server is asked for a small picture, and
// whatever arrives is refused before decode if its declared size is unreasonable.
constexpr int PosterMaxHeight = 480;
constexpr int BackdropMaxWidth = 640;
constexpr int ImageDecodeLimit = 1024;
constexpr int ImageCacheFiles = 400;

bool validImageKey(const QString& value) {
    if (value.isEmpty() || value.size() > 128) return false;
    for (const QChar ch : value) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_')) return false;
    }
    return true;
}

// Bounded decode: the byte length is already capped by the caller; this refuses an absurd pixel
// count before allocating it and scales anything over the limit down before reading.
QImage decodeBoundedArtwork(const QByteArray& bytes) {
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0 ||
        size.width() > 4096 || size.height() > 4096) return {};
    if (size.width() > ImageDecodeLimit || size.height() > ImageDecodeLimit)
        reader.setScaledSize(size.scaled(ImageDecodeLimit, ImageDecodeLimit, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull() || image.width() > ImageDecodeLimit || image.height() > ImageDecodeLimit) return {};
    return image;
}

#ifdef Q_OS_WIN
QByteArray protectForUser(const QByteArray& plain) {
    const QByteArray entropy = QByteArrayLiteral("xmb-native-jellyfin-session-v1");
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    if (!CryptProtectData(&in, L"XMB Jellyfin session", &extra, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotectForUser(const QByteArray& sealed) {
    const QByteArray entropy = QByteArrayLiteral("xmb-native-jellyfin-session-v1");
    DATA_BLOB in{static_cast<DWORD>(sealed.size()), reinterpret_cast<BYTE*>(const_cast<char*>(sealed.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    if (!CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
#else
QByteArray protectForUser(const QByteArray&) { return {}; }
QByteArray unprotectForUser(const QByteArray&) { return {}; }
#endif

bool safeToken(const QString& token) {
    if (token.isEmpty() || token.size() > 4096) return false;
    for (const QChar ch : token) {
        if (ch.isNull() || ch.category() == QChar::Other_Control ||
            ch == QLatin1Char('"') || ch == QLatin1Char('\\')) return false;
    }
    return true;
}

QByteArray authValue(const QByteArray& token, const QString& deviceId) {
    QByteArray value = QByteArrayLiteral("MediaBrowser Client=\"XMB\", Device=\"XMB Native\", DeviceId=\"") +
                       deviceId.toUtf8() + QByteArrayLiteral("\", Version=\"1.0\"");
    if (!token.isEmpty()) value += QByteArrayLiteral(", Token=\"") + token + QByteArrayLiteral("\"");
    return value;
}

QUrl withoutSecrets(QUrl url) {
    QUrlQuery source(url);
    const auto entries = source.queryItems(QUrl::FullyDecoded);
    QUrlQuery clean;
    for (const auto& entry : entries) {
        const QString key = entry.first.toLower();
        if (key == QLatin1String("apikey") || key == QLatin1String("api_key") || key == QLatin1String("token") ||
            key == QLatin1String("access_token")) continue;
        clean.addQueryItem(entry.first, entry.second);
    }
    url.setQuery(clean);
    url.setFragment({});
    return url;
}

bool validId(const QString& value) {
    if (value.isEmpty() || value.size() > 128) return false;
    for (const QChar ch : value) {
        if (ch.isNull() || ch.category() == QChar::Other_Control ||
            ch == QLatin1Char('/') || ch == QLatin1Char('\\') ||
            ch == QLatin1Char('%') || ch == QLatin1Char('?') || ch == QLatin1Char('#')) return false;
    }
    return value != QLatin1String(".") && value != QLatin1String("..");
}

QString typeName(const QJsonObject& item) { return item.value(QStringLiteral("Type")).toString(); }

qint64 millisecondsFromTicks(const QJsonValue& value) {
    if (!value.isDouble()) return 0;
    const double ticks = value.toDouble(-1.0);
    // JSON numbers are doubles; reject non-finite, negative, and values outside
    // qint64 before converting, then truncate sub-millisecond ticks.
    if (!std::isfinite(ticks) || ticks < 0.0 ||
        ticks >= static_cast<double>(std::numeric_limits<qint64>::max())) return 0;
    return static_cast<qint64>(ticks / 10000.0);
}

JellyfinItem parseItem(const QJsonObject& json) {
    JellyfinItem result;
    result.id = json.value(QStringLiteral("Id")).toString();
    result.title = json.value(QStringLiteral("Name")).toString();
    result.type = typeName(json);
    result.overview = json.value(QStringLiteral("Overview")).toString();
    result.folder = json.value(QStringLiteral("IsFolder")).toBool();
    result.durationMs = millisecondsFromTicks(json.value(QStringLiteral("RunTimeTicks")));
    const QJsonObject userData = json.value(QStringLiteral("UserData")).toObject();
    result.played = userData.value(QStringLiteral("Played")).toBool(false);
    result.resumeMs = millisecondsFromTicks(userData.value(QStringLiteral("PlaybackPositionTicks")));
    if (result.played || (result.durationMs > 0 && result.resumeMs >= result.durationMs))
        result.resumeMs = 0;
    // Image tags are the server's own version stamp for the picture: they key the local cache, so
    // replacing a poster on the server replaces it here instead of replaying the old file.
    const QJsonObject tags = json.value(QStringLiteral("ImageTags")).toObject();
    const QString primary = tags.value(QStringLiteral("Primary")).toString();
    if (validImageKey(primary)) result.primaryTag = primary;
    const QJsonArray backdrops = json.value(QStringLiteral("BackdropImageTags")).toArray();
    const QString backdrop = backdrops.isEmpty() ? QString{} : backdrops.first().toString();
    if (validImageKey(backdrop)) result.backdropTag = backdrop;
    return result;
}
}

JellyfinClient::JellyfinClient(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)),
      deviceId_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    qRegisterMetaType<xmb::JellyfinItem>();
    qRegisterMetaType<xmb::JellyfinPage>();
    qRegisterMetaType<xmb::JellyfinPlayback>();
}

JellyfinClient::~JellyfinClient() {
    destroying_=true;
    ++generation_;
    cancelRequests(true);
    accessToken_.fill('\0');
}

bool JellyfinClient::signedIn() const { return !accessToken_.isEmpty() && !userId_.isEmpty(); }
QUrl JellyfinClient::serverUrl() const { return server_; }
QByteArray JellyfinClient::authorizationHeader() const {
    return authValue(safeToken(QString::fromUtf8(accessToken_)) ? accessToken_ : QByteArray{}, deviceId_);
}
QString JellyfinClient::userId() const { return userId_; }
bool JellyfinClient::isCurrent(quint64 generation) const { return generation == generation_; }

void JellyfinClient::cancelRequests(bool includeReports) {
    QVector<QNetworkReply*> keep;
    const auto pending = replies_;
    for (auto* reply : pending) {
        if (!reply) continue;
        if (!includeReports && reportReplies_.contains(reply)) {
            keep.push_back(reply);
            continue;
        }
        reply->abort();
    }
    replies_ = std::move(keep);
    if (includeReports) reportReplies_.clear();
}

QUrl JellyfinClient::apiUrl(const QString& route) const {
    QUrl result = server_;
    QString path = result.path();
    if (path.isEmpty()) path = QStringLiteral("/");
    if (!path.endsWith(QLatin1Char('/'))) path += QLatin1Char('/');
    QString suffix = route;
    while (suffix.startsWith(QLatin1Char('/'))) suffix.remove(0, 1);
    result.setPath(path + suffix);
    result.setQuery(QString{});
    result.setFragment({});
    return result;
}

QNetworkReply* JellyfinClient::get(const QUrl& url) {
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authorizationHeader());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(RequestTimeoutMs);
    return network_->get(request);
}

QNetworkReply* JellyfinClient::post(const QUrl& url, const QByteArray& body) {
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authorizationHeader());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(RequestTimeoutMs);
    return network_->post(request, body);
}

QNetworkReply* JellyfinClient::deleteResource(const QUrl& url) {
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authorizationHeader());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(RequestTimeoutMs);
    return network_->deleteResource(request);
}

quint64 JellyfinClient::send(Operation operation, QNetworkReply* reply, QString context, bool audio) {
    const quint64 request = ++nextRequest_;
    const quint64 generation = generation_;
    replies_.push_back(reply);
    if (operation == Operation::Report) reportReplies_.insert(reply);
    reply->setReadBufferSize(MaxResponseBytes + 1);
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(RequestTimeoutMs);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > MaxResponseBytes) reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > MaxResponseBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, operation, request, generation,
                                                      context = std::move(context), audio]() mutable {
        finish(reply, operation, request, generation, std::move(context), audio);
    });
    return request;
}

void JellyfinClient::signIn(QUrl server, QString user, QString password) {
    // A fresh connection identity keeps a delayed old-session logout from revoking a new sign-in.
    deviceId_=QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++generation_;
    cancelRequests(false);
    accessToken_.fill('\0');
    accessToken_.clear();
    userId_.clear();
    server_ = {};
    server = server.adjusted(QUrl::NormalizePathSegments);
    const bool hasQueryOrFragment = server.hasQuery() || server.hasFragment();
    while (server.path().endsWith(QLatin1Char('/')) && server.path().size() > 1)
        server.setPath(server.path().left(server.path().size() - 1));
    const auto scheme = server.scheme().toLower();
    if (!server.isValid() || server.host().isEmpty() ||
        (scheme != QLatin1String("http") && scheme != QLatin1String("https")) ||
        !server.userName().isEmpty() || !server.password().isEmpty() || hasQueryOrFragment ||
        user.trimmed().isEmpty()) {
        password.fill(QChar(0)); user.fill(QChar(0));
        fail(0, QStringLiteral("Enter a valid Jellyfin server and username."));
        return;
    }
    server.setQuery(QString{});
    server.setFragment(QString{});
    server_ = server;
    const quint64 generation = generation_;
    QJsonObject payload{{QStringLiteral("Username"), user}, {QStringLiteral("Pw"), password}};
    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    payload.insert(QStringLiteral("Pw"), QString());
    password.fill(QChar(0)); user.fill(QChar(0));
    QNetworkRequest request(apiUrl(QStringLiteral("Users/AuthenticateByName")));
    request.setRawHeader("Authorization", authValue({}, deviceId_));
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(RequestTimeoutMs);
    auto* reply = network_->post(request, body);
    body.fill('\0');
    const quint64 requestId = ++nextRequest_;
    replies_.push_back(reply);
    reply->setReadBufferSize(MaxResponseBytes + 1);
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(RequestTimeoutMs);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > MaxResponseBytes) reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > MaxResponseBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, generation] {
        replies_.removeAll(reply);
        if (!isCurrent(generation)) { reply->deleteLater(); return; }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 &&
                        bytes.size() <= MaxResponseBytes;
        reply->deleteLater();
        QJsonParseError parse{};
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        const QJsonObject root = document.object();
        const QString token = root.value(QStringLiteral("AccessToken")).toString();
        const QString id = root.value(QStringLiteral("User")).toObject().value(QStringLiteral("Id")).toString();
        if (!ok || parse.error != QJsonParseError::NoError || !safeToken(token) || !validId(id)) {
            fail(0, QStringLiteral("Jellyfin sign-in failed. Check the server, username, and password."));
            return;
        }
        accessToken_ = token.toUtf8();
        userId_ = id;
        Q_UNUSED(requestId)
        emit authenticated();
    });
}

void JellyfinClient::signOut() {
    if(signedIn())logouts_.push_back({apiUrl(QStringLiteral("Sessions/Logout")),authorizationHeader()});
    ++generation_;
    cancelRequests(false);
    accessToken_.fill('\0');
    accessToken_.clear();
    userId_.clear();
    server_ = {};
    flushLogouts();
}

quint64 JellyfinClient::views() {
    if (!signedIn()) { const quint64 request = ++nextRequest_; fail(request, QStringLiteral("Sign in to Jellyfin first.")); return request; }
    QUrl url = apiUrl(QStringLiteral("UserViews"));
    QUrlQuery query;query.addQueryItem(QStringLiteral("userId"),userId_);url.setQuery(query);
    return send(Operation::Views, get(url));
}

quint64 JellyfinClient::items(QString parentId, int startIndex) {
    if (!signedIn()) { const quint64 request = ++nextRequest_; fail(request, QStringLiteral("Sign in to Jellyfin first.")); return request; }
    if (startIndex < 0 || !validId(parentId)) { const quint64 request = ++nextRequest_; fail(request, QStringLiteral("Choose a valid Jellyfin folder.")); return request; }
    QUrl url = apiUrl(QStringLiteral("Items"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ParentId"), parentId);
    query.addQueryItem(QStringLiteral("UserId"), userId_);
    query.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
    query.addQueryItem(QStringLiteral("Limit"), QString::number(PageLimit));
    query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    query.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
    url.setQuery(query);
    return send(Operation::Items, get(url), QString::number(startIndex));
}

quint64 JellyfinClient::search(QString term, int startIndex) {
    if (!signedIn()) { const quint64 request = ++nextRequest_; fail(request, QStringLiteral("Sign in to Jellyfin first.")); return request; }
    term = term.trimmed();
    bool printable = !term.isEmpty() && term.size() <= 200;
    for (const QChar ch : term) if (ch.isNull() || ch.category() == QChar::Other_Control) printable = false;
    if (!printable || startIndex < 0) {
        const quint64 request = ++nextRequest_;
        fail(request, QStringLiteral("Enter a search term."));
        return request;
    }
    QUrl url = apiUrl(QStringLiteral("Items"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("UserId"), userId_);
    query.addQueryItem(QStringLiteral("SearchTerm"), term);
    query.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
    query.addQueryItem(QStringLiteral("Limit"), QString::number(PageLimit));
    query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("IncludeItemTypes"),
                       QStringLiteral("Movie,Episode,Video,MusicVideo,Audio,Trailer,Recording,Series,Season"));
    query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    query.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
    url.setQuery(query);
    return send(Operation::Search, get(url), QString::number(startIndex));
}

quint64 JellyfinClient::playback(QString itemId, bool audio) {
    if (!signedIn()) { const quint64 request = ++nextRequest_; fail(request, QStringLiteral("Sign in to Jellyfin first.")); return request; }
    if (!validId(itemId)) { const quint64 request = ++nextRequest_; fail(request, QStringLiteral("Choose a valid Jellyfin item.")); return request; }
    const QJsonObject videoDirect{{QStringLiteral("Container"), QStringLiteral("mp4,mkv,webm")},
                                  {QStringLiteral("Type"), QStringLiteral("Video")},
                                  {QStringLiteral("VideoCodec"), QStringLiteral("h264,hevc")},
                                  {QStringLiteral("AudioCodec"), QStringLiteral("aac,ac3,mp3,opus")}};
    const QJsonObject audioDirect{{QStringLiteral("Container"), QStringLiteral("mp3,aac,m4a,flac,ogg")},
                                  {QStringLiteral("Type"), QStringLiteral("Audio")},
                                  {QStringLiteral("AudioCodec"), QStringLiteral("aac,mp3,opus,flac")}};
    const QJsonObject videoTranscode{{QStringLiteral("Container"), QStringLiteral("ts")},
        {QStringLiteral("Type"), QStringLiteral("Video")}, {QStringLiteral("VideoCodec"), QStringLiteral("h264")},
        {QStringLiteral("AudioCodec"), QStringLiteral("aac")}, {QStringLiteral("Protocol"), QStringLiteral("hls")},
        {QStringLiteral("Context"), QStringLiteral("Streaming")}, {QStringLiteral("MaxAudioChannels"), QStringLiteral("2")}};
    const QJsonObject audioTranscode{{QStringLiteral("Container"), QStringLiteral("ts")},
        {QStringLiteral("Type"), QStringLiteral("Audio")}, {QStringLiteral("AudioCodec"), QStringLiteral("aac")},
        {QStringLiteral("Protocol"), QStringLiteral("hls")}, {QStringLiteral("Context"), QStringLiteral("Streaming")}};
    const QJsonObject profile{{QStringLiteral("Name"), QStringLiteral("XMB Qt FFmpeg")},
        {QStringLiteral("MaxStreamingBitrate"), 20000000}, {QStringLiteral("MaxStaticBitrate"), 20000000},
        {QStringLiteral("MusicStreamingTranscodingBitrate"), 192000},
        {QStringLiteral("DeviceId"), deviceId_},
        {QStringLiteral("DirectPlayProfiles"), QJsonArray{audio ? audioDirect : videoDirect}},
        {QStringLiteral("TranscodingProfiles"), QJsonArray{audio ? audioTranscode : videoTranscode}}};
    const QJsonObject bodyObject{{QStringLiteral("DeviceId"), deviceId_},
        {QStringLiteral("DeviceProfile"), profile},
        {QStringLiteral("UserId"), userId_}, {QStringLiteral("EnableDirectPlay"), true},
        {QStringLiteral("EnableDirectStream"), true}, {QStringLiteral("EnableTranscoding"), true},
        {QStringLiteral("IsPlayback"), true}, {QStringLiteral("AutoOpenLiveStream"), true},
        {QStringLiteral("StartTimeTicks"), 0}};
    QByteArray body = QJsonDocument(bodyObject).toJson(QJsonDocument::Compact);
    auto* reply = post(apiUrl(QStringLiteral("Items/") + itemId + QStringLiteral("/PlaybackInfo")), body);
    body.fill('\0');
    return send(Operation::Playback, reply, itemId, audio);
}

void JellyfinClient::reportPlayback(const JellyfinPlayback& playbackInfo, qint64 positionMs,
                                    bool paused, QString event) {
    if (!signedIn() || !validId(playbackInfo.itemId) || positionMs < 0) return;
    event = event.trimmed().toLower();
    QString endpoint;
    if (event == QLatin1String("start")) endpoint = QStringLiteral("Sessions/Playing");
    else if (event == QLatin1String("progress")) endpoint = QStringLiteral("Sessions/Playing/Progress");
    else if (event == QLatin1String("stop")) endpoint = QStringLiteral("Sessions/Playing/Stopped");
    else return;
    const qint64 ticks = positionMs > std::numeric_limits<qint64>::max() / 10000
        ? std::numeric_limits<qint64>::max() : positionMs * 10000;
    QJsonObject body{{QStringLiteral("ItemId"), playbackInfo.itemId},
        {QStringLiteral("DeviceId"), deviceId_},
        {QStringLiteral("MediaSourceId"), playbackInfo.mediaSourceId},
        {QStringLiteral("PlaySessionId"), playbackInfo.playSessionId},
        {QStringLiteral("PlayMethod"), playbackInfo.playMethod},
        {QStringLiteral("PositionTicks"), ticks}, {QStringLiteral("IsPaused"), paused},
        {QStringLiteral("CanSeek"), true}, {QStringLiteral("IsMuted"), false},
        {QStringLiteral("VolumeLevel"), 100}, {QStringLiteral("PlaybackRate"), 1},
        {QStringLiteral("RepeatMode"), QStringLiteral("RepeatNone")}};
    QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto* reply = post(apiUrl(endpoint), bytes);
    bytes.fill('\0');
    send(Operation::Report, reply);
    if (event == QLatin1String("stop") && !playbackInfo.playSessionId.isEmpty()) {
        QUrl cleanup = apiUrl(QStringLiteral("Videos/ActiveEncodings"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("deviceId"), deviceId_);
        query.addQueryItem(QStringLiteral("playSessionId"), playbackInfo.playSessionId);
        cleanup.setQuery(query);
        send(Operation::Report, deleteResource(cleanup));
    }
}

void JellyfinClient::setImageCache(QString directory) {
    imageCache_ = std::move(directory);
    if (!imageCache_.isEmpty()) QDir().mkpath(imageCache_);
}

QString JellyfinClient::cachedImagePath(const QString& itemId, const QString& tag, bool backdrop) const {
    if (imageCache_.isEmpty() || !validId(itemId)) return {};
    const QByteArray stamp = QCryptographicHash::hash(tag.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    const QString name = itemId + (backdrop ? QStringLiteral("-b-") : QStringLiteral("-p-")) +
                         QString::fromLatin1(stamp) + QStringLiteral(".img");
    return QDir(imageCache_).filePath(name);
}

void JellyfinClient::writeImageCache(const QString& path, const QByteArray& bytes) const {
    if (path.isEmpty() || bytes.isEmpty() || bytes.size() > MaxImageBytes) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        QFile::remove(path);
        return;
    }
    trimImageCache();
}

// A poster per item accumulates for as long as the library is browsed. The cap is generous and
// the oldest files go first, so the cache stays a cache rather than a second library.
void JellyfinClient::trimImageCache() const {
    if (imageCache_.isEmpty()) return;
    QDir directory(imageCache_);
    const auto files = directory.entryInfoList({QStringLiteral("*.img")}, QDir::Files, QDir::Time);
    if (files.size() <= ImageCacheFiles) return;
    for (qsizetype i = ImageCacheFiles; i < files.size(); ++i) QFile::remove(files[i].absoluteFilePath());
}

quint64 JellyfinClient::artwork(QString itemId, QString tag, bool backdrop) {
    if (!signedIn() || !validId(itemId)) {
        const quint64 request = ++nextRequest_;
        QTimer::singleShot(0, this, [this, request, itemId, backdrop] {
            emit artworkReady(request, itemId, backdrop, QImage{});
        });
        return request;
    }
    if (!validImageKey(tag)) tag.clear();
    const QString cached = cachedImagePath(itemId, tag, backdrop);
    if (!cached.isEmpty()) {
        QFile file(cached);
        if (file.exists() && file.size() > 0 && file.size() <= MaxImageBytes && file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            file.close();
            const QImage image = decodeBoundedArtwork(bytes);
            if (!image.isNull()) {
                const quint64 request = ++nextRequest_;
                QTimer::singleShot(0, this, [this, request, itemId, backdrop, image] {
                    emit artworkReady(request, itemId, backdrop, image);
                });
                return request;
            }
            file.close(); // a truncated or corrupt cache entry is dropped and fetched again
            QFile::remove(cached);
        }
    }
    QUrl url = apiUrl(QStringLiteral("Items/") + itemId + QStringLiteral("/Images/") +
                      (backdrop ? QStringLiteral("Backdrop") : QStringLiteral("Primary")));
    QUrlQuery query;
    if (backdrop) query.addQueryItem(QStringLiteral("maxWidth"), QString::number(BackdropMaxWidth));
    else query.addQueryItem(QStringLiteral("maxHeight"), QString::number(PosterMaxHeight));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("80"));
    if (!tag.isEmpty()) query.addQueryItem(QStringLiteral("tag"), tag);
    url.setQuery(query);
    return send(Operation::Artwork, get(url), itemId + QLatin1Char('#') + (backdrop ? QLatin1Char('1') : QLatin1Char('0')) +
                QLatin1Char('#') + tag);
}

void JellyfinClient::setSessionStore(QString path) { sessionPath_ = std::move(path); }

bool JellyfinClient::hasStoredSession() const {
    if (sessionPath_.isEmpty()) return false;
    const QFileInfo info(sessionPath_);
    return info.isFile() && info.size() > 9 && info.size() <= 16 * 1024;
}

bool JellyfinClient::saveSession() {
    if (sessionPath_.isEmpty() || !signedIn()) return false;
    if (accessToken_.isEmpty() || accessToken_.size() > 4096) return false;
    QJsonObject record{{QStringLiteral("v"), 1},
                       {QStringLiteral("server"), server_.toString()},
                       {QStringLiteral("userId"), userId_},
                       {QStringLiteral("token"), QString::fromUtf8(accessToken_)}};
    QByteArray plain = QJsonDocument(record).toJson(QJsonDocument::Compact);
    record.insert(QStringLiteral("token"), QString{});
    const QByteArray sealed = protectForUser(plain);
    plain.fill('\0');
    if (sealed.isEmpty()) return false;
    const QFileInfo info(sessionPath_);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(sessionPath_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    static const QByteArray magic = QByteArrayLiteral("XMBJF1\n");
    if (file.write(magic) != magic.size() || file.write(sealed) != sealed.size() || !file.commit()) {
        QFile::remove(sessionPath_);
        return false;
    }
    return true;
}

bool JellyfinClient::restoreSession() {
    if (sessionPath_.isEmpty() || signedIn()) return false;
    QFile file(sessionPath_);
    if (!file.exists() || file.size() <= 9 || file.size() > 16 * 1024 || !file.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = file.readAll();
    file.close();
    static const QByteArray magic = QByteArrayLiteral("XMBJF1\n");
    if (!bytes.startsWith(magic)) { forgetSession(); return false; }
    QByteArray sealed = bytes.mid(magic.size());
    bytes.fill('\0');
    const QByteArray plain = unprotectForUser(sealed);
    sealed.fill('\0');
    if (plain.isEmpty()) return false; // the record belongs to another user or machine: leave it alone
    const auto document = QJsonDocument::fromJson(plain);
    const auto record = document.object();
    const QString token = record.value(QStringLiteral("token")).toString();
    const QString id = record.value(QStringLiteral("userId")).toString();
    const QUrl server(record.value(QStringLiteral("server")).toString());
    const auto scheme = server.scheme().toLower();
    if (!safeToken(token) || !validId(id) || !server.isValid() || server.host().isEmpty() ||
        (scheme != QLatin1String("http") && scheme != QLatin1String("https")) ||
        !server.userName().isEmpty() || server.hasQuery() || server.hasFragment()) {
        forgetSession();
        return false;
    }
    deviceId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++generation_;
    cancelRequests(false);
    accessToken_.fill('\0');
    accessToken_ = token.toUtf8();
    userId_ = id;
    server_ = server;
    return true;
}

void JellyfinClient::forgetSession() {
    if (sessionPath_.isEmpty()) return;
    QFile::remove(sessionPath_);
}

bool JellyfinClient::trustedServerUrl(const QUrl& target) const {
    if (!target.isValid() || target.scheme().toLower() != server_.scheme().toLower() ||
        target.host().compare(server_.host(), Qt::CaseInsensitive) != 0 ||
        target.port(target.scheme()=="https"?443:80) != server_.port(server_.scheme()=="https"?443:80) || !target.userName().isEmpty() || !target.password().isEmpty()) return false;
    QString prefix = server_.path();
    if (!prefix.isEmpty() && prefix != QLatin1String("/")) {
        while (prefix.endsWith(QLatin1Char('/'))) prefix.chop(1);
        if (target.path() != prefix && !target.path().startsWith(prefix + QLatin1Char('/'))) return false;
    }
    return true;
}

void JellyfinClient::finish(QNetworkReply* reply, Operation operation, quint64 request,
                            quint64 generation, QString context, bool audio) {
    replies_.removeAll(reply);
    reportReplies_.remove(reply);
    if (operation != Operation::Report && !isCurrent(generation)) { reply->deleteLater(); return; }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray bytes = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 &&
                    bytes.size() <= MaxResponseBytes;
    reply->deleteLater();
    if (operation == Operation::Report) {flushLogouts();return;}
    // Artwork missing or broken is not an error the viewer needs: the row keeps its built-in
    // glyph and the report is a null image.
    if (operation == Operation::Artwork) {
        const auto parts = context.split(QLatin1Char('#'));
        const QString itemId = parts.value(0);
        const bool backdrop = parts.value(1) == QLatin1String("1");
        const QString tag = parts.value(2);
        QImage image;
        if (ok && bytes.size() <= MaxImageBytes) {
            image = decodeBoundedArtwork(bytes);
            if (!image.isNull()) writeImageCache(cachedImagePath(itemId, tag, backdrop), bytes);
        }
        emit artworkReady(request, itemId, backdrop, image);
        return;
    }
    if (!ok) { fail(request, QStringLiteral("Jellyfin could not complete that request.")); return; }
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError) {
        fail(request, QStringLiteral("Jellyfin returned an unexpected response.")); return;
    }
    if (operation == Operation::Views || operation == Operation::Items || operation == Operation::Search) {
        JellyfinPage page;
        page.startIndex = operation == Operation::Items || operation == Operation::Search ? context.toInt() : 0;
        QJsonArray values;
        if (document.isArray()) values = document.array();
        else if (document.isObject()) {
            const auto root = document.object();
            values = root.value(QStringLiteral("Items")).toArray();
            page.startIndex = root.value(QStringLiteral("StartIndex")).toInt(page.startIndex);
            page.total = root.value(QStringLiteral("TotalRecordCount")).toInt(values.size());
        }
        if (page.total == 0) page.total = values.size();
        page.items.reserve(std::min<qsizetype>(values.size(), PageLimit));
        for (const auto& value : values) if (value.isObject() && page.items.size() < PageLimit) {
            auto item = parseItem(value.toObject());
            if (!validId(item.id)) continue;
            page.items.push_back(std::move(item));
        }
        emit pageReady(request, page);
        return;
    }
    if (operation == Operation::Playback) {
        const auto root = document.object();
        const QString session = root.value(QStringLiteral("PlaySessionId")).toString();
        const auto sources = root.value(QStringLiteral("MediaSources")).toArray();
        for (const auto& sourceValue : sources) {
            if (!sourceValue.isObject()) continue;
            const auto source = sourceValue.toObject();
            const QString sourceId = source.value(QStringLiteral("Id")).toString();
            const bool directPlay = source.value(QStringLiteral("SupportsDirectPlay")).toBool();
            const bool directStream = source.value(QStringLiteral("SupportsDirectStream")).toBool();
            QString method;
            QUrl stream;
            if (directPlay) {
                method = QStringLiteral("DirectPlay");
                stream = apiUrl((audio ? QStringLiteral("Audio/") : QStringLiteral("Videos/")) + context +
                                QStringLiteral("/stream"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("static"), QStringLiteral("true"));
                if (!sourceId.isEmpty()) query.addQueryItem(QStringLiteral("MediaSourceId"), sourceId);
                stream.setQuery(query);
            } else {
                method = directStream ? QStringLiteral("DirectStream") : QStringLiteral("Transcode");
                stream = QUrl(source.value(QStringLiteral("TranscodingUrl")).toString());
                if (stream.isEmpty() || !stream.isValid()) continue;
                if (stream.isRelative()) {
                    QUrl base = server_;
                    QString basePath = base.path();
                    if (!basePath.endsWith(QLatin1Char('/'))) basePath += QLatin1Char('/');
                    base.setPath(basePath);
                    stream = base.resolved(stream);
                }
                if (stream.isValid() && stream.scheme().compare(server_.scheme(), Qt::CaseInsensitive) == 0 &&
                    stream.host().compare(server_.host(), Qt::CaseInsensitive) == 0 &&
                    stream.port(-1) == server_.port(-1)) {
                    QString prefix = server_.path();
                    while (prefix.endsWith(QLatin1Char('/'))) prefix.chop(1);
                    if (!prefix.isEmpty() && prefix != QLatin1String("/") &&
                        stream.path() != prefix && !stream.path().startsWith(prefix + QLatin1Char('/'))) {
                        stream.setPath(prefix + (stream.path().startsWith(QLatin1Char('/'))
                            ? stream.path() : QLatin1Char('/') + stream.path()));
                    }
                }
            }
            stream = withoutSecrets(stream);
            if (!trustedServerUrl(stream)) continue;
            JellyfinPlayback result;
            result.url = stream;
            result.itemId = context;
            result.mediaSourceId = sourceId;
            result.playSessionId = session;
            result.playMethod = method;
            result.audio = audio;
            // The server's own stream list. What the player can actually switch to is decided
            // later against QMediaPlayer::audioTracks(), so this is a catalogue, not a promise.
            const QJsonArray streams = source.value(QStringLiteral("MediaStreams")).toArray();
            for (const auto& streamValue : streams) {
                if (!streamValue.isObject()) continue;
                const auto entry = streamValue.toObject();
                JellyfinTrack track;
                track.index = entry.value(QStringLiteral("Index")).toInt(-1);
                track.type = entry.value(QStringLiteral("Type")).toString();
                track.codec = entry.value(QStringLiteral("Codec")).toString();
                track.language = entry.value(QStringLiteral("Language")).toString();
                track.channels = entry.value(QStringLiteral("Channels")).toInt(0);
                track.external = entry.value(QStringLiteral("IsExternal")).toBool(false);
                track.isDefault = entry.value(QStringLiteral("IsDefault")).toBool(false);
                track.title = entry.value(QStringLiteral("DisplayTitle")).toString().trimmed();
                if (track.title.isEmpty()) track.title = track.language;
                if (track.title.isEmpty()) track.title = track.codec;
                if (track.type.isEmpty() || track.title.isEmpty()) continue;
                result.streams.push_back(track);
            }
            emit playbackReady(request, result);
            return;
        }
        fail(request, QStringLiteral("Jellyfin did not provide a compatible stream."));
    }
}

void JellyfinClient::flushLogouts() {
    if(destroying_ || !reportReplies_.empty() || logouts_.empty())return;
    const auto logout=logouts_.takeFirst();
    QNetworkRequest request(logout.url);request.setRawHeader("Authorization",logout.authorization);
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(RequestTimeoutMs);
    send(Operation::Report,network_->post(request,QByteArray("{}")));
}

void JellyfinClient::fail(quint64 request, const QString& message) {
    if (request == 0) { emit failed(request, message); return; }
    const quint64 generation = generation_;
    QTimer::singleShot(0, this, [this, request, message, generation] {
        if (isCurrent(generation)) emit failed(request, message);
    });
}

} // namespace xmb

