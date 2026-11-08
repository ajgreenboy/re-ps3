#include "wol_client.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkInterface>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace xmb {
namespace {
constexpr qint64 MaxFileBytes = 64 * 1024;
constexpr int MaxDevices = 64;
constexpr int MaxNameChars = 64;
constexpr int MaxIdChars = 64;
constexpr int MaxCandidates = 256;
constexpr int MaxWakes = 8;
constexpr int MaxChecks = 32;
constexpr int PacketSends = 3;
constexpr int MagicPacketBytes = 102;
constexpr quint16 EchoPort = 7;
const QVector<quint16> DefaultProbePorts{3389, 22, 445, 80};
quint16 gLoopbackRedirect = 0;   // wol_testing only

bool hexDigits(const QString& text) {
    for (const QChar ch : text)
        if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f') || (ch >= u'A' && ch <= u'F'))) return false;
    return true;
}

// Dotted-quad only: QHostAddress also takes "10.1", "0x0a.1.1.1" and decimal forms, which a
// config file has no business using.
bool dottedQuad(const QString& text, QHostAddress* address) {
    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() != 4) return false;
    for (const QString& part : parts) {
        if (part.isEmpty() || part.size() > 3) return false;
        for (const QChar ch : part) if (ch < u'0' || ch > u'9') return false;
        if (part.size() > 1 && part[0] == u'0') return false;
        if (part.toInt() > 255) return false;
    }
    const QHostAddress parsed(text);
    if (parsed.protocol() != QAbstractSocket::IPv4Protocol) return false;
    *address = parsed;
    return true;
}

// Private (RFC 1918) or link-local IPv4: the networks a wake packet may reach.
bool lanIPv4(quint32 ip) {
    return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u) ||
           (ip >> 16) == ((169u << 8) | 254u);
}

bool ipv4(const QHostAddress& address, quint32* ip) {
    if (address.protocol() != QAbstractSocket::IPv4Protocol) return false;
    bool ok = false;
    *ip = address.toIPv4Address(&ok);
    return ok;
}

bool localInterfaceBroadcast(const QHostAddress& address) {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces)
        for (const QNetworkAddressEntry& entry : iface.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull() &&
                entry.broadcast().isEqual(address)) return true;
    return false;
}

bool cleanText(const QString& text) {
    for (const QChar ch : text)
        if (ch.isNull() || ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format) return false;
    return true;
}

bool validId(const QString& id) {
    if (id.isEmpty() || id.size() > MaxIdChars) return false;
    for (const QChar ch : id)
        if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') ||
              ch == u'-' || ch == u'_' || ch == u'.')) return false;
    return true;
}

bool readPort(const QJsonObject& object, const QString& key, quint16* port, QString* why) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || value.isNull()) return true;
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number != std::floor(number) || number < 1 || number > 65535) {
        *why = QStringLiteral("\"%1\" must be a whole number 1..65535").arg(key);
        return false;
    }
    *port = static_cast<quint16>(number);
    return true;
}

bool optionalString(const QJsonObject& object, const QString& key, QString* text, QString* why) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || value.isNull()) return true;
    if (!value.isString()) { *why = QStringLiteral("\"%1\" must be a string").arg(key); return false; }
    *text = value.toString().trimmed();
    return true;
}

bool readDevice(const QJsonObject& object, WolDevice* device, QString* why) {
    const QJsonValue macValue = object.value(QStringLiteral("mac"));
    if (!macValue.isString()) { *why = QStringLiteral("\"mac\" is missing or not a string"); return false; }
    QByteArray mac;
    if (!parseMacAddress(macValue.toString(), &mac)) {
        *why = QStringLiteral("\"mac\" is not a unicast MAC address (AA:BB:CC:DD:EE:FF, AA-BB-... or AABBCCDDEEFF)");
        return false;
    }
    device->mac = formatMacAddress(mac);

    QString host, broadcast, name, id;
    if (!optionalString(object, QStringLiteral("host"), &host, why) ||
        !optionalString(object, QStringLiteral("broadcast"), &broadcast, why) ||
        !optionalString(object, QStringLiteral("name"), &name, why) ||
        !optionalString(object, QStringLiteral("id"), &id, why)) return false;

    if (!host.isEmpty()) {
        QHostAddress address;
        if (!dottedQuad(host, &address) || !wolHostAllowed(address)) {
            *why = QStringLiteral("\"host\" must be a private, link-local or loopback IPv4 address");
            return false;
        }
        device->host = address.toString();
    }
    if (broadcast.isEmpty()) {
        device->broadcast = QStringLiteral("255.255.255.255");
    } else {
        QHostAddress address;
        if (!dottedQuad(broadcast, &address) || !wolBroadcastAllowed(address)) {
            *why = QStringLiteral("\"broadcast\" must be 255.255.255.255 or a private network's IPv4 broadcast");
            return false;
        }
        device->broadcast = address.toString();
    }
    if (name.size() > MaxNameChars || !cleanText(name)) {
        *why = QStringLiteral("\"name\" must be at most %1 printable characters").arg(MaxNameChars);
        return false;
    }
    device->name = name.isEmpty() ? (device->host.isEmpty() ? device->mac : device->host) : name;
    if (!id.isEmpty() && !validId(id)) {
        *why = QStringLiteral("\"id\" must be 1..%1 of A-Z a-z 0-9 - _ .").arg(MaxIdChars);
        return false;
    }
    device->id = id.isEmpty() ? QString::fromLatin1(mac.toHex()) : id;
    return readPort(object, QStringLiteral("port"), &device->port, why) &&
           readPort(object, QStringLiteral("probePort"), &device->probePort, why);
}

void addError(QStringList* errors, const QString& message) {
    if (errors && errors->size() < MaxDevices + 4) errors->push_back(message);
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

// Validates a device for sending and builds its packet and destination.
bool prepareSend(const WolDevice& device, QByteArray* packet, QHostAddress* target, QString* error) {
    QByteArray mac;
    if (!parseMacAddress(device.mac, &mac)) { setError(error, QStringLiteral("invalid MAC address")); return false; }
    *packet = buildMagicPacket(mac);
    const QString broadcast = device.broadcast.isEmpty() ? QStringLiteral("255.255.255.255") : device.broadcast;
    if (!dottedQuad(broadcast, target) || !wolBroadcastAllowed(*target)) {
        setError(error, QStringLiteral("broadcast %1 is not a private network broadcast").arg(broadcast));
        return false;
    }
    if (device.port == 0) { setError(error, QStringLiteral("port 0")); return false; }
    return true;
}

QVector<quint16> sendPorts(quint16 port) {
    QVector<quint16> ports{port};
    if (port != EchoPort) ports.push_back(EchoPort);
    return ports;
}

// PacketSends rounds, each to every port, from one socket per source (a null source is unbound).
int sendRounds(const QVector<QHostAddress>& sources, const QByteArray& packet, const QHostAddress& target,
               const QVector<quint16>& ports, QString* lastError) {
    int sent = 0;
    QVector<QUdpSocket*> sockets;
    for (const QHostAddress& source : sources) {
        auto* socket = new QUdpSocket;
        if (!socket->bind(source.isNull() ? QHostAddress(QHostAddress::AnyIPv4) : source, 0)) {
            *lastError = QStringLiteral("bind %1: %2").arg(source.toString(), socket->errorString());
            delete socket;
            continue;
        }
        sockets.push_back(socket);
    }
    for (int round = 0; round < PacketSends; ++round)
        for (QUdpSocket* socket : std::as_const(sockets))
            for (quint16 port : ports) {
                if (socket->writeDatagram(packet, target, port) == packet.size()) ++sent;
                else *lastError = QStringLiteral("send from %1 to %2:%3: %4")
                                      .arg(socket->localAddress().toString(), target.toString()).arg(port)
                                      .arg(socket->errorString());
            }
    qDeleteAll(sockets);
    return sent;
}
} // namespace

// ---- addresses and packet ----------------------------------------------------------------
bool parseMacAddress(const QString& text, QByteArray* bytes) {
    const QString trimmed = text.trimmed();
    QString hex;
    if (trimmed.size() == 12) {
        hex = trimmed;
    } else if (trimmed.size() == 17) {
        const QChar sep = trimmed[2];
        if (sep != u':' && sep != u'-') return false;
        for (int i = 0; i < 17; ++i) {
            if (i % 3 == 2) { if (trimmed[i] != sep) return false; }
            else hex += trimmed[i];
        }
    } else {
        return false;
    }
    if (!hexDigits(hex)) return false;
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    if (raw.size() != 6) return false;
    if (raw == QByteArray(6, '\0') || raw == QByteArray(6, '\xff')) return false;
    if (static_cast<quint8>(raw[0]) & 0x01) return false;   // multicast/group bit
    if (bytes) *bytes = raw;
    return true;
}

QString formatMacAddress(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toHex(':').toUpper());
}

QByteArray buildMagicPacket(const QByteArray& macBytes) {
    if (macBytes.size() != 6) return {};
    QByteArray packet(6, '\xff');
    packet.reserve(MagicPacketBytes);
    for (int i = 0; i < 16; ++i) packet += macBytes;
    return packet;
}

bool wolHostAllowed(const QHostAddress& address) {
    quint32 ip = 0;
    if (!ipv4(address, &ip)) return false;
    return address.isLoopback() || lanIPv4(ip);
}

bool wolBroadcastAllowed(const QHostAddress& address) {
    quint32 ip = 0;
    if (!ipv4(address, &ip)) return false;
    if (ip == 0xffffffffu) return true;
    if (!lanIPv4(ip)) return false;
    return (ip & 0xffu) == 0xffu || localInterfaceBroadcast(address);
}

// ---- config --------------------------------------------------------------------------------
QVector<WolDevice> parseWolDevices(const QByteArray& json, QStringList* errors) {
    QVector<WolDevice> devices;
    if (json.size() > MaxFileBytes) {
        addError(errors, QStringLiteral("wol.json is larger than 64 KB"));
        return devices;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addError(errors, QStringLiteral("wol.json is not valid JSON: %1 at offset %2")
                             .arg(parseError.errorString()).arg(parseError.offset));
        return devices;
    }
    if (!document.isObject()) {
        addError(errors, QStringLiteral("wol.json must be an object"));
        return devices;
    }
    const QJsonValue list = document.object().value(QStringLiteral("devices"));
    if (!list.isArray()) {
        addError(errors, QStringLiteral("wol.json has no \"devices\" array"));
        return devices;
    }
    const QJsonArray array = list.toArray();
    QSet<QString> ids;
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (devices.size() >= MaxDevices) {
            addError(errors, QStringLiteral("only the first %1 devices are used").arg(MaxDevices));
            break;
        }
        if (!array.at(i).isObject()) {
            addError(errors, QStringLiteral("device %1: not an object").arg(i));
            continue;
        }
        WolDevice device;
        QString why;
        if (!readDevice(array.at(i).toObject(), &device, &why)) {
            addError(errors, QStringLiteral("device %1: %2").arg(i).arg(why));
            continue;
        }
        if (ids.contains(device.id)) {
            addError(errors, QStringLiteral("device %1: duplicate id %2").arg(i).arg(device.id));
            continue;
        }
        ids.insert(device.id);
        devices.push_back(std::move(device));
    }
    return devices;
}

QVector<WolDevice> loadWolDevices(const QString& dataDirectory, QStringList* errors) {
    QFile file(QDir(dataDirectory).filePath(QStringLiteral("wol.json")));
    if (!file.exists()) return {};
    if (file.size() > MaxFileBytes) {
        addError(errors, QStringLiteral("wol.json is larger than 64 KB"));
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        addError(errors, QStringLiteral("wol.json: %1").arg(file.errorString()));
        return {};
    }
    return parseWolDevices(file.read(MaxFileBytes + 1), errors);
}

// ---- sending -------------------------------------------------------------------------------
bool sendMagicPacket(const WolDevice& device, QString* error) {
    QByteArray packet;
    QHostAddress target;
    if (!prepareSend(device, &packet, &target, error)) return false;
    if (gLoopbackRedirect) {
        QString lastError;
        const QHostAddress loopback(QHostAddress::LocalHost);
        const int sent = sendRounds({loopback}, packet, loopback, {gLoopbackRedirect}, &lastError);
        if (sent == 0) setError(error, lastError);
        return sent > 0;
    }
    const bool limited = target == QHostAddress(QHostAddress::Broadcast);

    // One source per LAN IPv4 address on an up interface whose subnet holds the broadcast: Windows
    // sends a broadcast out of one adapter only, chosen by the socket's local address.
    QVector<QHostAddress> sources;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack) || !(flags & QNetworkInterface::CanBroadcast)) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (!wolHostAllowed(entry.ip()) || entry.ip().isLoopback()) continue;
            const int prefix = entry.prefixLength();
            if (prefix < 1 || prefix > 30) continue;
            if (limited || target.isInSubnet(entry.ip(), prefix)) {
                if (!sources.contains(entry.ip()) && sources.size() < 16) sources.push_back(entry.ip());
            }
        }
    }
    if (sources.isEmpty()) {
        if (!limited) {
            setError(error, QStringLiteral("no local network interface is on the %1 network").arg(target.toString()));
            return false;
        }
        sources.push_back(QHostAddress());   // let Windows pick the adapter
    }
    QString lastError;
    const int sent = sendRounds(sources, packet, target, sendPorts(device.port), &lastError);
    qInfo().noquote() << "wol: magic packet for" << device.mac << "to" << target.toString() << "ports"
                      << device.port << "and 7 from" << sources.size() << "interface address(es):" << sent << "sent";
    if (sent == 0) {
        setError(error, lastError.isEmpty() ? QStringLiteral("no packet could be sent") : lastError);
        return false;
    }
    return true;
}

namespace wol_testing {
bool sendMagicPacketToLoopback(const WolDevice& device, const QHostAddress& loopback, quint16 port, QString* error) {
    QByteArray mac;
    if (!parseMacAddress(device.mac, &mac)) { setError(error, QStringLiteral("invalid MAC address")); return false; }
    if (!loopback.isLoopback() || loopback.protocol() != QAbstractSocket::IPv4Protocol || port == 0) {
        setError(error, QStringLiteral("test sends go to an IPv4 loopback address only"));
        return false;
    }
    QString lastError;
    const int sent = sendRounds({QHostAddress(QHostAddress::LocalHost)}, buildMagicPacket(mac), loopback, {port}, &lastError);
    if (sent == 0) setError(error, lastError);
    return sent > 0;
}

void setLoopbackRedirect(quint16 port) {
    gLoopbackRedirect = port;
}
} // namespace wol_testing

// ---- neighbour table -----------------------------------------------------------------------
QVector<WolDevice> discoverCandidates(QString* error) {
    QVector<WolDevice> found;
#ifdef _WIN32
    PMIB_IPNET_TABLE2 table = nullptr;
    const DWORD status = GetIpNetTable2(AF_INET, &table);
    if (status != NO_ERROR || !table) {
        setError(error, QStringLiteral("GetIpNetTable2 failed (%1)").arg(status));
        return found;
    }
    QSet<quint32> seen;
    for (ULONG i = 0; i < table->NumEntries && found.size() < MaxCandidates; ++i) {
        const MIB_IPNET_ROW2& row = table->Table[i];
        if (row.Address.si_family != AF_INET || row.PhysicalAddressLength != 6) continue;
        if (row.State == NlnsUnreachable || row.State == NlnsIncomplete) continue;
        const quint32 ip = ntohl(row.Address.Ipv4.sin_addr.S_un.S_addr);
        const QHostAddress address(ip);
        if (!lanIPv4(ip) || seen.contains(ip)) continue;
        const QByteArray mac(reinterpret_cast<const char*>(row.PhysicalAddress), 6);
        if (!parseMacAddress(formatMacAddress(mac), nullptr)) continue;   // zero, broadcast, multicast
        WolDevice device;
        device.mac = formatMacAddress(mac);
        device.id = QString::fromLatin1(mac.toHex());
        device.host = address.toString();
        device.name = device.host;
        device.broadcast = QStringLiteral("255.255.255.255");
        const QNetworkInterface iface = QNetworkInterface::interfaceFromIndex(static_cast<int>(row.InterfaceIndex));
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol || entry.broadcast().isNull()) continue;
            if (address.isInSubnet(entry.ip(), entry.prefixLength()) && wolBroadcastAllowed(entry.broadcast())) {
                device.broadcast = entry.broadcast().toString();
                break;
            }
        }
        seen.insert(ip);
        found.push_back(std::move(device));
    }
    FreeMibTable(table);
    std::sort(found.begin(), found.end(), [](const WolDevice& a, const WolDevice& b) {
        return QHostAddress(a.host).toIPv4Address() < QHostAddress(b.host).toIPv4Address();
    });
#else
    setError(error, QStringLiteral("neighbour table is only read on Windows"));
#endif
    return found;
}

// ---- WolClient -----------------------------------------------------------------------------
WolClient::WolClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<xmb::WolDevice>();
}

WolClient::~WolClient() {
    cancelAll();
    for (const quint64 serial : probes_) abortCheck(serial);
    probes_.clear();
    const auto serials = checks_.keys();
    for (const quint64 serial : serials) abortCheck(serial);
}

void WolClient::setTiming(int pollIntervalMs, int timeoutMs, int connectTimeoutMs) {
    pollIntervalMs_ = std::clamp(pollIntervalMs, 1000, 30000);
    timeoutMs_ = std::clamp(timeoutMs, 2000, 600000);
    connectTimeoutMs_ = std::clamp(connectTimeoutMs, 200, 10000);
}

QVector<quint16> WolClient::probePorts(const WolDevice& device) {
    return device.probePort ? QVector<quint16>{device.probePort} : DefaultProbePorts;
}

quint64 WolClient::startCheck(const QHostAddress& host, const QVector<quint16>& ports, std::function<void(bool)> done) {
    const quint64 serial = ++nextCheck_;
    auto* check = new Check;
    check->done = std::move(done);
    check->timer = new QTimer(this);
    check->timer->setSingleShot(true);
    connect(check->timer, &QTimer::timeout, this, [this, serial] { finishCheck(serial, false); });
    checks_.insert(serial, check);
    for (qsizetype i = 0; i < ports.size(); ++i) {
        auto* socket = new QTcpSocket(this);
        // Any answer from the host itself, an accept or a reset, means it is running.
        connect(socket, &QTcpSocket::connected, this, [this, serial] { finishCheck(serial, true); });
        connect(socket, &QAbstractSocket::errorOccurred, this, [this, serial](QAbstractSocket::SocketError code) {
            if (code == QAbstractSocket::ConnectionRefusedError) { finishCheck(serial, true); return; }
            Check* c = checks_.value(serial);
            if (c && --c->pending <= 0) finishCheck(serial, false);
        });
        check->sockets.push_back(socket);
        ++check->pending;
    }
    check->timer->start(connectTimeoutMs_);
    // Connect after the bookkeeping: a synchronous failure must find the check.
    const QVector<QTcpSocket*> sockets = check->sockets;
    for (int i = 0; i < sockets.size(); ++i) {
        if (!checks_.contains(serial)) break;
        sockets[i]->connectToHost(host, ports[i]);
    }
    return serial;
}

void WolClient::abortCheck(quint64 serial) {
    Check* check = checks_.take(serial);
    if (!check) return;
    for (QTcpSocket* socket : std::as_const(check->sockets)) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
    check->timer->stop();
    check->timer->deleteLater();
    delete check;
}

void WolClient::finishCheck(quint64 serial, bool online) {
    Check* check = checks_.value(serial);
    if (!check) return;
    std::function<void(bool)> done = std::move(check->done);
    abortCheck(serial);
    if (done) done(online);
}

void WolClient::probe(const WolDevice& device) {
    const QString id = device.id;
    if (probes_.contains(id)) abortCheck(probes_.take(id));
    QHostAddress host;
    if (device.host.isEmpty() || !dottedQuad(device.host, &host) || !wolHostAllowed(host) || checks_.size() >= MaxChecks) {
        QMetaObject::invokeMethod(this, [this, id] { emit reachable(id, false); }, Qt::QueuedConnection);
        return;
    }
    probes_.insert(id, startCheck(host, probePorts(device), [this, id](bool online) {
        probes_.remove(id);
        emit reachable(id, online);
    }));
}

void WolClient::wake(const WolDevice& device) {
    const QString id = device.id;
    stopWake(id);
    QString error;
    if (!sendMagicPacket(device, &error)) {
        emit failed(id, error);
        return;
    }
    emit sent(id);
    QHostAddress host;
    if (device.host.isEmpty() || !dottedQuad(device.host, &host) || !wolHostAllowed(host)) {
        emit progress(id, QStringLiteral("Wake packet sent."));
        return;
    }
    while (wakes_.size() >= MaxWakes && !wakeOrder_.isEmpty()) stopWake(wakeOrder_.first());
    auto* wake = new Wake;
    wake->device = device;
    wake->clock.start();
    wake->poll = new QTimer(this);
    wake->poll->setInterval(pollIntervalMs_);
    connect(wake->poll, &QTimer::timeout, this, [this, id] { pollWake(id); });
    wakes_.insert(id, wake);
    wakeOrder_.push_back(id);
    emit progress(id, QStringLiteral("Wake packet sent. Waiting for %1...").arg(device.host));
    wake->poll->start();
    pollWake(id);
}

void WolClient::pollWake(const QString& id) {
    Wake* wake = wakes_.value(id);
    if (!wake || wake->check) return;   // the previous check is still running
    if (wake->clock.elapsed() >= timeoutMs_) {
        stopWake(id);
        emit timedOut(id);
        return;
    }
    ++wake->attempts;
    wake->check = startCheck(QHostAddress(wake->device.host), probePorts(wake->device), [this, id](bool online) {
        Wake* w = wakes_.value(id);
        if (!w) return;
        w->check = 0;
        const qint64 elapsed = w->clock.elapsed();
        if (online) {
            stopWake(id);
            emit woke(id, static_cast<int>(elapsed / 1000));
            return;
        }
        if (elapsed >= timeoutMs_) {
            stopWake(id);
            emit timedOut(id);
            return;
        }
        emit progress(id, QStringLiteral("Waiting for %1 (%2 s)...").arg(w->device.host).arg(elapsed / 1000));
    });
}

void WolClient::stopWake(const QString& id) {
    Wake* wake = wakes_.take(id);
    wakeOrder_.removeAll(id);
    if (!wake) return;
    if (wake->check) abortCheck(wake->check);
    wake->poll->stop();
    wake->poll->deleteLater();
    delete wake;
}

void WolClient::cancel(const QString& id) {
    stopWake(id);
}

void WolClient::cancelAll() {
    const auto ids = wakes_.keys();
    for (const QString& id : ids) stopWake(id);
}

} // namespace xmb
