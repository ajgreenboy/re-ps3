#include "tailscale_client.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <memory>

namespace xmb {

namespace {

constexpr qint64 kMaxStdout = 4 * 1024 * 1024;
constexpr qint64 kMaxStderr = 64 * 1024;
constexpr int kStatusTimeoutMs = 8000;
constexpr int kUpDownTimeoutMs = 30000;
constexpr int kPingTimeoutMs = 15000;

QString notInstalledMessage() {
    return QStringLiteral("Tailscale is not installed (tailscale.exe not found on PATH or in Program Files)");
}

QStringList stringList(const QJsonValue& v) {
    QStringList list;
    const QJsonArray a = v.toArray();
    for (const QJsonValue& e : a)
        if (e.isString() && !e.toString().isEmpty()) list << e.toString();
    return list;
}

QString stripDot(QString s) {
    while (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

QString stripPrefixLen(QString ip) {   // "100.64.0.1/32" -> "100.64.0.1"
    const int slash = ip.indexOf(QLatin1Char('/'));
    if (slash > 0) ip.truncate(slash);
    return ip;
}

// Go's time.Time JSON: RFC 3339 with up to nanoseconds. The zero time means "unknown".
QDateTime parseGoTime(const QString& s) {
    static const QRegularExpression re(QStringLiteral(
        R"(^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)(?:\.(\d+))?(Z|[+-]\d\d:\d\d)$)"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch() || m.captured(1).toInt() <= 1) return {};
    QDateTime dt = QDateTime::fromString(
        QStringLiteral("%1-%2-%3T%4:%5:%6%7").arg(m.captured(1), m.captured(2), m.captured(3),
            m.captured(4), m.captured(5), m.captured(6), m.captured(8)), Qt::ISODate);
    if (!dt.isValid()) return {};
    const QString frac = m.captured(7);
    if (!frac.isEmpty()) dt = dt.addMSecs(frac.left(3).leftJustified(3, QLatin1Char('0')).toInt());
    return dt.toUTC();
}

qint64 toInt64(const QJsonValue& v) {
    return v.isDouble() ? v.toInteger() : v.toString().toLongLong();
}

TailscalePeer parsePeer(const QJsonObject& o) {
    TailscalePeer p;
    const QJsonValue id = o.value(QLatin1String("ID"));
    p.id = id.isString() ? id.toString() : (id.isDouble() ? QString::number(id.toInteger()) : QString());
    p.hostName = o.value(QLatin1String("HostName")).toString();
    p.dnsName = stripDot(o.value(QLatin1String("DNSName")).toString());
    p.os = o.value(QLatin1String("OS")).toString();
    p.ips = stringList(o.value(QLatin1String("TailscaleIPs")));
    p.online = o.value(QLatin1String("Online")).toBool();
    p.exitNode = o.value(QLatin1String("ExitNode")).toBool();
    p.exitNodeOption = o.value(QLatin1String("ExitNodeOption")).toBool();
    p.lastSeen = parseGoTime(o.value(QLatin1String("LastSeen")).toString());
    p.rxBytes = toInt64(o.value(QLatin1String("RxBytes")));
    p.txBytes = toInt64(o.value(QLatin1String("TxBytes")));
    if (p.hostName.isEmpty()) p.hostName = p.dnsName.section(QLatin1Char('.'), 0, 0);
    return p;
}

// First non-empty line(s) of CLI text, trimmed and capped for display.
QString firstLines(const QString& text, int maxLines = 2) {
    QStringList kept;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        if (t.isEmpty()) continue;
        kept << t;
        if (kept.size() >= maxLines) break;
    }
    QString s = kept.join(QLatin1Char(' '));
    if (s.size() > 300) s = s.left(297) + QStringLiteral("...");
    return s;
}

QString lastLine(const QString& text) {
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it)
        if (!it->trimmed().isEmpty()) return it->trimmed().left(300);
    return {};
}

QString loginUrlIn(const QByteArray& text) {
    static const QRegularExpression re(QStringLiteral(R"(https://[A-Za-z0-9.-]+/a/[A-Za-z0-9_-]+)"));
    return re.match(QString::fromUtf8(text)).captured(0);
}

} // namespace

TailscaleClient::TailscaleClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<xmb::TailscalePeer>();
    qRegisterMetaType<xmb::TailscaleStatus>();
    qRegisterMetaType<QVector<xmb::TailscalePeer>>();
}

TailscaleClient::~TailscaleClient() {
    // Stop children without letting their finished() handlers run into a half-destroyed object.
    const auto procs = findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly);
    for (QProcess* p : procs) {
        p->disconnect();
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(1000);
        }
    }
}

QString TailscaleClient::executable() const {
    if (!executable_.isEmpty()) return QFileInfo(executable_).isFile() ? executable_ : QString();
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("tailscale"));
    if (!onPath.isEmpty()) return onPath;
#ifdef Q_OS_WIN
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList roots;
    for (const char* var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const QString v = env.value(QLatin1String(var));
        if (!v.isEmpty() && !roots.contains(v, Qt::CaseInsensitive)) roots << v;
    }
    if (roots.isEmpty()) roots << QStringLiteral("C:/Program Files");
    for (const QString& root : roots) {
        const QString candidate = QDir(root).filePath(QStringLiteral("Tailscale/tailscale.exe"));
        if (QFileInfo(candidate).isFile()) return QDir::toNativeSeparators(candidate);
    }
#endif
    return {};
}

bool TailscaleClient::validPeerArgument(const QString& value) {
    if (value.isEmpty() || value.size() > 253 || value.startsWith(QLatin1Char('-'))) return false;
    if (!value.contains(QLatin1Char('%'))) {
        QHostAddress addr;
        if (addr.setAddress(value)) return true;
    }
    static const QRegularExpression host(QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9.-]*$)"));
    return host.match(value).hasMatch();
}

QString TailscaleClient::describeCliError(const QString& stderrText, int exitCode) {
    const QString e = stderrText.toLower();
    if (e.contains(QLatin1String("is tailscale running")) || e.contains(QLatin1String("failed to connect to local tailscale"))
        || e.contains(QLatin1String("failed to connect to tailscaled"))
        || (e.contains(QLatin1String("pipe")) && e.contains(QLatin1String("cannot find the file"))))
        return QStringLiteral("Tailscale service is not running (start the Tailscale app or the \"Tailscale\" Windows service)");
    if (e.contains(QLatin1String("in use by another user")))
        return QStringLiteral("Tailscale is in use by another Windows user");
    if (e.contains(QLatin1String("access is denied")))
        return QStringLiteral("Access to the Tailscale service was denied: ") + firstLines(stderrText);
    if (e.contains(QLatin1String("logged out")) || e.contains(QLatin1String("needslogin"))
        || e.contains(QLatin1String("not logged in")))
        return QStringLiteral("Tailscale needs login");
    const QString text = firstLines(stderrText);
    if (!text.isEmpty()) return text;
    return QStringLiteral("tailscale exited with code %1").arg(exitCode);
}

bool TailscaleClient::parseStatus(const QByteArray& json, TailscaleStatus& out, QString* error) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("unreadable tailscale status JSON: %1").arg(
            pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not an object"));
        return false;
    }
    const QJsonObject o = doc.object();
    TailscaleStatus s;
    s.backendState = o.value(QLatin1String("BackendState")).toString();
    if (s.backendState.isEmpty()) s.backendState = QStringLiteral("NoState");
    s.version = o.value(QLatin1String("Version")).toString();
    s.authUrl = o.value(QLatin1String("AuthURL")).toString();
    s.health = stringList(o.value(QLatin1String("Health")));

    const QJsonObject self = o.value(QLatin1String("Self")).toObject();
    if (!self.isEmpty()) s.self = parsePeer(self);
    if (s.self.ips.isEmpty()) s.self.ips = stringList(o.value(QLatin1String("TailscaleIPs")));

    const QJsonObject tailnet = o.value(QLatin1String("CurrentTailnet")).toObject();
    s.tailnetName = tailnet.value(QLatin1String("Name")).toString();
    s.magicDnsSuffix = stripDot(tailnet.value(QLatin1String("MagicDNSSuffix")).toString());
    if (s.magicDnsSuffix.isEmpty()) s.magicDnsSuffix = stripDot(o.value(QLatin1String("MagicDNSSuffix")).toString());

    const QJsonObject peers = o.value(QLatin1String("Peer")).toObject();
    s.peers.reserve(peers.size());
    for (auto it = peers.constBegin(); it != peers.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        TailscalePeer p = parsePeer(it.value().toObject());
        if (p.exitNode) {
            s.exitNodeId = p.id;
            s.exitNodeName = p.dnsName.isEmpty() ? p.hostName : p.dnsName;
            s.exitNodeIp = p.ips.value(0);
            s.exitNodeOnline = p.online;
        }
        s.peers.push_back(std::move(p));
    }
    const QJsonObject ens = o.value(QLatin1String("ExitNodeStatus")).toObject();
    if (!ens.isEmpty()) {
        const QString id = ens.value(QLatin1String("ID")).toString();
        if (s.exitNodeId.isEmpty() || s.exitNodeId == id) {
            s.exitNodeId = id;
            s.exitNodeOnline = ens.value(QLatin1String("Online")).toBool();
            if (s.exitNodeIp.isEmpty()) s.exitNodeIp = stripPrefixLen(stringList(ens.value(QLatin1String("TailscaleIPs"))).value(0));
            if (s.exitNodeName.isEmpty()) s.exitNodeName = s.exitNodeIp;
        }
    }
    std::stable_sort(s.peers.begin(), s.peers.end(), [](const TailscalePeer& a, const TailscalePeer& b) {
        if (a.online != b.online) return a.online;
        return a.hostName.compare(b.hostName, Qt::CaseInsensitive) < 0;
    });
    out = std::move(s);
    return true;
}

void TailscaleClient::run(const QStringList& args, int timeoutMs, const Handler& done,
                          const std::function<bool(const QByteArray&)>& earlyStop) {
    auto* proc = new QProcess(this);
    auto* timer = new QTimer(proc);
    auto res = std::make_shared<Result>();
    auto finishedFlag = std::make_shared<bool>(false);
    proc->setProgram(executable());
    proc->setArguments(args);   // argument array: no shell, no quoting issues
    proc->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    // tailscale.exe is a console program; don't flash a console window from the GUI.
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= 0x08000000;   // CREATE_NO_WINDOW
    });
#endif
    QPointer<QProcess> guard(proc);
    auto stopProcess = [guard]() {
        if (guard && guard->state() != QProcess::NotRunning) guard->kill();
    };
    auto finish = [this, proc, timer, res, finishedFlag, done]() {
        if (*finishedFlag) return;
        *finishedFlag = true;
        timer->stop();
        proc->disconnect(this);
        proc->deleteLater();
        done(*res);
    };
    auto drain = [proc, res, stopProcess, earlyStop]() {
        const QByteArray o = proc->readAllStandardOutput();
        const QByteArray e = proc->readAllStandardError();
        if (res->out.size() + o.size() > kMaxStdout || res->err.size() + e.size() > kMaxStderr) {
            res->overflow = true;
            stopProcess();
            return;
        }
        res->out += o;
        res->err += e;
        if (earlyStop && !res->stoppedEarly && earlyStop(res->out + res->err)) {
            res->stoppedEarly = true;
            stopProcess();
        }
    };
    connect(proc, &QProcess::readyReadStandardOutput, this, drain);
    connect(proc, &QProcess::readyReadStandardError, this, drain);
    connect(proc, &QProcess::finished, this, [proc, res, drain, finish](int code, QProcess::ExitStatus st) {
        if (!res->overflow) drain();
        res->exitCode = code;
        res->crashed = st == QProcess::CrashExit;
        finish();
        Q_UNUSED(proc);
    });
    connect(proc, &QProcess::errorOccurred, this, [res, finish](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            res->started = false;
            finish();
        }
    });
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [res, stopProcess]() {
        res->timedOut = true;
        stopProcess();
    });
    res->started = true;
    timer->start(timeoutMs);
    proc->start(QIODevice::ReadOnly);
}

void TailscaleClient::refresh() {
    if (statusRunning_) return;
    if (!installed()) {
        qInfo().noquote() << "tailscale: not installed";
        emit failed(notInstalledMessage());
        return;
    }
    statusRunning_ = true;
    run({QStringLiteral("status"), QStringLiteral("--json")}, kStatusTimeoutMs, [this](const Result& r) {
        statusRunning_ = false;
        if (!r.started) { emit failed(notInstalledMessage()); return; }
        if (r.timedOut) {
            qInfo().noquote() << "tailscale: status timed out";
            emit failed(QStringLiteral("tailscale status timed out after %1 s").arg(kStatusTimeoutMs / 1000));
            return;
        }
        if (r.overflow) { emit failed(QStringLiteral("tailscale status output exceeds 4 MB")); return; }
        TailscaleStatus st;
        QString parseError;
        // A logged-out node may exit non-zero yet still print valid JSON; trust the JSON.
        if (!r.out.trimmed().isEmpty() && parseStatus(r.out, st, &parseError)) {
            qInfo().noquote() << "tailscale: status" << st.backendState << "self" << st.self.hostName
                              << "peers" << st.peers.size()
                              << "exit node" << (st.exitNodeName.isEmpty() ? QStringLiteral("none") : st.exitNodeName)
                              << (st.authUrl.isEmpty() ? QString() : QStringLiteral("(auth URL present)"));
            emit statusReady(st);
            return;
        }
        const QString reason = (r.exitCode != 0 || r.crashed)
            ? describeCliError(QString::fromUtf8(r.err + "\n" + r.out), r.exitCode) : parseError;
        qInfo().noquote() << "tailscale: status failed:" << reason;
        emit failed(reason);
    });
}

void TailscaleClient::runCommand(const QString& what, const QStringList& args, int timeoutMs) {
    if (!installed()) { emit commandDone(what, false, notInstalledMessage()); return; }
    if (commandRunning_) {
        emit commandDone(what, false, QStringLiteral("another Tailscale command is still running"));
        return;
    }
    commandRunning_ = true;
    qInfo().noquote() << "tailscale:" << what << "requested";
    const bool isUp = what == QLatin1String("up");
    std::function<bool(const QByteArray&)> stopOnLogin;
    if (isUp) stopOnLogin = [](const QByteArray& text) { return !loginUrlIn(text).isEmpty(); };
    run(args, timeoutMs, [this, what](const Result& r) {
        commandRunning_ = false;
        bool ok = false;
        QString msg;
        if (!r.started) msg = notInstalledMessage();
        else if (r.stoppedEarly) msg = QStringLiteral("Tailscale needs login: open ") + loginUrlIn(r.out + r.err);
        else if (r.timedOut) msg = QStringLiteral("tailscale %1 timed out").arg(what);
        else if (r.overflow) msg = QStringLiteral("tailscale %1 output exceeds the limit").arg(what);
        else if (r.exitCode == 0 && !r.crashed) {
            ok = true;
            if (what == QLatin1String("up")) msg = QStringLiteral("Tailscale connected");
            else if (what == QLatin1String("down")) msg = QStringLiteral("Tailscale disconnected");
            else msg = firstLines(QString::fromUtf8(r.out));
            if (msg.isEmpty()) msg = QStringLiteral("done");
        } else msg = describeCliError(QString::fromUtf8(r.err + "\n" + r.out), r.exitCode);
        qInfo().noquote() << "tailscale:" << what << (ok ? "ok" : "failed:")
                          << (ok ? QString() : (r.stoppedEarly ? QStringLiteral("needs login") : msg));
        emit commandDone(what, ok, msg);
    }, stopOnLogin);
}

void TailscaleClient::setConnected(bool up) {
    // Bare "tailscale up" only flips WantRunning on a logged-in node; no flags means prefs are untouched.
    runCommand(up ? QStringLiteral("up") : QStringLiteral("down"),
               {up ? QStringLiteral("up") : QStringLiteral("down")}, kUpDownTimeoutMs);
}

void TailscaleClient::setExitNode(const QString& peerIpOrEmpty) {
    const QString v = peerIpOrEmpty.trimmed();
    if (!v.isEmpty() && !validPeerArgument(v)) {
        emit commandDone(QStringLiteral("exit-node"), false, QStringLiteral("invalid exit node (expected an IP or host name)"));
        return;
    }
    runCommand(QStringLiteral("exit-node"), {QStringLiteral("set"), QStringLiteral("--exit-node=") + v}, kUpDownTimeoutMs);
}

void TailscaleClient::ping(const QString& peer) {
    const QString v = peer.trimmed();
    if (!validPeerArgument(v)) {
        emit commandDone(QStringLiteral("ping"), false, QStringLiteral("invalid peer (expected an IP or host name)"));
        return;
    }
    if (!installed()) { emit commandDone(QStringLiteral("ping"), false, notInstalledMessage()); return; }
    if (pingRunning_) { emit commandDone(QStringLiteral("ping"), false, QStringLiteral("a ping is already running")); return; }
    pingRunning_ = true;
    run({QStringLiteral("ping"), QStringLiteral("-c"), QStringLiteral("1"), QStringLiteral("--timeout=5s"), v},
        kPingTimeoutMs, [this, v](const Result& r) {
        pingRunning_ = false;
        const QString outText = QString::fromUtf8(r.out);
        bool ok = false;
        QString msg;
        if (!r.started) msg = notInstalledMessage();
        else if (r.timedOut) msg = QStringLiteral("ping %1 timed out").arg(v);
        else if (r.overflow) msg = QStringLiteral("ping output exceeds the limit");
        else if (r.exitCode == 0 && !r.crashed) { ok = true; msg = lastLine(outText); }
        else {
            const QString err = QString::fromUtf8(r.err).trimmed();
            msg = err.isEmpty() ? lastLine(outText) : describeCliError(err, r.exitCode);
            if (msg.isEmpty()) msg = QStringLiteral("no reply from %1").arg(v);
        }
        qInfo().noquote() << "tailscale: ping" << v << (ok ? "ok:" : "failed:") << msg;
        emit commandDone(QStringLiteral("ping"), ok, msg);
    });
}

} // namespace xmb
