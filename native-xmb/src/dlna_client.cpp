#include "dlna_client.h"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUdpSocket>
#include <QXmlStreamReader>
#include <QDebug>
#include <algorithm>
#include <limits>
#include <utility>

namespace xmb {
namespace {
constexpr int TransferTimeoutMs = 10000;
constexpr qint64 MaxResponseBytes = 4 * 1024 * 1024;
constexpr int MaxLocations = 64;
constexpr int MaxItems = 5000;
constexpr int MaxDeviceDepth = 4;
constexpr int MaxListEntries = 32;
constexpr int SearchSends = 3;
const QHostAddress SsdpGroup(QStringLiteral("239.255.255.250"));
constexpr quint16 SsdpPort = 1900;
const QString MediaServerType = QStringLiteral("urn:schemas-upnp-org:device:MediaServer:1");
const QByteArray UserAgent = QByteArrayLiteral("Windows/10 UPnP/1.0 XMB/1.0");

bool httpUrl(const QUrl& url) {
    if (!url.isValid() || url.host().isEmpty()) return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// A LAN server's address: IPv4/IPv6 private ranges, link-local or loopback. A device on the network
// answering SSDP must not be able to steer this client at an internet host.
bool localAddress(const QHostAddress& address) {
    if (address.isNull()) return false;
    bool v4 = false;
    const QHostAddress plain(address.toIPv4Address(&v4));
    const QHostAddress& a = v4 ? plain : address;
    return a.isLoopback() || a.isLinkLocal() || a.isPrivateUse();
}

bool validLocation(const QUrl& url) {
    return httpUrl(url) && localAddress(QHostAddress(url.host()));
}

bool sameHost(const QUrl& a, const QUrl& b) {
    const QHostAddress ha(a.host()), hb(b.host());
    if (!ha.isNull() && !hb.isNull()) return ha.isEqual(hb, QHostAddress::TolerantConversion);
    return a.host().compare(b.host(), Qt::CaseInsensitive) == 0;
}

QString clipped(QString text, int limit = 512) {
    text = text.trimmed();
    if (text.size() > limit) text.truncate(limit);
    return text;
}

QString readText(QXmlStreamReader& xml, int limit = 512) {
    return clipped(xml.readElementText(QXmlStreamReader::SkipChildElements), limit);
}

// ---- device description -----------------------------------------------------------------
struct DescIcon { QString mime, url; qint64 area = 0; };
struct DescService { QString type, control; };
struct DescDevice {
    QString type, name, udn;
    QVector<DescIcon> icons;
    QVector<DescService> services;
    QVector<DescDevice> children;
};

void readIcon(QXmlStreamReader& xml, DescDevice& device) {
    DescIcon icon;
    qint64 width = 0, height = 0;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == u"mimetype") icon.mime = readText(xml, 64).toLower();
        else if (name == u"width") width = std::clamp<qint64>(readText(xml, 16).toLongLong(), 0, 65535);
        else if (name == u"height") height = std::clamp<qint64>(readText(xml, 16).toLongLong(), 0, 65535);
        else if (name == u"url") icon.url = readText(xml, 2048);
        else xml.skipCurrentElement();
    }
    icon.area = width * height;
    if (device.icons.size() < MaxListEntries) device.icons.push_back(icon);
}

void readService(QXmlStreamReader& xml, DescDevice& device) {
    DescService service;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == u"serviceType") service.type = readText(xml, 256);
        else if (name == u"controlURL") service.control = readText(xml, 2048);
        else xml.skipCurrentElement();
    }
    if (device.services.size() < MaxListEntries) device.services.push_back(service);
}

// Positioned on <device>; returns at its end element (or on a reader error, which stops the walk).
void readDevice(QXmlStreamReader& xml, DescDevice& device, int depth) {
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == u"deviceType") device.type = readText(xml, 256);
        else if (name == u"friendlyName") device.name = readText(xml, 256);
        else if (name == u"UDN") device.udn = readText(xml, 256);
        else if (name == u"iconList") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"icon") readIcon(xml, device);
                else xml.skipCurrentElement();
            }
        } else if (name == u"serviceList") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"service") readService(xml, device);
                else xml.skipCurrentElement();
            }
        } else if (name == u"deviceList") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"device" && depth < MaxDeviceDepth && device.children.size() < MaxListEntries) {
                    DescDevice child;
                    readDevice(xml, child, depth + 1);
                    device.children.push_back(std::move(child));
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

const DescService* contentDirectory(const DescDevice& device) {
    for (const DescService& service : device.services)
        if (service.type.contains(QLatin1String(":service:ContentDirectory:"), Qt::CaseInsensitive) &&
            !service.control.isEmpty()) return &service;
    return nullptr;
}

// The MediaServer device with a ContentDirectory, searched depth-first; failing that, any device
// that has a ContentDirectory at all.
const DescDevice* mediaServer(const DescDevice& device, bool strict) {
    if (contentDirectory(device) &&
        (!strict || device.type.contains(QLatin1String(":device:MediaServer:"), Qt::CaseInsensitive)))
        return &device;
    for (const DescDevice& child : device.children)
        if (const DescDevice* found = mediaServer(child, strict)) return found;
    return nullptr;
}

bool parseDescription(const QByteArray& bytes, DescDevice& root, QString& urlBase, QString& error) {
    QXmlStreamReader xml(bytes);
    bool sawDevice = false;
    if (xml.readNextStartElement() && xml.name() == u"root") {
        while (xml.readNextStartElement()) {
            if (xml.name() == u"URLBase") urlBase = readText(xml, 2048);
            else if (xml.name() == u"device" && !sawDevice) { readDevice(xml, root, 0); sawDevice = true; }
            else xml.skipCurrentElement();
        }
    }
    if (xml.hasError() && !sawDevice) { error = QStringLiteral("malformed description XML: ") + xml.errorString(); return false; }
    if (!sawDevice) { error = QStringLiteral("description has no <root><device>"); return false; }
    return true;
}

// ---- SOAP / DIDL-Lite -------------------------------------------------------------------
qint64 parseDuration(QString text) {
    text = text.trimmed();
    if (text.isEmpty() || text.size() > 32) return 0;
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 3) return 0;
    bool ok = false;
    const qint64 hours = parts[0].toLongLong(&ok);
    if (!ok || hours < 0 || hours > 100000) return 0;
    const int minutes = parts[1].toInt(&ok);
    if (!ok || minutes < 0 || minutes > 59) return 0;
    QString secondsText = parts[2];
    qint64 fractionMs = 0;
    const int dot = secondsText.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        const QString fraction = secondsText.mid(dot + 1);
        secondsText.truncate(dot);
        const int slash = fraction.indexOf(QLatin1Char('/'));
        if (slash >= 0) {   // DLNA's F0/F1 form
            const qint64 numerator = fraction.left(slash).toLongLong(&ok);
            bool okDen = false;
            const qint64 denominator = fraction.mid(slash + 1).toLongLong(&okDen);
            if (ok && okDen && denominator > 0 && numerator >= 0 && numerator < denominator && denominator < 1000000)
                fractionMs = numerator * 1000 / denominator;
        } else if (!fraction.isEmpty()) {
            QString digits = fraction.left(3);
            while (digits.size() < 3) digits += QLatin1Char('0');
            fractionMs = std::max(0, digits.toInt(&ok));
            if (!ok) fractionMs = 0;
        }
    }
    const int seconds = secondsText.toInt(&ok);
    if (!ok || seconds < 0 || seconds > 59) return 0;
    return ((hours * 60 + minutes) * 60 + seconds) * 1000 + fractionMs;
}

QString mimeFromProtocolInfo(const QString& protocolInfo) {
    const QStringList fields = protocolInfo.split(QLatin1Char(':'));
    return fields.size() >= 3 ? clipped(fields[2], 128).toLower() : QString{};
}

QString mimePrefixForClass(const QString& upnpClass) {
    if (upnpClass.startsWith(QLatin1String("object.item.videoItem"))) return QStringLiteral("video/");
    if (upnpClass.startsWith(QLatin1String("object.item.audioItem"))) return QStringLiteral("audio/");
    if (upnpClass.startsWith(QLatin1String("object.item.imageItem"))) return QStringLiteral("image/");
    return {};
}

struct Resource { QString url, mime; qint64 durationMs = 0, size = 0; };

QString safeUrl(const QString& text) {
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    return httpUrl(url) ? url.toString(QUrl::FullyEncoded) : QString{};
}

// Positioned on <item> or <container>.
DlnaItem readEntry(QXmlStreamReader& xml) {
    DlnaItem item;
    item.container = xml.name() == u"container";
    const QXmlStreamAttributes attributes = xml.attributes();
    item.id = clipped(attributes.value(QLatin1String("id")).toString(), 1024);
    item.parentId = clipped(attributes.value(QLatin1String("parentID")).toString(), 1024);
    if (attributes.hasAttribute(QLatin1String("childCount"))) {
        bool ok = false;
        const int count = attributes.value(QLatin1String("childCount")).toInt(&ok);
        if (ok && count >= 0) item.childCount = count;
    }
    QVector<Resource> resources;
    QString creator;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == u"title") item.title = readText(xml, 512);
        else if (name == u"class") item.upnpClass = readText(xml, 128);
        else if (name == u"album" && item.album.isEmpty()) item.album = readText(xml, 512);
        else if (name == u"artist" && item.artist.isEmpty()) item.artist = readText(xml, 512);
        else if (name == u"creator" && creator.isEmpty()) creator = readText(xml, 512);
        else if (name == u"albumArtURI" && item.albumArtUrl.isEmpty()) item.albumArtUrl = safeUrl(readText(xml, 4096));
        else if (name == u"res" && resources.size() < MaxListEntries) {
            const QXmlStreamAttributes res = xml.attributes();
            Resource resource;
            resource.mime = mimeFromProtocolInfo(res.value(QLatin1String("protocolInfo")).toString());
            resource.durationMs = parseDuration(res.value(QLatin1String("duration")).toString());
            bool ok = false;
            const qint64 size = res.value(QLatin1String("size")).toLongLong(&ok);
            if (ok && size >= 0) resource.size = size;
            resource.url = safeUrl(readText(xml, 4096));
            if (!resource.url.isEmpty()) resources.push_back(resource);
        } else {
            xml.skipCurrentElement();
        }
    }
    if (item.artist.isEmpty()) item.artist = creator;
    if (!resources.isEmpty()) {
        const QString prefix = mimePrefixForClass(item.upnpClass);
        const Resource* chosen = &resources.first();
        if (!prefix.isEmpty()) {
            for (const Resource& resource : resources)
                if (resource.mime.startsWith(prefix)) { chosen = &resource; break; }
        }
        item.url = chosen->url;
        item.mimeType = chosen->mime;
        item.size = chosen->size;
        item.durationMs = chosen->durationMs;
        if (item.durationMs == 0)
            for (const Resource& resource : resources)
                if (resource.durationMs > 0) { item.durationMs = resource.durationMs; break; }
    }
    return item;
}

QVector<DlnaItem> parseDidl(const QString& didl, QString& error) {
    QVector<DlnaItem> items;
    QXmlStreamReader xml(didl);
    while (!xml.atEnd() && items.size() < MaxItems) {
        xml.readNext();
        if (xml.isStartElement() && (xml.name() == u"item" || xml.name() == u"container"))
            items.push_back(readEntry(xml));
    }
    if (xml.hasError() && xml.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        error = xml.errorString();
    else if (xml.hasError() && items.isEmpty())
        error = xml.errorString();
    return items;
}

struct BrowseResponse {
    QString result, faultCode, faultText;
    int totalMatches = -1, numberReturned = -1;
    bool sawResult = false;
};

BrowseResponse parseSoap(const QByteArray& bytes) {
    BrowseResponse response;
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto name = xml.name();
        if (name == u"Result") {
            response.result = xml.readElementText(QXmlStreamReader::SkipChildElements);
            response.sawResult = true;
        } else if (name == u"TotalMatches") {
            bool ok = false;
            const int value = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed().toInt(&ok);
            if (ok && value >= 0) response.totalMatches = value;
        } else if (name == u"NumberReturned") {
            bool ok = false;
            const int value = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed().toInt(&ok);
            if (ok && value >= 0) response.numberReturned = value;
        } else if (name == u"errorCode") {
            response.faultCode = readText(xml, 32);
        } else if (name == u"errorDescription") {
            response.faultText = readText(xml, 256);
        } else if (name == u"faultstring" && response.faultText.isEmpty()) {
            response.faultText = readText(xml, 256);
        }
    }
    return response;
}

bool validObjectId(const QString& id) {
    if (id.isEmpty() || id.size() > 1024) return false;
    for (const QChar ch : id)
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}

QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(TransferTimeoutMs);
    request.setRawHeader("User-Agent", UserAgent);
    return request;
}

bool bodyWithinLimit(QNetworkReply* reply, QByteArray& bytes) {
    bytes.clear();
    if (!reply->isOpen()) return !reply->property("dlnaAbort").toString().contains(QLatin1String("4 MB"));
    bytes = reply->read(MaxResponseBytes + 1);
    return bytes.size() <= MaxResponseBytes;
}
} // namespace

DlnaClient::DlnaClient(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<xmb::DlnaServer>();
    qRegisterMetaType<xmb::DlnaItem>();
    qRegisterMetaType<QVector<xmb::DlnaServer>>();
    qRegisterMetaType<QVector<xmb::DlnaItem>>();
}

DlnaClient::~DlnaClient() {
    ++discoveryGeneration_;
    stopDiscovery();
    cancelBrowses();
}

void DlnaClient::setUnicastTargets(QVector<QHostAddress> hosts) {
    unicastTargets_.clear();
    for (const QHostAddress& host : hosts)
        if (localAddress(host) && unicastTargets_.size() < MaxListEntries) unicastTargets_.push_back(host);
}

void DlnaClient::setKnownLocations(QVector<QUrl> locations) {
    knownLocations_.clear();
    for (const QUrl& url : locations)
        if (validLocation(url) && knownLocations_.size() < MaxListEntries) knownLocations_.push_back(url);
}

QNetworkReply* DlnaClient::track(QNetworkReply* reply) {
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply] {
        reply->setProperty("dlnaAbort", QStringLiteral("timed out after 10 s"));
        reply->abort();
    });
    timeout->start(TransferTimeoutMs);
    auto oversize = [reply] {
        reply->setProperty("dlnaAbort", QStringLiteral("response exceeds 4 MB"));
        reply->abort();
    };
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, oversize] {
        const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() > MaxResponseBytes) oversize();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [oversize](qint64 received, qint64) {
        if (received > MaxResponseBytes) oversize();
    });
    return reply;
}

// Why a reply failed, in words: this client's own aborts (size, timeout) say so instead of Qt's
// generic "Operation canceled".
QString DlnaClient::replyError(QNetworkReply* reply) {
    const QString own = reply->property("dlnaAbort").toString();
    return own.isEmpty() ? reply->errorString() : own;
}

// ---- discovery ----------------------------------------------------------------------------
void DlnaClient::discover(int timeoutMs) {
    ++discoveryGeneration_;
    stopDiscovery();
    found_.clear();
    foundUdns_.clear();
    seenLocations_.clear();
    timeoutMs = std::clamp(timeoutMs, 1000, 30000);

    // One socket per multicast-capable IPv4 interface, so the search leaves every LAN adapter and
    // not only the one Windows routes 239.255.255.250 through; plus one unbound-interface socket.
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            !(flags & QNetworkInterface::CanMulticast) || (flags & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            auto* socket = new QUdpSocket(this);
            if (!socket->bind(entry.ip(), 0)) { delete socket; continue; }
            socket->setMulticastInterface(iface);
            socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 2);
            socket->setProperty("multicast", true);
            connect(socket, &QUdpSocket::readyRead, this, [this, socket] { readDatagrams(socket); });
            sockets_.push_back(socket);
        }
    }
    auto* any = new QUdpSocket(this);
    if (any->bind(QHostAddress::AnyIPv4, 0)) {
        any->setSocketOption(QAbstractSocket::MulticastTtlOption, 2);
        any->setProperty("multicast", sockets_.isEmpty());
        connect(any, &QUdpSocket::readyRead, this, [this, any] { readDatagrams(any); });
        sockets_.push_back(any);
    } else {
        delete any;
    }

    const quint64 generation = discoveryGeneration_;
    discoveryTimer_ = new QTimer(this);
    discoveryTimer_->setSingleShot(true);
    connect(discoveryTimer_, &QTimer::timeout, this, [this, generation] {
        if (generation == discoveryGeneration_) finishDiscovery();
    });
    discoveryTimer_->start(timeoutMs);

    sendSearch();
    for (int i = 1; i < SearchSends; ++i) {
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, generation] {
            if (generation == discoveryGeneration_) sendSearch();
        });
        timer->start(std::min(timeoutMs / 2, 400 * i));
        searchTimers_.push_back(timer);
    }
    for (const QUrl& location : knownLocations_) fetchDescription(location);
    qInfo().noquote() << "dlna: discovery started on" << sockets_.size() << "socket(s),"
                      << unicastTargets_.size() << "unicast target(s)," << knownLocations_.size()
                      << "known location(s)";
}

void DlnaClient::sendSearch() {
    const int mx = std::clamp(discoveryTimer_ ? discoveryTimer_->remainingTime() / 1000 - 1 : 1, 1, 3);
    auto message = [mx](const QString& host) {
        return QStringLiteral("M-SEARCH * HTTP/1.1\r\nHOST: %1\r\nMAN: \"ssdp:discover\"\r\nMX: %2\r\n"
                              "ST: %3\r\nUSER-AGENT: %4\r\n\r\n")
            .arg(host).arg(mx).arg(MediaServerType, QString::fromLatin1(UserAgent)).toLatin1();
    };
    const QByteArray multicast = message(QStringLiteral("239.255.255.250:1900"));
    for (QUdpSocket* socket : std::as_const(sockets_)) {
        if (socket->property("multicast").toBool()) socket->writeDatagram(multicast, SsdpGroup, SsdpPort);
    }
    if (!unicastTargets_.isEmpty() && !sockets_.isEmpty()) {
        QUdpSocket* socket = sockets_.last();
        for (const QHostAddress& host : std::as_const(unicastTargets_))
            socket->writeDatagram(message(host.toString() + QStringLiteral(":1900")), host, SsdpPort);
    }
}

void DlnaClient::readDatagrams(QUdpSocket* socket) {
    while (socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = socket->receiveDatagram(8192);
        if (!discoveryTimer_) continue;
        const QString text = QString::fromLatin1(datagram.data());
        const QStringList lines = text.split(QLatin1Char('\n'));
        if (lines.isEmpty() || !lines.first().startsWith(QLatin1String("HTTP/1."), Qt::CaseInsensitive) ||
            !lines.first().contains(QLatin1String(" 200"))) continue;
        QString location, st;
        for (int i = 1; i < lines.size() && i < 64; ++i) {
            const QString line = lines[i].trimmed();
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon <= 0) continue;
            const QString key = line.left(colon).trimmed().toUpper();
            const QString value = line.mid(colon + 1).trimmed();
            if (key == QLatin1String("LOCATION")) location = value;
            else if (key == QLatin1String("ST")) st = value;
        }
        if (location.isEmpty()) continue;
        if (!st.isEmpty() && !st.contains(QLatin1String("MediaServer"), Qt::CaseInsensitive) &&
            !st.contains(QLatin1String("ContentDirectory"), Qt::CaseInsensitive)) continue;
        const QUrl url(location, QUrl::StrictMode);
        if (!validLocation(url)) {
            qInfo().noquote() << "dlna: ignoring LOCATION outside the LAN from"
                              << datagram.senderAddress().toString();
            continue;
        }
        fetchDescription(url);
    }
}

void DlnaClient::fetchDescription(const QUrl& location) {
    const QString key = location.toString(QUrl::FullyEncoded);
    if (seenLocations_.contains(key) || seenLocations_.size() >= MaxLocations) return;
    seenLocations_.insert(key);
    QNetworkRequest request = makeRequest(location);
    request.setRawHeader("Accept", "text/xml, application/xml");
    QNetworkReply* reply = track(network_->get(request));
    descriptionReplies_.push_back(reply);
    const quint64 generation = discoveryGeneration_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, location, generation] {
        descriptionFinished(reply, location, generation);
    });
}

void DlnaClient::descriptionFinished(QNetworkReply* reply, const QUrl& location, quint64 generation) {
    descriptionReplies_.removeAll(reply);
    reply->deleteLater();
    if (generation != discoveryGeneration_ || !discoveryTimer_) return;
    const QString where = location.host() + QLatin1Char(':') + QString::number(location.port(80));
    if (reply->error() != QNetworkReply::NoError) {
        qInfo().noquote() << "dlna: description" << where << "failed:" << replyError(reply);
        return;
    }
    QByteArray bytes;
    if (!bodyWithinLimit(reply, bytes)) {
        qInfo().noquote() << "dlna: description" << where << "exceeds 4 MB";
        return;
    }
    DescDevice root;
    QString urlBase, error;
    if (!parseDescription(bytes, root, urlBase, error)) {
        qInfo().noquote() << "dlna: description" << where << "rejected:" << error;
        return;
    }
    const DescDevice* device = mediaServer(root, true);
    if (!device) device = mediaServer(root, false);
    if (!device) {
        qInfo().noquote() << "dlna: device at" << where << "has no ContentDirectory service";
        return;
    }
    // The final URL after any same-origin redirect is the description's real address.
    const QUrl described = reply->url().isValid() ? reply->url() : location;
    QUrl base(urlBase, QUrl::StrictMode);
    if (!httpUrl(base) || !sameHost(base, described)) base = described;
    const QUrl control = base.resolved(QUrl(contentDirectory(*device)->control));
    if (!httpUrl(control) || !sameHost(control, described)) {
        qInfo().noquote() << "dlna: device at" << where << "has an unusable controlURL";
        return;
    }
    DlnaServer server;
    server.udn = device->udn.isEmpty() ? QStringLiteral("location:") + described.toString() : device->udn;
    server.name = device->name.isEmpty() ? described.host() : device->name;
    server.controlUrl = control.toString(QUrl::FullyEncoded);
    qint64 bestArea = -1;
    for (const DescIcon& icon : device->icons) {
        if (icon.url.isEmpty()) continue;
        if (icon.mime != QLatin1String("image/png") && icon.mime != QLatin1String("image/jpeg") &&
            icon.mime != QLatin1String("image/jpg")) continue;
        const QUrl url = base.resolved(QUrl(icon.url));
        if (!httpUrl(url) || !sameHost(url, described) || icon.area <= bestArea) continue;
        bestArea = icon.area;
        server.iconUrl = url.toString(QUrl::FullyEncoded);
    }
    if (foundUdns_.contains(server.udn)) return;
    foundUdns_.insert(server.udn);
    found_.push_back(server);
    qInfo().noquote() << "dlna: server" << QStringLiteral("\"%1\"").arg(server.name) << "at" << where
                      << server.udn;
}

void DlnaClient::stopDiscovery() {
    if (discoveryTimer_) { discoveryTimer_->stop(); discoveryTimer_->deleteLater(); discoveryTimer_ = nullptr; }
    for (QTimer* timer : std::as_const(searchTimers_)) { timer->stop(); timer->deleteLater(); }
    searchTimers_.clear();
    const auto sockets = std::exchange(sockets_, {});
    for (QUdpSocket* socket : sockets) { socket->close(); socket->deleteLater(); }
    const auto replies = std::exchange(descriptionReplies_, {});
    for (QNetworkReply* reply : replies) { reply->abort(); }
}

void DlnaClient::finishDiscovery() {
    const QVector<DlnaServer> servers = found_;
    ++discoveryGeneration_;   // late description replies from this window are ignored
    stopDiscovery();
    qInfo().noquote() << "dlna: discovery finished," << servers.size() << "server(s)";
    emit serversReady(servers);
}

// ---- browse --------------------------------------------------------------------------------
void DlnaClient::cancelBrowses() {
    const auto replies = std::exchange(browseReplies_, {});
    for (QNetworkReply* reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

quint64 DlnaClient::browse(const DlnaServer& server, const QString& objectId, int start, int count) {
    const quint64 request = ++nextRequest_;
    const QUrl control(server.controlUrl, QUrl::StrictMode);
    auto failLater = [this, request](QString message) {
        QMetaObject::invokeMethod(this, [this, request, message] { emit failed(request, message); },
                                  Qt::QueuedConnection);
        return request;
    };
    if (!httpUrl(control)) return failLater(QStringLiteral("The server's control URL is not http(s)."));
    if (!validObjectId(objectId)) return failLater(QStringLiteral("Invalid object id."));
    start = std::max(0, start);
    count = std::clamp(count, 1, 1000);

    const QByteArray body =
        QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                       "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
                       "<ObjectID>%1</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
                       "<Filter>*</Filter><StartingIndex>%2</StartingIndex>"
                       "<RequestedCount>%3</RequestedCount><SortCriteria></SortCriteria>"
                       "</u:Browse></s:Body></s:Envelope>")
            .arg(objectId.toHtmlEscaped()).arg(start).arg(count).toUtf8();
    QNetworkRequest httpRequest = makeRequest(control);
    httpRequest.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/xml; charset=\"utf-8\""));
    httpRequest.setRawHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");
    QNetworkReply* reply = track(network_->post(httpRequest, body));
    browseReplies_.push_back(reply);
    const QString host = control.host();
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, objectId, host] {
        browseReplies_.removeAll(reply);
        reply->deleteLater();
        QByteArray bytes;
        const bool withinLimit = bodyWithinLimit(reply, bytes);
        const BrowseResponse response = withinLimit ? parseSoap(bytes) : BrowseResponse{};
        if (!response.faultCode.isEmpty() || (!response.faultText.isEmpty() && !response.sawResult)) {
            const QString message = QStringLiteral("UPnP error %1: %2").arg(response.faultCode, response.faultText);
            qInfo().noquote() << "dlna: browse" << objectId << "on" << host << "failed:" << message;
            emit failed(request, message);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || !withinLimit || !response.sawResult) {
            const QString message = !withinLimit ? QStringLiteral("Browse response exceeds 4 MB.")
                                    : reply->error() != QNetworkReply::NoError ? replyError(reply)
                                    : !withinLimit ? QStringLiteral("Browse response exceeds 4 MB.")
                                                   : QStringLiteral("Browse response has no Result.");
            qInfo().noquote() << "dlna: browse" << objectId << "on" << host << "failed:" << message;
            emit failed(request, message);
            return;
        }
        QString error;
        const QVector<DlnaItem> items = parseDidl(response.result, error);
        if (!error.isEmpty() && items.isEmpty()) {
            qInfo().noquote() << "dlna: browse" << objectId << "on" << host << "malformed DIDL:" << error;
            emit failed(request, QStringLiteral("Malformed DIDL-Lite: ") + error);
            return;
        }
        const int total = response.totalMatches >= 0 ? response.totalMatches : int(items.size());
        qInfo().noquote() << "dlna: browse" << objectId << "on" << host << "->" << items.size() << "of" << total
                          << (error.isEmpty() ? QString{} : QStringLiteral("(partial: ") + error + QLatin1Char(')'));
        emit browseReady(request, items, total);
    });
    return request;
}

} // namespace xmb
