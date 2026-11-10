#include "firmware_fetcher.h"
#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QVersionNumber>

namespace xmb {
namespace {
void fail(QString* error, const QString& message) { if(error) *error = message; }
bool fetch(const QUrl& url, QIODevice* output, qint64 limit, int timeoutMs, QString* error,
           const std::function<void(qint64, qint64)>& progress = {}) {
    if(url.scheme() != QStringLiteral("https") || url.host().isEmpty()) { fail(error, QStringLiteral("Invalid HTTPS URL")); return false; }
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "XMB-Firmware-Fetcher/0.2");
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
    timer.start(timeoutMs); loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(timedOut) fail(error, QStringLiteral("Request timed out"));
    else if(failedWrite) fail(error, QStringLiteral("Response too large or cannot write download"));
    else if(reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
        fail(error, QStringLiteral("Network request failed: %1 (HTTP %2)").arg(reply->errorString()).arg(status));
    else return true;
    return false;
}
}

FirmwareRelease latestFirmware(const QString& region, QString* error) {
    if(error) error->clear();
    static const QRegularExpression regionPattern(QStringLiteral("^[a-z]{2}$"));
    if(!regionPattern.match(region).hasMatch()) { fail(error, QStringLiteral("Invalid firmware region")); return {}; }
    const QUrl listUrl(QStringLiteral("https://dus01.ps3.update.playstation.net/update/ps3/list/%1/ps3-updatelist.txt").arg(region));
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if(!fetch(listUrl, &buffer, 256*1024, 15000, error)) return {};
    FirmwareRelease best; QVersionNumber bestVersion;
    for(const QByteArray& line : bytes.split('\n')) {
        QString version, cdn;
        for(const QByteArray& field : line.split(';')) {
            const int at = field.indexOf('='); if(at < 0) continue;
            const QByteArray key = field.left(at).trimmed();
            if(key == "SystemSoftwareVersion") version = QString::fromLatin1(field.mid(at+1).trimmed());
            if(key == "CDN") cdn = QString::fromLatin1(field.mid(at+1).trimmed());
        }
        static const QRegularExpression versionPattern(QStringLiteral("^[0-9]+(?:\\.[0-9]+)+$"));
        if(!versionPattern.match(version).hasMatch()) continue;
        while(version.endsWith(QLatin1Char('0')) && version.size() - version.lastIndexOf(QLatin1Char('.')) - 1 > 2) version.chop(1);
        QUrl url = listUrl.resolved(QUrl(cdn));
        if(url.host() != QStringLiteral("dus01.ps3.update.playstation.net") ||
           (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
           !url.path().endsWith(QStringLiteral("/PS3UPDAT.PUP"), Qt::CaseInsensitive)) continue;
        url.setScheme(QStringLiteral("https"));
        const QVersionNumber parsed = QVersionNumber::fromString(version).normalized();
        if(best.version.isEmpty() || QVersionNumber::compare(parsed, bestVersion) > 0) {
            bestVersion = parsed; best = {parsed.toString(), url.toString()};
        }
    }
    if(best.version.isEmpty()) fail(error, QStringLiteral("No full firmware release found in Sony list"));
    return best;
}

bool downloadPup(const QString& url, QString* path, QString* error, const std::function<void(qint64, qint64)>& progress) {
    if(path) path->clear(); if(error) error->clear();
    if(!path) { fail(error, QStringLiteral("Missing output path")); return false; }
    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/xmb-firmware-XXXXXX.PUP"));
    if(!file.open()) { fail(error, QStringLiteral("Cannot create temporary PUP")); return false; }
    if(!fetch(QUrl(url), &file, 512LL*1024*1024, 10*60*1000, error, progress)) return false;
    if(!file.flush()) { fail(error, QStringLiteral("Cannot flush temporary PUP")); return false; }
    *path = file.fileName(); file.close(); file.setAutoRemove(false); return true;
}
QString expectedFirmwareVersion() { return QStringLiteral("4.93"); }
} // namespace xmb
