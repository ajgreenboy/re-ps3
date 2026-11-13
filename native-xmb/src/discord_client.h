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

// Plain-language messages for each way the Discord sign-in or friends request can fail, so the
// Friends column says what to fix (docs: audit/DISCORD.md). Pure functions, covered by tests/discord_tests.cpp.
namespace discord {
// The `error` (and optional `error_description`) Discord put on the loopback redirect.
QString authorizeError(const QString& error, const QString& description);
// A failed POST to /oauth2/token. status 0 means no HTTP answer; networkError then says why.
QString tokenError(int status, const QByteArray& body, const QString& networkError, const QString& redirectUri, bool refreshing);
// A failed GET of /users/@me/relationships.
QString friendsError(int status, const QByteArray& body, const QString& networkError);
// Parses the discord.json text. On success fills clientId/port (port 0 when absent) and returns an empty string.
QString parseConfig(const QByteArray& json, QString& clientId, int& port);
// The setup steps the Discord Connection row shows.
QString setupText(const QString& redirectUri);
}

class DiscordClient final : public QObject {
    Q_OBJECT
public:
    explicit DiscordClient(QObject* parent = nullptr);
    ~DiscordClient() override;
    bool configure(const QString& dataDirectory);
    bool configured() const { return !clientId_.isEmpty(); }
    QString configurationError() const { return configError_; }
    // No client id anywhere and no broken discord.json: Discord simply has not been set up.
    bool unset() const { return unset_; }
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
    bool unset_ = true;
    QByteArray verifier_, state_, accessToken_, refreshToken_;
    QDateTime accessExpiry_;
    quint64 generation_ = 0;
    bool refreshing_ = false;
    QHash<QString, QImage> avatars_;
    QSet<QString> avatarRequests_;
};
}
