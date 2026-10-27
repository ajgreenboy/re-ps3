#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace xmb {

// Steam's own download state, read from the files Steam writes while it runs. Read-only: no network
// access, no Steam API key, no writes to Steam's files or registry.
//
// Sources used, each probed on the development machine (audit/STEAM-DOWNLOADS.md):
//  - HKCU\Software\Valve\Steam: SteamPath names the installation root (AutoLoginUser and
//    RunningAppID were verified as well and are documented there, but no download state needs them);
//  - HKCU\Software\Valve\Steam\Apps\<appid> values Running, Updating, Installed, Name;
//  - <steamRoot>/steamapps/libraryfolders.vdf ("path" entries name every library root);
//  - <library>/steamapps/appmanifest_<appid>.acf (AppState StateFlags, name and byte counters);
//  - <library>/steamapps/downloading/<appid>/appmanifest_<appid>.acf if Steam ever leaves one there.
//
// StateFlags is Steam's app state bitmask. These values were verified against this machine's ACFs and
// against the state names Steam itself prints in steam/logs/content_log.txt:
//   2    Update Required
//   4    Fully Installed
//   8    Update Queued
//   16   Update Optional
//   32   Files Missing
//   64   Shared Only
//   128  Files Corrupt
//   512  Update Paused
//   1024 Update Started
//   8192 App Running
// Classification only needs 2, 8, 16, 32, 128, 512 and 1024 (plus 256, documented in Steam's
// AppState enum but not present in the local log masks, as an extra "active" hint); 4 and 8192 are
// listed because they are verified on this machine. See the audit file for the name/hex evidence.
struct SteamDownload {
    QString appId;
    QString title;
    qint64 bytesDownloaded = 0;
    qint64 bytesToDownload = 0;
    qint64 bytesStaged = 0;
    qint64 bytesToStage = 0;
    // One of "downloading", "staging", "updating", "paused", "queued", "update-pending", "idle".
    QString state;
    // Mutually exclusive UI flags. Steam keeps its Update Queued bit set while an update is running,
    // so `queued` here only means "waiting to start" and never overlaps active or paused.
    bool active = false;
    bool paused = false;
    bool queued = false;

    // Progress of the phase the state names: staged bytes while staging, otherwise downloaded bytes,
    // clamped to [0, 1]. 0 when Steam publishes no counters for the app.
    double fraction() const;
};

// Reads Steam's download state. `steamRoot` is the directory that contains `steamapps`; an empty
// value tries XMB_STEAM_ROOT, then HKCU\Software\Valve\Steam\SteamPath, then the usual install
// directories. Only apps with a download or update worth showing are returned (in progress, paused,
// queued, or pending), sorted active -> paused -> queued -> pending and then by title.
// `error`, when given, is cleared on success and receives a reason when Steam could not be read at
// all. An empty result with no error simply means nothing is downloading.
QVector<SteamDownload> scanSteamDownloads(const QString& steamRoot = QString(), QString* error = nullptr);

// XMB download-management text: "Downloading 43%", "Staging 12%", "Paused", "Queued",
// "Update pending", "Updating".
QString steamDownloadStateText(const SteamDownload& download);

// Prints the scan through qInfo() when XMB_STEAM_DOWNLOADS_LOG is set. Self-check helper for
// integration: scanSteamDownloads() then logSteamDownloads(result).
void logSteamDownloads(const QVector<SteamDownload>& downloads);

} // namespace xmb
