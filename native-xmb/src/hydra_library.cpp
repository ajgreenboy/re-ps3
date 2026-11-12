#include "hydra_library.h"
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <algorithm>

namespace xmb {
namespace {
bool fetchJson(const QUrl& url, QJsonObject* out, QString* error) {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent","XMB-Release-Sources/0.2");
    QNetworkReply* reply=manager.get(request);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    bool timedOut=false;
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timedOut=true;reply->abort();});
    QObject::connect(reply,&QNetworkReply::downloadProgress,&loop,[&](qint64 received,qint64){if(received>16*1024*1024){timedOut=true;reply->abort();}}); // a source cannot stream an unbounded body into memory
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    timer.start(30000); loop.exec();
    if(timedOut || reply->error()!=QNetworkReply::NoError) {
        const QString message=reply->errorString(); reply->deleteLater();
        if(error)*error=QStringLiteral("Hydra library: %1").arg(message); return false;
    }
    const QJsonObject root=QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    if(out)*out=root; return true;
}
// The Hydra Cloud catalogue endpoints (catalogue/hot, catalogue/featured) answer a bare JSON array.
bool fetchJsonArray(const QUrl& url, QJsonArray* out, QString* error) {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent","XMB-Release-Sources/0.2");
    QNetworkReply* reply=manager.get(request);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    bool timedOut=false;
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timedOut=true;reply->abort();});
    QObject::connect(reply,&QNetworkReply::downloadProgress,&loop,[&](qint64 received,qint64){if(received>16*1024*1024){timedOut=true;reply->abort();}}); // a source cannot stream an unbounded body into memory
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    timer.start(30000); loop.exec();
    if(timedOut || reply->error()!=QNetworkReply::NoError) {
        const QString message=reply->errorString(); reply->deleteLater();
        if(error)*error=QStringLiteral("Hydra catalogue: %1").arg(message); return false;
    }
    const QJsonDocument document=QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if(!document.isArray()) { if(error)*error=QStringLiteral("Hydra catalogue returned an unexpected response"); return false; }
    if(out)*out=document.array(); return true;
}
bool isPreinstalled(const HydraSource& source) {
    const QString desc=source.description.toLower();
    const QString title=source.title.toLower();
    bool direct=false,torrent=false;
    for(const auto& option:source.topDownloadOption) {
        const QString name=option.toLower();
        if(name==QStringLiteral("torrent"))torrent=true;
        else if(!name.isEmpty())direct=true;
    }
    return desc.contains(QStringLiteral("pre-installed")) || desc.contains(QStringLiteral("preinstalled")) ||
           title.contains(QStringLiteral("portable")) || (direct && !torrent);
}
bool isPreinstalledFor(const QString& sourceName,const QVector<HydraSource>& sources) {
    for(const auto& source:sources)if(source.title==sourceName)return isPreinstalled(source);
    return false;
}
}
bool HydraLibrary::listSources(QVector<HydraSource>* out, QString* error) const {
    if(out)out->clear();
    QVector<HydraSource> all;
    int page=1;
    for(;;) {
        QJsonObject root;
        if(!fetchJson(QUrl(QStringLiteral("https://api.hydralibrary.com/sources?page=%1&limit=100").arg(page)),&root,error))return false;
        const QJsonArray sources=root.value(QStringLiteral("sources")).toArray();
        for(const auto& value:sources) {
            const QJsonObject object=value.toObject();
            HydraSource source;
            source.title=object.value(QStringLiteral("title")).toString();
            source.url=QUrl(object.value(QStringLiteral("url")).toString());
            source.gamesCount=object.value(QStringLiteral("gamesCount")).toInt();
            source.description=object.value(QStringLiteral("description")).toString();
            source.installs=object.value(QStringLiteral("stats")).toObject().value(QStringLiteral("installs")).toInt();
            for(const auto& status:object.value(QStringLiteral("status")).toArray())source.status.append(status.toString());
            for(const auto& option:object.value(QStringLiteral("topDownloadOption")).toArray())
                source.topDownloadOption.append(option.toObject().value(QStringLiteral("name")).toString());
            all.append(source);
        }
        const int total=root.value(QStringLiteral("total")).toInt();
        const int limit=root.value(QStringLiteral("limit")).toInt();
        if(limit<=0 || all.size()>=total || sources.isEmpty())break;
        if(++page>100)break; // the server controls total; never page forever
    }
    if(all.isEmpty()) { if(error)*error=QStringLiteral("Hydra library returned no sources"); return false; }
    if(out)*out=all; return true;
}
bool HydraLibrary::preinstalledSources(QStringList* out, QString* error) const {
    QVector<HydraSource> sources;
    if(!listSources(&sources,error))return false;
    QStringList names;
    for(const auto& source:sources)if(isPreinstalled(source))names.append(source.title);
    if(out)*out=names; return true;
}
bool HydraLibrary::searchGames(const QString& query, QVector<GameResult>* out, QString* error) const {
    if(out)out->clear();
    QVector<HydraSource> sources;
    if(!listSources(&sources,error))return false;
    QHash<QString,int> installs;
    for(const auto& source:sources)installs.insert(source.title,source.installs);
    QVector<GameResult> results;
    QHash<QString,int> byName;
    for(int page=1;page<=5;++page) {
        QJsonObject root;
        const QUrl url(QStringLiteral("https://api.hydralibrary.com/games?q=%1&limit=100&page=%2")
            .arg(QString::fromUtf8(QUrl::toPercentEncoding(query))).arg(page));
        if(!fetchJson(url,&root,error))return false;
        const QJsonArray games=root.value(QStringLiteral("games")).toArray();
        if(games.isEmpty())break;
        for(const auto& value:games) {
            const QJsonObject game=value.toObject();
            const QString sourceName=game.value(QStringLiteral("source")).toString();
            if(!isPreinstalledFor(sourceName,sources))continue;
            const QString name=game.value(QStringLiteral("name")).toString();
            const QString key=name.trimmed().toLower();
            if(key.isEmpty())continue;
            GameSourceOption option;
            option.source=sourceName;
            option.sourceUrl=game.value(QStringLiteral("sourceUrl")).toString();
            option.fileSize=game.value(QStringLiteral("fileSize")).toString();
            option.sourceStatus=game.value(QStringLiteral("sourceStatus")).toString();
            const auto existing=byName.find(key);
            if(existing==byName.end()) {
                GameResult result;
                result.name=name;
                result.title=game.value(QStringLiteral("title")).toString();
                result.originalTitle=game.value(QStringLiteral("originalTitle")).toString();
                result.options.append(option);
                byName.insert(key,results.size());
                results.append(result);
            } else {
                results[existing.value()].options.append(option);
            }
        }
        const int filtered=root.value(QStringLiteral("filteredCount")).toInt();
        if(results.size()>=filtered || results.size()>=500)break;
    }
    // Popularity: sort by the most-installed source each game is available from, descending.
    std::sort(results.begin(),results.end(),[&installs](const GameResult& a,const GameResult& b){
        int pa=0,pb=0;
        for(const auto& o:a.options)pa=std::max(pa,installs.value(o.source,0));
        for(const auto& o:b.options)pb=std::max(pb,installs.value(o.source,0));
        return pa>pb;
    });
    if(out)*out=results; return true;
}
bool HydraLibrary::popularGames(QVector<GameResult>* out, QString* error) const {
    if(out)out->clear();
    QVector<HydraSource> sources;
    if(!listSources(&sources,error))return false;

    // The Hydra Cloud "hot" catalogue is the real popularity ranking. take=40 keeps the worker's
    // round-trips bounded (one search per title).
    QJsonArray hot;
    if(!fetchJsonArray(QUrl(QStringLiteral("https://api.hydralauncher.gg/catalogue/hot?take=40&skip=0")),&hot,error))
        return false;

    QVector<GameResult> results;
    QHash<QString,int> byName;
    for(const auto& value:hot) {
        const QJsonObject entry=value.toObject();
        const QString title=entry.value(QStringLiteral("title")).toString().trimmed();
        if(title.isEmpty())continue;
        const QString cover=entry.value(QStringLiteral("libraryImageUrl")).toString();

        // Match this popular title against the download library (single page; the query is specific).
        QJsonObject root;
        const QString searchUrl=QStringLiteral("https://api.hydralibrary.com/games?q=%1&limit=100&page=1")
            .arg(QString::fromUtf8(QUrl::toPercentEncoding(title)));
        if(!fetchJson(QUrl(searchUrl),&root,nullptr))continue;

        GameResult result;
        result.name=title;
        result.title=title;
        result.coverUrl=cover;
        QHash<QString,bool> seenSource;   // one download option per source
        for(const auto& gv:root.value(QStringLiteral("games")).toArray()) {
            const QJsonObject game=gv.toObject();
            const QString sourceName=game.value(QStringLiteral("source")).toString();
            if(!isPreinstalledFor(sourceName,sources))continue;
            if(seenSource.contains(sourceName))continue;
            seenSource.insert(sourceName,true);
            GameSourceOption option;
            option.source=sourceName;
            option.sourceUrl=game.value(QStringLiteral("sourceUrl")).toString();
            option.fileSize=game.value(QStringLiteral("fileSize")).toString();
            option.sourceStatus=game.value(QStringLiteral("sourceStatus")).toString();
            if(result.originalTitle.isEmpty())
                result.originalTitle=game.value(QStringLiteral("originalTitle")).toString();
            result.options.append(option);
        }
        if(result.options.isEmpty())continue;   // not downloadable: drop it

        const QString key=title.toLower();
        const auto existing=byName.find(key);
        if(existing==byName.end()) {
            byName.insert(key,results.size());
            results.append(result);
        } else {
            auto& dst=results[existing.value()];
            if(dst.coverUrl.isEmpty())dst.coverUrl=cover;
            QHash<QString,bool> have;
            for(const auto& o:dst.options)have.insert(o.source,true);
            for(const auto& o:result.options)if(!have.contains(o.source))dst.options.append(o);
        }
    }
    if(out)*out=results;
    return !results.isEmpty();
}
} // namespace xmb
