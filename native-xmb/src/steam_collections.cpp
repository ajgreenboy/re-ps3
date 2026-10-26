#include "steam_collections.h"

#include "steam_library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace xmb {
namespace {
constexpr qint64 MaxFileBytes = 8 * 1024 * 1024;
constexpr int MaxCollections = 512;
constexpr int MaxAppsPerCollection = 20000;
constexpr int MaxNameLength = 120;

// SteamID64 is a 64-bit value whose low 32 bits are the account id.
constexpr qint64 SteamId64Base = 76561197960265728LL;
} // namespace

bool steamAccountId(const QString& steamId64, QString* accountId) {
    bool ok = false;
    const qint64 value = steamId64.trimmed().toLongLong(&ok);
    if (!ok || value <= SteamId64Base) return false;
    if (accountId) *accountId = QString::number(value - SteamId64Base);
    return true;
}

SteamCollectionList scanSteamCollections(const QString& steamId64, QString* error) {
    if (error) error->clear();
    QString id = steamId64.trimmed();
    if (id.isEmpty()) id = scanSteam().steamId;
    QString account;
    if (!steamAccountId(id, &account)) {
        if (error) *error = QStringLiteral("No Steam account was found; set XMB_STEAM_ID to a SteamID64.");
        return {};
    }

    // Steam is installed where the library scan found it; fall back to the
    // conventional location so a collections-only read still works.
    const QStringList roots{
        scanSteam().root,
        QStringLiteral("C:/Program Files (x86)/Steam"),
        QStringLiteral("C:/Program Files/Steam"),
    };
    for (const QString& root : roots) {
        if (root.isEmpty()) continue;
        const QDir dir(QDir(root).filePath(QStringLiteral("userdata/%1/config/cloudstorage").arg(account)));
        const QString path = dir.filePath(QStringLiteral("cloud-storage-namespace-1.json"));
        QFile file(path);
        if (!file.exists()) continue;
        if (file.size() > MaxFileBytes) {
            if (error) *error = QStringLiteral("The Steam collection file is larger than expected.");
            return {};
        }
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = file.errorString();
            return {};
        }
        QJsonParseError parseError{};
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            if (error) *error = QStringLiteral("The Steam collection file was not valid JSON.");
            return {};
        }

        SteamCollectionList collections;
        for (const auto& entry : document.array()) {
            if (collections.size() >= MaxCollections) break;
            if (!entry.isArray()) continue;
            const auto pair = entry.toArray();
            if (pair.size() != 2 || !pair.at(1).isObject()) continue;
            const auto record = pair.at(1).toObject();
            if (record.value(QLatin1String("is_deleted")).toBool(false)) continue;
            const QString key = record.value(QLatin1String("key")).toString();
            if (!key.startsWith(QLatin1String("user-collections."))) continue;
            // The payload is a JSON document carried as a string.
            const QString payload = record.value(QLatin1String("value")).toString();
            if (payload.isEmpty()) continue;
            QJsonParseError innerError{};
            const auto collection = QJsonDocument::fromJson(payload.toUtf8(), &innerError);
            if (innerError.error != QJsonParseError::NoError || !collection.isObject()) continue;
            const auto object = collection.object();
            SteamCollection item;
            item.id = key.mid(int(std::strlen("user-collections.")));
            item.name = object.value(QLatin1String("name")).toString().trimmed();
            if (item.name.isEmpty()) item.name = item.id;
            if (item.name.size() > MaxNameLength) item.name.truncate(MaxNameLength);
            QSet<QString> seen;
            for (const auto& appId : object.value(QLatin1String("added")).toArray()) {
                if (item.appIds.size() >= MaxAppsPerCollection) break;
                if (!appId.isDouble()) continue;
                const QString text = QString::number(appId.toInt());
                if (text == QLatin1String("0") || seen.contains(text)) continue;
                seen.insert(text);
                item.appIds.push_back(text);
            }
            collections.push_back(std::move(item));
        }
        std::sort(collections.begin(), collections.end(),
                  [](const SteamCollection& a, const SteamCollection& b) {
                      return a.name.localeAwareCompare(b.name) < 0;
                  });
        return collections;
    }
    if (error) *error = QStringLiteral("Steam's userdata folder was not found for this account.");
    return {};
}

} // namespace xmb
