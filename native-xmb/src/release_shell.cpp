#include "shell.h"
#include "rawg_artwork.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace xmb {
namespace {
QString releaseId(const QString& source,const QString& title) {
    return QStringLiteral("release:item:")+source+QLatin1Char(':')+
        QString::fromLatin1(QCryptographicHash::hash(title.toUtf8(),QCryptographicHash::Sha256).toHex());
}
Entry releaseRow(const QString& id,const QString& title,Action action,const QString& value={}) {
    Entry entry;
    entry.id=id;entry.title=title;entry.action=action;entry.value=value;
    entry.container=QStringLiteral("explore_plugin_full");
    entry.texture=action==Action::ReleaseDownload?QStringLiteral("game_tex_default_ps3"):QStringLiteral("item_tex_plain_folder");
    entry.source=QStringLiteral("native");
    return entry;
}
QString coverUrl(const IndexedRelease& release) {
    for(const char* key:{"artwork","thumbnail","cover","image","poster","cover_url","image_url"}) {
        const auto value=release.raw.value(QLatin1String(key));
        const QString url=value.isObject()?value.toObject().value(QStringLiteral("url")).toString():value.toString();
        if(!url.isEmpty())return url;
    }
    return {};
}
QImage readCover(const QByteArray& bytes) {
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size=reader.size();
    if(size.isEmpty() || size.width()>8192 || size.height()>8192)return {};
    reader.setScaledSize(size.scaled(512,512,Qt::KeepAspectRatio));
    return reader.read();
}
QImage fetchCover(const QUrl& url,const QString& path) {
    QImage cached(path);
    if(!cached.isNull())return cached;
    if(url.scheme()!=QLatin1String("https") && url.scheme()!=QLatin1String("http"))return {};
    QNetworkAccessManager network;
    QNetworkRequest request(url);request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply=network.get(request);
    QEventLoop loop;
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    QByteArray bytes;
    QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&]{
        bytes+=reply->readAll();
        if(bytes.size()>8*1024*1024)reply->abort();
    });
    loop.exec();
    if(reply->error()!=QNetworkReply::NoError)return {};
    bytes+=reply->readAll();
    if(bytes.size()>8*1024*1024)return {};
    QImage image=readCover(bytes);
    if(!image.isNull()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if(file.open(QIODevice::WriteOnly) && image.save(&file,"PNG"))file.commit();
    }
    return image;
}
}

void Shell::refreshReleaseSources(const QUrl& importUrl) {
    const bool importing=!importUrl.isEmpty();
    if(importing && busy())return;
    struct Result { QVector<ReleaseSource> sources; QHash<QString,QString> paths; QString error; };
    auto* watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,importing]{
        const auto result=watcher->result();watcher->deleteLater();
        releaseSources_=result.sources;releaseLocalPaths_=result.paths;
        rebuildReleaseSources();
        if(importing){busy_=false;status_.clear();busyProgress_=-1.;busySpinnerPhase_=-1.;}
        if(!result.error.isEmpty())showMessage(tr("Release Sources"),result.error,MessageKind::Page);
        else if(importing)showMessage(tr("Import Source"),tr("Source imported. Open its folder in Game to browse releases."),MessageKind::Page);
        emit changed();
    });
    const QString data=dataDirectory_;
    watcher->setFuture(QtConcurrent::run([data,importUrl,importing]{
        Preferences preferences(data);ReleaseSources store(data,preferences);Result result;
        if(importing)store.importSource(importUrl,&result.error);
        QString listError;result.sources=store.listSources(&listError);
        if(result.error.isEmpty())result.error=listError;
        for(const auto& source:result.sources)for(const auto& release:source.releases)
            result.paths.insert(releaseId(source.slug,release.title),store.downloadedPath(source.slug,release.title));
        return result;
    }));
    if(importing) {
        clearMessage();busy_=true;status_=tr("Importing source...");
        busyProgress_=-1.;busySpinnerPhase_=0.;busyClock_.start();emit changed();
    }
}
void Shell::rebuildReleaseSources() {
    for(auto& category:categories_)if(category.id==QLatin1String("game")) {
        category.entries.erase(std::remove_if(category.entries.begin(),category.entries.end(),[](const Entry& entry){
            return entry.action==Action::ReleaseFolder;
        }),category.entries.end());
        for(const auto& source:releaseSources_) {
            Entry entry=releaseRow(QStringLiteral("release:source:")+source.slug,source.name,Action::ReleaseFolder,source.slug);
            entry.detail=tr("%1 releases").arg(source.releases.size());category.entries.append(entry);
        }
    }
}
void Shell::openReleaseSource(const QString& slug) {
    if(pages_.size()>=32)return;
    for(const auto& source:releaseSources_)if(source.slug==slug) {
        Page page;page.title=source.name;
        for(const auto& release:source.releases) {
            Entry entry=releaseRow(releaseId(slug,release.title),release.title,Action::ReleaseDownload,slug);
            entry.setting=release.title;entry.artwork=releaseArt_.value(entry.id);entry.iconSized=true;
            entry.rawgArtworkVisible=releaseRawgArt_.contains(entry.id);
            entry.artworkUrl=coverUrl(release);
            entry.detail=release.sizeLabel;
            const QString local=releaseLocalPaths_.value(entry.id);
            entry.fieldValue=local.isEmpty()?tr("Download"):tr("Downloaded");
            if(!local.isEmpty())entry.detail=QDir::toNativeSeparators(local);
            page.entries.append(entry);
        }
        pages_.push_back(std::move(page));
        if(source.releases.isEmpty())showMessage(tr("There are no content items."),MessageKind::Inline);
        for(int i=0;i<source.releases.size();++i)requestReleaseArt(pages_.last().entries[i],source.releases[i],source.url);
        emit changed();return;
    }
}
void Shell::requestReleaseArt(const Entry& entry,const IndexedRelease& release,const QUrl& sourceUrl) {
    if(!entry.artwork.isNull())return;
    if(!releaseArtwork_) {
        releaseArtwork_=std::make_unique<RawgArtwork>(QDir(dataDirectory_).filePath(QStringLiteral("cache/release-art")),this);
        connect(releaseArtwork_.get(),&RawgArtwork::ready,this,[this](const QString& id,const QString&,const QString&,const QImage& image){
            releaseArt_.insert(id,image);
            releaseRawgArt_.insert(id);
            for(auto& page:pages_)for(auto& row:page.entries)if(row.id==id){row.artwork=image;row.rawgArtworkVisible=true;}
            emit changed();
        });
    }
    const QString imageUrl=coverUrl(release);
    const auto fallback=[this]{
        QVector<QPair<QString,QString>> targets;
        for(const auto& page:pages_)for(const auto& row:page.entries)
            if(row.action==Action::ReleaseDownload && row.artwork.isNull() && row.artworkUrl.isEmpty() && !releaseArtRequested_.contains(row.id))
                targets.append(qMakePair(row.id,row.title));
        releaseArtwork_->refresh(targets);
    };
    if(imageUrl.isEmpty()){fallback();return;}
    if(releaseArtRequested_.contains(entry.id))return;
    releaseArtRequested_.insert(entry.id);
    const QString cache=QDir(dataDirectory_).filePath(QStringLiteral("cache/release-covers/")+
        QString::fromLatin1(QCryptographicHash::hash((entry.id+imageUrl).toUtf8(),QCryptographicHash::Sha256).toHex())+QStringLiteral(".png"));
    auto* watcher=new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,id=entry.id,fallback]{
        const QImage image=watcher->result();watcher->deleteLater();releaseArtRequested_.remove(id);
        if(image.isNull()) {
            for(auto& page:pages_)for(auto& row:page.entries)if(row.id==id)row.artworkUrl.clear();
            fallback();return;
        }
        releaseArt_.insert(id,image);
        for(auto& page:pages_)for(auto& row:page.entries)if(row.id==id)row.artwork=image;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([url=sourceUrl.resolved(QUrl(imageUrl)),cache]{return fetchCover(url,cache);}));
}
void Shell::activateRelease(const Entry& entry) {
    const QString local=releaseLocalPaths_.value(entry.id);
    if(!local.isEmpty() && QFileInfo(local).isFile()) {
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(local)))showMessage(tr("Download"),tr("Could not open the downloaded file."),MessageKind::Page);
        emit changed();return;
    }
    busy_=true;status_=tr("Downloading %1...").arg(entry.title);
    busyProgress_=-1.;busySpinnerPhase_=0.;busyClock_.start();
    auto* watcher=new QFutureWatcher<QPair<QString,QString>>(this);
    connect(watcher,&QFutureWatcher<QPair<QString,QString>>::finished,this,[this,watcher,entry]{
        const auto result=watcher->result();watcher->deleteLater();
        busy_=false;status_.clear();busyProgress_=-1.;busySpinnerPhase_=-1.;
        if(result.first.isEmpty())showMessage(tr("Download"),result.second,MessageKind::Page);
        else {
            releaseLocalPaths_.insert(entry.id,result.first);
            for(auto& page:pages_)for(auto& row:page.entries)if(row.id==entry.id) {
                row.fieldValue=tr("Downloaded");row.detail=QDir::toNativeSeparators(result.first);
            }
            showMessage(tr("Download Complete"),tr("%1 is ready. Select it again to open the local file.").arg(entry.title),MessageKind::Page);
        }
        emit changed();
    });
    const QString data=dataDirectory_;
    connect(watcher,&QFutureWatcher<QPair<QString,QString>>::progressValueChanged,this,[this](int value){
        busyProgress_=value>0?double(value-1)/1000.:-1.;
        emit changed();
    });
    // Worker-local preferences and QPromise keep both store access and progress delivery thread-safe.
    watcher->setFuture(QtConcurrent::run([data,entry](QPromise<QPair<QString,QString>>& promise){
        promise.setProgressRange(0,1001);
        Preferences preferences(data);ReleaseSources store(data,preferences);QString error;
        const QString path=store.downloadRelease(entry.value,entry.setting,&error,[&promise](qint64 received,qint64 total){
            promise.setProgressValue(total>0?1+int(std::clamp(double(received)/double(total),0.,1.)*1000):0);
        });
        promise.addResult(qMakePair(path,error));
    }));
    emit changed();
}
void Shell::openDownloadManager() {
    if(pages_.size()>=32)return;
    Page page;page.title=tr("Download Manager");
    Entry entry=releaseRow(QStringLiteral("debrid:token"),tr("Real-Debrid API Key"),Action::DebridToken);
    entry.fieldValue=preferences_.value(QStringLiteral("debrid/realDebridToken")).toString().isEmpty()?tr("Not set"):QStringLiteral("********");
    entry.detail=tr("Enter a replacement API key, or clear the field to remove it. Stored for this profile; never shown in plain text.");
    page.entries.append(entry);pages_.push_back(std::move(page));emit changed();
}
void Shell::editDebridToken() {
    // An empty, masked editor never exposes the existing token to the view or its capture diagnostics.
    emit requestJellyfinFieldEdit(QStringLiteral("debrid:token"),tr("Real-Debrid API Key"),{},true);
}
void Shell::saveDebridToken(const QString& token) {
    if(!preferences_.setValue(QStringLiteral("debrid/realDebridToken"),token.trimmed()))return;
    for(auto& page:pages_)for(auto& row:page.entries)if(row.id==QStringLiteral("debrid:token"))
        row.fieldValue=token.trimmed().isEmpty()?tr("Not set"):QStringLiteral("********");
    setupRefresh();emit changed();
}
} // namespace xmb
