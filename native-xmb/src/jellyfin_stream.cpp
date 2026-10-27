#include "jellyfin_stream.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

namespace xmb {
namespace {
constexpr qsizetype kMaxRequestHeaders = 16 * 1024;
constexpr qsizetype kMaxPlaylistBytes = 1024 * 1024;
constexpr qint64 kReplyReadBuffer = 128 * 1024;
constexpr qint64 kSocketHighWater = 256 * 1024;
constexpr int kRequestTimeoutMs = 60 * 1000;

bool validHttpUrl(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
           !url.host().isEmpty() && url.userInfo().isEmpty() && url.isValid();
}

QUrl withoutSecretQueryKeys(QUrl url) {
    const QString rawQuery = url.query(QUrl::FullyEncoded);
    QStringList retained;
    for (const QString& item : rawQuery.split(QLatin1Char('&'), Qt::KeepEmptyParts)) {
        const QString rawKey = item.section(QLatin1Char('='), 0, 0);
        const QString key = QUrl::fromPercentEncoding(rawKey.toUtf8().replace('+', ' ')).toLower();
        if (key == QStringLiteral("apikey") || key == QStringLiteral("api_key") || key == QStringLiteral("access_token") ||
            key == QStringLiteral("token"))
            continue;
        retained.append(item);
    }
    url.setQuery(retained.join(QLatin1Char('&')), QUrl::TolerantMode);
    url.setFragment({});
    return url;
}

QByteArray reasonPhrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 416: return "Range Not Satisfiable";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "Upstream Response";
    }
}

QByteArray safeHeaderValue(QByteArray value) {
    if (value.contains('\r') || value.contains('\n') || value.contains('\0'))
        return {};
    return value;
}

} // namespace

struct JellyfinStream::Request {
    QPointer<QTcpSocket> socket;
    QPointer<QNetworkReply> reply;
    QTimer* timeout = nullptr;
    QByteArray input;
    QByteArray range;
    QByteArray ifRange;
    QByteArray pending;
    QByteArray playlistBytes;
    QByteArray playlistOutput;
    qsizetype playlistOffset = 0;
    QUrl url;
    int redirects = 0;
    int status = 502;
    bool head = false;
    bool headersSent = false;
    bool playlist = false;
    bool playlistReady = false;
    bool upstreamFinished = false;
    bool failed = false;
    bool validated = false;
};

JellyfinStream::JellyfinStream(QObject* parent) : QObject(parent) {
    server_ = new QTcpServer(this);
    network_ = new QNetworkAccessManager(this);
    connect(server_, &QTcpServer::newConnection, this, &JellyfinStream::acceptConnections);
}

JellyfinStream::~JellyfinStream() { close(); }

QUrl JellyfinStream::open(const QUrl& upstream, const QByteArray& authorization) {
    close();
    if (!validHttpUrl(upstream) || authorization.contains('\r') || authorization.contains('\n') ||
        authorization.contains('\0')) {
        emit failed(QStringLiteral("Jellyfin stream URL or authorization is invalid."));
        return {};
    }

    upstream_ = withoutSecretQueryKeys(upstream);
    authorization_ = authorization;
    nonce_ = QByteArray::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        authorization_.clear();
        upstream_.clear();
        nonce_.clear();
        emit failed(QStringLiteral("Could not start the local Jellyfin stream."));
        return {};
    }
    active_ = true;
    return localUrl(upstream_);
}

void JellyfinStream::close() {
    active_ = false;
    if (server_->isListening())
        server_->close();
    const auto sockets = requests_.keys();
    for (QTcpSocket* socket : sockets)
        removeRequest(socket);
    authorization_.fill('\0');
    authorization_.clear();
    nonce_.fill('\0');
    nonce_.clear();
    upstream_.clear();
}

void JellyfinStream::acceptConnections() {
    while (server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        if (!socket)
            continue;
        if (!active_ || requests_.size()>=32 || socket->peerAddress() != QHostAddress::LocalHost) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        auto* request = new Request;
        request->socket = socket;
        request->timeout = new QTimer(socket);
        request->timeout->setSingleShot(true);
        request->timeout->setInterval(kRequestTimeoutMs);
        requests_.insert(socket, request);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QTcpSocket::bytesWritten, this, [this, socket] {
            auto* current = requests_.value(socket, nullptr);
            if (current) {
                current->timeout->start();
                pump(current);
            }
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { removeRequest(socket); });
        connect(request->timeout, &QTimer::timeout, this, [this, socket] {
            auto* current = requests_.value(socket, nullptr);
            if (current) {
                if(current->headersSent && (socket->bytesToWrite()>0 || !current->pending.isEmpty()))current->timeout->start();
                else fail(current,504);
            }
        });
        request->timeout->start();
        readRequest(socket);
    }
}

void JellyfinStream::readRequest(QTcpSocket* socket) {
    auto* request = requests_.value(socket, nullptr);
    if (!request || request->reply || request->headersSent)
        return;
    request->input += socket->read(kMaxRequestHeaders + 1 - request->input.size());
    if (request->input.size() > kMaxRequestHeaders) {
        fail(request, 400);
        return;
    }
    const qsizetype end = request->input.indexOf("\r\n\r\n");
    if (end < 0)
        return;
    beginRequest(socket, request->input.left(end));
}

void JellyfinStream::beginRequest(QTcpSocket* socket, const QByteArray& bytes) {
    auto* request = requests_.value(socket, nullptr);
    if (!request)
        return;
    const QList<QByteArray> lines = bytes.split('\n');
    if (lines.isEmpty()) {
        fail(request, 400);
        return;
    }
    const QByteArray first = lines.first().trimmed();
    const QList<QByteArray> parts = first.split(' ');
    if (parts.size() != 3 || (parts[2] != "HTTP/1.0" && parts[2] != "HTTP/1.1")) {
        fail(request, 400);
        return;
    }
    request->head = parts[0] == "HEAD";
    if (!request->head && parts[0] != "GET") {
        fail(request, 405);
        return;
    }
    const QByteArray targetBytes = parts[1];
    if (!targetBytes.startsWith('/') || targetBytes.startsWith("//")) {
        fail(request, 400);
        return;
    }

    QByteArray path = targetBytes.left(targetBytes.indexOf('?') < 0 ? targetBytes.size() : targetBytes.indexOf('?'));
    const QByteArray prefix = '/' + nonce_ + '/';
    const QString decodedPath = QUrl::fromPercentEncoding(path);
    if (!path.startsWith(prefix) || decodedPath.contains(QStringLiteral("/../")) ||
        decodedPath.endsWith(QStringLiteral("/..")) || decodedPath.contains(QChar('\0'))) {
        fail(request, 403);
        return;
    }
    QByteArray upstreamPath = '/' + path.mid(prefix.size());
    if (upstreamPath.isEmpty())
        upstreamPath = "/";
    QUrl resolved = upstream_;
    resolved.setPath(QString::fromLatin1(upstreamPath), QUrl::TolerantMode);
    const qsizetype queryStart = targetBytes.indexOf('?');
    if (queryStart >= 0) {
        QUrl temp = QUrl::fromEncoded("http://local/?" + targetBytes.mid(queryStart + 1));
        resolved.setQuery(temp.query(QUrl::FullyEncoded));
    } else {
        resolved.setQuery(QString());
    }
    resolved = withoutSecretQueryKeys(resolved);
    if (!sameOrigin(resolved)) {
        fail(request, 403);
        return;
    }

    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            fail(request, 400);
            return;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length" && value != "0") {
            fail(request, 400);
            return;
        }
        if (name == "transfer-encoding") {
            fail(request, 400);
            return;
        }
        if (name == "range")
            request->range = safeHeaderValue(value);
        else if (name == "if-range")
            request->ifRange = safeHeaderValue(value);
    }
    request->validated=true;
    request->url = resolved;
    startUpstream(request, resolved);
}

void JellyfinStream::startUpstream(Request* request, const QUrl& url, int redirects) {
    if (!request || !request->socket || !active_ || !sameOrigin(url) || redirects > 5) {
        if (request)
            fail(request);
        return;
    }
    request->url = url;
    request->redirects = redirects;
    QNetworkRequest networkRequest(url);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    networkRequest.setRawHeader("User-Agent", "xmb-native/1.0");
    networkRequest.setRawHeader("Accept-Encoding", "identity");
    if (!authorization_.isEmpty())
        networkRequest.setRawHeader("Authorization", authorization_);
    if (!request->range.isEmpty())
        networkRequest.setRawHeader("Range", request->range);
    if (!request->ifRange.isEmpty())
        networkRequest.setRawHeader("If-Range", request->ifRange);
    QNetworkReply* reply = request->head ? network_->head(networkRequest) : network_->get(networkRequest);
    request->reply = reply;
    reply->setReadBufferSize(kReplyReadBuffer);
    const QPointer<QTcpSocket> client=request->socket;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, client, reply] {
        auto* current=requests_.value(client,nullptr);if(current && current->reply==reply)onMetadata(current);
    });
    connect(reply, &QIODevice::readyRead, this, [this, client, reply] {
        auto* current=requests_.value(client,nullptr);if(!current || current->reply!=reply)return;
        current->timeout->start();onMetadata(current);
        current=requests_.value(client,nullptr);if(current && current->reply==reply)pump(current);
    });
    connect(reply, &QNetworkReply::finished, this, [this, client, reply] {
        auto* current=requests_.value(client,nullptr);if(current && current->reply==reply)finish(current);
    });
    request->timeout->start();
}

bool JellyfinStream::sameOrigin(const QUrl& url) const {
    if (!validHttpUrl(url))
        return false;
    const auto port = [](const QUrl& value) {
        return value.port(value.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ? 443 : 80);
    };
    return url.scheme().compare(upstream_.scheme(), Qt::CaseInsensitive) == 0 &&
           url.host().compare(upstream_.host(), Qt::CaseInsensitive) == 0 && port(url) == port(upstream_);
}

bool JellyfinStream::isPlaylist(const Request* request) const {
    if (!request)
        return false;
    const QByteArray type = request->reply->rawHeader("Content-Type").toLower();
    const QString path = request->url.path().toLower();
    return path.endsWith(QStringLiteral(".m3u8")) || path.endsWith(QStringLiteral(".m3u")) ||
           type.contains("mpegurl") || type.contains("m3u");
}

QByteArray JellyfinStream::responseHead(int status,
    const QList<QPair<QByteArray, QByteArray>>& headers, bool closeDelimited) const {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    for (const auto& header : headers) {
        if (header.first.isEmpty() || header.second.contains('\r') || header.second.contains('\n'))
            continue;
        response += header.first + ": " + header.second + "\r\n";
    }
    Q_UNUSED(closeDelimited);
    response += "Connection: close\r\n";
    response += "\r\n";
    return response;
}

void JellyfinStream::onMetadata(Request* request) {
    if (!request || !request->reply || request->headersSent || !request->socket)
        return;
    const int status = request->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!status)
        return;
    const QByteArray location = request->reply->rawHeader("Location");
    if (status >= 300 && status < 400 && !location.isEmpty()) {
        const QUrl redirect = request->url.resolved(QUrl::fromEncoded(location));
        if (sameOrigin(redirect) && request->redirects < 5) {
            QNetworkReply* old = request->reply;
            request->reply = nullptr;
            disconnect(old,nullptr,this,nullptr);old->abort();
            old->deleteLater();
            startUpstream(request, withoutSecretQueryKeys(redirect), request->redirects + 1);
            return;
        }
        fail(request);
        return;
    }
    request->status = status;
    request->playlist = !request->head && status >= 200 && status < 300 && isPlaylist(request);
    if (request->playlist)
        return;

    QList<QPair<QByteArray, QByteArray>> headers;
    for (const QByteArray name : {QByteArray("Content-Type"), QByteArray("Content-Length"),
                                  QByteArray("Content-Range"), QByteArray("Accept-Ranges")}) {
        const QByteArray value = safeHeaderValue(request->reply->rawHeader(name));
        if (!value.isEmpty())
            headers.append({name, value});
    }
    bool lengthOk = false;
    request->reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&lengthOk);
    request->headersSent = true;
    request->socket->write(responseHead(status, headers, !lengthOk));
    if (request->head || status == 204 || status == 304) {
        request->upstreamFinished = true;
        auto* old=request->reply.data();request->reply=nullptr;disconnect(old,nullptr,this,nullptr);old->abort();old->deleteLater();
        request->socket->disconnectFromHost();
    }
}

QUrl JellyfinStream::localUrl(const QUrl& upstream) const {
    if (!server_->isListening() || nonce_.isEmpty())
        return {};
    QUrl local;
    local.setScheme(QStringLiteral("http"));
    local.setHost(QStringLiteral("127.0.0.1"));
    local.setPort(server_->serverPort());
    QByteArray path = upstream.path(QUrl::FullyEncoded).toUtf8();
    if (!path.startsWith('/'))
        path.prepend('/');
    local.setPath(QStringLiteral("/") + QString::fromLatin1(nonce_) + QString::fromUtf8(path), QUrl::TolerantMode);
    local.setQuery(upstream.query(QUrl::FullyEncoded), QUrl::TolerantMode);
    return local;
}

void JellyfinStream::pump(Request* request) {
    if (!request || !request->socket || request->failed)
        return;
    QTcpSocket* socket = request->socket;
    if (!request->pending.isEmpty() && socket->bytesToWrite() < kSocketHighWater) {
        const qsizetype count = std::min<qsizetype>(request->pending.size(), 64 * 1024);
        socket->write(request->pending.constData(), count);
        request->pending.remove(0, count);
    }
    if (request->playlistReady && request->pending.isEmpty() &&
        request->playlistOffset < request->playlistOutput.size() && socket->bytesToWrite() < kSocketHighWater) {
        const qsizetype count = std::min<qsizetype>(64 * 1024,
            request->playlistOutput.size() - request->playlistOffset);
        socket->write(request->playlistOutput.constData() + request->playlistOffset, count);
        request->playlistOffset += count;
    }
    if (request->playlist && request->reply && !request->playlistReady) {
        while (request->reply->bytesAvailable() > 0) {
            const qint64 room = kMaxPlaylistBytes - request->playlistBytes.size();
            if (room <= 0) {
                fail(request);
                return;
            }
            request->playlistBytes += request->reply->read(std::min<qint64>(room, 64 * 1024));
        }
        return;
    }
    if (request->reply && request->headersSent && request->pending.isEmpty() &&
        socket->bytesToWrite() < kSocketHighWater && request->reply->bytesAvailable() > 0) {
        const qint64 amount = std::min<qint64>(64 * 1024, request->reply->bytesAvailable());
        request->pending = request->reply->read(amount);
        if (!request->pending.isEmpty())
            pump(request);
    }
    if (request->upstreamFinished && request->pending.isEmpty() && socket->bytesToWrite() == 0 &&
        (!request->reply || request->reply->bytesAvailable() == 0) &&
        (!request->playlistReady || request->playlistOffset >= request->playlistOutput.size())) {
        if (request->reply) {
            request->reply->deleteLater();
            request->reply = nullptr;
        }
        socket->disconnectFromHost();
    }
}

void JellyfinStream::finish(Request* request) {
    if (!request || !request->reply || request->failed)
        return;
    QNetworkReply* completedReply = request->reply;
    const QPointer<QTcpSocket> client=request->socket;
    onMetadata(request);
    request=requests_.value(client,nullptr);
    if (!request || request->reply != completedReply || request->failed)
        return;
    QNetworkReply* reply = completedReply;
    if (request->playlist) {
        if (reply->error() != QNetworkReply::NoError || request->playlistBytes.size() > kMaxPlaylistBytes) {
            fail(request);
            return;
        }
        while (reply->bytesAvailable() > 0) {
            if (request->playlistBytes.size() + reply->bytesAvailable() > kMaxPlaylistBytes) {
                fail(request);
                return;
            }
            request->playlistBytes += reply->read(64 * 1024);
        }
        QString text = QString::fromUtf8(request->playlistBytes);
        static const QRegularExpression uriAttribute(QStringLiteral(R"hls(URI\s*=\s*"([^"]*)")hls"),
                                                     QRegularExpression::CaseInsensitiveOption);
        QStringList outputLines;
        qsizetype rewrittenBytes=0;
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (QString line : lines) {
            if (line.trimmed().isEmpty()) {
                outputLines.append(line);
                continue;
            }
            if (!line.trimmed().startsWith(QLatin1Char('#'))) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) {
                    const QUrl candidate = request->url.resolved(QUrl(trimmed));
                    if (!sameOrigin(candidate)) {
                        fail(request);
                        return;
                    }
                    const QString local = localUrl(withoutSecretQueryKeys(candidate)).toString(QUrl::FullyEncoded);
                    const qsizetype start = line.indexOf(trimmed);
                    line.replace(start, trimmed.size(), local);
                }
            } else {
                QRegularExpressionMatchIterator it = uriAttribute.globalMatch(line);
                QList<QPair<qsizetype, qsizetype>> spans;
                QStringList replacements;
                while (it.hasNext()) {
                    const auto match = it.next();
                    const QUrl candidate = request->url.resolved(QUrl(match.captured(1)));
                    if (!sameOrigin(candidate)) {
                        fail(request);
                        return;
                    }
                    replacements.append(localUrl(withoutSecretQueryKeys(candidate)).toString(QUrl::FullyEncoded));
                    spans.append({match.capturedStart(1), match.capturedLength(1)});
                }
                for (qsizetype i = spans.size() - 1; i >= 0; --i)
                    line.replace(spans.at(i).first, spans.at(i).second, replacements.at(i));
            }
            rewrittenBytes+=line.toUtf8().size()+1;
            if(rewrittenBytes>4*kMaxPlaylistBytes){fail(request);return;}
            outputLines.append(line);
        }
        request->playlistOutput = outputLines.join(QLatin1Char('\n')).toUtf8();
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray contentType = safeHeaderValue(reply->rawHeader("Content-Type"));
        if (contentType.isEmpty())
            contentType = "application/vnd.apple.mpegurl";
        headers.append({"Content-Type", contentType});
        headers.append({"Content-Length", QByteArray::number(request->playlistOutput.size())});
        headers.append(qMakePair(QByteArray("Accept-Ranges"),QByteArray("none")));
        request->headersSent = true;
        request->socket->write(responseHead(request->status, headers, false));
        request->playlistReady = true;
        request->upstreamFinished = true;
        request->reply = nullptr;
        reply->deleteLater();
        pump(request);
        return;
    }

    if (!request->headersSent) {
        if (reply->error() != QNetworkReply::NoError) {
            fail(request);
            return;
        }
        onMetadata(request);
    }
    request->upstreamFinished = true;
    pump(request); // Retain unread reply bytes until downstream backpressure clears.
}

void JellyfinStream::fail(Request* request, int status) {
    if (!request || request->failed)
        return;
    const bool notify=request->validated && status>=500;
    request->failed = true;
    if (request->reply) {
        auto* old=request->reply.data();request->reply=nullptr;disconnect(old,nullptr,this,nullptr);old->abort();old->deleteLater();
    }
    if (request->socket && !request->headersSent) {
        request->socket->write(responseHead(status, {{"Content-Length", "0"}}, false));
        request->headersSent = true;
        request->socket->disconnectFromHost();
    } else if (request->socket) {
        request->socket->abort();
    }
    if(notify)emit failed(QStringLiteral("Jellyfin stream request failed."));
}

void JellyfinStream::removeRequest(QTcpSocket* socket) {
    Request* request = requests_.take(socket);
    if (!request)
        return;
    if (request->timeout)
        request->timeout->stop();
    if (request->reply) {
        auto* old=request->reply.data();request->reply=nullptr;disconnect(old,nullptr,this,nullptr);old->abort();old->deleteLater();
    }
    if (socket) {
        socket->disconnect(this);
        socket->deleteLater();
    }
    delete request;
}

} // namespace xmb
