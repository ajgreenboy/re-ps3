#include "steam_family.h"

#include "steam_collections.h"
#include "steam_library.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace xmb {
namespace {
constexpr qint64 MaxLocalConfigBytes = 16 * 1024 * 1024;
constexpr qint64 MaxFamilyListBytes = 1024 * 1024;
constexpr int MaxFamilyGames = 5000;

// VDF is a nested key/value text format; the family block is flat enough to read with a
// small brace-aware scan instead of a full parser.
QString familyBlock(const QString& text) {
    const int start = text.indexOf(QStringLiteral("\"FamilyGroup\""));
    if (start < 0) return {};
    int depth = 0;
    int i = start;
    bool inQuote = false;
    bool opened = false;
    for (; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) inQuote = !inQuote;
        if (inQuote) continue;
        if (c == QLatin1Char('{')) { ++depth; opened = true; }
        else if (c == QLatin1Char('}')) {
            --depth;
            if (opened && depth == 0) return text.mid(start, i - start + 1);
        }
    }
    return opened ? text.mid(start) : QString{};
}

QString valueFor(const QString& block, const QString& key) {
    const QRegularExpression pattern(
        QStringLiteral("\"%1\"\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(key)));
    const auto match = pattern.match(block);
    return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}
} // namespace

SteamFamily scanSteamFamily(const QString& steamId64) {
    SteamFamily family;
    QString account;
    if (!steamAccountId(steamId64.isEmpty() ? scanSteam().steamId : steamId64, &account)) return family;

    const QStringList roots{scanSteam().root, QStringLiteral("C:/Program Files (x86)/Steam"),
                            QStringLiteral("C:/Program Files/Steam")};
    for (const QString& root : roots) {
        if (root.isEmpty()) continue;
        QFile file(QDir(root).filePath(QStringLiteral("userdata/%1/config/localconfig.vdf").arg(account)));
        if (!file.exists()) continue;
        if (file.size() > MaxLocalConfigBytes || !file.open(QIODevice::ReadOnly)) return family;
        const QString text = QString::fromUtf8(file.readAll());
        file.close();
        const QString block = familyBlock(text);
        if (block.isEmpty()) return family;
        family.groupId = valueFor(block, QStringLiteral("groupid"));
        family.name = valueFor(block, QStringLiteral("name"));
        const QRegularExpression member(QStringLiteral("\"accountid\"\\s*\"(\\d+)\""));
        auto it = member.globalMatch(block);
        while (it.hasNext()) {
            const QString id = it.next().captured(1);
            if (!id.isEmpty() && !family.memberAccountIds.contains(id)) family.memberAccountIds.push_back(id);
        }
        family.valid = !family.groupId.isEmpty();
        return family;
    }
    return family;
}

QVector<FamilyGame> scanFamilySharedGames(const QString& dataDirectory, QString* error) {
    if (error) error->clear();
    QVector<FamilyGame> games;
    if (dataDirectory.isEmpty()) return games;
    QFile file(QDir(dataDirectory).filePath(QStringLiteral("family-shared.json")));
    if (!file.exists()) return games;  // optional list; its absence is not an error
    if (file.size() > MaxFamilyListBytes) {
        if (error) *error = QStringLiteral("family-shared.json is larger than expected.");
        return games;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return games;
    }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("family-shared.json was not valid JSON.");
        return games;
    }
    const auto rows = document.object().value(QLatin1String("apps")).toArray();
    games.reserve(std::min<int>(int(rows.size()), MaxFamilyGames));
    for (const auto& value : rows) {
        if (games.size() >= MaxFamilyGames) break;
        if (!value.isObject()) continue;
        const auto row = value.toObject();
        FamilyGame game;
        game.appId = QString::number(row.value(QLatin1String("id")).toInt());
        game.title = row.value(QLatin1String("title")).toString().trimmed();
        if (game.appId.isEmpty() || game.appId == QLatin1String("0") || game.title.isEmpty()) continue;
        games.push_back(game);
    }
    return games;
}

} // namespace xmb
