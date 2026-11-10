#pragma once
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QVector>

class QNetworkAccessManager;
class QTcpServer;

namespace xmb {
struct DiscordFriend { QString id, name, avatarUrl; };

class DiscordClient final : public QObject {
    Q_OBJECT
public:
    explicit DiscordClient(QObject* parent = nullptr);
    ~DiscordClient() override;
    bool configure(const QString& dataDirectory);
    bool configured() const { return !clientId_.isEmpty(); }
    QString configurationError() const { return configError_; }
    QString redirectUri() const;
    void signIn();
    bool restoreSession();
    void signOut();
    bool signedIn() const { return !refreshToken_.isEmpty(); }
    void refresh();
    void fetchAvatar(const QString& url);
    QImage avatar(const QString& url) const { return avatars_.value(url); }
signals:
    void loaded(QVector<xmb::DiscordFriend> friends, QString error);
    void signedInChanged();
    void avatarReady(QString url, QImage image);
private:
    void callback();
    void cancelSignIn();
    void tokenRequest(const QByteArray& code, bool refreshing, bool retry = false);
    bool acceptToken(const QByteArray& bytes);
    bool saveSession();
    void requestFriends(bool retried = false);
    QNetworkAccessManager* network_;
    QTcpServer* listener_ = nullptr;
    QString clientId_, configError_, sessionPath_;
    int port_ = 8888;
    QByteArray verifier_, state_, accessToken_, refreshToken_;
    QDateTime accessExpiry_;
    quint64 generation_ = 0;
    bool refreshing_ = false;
    QHash<QString, QImage> avatars_;
    QSet<QString> avatarRequests_;
};
}
