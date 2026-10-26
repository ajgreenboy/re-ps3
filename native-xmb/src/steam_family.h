#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {

// The Steam Families group this account belongs to, as Steam records it locally in
// userdata/<accountid>/config/localconfig.vdf ("FamilyGroup": groupid, name, members).
// Membership is local and keyless; the *shared app list* is not stored there.
struct SteamFamily {
    QString groupId;
    QString name;
    QVector<QString> memberAccountIds;  // 32-bit account ids, as Steam writes them
    bool valid = false;
};

SteamFamily scanSteamFamily(const QString& steamId64 = {});

// Family-shared titles are licensed to another member, so IPlayerService/GetOwnedGames
// does not return them, and Valve's IFamilyGroupsService requires an OAuth access token
// (a Web API key is rejected with 401). Until that token flow exists, the XMB reads an
// optional list the user provides under <data>/family-shared.json:
//
//   {"apps": [{"id": 620, "title": "Portal 2"}, ...]}
//
// Titles are required, because no local source maps these ids to names.
struct FamilyGame {
    QString appId;
    QString title;
};

QVector<FamilyGame> scanFamilySharedGames(const QString& dataDirectory, QString* error = nullptr);

} // namespace xmb
