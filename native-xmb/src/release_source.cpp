#include "release_source.h"
#include "real_debrid.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <limits>

namespace xmb {
namespace {
void fail(QString* error,const QString& message) { if(error)*error=message; }
QString safeName(QString text) {
    text.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")),QStringLiteral("_"));
    text=text.trimmed().left(100);
    while(text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(' ')))text.chop(1);
    if(text.isEmpty() || text==QStringLiteral(".") || text==QStringLiteral(".."))text=QStringLiteral("release");
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),QRegularExpression::CaseInsensitiveOption);
    if(reserved.match(text).hasMatch())text.prepend(QLatin1Char('_'));
    return text;
}
QString slugFor(const QString& name,const QUrl& url) {
    QString slug=name.toLower(); slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),QStringLiteral("-"));
    slug=slug.left(48); while(slug.endsWith(QLatin1Char('-')))slug.chop(1);
    if(slug.isEmpty())slug=QStringLiteral("source");
    return slug+QLatin1Char('-')+QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(),QCryptographicHash::Sha256).toHex().left(12));
}
// Same streaming QNAM + event-loop + timer pattern as firmware_fetcher::fetch.
// file: is allowed only for importing a local index, never for resolved downloads.
bool fetch(const QUrl& url,QIODevice* output,qint64 limit,int timeoutMs,QString* error,
           const ReleaseProgress& progress={},bool localIndex=false) {
    if(localIndex && url.isLocalFile()) {
        QFile file(url.toLocalFile());
        if(!file.open(QIODevice::ReadOnly)){fail(error,QStringLiteral("Cannot read local source: ")+file.errorString());return false;}
        if(file.size()>limit){fail(error,QStringLiteral("Source index too large"));return false;}
        while(!file.atEnd()) {
            const auto bytes=file.read(65536);
            if(file.error()!=QFileDevice::NoError || output->write(bytes)!=bytes.size()){fail(error,QStringLiteral("Cannot read or write source index"));return false;}
        }
        return true;
    }
    if(!url.isValid() || url.host().isEmpty() || (url.scheme()!=QStringLiteral("http") && url.scheme()!=QStringLiteral("https"))) {
        fail(error,QStringLiteral("Invalid HTTP(S) URL"));return false;
    }
    QNetworkAccessManager manager; QNetworkRequest request(url);
    request.setRawHeader("User-Agent","XMB-Release-Sources/0.2");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply=manager.get(request);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    bool timedOut=false,failedWrite=false; qint64 received=0;
    const auto consume=[&] {
        const auto bytes=reply->readAll();
        if(bytes.size()>limit-received || output->write(bytes)!=bytes.size()){failedWrite=true;reply->abort();return;}
        received+=bytes.size();
    };
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timedOut=true;reply->abort();});
    QObject::connect(reply,&QNetworkReply::readyRead,&loop,consume);
    QObject::connect(reply,&QNetworkReply::downloadProgress,&loop,[&](qint64 a,qint64 b){timer.start(timeoutMs);if(progress)progress(a,b);});
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    timer.start(timeoutMs);loop.exec();consume();
    const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(timedOut)fail(error,QStringLiteral("Download timed out"));
    else if(failedWrite)fail(error,QStringLiteral("Response too large or cannot write download"));
    else if(reply->error()!=QNetworkReply::NoError || status<200 || status>=300)
        fail(error,QStringLiteral("Download request failed (HTTP %1, network error %2)").arg(status).arg(int(reply->error())));
    else return true;
    return false;
}
void pause(int milliseconds) {
    QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();
}
}
ReleaseSources::ReleaseSources(const QString& dataDirectory,const Preferences& preferences)
    :dataDirectory_(dataDirectory),preferences_(preferences) {}
bool ReleaseSources::importSource(const QUrl& jsonUrl,QString* error) {
    if(error)error->clear();
    QByteArray bytes; QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
    if(!fetch(jsonUrl,&buffer,64LL*1024*1024,30000,error,{},true))return false;
    // Match Bun's replacement decoding of malformed UTF-8 before JSON parsing.
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(QString::fromUtf8(bytes).toUtf8(),&parse);
    if(parse.error!=QJsonParseError::NoError){fail(error,QStringLiteral("Invalid source JSON: ")+parse.errorString());return false;}
    QString parseError; const auto releases=parseReleaseIndex(document,&parseError);
    if(!parseError.isEmpty()){fail(error,parseError);return false;}
    QString name=document.object().value(QStringLiteral("name")).toString().trimmed();
    if(name.isEmpty())name=releases.isEmpty()?QStringLiteral("Source"):releases.first().source;
    const QString slug=slugFor(name,jsonUrl);
    const QDir directory(QDir(dataDirectory_).filePath(QStringLiteral("sources")));
    if(!QDir().mkpath(directory.path())){fail(error,QStringLiteral("Cannot create sources directory"));return false;}
    QJsonArray records;for(const auto& release:releases)records.append(releaseToJson(release));
    const QJsonObject root{{QStringLiteral("version"),1},{QStringLiteral("slug"),slug},{QStringLiteral("name"),name},
        {QStringLiteral("url"),jsonUrl.toString(QUrl::FullyEncoded)},{QStringLiteral("releases"),records}};
    QSaveFile file(directory.filePath(slug+QStringLiteral(".json")));file.setDirectWriteFallback(false);
    const auto saved=QJsonDocument(root).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly) || file.write(saved)!=saved.size() || !file.commit()) {
        fail(error,QStringLiteral("Cannot save source: ")+file.errorString());return false;
    }
    return true;
}
QVector<ReleaseSource> ReleaseSources::listSources(QString* error) const {
    if(error)error->clear();
    QVector<ReleaseSource> sources;
    const QDir directory(QDir(dataDirectory_).filePath(QStringLiteral("sources")));
    for(const auto& entry:directory.entryInfoList({QStringLiteral("*.json")},QDir::Files,QDir::Name)) {
        QFile file(entry.filePath());
        if(!file.open(QIODevice::ReadOnly) || file.size()>256LL*1024*1024){fail(error,QStringLiteral("Cannot read stored source: ")+entry.fileName());continue;}
        QJsonParseError parse; const auto document=QJsonDocument::fromJson(file.readAll(),&parse); const auto root=document.object();
        if(parse.error!=QJsonParseError::NoError || root.value(QStringLiteral("version")).toInt()!=1 || !root.value(QStringLiteral("releases")).isArray()) {
            fail(error,QStringLiteral("Invalid stored source: ")+entry.fileName());continue;
        }
        ReleaseSource source;source.slug=entry.completeBaseName();source.name=root.value(QStringLiteral("name")).toString();source.url=QUrl(root.value(QStringLiteral("url")).toString());
        for(const auto& record:root.value(QStringLiteral("releases")).toArray())if(record.isObject())source.releases.append(releaseFromJson(record.toObject()));
        sources.append(source);
    }
    return sources;
}
QString ReleaseSources::downloadRelease(const QString& source,const QString& title,QString* error,const ReleaseProgress& progress) const {
    if(error)error->clear();
    QString storeError;const auto sources=listSources(&storeError);
    for(const auto& stored:sources) {
        if(stored.slug!=source && stored.name!=source)continue;
        for(const auto& release:stored.releases) {
            if(release.title!=title)continue;
            QVector<Download> candidates=release.downloads;
            for(const auto& mirror:release.mirrors)candidates+=mirror.parts;
            QString direct,magnet;
            for(const auto& download:candidates) {
                if(direct.isEmpty() && download.kind==QStringLiteral("direct"))direct=download.uri;
                if(magnet.isEmpty() && download.kind==QStringLiteral("magnet"))magnet=download.uri;
            }
            RealDebrid debrid(preferences_);QUrl resolved;
            QString debridError;
            if(!direct.isEmpty()) {
                // The spec permits ordinary HTTP downloads when no manager token was configured.
                resolved=debrid.hasToken()?debrid.unrestrict(QUrl(direct),&debridError):QUrl(direct);
            } else if(!magnet.isEmpty()) {
                const QString id=debrid.addMagnet(magnet,&debridError);
                if(id.isEmpty()){fail(error,debridError);return {};}
                QElapsedTimer polling;polling.start();bool selected=false;
                while(polling.elapsed()<10*60*1000) {
                    const auto info=debrid.torrentInfo(id,&debridError);
                    if(!debridError.isEmpty())break;
                    if(info.status==QStringLiteral("waiting_files_selection") && !selected) {
                        if(!debrid.selectFiles(id,QStringLiteral("all"),&debridError))break;
                        selected=true;
                    } else if(info.status==QStringLiteral("downloaded")) {
                        if(info.links.isEmpty())debridError=QStringLiteral("Real-Debrid torrent has no download links");
                        else resolved=debrid.unrestrict(QUrl(info.links.first()),&debridError);
                        break;
                    } else if(info.status==QStringLiteral("error") || info.status==QStringLiteral("magnet_error") || info.status==QStringLiteral("virus") || info.status==QStringLiteral("dead")) {
                        debridError=QStringLiteral("Real-Debrid torrent failed: ")+info.status;break;
                    }
                    if(progress)progress(0,-1);pause(2000);
                }
                if(resolved.isEmpty() && debridError.isEmpty())debridError=QStringLiteral("Real-Debrid torrent timed out");
            } else {
                fail(error,QStringLiteral("No direct download or supported magnet for this release"));return {};
            }
            if(!debridError.isEmpty()){fail(error,debridError);return {};}
            QString extension=QFileInfo(safeName(resolved.fileName())).suffix();
            if(extension.isEmpty())extension=QStringLiteral("bin");
            const QDir directory(QDir(dataDirectory_).filePath(QStringLiteral("downloads/")+safeName(stored.slug)));
            if(!QDir().mkpath(directory.path())){fail(error,QStringLiteral("Cannot create download directory"));return {};}
            const QString path=directory.filePath(safeName(title)+QLatin1Char('.')+extension);
            QSaveFile file(path);file.setDirectWriteFallback(false);
            if(!file.open(QIODevice::WriteOnly)){fail(error,QStringLiteral("Cannot open download: ")+file.errorString());return {};}
            if(!fetch(resolved,&file,std::numeric_limits<qint64>::max(),60000,error,progress))return {};
            if(!file.commit()){fail(error,QStringLiteral("Cannot save download: ")+file.errorString());return {};}
            return path;
        }
    }
    fail(error,storeError.isEmpty()?QStringLiteral("Release not found in imported sources"):storeError);return {};
}
}
