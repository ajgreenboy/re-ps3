#pragma once
// What Steam itself records about this account's play: per app, when it was last played and the total
// minutes played. Read from userdata/<account>/config/localconfig.vdf
// (UserLocalConfigStore > Software > Valve > Steam > apps > <appid> > LastPlayed / Playtime), which the
// Steam client keeps on disk. Nothing is sent anywhere; a missing or unreadable file is simply no data.
#include <QHash>
#include <QString>

namespace xmb {

struct SteamActivity {
    qint64 lastPlayed = 0;     // Unix seconds; 0 when never recorded
    int playtimeMinutes = 0;
};

QHash<QString, SteamActivity> scanSteamActivity(const QString& steamRoot, const QString& steamId64);
// "12 h 5 min played - Last played 11/03/2026" style detail, in the date order the user chose
// (dateFormat: 0 DD/MM/YYYY, 1 MM/DD/YYYY, 2 YYYY/MM/DD). Empty when there is nothing to say.
QString steamActivityText(const SteamActivity& activity, int dateFormat);

}
