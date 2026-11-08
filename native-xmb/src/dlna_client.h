#pragma once
// DLNA / UPnP AV media servers on the local network (Plex, Jellyfin, Serviio, minidlna, a NAS...).
//
// Discovery is SSDP: an M-SEARCH for urn:schemas-upnp-org:device:MediaServer:1 multicast to
// 239.255.255.250:1900 from every IPv4 interface that can multicast, sent three times across the
// window. Each answer's LOCATION is a device description; the client fetches it, takes the media
// server device (the root or an embedded one), its friendlyName, UDN, the ContentDirectory
// controlURL and the largest PNG/JPEG icon. serversReady fires once, at the end of the window,
// deduplicated by UDN.
//
// Browsing is the ContentDirectory Browse action (BrowseDirectChildren, Filter "*") over SOAP; the
// DIDL-Lite Result is parsed into DlnaItem rows.
//
// Safety: description locations must be private, link-local or loopback IP literals; the control
// URL and icon must live on the same host as the description; only http(s) URLs are accepted
// anywhere (media and album art included); redirects are followed only within the same origin;
// every response is capped at 4 MB and every transfer times out after 10 s. Malformed XML yields
// an error or a partial result, never a crash. No credentials are sent.
#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QtGlobal>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QUdpSocket;

namespace xmb {

struct DlnaServer {
    QString udn, name, controlUrl, iconUrl;   // iconUrl is empty when the server lists no PNG/JPEG icon
};

struct DlnaItem {
    QString id, parentId, title, upnpClass, url, mimeType, albumArtUrl, artist, album;
    qint64 durationMs = 0, size = 0;
    bool container = false;
    int childCount = -1;   // -1 when the server does not say
};

class DlnaClient final : public QObject {
    Q_OBJECT
public:
    explicit DlnaClient(QObject* parent = nullptr);
    ~DlnaClient() override;

    // Starts a discovery window; a discovery already running is cancelled (its result is never
    // emitted). serversReady fires exactly once, timeoutMs (clamped to 1..30 s) from now.
    void discover(int timeoutMs = 3000);
    bool discovering() const { return discoveryTimer_ != nullptr; }
    // Extra unicast M-SEARCH targets for the next discover() (for example a server whose SSDP
    // multicast never leaves a Docker bridge but whose UDP 1900 is published). Each must be a
    // private, link-local or loopback address.
    void setUnicastTargets(QVector<QHostAddress> hosts);
    // Device description URLs to fetch in the next discover() without waiting for an SSDP answer
    // (a server whose description URL is known). Same rules as an SSDP LOCATION.
    void setKnownLocations(QVector<QUrl> locations);

    // ContentDirectory Browse of objectId's direct children ("0" is the root). count is clamped
    // to 1..1000. Returns the request id carried by browseReady or failed.
    quint64 browse(const DlnaServer& server, const QString& objectId, int start = 0, int count = 200);
    void cancelBrowses();

signals:
    void serversReady(QVector<xmb::DlnaServer> servers);
    void browseReady(quint64 request, QVector<xmb::DlnaItem> items, int totalMatches);
    void failed(quint64 request, QString message);

private:
    void sendSearch();
    void readDatagrams(QUdpSocket* socket);
    void fetchDescription(const QUrl& location);
    void descriptionFinished(QNetworkReply* reply, const QUrl& location, quint64 generation);
    void finishDiscovery();
    void stopDiscovery();
    QNetworkReply* track(QNetworkReply* reply);
    static QString replyError(QNetworkReply* reply);

    QNetworkAccessManager* network_ = nullptr;
    QVector<QUdpSocket*> sockets_;
    QTimer* discoveryTimer_ = nullptr;
    QVector<QTimer*> searchTimers_;
    QVector<QNetworkReply*> descriptionReplies_;
    QVector<QNetworkReply*> browseReplies_;
    QVector<QHostAddress> unicastTargets_;
    QVector<QUrl> knownLocations_;
    QSet<QString> seenLocations_;
    QVector<DlnaServer> found_;
    QSet<QString> foundUdns_;
    quint64 discoveryGeneration_ = 0;
    quint64 nextRequest_ = 0;
};

} // namespace xmb
Q_DECLARE_METATYPE(xmb::DlnaServer)
Q_DECLARE_METATYPE(xmb::DlnaItem)
