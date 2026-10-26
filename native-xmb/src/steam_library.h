#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {

struct SteamGame {
    QString appId;
    QString title;
    QString installDirectory;
    QString artwork;
};

struct SteamInstallation {
    QString root;
    QString steamId;
    QVector<SteamGame> games;
    QStringList warnings;
};

// Reads Steam's local configuration and installed-game manifests. This function
// performs no network access and makes no changes to Steam files.
SteamInstallation scanSteam(const QString& explicitRoot = {});

}
