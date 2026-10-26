#include "steam_owned.h"

#include "steam_library.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QFile>
#include <QTimer>
#include <QUrlQuery>

namespace xmb {
namespace {
constexpr int RequestTimeoutMs = 15000;
constexpr qint64 MaxResponseBytes = 8 * 1024 * 1024;
constexpr int MaxGames = 4096;

QString keyFromEnvironment() { return qEnvironmentVariable("STEAM_WEB_API_KEY").trimmed(); }

QString iconUrlFor(const QString& appId, const QString& hash) {
    if (hash.isEmpty()) return {};
    return QStringLiteral("https://media.steampowered.com/steamcommunity/public/images/apps/%1/%2.jpg")
        .arg(appId, hash);
}

QString headerUrlFor(const QString& appId) {
    if (appId.isEmpty()) return {};
    return QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/header.jpg").arg(appId);
}
} // namespace

SteamOwned::SteamOwned(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<OwnedGameList>("xmb::OwnedGameList");
    qRegisterMetaType<OwnedGame>("xmb::OwnedGame");
}

SteamOwned::~SteamOwned() { cancel(); }

QString SteamOwned::apiKeyFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) return {};
    return QDir(dir).filePath(QStringLiteral("steam_api_key"));
}

QString SteamOwned::apiKey() {
    const QString fromEnv = keyFromEnvironment();
    if (!fromEnv.isEmpty()) return fromEnv;
    // A single-line user-only file keeps the key off the command line and out of Git.
    QFile file(apiKeyFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (file.size() > 4096) return {};
    const QString text = QString::fromUtf8(file.readAll()).trimmed();
    const int newline = text.indexOf(QRegularExpression(QStringLiteral("[\\r\\n]")));
    return (newline > 0 ? text.left(newline) : text).trimmed();
}

bool SteamOwned::hasApiKey() { return !apiKey().isEmpty(); }

void SteamOwned::cancel() {
    ++generation_;
    if (reply_) {
        auto* reply = reply_;
        reply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (busy_) {
        busy_ = false;
        emit busyChanged(false);
    }
}

void SteamOwned::refresh(QString steamId64) {
    cancel();
    steamId64 = steamId64.trimmed();
    if (steamId64.isEmpty()) {
        // Fall back to the account Steam recorded as active, then the sole valid one.
        steamId64 = scanSteam().steamId;
    }
    if (steamId64.isEmpty()) {
        emit loaded({}, tr("No Steam account was found. Set XMB_STEAM_ID or sign in to Steam."), false);
        return;
    }
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit loaded({}, tr("Set STEAM_WEB_API_KEY to list the games this account owns."), true);
        return;
    }

    const quint64 generation = ++generation_;
    busy_ = true;
    emit busyChanged(true);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    query.addQueryItem(QStringLiteral("steamid"), steamId64);
    query.addQueryItem(QStringLiteral("include_appinfo"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("include_played_free_games"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    QNetworkRequest request{
        QUrl(QStringLiteral("https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/?") +
             query.toString(QUrl::FullyEncoded))};
    // The query string carries the key, so never follow a redirect to another host.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("xmb-native/1.0"));
    auto* reply = network_->get(request);
    reply_ = reply;

    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(RequestTimeoutMs);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, generation](qint64 received, qint64) {
        if (generation != generation_ || received <= MaxResponseBytes) return;
        reply->abort();
        fail(generation, tr("The owned-games response was larger than expected."), false);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const auto networkError = reply->error();
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            // Valve answers 401/403 for a bad key and 400 for a malformed SteamID64.
            if (status == 401 || status == 403)
                fail(generation, tr("Steam rejected the API key or it is not allowed to read this profile."), false);
            else if (status == 400)
                fail(generation, tr("Steam rejected the account id. Set XMB_STEAM_ID to a SteamID64."), false);
            else
                fail(generation, tr("The Steam Web API could not be reached."), false);
            return;
        }
        if (body.size() > MaxResponseBytes) {
            fail(generation, tr("The owned-games response was larger than expected."), false);
            return;
        }
        QJsonParseError parseError{};
        const auto document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(generation, tr("The Steam Web API returned an unexpected response."), false);
            return;
        }
        const auto response = document.object().value(QLatin1String("response")).toObject();
        if (!response.contains(QLatin1String("games"))) {
            // Documented shape for a private or empty profile: response is {} or {game_count:0}.
            if (response.value(QLatin1String("game_count")).toInt(0) == 0) {
                if (generation != generation_) return;
                finish(generation);
                emit loaded({}, tr("This account owns no games, or its game details are private."), false);
                return;
            }
            fail(generation, tr("The Steam Web API returned no game list."), false);
            return;
        }

        QSet<QString> installedIds;
        for (const auto& game : scanSteam().games) installedIds.insert(game.appId);

        OwnedGameList games;
        const auto rows = response.value(QLatin1String("games")).toArray();
        games.reserve(std::min<int>(int(rows.size()), MaxGames));
        for (const auto& value : rows) {
            if (!value.isObject()) continue;
            const auto row = value.toObject();
            OwnedGame game;
            game.appId = QString::number(row.value(QLatin1String("appid")).toInt());
            if (game.appId.isEmpty() || game.appId == QLatin1String("0")) continue;
            game.title = row.value(QLatin1String("name")).toString().trimmed();
            if (game.title.isEmpty()) continue;
            game.iconUrl = iconUrlFor(game.appId, row.value(QLatin1String("img_icon_url")).toString());
            game.headerUrl = headerUrlFor(game.appId);
            game.playtimeMinutes = row.value(QLatin1String("playtime_forever")).toInt();
            game.installed = installedIds.contains(game.appId);
            games.push_back(game);
            if (games.size() >= MaxGames) break;
        }
        std::sort(games.begin(), games.end(), [](const OwnedGame& a, const OwnedGame& b) {
            return a.title.localeAwareCompare(b.title) < 0;
        });
        if (generation != generation_) return;
        finish(generation);
        emit loaded(games, {}, false);
    });
}

void SteamOwned::finish(quint64 generation) {
    if (generation != generation_) return;
    if (busy_) {
        busy_ = false;
        emit busyChanged(false);
    }
}

void SteamOwned::fail(quint64 generation, const QString& message, bool needsKey) {
    if (generation != generation_) return;
    finish(generation);
    emit loaded({}, message, needsKey);
}

QString SteamOwned::installUrl(const QString& appId) {
    const QString trimmed = appId.trimmed();
    if (trimmed.isEmpty() || trimmed.toLongLong() <= 0) return {};
    return QStringLiteral("steam://install/") + trimmed;
}

QString SteamOwned::launchUrl(const QString& appId) {
    const QString trimmed = appId.trimmed();
    if (trimmed.isEmpty() || trimmed.toLongLong() <= 0) return {};
    return QStringLiteral("steam://rungameid/") + trimmed;
}

QString SteamOwned::storeUrl(const QString& appId) {
    const QString trimmed = appId.trimmed();
    if (trimmed.isEmpty() || trimmed.toLongLong() <= 0) return {};
    return QStringLiteral("https://store.steampowered.com/app/") + trimmed;
}

} // namespace xmb
