#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace xmb {
// One entry in the Hydra Launcher library (api.hydralibrary.com/sources). The url is the release-index
// JSON (e.g. https://hydralinks.cloud/sources/fitgirl.json) that importSource() can read.
struct HydraSource {
    QString title; QUrl url; int gamesCount=0; QString description; QStringList status; QStringList topDownloadOption;
};
// One download source a game is available from. A game may appear in several sources.
struct GameSourceOption {
    QString source;         // source display name
    QString sourceUrl;      // the source's release-index JSON, to resolve download links
    QString fileSize;
    QString sourceStatus;   // Trusted / Safe For Use / ...
};
// One search hit from /games, filtered to the extract-and-play sources and deduplicated by name.
struct GameResult {
    QString name;           // clean name (the dedup key)
    QString title;
    QString originalTitle;  // the full release line from the first option
    QVector<GameSourceOption> options;
};
// Blocking client, like the other network clients; call from a worker.
class HydraLibrary {
public:
    bool listSources(QVector<HydraSource>* out, QString* error=nullptr) const;
    // The source names that ship extract-and-play (no-setup) games: description/title says
    // "pre-installed"/"portable", or its downloads are direct-host rather than torrent.
    bool preinstalledSources(QStringList* out, QString* error=nullptr) const;
    // Full-text search across /games, filtered to the preinstalled sources.
    bool searchGames(const QString& query, QVector<GameResult>* out, QString* error=nullptr) const;
};
} // namespace xmb
