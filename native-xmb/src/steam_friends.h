#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace xmb {

struct SteamFriend {
    QString steamId;
    QString name;
    QString game;
    int personaState=0;
};
using SteamFriendList=QVector<SteamFriend>;

class SteamFriends final : public QObject {
    Q_OBJECT
public:
    explicit SteamFriends(QObject* parent=nullptr);
    ~SteamFriends() override;

    // Prefer the signed-in Steam client helper; fall back to the optional Web API key.
    static bool available();
    QString source() const { return source_; }
    void refresh(QString steamId);

signals:
    void loaded(xmb::SteamFriendList friends,QString error);
    void busyChanged(bool busy);

private:
    void cancelRequests();
    bool tryClient(quint64 generation,const QString& steamId);
    void startWeb(quint64 generation,const QString& steamId);
    void finishFriends();
    void requestFriendList(quint64 generation,const QString& steamId,const QString& key);
    void requestSummaries(quint64 generation,const QString& key);
    void fail(quint64 generation,const QString& message);
    bool isCurrent(quint64 generation) const;

    QNetworkAccessManager* network_=nullptr;
    QVector<QNetworkReply*> replies_;
    QVector<QString> friendIds_;
    QSet<QString> summaryIds_;
    SteamFriendList friends_;
    QString key_,source_,clientError_;
    QProcess* clientProcess_=nullptr;
    quint64 generation_=0;
    int nextSummary_=0;
    bool busy_=false;
};

} // namespace xmb

Q_DECLARE_METATYPE(xmb::SteamFriend)
Q_DECLARE_METATYPE(xmb::SteamFriendList)
