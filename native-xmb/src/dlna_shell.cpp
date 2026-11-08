// Search for Media Servers (category_video/music/photo.xml #seg_dlna_scan; audit/DLNA.md).
//
//   Search for Media Servers   SSDP discovery (dlna_client.h); a page of the servers found, or dlna_plugin's own
//                              msg_error_mediaserver_notfound when none answers
//   a server / a folder        ContentDirectory Browse; folders, then the column's kind of item (audio for Music,
//                              video for Video, images for Photo)
//   an audio or video item     an ordinary File row whose value is the server's http URL, so the existing queue,
//                              player, resume and controls play it
//   a photo                    downloaded (bounded) and shown in the photo viewer
//
// Servers whose SSDP never reaches this PC (a Docker bridge, another subnet) can be listed in <data>/dlna.json:
// {"locations": ["http://192.168.1.10:8200/rootDesc.xml"], "unicast": ["192.168.1.10"]}. The client applies the
// same address rules to them as to an SSDP answer (private, link-local or loopback only).
#include "shell.h"
#include "assets.h"
#include "media_controls.h"
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QImageReader>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace xmb {
namespace {
constexpr qint64 MaxPhotoBytes = 48LL * 1024 * 1024;
Entry dlnaRow(QString id,QString title,Action action,QString value={},QString detail={}) {
    Entry e;e.id=std::move(id);e.title=std::move(title);e.action=action;e.value=std::move(value);e.detail=std::move(detail);
    e.container=QStringLiteral("explore_plugin_full");e.texture=QStringLiteral("item_tex_plain_folder");e.source=QStringLiteral("native");
    return e;
}
// The column's own kind of item, by UPnP class.
bool wanted(const QString& column,const QString& upnpClass) {
    if(column==QLatin1String("music"))return upnpClass.startsWith(QLatin1String("object.item.audioItem"));
    if(column==QLatin1String("video"))return upnpClass.startsWith(QLatin1String("object.item.videoItem"));
    if(column==QLatin1String("photo"))return upnpClass.startsWith(QLatin1String("object.item.imageItem"));
    return false;
}
}

void Shell::initDlna() {
    dlnaNetwork_=new QNetworkAccessManager(this);
    QVector<QUrl> locations;QVector<QHostAddress> unicast;
    QFile config(QDir(dataDirectory_).filePath(QStringLiteral("dlna.json")));
    if(config.size()<=64*1024 && config.open(QIODevice::ReadOnly)) {
        const auto object=QJsonDocument::fromJson(config.readAll()).object();
        for(const auto& v:object.value(QStringLiteral("locations")).toArray())if(locations.size()<16)locations<<QUrl(v.toString());
        for(const auto& v:object.value(QStringLiteral("unicast")).toArray()){QHostAddress a(v.toString());if(!a.isNull() && unicast.size()<16)unicast<<a;}
    }
    // Tests point the scan at a local mock without a file.
    for(const auto& v:qEnvironmentVariable("XMB_DLNA_LOCATIONS").split(',',Qt::SkipEmptyParts))if(locations.size()<16)locations<<QUrl(v.trimmed());
    if(!locations.isEmpty() || !unicast.isEmpty())
        qInfo().noquote()<<"dlna:"<<locations.size()<<"known location(s),"<<unicast.size()<<"unicast host(s) configured";
    dlna_.setKnownLocations(locations);dlna_.setUnicastTargets(unicast);

    connect(&dlna_,&DlnaClient::serversReady,this,[this](const QVector<DlnaServer>& servers){
        if(!dlnaScanning_)return;
        dlnaScanning_=false;loading_=false;status_.clear();
        dlnaServers_=servers;
        qInfo().noquote()<<"dlna: scan found"<<servers.size()<<"media server(s)";
        if(servers.isEmpty()) {
            showMessage(assets_.text("dlna_plugin","msg_error_mediaserver_notfound","No media servers were found."),MessageKind::Page);
            emit changed();return;
        }
        if(pages_.size()>=32){emit changed();return;}
        Page page;page.title=assets_.text("explore_plugin_full","msg_mediaserver","Media Server");page.dlna=QStringLiteral("servers");
        for(int i=0;i<servers.size() && i<64;++i)
            page.entries.push_back(dlnaRow(QStringLiteral("dlna:server:%1").arg(i),servers[i].name,Action::DlnaServer,QString::number(i),
                                           QUrl(servers[i].controlUrl).host()));
        if(servers.size()>64)showMessage(assets_.text("explore_plugin_full","msg_cannot_display_all_mediaserver",
                                                       "There are too many media servers.\nSome media servers will not be displayed."),MessageKind::Page);
        pages_.push_back(std::move(page));
        emit changed();
    });
    connect(&dlna_,&DlnaClient::browseReady,this,[this](quint64 request,const QVector<DlnaItem>& items,int total){
        if(request!=dlnaRequest_)return;
        dlnaRequest_=0;loading_=false;status_.clear();
        if(pages_.isEmpty() || pages_.last().dlna!=QLatin1String("folder"))return;
        auto& page=pages_.last();
        page.entries.clear();
        int skipped=0;
        for(const auto& item:items) {
            if(item.container) {
                auto row=dlnaRow(QStringLiteral("dlna:folder:")+item.id,item.title,Action::DlnaFolder,item.id,
                                 item.childCount>=0?QObject::tr("%1 items").arg(item.childCount):QString());
                page.entries.push_back(std::move(row));
                continue;
            }
            if(!wanted(dlnaKind_,item.upnpClass) || item.url.isEmpty()){++skipped;continue;}
            QStringList detail;
            if(!item.artist.isEmpty())detail<<item.artist;
            if(!item.album.isEmpty())detail<<item.album;
            if(item.durationMs>0)detail<<MediaControls::formatTime(item.durationMs);
            if(dlnaKind_==QLatin1String("photo")) {
                auto row=dlnaRow(QStringLiteral("dlna:item:")+item.id,item.title,Action::DlnaPhoto,item.url,detail.join(QStringLiteral(" - ")));
                row.texture=QStringLiteral("tex_photo");
                page.entries.push_back(std::move(row));
            } else {
                auto row=dlnaRow(QStringLiteral("dlna:item:")+item.id,item.title,Action::File,item.url,detail.join(QStringLiteral(" - ")));
                row.texture=dlnaKind_==QLatin1String("music")?QStringLiteral("tex_music"):QStringLiteral("tex_video");
                page.entries.push_back(std::move(row));
            }
        }
        qInfo().noquote()<<"dlna:"<<page.title<<"with"<<page.entries.size()<<"row(s)"<<(skipped?QStringLiteral("(%1 of another kind)").arg(skipped):QString())
                         <<"of"<<total;
        if(page.entries.isEmpty()) {
            pages_.removeLast();
            showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
        }
        emit changed();
    });
    connect(&dlna_,&DlnaClient::failed,this,[this](quint64 request,const QString& message){
        if(request!=dlnaRequest_)return;
        dlnaRequest_=0;loading_=false;status_.clear();
        if(!pages_.isEmpty() && pages_.last().dlna==QLatin1String("folder") && pages_.last().entries.isEmpty())pages_.removeLast();
        showMessage(assets_.text("explore_plugin_full","msg_error_server_transaction_failure","An error occurred during communication with the server.")
                    +QStringLiteral("\n\n")+message,MessageKind::Page);
        emit changed();
    });
}

void Shell::openDlnaFolder(int server,const QString& objectId,const QString& title) {
    if(server<0 || server>=dlnaServers_.size())return;
    if(pages_.size()>=32){showMessage(QObject::tr("The maximum folder depth has been reached."),MessageKind::Page);return;}
    Page page;page.title=title;page.dlna=QStringLiteral("folder");page.dlnaServer=server;
    pages_.push_back(std::move(page));
    loading_=true;status_=QObject::tr("Reading from the media server...");
    dlnaRequest_=dlna_.browse(dlnaServers_[server],objectId,0,500);
}

void Shell::openDlnaPhoto(const Entry& entry) {
    const QUrl url(entry.value);
    if(url.scheme()!=QLatin1String("http") && url.scheme()!=QLatin1String("https"))return;
    const auto request=++dlnaPhotoRequest_;
    ++photoRequest_;loading_=true;photoRotation_=0;photo_={};emit mediaFrame({});
    mediaOpen_=true;mediaTitle_=entry.title;jellyfinQueue_=false;
    QNetworkRequest get(url);get.setTransferTimeout(20000);
    get.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::SameOriginRedirectPolicy);
    auto* reply=dlnaNetwork_->get(get);
    connect(reply,&QNetworkReply::downloadProgress,reply,[reply](qint64 received,qint64){if(received>MaxPhotoBytes)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,request]{
        reply->deleteLater();
        if(request!=dlnaPhotoRequest_ || !mediaOpen_)return;
        loading_=false;
        const QByteArray bytes=reply->error()==QNetworkReply::NoError?reply->readAll():QByteArray();
        QImage image;
        if(!bytes.isEmpty() && bytes.size()<=MaxPhotoBytes) {
            QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);reader.setAutoTransform(true);
            const auto size=reader.size();
            if(size.isValid() && size.width()<=65536 && size.height()<=65536) {
                if(size.width()>3840 || size.height()>2160)reader.setScaledSize(size.scaled(3840,2160,Qt::KeepAspectRatio));
                image=reader.read();
            }
        }
        qInfo().noquote()<<"dlna: photo"<<bytes.size()<<"byte(s)"<<(image.isNull()?QStringLiteral("not decoded"):QStringLiteral("%1x%2").arg(image.width()).arg(image.height()));
        photo_=image;
        if(photo_.isNull()){mediaOpen_=false;showMessage(QObject::tr("This image could not be opened."),MessageKind::Page);}
        else showPhoto();
        emit changed();
    });
}

bool Shell::activateDlna(const Entry& entry) {
    switch(entry.action) {
    case Action::DlnaScan:
        if(dlnaScanning_)return true;
        dlnaScanning_=true;dlnaKind_=categories_[category_].id;
        loading_=true;status_=QObject::tr("Searching for media servers...");
        qInfo().noquote()<<"dlna: scan started from the"<<dlnaKind_<<"column";
        dlna_.discover(3000);
        emit changed();
        return true;
    case Action::DlnaServer: {
        const int index=entry.value.toInt();
        openDlnaFolder(index,QStringLiteral("0"),entry.title);
        return true;
    }
    case Action::DlnaFolder: {
        const int server=pages_.isEmpty()?-1:pages_.last().dlnaServer;
        openDlnaFolder(server,entry.value,entry.title);
        return true;
    }
    case Action::DlnaPhoto:openDlnaPhoto(entry);return true;
    default:return false;
    }
}

}
