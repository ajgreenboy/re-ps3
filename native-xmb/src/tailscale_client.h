#pragma once
// Tailscale status and control through the official CLI (tailscale.exe), run with QProcess and an
// argument array (never a shell). The CLI is Tailscale's supported, versioned interface; the
// Windows LocalAPI named pipe (\\.\pipe\ProtectedPrefix\Administrators\Tailscale\tailscaled) is an
// internal HTTP-over-pipe protocol with per-user access checks that changes between releases, so
// the CLI is used instead and does that handshake for us.
//
// refresh()      -> tailscale status --json          (8 s)  -> statusReady / failed
// setConnected() -> tailscale up | tailscale down     (30 s) -> commandDone("up"|"down", ...)
// setExitNode()  -> tailscale set --exit-node=<x>     (30 s) -> commandDone("exit-node", ...)
// ping()         -> tailscale ping -c 1 --timeout=5s <peer> (15 s) -> commandDone("ping", ...)
//
// Safety: stdout is capped at 4 MB and stderr at 64 KB (the process is killed past either); every
// command has a timeout; peer and exit-node arguments must be an IP literal or a hostname made of
// [A-Za-z0-9.-] that does not start with '-' (so it can never be read as a flag). Nothing is stored
// and the auth URL is never logged. The client never runs logout, login, switch or anything that
// changes the account; "tailscale up" is run without flags, which only sets WantRunning on a
// logged-in node (it does not reset prefs). If "up" prints a login URL the process is stopped and
// the URL is returned instead of waiting for an interactive login. up/down/exit-node only run when
// their method is called, and one of them at a time.
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <functional>

class QProcess;
class QTimer;

namespace xmb {

struct TailscalePeer {
    QString id, hostName, dnsName, os;   // dnsName without the trailing dot
    QStringList ips;                     // tailnet IPs (IPv4 first as the CLI lists them)
    bool online = false, exitNode = false, exitNodeOption = false;
    QDateTime lastSeen;                  // invalid when the CLI reports the zero time (e.g. online now)
    qint64 rxBytes = 0, txBytes = 0;
};

struct TailscaleStatus {
    QString backendState;                // Running, Stopped, NeedsLogin, NeedsMachineAuth, Starting, NoState
    QString version;
    TailscalePeer self;
    QString tailnetName, magicDnsSuffix;
    QString exitNodeId, exitNodeName, exitNodeIp;   // empty when no exit node is in use
    bool exitNodeOnline = false;
    QVector<TailscalePeer> peers;        // online first, then by host name
    QStringList health;                  // health warnings, if any
    QString authUrl;                     // only when NeedsLogin and a login was started

    bool running() const { return backendState == QLatin1String("Running"); }
    bool needsLogin() const { return backendState == QLatin1String("NeedsLogin"); }
};

class TailscaleClient final : public QObject {
    Q_OBJECT
public:
    explicit TailscaleClient(QObject* parent = nullptr);
    ~TailscaleClient() override;

    // Path of tailscale.exe: an explicit setExecutable(), else PATH, else
    // %ProgramFiles%\Tailscale\tailscale.exe. Empty when Tailscale is not installed.
    QString executable() const;
    void setExecutable(const QString& path) { executable_ = path; }
    bool installed() const { return !executable().isEmpty(); }

    // A refresh already in flight is not duplicated (its result answers both calls).
    void refresh();
    void setConnected(bool up);
    void setExitNode(const QString& peerIpOrEmpty);   // empty clears the exit node
    void ping(const QString& peer);
    bool busy() const { return commandRunning_; }

    // Exposed for tests and for callers that already hold the JSON.
    static bool parseStatus(const QByteArray& json, TailscaleStatus& out, QString* error = nullptr);
    static bool validPeerArgument(const QString& value);
    // Maps CLI stderr / exit status to a user-facing reason.
    static QString describeCliError(const QString& stderrText, int exitCode);

signals:
    void statusReady(xmb::TailscaleStatus status);
    void commandDone(QString what, bool ok, QString message);
    void failed(QString message);

private:
    struct Result {
        bool started = false, timedOut = false, overflow = false, crashed = false, stoppedEarly = false;
        int exitCode = -1;
        QByteArray out, err;
    };
    using Handler = std::function<void(const Result&)>;
    void run(const QStringList& args, int timeoutMs, const Handler& done,
             const std::function<bool(const QByteArray&)>& earlyStop = {});
    void runCommand(const QString& what, const QStringList& args, int timeoutMs);

    QString executable_;
    bool statusRunning_ = false, commandRunning_ = false, pingRunning_ = false;
};

} // namespace xmb
Q_DECLARE_METATYPE(xmb::TailscalePeer)
Q_DECLARE_METATYPE(xmb::TailscaleStatus)
