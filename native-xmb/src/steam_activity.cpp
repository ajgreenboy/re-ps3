#include "steam_activity.h"
#include "steam_collections.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QtDebug>

namespace xmb {
namespace {
constexpr qint64 MaxLocalConfigBytes = 32LL * 1024 * 1024;

// A tolerant reader for Valve's KeyValues text: quoted keys, quoted values, braces. It walks the
// tree once, tracking the key path, and calls `leaf` for every key/value pair.
template <typename Leaf>
void walk(const QString& text, Leaf leaf) {
    QStringList path;
    QString pendingKey;bool haveKey = false;
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text[i];
        if (c == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('/')) {   // line comment
            while (i < n && text[i] != QLatin1Char('\n')) ++i;
            continue;
        }
        if (c == QLatin1Char('{')) {
            if (path.size() > 64) return;
            path << (haveKey ? pendingKey : QString());haveKey = false;
            continue;
        }
        if (c == QLatin1Char('}')) {
            if (!path.isEmpty()) path.removeLast();
            haveKey = false;
            continue;
        }
        if (c != QLatin1Char('"')) continue;
        QString token;
        for (++i; i < n && text[i] != QLatin1Char('"'); ++i) {
            if (text[i] == QLatin1Char('\\') && i + 1 < n) { ++i; }
            token += text[i];
        }
        if (!haveKey) { pendingKey = token;haveKey = true; }
        else { leaf(path, pendingKey, token);haveKey = false; }
    }
}
}

QHash<QString, SteamActivity> scanSteamActivity(const QString& steamRoot, const QString& steamId64) {
    QHash<QString, SteamActivity> result;
    // Deterministic captures (work/ab-layers.ts) turn the live play records off.
    if (qEnvironmentVariable("XMB_STEAM_ACTIVITY") == QLatin1String("0")) return result;
    QString account;
    if (steamRoot.isEmpty() || !steamAccountId(steamId64, &account)) return result;
    QFile file(QDir(steamRoot).filePath(QStringLiteral("userdata/%1/config/localconfig.vdf").arg(account)));
    if (!file.exists() || file.size() > MaxLocalConfigBytes || !file.open(QIODevice::ReadOnly)) return result;
    const QString text = QString::fromUtf8(file.readAll());
    walk(text, [&](const QStringList& path, const QString& key, const QString& value) {
        // ... > Software > Valve > Steam > apps > <appid> > key
        if (path.size() < 2) return;
        const QString& appId = path.last();
        if (path[path.size() - 2].compare(QLatin1String("apps"), Qt::CaseInsensitive) != 0) return;
        if (path.size() >= 3 && path[path.size() - 3].compare(QLatin1String("Steam"), Qt::CaseInsensitive) != 0) return;
        bool numeric = false;appId.toULongLong(&numeric);
        if (!numeric) return;
        if (key.compare(QLatin1String("LastPlayed"), Qt::CaseInsensitive) == 0) result[appId].lastPlayed = value.toLongLong();
        else if (key.compare(QLatin1String("Playtime"), Qt::CaseInsensitive) == 0) result[appId].playtimeMinutes = value.toInt();
    });
    qInfo().noquote() << "steam activity:" << result.size() << "app(s) with play records";
    return result;
}

QString steamActivityText(const SteamActivity& activity, int dateFormat) {
    QStringList parts;
    if (activity.playtimeMinutes > 0) {
        const int hours = activity.playtimeMinutes / 60, minutes = activity.playtimeMinutes % 60;
        parts << (hours > 0 ? (minutes > 0 ? QStringLiteral("%1 h %2 min played").arg(hours).arg(minutes)
                                           : QStringLiteral("%1 h played").arg(hours))
                            : QStringLiteral("%1 min played").arg(minutes));
    }
    if (activity.lastPlayed > 86400) {
        const QDate date = QDateTime::fromSecsSinceEpoch(activity.lastPlayed).date();
        const QString pattern = dateFormat == 0 ? QStringLiteral("dd/MM/yyyy")
                              : dateFormat == 2 ? QStringLiteral("yyyy/MM/dd") : QStringLiteral("MM/dd/yyyy");
        parts << QStringLiteral("Last played %1").arg(date.toString(pattern));
    }
    return parts.join(QStringLiteral(" - "));
}

}
