#include "updater.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>

namespace xmb {
namespace {
void fail(QString* error, const QString& message) { if(error) *error = message; }
QString bodyHash(const QString& body) {
    static const QRegularExpression re(QStringLiteral("(?:^|\\n)\\s*SHA256[: ]*([0-9a-fA-F]{64})(?=\\s|$)"), QRegularExpression::CaseInsensitiveOption);
    return re.match(body).captured(1);
}
Asset sidecar(const Release& release, const Asset& asset) {
    for(const auto& a : release.assets)
        if(a.name.compare(asset.name + QStringLiteral(".sha256"), Qt::CaseInsensitive) == 0) return a;
    // An unnamed association is safe only when there is exactly one ZIP and one sidecar.
    int zips = 0, hashes = 0; Asset found;
    for(const auto& a : release.assets) {
        if(a.name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) ++zips;
        if(a.name.endsWith(QStringLiteral(".sha256"), Qt::CaseInsensitive)) { ++hashes; found = a; }
    }
    return zips == 1 && hashes == 1 ? found : Asset{};
}
bool fetch(const QString& url, QIODevice* output, qint64 limit, int timeoutMs, QString* error,
           const std::function<void(qint64, qint64)>& progress = {}) {
    const QUrl parsed(url);
    if(parsed.scheme() != QStringLiteral("https") || parsed.host().isEmpty()) { fail(error, QStringLiteral("Invalid HTTPS URL")); return false; }
    QNetworkAccessManager manager;
    QNetworkRequest request(parsed);
    request.setRawHeader("User-Agent", "XMB-Rewritten-Updater/0.2");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    bool timedOut = false, failedWrite = false; qint64 received = 0;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const QByteArray bytes = reply->readAll(); received += bytes.size();
        if(received > limit || output->write(bytes) != bytes.size()) { failedWrite = true; reply->abort(); }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 a, qint64 b) { if(progress) progress(a, b); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(qMax(1, timeoutMs)); loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(timedOut) fail(error, QStringLiteral("Request timed out"));
    else if(failedWrite) fail(error, QStringLiteral("Response too large or cannot write download"));
    else if(status == 404) fail(error, QStringLiteral("No release or asset found (HTTP 404)"));
    else if(reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
        fail(error, QStringLiteral("Network request failed: %1 (HTTP %2)").arg(reply->errorString()).arg(status));
    else return true;
    return false;
}
}

Release latestRelease(int timeoutMs) {
    Release release; QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if(!fetch(QStringLiteral("https://api.github.com/repos/ajgreenboy/XMB-Rewritten/releases/latest"), &buffer, 4*1024*1024, timeoutMs, &release.error)) return release;
    QJsonParseError parse; const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if(parse.error != QJsonParseError::NoError || !doc.isObject()) { release.error = QStringLiteral("Invalid release JSON"); return release; }
    const QJsonObject obj = doc.object(); release.tag = obj.value(QStringLiteral("tag_name")).toString();
    if(release.tag.isEmpty()) { release.error = QStringLiteral("Release has no tag"); return release; }
    release.name = obj.value(QStringLiteral("name")).toString(); release.body = obj.value(QStringLiteral("body")).toString();
    release.published = QDateTime::fromString(obj.value(QStringLiteral("published_at")).toString(), Qt::ISODate);
    for(const auto& value : obj.value(QStringLiteral("assets")).toArray()) {
        const auto a = value.toObject();
        release.assets.append({a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("browser_download_url")).toString(), qint64(a.value(QStringLiteral("size")).toDouble())});
    }
    return release;
}

bool newerVersion(const QString& current, const QString& releaseTag, QString* why) {
    auto parse = [](QString s, QVector<qulonglong>& parts) {
        if(s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) s.remove(0, 1);
        const auto list = s.split(QLatin1Char('.')); if(list.size() < 1 || list.size() > 3) return false;
        static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
        for(const auto& part : list) { bool ok = false; const auto n = part.toULongLong(&ok); if(!ok || !digits.match(part).hasMatch()) return false; parts.append(n); }
        while(parts.size() < 3) parts.append(0); return true;
    };
    QVector<qulonglong> a, b;
    if(!parse(current, a) || !parse(releaseTag, b)) { fail(why, QStringLiteral("Invalid numeric version")); return false; }
    if(why) why->clear();
    for(int i = 0; i < 3; ++i) if(a[i] != b[i]) return b[i] > a[i];
    return false;
}
Asset updateAsset(const Release& release) {
    Asset fallback;
    for(const auto& a : release.assets) if(a.name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        if(a.name.compare(QStringLiteral("re-ps3.zip"), Qt::CaseInsensitive) == 0) return a;
        if(fallback.name.isEmpty()) fallback = a; // GitHub returns assets in creation order.
        else fallback = a;
    }
    return fallback;
}
QString updateHashSource(const Release& release, const Asset& asset) {
    if(!bodyHash(release.body).isEmpty()) return QStringLiteral("body");
    if(!sidecar(release, asset).name.isEmpty()) return QStringLiteral("sidecar");
    return QStringLiteral("none");
}
bool verifyUpdateFile(const QString& path, const QString& expectedHex, QString* error) {
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if(!hex.match(expectedHex).hasMatch()) { fail(error, QStringLiteral("Invalid SHA-256")); return false; }
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) { fail(error, QStringLiteral("Cannot read downloaded ZIP")); return false; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file)) { fail(error, QStringLiteral("Cannot hash downloaded ZIP")); return false; }
    if(hash.result().toHex() != expectedHex.toLatin1().toLower()) { fail(error, QStringLiteral("SHA-256 mismatch")); return false; }
    if(error) error->clear(); return true;
}
QString downloadUpdate(const Release& release, QString* error, const std::function<void(qint64, qint64)>& progress) {
    const Asset asset = updateAsset(release); if(asset.name.isEmpty()) { fail(error, QStringLiteral("No ZIP asset")); return {}; }
    QString hash = bodyHash(release.body);
    if(hash.isEmpty()) {
        const Asset checksum = sidecar(release, asset);
        if(checksum.name.isEmpty()) { fail(error, QStringLiteral("Update cannot be verified: no SHA-256")); return {}; }
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if(!fetch(checksum.url, &buffer, 64*1024, 10000, error)) return {};
        static const QRegularExpression re(QStringLiteral("^\\s*([0-9a-fA-F]{64})(?:\\s|$)"));
        hash = re.match(QString::fromUtf8(bytes)).captured(1);
        if(hash.isEmpty()) { fail(error, QStringLiteral("Invalid SHA-256 sidecar")); return {}; }
    }
    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/xmb-update-XXXXXX.zip"));
    if(!file.open()) { fail(error, QStringLiteral("Cannot create temporary ZIP")); return {}; }
    if(!fetch(asset.url, &file, 2LL*1024*1024*1024, 10*60*1000, error, progress)) return {};
    file.flush(); const QString path = file.fileName(); file.close();
    if(!verifyUpdateFile(path, hash, error)) return {};
    file.setAutoRemove(false); return path;
}

bool applyZip(const QString& zipPath, const QString& installDir, QString* stagedPath, QString* error) {
    if(stagedPath) stagedPath->clear();
    const QFileInfo install(installDir);
    if(!install.isDir() || !QFileInfo(zipPath).isFile()) { fail(error, QStringLiteral("Install directory or ZIP does not exist")); return false; }
    const QString destination = install.absoluteFilePath() + QStringLiteral(".update.") + QUuid::createUuid().toString(QUuid::Id128);
    if(!QDir().mkdir(destination)) { fail(error, QStringLiteral("Cannot create staging folder")); return false; }
    // Validate every entry before extracting: reject traversal, rooted paths, links and oversized archives.
    const QString script = QStringLiteral(R"PS($ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=[IO.Compression.ZipFile]::OpenRead($env:XMB_UPDATE_ZIP)
try {
 $root=[IO.Path]::GetFullPath($env:XMB_UPDATE_DEST)+'\'; $total=0
 foreach($e in $z.Entries) {
  $p=[IO.Path]::GetFullPath((Join-Path $root $e.FullName))
  if(!$p.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or $e.FullName.Contains(':') -or (($e.ExternalAttributes -shr 16) -band 61440) -eq 40960) { throw 'Unsafe ZIP entry' }
  $total += $e.Length; if($total -gt 4294967296) { throw 'ZIP too large' }
 }
 [IO.Compression.ZipFileExtensions]::ExtractToDirectory($z,$env:XMB_UPDATE_DEST)
} finally { $z.Dispose() }
)PS");
    QProcess process; auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XMB_UPDATE_ZIP"), QFileInfo(zipPath).absoluteFilePath()); env.insert(QStringLiteral("XMB_UPDATE_DEST"), destination); process.setProcessEnvironment(env);
    process.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script});
    if(!process.waitForStarted(5000) || !process.waitForFinished(120000)) { process.kill(); process.waitForFinished(); fail(error, QStringLiteral("ZIP extraction timed out or could not start")); return false; }
    if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo::exists(destination + QStringLiteral("/re-ps3-native.exe"))) { fail(error, QStringLiteral("ZIP extraction failed or package is missing the executable; staging folder: %1").arg(destination)); return false; }
    if(stagedPath) *stagedPath = destination; if(error) error->clear(); return true;
}
} // namespace xmb
