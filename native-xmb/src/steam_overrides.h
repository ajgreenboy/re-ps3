#pragma once

#include <QString>
#include <QStringList>

namespace xmb {

// User-supplied artwork for one Steam title, so an owned game can be shown with the
// same PKG-style pictures as an installed PS3 game. Nothing is ever written into the
// Steam library; these are plain files under the native data directory:
//
//   <data>/artwork/<appid>.png          wide row art (also used as the background)
//   <data>/artwork/<appid>_icon.png     square ICON0-style icon
//   <data>/artwork/<appid>_bg.png       PIC1-style background
//
// .jpg/.jpeg/.webp are accepted too. Missing files simply yield empty paths.
struct SteamArtworkOverride {
    QString row;         // wide row artwork, or empty
    QString icon;        // square icon, or empty
    QString background;  // full-screen background, or empty
};

SteamArtworkOverride steamArtworkOverride(const QString& dataDirectory, const QString& appId);

// One entry per app id that has any override on disk, for the headless report.
struct SteamArtworkEntry {
    QString appId;
    QString row;
    QString icon;
    QString background;
};
QVector<SteamArtworkEntry> scanSteamArtworkOverrides(const QString& dataDirectory);

} // namespace xmb
