#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {

// One Steam library collection (the user's own groups such as "Multiplayer" or
// "Finished"). Steam stores these locally for each signed-in account, so reading
// them needs no API key, no network and no write access to Steam's files.
struct SteamCollection {
    QString id;
    QString name;
    QVector<QString> appIds;
};

using SteamCollectionList = QVector<SteamCollection>;

// Reads the collections Steam keeps under
//   <Steam>/userdata/<accountid>/config/cloudstorage/cloud-storage-namespace-1.json
// as "user-collections.*" entries. `steamId64` defaults to the account scanSteam()
// selects. Returns an empty list (never a fabricated one) when the account has no
// collections or the file is missing; `error` explains an unreadable file only.
//
// Unassigned games are deliberately *not* returned here: Steam's implicit "All
// Games" bucket is the caller's own filter over the owned-games list.
SteamCollectionList scanSteamCollections(const QString& steamId64 = {}, QString* error = nullptr);

// SteamID64 -> the 32-bit account id used in userdata/ directory names.
bool steamAccountId(const QString& steamId64, QString* accountId);

} // namespace xmb
