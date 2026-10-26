#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {

// One title the signed-in account owns. This is the owner's actual purchase list,
// so it includes games that were never downloaded; `installed` comes from the local
// Steam manifests instead of from Valve.
struct OwnedGame {
    QString appId;
    QString title;
    QString iconUrl;
    QString headerUrl;
    int playtimeMinutes = 0;
    bool installed = false;
};

using OwnedGameList = QVector<OwnedGame>;

// Reads the account's owned games through Valve's documented IPlayerService
// endpoint. A Steam Web API key is required by Valve for this call; the key is read
// only from this process's environment (STEAM_WEB_API_KEY), never stored, logged or
// committed. Without a key the caller gets an explicit needsKey error - no games are
// invented and non-owned entries are never listed.
//
//   GET https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/
//       ?key=<k>&steamid=<id>&include_appinfo=1&include_played_free_games=1
class SteamOwned final : public QObject {
    Q_OBJECT
public:
    explicit SteamOwned(QObject* parent = nullptr);
    ~SteamOwned() override;

    // True when STEAM_WEB_API_KEY is present in the environment (the value is not exposed).
    static bool hasApiKey();

    // Asynchronously fetch the owned list for a SteamID64. Calls loaded() exactly once.
    void refresh(QString steamId64);
    void cancel();

    // Steam remains the installer and launcher; these only build the handoffs.
    static QString installUrl(const QString& appId);  // steam://install/<id>
    static QString launchUrl(const QString& appId);   // steam://rungameid/<id>
    static QString storeUrl(const QString& appId);    // store page (screenshots/description)

signals:
    // needsKey is set when the failure is only "no API key configured"; the shell shows
    // a setup hint instead of a network error in that case.
    void loaded(xmb::OwnedGameList games, QString error, bool needsKey);
    void busyChanged(bool busy);

private:
    void fail(quint64 generation, const QString& message, bool needsKey);
    void finish(quint64 generation);

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    quint64 generation_ = 0;
    bool busy_ = false;
};

} // namespace xmb

Q_DECLARE_METATYPE(xmb::OwnedGame)
Q_DECLARE_METATYPE(xmb::OwnedGameList)
