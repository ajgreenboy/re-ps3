#pragma once
// The services' own icons, taken at runtime from where each service already keeps it on this PC or network,
// never shipped in this repository and never downloaded from a third party:
//   Spotify   the desktop client's own icon file (%APPDATA%/Spotify/toast_icon.png)
//   Steam     steam.exe's own icon at its largest size (the shell's jumbo image list)
//   Jellyfin  the icon the user's own Jellyfin server serves to its web client (its touch icon), cached under
//             <data>/jellyfin/server-icon.png once fetched
// Each falls back to this build's drawn mark (glyphs.h) when the official one is not available.
#include <QImage>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;

namespace xmb::serviceIcons {
const QImage& spotify();
const QImage& steam(const QString& steamRoot);
const QImage& jellyfin();
const QImage& tailscale();   // the Windows client's own icon (tailscale-ipn.exe), else the drawn mark
// Loads the cached Jellyfin icon, or fetches it from `server` (same host only, bounded) and caches it.
// `done` runs on the caller's thread when a new icon is available.
void loadJellyfin(QNetworkAccessManager* network, const QUrl& server, const QString& cacheFile, std::function<void()> done);
}
