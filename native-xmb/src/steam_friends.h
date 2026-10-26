#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

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

    // Reads STEAM_WEB_API_KEY from this process's environment and asynchronously refreshes the list.
    void refresh(QString steamId);

signals:
    void loaded(xmb::SteamFriendList friends,QString error);
    void busyChanged(bool busy);

private:
    void cancelRequests();
    void requestFriendList(quint64 generation,const QString& steamId,const QString& key);
    void requestSummaries(quint64 generation,const QString& key);
    void fail(quint64 generation,const QString& message);
    bool isCurrent(quint64 generation) const;

    QNetworkAccessManager* network_=nullptr;
    QVector<QNetworkReply*> replies_;
    QVector<QString> friendIds_;
    QSet<QString> summaryIds_;
    SteamFriendList friends_;
    QString key_;
    quint64 generation_=0;
    int nextSummary_=0;
    bool busy_=false;
};

} // namespace xmb

Q_DECLARE_METATYPE(xmb::SteamFriend)
Q_DECLARE_METATYPE(xmb::SteamFriendList)
