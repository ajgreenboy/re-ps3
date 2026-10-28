#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <functional>

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

// A friend's own avatar, by SteamID, without a Web API key, a cookie jar or a signed-in session.
// The friends list only carries IDs, so the picture comes from Steam's public profile page:
//   1. GET https://steamcommunity.com/profiles/<steamid>?xml=1 (https, that exact host, <= 256 KiB,
//      8 s) and read <avatarMedium> (or <avatarFull>) out of the returned XML;
//   2. GET that URL only if it is https on Steam's own avatar CDN, <= 1 MiB, and the response says
//      Content-Type: image/...;
//   3. decode with hard dimension/byte caps, scale to 64x64, and cache it as PNG at
//      <data>/cache/steam/avatars/<steamid>.image (QSaveFile, atomic).
// A private profile, a missing XML field, a network error, a non-image response or an oversized
// image simply leaves no cache file. The caller keeps the firmware's tex_Avatar_Default icon then:
// a fallback, never a fabricated picture and never a retry storm (16 fetches per refresh, 2 at a
// time, and a cached avatar is reused for 30 days).
class SteamFriendAvatars final : public QObject {
    Q_OBJECT
public:
    explicit SteamFriendAvatars(QString cacheDirectory, QObject* parent = nullptr);
    ~SteamFriendAvatars() override;

    // <cacheDirectory>/<steamid>.image, or empty when the id or the directory is unusable.
    static QString cachePath(QString cacheDirectory, const QString& steamId);

    // The avatar URL a public profile page carries (<avatarMedium>, then <avatarFull>), or an empty
    // URL when the page has none or names a host that is not Steam's own CDN. Pure parsing: no
    // network, no file, so a test can prove the accepted shape and the host allow-list offline.
    static QUrl avatarUrlForProfileXml(const QByteArray& profileXml);

    // Decodes at most `maximumDecodes` cached avatars (64x64, content-detected format). An id with
    // no readable cache file is absent from the result; that is the caller's fallback signal.
    static QHash<QString, QImage> cachedAvatars(QString cacheDirectory, const QStringList& steamIds,
                                                int maximumDecodes = 64);

    // Fetches avatars for the ids that have no fresh cache file, in the order given, at most 16 per
    // call. Supersedes any fetch already in flight.
    void refresh(QStringList steamIds);

signals:
    // Emitted once per avatar that was fetched, decoded and cached. Never emitted for a fallback.
    void loaded(QString steamId, QImage image);

private:
    using ResponseHandler = std::function<void(const QByteArray&, const QByteArray&, int, int)>;

    bool isCurrent(quint64 generation) const;
    void cancelRequests();
    void pump(quint64 generation);
    void startProfile(const QString& steamId, quint64 generation);
    void startImage(const QString& steamId, const QUrl& url, quint64 generation);
    void get(const QString& steamId, bool imageRequest, const QUrl& url, quint64 generation,
             int redirectCount, ResponseHandler handler);
    void storeAvatar(const QString& steamId, quint64 generation, const QByteArray& bytes);
    void finishJob(const QString& steamId, quint64 generation, QImage image = {});

    QString cacheDirectory_;
    QNetworkAccessManager* network_ = nullptr;
    QVector<QNetworkReply*> replies_;
    QStringList pendingIds_;
    quint64 generation_ = 0;
    int activeJobs_ = 0;
};

} // namespace xmb

Q_DECLARE_METATYPE(xmb::SteamFriend)
Q_DECLARE_METATYPE(xmb::SteamFriendList)
