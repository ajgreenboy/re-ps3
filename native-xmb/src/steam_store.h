#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace xmb {

// One row of Valve's public store catalogue. Prices are in the currency's minor
// units (cents), exactly as the store sends them; -1 means "not priced".
struct StoreItem {
    QString appId;
    QString title;
    QString imageUrl;      // wide capsule when the store offers one
    QString currency;
    int originalPriceCents = -1;
    int finalPriceCents = -1;
    int discountPercent = 0;
    bool discounted = false;
    bool windows = true;
};

using StoreItemList = QVector<StoreItem>;

// Read-only browser for Valve's public storefront endpoints, so the XMB can list
// games that are not installed yet. This class never buys, downloads or launches
// anything: it only reports what the store lists and builds the Steam client
// handoff URLs. Steam remains the owner of purchase, download, DRM and launch.
//
// Endpoints (both public, no API key, no cookies, no account):
//   https://store.steampowered.com/api/storesearch/?term=..&l=en&cc=US
//   https://store.steampowered.com/api/featuredcategories?cc=US&l=en
class SteamStore final : public QObject {
    Q_OBJECT
public:
    explicit SteamStore(QObject* parent = nullptr);
    ~SteamStore() override;

    // Free-text catalogue search. Calls loaded() once with either items or error.
    void search(const QString& term, int page = 1);
    // A featured section of the storefront: "top_sellers", "new_releases" or "specials".
    void featured(const QString& section);
    // Featured top sellers, i.e. a sensible "browse" list before the user types.
    void topSellers();
    // Abandon the in-flight request; no signal is emitted for the cancelled query.
    void cancel();

    // Handoffs the shell opens. A download starts in the Steam client, never here.
    static QString installUrl(const QString& appId); // steam://install/<id>
    static QString storeUrl(const QString& appId);   // store page for a full description
    // "$9.99", "$4.99 (-75%)" or an empty string when the store sends no price.
    static QString priceText(const StoreItem& item);

signals:
    void loaded(xmb::StoreItemList items, QString query, QString error);
    void busyChanged(bool busy);

private:
    void request(const QString& url, const QString& query, bool searchResults, const QString& section = QStringLiteral("top_sellers"));
    void finish(quint64 generation);
    void fail(quint64 generation, const QString& query, const QString& message);

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    quint64 generation_ = 0;
    bool busy_ = false;
};

} // namespace xmb

Q_DECLARE_METATYPE(xmb::StoreItem)
Q_DECLARE_METATYPE(xmb::StoreItemList)
