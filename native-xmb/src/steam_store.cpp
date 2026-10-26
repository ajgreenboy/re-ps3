#include "steam_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

namespace xmb {
namespace {
constexpr int RequestTimeoutMs = 12000;
constexpr qint64 MaxResponseBytes = 4 * 1024 * 1024;
constexpr int MaxItems = 60;

// The store sends URL-encoded JSON with escaped slashes in image URLs; QJsonDocument
// does not decode inside strings, so restore them for Qt's own URL loader.
QString decodeUrl(const QString& raw) {
    return QString(raw).replace(QStringLiteral("\\/"), QStringLiteral("/"));
}

QString firstString(const QJsonObject& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const auto value = object.value(QLatin1String(key));
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty()) return text;
        }
    }
    return {};
}

int firstInt(const QJsonObject& object, std::initializer_list<const char*> keys, int fallback = -1) {
    for (const char* key : keys) {
        const auto value = object.value(QLatin1String(key));
        if (value.isDouble()) return value.toInt();
        if (value.isString()) {
            bool ok = false;
            const int parsed = value.toString().toInt(&ok);
            if (ok) return parsed;
        }
    }
    return fallback;
}

StoreItem parseItem(const QJsonObject& object) {
    StoreItem item;
    item.appId = QString::number(firstInt(object, {"id", "appid", "app_id"}));
    item.title = firstString(object, {"name", "title"});
    item.imageUrl = decodeUrl(firstString(object, {"large_capsule_image", "header_image", "tiny_image", "small_capsule_image"}));
    item.currency = firstString(object, {"currency"});
    item.discounted = object.value(QLatin1String("discounted")).toBool(false);
    item.discountPercent = firstInt(object, {"discount_percent"}, 0);
    if (const auto price = object.value(QLatin1String("price")); price.isObject()) {
        const auto p = price.toObject();
        item.currency = firstString(p, {"currency"});
        item.originalPriceCents = firstInt(p, {"initial"});
        item.finalPriceCents = firstInt(p, {"final"});
    }
    if (item.originalPriceCents < 0) item.originalPriceCents = firstInt(object, {"original_price", "initial_price"});
    if (item.finalPriceCents < 0) item.finalPriceCents = firstInt(object, {"final_price", "price_overview"});
    if (const auto platforms = object.value(QLatin1String("platforms")); platforms.isObject())
        item.windows = platforms.toObject().value(QLatin1String("windows")).toBool(true);
    else
        item.windows = object.value(QLatin1String("windows_available")).toBool(true);
    return item;
}

// A store row without an ID or a title is unusable; drop it rather than show a blank card.
bool usable(const StoreItem& item) {
    return !item.appId.isEmpty() && item.appId != QLatin1String("0") && !item.title.isEmpty();
}
} // namespace

SteamStore::SteamStore(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<StoreItemList>("xmb::StoreItemList");
    qRegisterMetaType<StoreItem>("xmb::StoreItem");
}

SteamStore::~SteamStore() { cancel(); }

void SteamStore::cancel() {
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

void SteamStore::search(const QString& term, int page) {
    const QString trimmed = term.trimmed();
    if (trimmed.isEmpty()) {
        emit loaded({}, {}, tr("Enter a title to search the Steam store."));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("term"), trimmed);
    query.addQueryItem(QStringLiteral("l"), QStringLiteral("en"));
    query.addQueryItem(QStringLiteral("cc"), QStringLiteral("US"));
    if (page > 1) query.addQueryItem(QStringLiteral("page"), QString::number(page));
    request(QStringLiteral("https://store.steampowered.com/api/storesearch/?") + query.toString(QUrl::FullyEncoded),
            tr("Search “%1”").arg(trimmed), true);
}

void SteamStore::topSellers() {
    request(QStringLiteral("https://store.steampowered.com/api/featuredcategories?cc=US&l=en"),
            tr("Steam Top Sellers"), false);
}

void SteamStore::request(const QString& url, const QString& label, bool searchResults) {
    cancel();
    const quint64 generation = ++generation_;
    busy_ = true;
    emit busyChanged(true);

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("xmb-native/1.0"));
    request.setRawHeader("Accept", "application/json");
    auto* reply = network_->get(request);
    reply_ = reply;

    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(RequestTimeoutMs);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, generation](qint64 received, qint64) {
        if (generation != generation_ || received <= MaxResponseBytes) return;
        reply->abort();
        fail(generation, {}, tr("The store response was larger than expected."));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, label, searchResults, generation] {
        reply_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            const QString reason = reply->errorString();
            reply->deleteLater();
            fail(generation, label, reason.isEmpty() ? tr("The Steam store could not be reached.") : reason);
            return;
        }
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (body.size() > MaxResponseBytes) {
            fail(generation, label, tr("The store response was larger than expected."));
            return;
        }
        QJsonParseError parseError{};
        const auto document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(generation, label, tr("The Steam store returned an unexpected response."));
            return;
        }
        const auto root = document.object();
        QVector<QJsonObject> rows;
        if (searchResults) {
            for (const auto& value : root.value(QLatin1String("items")).toArray())
                if (value.isObject()) rows.push_back(value.toObject());
        } else {
            // featuredcategories: {"top_sellers":{"items":[..]},"specials":{"items":[..]},..}
            const auto sellers = root.value(QLatin1String("top_sellers")).toObject();
            for (const auto& value : sellers.value(QLatin1String("items")).toArray())
                if (value.isObject()) rows.push_back(value.toObject());
        }
        StoreItemList items;
        QSet<QString> seen;
        items.reserve(std::min<int>(int(rows.size()), MaxItems));
        for (const auto& row : rows) {
            const auto item = parseItem(row);
            if (!usable(item) || seen.contains(item.appId)) continue;
            seen.insert(item.appId);
            items.push_back(item);
            if (items.size() >= MaxItems) break;
        }
        if (generation != generation_) return;
        finish(generation);
        emit loaded(items, label, items.isEmpty() ? tr("The Steam store listed nothing for this selection.") : QString());
    });
}

void SteamStore::finish(quint64 generation) {
    if (generation != generation_) return;
    if (busy_) {
        busy_ = false;
        emit busyChanged(false);
    }
}

void SteamStore::fail(quint64 generation, const QString& label, const QString& message) {
    if (generation != generation_) return;
    finish(generation);
    emit loaded({}, label, message);
}

QString SteamStore::installUrl(const QString& appId) {
    const QString trimmed = appId.trimmed();
    if (trimmed.isEmpty() || trimmed.toLongLong() <= 0) return {};
    return QStringLiteral("steam://install/") + trimmed;
}

QString SteamStore::storeUrl(const QString& appId) {
    const QString trimmed = appId.trimmed();
    if (trimmed.isEmpty() || trimmed.toLongLong() <= 0) return {};
    return QStringLiteral("https://store.steampowered.com/app/") + trimmed;
}

QString SteamStore::priceText(const StoreItem& item) {
    const auto money = [](const QString& currency, int cents) {
        const QString amount = QString::number(cents / 100.0, 'f', 2);
        if (currency.compare(QLatin1String("USD"), Qt::CaseInsensitive) == 0) return QStringLiteral("$") + amount;
        return currency.isEmpty() ? amount : amount + QLatin1Char(' ') + currency;
    };
    if (item.finalPriceCents < 0) return {};
    if (item.finalPriceCents == 0) return QObject::tr("Free");
    QString text = money(item.currency, item.finalPriceCents);
    if (item.discounted && item.originalPriceCents > item.finalPriceCents) {
        text = QStringLiteral("%1 (-%2%)")
                   .arg(text)
                   .arg(item.discountPercent > 0 ? item.discountPercent
                                                 : int(100.0 - 100.0 * item.finalPriceCents / item.originalPriceCents));
    }
    return text;
}

} // namespace xmb
