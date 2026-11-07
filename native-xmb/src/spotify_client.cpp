#include "spotify_client.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QtDebug>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace xmb {
namespace {
constexpr qint64 MaxResponseBytes = 4 * 1024 * 1024;
constexpr int RequestTimeoutMs = 15000;
constexpr int SignInTimeoutMs = 5 * 60 * 1000;
constexpr int MaxPages = 20;          // 20 x 50 playlists, 20 x 100 playlist tracks, 20 x 50 liked tracks
constexpr int MaxItems = 2000;
const char* const Scopes = "user-read-private playlist-read-private playlist-read-collaborative user-library-read "
                           "user-read-playback-state user-modify-playback-state user-read-currently-playing";

QByteArray randomToken(int length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QByteArray out(length, '\0');
    for (int i = 0; i < length; ++i) out[i] = alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))];
    return out;
}

bool validClientId(const QString& id) {
    if (id.size() != 32) return false;
    for (const QChar c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

// A base URL override is honoured only on the loopback address: it exists for a local test stub and can never
// send a token anywhere else.
QUrl loopbackOverride(const char* name, const QUrl& fallback) {
    const QString value = qEnvironmentVariable(name);
    if (value.isEmpty()) return fallback;
    const QUrl url(value);
    if (url.isValid() && url.scheme() == QLatin1String("http") && url.host() == QLatin1String("127.0.0.1")) return url;
    qWarning().noquote() << "spotify:" << name << "ignored: only an http://127.0.0.1 override is accepted";
    return fallback;
}

QString join(QString base, const QString& path) {
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    return base + path;
}

#ifdef Q_OS_WIN
QByteArray seal(const QByteArray& plain, bool protect) {
    const QByteArray entropy = QByteArrayLiteral("xmb-native-spotify-session-v1");
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    const BOOL ok = protect ? CryptProtectData(&in, L"XMB Spotify session", &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                            : CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
#else
QByteArray seal(const QByteArray&, bool) { return {}; }
#endif

QString artistsOf(const QJsonObject& track) {
    QStringList names;
    for (const auto& value : track.value(QStringLiteral("artists")).toArray())
        names << value.toObject().value(QStringLiteral("name")).toString();
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

SpotifyTrack parseTrack(const QJsonObject& object) {
    SpotifyTrack track;
    track.id = object.value(QStringLiteral("id")).toString();
    track.name = object.value(QStringLiteral("name")).toString();
    track.uri = object.value(QStringLiteral("uri")).toString();
    track.artists = artistsOf(object);
    if (track.artists.isEmpty()) track.artists = object.value(QStringLiteral("show")).toObject().value(QStringLiteral("name")).toString();
    track.album = object.value(QStringLiteral("album")).toObject().value(QStringLiteral("name")).toString();
    track.durationMs = qint64(object.value(QStringLiteral("duration_ms")).toDouble());
    // A local file in a Spotify playlist has no streamable URI; is_playable=false is a market restriction.
    track.playable = !object.value(QStringLiteral("is_local")).toBool(false) &&
                     object.value(QStringLiteral("is_playable")).toBool(true) && track.uri.startsWith(QLatin1String("spotify:"));
    return track;
}
}

SpotifyClient::SpotifyClient(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<SpotifyPlaylist>();qRegisterMetaType<SpotifyTrack>();
    qRegisterMetaType<SpotifyDevice>();qRegisterMetaType<SpotifyNowPlaying>();
    apiBase_ = loopbackOverride("XMB_SPOTIFY_API_BASE", QUrl(QStringLiteral("https://api.spotify.com/v1")));
    accountsBase_ = loopbackOverride("XMB_SPOTIFY_ACCOUNTS_BASE", QUrl(QStringLiteral("https://accounts.spotify.com")));
}

SpotifyClient::~SpotifyClient() { accessToken_.fill('\0'); refreshToken_.fill('\0'); }

bool SpotifyClient::configure(const QString& dataDirectory) {
    clientId_.clear();configError_.clear();
    QString id = qEnvironmentVariable("SPOTIFY_CLIENT_ID").trimmed();
    int port = qEnvironmentVariableIntValue("SPOTIFY_REDIRECT_PORT");
    QFile file(QDir(dataDirectory).filePath(QStringLiteral("spotify.json")));
    if (file.exists() && file.size() <= 4096 && file.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        if (id.isEmpty()) id = object.value(QStringLiteral("clientId")).toString().trimmed();
        if (port <= 0) port = object.value(QStringLiteral("redirectPort")).toInt(0);
    }
    port_ = (port >= 1024 && port <= 65535) ? port : 8888;
    if (id.isEmpty()) {
        configError_ = tr("No Spotify client id is set. Create an app at developer.spotify.com/dashboard, register the "
                          "redirect URI %1, and put its client id in spotify.json in the data folder.").arg(redirectUri());
        return false;
    }
    if (!validClientId(id)) { configError_ = tr("The Spotify client id must be the app's 32-character hexadecimal id.");return false; }
    clientId_ = id;
    return true;
}

QString SpotifyClient::redirectUri() const { return QStringLiteral("http://127.0.0.1:%1/callback").arg(port_); }

// ---- Sign-in (Authorization Code with PKCE) --------------------------------------------------------------
void SpotifyClient::signIn() {
    if (!configured()) { fail(0, configError_);return; }
    cancelSignIn();
    listener_ = new QTcpServer(this);
    if (!listener_->listen(QHostAddress(QStringLiteral("127.0.0.1")), quint16(port_))) {
        const QString reason = listener_->errorString();
        delete listener_;listener_ = nullptr;
        fail(0, tr("Port %1 on 127.0.0.1 is in use (%2), so Spotify cannot return to this app. Close what uses it, "
                   "or set another redirectPort and register that redirect URI.").arg(port_).arg(reason));
        return;
    }
    connect(listener_, &QTcpServer::newConnection, this, &SpotifyClient::onCallback);
    QTimer::singleShot(SignInTimeoutMs, listener_, [this] {
        if (!listener_) return;
        cancelSignIn();
        fail(0, tr("Spotify sign-in timed out. Try again and finish it in the browser within five minutes."));
    });
    verifier_ = randomToken(64);
    state_ = randomToken(24);
    const QByteArray challenge = QCryptographicHash::hash(verifier_, QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url(join(accountsBase_.toString(), QStringLiteral("/authorize")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId_);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(state_));
    query.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(Scopes));
    url.setQuery(query);
    // Tests point the browser step at a file instead (the URL carries no secret: PKCE keeps the verifier here).
    const QString handoff = qEnvironmentVariable("XMB_SPOTIFY_AUTHORIZE_FILE");
    if (!handoff.isEmpty()) {
        QSaveFile out(handoff);
        if (out.open(QIODevice::WriteOnly)) { out.write(url.toEncoded());out.commit(); }
    } else if (!QDesktopServices::openUrl(url)) {
        cancelSignIn();
        fail(0, tr("The browser could not be opened for the Spotify sign-in."));
        return;
    }
    qInfo().noquote() << "spotify: sign-in opened in the browser; waiting on" << redirectUri();
    emit signInStarted(url.toString());
}

void SpotifyClient::cancelSignIn() {
    if (listener_) { listener_->close();listener_->deleteLater();listener_ = nullptr; }
}

void SpotifyClient::onCallback() {
    if (!listener_) return;
    QTcpSocket* socket = listener_->nextPendingConnection();
    if (!socket) return;
    // An accepted socket is the server's child; the listener closes below, so the socket must outlive it
    // long enough to deliver the page.
    socket->setParent(this);
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    auto read = [this, socket] {
        if (socket->property("handled").toBool()) return;
        if (socket->bytesAvailable() > 16384) { socket->abort();return; }
        if (!socket->canReadLine()) return;
        socket->setProperty("handled", true);
        const QByteArray line = socket->readLine(8192).trimmed();      // "GET /callback?code=...&state=... HTTP/1.1"
        const auto parts = line.split(' ');
        QUrl target(QString::fromLatin1(parts.value(1)));
        QString page;
        if (parts.value(0) != "GET" || target.path() != QLatin1String("/callback")) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();   // flushes, then `disconnected` deletes it
            return;
        }
        const QUrlQuery query(target);
        const QString error = query.queryItemValue(QStringLiteral("error"));
        const QByteArray code = query.queryItemValue(QStringLiteral("code")).toLatin1();
        const bool stateOk = !state_.isEmpty() && query.queryItemValue(QStringLiteral("state")).toLatin1() == state_;
        if (!error.isEmpty()) page = tr("Spotify sign-in was not completed (%1). You can close this tab.").arg(error.left(64).toHtmlEscaped());
        else if (!stateOk || code.isEmpty() || code.size() > 2048) page = tr("This sign-in response did not match the request. You can close this tab.");
        else page = tr("Signed in to Spotify. You can close this tab and return to the XMB.");
        const QByteArray body = "<!doctype html><meta charset=utf-8><title>XMB</title><p>" + page.toUtf8() + "</p>";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();   // flushes, then `disconnected` deletes it
        cancelSignIn();
        if (!error.isEmpty()) { fail(0, tr("Spotify sign-in was not completed: %1.").arg(error.left(64)));return; }
        if (!stateOk || code.isEmpty() || code.size() > 2048) { fail(0, tr("The Spotify sign-in response did not match this request."));return; }
        state_.clear();
        exchangeCode(code);
    };
    connect(socket, &QTcpSocket::readyRead, this, read);
    read();   // the request may already be buffered
}

void SpotifyClient::exchangeCode(const QByteArray& code) {
    QNetworkRequest request(QUrl(join(accountsBase_.toString(), QStringLiteral("/api/token"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(RequestTimeoutMs);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), QString::fromLatin1(code));
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    form.addQueryItem(QStringLiteral("client_id"), clientId_);
    form.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(verifier_));
    QNetworkReply* reply = network_->post(request, form.toString(QUrl::FullyEncoded).toLatin1());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        verifier_.fill('\0');verifier_.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->read(MaxResponseBytes);
        if (status != 200) { fail(0, tr("Spotify refused the sign-in (%1).").arg(apiError(status, bytes)));return; }
        acceptToken(QJsonDocument::fromJson(bytes).object());
        if (!signedIn()) { fail(0, tr("Spotify did not return a refresh token."));return; }
        saveSession();
        qInfo().noquote() << "spotify: signed in (refresh token sealed for this Windows user)";
        fetchMe();
        emit signedInChanged();
    });
}

void SpotifyClient::acceptToken(const QJsonObject& token) {
    const QString access = token.value(QStringLiteral("access_token")).toString();
    const QString refresh = token.value(QStringLiteral("refresh_token")).toString();
    if (access.isEmpty() || access.size() > 4096) return;
    accessToken_.fill('\0');accessToken_ = access.toLatin1();
    const int seconds = std::clamp(token.value(QStringLiteral("expires_in")).toInt(3600), 60, 86400);
    accessExpiry_ = QDateTime::currentDateTimeUtc().addSecs(seconds - 60);
    // Spotify may rotate the refresh token on a refresh; a response without one keeps the current token.
    if (!refresh.isEmpty() && refresh.size() <= 4096) {
        const bool rotated = !refreshToken_.isEmpty() && refreshToken_ != refresh.toLatin1();
        refreshToken_.fill('\0');refreshToken_ = refresh.toLatin1();
        if (rotated) saveSession();
    }
}

void SpotifyClient::refreshAccess(std::function<void(bool)> done) {
    if (refreshToken_.isEmpty()) { done(false);return; }
    QNetworkRequest request(QUrl(join(accountsBase_.toString(), QStringLiteral("/api/token"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(RequestTimeoutMs);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), QString::fromLatin1(refreshToken_));
    form.addQueryItem(QStringLiteral("client_id"), clientId_);
    QNetworkReply* reply = network_->post(request, form.toString(QUrl::FullyEncoded).toLatin1());
    const quint64 generation = generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, generation] {
        reply->deleteLater();
        if (generation != generation_) { done(false);return; }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->read(MaxResponseBytes);
        if (status == 200) acceptToken(QJsonDocument::fromJson(bytes).object());
        const bool ok = status == 200 && !accessToken_.isEmpty();
        if (!ok && (status == 400 || status == 401)) {
            // invalid_grant: the user revoked this app or the token expired. Sign out rather than retry forever.
            qWarning().noquote() << "spotify: the stored sign-in was refused (" << apiError(status, bytes) << "); signing out";
            signOut();
        }
        done(ok);
    });
}

void SpotifyClient::fetchMe() {
    enqueue(Kind::Me, QStringLiteral("GET"), QStringLiteral("/me"));
}

void SpotifyClient::signOut() {
    ++generation_;cancelSignIn();waiting_.clear();refreshing_ = false;
    accessToken_.fill('\0');accessToken_.clear();refreshToken_.fill('\0');refreshToken_.clear();
    displayName_.clear();product_.clear();
    forgetSession();
    emit signedInChanged();
}

bool SpotifyClient::saveSession() {
    if (sessionPath_.isEmpty() || refreshToken_.isEmpty()) return false;
    QByteArray plain = QJsonDocument(QJsonObject{{"v", 1}, {"clientId", clientId_}, {"refresh", QString::fromLatin1(refreshToken_)}})
                           .toJson(QJsonDocument::Compact);
    const QByteArray sealed = seal(plain, true);
    plain.fill('\0');
    if (sealed.isEmpty()) return false;
    QDir().mkpath(QFileInfo(sessionPath_).absolutePath());
    QSaveFile file(sessionPath_);
    static const QByteArray magic = QByteArrayLiteral("XMBSP1\n");
    if (!file.open(QIODevice::WriteOnly) || file.write(magic) != magic.size() || file.write(sealed) != sealed.size() || !file.commit()) return false;
    return true;
}

bool SpotifyClient::restoreSession() {
    if (sessionPath_.isEmpty() || !configured() || signedIn()) return false;
    QFile file(sessionPath_);
    if (!file.exists() || file.size() <= 7 || file.size() > 16 * 1024 || !file.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = file.readAll();
    file.close();
    static const QByteArray magic = QByteArrayLiteral("XMBSP1\n");
    if (!bytes.startsWith(magic)) { forgetSession();return false; }
    QByteArray plain = seal(bytes.mid(magic.size()), false);
    bytes.fill('\0');
    if (plain.isEmpty()) return false;   // another Windows user's record: leave it alone
    const auto record = QJsonDocument::fromJson(plain).object();
    plain.fill('\0');
    const QString refresh = record.value(QStringLiteral("refresh")).toString();
    if (record.value(QStringLiteral("clientId")).toString() != clientId_ || refresh.isEmpty() || refresh.size() > 4096) return false;
    refreshToken_ = refresh.toLatin1();
    accessExpiry_ = {};
    qInfo().noquote() << "spotify: remembered sign-in restored";
    fetchMe();
    return true;
}

void SpotifyClient::forgetSession() { if (!sessionPath_.isEmpty()) QFile::remove(sessionPath_); }

// ---- Web API ---------------------------------------------------------------------------------------------
quint64 SpotifyClient::playlists() { return enqueue(Kind::Playlists, QStringLiteral("GET"), QStringLiteral("/me/playlists?limit=50")); }
quint64 SpotifyClient::likedTracks() { return enqueue(Kind::Liked, QStringLiteral("GET"), QStringLiteral("/me/tracks?limit=50")); }
quint64 SpotifyClient::devices() { return enqueue(Kind::Devices, QStringLiteral("GET"), QStringLiteral("/me/player/devices")); }
quint64 SpotifyClient::nowPlaying() { return enqueue(Kind::NowPlaying, QStringLiteral("GET"), QStringLiteral("/me/player")); }

quint64 SpotifyClient::playlistTracks(const QString& playlistId) {
    QString id = playlistId;
    for (const QChar c : id) if (!c.isLetterOrNumber()) { id.clear();break; }
    if (id.isEmpty() || id.size() > 64) { const quint64 r = ++nextRequest_;QTimer::singleShot(0, this, [this, r] { fail(r, tr("That playlist id is not valid.")); });return r; }
    return enqueue(Kind::PlaylistTracks, QStringLiteral("GET"), QStringLiteral("/playlists/%1/tracks?limit=100").arg(id));
}

namespace {
QString deviceQuery(const QString& deviceId) {
    return deviceId.isEmpty() ? QString() : QStringLiteral("?device_id=") + QString::fromLatin1(QUrl::toPercentEncoding(deviceId));
}
}

quint64 SpotifyClient::play(const QString& deviceId, const QString& contextUri, const QString& offsetUri, const QStringList& uris) {
    QJsonObject body;
    if (!contextUri.isEmpty()) {
        body.insert(QStringLiteral("context_uri"), contextUri);
        if (!offsetUri.isEmpty()) body.insert(QStringLiteral("offset"), QJsonObject{{"uri", offsetUri}});
    } else if (!uris.isEmpty()) {
        body.insert(QStringLiteral("uris"), QJsonArray::fromStringList(uris.mid(0, 100)));
    }
    return enqueue(Kind::Command, QStringLiteral("PUT"), QStringLiteral("/me/player/play") + deviceQuery(deviceId),
                   QJsonDocument(body).toJson(QJsonDocument::Compact));
}
quint64 SpotifyClient::pause(const QString& deviceId) { return enqueue(Kind::Command, QStringLiteral("PUT"), QStringLiteral("/me/player/pause") + deviceQuery(deviceId)); }
quint64 SpotifyClient::next(const QString& deviceId) { return enqueue(Kind::Command, QStringLiteral("POST"), QStringLiteral("/me/player/next") + deviceQuery(deviceId)); }
quint64 SpotifyClient::previous(const QString& deviceId) { return enqueue(Kind::Command, QStringLiteral("POST"), QStringLiteral("/me/player/previous") + deviceQuery(deviceId)); }

quint64 SpotifyClient::enqueue(Kind kind, const QString& method, const QString& path, const QByteArray& body) {
    Pending pending{kind, method, QUrl(join(apiBase_.toString(), path)), body, ++nextRequest_};
    if (kind == Kind::PlaylistTracks) pending.fallbackPath = join(apiBase_.toString(), QString(path).replace(QStringLiteral("/tracks?"), QStringLiteral("/items?")));
    const quint64 request = pending.request;
    if (!signedIn()) { QTimer::singleShot(0, this, [this, request] { fail(request, tr("Sign in to Spotify first.")); });return request; }
    send(std::move(pending));
    return request;
}

void SpotifyClient::send(Pending pending) {
    if (accessToken_.isEmpty() || QDateTime::currentDateTimeUtc() >= accessExpiry_) {
        waiting_.push_back(std::move(pending));
        if (refreshing_) return;
        refreshing_ = true;
        refreshAccess([this](bool ok) {
            refreshing_ = false;
            auto parked = std::move(waiting_);waiting_.clear();
            for (auto& item : parked) {
                if (ok) send(std::move(item));
                else fail(item.request, signedIn() ? tr("Spotify could not refresh the sign-in. Check the network and try again.")
                                                   : tr("The Spotify sign-in has expired or was revoked. Sign in again."));
            }
        });
        return;
    }
    QNetworkRequest request(pending.url);
    request.setRawHeader("Authorization", "Bearer " + accessToken_);
    request.setTransferTimeout(RequestTimeoutMs);
    if (!pending.body.isEmpty() || pending.method != QLatin1String("GET"))
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = network_->sendCustomRequest(request, pending.method.toLatin1(), pending.body);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) { if (received > MaxResponseBytes) reply->abort(); });
    const quint64 generation = generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending = std::move(pending), generation]() mutable {
        handle(reply, std::move(pending), generation);
    });
}

QString SpotifyClient::apiError(int status, const QByteArray& body) const {
    const auto object = QJsonDocument::fromJson(body).object();
    const auto error = object.value(QStringLiteral("error"));
    QString message = error.isObject() ? error.toObject().value(QStringLiteral("message")).toString()
                                       : object.value(QStringLiteral("error_description")).toString();
    if (message.isEmpty() && error.isString()) message = error.toString();
    message = message.left(200);
    return message.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : QStringLiteral("HTTP %1: %2").arg(status).arg(message);
}

void SpotifyClient::handle(QNetworkReply* reply, Pending pending, quint64 generation) {
    reply->deleteLater();
    if (generation != generation_) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray bytes = reply->read(MaxResponseBytes + 1);
    if (bytes.size() > MaxResponseBytes) { fail(pending.request, tr("Spotify sent a response larger than this app reads."));return; }
    if (status == 401 && !pending.retried) {
        pending.retried = true;accessToken_.clear();
        send(std::move(pending));
        return;
    }
    if (status == 0) { fail(pending.request, tr("Spotify could not be reached (%1).").arg(reply->errorString()));return; }
    const auto object = QJsonDocument::fromJson(bytes).object();
    const QString reason = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("reason")).toString();
    if (status == 429) {
        const int retry = reply->rawHeader("Retry-After").toInt();
        fail(pending.request, tr("Spotify is limiting requests from this app. Try again in %1 seconds.").arg(std::max(1, retry)));
        return;
    }
    if (pending.kind == Kind::PlaylistTracks && (status == 404 || status == 410) && !pending.fallbackPath.isEmpty() && pending.pages == 0) {
        // The playlist-items route has two spellings across API revisions; try the other once.
        pending.url = QUrl(pending.fallbackPath);pending.fallbackPath.clear();
        send(std::move(pending));
        return;
    }
    if (status < 200 || status >= 300) {
        QString message;
        if (reason == QLatin1String("PREMIUM_REQUIRED") || (pending.kind == Kind::Command && status == 403))
            message = tr("Spotify only lets Premium accounts control playback from other apps (%1).").arg(apiError(status, bytes));
        else if (reason == QLatin1String("NO_ACTIVE_DEVICE") || (pending.kind == Kind::Command && status == 404))
            message = tr("No Spotify device is active. Open Spotify on this PC, a phone or a speaker, then try again.");
        else if (pending.kind == Kind::PlaylistTracks && status == 404)
            message = tr("Spotify does not let this app read that playlist's tracks (%1). Playlists Spotify itself makes are "
                         "closed to apps in development mode.").arg(apiError(status, bytes));
        else message = tr("Spotify refused the request (%1).").arg(apiError(status, bytes));
        fail(pending.request, message);
        return;
    }
    switch (pending.kind) {
    case Kind::Me:
        displayName_ = object.value(QStringLiteral("display_name")).toString().left(64);
        if (displayName_.isEmpty()) displayName_ = object.value(QStringLiteral("id")).toString().left(64);
        product_ = object.value(QStringLiteral("product")).toString().left(16);
        qInfo().noquote() << "spotify: account" << (displayName_.isEmpty() ? QStringLiteral("(no name)") : displayName_) << "product" << product_;
        emit signedInChanged();
        return;
    case Kind::Command:
        emit commandDone(pending.request);
        return;
    case Kind::Devices: {
        QVector<SpotifyDevice> list;
        for (const auto& value : object.value(QStringLiteral("devices")).toArray()) {
            const auto d = value.toObject();
            SpotifyDevice device{d.value(QStringLiteral("id")).toString(), d.value(QStringLiteral("name")).toString().left(80),
                                 d.value(QStringLiteral("type")).toString(), d.value(QStringLiteral("is_active")).toBool(),
                                 d.value(QStringLiteral("is_restricted")).toBool(), d.value(QStringLiteral("volume_percent")).toInt(-1)};
            if (!device.name.isEmpty()) list.push_back(device);
            if (list.size() >= 32) break;
        }
        emit devicesReady(pending.request, list);
        return;
    }
    case Kind::NowPlaying: {
        SpotifyNowPlaying state;
        if (status == 200 && !object.isEmpty()) {
            const auto item = object.value(QStringLiteral("item")).toObject();
            state.playing = object.value(QStringLiteral("is_playing")).toBool();
            state.track = item.value(QStringLiteral("name")).toString().left(200);
            state.artists = artistsOf(item).left(200);
            state.device = object.value(QStringLiteral("device")).toObject().value(QStringLiteral("name")).toString().left(80);
            state.progressMs = qint64(object.value(QStringLiteral("progress_ms")).toDouble());
            state.durationMs = qint64(item.value(QStringLiteral("duration_ms")).toDouble());
        }
        emit nowPlayingReady(pending.request, state);
        return;
    }
    case Kind::Playlists:
        for (const auto& value : object.value(QStringLiteral("items")).toArray()) {
            const auto p = value.toObject();
            if (p.isEmpty()) continue;
            const auto tracks = p.value(QStringLiteral("tracks")).isObject() ? p.value(QStringLiteral("tracks")).toObject()
                                                                               : p.value(QStringLiteral("items")).toObject();
            SpotifyPlaylist playlist{p.value(QStringLiteral("id")).toString(), p.value(QStringLiteral("name")).toString().left(120),
                                     p.value(QStringLiteral("uri")).toString(),
                                     p.value(QStringLiteral("owner")).toObject().value(QStringLiteral("display_name")).toString().left(64),
                                     tracks.value(QStringLiteral("total")).toInt()};
            if (!playlist.id.isEmpty() && !playlist.name.isEmpty()) pending.playlists.push_back(playlist);
        }
        break;
    case Kind::PlaylistTracks:
    case Kind::Liked:
        for (const auto& value : object.value(QStringLiteral("items")).toArray()) {
            const auto entry = value.toObject();
            const auto track = entry.value(QStringLiteral("track")).isObject() ? entry.value(QStringLiteral("track")).toObject()
                                                                                : entry.value(QStringLiteral("item")).toObject();
            if (track.isEmpty()) continue;
            auto parsed = parseTrack(track);
            if (!parsed.name.isEmpty()) pending.tracks.push_back(parsed);
        }
        break;
    }
    // Paging: follow `next` while it stays on the API host, within the page and item bounds.
    const QUrl next(object.value(QStringLiteral("next")).toString());
    const int count = int(pending.kind == Kind::Playlists ? pending.playlists.size() : pending.tracks.size());
    if (next.isValid() && !next.isEmpty() && next.host() == apiBase_.host() && next.port() == apiBase_.port() &&
        next.scheme() == apiBase_.scheme() && ++pending.pages < MaxPages && count < MaxItems) {
        pending.url = next;pending.retried = false;
        send(std::move(pending));
        return;
    }
    if (pending.kind == Kind::Playlists) emit playlistsReady(pending.request, pending.playlists);
    else emit tracksReady(pending.request, pending.tracks);
}

void SpotifyClient::fail(quint64 request, const QString& message) {
    qWarning().noquote() << "spotify:" << message;
    emit failed(request, message);
}

}
