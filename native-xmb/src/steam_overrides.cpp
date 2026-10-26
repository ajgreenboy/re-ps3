#include "steam_overrides.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>

namespace xmb {
namespace {
constexpr qint64 MaxArtworkBytes = 32 * 1024 * 1024;
const QStringList Extensions{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp")};

QString existing(const QDir& dir, const QString& stem) {
    for (const auto& extension : Extensions) {
        const QFileInfo file(dir.filePath(stem + QLatin1Char('.') + extension));
        if (!file.isFile() || file.size() <= 0 || file.size() > MaxArtworkBytes) continue;
        return file.absoluteFilePath();
    }
    return {};
}

// "<appid>_icon", "<appid>_bg" and a bare "<appid>" are the only accepted stems; anything
// else in the folder is ignored so a stray file cannot hijack a row.
QString stemFor(const QString& baseName) {
    for (const auto& suffix : {QStringLiteral("_icon"), QStringLiteral("_bg")}) {
        if (baseName.endsWith(suffix)) return baseName.chopped(suffix.size());
    }
    return baseName;
}

QString numericAppId(const QString& stem) {
    const QString trimmed = stem.trimmed();
    if (trimmed.isEmpty()) return {};
    bool ok = false;
    const qlonglong value = trimmed.toLongLong(&ok);
    if (!ok || value <= 0) return {};
    return QString::number(value);
}
} // namespace

SteamArtworkOverride steamArtworkOverride(const QString& dataDirectory, const QString& appId) {
    SteamArtworkOverride result;
    const QString id = numericAppId(appId);
    if (id.isEmpty() || dataDirectory.isEmpty()) return result;
    const QDir dir(QDir(dataDirectory).filePath(QStringLiteral("artwork")));
    if (!dir.exists()) return result;
    result.row = existing(dir, id);
    result.icon = existing(dir, id + QStringLiteral("_icon"));
    result.background = existing(dir, id + QStringLiteral("_bg"));
    // A bare <appid>.png is the wide row art; it also stands in as the background when
    // no dedicated _bg image exists, which is what the installed-PKG layout expects.
    if (result.background.isEmpty()) result.background = result.row;
    if (result.icon.isEmpty()) result.icon = result.row;
    return result;
}

QVector<SteamArtworkEntry> scanSteamArtworkOverrides(const QString& dataDirectory) {
    QVector<SteamArtworkEntry> entries;
    if (dataDirectory.isEmpty()) return entries;
    const QDir dir(QDir(dataDirectory).filePath(QStringLiteral("artwork")));
    if (!dir.exists()) return entries;

    QMap<QString, SteamArtworkEntry> byId;
    for (const auto& info : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!Extensions.contains(info.suffix().toLower())) continue;
        if (info.size() <= 0 || info.size() > MaxArtworkBytes) continue;
        // Split "<appid>[_icon|_bg]" into the id and which slot the file fills.
        QString base = info.completeBaseName();
        QString slot = QStringLiteral("row");
        if (base.endsWith(QStringLiteral("_icon"))) { base.chop(5); slot = QStringLiteral("icon"); }
        else if (base.endsWith(QStringLiteral("_bg"))) { base.chop(3); slot = QStringLiteral("background"); }
        const QString id = numericAppId(stemFor(base));
        if (id.isEmpty()) continue;
        auto& entry = byId[id];
        entry.appId = id;
        if (slot == QLatin1String("icon")) entry.icon = info.absoluteFilePath();
        else if (slot == QLatin1String("background")) entry.background = info.absoluteFilePath();
        else entry.row = info.absoluteFilePath();
    }
    for (auto it = byId.cbegin(); it != byId.cend(); ++it) entries.push_back(it.value());
    return entries;
}

} // namespace xmb
