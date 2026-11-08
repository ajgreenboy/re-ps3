#include "service_icons.h"
#include "glyphs.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QtDebug>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#endif

namespace xmb::serviceIcons {
namespace {
constexpr qint64 MaxIconBytes = 2 * 1024 * 1024;
QImage jellyfinIcon;
// Icons are shown in a square box; a non-square official image is centred on a transparent square.
QImage squared(const QImage& image) {
    if (image.isNull() || image.width() == image.height()) return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int side = std::max(image.width(), image.height());
    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);out.fill(Qt::transparent);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            out.setPixelColor(x + (side - image.width()) / 2, y + (side - image.height()) / 2, image.pixelColor(x, y));
    return out;
}
#ifdef Q_OS_WIN
QImage jumboIcon(const QString& file) {
    SHFILEINFOW info{};
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX)) return {};
    IImageList* list = nullptr;
    if (FAILED(SHGetImageList(SHIL_JUMBO, IID_IImageList, reinterpret_cast<void**>(&list))) || !list) return {};
    HICON icon = nullptr;
    list->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
    list->Release();
    if (!icon) return {};
    QImage image = QImage::fromHICON(icon);
    DestroyIcon(icon);
    // The jumbo list pads small icons into a 256 box; crop to the drawn pixels so it fills the icon box.
    if (!image.isNull()) {
        QRect used;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) > 8) used |= QRect(x, y, 1, 1);
        if (used.isValid() && used.width() >= 24) image = image.copy(used);
        else image = {};
    }
    return image;
}
#endif
}

const QImage& spotify() {
    static const QImage icon = [] {
        QStringList candidates;
        const QString appData = qEnvironmentVariable("APPDATA");
        if (!appData.isEmpty()) candidates << QDir(appData).filePath(QStringLiteral("Spotify/toast_icon.png"));
        for (const auto& path : candidates) {
            if (QFileInfo(path).size() <= 0 || QFileInfo(path).size() > MaxIconBytes) continue;
            QImage image(path);
            if (!image.isNull() && image.width() >= 64) {
                qInfo().noquote() << "service icons: Spotify's own icon from" << QDir::toNativeSeparators(path);
                return squared(image);
            }
        }
        return QImage();
    }();
    return icon.isNull() ? glyphs::spotifyLogo() : icon;
}

const QImage& steam(const QString& steamRoot) {
    static QString loadedFor;
    static QImage icon;
    if (loadedFor != steamRoot) {
        loadedFor = steamRoot;icon = {};
#ifdef Q_OS_WIN
        const QString exe = QDir(steamRoot).filePath(QStringLiteral("steam.exe"));
        if (!steamRoot.isEmpty() && QFileInfo::exists(exe)) icon = squared(jumboIcon(exe));
        if (!icon.isNull()) qInfo().noquote() << "service icons: Steam's own icon from steam.exe" << icon.size();
#endif
    }
    return icon.isNull() ? glyphs::steamLogo() : icon;
}

const QImage& tailscale() {
    static const QImage icon=[]{
        QImage image;
#ifdef Q_OS_WIN
        const QString programFiles=qEnvironmentVariable("ProgramFiles",QStringLiteral("C:/Program Files"));
        const QString exe=QDir(programFiles).filePath(QStringLiteral("Tailscale/tailscale-ipn.exe"));
        if(QFileInfo::exists(exe))image=squared(jumboIcon(exe));
        if(!image.isNull())qInfo().noquote()<<"service icons: Tailscale's own icon from tailscale-ipn.exe"<<image.size();
#endif
        return image;
    }();
    return icon.isNull()?glyphs::tailscaleLogo():icon;
}

QHash<QString, QImage> siteIcons;
QSet<QString> siteIconsAsked;

const QImage& siteIcon(const QUrl& url) {
    static const QImage none;
    const auto found = siteIcons.constFind(url.host().toLower());
    return found == siteIcons.cend() ? none : *found;
}

namespace {
// Tries the candidates in order and keeps the largest picture; stops early at 180 px (an apple-touch-icon).
void fetchIconCandidate(QNetworkAccessManager* network, QStringList candidates, const QString& host, const QString& cacheFile,
                        QImage best, std::function<void()> done) {
    const auto finish = [&](const QImage& image) {
        if (image.isNull()) { qInfo().noquote() << "service icons: no icon found for" << host;return; }
        siteIcons.insert(host, squared(image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        if (QDir().mkpath(QFileInfo(cacheFile).absolutePath())) siteIcons.value(host).save(cacheFile, "PNG");
        qInfo().noquote() << "service icons:" << host << "icon" << image.size();
        if (done) done();
    };
    if (candidates.isEmpty() || best.width() >= 180) { finish(best);return; }
    const QUrl url(candidates.takeFirst());
    QNetworkRequest request(url);request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) { if (received > MaxIconBytes) reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, reply, [network, reply, candidates, host, cacheFile, best, done] {
        reply->deleteLater();
        const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->read(MaxIconBytes + 1) : QByteArray();
        QImage image;
        if (bytes.size() <= MaxIconBytes) image.loadFromData(bytes);
        const QImage keep = (!image.isNull() && image.width() >= 32 && image.width() > best.width()) ? image : best;
        fetchIconCandidate(network, candidates, host, cacheFile, keep, done);
    });
}

// The <link> icons a page declares, largest first (an apple-touch-icon without sizes counts as 180).
QStringList declaredIcons(const QString& html, const QUrl& base) {
    const QRegularExpression link(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression rel(QStringLiteral("rel=[\"']([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression href(QStringLiteral("href=[\"']([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression sizes(QStringLiteral("sizes=[\"'](\\d+)x"), QRegularExpression::CaseInsensitiveOption);
    QVector<QPair<int, QString>> found;
    for (auto it = link.globalMatch(html); it.hasNext();) {
        const QString tag = it.next().captured(0);
        const QString r = rel.match(tag).captured(1).toLower(), h = href.match(tag).captured(1);
        if (h.isEmpty() || h.startsWith(QLatin1String("data:"))) continue;
        const bool touch = r.contains(QLatin1String("apple-touch-icon"));
        if (!touch && !r.split(QLatin1Char(' ')).contains(QLatin1String("icon"))) continue;
        int size = sizes.match(tag).captured(1).toInt();
        if (size == 0) size = touch ? 180 : 16;
        found.push_back({size, base.resolved(QUrl(h)).toString()});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QStringList out;for (const auto& f : found) out << f.second;
    return out;
}

void readPage(QNetworkAccessManager* network, const QUrl& url, const QString& host, const QString& cacheFile, bool rootTried,
              std::function<void()> done, QStringList extra = {}) {
    QNetworkRequest request(url);request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) { if (received > MaxIconBytes) reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, reply, [network, reply, url, host, cacheFile, rootTried, done, extra] {
        reply->deleteLater();
        const QString html = QString::fromUtf8(reply->read(MaxIconBytes));
        const QUrl base = reply->url().isValid() ? reply->url() : url;
        QStringList candidates = declaredIcons(html, base);
        qInfo().noquote() << "service icons:" << host << "page" << base.toString() << html.size() << "chars," << candidates.size() << "declared icon(s)";
        // An app page (/tv, /web) often declares only a small favicon; the site's root page declares the real sizes, so
        // it is read too and its icons are tried first.
        if (!rootTried) {
            QUrl root = base;root.setPath(QStringLiteral("/"));root.setQuery(QString());root.setFragment(QString());
            if (root != base) { readPage(network, root, host, cacheFile, true, done, candidates);return; }
        }
        candidates << extra;
        QUrl conventional = base;conventional.setQuery(QString());
        conventional.setPath(QStringLiteral("/apple-touch-icon.png"));candidates << conventional.toString();
        conventional.setPath(QStringLiteral("/favicon.ico"));candidates << conventional.toString();
        candidates.removeDuplicates();
        fetchIconCandidate(network, candidates.mid(0, 6), host, cacheFile, QImage(), done);
    });
}
}

void loadSiteIcon(QNetworkAccessManager* network, const QUrl& url, const QString& cacheDirectory, std::function<void()> done) {
    const QString host = url.host().toLower();
    if (host.isEmpty() || siteIcons.contains(host) || siteIconsAsked.contains(host)) return;
    siteIconsAsked.insert(host);
    const QString cacheFile = QDir(cacheDirectory).filePath(QString(host).replace(QLatin1Char(':'), QLatin1Char('_')) + QStringLiteral(".png"));
    if (QFileInfo(cacheFile).size() > 0 && QFileInfo(cacheFile).size() <= MaxIconBytes) {
        QImage cached(cacheFile);
        if (!cached.isNull()) { siteIcons.insert(host, cached);if (done) done();return; }
    }
    if (!network || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) return;
    readPage(network, url, host, cacheFile, false, done);
}

QImage homeAssistantIcon;
const QImage& homeAssistant() { return homeAssistantIcon.isNull() ? glyphs::house() : homeAssistantIcon; }

void loadHomeAssistant(QNetworkAccessManager* network, const QUrl& server, const QString& cacheFile, std::function<void()> done) {
    if (homeAssistantIcon.isNull() && QFileInfo(cacheFile).size() > 0 && QFileInfo(cacheFile).size() <= MaxIconBytes) {
        QImage cached(cacheFile);
        if (!cached.isNull()) { homeAssistantIcon = squared(cached);if (done) done();return; }
    }
    if (!homeAssistantIcon.isNull() || !network || !server.isValid() ||
        (server.scheme() != QLatin1String("http") && server.scheme() != QLatin1String("https"))) return;
    // Home Assistant serves its own icon to its web frontend without sign-in.
    QUrl url = server;url.setPath(QStringLiteral("/static/icons/favicon-1024x1024.png"));url.setQuery(QString());
    QNetworkRequest request(url);request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) { if (received > MaxIconBytes) reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cacheFile, done] {
        reply->deleteLater();
        const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->read(MaxIconBytes + 1) : QByteArray();
        QImage decoded;
        if (bytes.size() <= MaxIconBytes) decoded.loadFromData(bytes);
        if (decoded.isNull() || decoded.width() < 32) return;
        homeAssistantIcon = squared(decoded.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (!cacheFile.isEmpty() && QDir().mkpath(QFileInfo(cacheFile).absolutePath())) {
            QSaveFile out(cacheFile);
            if (out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size()) out.commit();
        }
        qInfo().noquote() << "service icons: Home Assistant's own icon from the server" << decoded.size();
        if (done) done();
    });
}

const QImage& jellyfin() { return jellyfinIcon.isNull() ? glyphs::jellyfinLogo() : jellyfinIcon; }

void loadJellyfin(QNetworkAccessManager* network, const QUrl& server, const QString& cacheFile, std::function<void()> done) {
    if (jellyfinIcon.isNull() && QFileInfo(cacheFile).size() > 0 && QFileInfo(cacheFile).size() <= MaxIconBytes) {
        QImage cached(cacheFile);
        if (!cached.isNull()) { jellyfinIcon = squared(cached);if (done) done();return; }
    }
    if (!jellyfinIcon.isNull() || !network || !server.isValid() ||
        (server.scheme() != QLatin1String("http") && server.scheme() != QLatin1String("https"))) return;
    // The web client's index names its touch icon (a hashed file name); fetch that page, then the icon.
    QUrl base = server;QString path = base.path();
    if (!path.endsWith(QLatin1Char('/'))) path += QLatin1Char('/');
    base.setPath(path);base.setQuery(QString());base.setFragment(QString());
    const QUrl index = base.resolved(QUrl(QStringLiteral("web/index.html")));
    QNetworkRequest request(index);request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, [network, reply, index, cacheFile, done] {
        reply->deleteLater();
        const QString html = QString::fromUtf8(reply->read(MaxIconBytes));
        const auto match = QRegularExpression(QStringLiteral("href=\"(touchicon[A-Za-z0-9._-]*\\.png)\"")).match(html);
        if (reply->error() != QNetworkReply::NoError || !match.hasMatch()) {
            qInfo().noquote() << "service icons: Jellyfin's web client names no touch icon; keeping the drawn mark";
            return;
        }
        const QUrl iconUrl = index.resolved(QUrl(match.captured(1)));
        if (iconUrl.host() != index.host()) return;
        QNetworkRequest get(iconUrl);get.setTransferTimeout(8000);
        auto* image = network->get(get);
        QObject::connect(image, &QNetworkReply::downloadProgress, image, [image](qint64 received, qint64) { if (received > MaxIconBytes) image->abort(); });
        QObject::connect(image, &QNetworkReply::finished, image, [image, cacheFile, done] {
            image->deleteLater();
            const QByteArray bytes = image->error() == QNetworkReply::NoError ? image->read(MaxIconBytes + 1) : QByteArray();
            QImage decoded;
            if (bytes.size() <= MaxIconBytes) decoded.loadFromData(bytes);
            if (decoded.isNull() || decoded.width() < 32) return;
            jellyfinIcon = squared(decoded);
            if (!cacheFile.isEmpty() && QDir().mkpath(QFileInfo(cacheFile).absolutePath())) {
                QSaveFile out(cacheFile);
                if (out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size()) out.commit();
            }
            qInfo().noquote() << "service icons: Jellyfin's own icon from the server" << decoded.size();
            if (done) done();
        });
    });
}
}
