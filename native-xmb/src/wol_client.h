#pragma once
// Wake-on-LAN for machines on the local network (the server, a desktop, a NAS...).
//
// Devices come from <data>/wol.json:
//   {"devices":[{"name":"Gaming PC","mac":"AA:BB:CC:DD:EE:FF","host":"192.168.1.20",
//                "broadcast":"192.168.1.255","port":9,"probePort":3389,"id":"gaming-pc"}]}
// Only "mac" is required. The MAC may be written AA:BB:CC:DD:EE:FF, AA-BB-CC-DD-EE-FF or AABBCCDDEEFF
// (one separator style per address); it is stored as AA:BB:CC:DD:EE:FF and must be a unicast,
// non-zero address. "host" (used only to check whether the machine came up) is an IPv4 literal on a
// private, link-local or loopback network; no names are resolved. "broadcast" is 255.255.255.255
// (the default) or a private IPv4 directed broadcast: one whose last octet is 255 or that equals
// the broadcast of a local interface. "port" (default 9) is where the packet goes, plus port 7.
// "probePort" replaces the default reachability ports. "id" defaults to the MAC's 12 hex digits.
// Invalid entries are skipped with a message; the file is capped at 64 KB and 64 devices.
//
// The magic packet is 102 bytes: 6 x 0xFF then the MAC 16 times. It is sent three times to the
// broadcast address on the configured port and on port 7, from every up IPv4 interface whose subnet
// contains the broadcast (every up interface for 255.255.255.255). No packet ever goes to a
// non-private address. Nothing here starts a process or a shell.
//
// WolClient::wake sends the packet and then, when the device has a host, checks every 3 s (for at
// most 120 s) whether the host accepts or refuses a TCP connection on 3389, 22, 445 or 80 (or the
// probePort), each attempt bounded to 2 s. A refused connection counts as reachable: only a running
// host answers with a reset. probe() runs one such check for an Online/Offline status.
//
// discoverCandidates() reads the Windows IPv4 neighbour (ARP) table, read-only, and lists private
// hosts with a unicast MAC so a user can pick a machine instead of typing its MAC.
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <functional>

class QTcpSocket;
class QTimer;

namespace xmb {

struct WolDevice {
    QString id, name, mac, host;   // mac is AA:BB:CC:DD:EE:FF; host may be empty (no reachability check)
    quint16 port = 9;
    QString broadcast;             // empty means 255.255.255.255
    quint16 probePort = 0;         // 0: probe 3389, 22, 445 and 80
};

// MAC text to its 6 bytes; false (and *bytes untouched) for anything but one of the three formats,
// or a zero, broadcast or multicast address.
bool parseMacAddress(const QString& text, QByteArray* bytes);
QString formatMacAddress(const QByteArray& bytes);          // AA:BB:CC:DD:EE:FF
QByteArray buildMagicPacket(const QByteArray& macBytes);    // 102 bytes, empty if macBytes is not 6 bytes

// A host this client may connect to or that may be a wake target: an IPv4 private, link-local or
// loopback literal.
bool wolHostAllowed(const QHostAddress& address);
// A broadcast this client may send to (see the header comment).
bool wolBroadcastAllowed(const QHostAddress& address);

// Parses wol.json bytes. Each skipped entry (or a file-level problem) adds a line to *errors.
QVector<WolDevice> parseWolDevices(const QByteArray& json, QStringList* errors = nullptr);
// Reads <dataDirectory>/wol.json; a missing file is an empty list without an error.
QVector<WolDevice> loadWolDevices(const QString& dataDirectory, QStringList* errors = nullptr);

// Sends the magic packet (three times, port `port` and 7). True when at least one datagram left.
bool sendMagicPacket(const WolDevice& device, QString* error = nullptr);

// Reads the IPv4 neighbour table (GetIpNetTable2). Each candidate has id, mac, host and broadcast
// (the interface broadcast when known) filled; name is the host. Sorted by address, at most 256.
QVector<WolDevice> discoverCandidates(QString* error = nullptr);

namespace wol_testing {
// Test-only: the same packet and three sends, but to a loopback address and port of the caller's
// choosing, so a local listener can check the bytes. Refuses any non-loopback target.
bool sendMagicPacketToLoopback(const WolDevice& device, const QHostAddress& loopback, quint16 port,
                               QString* error = nullptr);
// Test-only: while non-zero, sendMagicPacket (and so WolClient::wake) validates the device as usual
// but sends to 127.0.0.1:port instead of the broadcast. 0 (the default) restores real sending.
void setLoopbackRedirect(quint16 port);
} // namespace wol_testing

class WolClient final : public QObject {
    Q_OBJECT
public:
    explicit WolClient(QObject* parent = nullptr);
    ~WolClient() override;

    // Sends the packet and watches the host. A wake already running for the same id is replaced.
    // At most 8 wakes run at once (the oldest is dropped, without a signal).
    void wake(const WolDevice& device);
    void cancel(const QString& id);
    void cancelAll();
    bool waking(const QString& id) const { return wakes_.contains(id); }

    // One reachability check; reachable(id, ...) follows within about 2 s. A probe already running
    // for the same id is restarted. Devices without a valid host report false at once (queued).
    void probe(const WolDevice& device);

    // Bounds, adjustable for tests (clamped: interval 1..30 s, timeout 2..600 s, connect 0.2..10 s).
    void setTiming(int pollIntervalMs, int timeoutMs, int connectTimeoutMs);

signals:
    void sent(QString id);
    void progress(QString id, QString message);
    void woke(QString id, int seconds);
    void timedOut(QString id);
    void failed(QString id, QString message);
    void reachable(QString id, bool online);

private:
    struct Check {
        QVector<QTcpSocket*> sockets;
        QTimer* timer = nullptr;
        int pending = 0;
        std::function<void(bool)> done;
    };
    struct Wake {
        WolDevice device;
        QElapsedTimer clock;
        QTimer* poll = nullptr;
        quint64 check = 0;
        int attempts = 0;
    };

    quint64 startCheck(const QHostAddress& host, const QVector<quint16>& ports, std::function<void(bool)> done);
    void finishCheck(quint64 serial, bool online);
    void abortCheck(quint64 serial);
    void pollWake(const QString& id);
    void stopWake(const QString& id);
    static QVector<quint16> probePorts(const WolDevice& device);

    QHash<quint64, Check*> checks_;
    QHash<QString, Wake*> wakes_;
    QVector<QString> wakeOrder_;
    QHash<QString, quint64> probes_;
    quint64 nextCheck_ = 0;
    int pollIntervalMs_ = 3000;
    int timeoutMs_ = 120000;
    int connectTimeoutMs_ = 2000;
};

} // namespace xmb
Q_DECLARE_METATYPE(xmb::WolDevice)
