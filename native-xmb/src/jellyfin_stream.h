#pragma once

#include <QByteArray>
#include <QObject>
#include <QUrl>
#include <QHash>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

namespace xmb {

class JellyfinStream final : public QObject {
    Q_OBJECT
public:
    explicit JellyfinStream(QObject* parent = nullptr);
    ~JellyfinStream() override;

    QUrl open(const QUrl& upstream, const QByteArray& authorization);
    void close();

signals:
    void failed(QString message);

private:
    struct Request;

    void acceptConnections();
    void readRequest(QTcpSocket* socket);
    void beginRequest(QTcpSocket* socket, const QByteArray& bytes);
    void startUpstream(Request* request, const QUrl& url, int redirects = 0);
    void onMetadata(Request* request);
    void pump(Request* request);
    void finish(Request* request);
    void fail(Request* request, int status = 502);
    void removeRequest(QTcpSocket* socket);
    QUrl localUrl(const QUrl& upstream) const;
    bool sameOrigin(const QUrl& url) const;
    bool isPlaylist(const Request* request) const;
    QByteArray responseHead(int status, const QList<QPair<QByteArray, QByteArray>>& headers,
                            bool closeDelimited) const;

    QTcpServer* server_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    QHash<QTcpSocket*, Request*> requests_;
    QUrl upstream_;
    QByteArray authorization_;
    QByteArray nonce_;
    bool active_ = false;
};

} // namespace xmb
