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

// Live state of one title as the running Steam client reports it in the registry
// (HKCU\Software\Valve\Steam). Read-only; always false off Windows.
struct SteamAppState {
    bool running = false;
    bool updating = false;
};
SteamAppState steamAppState(const QString& appId);

// Ask the running Steam client to stop a title through its console command line. The client executes
// "+<command>" arguments handed to steam.exe, so this is the supported-by-observation way to quit a
// Steam game without killing processes (audit/STEAM-CLI.md). Returns false when the client cannot be
// started or the id is not a number; the caller still has to wait for Apps\<id>\Running to clear.
bool steamStopApp(const QString& appId,const QString& steamRoot = {}, QString* error = nullptr);

}
