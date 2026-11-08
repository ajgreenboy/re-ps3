#ifdef _WIN32
// Read the IPv4/IPv6 routing table for the default-route check (read-only, no admin needed).
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

#include "self_check.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <memory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

namespace xmb {
namespace {

using Level = CheckResult::Level;

CheckResult result(const QString& id, const QString& title, Level level, const QString& detail,
                   const QString& fix = {}) {
    return CheckResult{id, title, level, detail, fix};
}

QString native(const QString& path) { return QDir::toNativeSeparators(path); }

QString bytesText(qint64 bytes) {
    const double gb = double(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) return QStringLiteral("%1 GB").arg(gb, 0, 'f', gb >= 100 ? 0 : 1);
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 0);
}

// Opens a file, reads its first bytes and compares them with the expected magic (empty = any).
enum class FileState { Missing, Unreadable, BadHeader, Ok };
FileState probeFile(const QString& path, const QByteArray& magic) {
    QFile file(path);
    if (!file.exists()) return FileState::Missing;
    if (!file.open(QIODevice::ReadOnly)) return FileState::Unreadable;
    const QByteArray head = file.read(qMax<qint64>(4, magic.size()));
    if (head.size() < 4) return FileState::Unreadable;
    if (!magic.isEmpty() && !head.startsWith(magic)) return FileState::BadHeader;
    return FileState::Ok;
}

QJsonObject readJsonObject(const QString& path, bool* exists, QString* error) {
    QFile file(path);
    *exists = file.exists();
    if (!*exists) return {};
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
    const QByteArray bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024) { *error = QStringLiteral("larger than 1 MB"); return {}; }
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = parse.error != QJsonParseError::NoError ? parse.errorString() : QStringLiteral("not a JSON object");
        return {};
    }
    return doc.object();
}

// ---------------------------------------------------------------- app

CheckResult checkVersion(const SelfCheckInputs& in) {
    QString detail = QStringLiteral("Version %1").arg(in.appVersion.isEmpty() ? QStringLiteral("unknown") : in.appVersion);
    if (!in.appCommit.isEmpty()) detail += QStringLiteral(", build %1").arg(in.appCommit.left(10));
    detail += QStringLiteral(", Qt %1").arg(QString::fromLatin1(qVersion()));
    return result(QStringLiteral("app.version"), QStringLiteral("System Software"), Level::Ok, detail);
}

// ---------------------------------------------------------------- dev_flash

struct Resource { const char* path; bool core; };
// The RCO containers the code loads (grep of src/*.cpp, 2026-11-08). Core = the XMB cannot draw without it.
const Resource kRcos[] = {
    {"system_plugin", true}, {"explore_plugin_full", true}, {"xmb_plugin_normal", true},
    {"sysconf_plugin", true}, {"impose_plugin", true}, {"playlist_plugin", false},
    {"videoplayer_plugin", false}, {"oskfullkeypanel_plugin", false}, {"oskpanel_plugin", false},
    {"user_plugin", false}, {"msgdialog_plugin", false}, {"software_update_plugin", false},
    {"premo_plugin", false}, {"netconf_plugin", false}, {"dlna_plugin", false},
    {"deviceconf_plugin", false}, {"auth_plugin", false},
};
const Resource kQrcs[] = {{"icontex", true}, {"lines", true}, {"icons", false}};

QVector<CheckResult> checkDevFlash(const SelfCheckInputs& in) {
    QVector<CheckResult> out;
    const QString id = QStringLiteral("assets.dev_flash");
    const QString title = QStringLiteral("System Assets");
    const QDir root(in.devFlashRoot);
    if (in.devFlashRoot.isEmpty() || !root.exists()) {
        out.push_back(result(id, title, Level::Fail,
            QStringLiteral("dev_flash not found at %1").arg(in.devFlashRoot.isEmpty() ? QStringLiteral("(not set)") : native(in.devFlashRoot)),
            QStringLiteral("Point --dev-flash at a dev_flash folder installed by RPCS3 from the official PS3 firmware.")));
        return out;
    }
    QStringList coreMissing, optionalMissing, damaged;
    int okCount = 0, total = 0;
    auto probe = [&](const QString& rel, const QByteArray& magic, bool core) {
        ++total;
        switch (probeFile(root.filePath(rel), magic)) {
        case FileState::Ok: ++okCount; break;
        case FileState::Missing: (core ? coreMissing : optionalMissing) << rel; break;
        case FileState::Unreadable: case FileState::BadHeader: damaged << rel; break;
        }
    };
    for (const auto& r : kRcos)
        probe(QStringLiteral("vsh/resource/%1.rco").arg(QLatin1String(r.path)), QByteArray("FRP\0", 4), r.core);
    for (const auto& r : kQrcs)
        probe(QStringLiteral("vsh/resource/qgl/%1.qrc").arg(QLatin1String(r.path)), QByteArrayLiteral("QRC"), r.core); // QRCF, or QRCC (compressed; qrc.cpp unpacks it)
    probe(QStringLiteral("vsh/resource/coldboot.raf"), QByteArrayLiteral("_RAF"), false);
    probe(QStringLiteral("vsh/resource/coldboot_stereo.ac3"), QByteArray(), false);

    Level level = Level::Ok;
    QString detail = QStringLiteral("%1 of %2 resource files readable in %3").arg(okCount).arg(total).arg(native(in.devFlashRoot));
    QString fix;
    if (!coreMissing.isEmpty() || !damaged.isEmpty()) {
        level = Level::Fail;
        if (!coreMissing.isEmpty()) detail += QStringLiteral("; missing: %1").arg(coreMissing.join(QStringLiteral(", ")));
        if (!damaged.isEmpty()) detail += QStringLiteral("; unreadable or damaged: %1").arg(damaged.join(QStringLiteral(", ")));
        fix = QStringLiteral("Reinstall the PS3 firmware in RPCS3 (File > Install Firmware) so dev_flash is complete.");
    }
    if (!optionalMissing.isEmpty()) {
        if (level == Level::Ok) level = Level::Warn;
        detail += QStringLiteral("; optional missing: %1").arg(optionalMissing.join(QStringLiteral(", ")));
        if (fix.isEmpty()) fix = QStringLiteral("Some pages will draw without firmware art. Reinstall the firmware in RPCS3 to restore them.");
    }
    out.push_back(result(id, title, level, detail, fix));

    // Fonts: Assets registers data/font/SCE-PS3-*.TTF; RD-R-LATIN is the main face, SR/VR the other styles.
    const QDir fontDir(root.filePath(QStringLiteral("data/font")));
    const QStringList fonts = fontDir.entryList({QStringLiteral("SCE-PS3-*.TTF")}, QDir::Files, QDir::Name);
    QStringList fontBad, fontMissing;
    for (const QString& f : fonts)
        if (probeFile(fontDir.filePath(f), QByteArray("\0\1\0\0", 4)) != FileState::Ok) fontBad << f;
    for (const char* stem : {"RD-R-LATIN", "SR-R-LATIN", "VR-R-LATIN"})
        if (!fonts.contains(QStringLiteral("SCE-PS3-%1.TTF").arg(QLatin1String(stem)), Qt::CaseInsensitive))
            fontMissing << QLatin1String(stem);
    const QString fid = QStringLiteral("assets.fonts"), ftitle = QStringLiteral("System Fonts");
    if (fonts.isEmpty()) {
        out.push_back(result(fid, ftitle, Level::Fail, QStringLiteral("No firmware fonts in %1; text falls back to Segoe UI").arg(native(fontDir.path())),
            QStringLiteral("Reinstall the PS3 firmware in RPCS3 so dev_flash/data/font is populated.")));
    } else if (!fontBad.isEmpty() || !fontMissing.isEmpty()) {
        QString d = QStringLiteral("%1 font files").arg(fonts.size());
        if (!fontMissing.isEmpty()) d += QStringLiteral("; missing %1").arg(fontMissing.join(QStringLiteral(", ")));
        if (!fontBad.isEmpty()) d += QStringLiteral("; unreadable %1").arg(fontBad.join(QStringLiteral(", ")));
        out.push_back(result(fid, ftitle, Level::Warn, d,
            QStringLiteral("Reinstall the PS3 firmware in RPCS3; missing styles fall back to the main face.")));
    } else {
        out.push_back(result(fid, ftitle, Level::Ok, QStringLiteral("%1 firmware font files readable").arg(fonts.size())));
    }
    return out;
}

// ---------------------------------------------------------------- data directory

QVector<CheckResult> checkData(const SelfCheckInputs& in) {
    QVector<CheckResult> out;
    const QString id = QStringLiteral("data.writable"), title = QStringLiteral("Data Folder");
    const QDir dir(in.dataDirectory);
    if (in.dataDirectory.isEmpty() || !dir.exists()) {
        out.push_back(result(id, title, Level::Fail,
            QStringLiteral("Data folder not found: %1").arg(in.dataDirectory.isEmpty() ? QStringLiteral("(not set)") : native(in.dataDirectory)),
            QStringLiteral("Start the app with --data-dir pointing at a folder you can write to.")));
        return out;
    }
    {
        // The only write this check makes: a uniquely named temp file, removed immediately.
        QTemporaryFile probe(dir.filePath(QStringLiteral(".selfcheck-XXXXXX.tmp")));
        probe.setAutoRemove(true);
        QString failure;
        QString probePath;
        if (!probe.open()) failure = probe.errorString();
        else {
            probePath = probe.fileName();
            const QByteArray bytes("xmb self check\n");
            if (probe.write(bytes) != bytes.size() || !probe.flush()) failure = probe.errorString();
            probe.close();
            if (!probe.remove()) failure = QStringLiteral("temporary file could not be deleted");
        }
        if (!failure.isEmpty())
            out.push_back(result(id, title, Level::Fail, QStringLiteral("Cannot write to %1 (%2)").arg(native(in.dataDirectory), failure),
                QStringLiteral("Make the folder writable for this Windows account, or move it with --data-dir.")));
        else if (!probePath.isEmpty() && QFileInfo::exists(probePath))
            out.push_back(result(id, title, Level::Warn, QStringLiteral("Writable, but the test file was left behind: %1").arg(native(probePath)),
                QStringLiteral("Delete the leftover .selfcheck file; antivirus may be holding it.")));
        else
            out.push_back(result(id, title, Level::Ok, QStringLiteral("Writable: %1").arg(native(in.dataDirectory))));
    }
    const QStorageInfo storage(in.dataDirectory);
    const QString sid = QStringLiteral("data.space"), stitle = QStringLiteral("Free Space");
    if (!storage.isValid() || !storage.isReady()) {
        out.push_back(result(sid, stitle, Level::Warn, QStringLiteral("The drive holding the data folder did not report its size")));
    } else {
        const qint64 free = storage.bytesAvailable();
        const QString drive = native(storage.rootPath());
        const QString d = QStringLiteral("%1 free of %2 on %3").arg(bytesText(free), bytesText(storage.bytesTotal()), drive);
        if (storage.isReadOnly())
            out.push_back(result(sid, stitle, Level::Fail, d + QStringLiteral(" (read-only)"), QStringLiteral("Move the data folder to a writable drive.")));
        else if (free < 200LL * 1024 * 1024)
            out.push_back(result(sid, stitle, Level::Fail, d, QStringLiteral("Free space on %1; settings, playlists and artwork can no longer be saved.").arg(drive)));
        else if (free < 2LL * 1024 * 1024 * 1024)
            out.push_back(result(sid, stitle, Level::Warn, d, QStringLiteral("Free at least 2 GB on %1 for artwork and image caches.").arg(drive)));
        else
            out.push_back(result(sid, stitle, Level::Ok, d));
    }
    return out;
}

// ---------------------------------------------------------------- GPU

CheckResult checkGpu(const SelfCheckInputs& in) {
    const QString id = QStringLiteral("gpu.opengl"), title = QStringLiteral("Graphics");
    if (in.glVersion.isEmpty())
        return result(id, title, Level::Warn, QStringLiteral("The renderer has not reported an OpenGL version yet"),
                      QStringLiteral("Run the check again once the XMB has drawn its first frame."));
    QString detail = in.glRenderer.isEmpty() ? QStringLiteral("unknown renderer") : in.glRenderer;
    if (!in.glVendor.isEmpty() && !detail.contains(in.glVendor, Qt::CaseInsensitive)) detail = in.glVendor + QLatin1Char(' ') + detail;
    detail += QStringLiteral(", OpenGL %1").arg(in.glVersion);
    static const QRegularExpression versionPattern(QStringLiteral("(\\d+)\\.(\\d+)"));
    const auto m = versionPattern.match(in.glVersion);
    const int major = m.hasMatch() ? m.captured(1).toInt() : 0, minor = m.hasMatch() ? m.captured(2).toInt() : 0;
    if (in.glVersion.contains(QLatin1String("OpenGL ES"), Qt::CaseInsensitive) || major < 3 || (major == 3 && minor < 3))
        return result(id, title, Level::Fail, detail,
                      QStringLiteral("The XMB needs OpenGL 3.3 core. Install the GPU vendor's driver (not the Windows Update display driver)."));
    const QString r = in.glRenderer.toLower();
    if (r.contains(QLatin1String("basic render")) || r.contains(QLatin1String("gdi generic")) || r.contains(QLatin1String("llvmpipe"))
        || r.contains(QLatin1String("softpipe")) || r.contains(QLatin1String("swiftshader")))
        return result(id, title, Level::Warn, detail + QStringLiteral(" (software rendering)"),
                      QStringLiteral("Install the GPU vendor's driver; software rendering cannot keep 60 fps."));
    return result(id, title, Level::Ok, detail);
}

// ---------------------------------------------------------------- network helpers

struct HttpProbe { bool reached = false; int status = 0; QString error; QByteArray body; qint64 ms = 0; };

// One unauthenticated GET (no tokens, no cookies, no redirects followed). Any HTTP status = reachable.
HttpProbe httpGet(const QUrl& url, int timeoutMs, bool wantBody) {
    HttpProbe probe;
    QElapsedTimer clock; clock.start();
    QNetworkAccessManager manager;
    manager.setCookieJar(nullptr);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("re-ps3-native-selfcheck"));
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer guard; guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 256 * 1024) reply->abort(); // public info endpoints are tiny
    });
    guard.start(timeoutMs + 100);
    if (!reply->isFinished()) loop.exec(QEventLoop::ExcludeUserInputEvents);
    probe.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    probe.reached = probe.status > 0;
    if (wantBody && probe.reached) probe.body = reply->read(256 * 1024);
    if (!probe.reached) probe.error = reply->error() == QNetworkReply::OperationCanceledError
        ? QStringLiteral("no answer within %1 ms").arg(timeoutMs) : reply->errorString();
    reply->deleteLater();
    probe.ms = clock.elapsed();
    return probe;
}

QString hostText(const QUrl& url) {
    QString text = url.host();
    if (url.port() > 0) text += QLatin1Char(':') + QString::number(url.port());
    return text;
}

// ---------------------------------------------------------------- network

CheckResult checkRoute() {
    const QString id = QStringLiteral("net.route"), title = QStringLiteral("Network Connection");
    bool defaultRoute = false;
    QString via;
#ifdef _WIN32
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    if (GetIpForwardTable2(AF_UNSPEC, &table) == NO_ERROR && table) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IPFORWARD_ROW2& row = table->Table[i];
            if (row.DestinationPrefix.PrefixLength != 0) continue;
            defaultRoute = true;
            if (via.isEmpty()) {
                const QNetworkInterface iface = QNetworkInterface::interfaceFromIndex(int(row.InterfaceIndex));
                if (iface.isValid() && (iface.flags() & QNetworkInterface::IsUp)) via = iface.humanReadableName();
            }
        }
        FreeMibTable(table);
    }
#else
    // Portable fallback: an up, running, non-loopback interface with a routable address.
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const auto f = iface.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack)) continue;
        for (const auto& entry : iface.addressEntries())
            if (!entry.ip().isLinkLocal() && !entry.ip().isLoopback()) { defaultRoute = true; via = iface.humanReadableName(); break; }
        if (defaultRoute) break;
    }
#endif
    if (!defaultRoute)
        return result(id, title, Level::Fail, QStringLiteral("No default route; this PC is not connected to a network"),
                      QStringLiteral("Check the Ethernet cable or Wi-Fi connection in Windows network settings."));
    return result(id, title, Level::Ok, via.isEmpty() ? QStringLiteral("Default route present") : QStringLiteral("Default route via %1").arg(via));
}

CheckResult checkDns(int timeoutMs) {
    const QString id = QStringLiteral("net.dns"), title = QStringLiteral("Internet Name Lookup");
    const QString name = QStringLiteral("www.msftconnecttest.com");
    QElapsedTimer clock; clock.start();
    QObject context;
    QEventLoop loop;
    bool done = false;
    QHostInfo info;
    const int lookup = QHostInfo::lookupHost(name, &context, [&](const QHostInfo& host) { info = host; done = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    if (!done) loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (!done) {
        QHostInfo::abortHostLookup(lookup);
        return result(id, title, Level::Fail, QStringLiteral("%1 did not resolve within %2 ms").arg(name).arg(timeoutMs),
                      QStringLiteral("Check the DNS server set on this PC or the router (AdGuard, Pi-hole) is answering."));
    }
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty())
        return result(id, title, Level::Fail, QStringLiteral("%1 did not resolve (%2)").arg(name, info.errorString()),
                      QStringLiteral("Check the DNS server set on this PC or the router (AdGuard, Pi-hole) is answering."));
    return result(id, title, Level::Ok, QStringLiteral("%1 resolved in %2 ms").arg(name).arg(clock.elapsed()));
}

QVector<CheckResult> checkJellyfin(const SelfCheckInputs& in) {
    QVector<CheckResult> out;
    const QString id = QStringLiteral("net.jellyfin"), title = QStringLiteral("Jellyfin");
    bool exists = false; QString error;
    const QJsonObject root = readJsonObject(QDir(in.dataDirectory).filePath(QStringLiteral("apps.json")), &exists, &error);
    if (!error.isEmpty()) {
        out.push_back(result(id, title, Level::Warn, QStringLiteral("apps.json cannot be read (%1)").arg(error),
                             QStringLiteral("Fix or re-import apps.json from Games and Apps.")));
        return out;
    }
    QVector<QPair<QString, QUrl>> servers;
    for (const QJsonValue& v : root.value(QStringLiteral("apps")).toArray()) {
        const QJsonObject app = v.toObject();
        if (app.value(QStringLiteral("kind")).toString() != QLatin1String("jellyfin")) continue;
        const QUrl url(app.value(QStringLiteral("url")).toString().trimmed());
        if (url.isValid() && !url.host().isEmpty()) servers.push_back({app.value(QStringLiteral("title")).toString(), url});
    }
    if (servers.isEmpty()) {
        out.push_back(result(id, title, Level::Ok, QStringLiteral("Not set up"),
                             QStringLiteral("Add a Jellyfin server under Games and Apps to browse its library.")));
        return out;
    }
    if (!in.network) {
        out.push_back(result(id, title, Level::Ok, QStringLiteral("%1 server(s) registered; not probed (network checks off)").arg(servers.size())));
        return out;
    }
    int index = 0;
    for (const auto& server : servers) {
        if (index == 4) break; // keep the whole check bounded
        const QString rowId = servers.size() == 1 ? id : QStringLiteral("%1.%2").arg(id).arg(index);
        const QString rowTitle = servers.size() == 1 ? title : QStringLiteral("Jellyfin: %1").arg(server.first);
        ++index;
        QUrl probeUrl = server.second;
        QString path = probeUrl.path();
        while (path.endsWith(QLatin1Char('/'))) path.chop(1);
        probeUrl.setPath(path + QStringLiteral("/System/Info/Public"));
        probeUrl.setQuery(QString()); probeUrl.setUserInfo(QString());
        const HttpProbe probe = httpGet(probeUrl, in.timeoutMs, true);
        if (!probe.reached) {
            out.push_back(result(rowId, rowTitle, Level::Fail, QStringLiteral("%1 not reachable (%2)").arg(hostText(server.second), probe.error),
                                 QStringLiteral("Check the Jellyfin server is running and this PC can reach %1.").arg(hostText(server.second))));
            continue;
        }
        const QJsonObject info = QJsonDocument::fromJson(probe.body).object();
        const QString version = info.value(QStringLiteral("Version")).toString();
        const QString name = info.value(QStringLiteral("ServerName")).toString();
        if (probe.status == 200 && !version.isEmpty())
            out.push_back(result(rowId, rowTitle, Level::Ok, QStringLiteral("%1 answered in %2 ms (Jellyfin %3%4)").arg(hostText(server.second))
                .arg(probe.ms).arg(version, name.isEmpty() ? QString() : QStringLiteral(", ") + name)));
        else
            out.push_back(result(rowId, rowTitle, Level::Warn, QStringLiteral("%1 answered HTTP %2 but is not a Jellyfin server").arg(hostText(server.second)).arg(probe.status),
                                 QStringLiteral("Check the server address in the Jellyfin registration.")));
    }
    return out;
}

CheckResult checkHomeAssistant(const SelfCheckInputs& in) {
    const QString id = QStringLiteral("net.homeassistant"), title = QStringLiteral("Home Assistant");
    bool exists = false; QString error;
    const QJsonObject config = readJsonObject(QDir(in.dataDirectory).filePath(QStringLiteral("homeassistant.json")), &exists, &error);
    if (!exists) return result(id, title, Level::Ok, QStringLiteral("Not set up"),
                               QStringLiteral("Put {\"url\": ..., \"token\": ...} in homeassistant.json in the data folder."));
    if (!error.isEmpty()) return result(id, title, Level::Warn, QStringLiteral("homeassistant.json cannot be read (%1)").arg(error),
                                        QStringLiteral("Fix homeassistant.json in the data folder."));
    const QUrl url(config.value(QStringLiteral("url")).toString().trimmed());
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return result(id, title, Level::Warn, QStringLiteral("homeassistant.json has no valid http(s) url"),
                      QStringLiteral("Set \"url\" in homeassistant.json, e.g. http://homeassistant.local:8123."));
    // Presence only: the token itself is never read into the report.
    const bool token = !config.value(QStringLiteral("token")).toString().trimmed().isEmpty()
        || QFileInfo(QDir(in.dataDirectory).filePath(QStringLiteral("homeassistant/session.bin"))).isFile();
    const QString tokenText = token ? QStringLiteral("token stored") : QStringLiteral("no token");
    if (!in.network) return result(id, title, token ? Level::Ok : Level::Warn,
                                   QStringLiteral("%1, %2; not probed").arg(hostText(url), tokenText));
    QUrl probeUrl = url;
    QString path = probeUrl.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    probeUrl.setPath(path + QStringLiteral("/api/"));
    probeUrl.setQuery(QString()); probeUrl.setUserInfo(QString());
    const HttpProbe probe = httpGet(probeUrl, in.timeoutMs, false);
    if (!probe.reached)
        return result(id, title, Level::Fail, QStringLiteral("%1 not reachable (%2)").arg(hostText(url), probe.error),
                      QStringLiteral("Check Home Assistant is running and the url in homeassistant.json is right."));
    if (!token)
        return result(id, title, Level::Warn, QStringLiteral("%1 answered in %2 ms, but no access token is stored").arg(hostText(url)).arg(probe.ms),
                      QStringLiteral("Create a long-lived access token in your Home Assistant profile and add it to homeassistant.json."));
    return result(id, title, Level::Ok, QStringLiteral("%1 answered in %2 ms, %3").arg(hostText(url)).arg(probe.ms).arg(tokenText));
}

CheckResult checkSpotify(const SelfCheckInputs& in) {
    const QString id = QStringLiteral("net.spotify"), title = QStringLiteral("Spotify");
    static const QRegularExpression clientIdPattern(QStringLiteral("^[0-9a-fA-F]{32}$"));
    QString source;
    if (clientIdPattern.match(qEnvironmentVariable("SPOTIFY_CLIENT_ID").trimmed()).hasMatch()) source = QStringLiteral("environment");
    bool exists = false; QString error;
    const QJsonObject config = readJsonObject(QDir(in.dataDirectory).filePath(QStringLiteral("spotify.json")), &exists, &error);
    if (source.isEmpty() && exists && error.isEmpty()
        && clientIdPattern.match(config.value(QStringLiteral("clientId")).toString().trimmed()).hasMatch())
        source = QStringLiteral("spotify.json");
    if (source.isEmpty()) {
        if (exists) return result(id, title, Level::Warn,
                                  error.isEmpty() ? QStringLiteral("spotify.json has no valid client id") : QStringLiteral("spotify.json cannot be read (%1)").arg(error),
                                  QStringLiteral("Put your Spotify app's 32-character client id in spotify.json as \"clientId\"."));
        return result(id, title, Level::Ok, QStringLiteral("Not set up"),
                      QStringLiteral("Register an app on the Spotify developer dashboard and put its client id in spotify.json."));
    }
    const bool session = QFileInfo(QDir(in.dataDirectory).filePath(QStringLiteral("spotify/session.bin"))).isFile();
    QString detail = QStringLiteral("Client id set (%1), %2").arg(source, session ? QStringLiteral("saved sign-in present") : QStringLiteral("not signed in"));
    if (in.network) {
        const HttpProbe probe = httpGet(QUrl(QStringLiteral("https://api.spotify.com/v1")), in.timeoutMs, false);
        if (!probe.reached)
            return result(id, title, Level::Fail, detail + QStringLiteral("; api.spotify.com not reachable (%1)").arg(probe.error),
                          QStringLiteral("Check the internet connection and that DNS filtering does not block api.spotify.com."));
        detail += QStringLiteral("; api.spotify.com answered in %1 ms").arg(probe.ms);
    }
    return result(id, title, session ? Level::Ok : Level::Warn, detail,
                  session ? QString() : QStringLiteral("Open Spotify in the Music column and sign in."));
}

QString findTailscale(const QString& explicitPath) {
    if (!explicitPath.isEmpty()) return QFileInfo(explicitPath).isFile() ? explicitPath : QString();
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("tailscale"));
    if (!onPath.isEmpty()) return onPath;
#ifdef _WIN32
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const QString root = env.value(QLatin1String(var));
        if (root.isEmpty()) continue;
        const QString candidate = QDir(root).filePath(QStringLiteral("Tailscale/tailscale.exe"));
        if (QFileInfo(candidate).isFile()) return candidate;
    }
#endif
    return {};
}

CheckResult checkTailscale(const SelfCheckInputs& in) {
    const QString id = QStringLiteral("net.tailscale"), title = QStringLiteral("Tailscale");
    const QString exe = findTailscale(in.tailscaleExecutable);
    if (exe.isEmpty()) return result(id, title, Level::Ok, QStringLiteral("Not installed"),
                                     QStringLiteral("Install Tailscale for Windows to reach this PC and your servers away from home."));
    QProcess process;
    process.setProgram(exe);
    process.setArguments({QStringLiteral("status"), QStringLiteral("--json")});
    process.setStandardInputFile(QProcess::nullDevice());
#ifdef _WIN32
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(in.timeoutMs))
        return result(id, title, Level::Warn, QStringLiteral("tailscale.exe could not be started (%1)").arg(process.errorString()),
                      QStringLiteral("Reinstall Tailscale for Windows."));
    if (!process.waitForFinished(in.timeoutMs)) {
        process.kill(); process.waitForFinished(200);
        return result(id, title, Level::Warn, QStringLiteral("tailscale status did not answer within %1 ms").arg(in.timeoutMs),
                      QStringLiteral("Restart the Tailscale service from the Tailscale tray icon."));
    }
    const QByteArray stdoutBytes = process.readAllStandardOutput();
    const QJsonObject status = QJsonDocument::fromJson(stdoutBytes).object();
    const QString state = status.value(QStringLiteral("BackendState")).toString();
    if (state.isEmpty()) {
        const QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed().section(QLatin1Char('\n'), 0, 0);
        return result(id, title, Level::Warn, QStringLiteral("Installed, service not answering%1").arg(err.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(err.left(120))),
                      QStringLiteral("Start Tailscale from the Start menu so its service is running."));
    }
    const QJsonObject peers = status.value(QStringLiteral("Peer")).toObject();
    int online = 0;
    for (auto it = peers.begin(); it != peers.end(); ++it) if (it->toObject().value(QStringLiteral("Online")).toBool()) ++online;
    if (state == QLatin1String("Running"))
        return result(id, title, Level::Ok, QStringLiteral("Connected, %1 of %2 devices online").arg(online).arg(peers.size()));
    if (state == QLatin1String("NeedsLogin") || state == QLatin1String("NeedsMachineAuth"))
        return result(id, title, Level::Warn, QStringLiteral("Installed, signed out (%1)").arg(state),
                      QStringLiteral("Sign in from Network > Tailscale or the Tailscale tray icon."));
    if (state == QLatin1String("Stopped"))
        return result(id, title, Level::Ok, QStringLiteral("Installed, turned off"),
                      QStringLiteral("Turn it on from Network > Tailscale when you need remote access."));
    return result(id, title, Level::Warn, QStringLiteral("Installed, state %1").arg(state),
                  QStringLiteral("Open the Tailscale tray icon to finish connecting."));
}

// ---------------------------------------------------------------- Steam and RAWG

QStringList steamCandidates(const QString& explicitRoot) {
    // Same order as steam_accounts.cpp::steamRootCandidates (explicit, XMB_STEAM_ROOT, registry, defaults).
    if (!explicitRoot.trimmed().isEmpty()) return {explicitRoot.trimmed()};
    const QString environmentRoot = qEnvironmentVariable("XMB_STEAM_ROOT").trimmed();
    if (!environmentRoot.isEmpty()) return {environmentRoot};
    QStringList candidates;
#ifdef _WIN32
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString registry = steam.value(QStringLiteral("SteamPath")).toString().trimmed();
    if (!registry.isEmpty()) candidates << registry;
    for (const char* var : {"ProgramFiles(x86)", "ProgramFiles"}) {
        const QString v = qEnvironmentVariable(var).trimmed();
        if (!v.isEmpty()) candidates << QDir(v).filePath(QStringLiteral("Steam"));
    }
#else
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    candidates << QDir(home).filePath(QStringLiteral(".steam/steam")) << QDir(home).filePath(QStringLiteral(".local/share/Steam"));
#endif
    return candidates;
}

QVector<CheckResult> checkSteam(const SelfCheckInputs& in) {
    QVector<CheckResult> out;
    const QString id = QStringLiteral("steam.root"), title = QStringLiteral("Steam");
    QString root;
    for (const QString& c : steamCandidates(in.steamRoot)) {
        const QFileInfo info(c);
        if (info.isDir() && QFileInfo(QDir(info.absoluteFilePath()).filePath(QStringLiteral("config"))).isDir()) {
            root = info.canonicalFilePath();
            break;
        }
    }
    if (root.isEmpty()) {
        out.push_back(result(id, title, Level::Warn, QStringLiteral("Steam not found (looked at --steam-root, XMB_STEAM_ROOT, the registry and Program Files)"),
                             QStringLiteral("Install Steam, or start the app with --steam-root pointing at the Steam folder.")));
        return out;
    }
    QString vdf = QDir(root).filePath(QStringLiteral("steamapps/libraryfolders.vdf"));
    if (!QFileInfo(vdf).isFile()) vdf = QDir(root).filePath(QStringLiteral("config/libraryfolders.vdf"));
    QFile file(vdf);
    if (!file.open(QIODevice::ReadOnly)) {
        out.push_back(result(id, title, Level::Warn, QStringLiteral("Found at %1, but libraryfolders.vdf cannot be read").arg(native(root)),
                             QStringLiteral("Start Steam once so it writes its library list, then run the check again.")));
        return out;
    }
    const QString text = QString::fromUtf8(file.read(1024 * 1024));
    static const QRegularExpression pathPattern(QStringLiteral("\"path\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\""));
    QStringList libraries, missing;
    for (auto it = pathPattern.globalMatch(text); it.hasNext();) {
        QString path = it.next().captured(1);
        path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        libraries << path;
        if (!QFileInfo(QDir(path).filePath(QStringLiteral("steamapps"))).isDir()) missing << path;
    }
    if (libraries.isEmpty()) {
        out.push_back(result(id, title, Level::Warn, QStringLiteral("Found at %1, but libraryfolders.vdf lists no libraries").arg(native(root)),
                             QStringLiteral("Open Steam > Settings > Storage once so it rewrites the library list.")));
    } else if (!missing.isEmpty()) {
        out.push_back(result(id, title, Level::Warn, QStringLiteral("Found at %1; %2 of %3 libraries unavailable: %4").arg(native(root))
                .arg(missing.size()).arg(libraries.size()).arg(missing.join(QStringLiteral(", "))),
                             QStringLiteral("Connect the drive that holds the missing library; its games are hidden until then.")));
    } else {
        out.push_back(result(id, title, Level::Ok, QStringLiteral("Found at %1, %2 librar%3").arg(native(root)).arg(libraries.size())
                .arg(libraries.size() == 1 ? QStringLiteral("y") : QStringLiteral("ies"))));
    }
    return out;
}

CheckResult checkRawg(const SelfCheckInputs& in) {
    const QString id = QStringLiteral("rawg.key"), title = QStringLiteral("Game Artwork (RAWG)");
    static const QRegularExpression keyPattern(QStringLiteral("^[A-Za-z0-9_-]{16,128}$")); // as rawg_client.cpp
    if (keyPattern.match(QString::fromUtf8(qgetenv("RAWG_API_KEY"))).hasMatch())
        return result(id, title, Level::Ok, QStringLiteral("API key set (environment)"));
    QString path = in.rawgKeyFile;
    if (path.isEmpty()) {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!appData.isEmpty()) path = QDir(appData).filePath(QStringLiteral("rawg_api_key"));
    }
    QFile file(path);
    if (path.isEmpty() || !file.exists())
        return result(id, title, Level::Ok, QStringLiteral("No API key; missing game artwork is not fetched"),
                      QStringLiteral("Add a free RAWG API key under Settings to fetch missing game artwork."));
    if (!file.open(QIODevice::ReadOnly))
        return result(id, title, Level::Warn, QStringLiteral("API key file cannot be read"), QStringLiteral("Enter the RAWG API key again."));
    const QByteArray bytes = file.read(257);
    const bool valid = bytes.size() <= 256 && keyPattern.match(QString::fromUtf8(bytes)).hasMatch();
    return valid ? result(id, title, Level::Ok, QStringLiteral("API key set"))
                 : result(id, title, Level::Warn, QStringLiteral("API key file present but not a valid key"),
                          QStringLiteral("Enter the RAWG API key again."));
}

} // namespace

QString checkLevelName(CheckResult::Level level) {
    switch (level) {
    case CheckResult::Ok: return QStringLiteral("OK");
    case CheckResult::Warn: return QStringLiteral("WARN");
    case CheckResult::Fail: return QStringLiteral("FAIL");
    }
    return {};
}

QVector<CheckResult> runSelfCheck(const SelfCheckInputs& in) {
    QVector<CheckResult> out;
    out.push_back(checkVersion(in));
    out += checkDevFlash(in);
    out += checkData(in);
    out.push_back(checkGpu(in));
    const CheckResult route = checkRoute();
    out.push_back(route);
    const bool probe = in.network && route.level != CheckResult::Fail;
    if (probe) out.push_back(checkDns(in.timeoutMs));
    else out.push_back(result(QStringLiteral("net.dns"), QStringLiteral("Internet Name Lookup"), Level::Warn,
                              in.network ? QStringLiteral("Skipped: no network connection") : QStringLiteral("Skipped: network checks off")));
    SelfCheckInputs services = in;
    services.network = probe;
    out += checkJellyfin(services);
    out.push_back(checkHomeAssistant(services));
    out.push_back(checkSpotify(services));
    out.push_back(checkTailscale(in));
    out += checkSteam(in);
    out.push_back(checkRawg(in));
    return out;
}

SelfCheck::SelfCheck(QObject* parent) : QObject(parent) {
    qRegisterMetaType<xmb::CheckResult>("xmb::CheckResult");
    qRegisterMetaType<QVector<xmb::CheckResult>>("QVector<xmb::CheckResult>");
}

void SelfCheck::run() {
    if (running_) return;
    running_ = true;
    const SelfCheckInputs inputs = inputs_;
    auto results = std::make_shared<QVector<CheckResult>>();
    QThread* worker = QThread::create([inputs, results]() { *results = runSelfCheck(inputs); });
    worker->setObjectName(QStringLiteral("xmb-selfcheck"));
    // QThread::finished is emitted on the worker thread; with `this` as context it is queued to this
    // object's thread and silently dropped if this object was destroyed while the check ran.
    connect(worker, &QThread::finished, this, [this, results]() {
        running_ = false;
        emit finished(*results);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start(QThread::LowPriority);
}

} // namespace xmb
