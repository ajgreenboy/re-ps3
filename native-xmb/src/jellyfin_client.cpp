#include "jellyfin_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>
#include <algorithm>
#include <limits>
#include <utility>

namespace xmb {
namespace {
constexpr int RequestTimeoutMs = 15000;
constexpr qsizetype MaxResponseBytes = 4 * 1024 * 1024;
constexpr int PageLimit = 200;

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
JellyfinItem parseItem(const QJsonObject& json) {
    JellyfinItem result;
    result.id = json.value(QStringLiteral("Id")).toString();
    result.title = json.value(QStringLiteral("Name")).toString();
    result.type = typeName(json);
    result.overview = json.value(QStringLiteral("Overview")).toString();
    result.folder = json.value(QStringLiteral("IsFolder")).toBool();
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
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
    url.setQuery(query);
    return send(Operation::Items, get(url), QString::number(startIndex));
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
    if (!ok) { fail(request, QStringLiteral("Jellyfin could not complete that request.")); return; }
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError) {
        fail(request, QStringLiteral("Jellyfin returned an unexpected response.")); return;
    }
    if (operation == Operation::Views || operation == Operation::Items) {
        JellyfinPage page;
        page.startIndex = operation == Operation::Items ? context.toInt() : 0;
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

