#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <QSet>
#include <QtGlobal>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {
struct JellyfinItem { QString id, title, type, overview; bool folder = false; };
struct JellyfinPage { QVector<JellyfinItem> items; int startIndex = 0, total = 0; };
struct JellyfinPlayback {
    QUrl url;
    QString itemId, mediaSourceId, playSessionId, playMethod;
    bool audio = false;
};

class JellyfinClient final : public QObject {
    Q_OBJECT
public:
    explicit JellyfinClient(QObject* parent = nullptr);
    ~JellyfinClient() override;
    void signIn(QUrl server, QString user, QString password);
    void signOut();
    bool signedIn() const;
    QUrl serverUrl() const;
    QByteArray authorizationHeader() const;
    QString userId() const;
    quint64 views();
    quint64 items(QString parentId, int startIndex = 0);
    quint64 playback(QString itemId, bool audio = false);
    void reportPlayback(const JellyfinPlayback& playback, qint64 positionMs, bool paused, QString event);
signals:
    void authenticated();
    void failed(quint64 request, QString message);
    void pageReady(quint64 request, xmb::JellyfinPage page);
    void playbackReady(quint64 request, xmb::JellyfinPlayback playback);
private:
    enum class Operation { SignIn, Views, Items, Playback, Report };
    quint64 send(Operation operation, QNetworkReply* reply, QString context = {}, bool audio = false);
    void finish(QNetworkReply* reply, Operation operation, quint64 request,
                quint64 generation, QString context = {}, bool audio = false);
    QNetworkReply* get(const QUrl& url);
    QNetworkReply* post(const QUrl& url, const QByteArray& body);
    QNetworkReply* deleteResource(const QUrl& url);
    QUrl apiUrl(const QString& route) const;
    bool isCurrent(quint64 generation) const;
    void fail(quint64 request, const QString& message);
    void cancelRequests(bool includeReports = true);
    bool trustedServerUrl(const QUrl& url) const;
    void flushLogouts();
    struct PendingLogout { QUrl url; QByteArray authorization; };
    QVector<PendingLogout> logouts_;
    bool destroying_=false;
    QNetworkAccessManager* network_ = nullptr;
    QVector<QNetworkReply*> replies_;
    QSet<QNetworkReply*> reportReplies_;
    QUrl server_;
    QByteArray accessToken_;
    QString userId_;
    QString deviceId_;
    quint64 generation_ = 0;
    quint64 nextRequest_ = 0;
};
} // namespace xmb
Q_DECLARE_METATYPE(xmb::JellyfinItem)
Q_DECLARE_METATYPE(xmb::JellyfinPage)
Q_DECLARE_METATYPE(xmb::JellyfinPlayback)
