#include "discord_client.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace xmb {
namespace {
constexpr qint64 MaxResponse = 4 * 1024 * 1024;
constexpr int Timeout = 15000;
QByteArray randomToken(int length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QByteArray result(length, '\0');
    for (int i = 0; i < length; ++i) result[i] = alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))];
    return result;
}
// Same per-Windows-user DPAPI protection as Spotify, with separate entropy and record.
QByteArray seal(const QByteArray& plain, bool protect) {
#ifdef Q_OS_WIN
    const QByteArray entropy = QByteArrayLiteral("xmb-native-discord-session-v1");
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    const BOOL ok = protect ? CryptProtectData(&in, L"XMB Discord session", &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                            : CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);LocalFree(out.pbData);return result;
#else
    Q_UNUSED(plain); Q_UNUSED(protect); return {};
#endif
}
}
DiscordClient::DiscordClient(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {}
DiscordClient::~DiscordClient() { accessToken_.fill('\0');refreshToken_.fill('\0');verifier_.fill('\0'); }
QString DiscordClient::redirectUri() const { return QStringLiteral("http://127.0.0.1:%1/callback").arg(port_); }
bool DiscordClient::configure(const QString& dataDirectory) {
    clientId_.clear();configError_.clear();
    QString id = qEnvironmentVariable("DISCORD_CLIENT_ID").trimmed();
    int port = qEnvironmentVariableIntValue("DISCORD_REDIRECT_PORT");
    QFile file(QDir(dataDirectory).filePath(QStringLiteral("discord.json")));
    if (file.exists() && file.size() <= 4096 && file.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        if (id.isEmpty()) id = object.value(QStringLiteral("clientId")).toString().trimmed();
        if (port <= 0) port = object.value(QStringLiteral("redirectPort")).toInt();
    }
    port_ = port >= 1024 && port <= 65535 ? port : 8888;
    sessionPath_ = QDir(dataDirectory).filePath(QStringLiteral("discord/session.bin"));
    if (id.isEmpty()) { configError_ = tr("No Discord client id is set. Put your application clientId in discord.json and register %1 as a redirect URI.").arg(redirectUri());return false; }
    if (id.size() > 32 || std::any_of(id.cbegin(), id.cend(), [](QChar c) { return c < '0' || c > '9'; })) {
        configError_ = tr("The Discord client id must be a numeric application id.");return false;
    }
    clientId_ = id;return true;
}
void DiscordClient::cancelSignIn() { if (listener_) { listener_->close();listener_->deleteLater();listener_ = nullptr; } }
void DiscordClient::signIn() {
    if (!configured()) { emit loaded({}, configError_);return; }
    cancelSignIn();listener_ = new QTcpServer(this);
    if (!listener_->listen(QHostAddress(QStringLiteral("127.0.0.1")), quint16(port_))) {
        const QString error = listener_->errorString();cancelSignIn();emit loaded({}, tr("Discord sign-in cannot listen on port %1: %2").arg(port_).arg(error));return;
    }
    connect(listener_, &QTcpServer::newConnection, this, &DiscordClient::callback);
    QTimer::singleShot(5 * 60 * 1000, listener_, [this] { cancelSignIn();emit loaded({}, tr("Discord sign-in timed out.")); });
    verifier_ = randomToken(64);state_ = randomToken(24);
    const QByteArray challenge = QCryptographicHash::hash(verifier_, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url(QStringLiteral("https://discord.com/oauth2/authorize"));QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId_);query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());query.addQueryItem(QStringLiteral("scope"), QStringLiteral("identify relationships.read"));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(state_));query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));url.setQuery(query);
    if (!QDesktopServices::openUrl(url)) { cancelSignIn();emit loaded({}, tr("Could not open the browser for Discord sign-in.")); }
}
void DiscordClient::callback() {
    if (!listener_) return;
    auto* socket = listener_->nextPendingConnection();if (!socket) return;
    socket->setParent(this);connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    auto read = [this, socket] {
        if (socket->property("handled").toBool()) return;
        if (socket->bytesAvailable() > 16384) { socket->abort();return; }
        if (!socket->canReadLine()) return;
        socket->setProperty("handled", true);const auto parts = socket->readLine(8192).trimmed().split(' ');
        const QUrl target(QString::fromLatin1(parts.value(1)));
        if (parts.value(0) != "GET" || target.path() != QLatin1String("/callback")) { socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return; }
        const QUrlQuery query(target);const QByteArray code = query.queryItemValue(QStringLiteral("code")).toLatin1();
        const bool ok = query.queryItemValue(QStringLiteral("error")).isEmpty() && !state_.isEmpty() && query.queryItemValue(QStringLiteral("state")).toLatin1() == state_ && !code.isEmpty() && code.size() <= 2048;
        const QByteArray body = ok ? QByteArrayLiteral("Return to XMB to finish signing in.") : QByteArrayLiteral("Discord sign-in failed. Return to XMB.");
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);socket->disconnectFromHost();
        cancelSignIn();state_.clear();if (ok) tokenRequest(code, false);else emit loaded({}, tr("Discord sign-in was denied or the response did not match."));
    };
    connect(socket, &QTcpSocket::readyRead, this, read);read();
}
void DiscordClient::tokenRequest(const QByteArray& code, bool refreshing, bool retry) {
    QNetworkRequest request{QUrl(QStringLiteral("https://discord.com/api/oauth2/token"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));request.setTransferTimeout(Timeout);
    QUrlQuery form;form.addQueryItem(QStringLiteral("client_id"), clientId_);
    form.addQueryItem(QStringLiteral("grant_type"), refreshing ? QStringLiteral("refresh_token") : QStringLiteral("authorization_code"));
    if (refreshing) form.addQueryItem(QStringLiteral("refresh_token"), QString::fromLatin1(refreshToken_));
    else { form.addQueryItem(QStringLiteral("code"), QString::fromLatin1(code));form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());form.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(verifier_)); }
    auto* reply = network_->post(request, form.toString(QUrl::FullyEncoded).toLatin1());const quint64 generation = generation_;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 n, qint64) { if (n > MaxResponse) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, refreshing, retry] {
        reply->deleteLater();if (generation != generation_) return;
        refreshing_ = false;verifier_.fill('\0');verifier_.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto bytes = reply->read(MaxResponse + 1);
        if (status != 200 || bytes.size() > MaxResponse || !acceptToken(bytes) || !signedIn()) {
            if (refreshing && (status == 400 || status == 401)) signOut();
            emit loaded({}, tr("Discord could not complete sign-in (HTTP %1). The relationships.read scope may require Discord approval.").arg(status));return;
        }
        saveSession();if (!refreshing) emit signedInChanged();requestFriends(retry);
    });
}
bool DiscordClient::acceptToken(const QByteArray& bytes) {
    const auto object = QJsonDocument::fromJson(bytes).object();const QString access = object.value(QStringLiteral("access_token")).toString();
    const QString refresh = object.value(QStringLiteral("refresh_token")).toString();
    if (access.isEmpty() || access.size() > 4096 || refresh.size() > 4096) return false;
    accessToken_.fill('\0');accessToken_ = access.toLatin1();
    accessExpiry_ = QDateTime::currentDateTimeUtc().addSecs(std::clamp(object.value(QStringLiteral("expires_in")).toInt(3600), 60, 86400) - 60);
    if (!refresh.isEmpty()) { refreshToken_.fill('\0');refreshToken_ = refresh.toLatin1(); } return true;
}
bool DiscordClient::saveSession() {
    if (sessionPath_.isEmpty() || !signedIn()) return false;
    QByteArray plain = QJsonDocument(QJsonObject{{"v", 1}, {"clientId", clientId_}, {"refresh", QString::fromLatin1(refreshToken_)}}).toJson(QJsonDocument::Compact);
    const QByteArray encrypted = seal(plain, true);plain.fill('\0');if (encrypted.isEmpty()) return false;
    QDir().mkpath(QFileInfo(sessionPath_).absolutePath());QSaveFile file(sessionPath_);const QByteArray magic("XMBDC1\n");
    return file.open(QIODevice::WriteOnly) && file.write(magic) == magic.size() && file.write(encrypted) == encrypted.size() && file.commit();
}
bool DiscordClient::restoreSession() {
    if (!configured() || signedIn()) return false;QFile file(sessionPath_);
    if (!file.exists() || file.size() <= 7 || file.size() > 16384 || !file.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = file.readAll();if (!bytes.startsWith("XMBDC1\n")) return false;
    QByteArray plain = seal(bytes.mid(7), false);bytes.fill('\0');if (plain.isEmpty()) return false;
    const auto record = QJsonDocument::fromJson(plain).object();plain.fill('\0');const QString token = record.value(QStringLiteral("refresh")).toString();
    if (record.value(QStringLiteral("clientId")).toString() != clientId_ || token.isEmpty() || token.size() > 4096) return false;
    refreshToken_ = token.toLatin1();accessExpiry_ = {};return true;
}
void DiscordClient::signOut() {
    ++generation_;cancelSignIn();refreshing_ = false;accessToken_.fill('\0');accessToken_.clear();refreshToken_.fill('\0');refreshToken_.clear();
    verifier_.fill('\0');verifier_.clear();state_.clear();QFile::remove(sessionPath_);emit signedInChanged();
}
void DiscordClient::refresh() {
    if (!signedIn()) { emit loaded({}, configured() ? tr("Sign in to Discord first.") : configError_);return; }
    requestFriends();
}
void DiscordClient::requestFriends(bool retried) {
    if (accessToken_.isEmpty() || QDateTime::currentDateTimeUtc() >= accessExpiry_) {
        if (!refreshing_) { refreshing_ = true;tokenRequest({}, true, retried); } return;
    }
    QNetworkRequest request{QUrl(QStringLiteral("https://discord.com/api/v10/users/@me/relationships"))};
    request.setRawHeader("Authorization", "Bearer " + accessToken_);request.setTransferTimeout(Timeout);
    auto* reply = network_->get(request);const quint64 generation = generation_;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 n, qint64) { if (n > MaxResponse) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, retried] {
        reply->deleteLater();if (generation != generation_) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const QByteArray bytes = reply->read(MaxResponse + 1);
        if (status == 401 && !retried) { accessToken_.fill('\0');accessToken_.clear();requestFriends(true);return; }
        QJsonParseError parse;const auto doc = QJsonDocument::fromJson(bytes, &parse);
        if (status != 200 || bytes.size() > MaxResponse || parse.error != QJsonParseError::NoError || !doc.isArray()) {
            emit loaded({}, tr("Discord friends could not be loaded (HTTP %1). The relationships.read scope may require Discord approval.").arg(status));return;
        }
        QVector<DiscordFriend> friends;
        for (const auto& value : doc.array()) {
            const auto relationship = value.toObject();if (relationship.value(QStringLiteral("type")).toInt() != 1) continue;
            const auto user = relationship.value(QStringLiteral("user")).toObject();DiscordFriend person;
            person.id = user.value(QStringLiteral("id")).toString();person.name = user.value(QStringLiteral("global_name")).toString().left(120);
            if (person.name.isEmpty()) person.name = user.value(QStringLiteral("username")).toString().left(120);
            const QString avatar = user.value(QStringLiteral("avatar")).toString();
            const auto safe = [](const QString& s) { return !s.isEmpty() && s.size() <= 128 && std::all_of(s.cbegin(), s.cend(), [](QChar c) { return c.isLetterOrNumber() || c == '_'; }); };
            if (safe(person.id) && safe(avatar)) person.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/%1/%2.png").arg(person.id, avatar);
            if (safe(person.id) && !person.name.isEmpty()) friends.push_back(person);
            if (friends.size() >= 2000) break;
        }
        emit loaded(friends, {});
    });
}
void DiscordClient::fetchAvatar(const QString& url) {
    if (avatars_.contains(url)) { emit avatarReady(url, avatars_.value(url));return; }
    const QUrl source(url);if (source.scheme() != QLatin1String("https") || source.host() != QLatin1String("cdn.discordapp.com") || url.size() > 512 || avatarRequests_.contains(url) || avatarRequests_.size() >= 32) return;
    avatarRequests_.insert(url);QNetworkRequest request(source);request.setTransferTimeout(Timeout);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = network_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 n, qint64) { if (n > 2 * 1024 * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();avatarRequests_.remove(url);const auto bytes = reply->read(2 * 1024 * 1024 + 1);QImage image;
        if (reply->error() == QNetworkReply::NoError && bytes.size() <= 2 * 1024 * 1024) image.loadFromData(bytes);
        if (!image.isNull()) { image = image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);if (avatars_.size() >= 128) avatars_.erase(avatars_.begin());avatars_.insert(url, image);emit avatarReady(url, image); }
    });
}
}
