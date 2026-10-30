#include "shell.h"
#include "settings_menu.h"
#include "media_controls.h"
#include <QApplication>
#include "assets.h"
#include "catalog.h"
#include "xmbml_view.h"
#include "host.h"
#include "p3t.h"
#include "steam_library.h"
#include "steam_artwork.h"
#include "steam_collections.h"
#include "steam_overrides.h"
#include "steam_family.h"
#include "steam_downloads.h"
#include <QDesktopServices>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVideoSink>
#include <QVideoFrame>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <QImageReader>
#include <QLoggingCategory>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QTransform>
#include <algorithm>
#include <functional>
#include <limits>

namespace xmb {
namespace {
Entry item(QString id,QString title,QString texture,QString container="explore_plugin_full",Action action=Action::Unavailable,QString value={}) {
    return {std::move(id),std::move(title),{},std::move(container),std::move(texture),action,std::move(value)};
}
// PARAM.SFO (PSF): "\0PSF", version, key table offset @8, data table offset @12, entry count @16, then 16-byte
// entries {u16 key offset, u16 format, u32 length, u32 max length, u32 data offset}; all little-endian.
QHash<QString,QString> readSfo(const QString& path) {
    QFile file(path);QHash<QString,QString> values;
    if(!file.open(QIODevice::ReadOnly))return values;
    const QByteArray d=file.readAll();
    if(d.size()<20 || !d.startsWith(QByteArray("\0PSF",4)))return values;
    const auto u16=[&](qsizetype at){return at+2<=d.size()?quint32(quint8(d[at])|quint8(d[at+1])<<8):0u;};
    const auto u32=[&](qsizetype at){return at+4<=d.size()?quint32(u16(at)|u16(at+2)<<16):0u;};
    const quint32 keys=u32(8),data=u32(12),count=std::min<quint32>(u32(16),256);
    for(quint32 i=0;i<count;++i) {
        const qsizetype e=20+qsizetype(i)*16;
        const quint32 keyAt=keys+u16(e),format=u16(e+2),length=u32(e+4),dataAt=data+u32(e+12);
        if(keyAt>=quint32(d.size()) || dataAt+length>quint32(d.size()))continue;
        const QString key=QString::fromLatin1(d.constData()+keyAt);
        if(format==0x0204 || format==0x0004)values.insert(key,QString::fromUtf8(d.constData()+dataAt,int(length)).remove(QChar(0)));
        else if(format==0x0404)values.insert(key,QString::number(u32(dataAt)));
    }
    return values;
}
}
// ---- Resume points and Continue Watching ---------------------------------------------------------
// One preferences string holds them, so they survive a restart without a second state file:
//   {"<id>": {"path": "...", "ms": 8768, "dur": 20000, "title": "...", "at": 1761...}}
// id is the first 24 hex characters of the SHA-1 of the path, i.e. exactly the key the earlier build's
// {"<id>": milliseconds} map used. That is why an upgraded data directory can still be matched: the id
// is unchanged, only the value grew from a number into a record that remembers enough to be *listed*.
// A record with an empty path is a legacy entry whose file has not been found yet; it is never listed,
// and migrateResumePoints() either fills its path in or drops it with the reason in the log.
// A finished file is removed (EndOfMedia) and a position inside the existing thresholds is not stored,
// so the map only ever holds titles that are genuinely half-watched.
namespace {
constexpr int kResumeMapLimit=64;                 // the cap the map has always had; only the eviction is smarter
constexpr int kContinueRowLimit=20;               // what the Continue Watching page lists
constexpr qint64 kContinueMaxAgeSeconds=30*86400; // older than this is left off the page
constexpr int kResumePathLimit=512;               // 64 records of this size stay inside Preferences' bound
constexpr int kResumeTitleLimit=200;
QString resumeIdFor(const QString& path) {
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha1).toHex().left(24));
}
QJsonObject resumeMap(const QString& json) {
    const auto document=QJsonDocument::fromJson(json.toUtf8());
    return document.isObject()?document.object():QJsonObject{};
}
QString resumeJson(const QJsonObject& map) {
    return QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact));
}
qint64 resumeSecondsNow() { return QDateTime::currentSecsSinceEpoch(); }
// The stored milliseconds of one entry, in either shape: a record's "ms" or the old bare number.
qint64 resumeMilliseconds(const QJsonValue& value) {
    const double ms=value.isObject()?value.toObject().value("ms").toDouble(0):value.toDouble(0);
    return std::isfinite(ms)&&ms>0?qint64(ms):0;
}
QJsonObject resumeRecord(const QString& path,qint64 positionMs,qint64 durationMs,const QString& title,qint64 at) {
    QJsonObject record;
    if(!path.isEmpty())record.insert("path",path.left(kResumePathLimit));
    record.insert("ms",double(positionMs));
    if(durationMs>0)record.insert("dur",double(durationMs));
    if(!title.trimmed().isEmpty())record.insert("title",title.trimmed().left(kResumeTitleLimit));
    record.insert("at",double(at));
    return record;
}
// One remembered item, newest first. A record that has no path yet sorts last and is never listed.
struct ResumePoint { QString id,path,title; qint64 ms=0,dur=0,at=0; };
QVector<ResumePoint> resumePoints(const QJsonObject& map) {
    QVector<ResumePoint> points;
    points.reserve(map.size());
    for(auto it=map.begin();it!=map.end();++it) {
        if(!it.value().isObject())continue;   // a legacy number that has not been migrated: no path to show
        const auto record=it.value().toObject();
        ResumePoint point;
        point.id=it.key();
        point.path=record.value("path").toString();
        point.title=record.value("title").toString();
        point.ms=qint64(record.value("ms").toDouble(0));
        point.dur=qint64(record.value("dur").toDouble(0));
        point.at=qint64(record.value("at").toDouble(0));
        points.push_back(std::move(point));
    }
    std::stable_sort(points.begin(),points.end(),[](const ResumePoint& left,const ResumePoint& right){
        return left.at>right.at;   // newest first; an entry with no time (at 0) falls to the end
    });
    return points;
}
// Keeps the map bounded exactly as before (64 entries) but evicts the least recently updated record
// instead of whichever key happened to sort first, so the newest Continue Watching list survives.
void boundResumeMap(QJsonObject& map,QStringList* evicted=nullptr) {
    while(map.size()>kResumeMapLimit) {
        QString oldest;qint64 oldestAt=std::numeric_limits<qint64>::max();
        for(auto it=map.begin();it!=map.end();++it) {
            const auto at=qint64(it.value().toObject().value("at").toDouble(0));
            if(oldest.isEmpty() || at<oldestAt){oldest=it.key();oldestAt=at;}
        }
        if(oldest.isEmpty())break;
        map.remove(oldest);
        if(evicted)*evicted<<oldest;
    }
}
// Resolves pathless (legacy) ids back to files by hashing the playable files under the configured
// media libraries with the same truncation the writer used, so a match is exact and not a guess.
// Bounded twice - 5000 files and 12 levels - and it stops as soon as every wanted id is found, so an
// upgrade cannot turn the start-up into a full library scan.
QHash<QString,QString> resolveResumePaths(const QSet<QString>& wanted,const QStringList& roots,
                                          int& scanned,bool& capped) {
    static const QSet<QString> suffixes{QStringLiteral("mp4"),QStringLiteral("mkv"),QStringLiteral("avi"),
        QStringLiteral("mov"),QStringLiteral("webm"),QStringLiteral("m4v"),QStringLiteral("mpg"),
        QStringLiteral("mpeg"),QStringLiteral("wmv"),QStringLiteral("ts"),QStringLiteral("m2ts"),
        QStringLiteral("mp3"),QStringLiteral("flac"),QStringLiteral("wav"),QStringLiteral("m4a"),
        QStringLiteral("ogg"),QStringLiteral("aac"),QStringLiteral("wma"),QStringLiteral("opus")};
    constexpr int kFileLimit=5000,kDepthLimit=12;
    QHash<QString,QString> found;
    QSet<QString> remaining=wanted,visited;
    scanned=0;capped=false;
    std::function<void(const QString&,int)> walk=[&](const QString& directory,int depth) {
        if(remaining.isEmpty() || capped || depth>kDepthLimit || scanned>=kFileLimit)return;
        const auto canonical=QFileInfo(directory).canonicalFilePath();
        const QString key=canonical.isEmpty()?QDir::cleanPath(directory):canonical;
        if(visited.contains(key))return;    // a directory link back up the tree cannot loop
        visited.insert(key);
        const QDir dir(key);
        for(const auto& info:dir.entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot)) {
            if(capped || remaining.isEmpty())return;
            if(info.isDir()) {
                if(info.isSymLink() && info.canonicalFilePath().isEmpty())continue;
                walk(info.absoluteFilePath(),depth+1);
                continue;
            }
            if(!info.isFile() || !suffixes.contains(info.suffix().toLower()))continue;
            if(scanned>=kFileLimit){capped=true;return;}
            const QString path=QDir::cleanPath(QDir::fromNativeSeparators(info.absoluteFilePath()));
            ++scanned;
            const QString id=resumeIdFor(path);
            if(!remaining.remove(id))continue;
            found.insert(id,path);
        }
    };
    for(const auto& root:roots)if(!remaining.isEmpty())walk(root,0);
    return found;
}
}
QString Shell::emulatorPath() const {
    const QDir root(QDir(assets_.hddRoot()).absoluteFilePath(".."));
    for(const QString& name:{QStringLiteral("rpcs3.exe"),QStringLiteral("rpcs3")})
        if(const QFileInfo exe(root.filePath(name));exe.isFile())return exe.absoluteFilePath();
    return {};
}
void Shell::addInstalledApps() {
    // Installed content in dev_hdd0/game (PARAM.SFO + ICON0.PNG), listed in the category its CATEGORY code belongs to,
    // as the firmware's category XMBML queries do (e.g. category_network.xml "gameDir"): games under Game, network
    // applications such as Information Board or Life with PlayStation under Network. Disc copies RPCS3 has in its
    // game list (config/games.yml, "TITLE_ID: folder") join the Game column.
    //
    // A title with USRDIR/EBOOT.BIN starts in the RPCS3 that owns this dev_hdd0. Following MaXMB (MIT, src-tauri/src/
    // ps3.rs): a title in RPCS3's list starts by Title ID ("%RPCS3_GAMEID%:<id>"), so RPCS3 uses its own licences and
    // installed data; any other by its EBOOT.BIN, since RPCS3 refuses a bare game folder.
    static const QHash<QString,QString> categoryFor{{"HG","game"},{"DG","game"},{"1P","game"},{"2P","game"},{"MN","game"},{"PE","game"},
        {"CB","network"},{"WT","network"},{"AT","tv"},{"AV","video"},{"BV","video"},{"AM","music"},{"AP","photo"}};
    const bool emulator=!emulatorPath().isEmpty();
    QHash<QString,QString> listed; // Title ID -> folder, from RPCS3's games.yml
    {
        const QDir root(QDir(assets_.hddRoot()).absoluteFilePath(".."));
        for(const QString& name:{QStringLiteral("config/games.yml"),QStringLiteral("games.yml")}) {
            QFile file(root.filePath(name));
            if(!file.open(QIODevice::ReadOnly|QIODevice::Text))continue;
            for(const auto& raw:QString::fromUtf8(file.readAll()).split('\n')) {
                const int colon=raw.indexOf(':');
                if(colon<=0)continue;
                const QString id=raw.left(colon).trimmed().remove('"').remove('\'').toUpper();
                QString path=raw.mid(colon+1).trimmed().remove('"').remove('\'');
                if(id.isEmpty() || path.isEmpty())continue;
                if(QDir::isRelativePath(path))path=root.filePath(path);
                listed.insert(id,QDir::cleanPath(path));
            }
            break;
        }
    }
    const auto add=[&](const QString& folder,const QString& content,bool discCopy) {
        const auto sfo=readSfo(QDir(content).filePath("PARAM.SFO"));
        const auto target=categoryFor.value(sfo.value("CATEGORY"));
        if(target.isEmpty() || sfo.value("TITLE").isEmpty())return;
        const QString id=sfo.value("TITLE_ID",QFileInfo(folder).fileName()).trimmed().toUpper();
        const QString eboot=QDir(content).filePath("USRDIR/EBOOT.BIN");
        const bool bootable=QFileInfo(eboot).isFile() || (discCopy && listed.contains(id));
        for(auto& category:categories_) {
            if(category.id!=target)continue;
            const QString icon=QDir(content).filePath("ICON0.PNG");
            const QString details=sfo.value("TITLE")+"\n\n"+id+"  "+sfo.value("APP_VER",sfo.value("VERSION"));
            auto entry=item("app:"+id+":"+QFileInfo(folder).fileName(),sfo.value("TITLE"),QFileInfo::exists(icon)?icon:QString("game_tex_default_ps3"),
                QFileInfo::exists(icon)?QString("file"):QString("explore_plugin_full"),Action::Information,
                details+(emulator?"\nThis title has no EBOOT.BIN to start.":"\nNo RPCS3 executable was found next to dev_hdd0, so it cannot be started."));
            if(emulator && bootable){entry.action=Action::Launch;entry.value=listed.contains(id)?"%RPCS3_GAMEID%:"+id:eboot;}
            category.entries.push_back(std::move(entry));
        }
    };
    const QDir games(QDir(assets_.hddRoot()).filePath("game"));
    QSet<QString> seen;
    for(const auto& folder:games.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) {
        seen.insert(QDir::cleanPath(folder.absoluteFilePath()).toLower());
        add(folder.absoluteFilePath(),folder.absoluteFilePath(),false);
    }
    for(auto it=listed.cbegin();it!=listed.cend();++it) {
        QString folder=it.value();
        if(folder.endsWith('/'))folder.chop(1);
        if(seen.contains(folder.toLower()) || !QFileInfo(folder).isDir())continue;
        seen.insert(folder.toLower());
        const QString content=QFileInfo(QDir(folder).filePath("PS3_GAME/PARAM.SFO")).isFile()?QDir(folder).filePath("PS3_GAME"):folder;
        add(folder,content,true);
    }
}
// --- Network column from the firmware's XMBML -------------------------------------------------------
// audit/FIRMWARE-PARITY-GAPS.md row 8 (M1) and audit/XMBML-NETWORK.md: the 4.93 firmware describes this
// column in vsh/resource/explore/xmb/category_network.xml - the root view lists the rows as <Query>
// nodes in display order, and each query names a pixmap view whose <Table> carries title_rsc and
// icon_rsc. Those keys are resolved through the RCO's English strings. Read once per process, read-only,
// bounded by readXmbDocument and the row cap; the built-in list stays in place when the definition
// cannot be read, and the reason is logged.
void Shell::resolveNetworkColumn() {
    const auto category=std::find_if(categories_.begin(),categories_.end(),[](const Category& c){return c.id==QStringLiteral("network");});
    if(category==categories_.end())return;
    const QString container=QStringLiteral("explore_category_network");
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    QElapsedTimer timer;timer.start();
    const auto column=readCategoryView(QDir(assets_.root()).filePath(QStringLiteral("vsh/resource/explore/xmb/category_network.xml")));
    const QString parseMs=QString::number(timer.nsecsElapsed()/1e6,'f',2);
    const auto builtin=[&](const QString& reason) {
        qWarning().noquote()<<"xmbml: network column keeps its built-in rows:"<<reason<<column.path
            <<("(definition parsed in "+parseMs+" ms)");
        for(auto& entry:category->entries)entry.source=QStringLiteral("fallback");
        if(verbose)for(const auto& entry:category->entries)
            qInfo().noquote()<<"xmbml: network fallback row"<<entry.id<<"("<<entry.title<<") source=fallback";
    };
    if(!column.ok()){builtin(column.error);return;}
    // Labels the firmware builds in a plugin rather than in XMBML: this file's seg_premo view is the bare
    // <Item class="type:x-xmb/xmlpremo">, and the 4.93 shop variant of the same column
    // (category_network_shop.xml, same dev_flash) carries the table this one omits (title_rsc
    // msg_psp_connection, icon_rsc tex_premo); explore_category_network ships both strings plus
    // msg_download_control/tex_dl_manage for the download query the ese plugin answers.
    struct PluginLabel { QString msg,texture; };
    static const QHash<QString,PluginLabel> pluginLabels{
        {"seg_premo",{"msg_psp_connection","tex_premo"}},
        {"seg_dlctrl",{"msg_download_control","tex_dl_manage"}}};
    // Last resort only, for a dev_flash whose RCO text is unreadable: the labels this build shipped
    // before the column came from XMBML.
    static const QHash<QString,QPair<QString,QString>> lastResort{
        {"seg_manual",{"Online Instruction Manuals","tex_onlinemanual"}},
        {"seg_premo",{"Remote Play","tex_premo"}},
        {"seg_browser",{"Internet Browser","tex_browser"}},
        {"seg_kensaku",{"Internet Search","tex_kensaku"}},
        {"seg_dlctrl",{"Download Management","tex_dl_manage"}}};
    // The XMBML item class decides the native action. Almost every Network row in 4.93 is a browser
    // handoff, a browser plugin action, Remote Play, an in-game row or a console-owned list; the one
    // exception is the download queue, which Steam keeps in a form this build can read. The XMBML
    // carries no URL, so an Action::OpenUrl target would have to be invented. The reason is logged per
    // row and recorded in the audit.
    const auto actionFor=[](RowKind kind,const XmbmlRow& row)->std::pair<Action,QString> {
        switch(kind) {
        case RowKind::DownloadControl:
            return {Action::SteamDownloads,QStringLiteral("the console's download plugin lists its queue; "
                "this build lists Steam's own download queue from the manifests Steam writes "
                "(src/steam_downloads.cpp, audit/STEAM-DOWNLOADS.md)")};
        case RowKind::WebBrowser:
            return {Action::Unavailable,QStringLiteral("the firmware hands this node to the PS3 Internet Browser; "
                "this build has no browser and the XMBML carries no URL to hand to the host one")};
        case RowKind::ModuleAction:
            return {Action::Unavailable,QStringLiteral("the plugin the firmware runs (")+(row.moduleName.isEmpty()?QStringLiteral("unknown"):row.moduleName)+
                QLatin1Char(':')+(row.moduleAction.isEmpty()?QStringLiteral("unknown"):row.moduleAction)+
                QStringLiteral(") is part of the PS3 browser, which this build does not have")};
        case RowKind::RemotePlay:
            return {Action::Unavailable,QStringLiteral("Remote Play needs a PS3 and a registered PSP/Vita; "
                "the PC equivalent is a different, destructive remote-desktop path")};
        case RowKind::DynamicProvider:
            return {Action::Unavailable,QStringLiteral("the row is produced by the console's ")+row.provider+
                QStringLiteral(":// provider at run time; this build has no such provider")};
        case RowKind::InGameExit:
        case RowKind::Unknown:
            break;
        }
        return {Action::Unavailable,QStringLiteral("the item class is not mapped to a native action")};
    };
    QVector<Entry> rows;
    int unresolved=0;
    for(const auto& row:column.rows) {
        // Firmware visibility that this build cannot reach: the in-game XMB, and the installed-content
        // query (addInstalledApps() appends those rows at the gameDir position, after the page rows).
        if(row.inGameOnly) {
            if(verbose)qInfo().noquote()<<"xmbml: network skip"<<row.key<<"(in-game only: this build has no in-game XMB)";
            continue;
        }
        if(row.key==QStringLiteral("gameDir")) {
            if(verbose)qInfo().noquote()<<"xmbml: network skip gameDir (the installed-content scan owns these rows)";
            continue;
        }
        QString title,source;
        if(!row.titleRsc.isEmpty()) {
            title=assets_.text(container,row.titleRsc);
            if(!title.isEmpty())source=QStringLiteral("xmbml");
        }
        if(title.isEmpty() && pluginLabels.contains(row.key)) {
            title=assets_.text(container,pluginLabels[row.key].msg);
            if(!title.isEmpty())source=QStringLiteral("rco");
        }
        if(title.isEmpty()) {
            title=lastResort.value(row.key).first;
            if(!title.isEmpty())source=QStringLiteral("builtin");
        }
        if(title.isEmpty()){++unresolved;qWarning().noquote()<<"xmbml: network row"<<row.key<<"has no title (no title_rsc, no plugin label)";continue;}
        QString texture=row.iconRsc;
        if(texture.isEmpty())texture=pluginLabels.value(row.key).texture;
        if(texture.isEmpty())texture=lastResort.value(row.key).second;
        Entry entry=item(row.id,title,texture,container,Action::Unavailable);
        entry.source=source;
        if(!row.infoRsc.isEmpty())entry.detail=assets_.text(container,row.infoRsc);
        const auto decision=actionFor(row.kind(),row);
        entry.action=decision.first;
        if(verbose)qInfo().noquote()<<"xmbml: network row"<<entry.id<<"key="+row.key
            <<"class="+(row.itemClass.isEmpty()?QStringLiteral("-"):row.itemClass)
            <<"title=\""+entry.title+"\""<<"titleSource="+(row.titleRsc.isEmpty()?source:row.titleRsc)
            <<"texture="+entry.texture<<"action="+QString::number(int(entry.action))<<"source="+source
            <<"reason=\""+decision.second+"\"";
        rows.push_back(std::move(entry));
    }
    if(rows.isEmpty()){builtin(QStringLiteral("no row of the definition could be titled")+" ("+QString::number(unresolved)+QStringLiteral(" unresolved)"));return;}
    category->entries=rows;
    qInfo().noquote()<<QStringLiteral("xmbml: network column resolved %1 rows from category_network.xml "
        "(queries %2, sha256 %3): parsed in %4 ms, resolved in %5 ms%6")
        .arg(int(rows.size())).arg(column.queries).arg(column.sha256.left(12)).arg(parseMs)
        .arg(QString::number(timer.nsecsElapsed()/1e6,'f',2))
        .arg(unresolved?QStringLiteral(", %1 untitleable rows").arg(unresolved):QString());
}
// --- The three media columns, from the firmware's XMBML ---------------------------------------------
// audit/FIDELITY-MEDIA.md V1/M1/P1 (and audit/MENUS-XMBML.md's column mechanism): category_video.xml,
// category_music.xml and category_photo.xml describe each media column as the root view's ordered <Query>
// list, and each query names the pixmap view whose <Table> carries the row's title_rsc and icon_rsc. The
// Network column proved this reader (resolveNetworkColumn above); the media columns are the same mechanism
// with one difference the console frames decide: a column at rest shows only the rows the console always
// registers - the utility rows - because every other query is a provider (an inserted disc, a DLNA server,
// the HDD contents, an install in progress) that has nothing to list on this rig and needs no row.
//   Video  3 rows: BD Data Utility, Search for Media Servers, Video Editor & Uploader  (col2-video-r00..r02)
//   Music  2 rows: Search for Media Servers, Playlists                                (col-music-r00/r01)
//   Photo  3 rows: Search for Media Servers, Photo Gallery, Playlists                 (col-photo-r00..r02)
// The rows this build adds for its own features (the library folder, the folder picker, Photo Settings)
// follow the console's rows in that order and carry source = "native", so a dump separates the two sets
// without a second list. A definition that cannot be read keeps the built-in table in the constructor,
// which is written in the same order, and says so.
void Shell::resolveMediaColumns() {
    struct Spec {
        const char* id;         // the native row id (what the catalogue and the activation path know)
        Action action;          // what Cross does; Unavailable carries `why` on its page
        const char* titleRsc;   // the column RCO's own key, used when the definition's view has none
        const char* icon;       // the texture, used when the definition's view has none
        const char* container;  // the RCO the icon is read from
        const char* why;        // the reason Cross cannot perform the firmware's action
    };
    static const QHash<QString,Spec> specs{
        // Video (category_video.xml root view, in document order)
        {"video:seg_bddata_mgmt",{"bddata",Action::Unavailable,"msg_bd_data_utility","item_tex_plain_folder","explore_category_video",
            "BD Data Management lists the bonus data a Blu-ray disc wrote to the console's hard disk. This computer has "
            "no Blu-ray drive and no PS3 BD data store, so there is nothing to manage."}},
        {"video:seg_dlna_scan",{"dlna-scan",Action::Unavailable,"msg_scan_mediaserver","item_tex_dlna_scan","explore_category_video",
            "Search for Media Servers asks the DLNA/UPnP client (dlna_plugin) to discover media servers on the network. "
            "This build has no DLNA client; its network library is Jellyfin, which is signed in from the TV column."}},
        {"video:seg_editingvideo_mgmt",{"video-editor",Action::Unavailable,"msg_video_editor_and_upload","item_tex_upload","explore_category_video",
            "Video Editor & Uploader is the console's own scene editor (scenefolder_plugin) for clips recorded on the "
            "system. No capture device and no editor exist in this build, so the row lists the firmware's function "
            "and opens this page instead."}},
        // Music (category_music.xml)
        {"music:seg_dlna_scan",{"dlna-scan",Action::Unavailable,"msg_scan_mediaserver","item_tex_dlna_scan","explore_category_music",
            "Search for Media Servers asks the DLNA/UPnP client (dlna_plugin) to discover media servers on the network. "
            "This build has no DLNA client; its network library is Jellyfin, which is signed in from the TV column."}},
        {"music:seg_playlist_mgmt",{"playlists",Action::Unavailable,"msg_playlists","tex_playlist","explore_category_music",
            "Playlists is the console's playlist manager (playlist_plugin). This build's queue is the folder a track was "
            "started from, so there is no stored playlist to list or edit."}},
        // Photo (category_photo.xml)
        {"photo:seg_dlna_scan",{"dlna-scan",Action::Unavailable,"msg_scan_mediaserver","item_tex_dlna_scan","explore_category_photo",
            "Search for Media Servers asks the DLNA/UPnP client (dlna_plugin) to discover media servers on the network. "
            "This build has no DLNA client; its network library is Jellyfin, which is signed in from the TV column."}},
        {"photo:seg_hakoniwa",{"gallery",Action::Unavailable,"msg_photo_gallery","","file",
            "Photo Gallery is the console's own 3D gallery application (hakoniwa). This build's photo viewer is the "
            "full-screen slideshow Photo Library opens."}},
        {"photo:seg_playlist_mgmt",{"playlists",Action::Unavailable,"msg_playlists","tex_playlist","explore_category_photo",
            "Playlists is the console's playlist manager (playlist_plugin). This build's queue is the folder a photo was "
            "started from, so there is no stored playlist to list or edit."}}};
    // The queries a media column never draws at rest, each with the reason it is not a row here. A provider with
    // nothing attached produces no row on the console either, which is why these are skipped rather than listed.
    static const QHash<QString,QString> skipped{
        {"video:seg_welcome","the PlayStation Store video page: this build has no store session"},
        {"video:seg_gameexit","an in-game XMB row"},
        {"video:gameDir","the installed-content scan owns these rows (this build lists none for Video)"},
        {"video:seg_dlna_device","a discovered DLNA server: nothing is discoverable without the client"},
        {"video:seg_bdmav_device","an inserted Blu-ray disc: no drive"},
        {"video:seg_avchd_device","an inserted AVCHD disc: no drive"},
        {"video:seg_dvdv_device","an inserted DVD: no drive"},
        {"video:seg_data_device","a data disc: no drive"},
        {"video:seg_hdd_install","an install in progress: the console's own download queue"},
        {"video:seg_hdd_contents","the HDD's video contents: Video Library is this build's version of it"},
        {"music:seg_now","the now-playing provider: this build has no XMB music player page"},
        {"music:seg_welcome","the PlayStation Store music page: this build has no store session"},
        {"music:seg_gameexit","an in-game XMB row"},
        {"music:gameDir","the installed-content scan owns these rows (this build lists none for Music)"},
        {"music:seg_dlna_device","a discovered DLNA server: nothing is discoverable without the client"},
        {"music:seg_sacd_device","an inserted SA-CD: no drive"},
        {"music:seg_cdda_device","an inserted audio CD: no drive"},
        {"music:seg_dsd_device","an inserted DSD disc: no drive"},
        {"music:seg_data_device","a data disc: no drive"},
        {"music:seg_hdd_contents","the HDD's music contents: Music Library is this build's version of it"},
        {"photo:seg_screenshot","the screenshot list: a console capture store this build does not keep"},
        {"photo:seg_gameexit","an in-game XMB row"},
        {"photo:gameDir","the installed-content scan owns these rows (this build lists none for Photo)"},
        {"photo:seg_dlna_device","a discovered DLNA server: nothing is discoverable without the client"},
        {"photo:seg_data_device","a data disc: no drive"},
        {"photo:seg_hdd_contents","the HDD's photo contents: Photo Library is this build's version of it"}};
    // The native rows each column appends, by the ids the built-in table already declared. They are taken
    // from the current entries so their action/value/setting stay exactly as the table wrote them.
    static const QHash<QString,QStringList> nativeIds{
        {"video",{"continue","library","choose"}},{"music",{"library","choose"}},
        {"photo",{"library","choose","options"}}};
    static const QHash<QString,QString> xmlFiles{
        {"video","category_video.xml"},{"music","category_music.xml"},{"photo","category_photo.xml"}};
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    for(auto& category:categories_) {
        const auto& file=xmlFiles.value(category.id);
        if(file.isEmpty())continue;
        const auto container=xmlFiles.contains(category.id)?QStringLiteral("explore_category_")+category.id:QString();
        QVector<Entry> native;
        for(const auto& entry:category.entries)if(nativeIds.value(category.id).contains(entry.id))native.push_back(entry);
        if(native.isEmpty())continue;
        QElapsedTimer timer;timer.start();
        const auto column=readCategoryView(QDir(assets_.root()).filePath(QStringLiteral("vsh/resource/explore/xmb/")+file));
        const auto builtin=[&](const QString& reason) {
            for(auto& entry:category.entries)
                entry.source=nativeIds.value(category.id).contains(entry.id)?QStringLiteral("native"):QStringLiteral("fallback");
            qWarning().noquote()<<"xmbml:"<<category.id<<"column keeps its built-in rows:"<<reason<<column.path;
        };
        if(!column.ok()){builtin(column.error);continue;}
        QVector<Entry> rows;
        int skippedRows=0,nativeRows=0;
        for(const auto& row:column.rows) {
            const QString key=category.id+QLatin1Char(':')+row.key;
            const auto spec=specs.constFind(key);
            if(spec==specs.cend()) {
                ++skippedRows;
                if(verbose)qInfo().noquote()<<"xmbml:"<<category.id<<"skip"<<row.key
                    <<"("+skipped.value(key,QStringLiteral("no row of this build's definition names it"))+")";
                continue;
            }
            QString title;
            if(!row.titleRsc.isEmpty())title=assets_.text(container,row.titleRsc);
            if(title.isEmpty())title=assets_.text(container,QString::fromLatin1(spec->titleRsc));
            if(title.isEmpty()){++skippedRows;qWarning().noquote()<<"xmbml:"<<category.id<<"row"<<row.key<<"has no title";continue;}
            QString texture=row.iconRsc;
            QString iconContainer=container;
            if(texture.isEmpty()) {
                texture=QString::fromLatin1(spec->icon);
                iconContainer=QString::fromLatin1(spec->container);
            }
            Entry entry=item(QString::fromLatin1(spec->id),title,texture,iconContainer,spec->action);
            entry.detail=QString::fromLatin1(spec->why);
            // Photo Gallery's art is the firmware's own hakoniwa.png on dev_flash, not an RCO texture; it is
            // square (180x180) where an installed title's ICON0 is a wide card, so the row says so and the view
            // keeps it in the icon box (Entry::iconSized).
            if(texture.isEmpty() && iconContainer==QLatin1String("file")) {
                entry.texture=QDir(assets_.root()).filePath(QStringLiteral("vsh/resource/explore/icon/hakoniwa.png"));
                entry.iconSized=true;
            }
            entry.source=row.titleRsc.isEmpty()?QStringLiteral("xmbml+plugin"):QStringLiteral("xmbml");
            // The Settings column's icon override map and the theme's entry icons key off these ids, so a
            // resolved media row keeps the container the definition gave it.
            if(verbose)qInfo().noquote()<<"xmbml:"<<category.id<<"row"<<entry.id<<"key="+row.key
                <<"view="+(row.viewId.isEmpty()?QStringLiteral("-"):row.viewId)
                <<"title=\""+entry.title+"\""<<"titleRsc="+(row.titleRsc.isEmpty()?QString::fromLatin1(spec->titleRsc):row.titleRsc)
                <<"texture="+entry.texture<<"container="+iconContainer
                <<"action="+QString::number(int(entry.action))<<"source="+entry.source;
            rows.push_back(std::move(entry));
        }
        for(auto& entry:native){entry.source=QStringLiteral("native");rows.push_back(entry);++nativeRows;}
        if(rows.size()==native.size()){builtin(QStringLiteral("no row of the definition is a row of this column"));continue;}
        category.entries=rows;
        qInfo().noquote()<<QStringLiteral("xmbml: %1 column resolved %2 rows from %3 (queries %4, sha256 %5): "
            "%6 from the definition, %7 native, %8 queries skipped, parsed in %9 ms")
            .arg(category.id).arg(int(rows.size())).arg(file).arg(column.queries).arg(column.sha256.left(12))
            .arg(int(rows.size())-nativeRows).arg(nativeRows).arg(skippedRows)
            .arg(QString::number(timer.nsecsElapsed()/1e6,'f',2));
    }
}
// Launches a child and keeps it parented so the XMB knows when it returns. While it runs the
// idle auto-close is suppressed; on exit the library is rescanned and the column restored,
// which is the return lifecycle a launched game needs (audit/FEATURE-COVERAGE.md, ISC-20).
bool Shell::startTracked(const QString& program,const QStringList& arguments,const QString& workingDirectory,const QString& title) {
    if(!game_.isNull()){showMessage(QObject::tr("A title is already running."),MessageKind::Page);return false;}
    auto* process=new QProcess(this);
    process->setProgram(program);
    process->setArguments(arguments);
    if(!workingDirectory.isEmpty())process->setWorkingDirectory(workingDirectory);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(process,&QProcess::errorOccurred,this,[this,process,title](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart)return;
        const QString cause=QObject::tr("This title could not be started.")+
            (process->errorString().isEmpty()?QString{}:QStringLiteral(" (")+process->errorString()+QStringLiteral(")"));
        process->deleteLater();game_.clear();
        // The firmware's error-code page (D2, spec-only) is the surface for a start failure: a title, a
        // Possible Cause and Suggested Actions. The cause here is the OS's own report and the action is
        // this build's wording - the firmware's own action bullets are network/PSN-specific.
        showErrorPage(title,
            cause,{QObject::tr("Check that the application is still installed, then try again.")});
        emit changed();
    });
    connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
            [this,process,title](int code,QProcess::ExitStatus status){
        process->deleteLater();game_.clear();gameTitle_.clear();touch();
        qInfo().noquote()<<"returned from"<<title<<"exit"<<code<<(status==QProcess::NormalExit?"normal":"crashed");
        // The return of a tracked title is a real event this build can report. The console itself has no
        // return pop-up (only launches), so this one is a native event, not a firmware one.
        if(status==QProcess::CrashExit) {
            showMessage(title,QObject::tr("This title ended unexpectedly."),MessageKind::Page);
            notify(title,QObject::tr("This title ended unexpectedly."),"tex_notification_caution");
        }
        else notify(title,QObject::tr("Returned to the XMB"),"tex_notification_info");
        emit changed();
        refreshContent();
    });
    process->start();
    if(!process->waitForStarted(4000)){
        const QString cause=QObject::tr("This title could not be started.")+
            (process->errorString().isEmpty()?QString{}:QStringLiteral(" (")+process->errorString()+QStringLiteral(")"));
        process->deleteLater();
        showErrorPage(title,cause,{QObject::tr("Check that the application is still installed, then try again.")});
        return false;
    }
    game_=QPointer<QProcess>(process);
    gameTitle_=title;
    activity_.start();
    qInfo().noquote()<<"launch:"<<program<<arguments.join(' ')<<"(tracked)";
    emit changed();
    return true;
}
bool Shell::gameRunning() const {
    // Also true while a Steam handoff is being watched, so the idle auto-close cannot quit behind a game.
    return (!game_.isNull() && game_->state()!=QProcess::NotRunning) || !steamGameId_.isEmpty();
}
// Steam launches itself, so the only honest lifecycle signal is Steam's own registry state. The
// handoff records the app id, watches Apps\<id>\Running, and reports the return when it clears.
bool Shell::startSteamTracked(const QString& appId,const QString& title) {
    const QString exe=QDir(steamRoot_).filePath("steam.exe");
    if(!QFileInfo(exe).isFile() || !QProcess::startDetached(exe,{"-applaunch",appId},steamRoot_)) {
        showMessage(title,"Steam could not be started. Refresh the game library and check the Steam installation.",MessageKind::Page);
        return false;
    }
    steamGameId_=appId;gameTitle_=title;steamGameSeen_=false;steamGameClear_=0;
    steamWatchAge_.start();steamWatch_.start();
    activity_.start();
    qInfo().noquote()<<"launch: steam -applaunch"<<appId<<"(watched through Steam's registry)";
    emit changed();
    return true;
}
void Shell::pollSteamGame() {
    if(steamGameId_.isEmpty()) { steamWatch_.stop(); return; }
    const auto state=steamAppState(steamGameId_);
    constexpr qint64 startTimeoutMs=5*60*1000;   // Steam may download/update before the title runs
    if(state.updating) { steamWatchAge_.restart(); return; }
    if(state.running) {
        if(!steamGameSeen_)qInfo().noquote()<<"steam:"<<gameTitle_<<"is running";
        steamGameSeen_=true;steamGameClear_=0;return;
    }
    if(!steamGameSeen_) {
        if(steamWatchAge_.elapsed()<startTimeoutMs)return;
        steamWatch_.stop();const QString title=gameTitle_;steamGameId_.clear();gameTitle_.clear();
        showMessage(title,"Steam did not report this title as running.",MessageKind::Page);
        qWarning().noquote()<<"steam:"<<title<<"never reported running";
        emit changed();return;
    }
    // Steam clears the flag when the last process exits; require two polls so a launcher-to-game
    // handover inside Steam is not mistaken for a return.
    if(++steamGameClear_<2)return;
    steamWatch_.stop();
    const QString title=gameTitle_;
    steamGameId_.clear();gameTitle_.clear();steamGameSeen_=false;steamGameClear_=0;
    qInfo().noquote()<<"returned from"<<title<<"(Steam reported it stopped)";
    notify(title,QObject::tr("Returned to the XMB"),"tex_notification_info");
    restoreFocus();
    emit changed();
    refreshContent();
}
void Shell::restoreFocus() {
    // Returning from a title starts a fresh idle interval, as the media and dialog paths do.
    activity_.start();
}
void Shell::launch(const Entry& entry) {
    const QString emulator=emulatorPath();
    if(emulator.isEmpty() || !startTracked(emulator,{"--no-gui",entry.value},QFileInfo(emulator).absolutePath(),entry.title)) {
        if(emulator.isEmpty())showMessage(entry.title,"RPCS3 could not be started.",MessageKind::Page);
        return;
    }
}
// The account's own Steam library collections turn the owned list into the user's own
// groups; the label is what the XMB row shows under the title.
QString Shell::collectionLabel(const QString& appId) const {
    QStringList names;
    for(const auto& collection:collections_)if(collection.appIds.contains(appId))names.push_back(collection.name);
    return names.join(QStringLiteral(", "));
}
bool Shell::stageOwnedForRefresh() const {
    // Only ask Valve when a key exists and no request is already running; the loaded
    // handler compares a signature so a rescan does not loop.
    return SteamOwned::hasApiKey() && !ownedInFlight_;
}
void Shell::refreshOwnedGames() {
    if(!SteamOwned::hasApiKey() || ownedInFlight_)return;
    ownedInFlight_=true;
    steamOwned_.refresh(steamId_);
}
// Distinct titles the Steam library knows about: installed, owned and family-shared.
// The Game column and the All Games folder show this as their detail count.
int Shell::steamTitleCount() const {
    QSet<QString> ids;
    for(const auto& game:steamInstalled_.games)ids.insert(game.appId);
    for(const auto& game:ownedGames_)ids.insert(game.appId);
    for(const auto& game:familyGames_)ids.insert(game.appId);
    return ids.size();
}
// Rows inside a Steam library folder. Built on demand so a 280-title library does not
// cost anything until the folder is opened; only a bounded number of cached thumbnails
// are decoded here.
QVector<Entry> Shell::steamFolderEntries(const QString& mode) const {
    QVector<Entry> rows;
    const QString cache=QDir(dataDirectory_).filePath("cache/steam");
    int decoded=0;
    const auto add=[&](const QString& appId,const QString& title,const QString& detail,Action action) {
        auto e=item("steam:"+appId,title,"game_tex_default_ps3","explore_plugin_full",action,appId);
        // Inside a page these are ordinary items, not the category column's wide provider
        // tiles: a gameTile row here is laid out at the wide-tile size and lands out of
        // alignment. Artwork is centre-cropped to a square for the same reason.
        e.gameTile=false;e.detail=detail;
        if(decoded<64) {
            QImageReader reader(SteamArtwork::cachePath(cache,appId));
            if(reader.canRead()) {
                const auto size=reader.size();
                if(size.isValid() && size.width()<=16384 && size.height()<=16384) {
                    reader.setScaledSize(size.scaled(512,288,Qt::KeepAspectRatio));
                    const auto image=reader.read();
                    if(!image.isNull()) {
                        const int side=std::min(image.width(),image.height());
                        e.artwork=image.copy((image.width()-side)/2,(image.height()-side)/2,side,side);
                        ++decoded;
                    }
                }
            }
        }
        rows.push_back(std::move(e));
    };
    // Folder rows only: no artwork decode and no app-id artwork request, because the value
    // is a folder mode string rather than an app id.
    const auto addFolder=[&](const QString& id,const QString& title,const QString& detail,const QString& folderMode) {
        auto e=item(id,title,"item_tex_plain_folder","explore_plugin_full",Action::SteamFolder,folderMode);
        e.detail=detail;
        rows.push_back(std::move(e));
    };
    // Title lookups shared by every folder mode. An installed title wins over its owned
    // entry because the local manifest title is the one the console shows.
    QHash<QString,QString> installedTitles,ownedTitles,familyTitles;
    for(const auto& game:steamInstalled_.games)installedTitles.insert(game.appId,game.title);
    for(const auto& game:ownedGames_)ownedTitles.insert(game.appId,game.title);
    for(const auto& game:familyGames_)familyTitles.insert(game.appId,game.title);
    const auto isKnown=[&](const QString& appId) {
        return installedTitles.contains(appId) || ownedTitles.contains(appId) || familyTitles.contains(appId);
    };
    const auto titleFor=[&](const QString& appId)->QString {
        if(installedTitles.contains(appId))return installedTitles.value(appId);
        if(ownedTitles.contains(appId))return ownedTitles.value(appId);
        return familyTitles.value(appId);
    };
    if(mode=="installed") {
        for(const auto& game:steamInstalled_.games) {
            const QString label=collectionLabel(game.appId);
            add(game.appId,game.title,label.isEmpty()?QObject::tr("Installed"):label,Action::LaunchSteam);
        }
    } else if(mode=="library") {
        // Everything that is not installed: owned titles plus family-shared ones.
        for(const auto& game:ownedGames_) {
            if(game.installed)continue;
            const QString label=collectionLabel(game.appId);
            add(game.appId,game.title,label.isEmpty()?QObject::tr("Not installed"):label+" - Not installed",Action::InstallSteam);
        }
        for(const auto& game:familyGames_)add(game.appId,game.title,QObject::tr("Family shared - Not installed"),Action::InstallSteam);
    } else if(mode=="family") {
        for(const auto& game:familyGames_)add(game.appId,game.title,QObject::tr("Family shared"),Action::InstallSteam);
    } else if(mode=="all") {
        // The All Games folder page: an All Titles folder, then one folder per Steam
        // collection that has at least one known title (collections are already in
        // collection-name order), then the Not Installed and Family Shared folders.
        addFolder("steam-folder-all-titles",QObject::tr("All Titles"),QString::number(steamTitleCount()),"all-titles");
        for(const auto& collection:collections_) {
            int known=0;
            for(const auto& appId:collection.appIds)if(isKnown(appId))++known;
            if(known<=0)continue;
            addFolder("steam-folder-collection:"+collection.id,collection.name,QString::number(known),
                "collection:"+collection.id);
        }
        const int ownedNotInstalled=int(std::count_if(ownedGames_.cbegin(),ownedGames_.cend(),
            [](const OwnedGame& game){return !game.installed;}));
        const int notInstalled=ownedNotInstalled+int(familyGames_.size());
        if(notInstalled>0)
            addFolder("steam-folder-library",QObject::tr("Not Installed"),QString::number(notInstalled),"library");
        if(!familyGames_.isEmpty())
            addFolder("steam-folder-family",QObject::tr("Family Shared"),QString::number(familyGames_.size()),"family");
    } else if(mode=="all-titles") {
        // Every title the library knows: installed rows launch, the rest install. An app
        // can be installed and owned at once, and a family title can also be owned, so a
        // seen set keeps each app id once.
        QSet<QString> listed;
        for(const auto& game:steamInstalled_.games) {
            listed.insert(game.appId);
            const QString label=collectionLabel(game.appId);
            add(game.appId,game.title,label.isEmpty()?QObject::tr("Installed"):label,Action::LaunchSteam);
        }
        for(const auto& game:ownedGames_) {
            if(game.installed || listed.contains(game.appId))continue;
            listed.insert(game.appId);
            const QString label=collectionLabel(game.appId);
            add(game.appId,game.title,label.isEmpty()?QObject::tr("Not installed"):label+" - Not installed",Action::InstallSteam);
        }
        for(const auto& game:familyGames_) {
            if(listed.contains(game.appId))continue;
            listed.insert(game.appId);
            add(game.appId,game.title,QObject::tr("Family shared - Not installed"),Action::InstallSteam);
        }
    } else if(mode.startsWith("collection:")) {
        // A collection page keeps the collection's own app order; titles can belong to
        // several collections, so the same app id may appear in more than one folder.
        const QString collectionId=mode.mid(QStringLiteral("collection:").size());
        const auto found=std::find_if(collections_.cbegin(),collections_.cend(),
            [&](const SteamCollection& collection){return collection.id==collectionId;});
        if(found==collections_.cend())return rows;
        for(const auto& appId:found->appIds) {
            if(!isKnown(appId))continue;
            if(installedTitles.contains(appId)) {
                const QString label=collectionLabel(appId);
                add(appId,titleFor(appId),label.isEmpty()?QObject::tr("Installed"):label,Action::LaunchSteam);
            } else if(ownedTitles.contains(appId)) {
                const QString label=collectionLabel(appId);
                add(appId,titleFor(appId),label.isEmpty()?QObject::tr("Not installed"):label+" - Not installed",Action::InstallSteam);
            } else {
                add(appId,titleFor(appId),QObject::tr("Family shared - Not installed"),Action::InstallSteam);
            }
        }
    }
    return rows;
}
void Shell::openSteamFolder(const QString& mode) {
    QString title;
    if(mode=="all")title=QObject::tr("All Games");
    else if(mode=="all-titles")title=QObject::tr("All Titles");
    else if(mode=="installed")title=QObject::tr("Installed Games");
    else if(mode=="family")title=QObject::tr("Family Shared");
    else if(mode.startsWith("collection:")) {
        const QString collectionId=mode.mid(QStringLiteral("collection:").size());
        for(const auto& collection:collections_)if(collection.id==collectionId){title=collection.name;break;}
        if(title.isEmpty())title=QObject::tr("Collection");
    } else title=QObject::tr("Not Installed");
    QElapsedTimer timer;timer.start();
    Page page;page.title=title;page.entries=steamFolderEntries(mode);
    if(qEnvironmentVariableIsSet("XMB_STEAM_ARTWORK_LOG"))
        qInfo()<<"steam folder"<<mode<<page.entries.size()<<"rows built in"<<timer.elapsed()<<"ms";
    // Rows without cached art ask for their store header in the background; the loaded handler
    // fills the open page, so a folder shows placeholders instead of blocking on 275 decodes.
    {
        QStringList missing;
        for(const auto& e:page.entries)
            if(e.action!=Action::SteamFolder && e.artwork.isNull())missing.push_back(e.value);
        if(!missing.isEmpty() && qEnvironmentVariable("XMB_STEAM_ARTWORK")!="0")steamArtwork_->refresh(missing);
    }
    // Verification hook: a folder open writes its own row list next to the catalogue dump.
    if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
        QJsonArray rows;
        for(const auto& e:page.entries)rows.push_back(QJsonObject{{"id",e.id},{"title",e.title},{"detail",e.detail},
            {"action",int(e.action)},{"hasArtwork",!e.artwork.isNull()}});
        QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".folder.json");
        if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"folder",mode},{"title",title},
            {"rows",rows},{"count",rows.size()}}).toJson());
    }
    // The firmware's own empty-list line (explore_plugin_full msg_error_no_content), drawn as a list row
    // in the label column rather than as a dialog (audit/DIALOGS-FIRMWARE.md D3).
    if(page.entries.isEmpty()){
        showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
        emit changed();return;
    }
    pages_.push_back(std::move(page));
    emit changed();
}
// --- Download Management --------------------------------------------------------------------------
// The firmware's Network column carries seg_dlctrl, an ese://download_control query the console's
// download plugin answers with its queue (audit/XMBML-NETWORK.md). This build has no console queue,
// but Steam writes the same facts into its own files (audit/STEAM-DOWNLOADS.md), so the row opens a
// page built from scanSteamDownloads(): one row per download in the scanner's order (active -> paused
// -> queued -> pending), with the scanner's own state text and its byte counters. Nothing is invented:
// no counters means no percentage, an empty queue says so, and a scan that cannot find Steam reports
// the scanner's error. F5 rebuilds the page in place (Shell::reloadSteamDownloads).
namespace {
QString downloadSize(qint64 bytes) {
    if(bytes<=0)return QStringLiteral("0 B");
    static const char* const units[]={"B","KB","MB","GB","TB"};
    double value=double(bytes);int unit=0;
    while(value>=1024.0 && unit<4){value/=1024.0;++unit;}
    if(unit==0)return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(QString::number(value,'f',unit>=2?1:0)).arg(QString::fromLatin1(units[unit]));
}
// The counters the scanner keeps for the phase the state names, turned into the same fraction() the
// scanner exposes: staged/toStage while staging, otherwise downloaded/toDownload. No counters means
// Steam published none for this phase, so no percentage is shown rather than a made-up zero.
QString downloadProgressText(const SteamDownload& download) {
    qint64 done=download.bytesDownloaded,total=download.bytesToDownload;
    if(download.state==QLatin1String("staging")){done=download.bytesStaged;total=download.bytesToStage;}
    if(total<=0)return QStringLiteral("no progress published");
    return QStringLiteral("%1% (%2 of %3)").arg(qRound(download.fraction()*100.0))
        .arg(downloadSize(done),downloadSize(total));
}
// The line under a row's title, as the console's own download list shows it: the scanner's state text
// plus the progress figure. An active phase's state text already ends with its percentage
// ("Downloading 43%"), so the same number is not printed twice.
QString downloadRowDetail(const SteamDownload& download) {
    const QString state=steamDownloadStateText(download);
    QString progress=downloadProgressText(download);
    const QString percent=QStringLiteral("%1%").arg(qRound(download.fraction()*100.0));
    if(state.endsWith(percent) && progress.startsWith(percent))
        progress=progress.mid(percent.size()).trimmed();
    return progress.isEmpty()?state:state+QStringLiteral(" - ")+progress;
}
// Why the row has no pause/resume control: Steam's command surface has none per title
// (audit/STEAM-CLI.md), so none is offered rather than a button that could not work.
QString downloadControlNote() {
    return QStringLiteral("Pause/resume is not supported: Steam has no per-title pause or resume command.");
}
// The information page a row opens: "Title", blank line, then the "Key: value" rows the existing
// information layout detects (src/view.cpp, drawScene).
QString downloadInformationText(const SteamDownload& download) {
    const bool staging=download.state==QLatin1String("staging");
    const qint64 done=staging?download.bytesStaged:download.bytesDownloaded;
    const qint64 total=staging?download.bytesToStage:download.bytesToDownload;
    QStringList rows;
    rows<<QStringLiteral("App: ")+download.appId;
    rows<<QStringLiteral("State: ")+steamDownloadStateText(download);
    rows<<QStringLiteral("Progress: ")+downloadProgressText(download);
    rows<<QStringLiteral("Bytes: ")+(staging?QStringLiteral("staged "):QStringLiteral("downloaded "))
        +downloadSize(done)+QStringLiteral(" of ")+downloadSize(total);
    rows<<QStringLiteral("Steam state: ")+download.state;
    rows<<QStringLiteral("Pause/resume: not supported");
    rows<<QStringLiteral("Note: Steam has no per-title pause or resume command (audit/STEAM-CLI.md)");
    return download.title+QStringLiteral("\n\n")+rows.join('\n');
}
}
QVector<Entry> Shell::steamDownloadRows(QString& error) const {
    error.clear();
    QElapsedTimer timer;timer.start();
    QVector<SteamDownload> downloads=scanSteamDownloads(QString(),&error);
    QVector<Entry> rows;
    rows.reserve(downloads.size());
    const QString cache=QDir(dataDirectory_).filePath("cache/steam");
    int decoded=0;
    for(const auto& download:downloads) {
        // A Steam download can be watched but not started, stopped, paused or resumed from here: Steam's
        // command surface has no per-title command for any of those (audit/STEAM-CLI.md), so the row
        // opens its own facts instead of a fabricated control.
        auto e=item("download:"+download.appId,download.title,"game_tex_default_ps3","explore_plugin_full",
            Action::Information);
        e.detail=downloadRowDetail(download);
        e.value=downloadInformationText(download);
        // The row icon is the app's own cached Steam header, exactly as the library folder rows get it:
        // bounded, centre-cropped to a square, and simply absent when nothing is cached.
        if(decoded<64) {
            QImageReader reader(SteamArtwork::cachePath(cache,download.appId));
            if(reader.canRead()) {
                const auto size=reader.size();
                if(size.isValid() && size.width()<=16384 && size.height()<=16384) {
                    reader.setScaledSize(size.scaled(512,288,Qt::KeepAspectRatio));
                    const auto image=reader.read();
                    if(!image.isNull()) {
                        const int side=std::min(image.width(),image.height());
                        e.artwork=image.copy((image.width()-side)/2,(image.height()-side)/2,side,side);
                        ++decoded;
                    }
                }
            }
        }
        rows.push_back(std::move(e));
    }
    const QString message=!error.isEmpty()?error:rows.isEmpty()?QObject::tr("Nothing is downloading."):QString{};
    qInfo().noquote()<<QStringLiteral("steam downloads: scanned %1 row(s) in %2 ms%3")
        .arg(downloads.size()).arg(timer.elapsed())
        .arg(error.isEmpty()?QString{}:QStringLiteral(", error: ")+error);
    // Verification hook: the download page dumps next to the catalogue and the store/folder pages
    // (<dump>.dl.json), including the honest empty and error cases so a script can check them.
    if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
        QJsonArray list;
        for(int i=0;i<downloads.size();++i) {
            const auto& download=downloads[i];
            list.push_back(QJsonObject{{"id","download:"+download.appId},{"appId",download.appId},
                {"title",download.title},{"state",download.state},{"text",steamDownloadStateText(download)},
                {"detail",downloadRowDetail(download)},{"note",downloadControlNote()},
                {"bytesDownloaded",double(download.bytesDownloaded)},
                {"bytesToDownload",double(download.bytesToDownload)},
                {"bytesStaged",double(download.bytesStaged)},{"bytesToStage",double(download.bytesToStage)},
                {"fraction",download.fraction()},{"active",download.active},
                {"paused",download.paused},{"queued",download.queued},
                {"hasArtwork",!rows.value(i).artwork.isNull()}});
        }
        QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".dl.json");
        if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"page","dl"},
            {"title",QObject::tr("Download Management")},{"rows",list},{"count",list.size()},
            {"error",error},{"message",message}}).toJson());
    }
    return rows;
}
void Shell::openSteamDownloads() {
    QString error;
    auto rows=steamDownloadRows(error);
    const QString title=QObject::tr("Download Management");
    if(!error.isEmpty()) {
        qWarning().noquote()<<"steam downloads: page unavailable:"<<error;
        showMessage(title,error,MessageKind::Page);
        emit changed();
        return;
    }
    if(rows.isEmpty()) {
        qInfo().noquote()<<"steam downloads: page empty (nothing is downloading)";
        showMessage(QObject::tr("Nothing is downloading."),MessageKind::Inline);
        emit changed();
        return;
    }
    qInfo().noquote()<<QStringLiteral("steam downloads: page opened with %1 row(s)").arg(rows.size());
    Page page;page.title=title;page.entries=std::move(rows);page.downloads=true;
    pages_.push_back(std::move(page));
    emit changed();
}
void Shell::reloadSteamDownloads() {
    if(pages_.isEmpty() || !pages_.last().downloads)return;
    const QString title=pages_.last().title;
    const QString selected=pages_.last().entries.isEmpty()?QString{}
        :pages_.last().entries[std::clamp(pages_.last().selection,0,int(pages_.last().entries.size())-1)].id;
    QString error;
    auto rows=steamDownloadRows(error);
    // A refresh replaces the page's rows with the fresh scan, never appends to them, so repeated F5
    // presses cannot grow the list. A queue that emptied meanwhile closes the page and says so rather
    // than leaving rows that no longer describe anything.
    if(!error.isEmpty() || rows.isEmpty()) {
        pages_.removeLast();
        if(error.isEmpty())showMessage(QObject::tr("Nothing is downloading."),MessageKind::Inline);
        else showMessage(title,error,MessageKind::Page);
        qInfo().noquote()<<"steam downloads: refresh emptied the page"
            <<(error.isEmpty()?QStringLiteral("(nothing is downloading)"):QStringLiteral("(error: ")+error+QStringLiteral(")"));
        emit changed();
        return;
    }
    auto& page=pages_.last();
    page.entries=std::move(rows);
    page.selection=0;
    for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==selected){page.selection=i;break;}
    qInfo().noquote()<<QStringLiteral("steam downloads: page refreshed to %1 row(s), selection %2")
        .arg(page.entries.size()).arg(page.selection);
    emit changed();
}
void Shell::refreshContent() {
    if(contentScanning_){contentRefreshPending_=true;return;}
    contentScanning_=true;
    struct Result { SteamInstallation steam; QVector<ContentApp> apps; QHash<QString,QImage> art; QString error; };
    auto* watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher]{
        auto result=watcher->result();watcher->deleteLater();contentScanning_=false;
        steamRoot_=result.steam.root;
        steamInstalled_=result.steam;
        family_=scanSteamFamily(steamInstalled_.steamId);
        { QString familyError; familyGames_=scanFamilySharedGames(dataDirectory_,&familyError);
          if(!familyError.isEmpty())qWarning().noquote()<<"steam family:"<<familyError; }
        const bool accountChanged=steamId_!=result.steam.steamId;
        steamId_=result.steam.steamId;
        if(!result.error.isEmpty()){showMessage("App Library",result.error,MessageKind::Page);result.apps=contentApps_;}
        contentApps_=result.apps;
        for(int c=0;c<categories_.size();++c) {
            auto& category=categories_[c];
            const auto selected=category.entries.isEmpty()?QString{}:category.entries[selections_[c]].id;
            category.entries.erase(std::remove_if(category.entries.begin(),category.entries.end(),[](const Entry& e){
                return e.id.startsWith("steam:") || e.id.startsWith("steam-folder-") || e.id.startsWith("steam-store") || e.id.startsWith("custom:");
            }),category.entries.end());
            QVector<Entry> providers;
            if(category.id=="game") {
                // Steam Store first, then one All Games folder carrying the whole library:
                // installed and owned titles launch or install from the collection folders on
                // its page, so the column itself no longer carries the installed rows inline.
                {
                    // Browsing the public storefront: the only place to find something not owned yet.
                    auto e=item("steam-store",QObject::tr("Steam Store"),"item_tex_ps_store","explore_plugin_full",
                        Action::SteamStoreBrowse,QString());
                    e.detail=QObject::tr("Browse and install");providers.push_back(std::move(e));
                }
                const int steamTitles=steamTitleCount();
                if(steamTitles>0) {
                    auto e=item("steam-folder-all-games",QObject::tr("All Games"),"item_tex_plain_folder",
                        "explore_plugin_full",Action::SteamFolder,"all");
                    e.detail=QString::number(steamTitles);providers.push_back(std::move(e));
                }
            }
            for(const auto& app:contentApps_)if(app.category==category.id) {
                auto e=item("custom:"+app.id,app.title,app.kind=="game"?"game_tex_default_ps3":"tex_video","explore_plugin_full",
                    app.kind=="game"?Action::LaunchApp:app.kind=="jellyfin"?Action::JellyfinApp:Action::OpenUrl,app.kind=="web"?app.url:app.id);
                // The separator is the console's U+00B7. It was double-encoded here (U+00C2 U+00B7, rendered
                // as two glyphs) until this fix; the source stays ASCII, the code point is built explicitly.
                e.detail=app.kind=="game"?QString{}:app.kind=="jellyfin"?QStringLiteral("Jellyfin"):QStringLiteral("Service app ")+QChar(0x00B7)+QStringLiteral(" Opens in your browser");
                e.artwork=result.art.value("custom:"+app.id);e.gameTile=app.kind=="game";providers.push_back(std::move(e));
            }
            category.entries=providers+category.entries;
            selections_[c]=0;
            for(int i=0;i<category.entries.size();++i)if(category.entries[i].id==selected){selections_[c]=i;break;}
            if(!contentLoaded_ && category.id=="game" && !providers.isEmpty())selections_[c]=0;
        }
        contentLoaded_=true;
        if(accountChanged){steamFriendList_.clear();friendsAge_.invalidate();rebuildFriends();}
        if(SteamFriends::available())refreshSteamFriends(false,accountChanged);
        if(stageOwnedForRefresh())refreshOwnedGames();
        QStringList missingArt;
        for(const auto& category:categories_)for(const auto& e:category.entries)
            if(e.id.startsWith("steam:") && e.artwork.isNull())missingArt.push_back(e.value);
        if(qEnvironmentVariable("XMB_STEAM_ARTWORK")!="0")steamArtwork_->refresh(missingArt);
        qInfo()<<"content: Steam installed"<<result.steam.games.size()<<"imported apps"<<contentApps_.size()<<"warnings"<<result.steam.warnings.size();
        // Verification hook: dump the rebuilt catalogue so scripted runs can be checked without
        // reading pixels (XMB_DUMP_ENTRIES=<json file>).
        if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
            QJsonArray categories;
            for(const auto& category:categories_) {
                QJsonArray entries;
                for(const auto& e:category.entries) {
                    // `container`/`texture` travel with every row: the row-art pass's binding (Game rows from
                    // category_game.xml's icon_rsc, media rows from their column's RCO) is then checkable from
                    // this dump instead of from pixels.
                    QJsonObject row{{"id",e.id},{"title",e.title},{"detail",e.detail},
                        {"action",int(e.action)},{"value",e.value},{"hasArtwork",!e.artwork.isNull()},
                        {"container",e.container},{"texture",e.texture}};
                    if(e.iconSized)row.insert("iconSized",true);
                    // Only generated rows carry an origin, so the dump stays quiet about the rest.
                    if(!e.source.isEmpty())row.insert("source",e.source);
                    entries.push_back(row);
                }
                categories.push_back(QJsonObject{{"id",category.id},{"entries",entries},{"count",entries.size()}});
            }
            QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES"));
            const auto bytes=QJsonDocument(QJsonObject{{"categories",categories},{"owned",ownedGames_.size()},
                {"collections",collections_.size()},{"installedSteam",result.steam.games.size()},
                {"importedApps",contentApps_.size()}}).toJson();
            if(file.open(QIODevice::WriteOnly))file.write(bytes);
        }
        emit changed();
        if(contentRefreshPending_){contentRefreshPending_=false;QTimer::singleShot(0,this,&Shell::refreshContent);}
    });
    const QString data=dataDirectory_;
    // App ids whose row artwork may come from a user override; captured for the worker.
    QStringList ownedIds;
    ownedIds.reserve(ownedGames_.size());
    for(const auto& game:ownedGames_)ownedIds.push_back(game.appId);
    watcher->setFuture(QtConcurrent::run([data,ownedIds]{
        Result result;result.steam=scanSteam();
        ContentRegistry::load(QDir(data).filePath("apps.json"),result.apps,result.error);
        const auto read=[&](const QString& key,const QString& path){
            if(path.isEmpty() || result.art.size()>=128 || result.art.contains(key))return;
            QImageReader reader(path);reader.setDecideFormatFromContent(true);reader.setAutoTransform(true);
            const auto size=reader.size();if(!size.isValid() || size.width()>16384 || size.height()>16384)return;
            reader.setScaledSize(size.scaled(512,288,Qt::KeepAspectRatio));
            const auto image=reader.read();if(!image.isNull())result.art.insert(key,image);
        };
        const QString cache=QDir(data).filePath("cache/steam");
        for(const auto& game:result.steam.games) {
            // A user override wins over the cached header and the manifest icon.
            const QString override=steamArtworkOverride(data,game.appId).row;
            if(!override.isEmpty() && qEnvironmentVariableIsSet("XMB_STEAM_ARTWORK_LOG"))qInfo().noquote()<<"artwork override"<<game.appId<<override;
            read("steam:"+game.appId,!override.isEmpty()?override:(game.artwork.isEmpty()?SteamArtwork::cachePath(cache,game.appId):game.artwork));
        }
        for(const auto& appId:ownedIds) {
            const QString override=steamArtworkOverride(data,appId).row;
            if(!override.isEmpty())read("steam:"+appId,override);
            if(!override.isEmpty() && qEnvironmentVariableIsSet("XMB_STEAM_ARTWORK_LOG"))qInfo().noquote()<<"artwork override"<<appId<<override;
        }
        for(const auto& app:result.apps)read("custom:"+app.id,app.artwork);
        return result;
    }));
}
void Shell::importApps(const QString& file) {
    if(file.isEmpty())return;
    QString error;
    if(!ContentRegistry::importFile(file,QDir(dataDirectory_).filePath("apps.json"),error))showMessage("Import Games and Apps",error,MessageKind::Page);
    else refreshContent();
    emit changed();
}
void Shell::refreshSteamFriends(bool showErrors,bool force) {
    if(friendsRefreshing_ && !force)return;
    showFriendsErrors_=showErrors;friendsRefreshing_=true;steamFriendsError_=QStringLiteral("Refreshing Steam friends")+QChar(0x2026);rebuildFriends();emit changed();
    steamFriends_.refresh(qEnvironmentVariable("XMB_STEAM_ID",steamId_));
}
void Shell::rebuildFriends() {
    for(int c=0;c<categories_.size();++c)if(categories_[c].id=="friends") {
        auto& entries=categories_[c].entries;
        const QString selected=entries.isEmpty()?QString{}:entries[std::clamp(selections_[c],0,int(entries.size())-1)].id;
        const bool hadFriends=std::any_of(entries.cbegin(),entries.cend(),[](const Entry& e){return e.id.startsWith("steam-friend:");});
        entries.clear();
        for(const auto& person:steamFriendList_) {
            auto e=item("steam-friend:"+person.steamId,person.name,"tex_Avatar_Default","explore_category_friend",Action::OpenUrl,"steam://friends/message/"+person.steamId);
            static const QStringList states{"Offline","Online","Busy","Away","Snooze","Looking to trade","Looking to play"};
            e.detail=person.game.isEmpty()?states.value(person.personaState,"Online"):"Playing "+person.game;
            e.friendRow=true;e.presenceState=person.game.isEmpty()?person.personaState:7;entries.push_back(std::move(e));
        }
        auto open=item("steam-friends:open","Open Steam Friends","item_tex_NewFriend","explore_category_friend",Action::OpenUrl,"steam://open/friends");
        open.detail=steamFriendsError_.isEmpty() && steamFriendList_.isEmpty()?"Your Steam friends list is empty.":steamFriendsError_;entries.push_back(std::move(open));
        entries.push_back(item("steam-friends:refresh","Refresh Steam Friends","item_tex_Players","explore_category_friend",Action::RefreshFriends));
        const QString setup="Steam Friends\n\nFriends and presence come from Steam.\nThe signed-in Steam client supplies your friends without changing profile privacy.\nThe Web API is an optional fallback.\nYour most recent local Steam account is selected automatically.\nUse XMB_STEAM_ID to select another numeric Steam ID.\nThe API key is never stored in XMB settings.\nPrivate lists and unavailable connections are reported here.";
        entries.push_back(item("steam-friends:setup","Steam Connection","item_tex_Profile_Onlines","explore_category_friend",Action::Information,setup));
        selections_[c]=0;
        if(hadFriends || steamFriendList_.isEmpty())for(int i=0;i<entries.size();++i)if(entries[i].id==selected){selections_[c]=i;break;}
    }
}
Shell::Shell(QString dataDirectory,Assets& assets,QObject* parent):QObject(parent),
    assets_(assets),preferences_(dataDirectory),library_(),interactionAudio_(assets.root()),profiles_(dataDirectory),
    dataDirectory_(dataDirectory),
    steamArtwork_(std::make_unique<SteamArtwork>(QDir(dataDirectory).filePath("cache/steam"))),
    player_(std::make_unique<QMediaPlayer>()),audio_(std::make_unique<QAudioOutput>()),video_(std::make_unique<QVideoSink>()) {
    QDir().mkpath(dataDirectory);
    // The resume-point format change (audit/MEDIA-CONTROLS.md): a data directory written by the earlier
    // build holds {id: milliseconds} with no path, so those entries are resolved or dropped here, once,
    // before anything can list or resume them.
    migrateResumePoints();
    const QString sys="explore_category_sysconf",full="explore_plugin_full";
    const auto friendText=[&assets](const QString& id,const QString& fallback){return assets.text("explore_category_friend",id,fallback);};
    categories_={
        {"user","Users",full,"user_tex_load",{item("new-user","Create New User","item_tex_NewUser"),item("user","User","user_tex_login")}},
        {"settings","Settings","qgl/icontex","2n",{
            item("update","System Update","tex_update",sys,Action::SettingsPage,"update"),item("game","Game Settings","tex_game",sys,Action::SettingsPage,"game"),
            item("video","Video Settings","tex_bddvd",sys,Action::SettingsPage,"video"),item("music","Music Settings","tex_music",sys,Action::SettingsPage,"music"),
            item("chat","Chat Settings","tex_chat",sys,Action::SettingsPage,"chat"),item("system","System Settings","tex_console",sys,Action::SettingsPage,"system"),
            item("theme","Theme Settings","tex_thema",sys,Action::SettingsPage,"theme"),item("date","Date and Time Settings","tex_datetime",sys,Action::SettingsPage,"date"),
            item("power","Power Save Settings","tex_power",sys,Action::SettingsPage,"power"),item("device","Accessory Settings","tex_pd",sys,Action::SettingsPage,"device"),
            item("print","Printer Settings","tex_printer",sys,Action::SettingsPage,"print"),item("display","Display Settings","tex_display",sys,Action::SettingsPage,"display"),
            item("sound","Sound Settings","tex_sound",sys,Action::SettingsPage,"sound"),item("security","Security Settings","tex_security",sys,Action::SettingsPage,"security"),
            item("remote","Remote Play Settings","tex_remote",sys,Action::SettingsPage,"remote"),item("network","Network Settings","tex_network",sys,Action::SettingsPage,"network")}},
        // ---- The three media columns ------------------------------------------------------------------
        // The console's own row sets, in its own order, then this build's host-folder rows LAST and marked
        // native (Entry::source = "native"). FIDELITY-MEDIA V1/M1/P1 measured 0 of the console's 8 media-column
        // rows in this build: Video is BD Data Utility / Search for Media Servers / Video Editor & Uploader,
        // Music is Search for Media Servers / Playlists and Photo is Search for Media Servers / Photo Gallery /
        // Playlists, from category_video|music|photo.xml's root views. resolveMediaColumns() re-reads those
        // definitions on every start and rewrites these rows from them (titles through the column's own RCO,
        // icons from the definition's icon_rsc where it carries one); this table is both the fallback for a
        // flash image without them and the declaration of the native rows. Order is the whole point, so the
        // console's rows come first even in the fallback.
        {"photo","Photo","qgl/icontex","3n",{
            item("dlna-scan","Search for Media Servers","item_tex_dlna_scan","explore_category_photo"),
            item("gallery","Photo Gallery","","file"),
            item("playlists","Playlists","tex_playlist","explore_category_photo"),
            // Native rows (FIDELITY-MEDIA P1's "10 native-only rows"): the library folder, the one settings page
            // that has no other surface, and the folder picker. They stay because each is a feature's only door.
            item("library","Photo Library","photo_tex_album_pic",full,Action::Library),
            item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary),
            item("options","Photo Settings","tex_photo",full,Action::SettingsPage,"photo")}},
        {"music","Music","qgl/icontex","4n",{
            item("dlna-scan","Search for Media Servers","item_tex_dlna_scan","explore_category_music"),
            item("playlists","Playlists","tex_playlist","explore_category_music"),
            // Native rows: the library and the picker. Music Settings is dropped from this column because the
            // Settings column's own "Music Settings" row opens the same page (id "music", Action::SettingsPage).
            item("library","Music Library","music_tex_album_pic",full,Action::Library),
            item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"video","Video","qgl/icontex","5n",{
            item("bddata","BD Data Utility","item_tex_plain_folder","explore_category_video"),
            item("dlna-scan","Search for Media Servers","item_tex_dlna_scan","explore_category_video"),
            item("video-editor","Video Editor & Uploader","item_tex_upload","explore_category_video"),
            // Native rows. Continue Watching is the surface a couch user wants first and the one row that works
            // before any library folder has been chosen; Action::Library is the existing "open a collection of
            // this column's own content" action and Shell::activate() recognises this row by id and builds the
            // page from the remembered resume points instead of reading a folder (audit/MEDIA-CONTROLS.md).
            // Video Settings is dropped here for the same reason as Music Settings above.
            item("continue","Continue Watching","video_tex_album_default",full,Action::Library),
            item("library","Video Library","video_tex_album_default",full,Action::Library),
            item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"tv","TV/Video Services","qgl/icontex","35n", /* normal-mapped like the others; tex_video is 46 px */{item("services","My Channels","tex_video")}},
        {"game","Game","qgl/icontex","6n",{
            // Row art is the firmware's own `icon_rsc` per segment view of category_game.xml, read this pass.
            // Five of the seven rows bound a texture the definition does not name there (FIDELITY-SURFACES
            // 3.1b, top gap 2): the Vita and Game Data rows are the folder-pixmap placeholder `tex_album_icon`
            // (a plain folder - the same art the folder rows share, NOT item_tex_vitaformat's 110x28 wordmark and
            // not a folder with a gamepad), and the three Saved Data rows take the two utility folders the
            // definition names - `item_tex_ps3util` (folder + gamepad) on seg_sdps2 and seg_sdps3, and
            // `item_tex_psputil` (folder + card slot) on seg_sdpsp. item_tex_psputil lives in
            // explore_category_game.rco, so its row carries that container. The resolved table is logged and
            // dumped (resolveGameColumnProof below is the same list the audit's table cites).
            item("vita","PS Vita System Application Utility","tex_album_icon","explore_plugin_full"),item("data","Game Data Utility","tex_album_icon","explore_plugin_full"),
            item("memory","Memory Card Utility","item_tex_ps12util","explore_plugin_full"),item("ps2","Saved Data Utility (PS2)","item_tex_ps3util","explore_plugin_full"),
            item("psp","Saved Data Utility (PSP)","item_tex_psputil","explore_category_game"),item("ps3","Saved Data Utility (PS3)","item_tex_ps3util","explore_plugin_full"),
            item("store","PlayStation Store","item_tex_ps_store","explore_plugin_full")}},
        {"network","Network","qgl/icontex","7n",{
            item("manual","Online Instruction Manuals","tex_onlinemanual","explore_category_network"),
            item("remote","Remote Play","tex_premo","explore_category_network"),
            item("browser","Internet Browser","tex_browser","explore_category_network"),
            item("search","Internet Search","tex_kensaku","explore_category_network"),
            item("downloads","Download Management","tex_dl_manage","explore_category_network",Action::SteamDownloads)}},
        {"psn","PlayStation Network","xmb_plugin_normal","tex_psn", /* 128x128; explore_plugin_full holds a 46x46 copy */{item("signin","Sign In","tex_Sign_In"),item("trophy","Trophy Collection","item_tex_trophy"),item("store","PlayStation Store","item_tex_ps_store","explore_plugin_full")}},
        {"friends","Friends","qgl/icontex","8n",{
            // category_friend.xml: items, icons and titles are the firmware's (explore_category_friend); PSN-backed
            // features answer with the firmware's own empty-state messages.
            item("add",friendText("msg_friend_register","Add a Friend"),"item_tex_NewFriend","explore_category_friend"),
            item("players",friendText("msg_play_with","Players Met"),"item_tex_Players","explore_category_friend",Action::Information,friendText("msg_error_no_player","No players were found.")),
            item("chatroom",friendText("msg_text_chatroom","Chat Room (Text Only)"),"item_tex_ChatRoomText","explore_category_friend"),
            item("chat",friendText("msg_start_new_chat2","Start New Chat"),"item_tex_NewAvc","explore_category_friend"),
            item("messages",friendText("msg_msg_box","Message Box"),"item_tex_Messages","explore_category_friend",Action::SettingsPage,"messages"),
            item("block",friendText("msg_ignore_list","Block List"),"item_tex_IgnoreList","explore_category_friend",Action::Information,friendText("msg_error_not_registered","No persons have been added."))}}
    };
    // The Network column's rows come from the firmware's own category_network.xml (the proof of the
    // XMBML-driven column mechanism); it keeps the list above when the definition cannot be read.
    resolveNetworkColumn();
    // The three media columns are the same mechanism (category_video|music|photo.xml): the console's own rows
    // in its own order, then this build's native rows marked as such. See resolveMediaColumns().
    resolveMediaColumns();
    // The Game column's row art, as one line per row: the texture each row binds and the container it is read
    // from. Five of the seven rows changed binding this pass (category_game.xml's icon_rsc), so the log is the
    // record a scripted run greps instead of reading pixels (FIDELITY-SURFACES 3.1b).
    for(const auto& category:categories_)if(category.id==QStringLiteral("game")) {
        QStringList bound;
        for(const auto& entry:category.entries)bound<<entry.id+QStringLiteral("=")+entry.container+QLatin1Char(':')+entry.texture;
        qInfo().noquote()<<"xmbml: game column row art ("<<bound.size()<<" rows)"<<bound.join(QStringLiteral(" "));
    }
    // The Settings column is the firmware's own definition (Phase 11.1, audit/SETTINGS-FIRMWARE.md
    // 1.1-1.2): category_sysconf.xml's root view names #seg_fixed_items, whose 18 tables give the
    // column's rows in document order. The 16 rows a retail console shows keep that order, with the
    // RCO's own title, info line and tex_* icon; edy and console_qa are omitted with a logged reason,
    // and the three child=segment rows lose their second label line (fo_lbl2_alpha=0). The resolver
    // keeps each id's native action, value and setting, so every page and theme icon still applies.
    if(const auto rows=settingsColumnEntries(categories_[1].entries,assets_);!rows.isEmpty())
        categories_[1].entries=rows;
    else qWarning().noquote()<<"xmbml: settings column keeps its built-in rows";
    // The resolved tex_* name is in Entry::texture and in the entry dump, but this renderer draws the
    // named RCO icon through drawItemGlyph's white item-glyph material, while the frame's brass settings
    // icon is the qgl icon program addressed by numeric id. Until the icon program takes a named RCO
    // texture (Phase 11.4/13), these ids keep the frame's material instead of a white outline.
    // QGL normal maps carry the original embossed silhouettes. IDs were checked
    // against a contact sheet and the user's reference recording; unknown shapes
    // keep their named RCO artwork rather than guessing a numeric ID.
    const QMap<QString,int> normalMaps{{"update",9},{"game",14},{"video",10},{"music",12},
        {"system",13},{"theme",24},{"date",15},{"power",57},{"device",16},{"display",17},
        {"sound",18},{"security",19},{"remote",21},{"network",20}};
    for(auto& entry:categories_[1].entries)if(normalMaps.contains(entry.id)) {
        entry.container="qgl/icontex";entry.texture=QString::number(normalMaps[entry.id])+"n";
    }
    // The icons a PS3 theme can replace, by its icontable ids (the firmware's own theme, dev_flash
    // vsh/resource/theme/01.p3t, carries all of these). Media libraries and folders keep the system's pictures.
    static const QHash<QString,QString> themeCategoryIcons{{"user","icon_user"},{"settings","icon_setting"},{"photo","icon_photo"},
        {"music","icon_music"},{"video","icon_video"},{"tv","icon_tv"},{"game","icon_game"},{"network","icon_network"},{"friends","icon_friend"}};
    static const QHash<QString,QString> themeEntryIcons{
        {"settings/update","icon_update"},{"settings/game","icon_game_setting"},{"settings/video","icon_bdvd_setting"},
        {"settings/music","icon_music_setting"},{"settings/chat","icon_chat_setting"},{"settings/system","icon_system_setting"},
        {"settings/theme","icon_theme_setting"},{"settings/date","icon_datetime_setting"},{"settings/power","icon_powersave_setting"},
        {"settings/device","icon_accessory"},{"settings/print","icon_printer_setting"},{"settings/display","icon_display_setting"},
        {"settings/sound","icon_sound_setting"},{"settings/security","icon_security_setting"},{"settings/remote","icon_remoteplay_setting"},
        {"settings/network","icon_network_setting"},{"music/options","icon_music_setting"},{"video/options","icon_bdvd_setting"},
        {"game/data","icon_gamedata"},{"game/memory","icon_vmc"},{"game/ps3","icon_savedata"},{"game/psp","icon_savedata_minis"},
        {"network/manual","icon_onlinemanual"},{"network/premo","icon_remoteplay"},{"network/browser","icon_browser"},
        {"network/kensaku","icon_inet_search"},{"network/dlctrl","icon_download"},{"psn/trophy","icon_trophy"},
        {"friends/add","icon_addfriend"},{"friends/players","icon_playermet"},{"friends/chatroom","icon_chatroom_text"},
        {"friends/chat","icon_chat"},{"friends/messages","icon_mbox"},{"friends/block","icon_blocklist"}};
    for(auto& category:categories_) {
        category.themeIcon=themeCategoryIcons.value(category.id);
        for(auto& entry:category.entries)entry.themeIcon=themeEntryIcons.value(category.id+"/"+entry.id);
    }
    selections_.fill(0,categories_.size());
    // The tenth column (FIDELITY-ROOT-BAR C1/#5): built like every other category above, then dropped here when
    // `appearance/tvColumn` is off, so the bar, the navigation clamps and the entry dump all follow categories_.
    syncTvColumn();
    rebuildUsers();
    addInstalledApps();
    connect(&steamFriends_,&SteamFriends::loaded,this,[this](const SteamFriendList& friends,const QString& error){
        steamFriendList_=friends;steamFriendsError_=error;friendsRefreshing_=false;rebuildFriends();emit changed();
        if(error.isEmpty())friendsAge_.restart();
        if(showFriendsErrors_ && !error.isEmpty()){showMessage("Steam Friends",error,MessageKind::Page);emit changed();}
        showFriendsErrors_=false;
    });
    connect(&steamOwned_,&SteamOwned::loaded,this,[this](const OwnedGameList& games,const QString& error,bool needsKey){
        ownedInFlight_=false;
        if(!error.isEmpty()){if(needsKey)qInfo()<<"steam owned: no API key configured";else qWarning().noquote()<<"steam owned:"<<error;return;}
        QString signature;
        for(const auto& game:games)signature+=game.appId+(game.installed?QStringLiteral("+") :QStringLiteral("-"));
        QString collectionsError;
        const auto collections=scanSteamCollections(steamId_,&collectionsError);
        for(const auto& collection:collections)signature+=collection.id;
        if(signature==ownedSignature_)return; // nothing changed: keep the current column
        ownedSignature_=signature;
        ownedGames_=games;
        collections_=collections;
        qInfo()<<"steam owned:"<<games.size()<<"games in"<<collections.size()<<"collections";
        refreshContent();
    });
    connect(steamArtwork_.get(),&SteamArtwork::loaded,this,[this](const QString& id,const QImage& image){
        bool touched=false;
        const auto apply=[&](Entry& e){if(e.id=="steam:"+id){e.artwork=image;touched=true;}};
        for(auto& category:categories_)for(auto& e:category.entries)apply(e);
        // A Steam library folder may be the open page: fill its rows too, so a folder opens with
        // placeholders and the cached headers arrive without a second visit.
        for(auto& page:pages_)for(auto& e:page.entries)apply(e);
        if(!touched)return;
        emit changed();
    });
    // Steam Store: the browse page is an ordinary XMB page, and every row hands off to Steam for
    // the actual purchase/install. No store page is fabricated while a request is in flight.
    connect(&steamStore_,&SteamStore::loaded,this,[this](const StoreItemList& items,const QString& query,const QString& error){
        if(!error.isEmpty()){showMessage(QObject::tr("Steam Store"),error,MessageKind::Page);emit changed();return;}
        Page page;page.title=query.isEmpty()?QObject::tr("Steam Store"):query;
        for(const auto& row:items) {
            auto e=item("store:"+row.appId,row.title,"game_tex_default_ps3","explore_plugin_full",
                Action::InstallSteam,row.appId);
            const QString price=SteamStore::priceText(row);
            e.detail=price.isEmpty()?QObject::tr("Open in Steam"):price;
            page.entries.push_back(std::move(e));
        }
        if(page.entries.isEmpty()){
            showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
            emit changed();return;
        }
        // Verification hook: the store page is dumped next to the catalogue for scripted checks.
        if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
            QJsonArray rows;
            for(const auto& e:page.entries)rows.push_back(QJsonObject{{"id",e.id},{"title",e.title},
                {"detail",e.detail},{"action",int(e.action)}});
            QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".store.json");
            if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"title",page.title},
                {"rows",rows},{"count",rows.size()}}).toJson());
        }
        pages_.push_back(std::move(page));
        emit changed();
    });
    connect(&steamStore_,&SteamStore::busyChanged,this,[this](bool busy){
        status_=busy?QObject::tr("Asking the Steam store..."):QString();
        emit changed();
    });
    connect(&steamRefresh_,&QTimer::timeout,this,[this]{
        if(categories_[category_].id=="friends" && SteamFriends::available())refreshSteamFriends();
    });
    steamRefresh_.start(60000);
    steamWatch_.setInterval(1000);
    connect(&steamWatch_,&QTimer::timeout,this,&Shell::pollSteamGame);
    steamFriendsError_="Connect the signed-in Steam client, or open the Steam friends window.";
    rebuildFriends();
    for(auto& category:categories_)if(category.id=="game") {
        category.entries.push_back(item("content:refresh","Refresh Game Library","item_tex_plain_folder",full,Action::RefreshContent));
        category.entries.push_back(item("content:import","Import Games and Apps","item_tex_plain_folder",full,Action::ImportApps));
    }
    QTimer::singleShot(0,this,&Shell::refreshContent);
    connect(&profiles_,&Profiles::changed,this,[this]{rebuildUsers();emit changed();});
    connect(&profiles_,&Profiles::failed,this,[this](const QString& error){showMessage(error,MessageKind::Page);emit changed();});
    player_->setAudioOutput(audio_.get());player_->setVideoSink(video_.get());
    connect(&preferences_,&Preferences::failed,this,[this](const QString& error){
        // The store reports its reason on this signal and only keeps it in error() for the read/format cases,
        // so the chain records the latest one: a refused value ("The setting value is invalid.") arrives here.
        lastPreferencesError_=error;showMessage(error,MessageKind::Page);emit changed();});
    connect(&preferences_,&Preferences::changed,this,[this]{applyPreferences();emit changed();});
    // The store's documented profile switch (preferences.h). Re-applying the settings here is what makes
    // a user switch a session change rather than an activeId rewrite (BEHAVIOUR-GAP-AUDIT D16): the
    // volume, key tones, theme, slideshow interval and repeat/shuffle all come from the new profile.
    connect(&preferences_,&Preferences::profileChanged,this,[this](const QString& id){
        applyPreferences();
        qInfo().noquote()<<"shell: profile changed to"<<id;
        emit changed();
    });
    connect(&library_,&MediaLibrary::loaded,this,[this](quint64 request,const QString& path,const MediaEntries& files,const QString& error,bool truncated){
        const bool refreshed=directoryRefresh_;directoryRefresh_=false;
        if(request!=scanRequest_ || pages_.isEmpty())return;
        loading_=false;scanRequest_=0;
        qInfo().noquote()<<QStringLiteral("busy: page_busy released (list scan landed) rows=%1 page=\"%2\"")
            .arg(files.size()).arg(QDir::toNativeSeparators(path));
        auto& page=pages_.last();
        const QString previous=page.entries.isEmpty()?QString{}:page.entries[page.selection].id;
        page.directory=path;page.entries.clear();page.selection=0;
        const QString kind=categories_[category_].id;
        for(const auto& file:files) {
            page.entries.push_back(item(file.path,file.title,file.isDirectory?"item_tex_plain_folder":kind=="photo"?"photo_tex_album_pic":kind=="music"?"music_tex_album_pic":"video_tex_album_default","explore_plugin_full",file.isDirectory?Action::Folder:Action::File,file.path));
            auto& entry=page.entries.last();
            entry.detail=file.isDirectory?QString{}:QString::number(file.size/1024)+QStringLiteral(" KB  ")+QChar(0x00B7)+QStringLiteral("  ")+file.modified.toString("yyyy/MM/dd");
            if(entry.id==previous)page.selection=int(page.entries.size())-1;
        }
        // An open page with no rows is the firmware's inline empty state: the line sits in the list's own
        // label column, not in a dialog and not in a status line under the page (D3, frame 14.4 s).
        status_=truncated?"Showing the first 5,000 matching items.":QString{};
        if(!error.isEmpty())showMessage(error,MessageKind::Page);
        else if(page.entries.isEmpty())
            showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
        else clearMessage();
        qInfo()<<"media: directory result"<<files.size()<<"entries; truncated"<<truncated<<"error"<<!error.isEmpty();
        // A rescan of an open media library (F5 / Refresh Directory) is a real library event: the console
        // reports finished library work in the notification pop-up.
        if(refreshed && error.isEmpty())
            notify(tr("%1 Library updated").arg(categories_[category_].title),
                   files.size()==1?tr("1 item"):tr("%1 items").arg(files.size()),
                   kind=="music"?"tex_notification_music":"tex_notification_info");
        emit changed();
    });
    connect(&slideshow_,&QTimer::timeout,this,[this]{stepPhoto(1);});
    connect(player_.get(),&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus state){
        if(mediaOpen_ && !photoOpen()) {
            loading_=state==QMediaPlayer::LoadingMedia || state==QMediaPlayer::StalledMedia;
            // A saved resume point is applied once the media reports LoadedMedia, not before.
            if(state==QMediaPlayer::LoadedMedia && resumePending_>0 && !jellyfinQueue_) {
                player_->setPosition(resumePending_);
                qInfo()<<"media: resumed at"<<resumePending_<<"ms";
                resumePending_=0;
            }
            if(state==QMediaPlayer::LoadedMedia && !jellyfinQueue_) {
                const auto tags=player_->metaData();const auto title=tags.value(QMediaMetaData::Title).toString().trimmed();
                const auto artist=tags.value(QMediaMetaData::ContributingArtist).toString().trimmed();
                mediaAlbum_=tags.value(QMediaMetaData::AlbumTitle).toString().trimmed();
                if(!title.isEmpty()){mediaTitle_=artist.isEmpty()?title:title+" - "+artist;mediaTags_[mediaQueue_.currentIndex()]=mediaTitle_;}
            }
            if(state==QMediaPlayer::LoadedMedia) {
                // Sidecar subtitles belong to a local video file: the Video column's own library. Streamed
                // Jellyfin media has no directory of the user's beside it, and a photo never reaches the
                // player, so both clear the state rather than showing a previous file's track.
                // The guard on the path matters: the ffmpeg backend reports LoadedMedia again on every
                // seek (QFFmpegMediaPlayer::setPosition), and re-running discovery there would re-parse
                // the sidecars and forget a refused audio track on each Left/Right.
                const bool local=categories_[category_].id=="video" && !jellyfinQueue_;
                const QString path=local?player_->source().toLocalFile():QString{};
                if(path!=subtitleMediaPath_) {
                    prepareSubtitles(path);
                    refreshAudioTracks();
                }
            }
            if(state==QMediaPlayer::EndOfMedia) {
                clearResumePosition(resumePath_);   // a finished file starts from the beginning next time
                const auto request=mediaRequest_;QTimer::singleShot(0,this,[this,request]{if(request==mediaRequest_ && mediaOpen_ && !photoOpen())advanceMedia(false);});
            }
        }
        emit changed();
    });
    connect(&idle_,&QTimer::timeout,this,[this]{
        // The host power plan must not end the evening in the middle of a film. One place asks for the
        // display-awake hold and drops it again, so every way playback stops is covered.
        // The player's own state decides this, not mediaOpen_: the overlay stays open (paused at 0:00) after a
        // title reaches its end, and holding on that would keep the display awake until Back. A photo slideshow
        // does not hold it, since the XMB is expected to dim over photos.
        const bool playing=mediaOpen_ && !photoOpen() && player_->playbackState()!=QMediaPlayer::StoppedState;
        const bool active=playing || gameRunning();
        if(active!=sessionHold_)sessionHold_=host::holdDisplayAwake(active);
        const int minutes=preferences_.value("power/idleMinutes").toInt();
        // Never close the XMB while a title it launched is still running.
        if(minutes && !mediaOpen_ && !gameRunning() && !QApplication::activeModalWidget() && activity_.elapsed()>=qint64(minutes)*60000)emit requestQuit();
    });
    activity_.start();idle_.start(1000);applyPreferences();
    if(!preferences_.error().isEmpty())showMessage(preferences_.error(),MessageKind::Page);
    if(!profiles_.error().isEmpty())showMessage(profiles_.error(),MessageKind::Page);
    connect(video_.get(),&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame& frame){emit mediaFrame(frame.toImage());});
    connect(player_.get(),&QMediaPlayer::positionChanged,this,[this](qint64 at){if(at>0)mediaFailures_=0;});
    connect(player_.get(),&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,const QString& error){
        if(!mediaOpen_ || photoOpen())return;
        const auto request=mediaRequest_;
        QTimer::singleShot(0,this,[this,request,error]{if(request==mediaRequest_ && mediaOpen_ && !photoOpen())advanceMedia(true,error);});
    });
    // The notification queue runs on its own monotonic clock so a pop-up expires on real time whatever the
    // render clock or the host frame rate is. 100 ms is fine for a 4 s lifetime and costs nothing idle.
    notificationLog_=qEnvironmentVariableIsSet("XMB_NOTIFY_LOG");
    notificationClock_.start();
    notificationTimer_.setInterval(100);
    connect(&notificationTimer_,&QTimer::timeout,this,&Shell::expireNotifications);
    // Sidecar subtitle work is quiet by default; XMB_SUBTITLE_LOG=1 adds discovery, parse, cue and
    // track-switch lines (audit/SUBTITLES.md). A sidecar this build cannot read is always named, because
    // a file the user put next to the film and that silently does nothing is the report this answers.
    subtitleLog_=qEnvironmentVariableIsSet("XMB_SUBTITLE_LOG");
    // The box's answer row is quiet by default; XMB_DIALOG_LOG=1 names each event (raised, moved, answered,
    // cancelled) so a scripted run can prove which choice it made without reading pixels (audit section 11).
    dialogLog_=qEnvironmentVariableIsSet("XMB_DIALOG_LOG");
    initJellyfin();
    // The sign-in surface is this shell's own page, not a modal dialog: openJellyfin() emits
    // requestJellyfinSignIn, which used to reach a QDialog in the view. Connecting it here keeps one sign-in
    // surface and leaves jellyfin_shell.cpp (outside this change's fence) as the owner of the app entry.
    connect(this,&Shell::requestJellyfinSignIn,this,&Shell::openJellyfinSignInPage);
    connect(this,&Shell::jellyfinSignInFinished,this,[this](bool ok,const QString& error){
        if(ok) {
            qInfo()<<"jellyfin: sign-in accepted";
            signInError_.clear();status_.clear();closeJellyfinSignInPage();
            emit changed();
            return;
        }
        // The client's own message, shown on the page instead of a throwaway dialog line.
        signInError_=error.isEmpty()?QObject::tr("Jellyfin sign-in failed."):error;
        status_=signInError_;
        qWarning().noquote()<<"jellyfin: sign-in failed:"<<signInError_;
        refreshJellyfinSignInPage();emit changed();
    });
}
Shell::~Shell(){host::holdDisplayAwake(false);jellyfinRequest_=0;finishJellyfinPlayback();jellyfin_.signOut();disconnect(&steamFriends_,nullptr,this,nullptr);disconnect(steamArtwork_.get(),nullptr,this,nullptr);}
// --- Notifications --------------------------------------------------------------------------------
// One pop-up on screen at a time; the rest wait in order. The lifetime starts when an entry reaches the
// front, so a burst of events each gets its own 4 s instead of the last one starving the others.
void Shell::notify(const QString& title,const QString& detail,const QString& icon) {
    if(title.isEmpty())return;
    static const QStringList firmwareIcons{
        "tex_notification","tex_notification_caution","tex_notification_friend","tex_notification_headset",
        "tex_notification_info","tex_notification_keypad","tex_notification_mediasever","tex_notification_music",
        "tex_notification_psbutton_insensitive","tex_notification_settings","tex_notification_trophy_bronze",
        "tex_notification_trophy_gold","tex_notification_trophy_platinum","tex_notification_trophy_silver"};
    // Accept either the full firmware texture name or just its suffix ("trophy_gold"); anything else falls
    // back to the plain information glyph rather than asking the asset service for a texture that is not there.
    QString texture=icon.trimmed();
    if(!texture.isEmpty() && !texture.startsWith("tex_notification"))
        texture="tex_notification_"+texture;
    if(!firmwareIcons.contains(texture))texture="tex_notification_info";
    if(notifications_.size()>=notificationQueueLimit) {
        // Drop the arriving event, not the pop-up already on screen: cutting a visible notification short
        // would be a worse lie than losing the newest one under a burst.
        if(notificationLog_)
            qInfo().noquote()<<"notify: drop"<<title<<(detail.isEmpty()?QString():QString("| ")+detail)
                <<"(queue full,"<<notificationQueueLimit<<"shown or waiting)";
        return;
    }
    notifications_.push_back(Notification{title,detail.isEmpty()?QString{}:detail,texture,0});
    if(notificationLog_)
        qInfo().noquote()<<"notify: enqueue"<<title<<(detail.isEmpty()?QString():QString("| ")+detail)
            <<texture<<"queue"<<notifications_.size()<<"/"<<notificationQueueLimit;
    if(notifications_.size()==1)promoteNotification();
    emit changed();
}
void Shell::promoteNotification() {
    if(notifications_.isEmpty()){notificationTimer_.stop();return;}
    auto& front=notifications_.first();
    front.expiresAt=notificationClock_.elapsed()+notificationLifetimeMs;
    if(!notificationTimer_.isActive())notificationTimer_.start();
    // Firmware: the trophy notification carries sound "snd_trophy" (np_trophy_util.prx builds the
    // descriptor with plugin=system_plugin, sound=snd_trophy; audit/INTERACTION-AUDIO.md section 10).
    if(front.icon.startsWith("tex_notification_trophy_"))
        interactionAudio_.play(InteractionAudio::Cue::Trophy);
    if(notificationLog_)qInfo().noquote()<<"notify: show"<<front.title<<front.icon<<notificationLifetimeMs<<"ms";
}
void Shell::expireNotifications() {
    // Exactly one entry per tick: a queued entry carries no expiry until it reaches the front, so a loop that
    // popped every due entry would drop the whole queue the moment the first one timed out.
    if(notifications_.isEmpty()){notificationTimer_.stop();return;}
    if(notificationClock_.elapsed()<notifications_.first().expiresAt)return;
    const auto done=notifications_.takeFirst();
    if(notificationLog_)qInfo().noquote()<<"notify: expire"<<done.title;
    if(notifications_.isEmpty())notificationTimer_.stop();
    else promoteNotification();
    emit changed();
}
qint64 Shell::notificationRemainingMs() const {
    if(notifications_.isEmpty())return 0;
    return std::max<qint64>(0,notifications_.first().expiresAt-notificationClock_.elapsed());
}
const QVector<Entry>& Shell::entries()const {return pages_.isEmpty()?categories_[category_].entries:pages_.last().entries;}
const QVector<Entry>& Shell::entriesAt(int depth)const {return depth==0?categories_[category_].entries:pages_.at(depth-1).entries;}
int Shell::selectionAt(int depth)const {return depth==0?selections_[category_]:pages_.at(depth-1).selection;}
// --- Message surfaces ------------------------------------------------------------------------------
// There is no dialog RCO to read a layout from (audit/DIALOGS-FIRMWARE.md section 1): msgdialog_plugin
// holds two objects and every plane, label and button of a message box is created by vsh.elf. So the
// shell owns the *kind* of surface and its text, and View draws the measured layout for that kind.
// The firmware strings for an empty list come from explore_plugin_full's own msg_error_* family; the
// English text is only the fallback for a dev_flash that cannot be read.
void Shell::showMessage(const QString& text,MessageKind kind) {
    message_=text;messageKind_=kind;messageActions_.clear();messageAccept_=nullptr;
    // A message never inherits an answer row: only showConfirm raises the two-choice box, so every other
    // surface (and an information-only Box) has no answer to move or confirm.
    messageBoxChoices_=false;messageBoxChoice_=BoxChoice::No;
}
void Shell::showMessage(const QString& title,const QString& body,MessageKind kind) {
    showMessage(body.isEmpty()?title:title+"\n\n"+body,kind);
}
void Shell::showConfirm(const QString& title,const QString& body,std::function<void()> accepted) {
    showMessage(title,body,MessageKind::Box);
    // The frame's box starts on No (the non-destructive answer), and the pair is live from here: Left/Right
    // move it, Cross confirms it, Circle cancels (audit/DIALOGS-FIRMWARE.md section 11).
    messageBoxChoices_=true;messageBoxChoice_=BoxChoice::No;
    messageAccept_=std::move(accepted);
    if(dialogLog_)qInfo().noquote()<<"dialog: box show choices=yes choice=no";
}
void Shell::showErrorPage(const QString& title,const QString& cause,const QStringList& actions) {
    showMessage(title,cause,MessageKind::ErrorCode);
    messageActions_=actions;
}
void Shell::clearMessage() {
    message_.clear();messageKind_=MessageKind::None;messageActions_.clear();messageAccept_=nullptr;
    messageBoxChoices_=false;messageBoxChoice_=BoxChoice::No;
}
int Shell::boxChoice() const {
    return messageKind_==MessageKind::Box && messageBoxChoices_?int(messageBoxChoice_):-1;
}
int Shell::boxChoiceSlot() const {
    return messageKind_==MessageKind::Box && messageBoxChoices_?
        (messageBoxChoice_==BoxChoice::Yes?0:1):-1;
}
void Shell::horizontal(int delta) {
    touch();if(!delta)return;
    columnSink_=0;
    if(!message_.isEmpty()) {
        // A confirm box is the one message with a horizontal choice row: Left/Right (the keyboard's and the
        // pad's, both already routed through here) move the answer with the XMB's own snd_cursor. Every other
        // surface ignores a horizontal press exactly as before, and a Box with no answers has nothing to move.
        // The frame draws Yes on the left and No on the right, and there are exactly two answers, so one step
        // lands on the other one from either side; the model's 0 = No / 1 = Yes order is what the getters expose
        // and what Cross commits, not a screen position. Whether the console clamps at an end instead of
        // wrapping is unproven (no local burst shows this box's D-pad), so the wrap is a stated native choice.
        if(messageKind_==MessageKind::Box && messageBoxChoices_) {
            messageBoxChoice_=messageBoxChoice_==BoxChoice::No?BoxChoice::Yes:BoxChoice::No;
            if(dialogLog_)qInfo().noquote()<<"dialog: box move choice="
                <<(messageBoxChoice_==BoxChoice::Yes?"yes":"no");
            interactionAudio_.play(InteractionAudio::Cue::Move);
            emit changed();
        }
        return;
    }
    if(mediaOpen_) {
        if(categories_[category_].id=="photo") {
            stepPhoto(delta);
        } else seekMedia(qint64(delta)*10000);
        return;
    }
    if(!pages_.isEmpty())return;
    // A busy console does not treat Left/Right as a cancel (FIDELITY-BEHAVIOUR N11: the earlier build
    // cancelled the scan here and cleared the status line, an escape the console does not document). The
    // scan keeps running and the page lands; Circle is still the way off the page.
    if(busy()) {
        qInfo().noquote()<<"busy: navigation refused (Left/Right at the root) while a list loads";
        return;
    }
    const int previous=category_;category_=std::clamp(category_+delta,0,int(categories_.size())-1);
    if(previous!=category_ && categories_[category_].id=="friends" && SteamOwned::hasApiKey()
       && (!friendsAge_.isValid() || friendsAge_.elapsed()>30000))refreshSteamFriends();
    if(previous!=category_)interactionAudio_.play(InteractionAudio::Cue::Move);emit changed();
}
void Shell::vertical(int delta) {
    touch();
    if(mediaOpen_) {
        // Photo viewer, Up/Down (BEHAVIOUR-GAP-AUDIT A32; MEDIA-PANELS-FIRMWARE 6.2). The console's photo
        // viewer gives Up/Down/Left/Right the four cursor buttons - "Move the image to display any hidden
        // portions ... when Display Mode is set to Zoom" (the console's own photo control-panel page) - and
        // Previous/Next and Rotate Left/Right are separate panel entries, not a d-pad binding. The view
        // pans an overflowing image before it ever reaches this call (View::pollController's
        // photoViewport_.canPan() branch); an image that fits has nothing hidden, so an Up/Down press
        // changes nothing. It is deliberately not a 90-degree rotation any more: rotatePhoto() owns that.
        if(photoOpen())qInfo().noquote()<<"photo: Up/Down are the viewer's pan cursor; nothing to pan (no rotation)";
        return;
    }
    if(!message_.isEmpty() || busy()) {
        // The busy state blocks list actions (DIALOGS-FIRMWARE 2.8; FIDELITY-BEHAVIOUR N10).
        if(busy() && message_.isEmpty())qInfo().noquote()<<"busy: navigation refused (Up/Down) while a list loads";
        return;
    }
    const int previous=selection();currentSelection()=std::clamp(selection()+delta,0,std::max(0,int(entries().size())-1));
    if(previous!=selection()){columnSink_=0;interactionAudio_.play(InteractionAudio::Cue::Move);emit changed();}
    else if(delta>0 && !entries().isEmpty()){++columnSink_;emit changed();}
    else emit changed();
}
void Shell::activate() {
    touch();
    // A modal box's confirm is its own path: TRACED 0x00341870 plays snd_decide (Cue::Confirm) when the
    // box's confirm button is pressed and then hides the label and the button. The other four kinds are
    // dismissed, not confirmed; the firmware cue for that is UNPROVEN, so they keep this build's
    // snd_system_ok acknowledgement (Cue::Accept).
    if(messageKind_==MessageKind::Box) {
        auto accepted=std::move(messageAccept_);
        // Cross/Enter confirms the focused answer. Yes runs the box's action and closes; No closes without it.
        // A Box raised with no answers (showMessage(..., MessageKind::Box)) has nothing to confirm, so it keeps
        // the single-action path every Box had before the choice model: cross dismisses and runs whatever the
        // message carries, if anything. The cue is snd_decide either way - the frame's footer names Enter as the
        // box's confirm button, whichever answer it commits.
        const bool commit=!messageBoxChoices_ || messageBoxChoice_==BoxChoice::Yes;
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        if(dialogLog_)qInfo().noquote()<<"dialog: box enter choice="
            <<(messageBoxChoices_?(messageBoxChoice_==BoxChoice::Yes?"yes":"no"):"none")
            <<"commit="<<(commit?1:0);
        clearMessage();
        if(commit && accepted)accepted();
        else if(steamPrompt_)steamSwitchCancel(QStringLiteral("No on the prompt"));
        else if(settingsWrite_.stage==WriteStage::Ask)cancelSettingWrite(QStringLiteral("No on the ask page"));
        emit changed();
        return;
    }
    if(!message_.isEmpty()){interactionAudio_.play(InteractionAudio::Cue::Accept);clearMessage();
        // Cross on a result page returns to the row list (the console's *_finish page does the same): the band
        // the write came from is popped here, and the parent page's focused row re-reads its value.
        if(settingsWrite_.stage==WriteStage::FinishOk || settingsWrite_.stage==WriteStage::FinishFailed)finishSettingWrite();
        emit changed();return;}
    if(mediaOpen_){togglePlayback();return;}
    if(entries().isEmpty() || busy()) {
        if(busy())qInfo().noquote()<<"busy: action refused (Cross) while a list loads";
        return;
    }
    const auto entry=entries()[selection()];
    // Committing a value is the firmware's module-action path: snd_system_ok when the action completes
    // (explore_plugin.prx FUN_00030954 / FUN_00062e6c). SetSetting is silent here and plays Cue::Accept
    // after its write below (audit/INTERACTION-AUDIO.md section 10).
        if(entry.action!=Action::SetSetting)
        interactionAudio_.play(entry.action==Action::Unavailable?InteractionAudio::Cue::Error:
            entry.action==Action::SettingChoices?InteractionAudio::Cue::Options:InteractionAudio::Cue::Confirm);
        switch(entry.action) {
    case Action::SettingsPage:openSettings(entry.value);break;
    case Action::SettingChoices:openChoices(entry.setting,entry.title);break;
    case Action::SetSetting:
        // The two file pickers that live inside the Theme and Background lists (`Install` and `Select
        // Wallpaper`): their items carry a sentinel value no bounded Preferences key accepts, so Cross asks the
        // view for the file dialog instead of starting a write. The rows are ordinary SetSetting rows of a
        // choice band, so nothing about the band's own class changes.
        if(entry.option.toString()==QLatin1String(kThemePickerValue)) {
            qInfo().noquote()<<"settings: theme picker row -> .p3t file dialog (Install, appearance/theme band)";
            emit requestTheme();break;
        }
        if(entry.option.toString()==QLatin1String(kWallpaperPickerValue)) {
            qInfo().noquote()<<"settings: wallpaper picker row -> image file dialog (Select Wallpaper, appearance/background band)";
            emit requestWallpaper();break;
        }
        // A picker row writes nothing yet, so it has no chain: a Background row that needs a file asks the
        // view for one (the file dialog's result comes back through chooseWallpaper()).
        if(entry.setting=="appearance/background" && entry.option.toInt()==2 && wallpaper().isEmpty() && theme_.background.isNull()){emit requestWallpaper();break;}
        // Every other write now runs the console's chain (ask -> [proceed] -> finish) instead of committing
        // and popping in one frame (FIDELITY-BEHAVIOUR I77). beginSettingWrite() decides the stages per row
        // class and names the evidence for each.
        beginSettingWrite(entry);
        break;
    case Action::EditName:emit requestSystemName(preferences_.value("system/name").toString());break;
    case Action::CreateUser:emit requestNewUser();break;
    case Action::SelectUser: {
        // D16 (BEHAVIOUR-GAP-AUDIT): selecting a user switches the whole session - that user's settings,
        // libraries, friends and trophies - not just the id in profiles.json. The settings store's
        // documented switch (Preferences::setProfile, preferences.h) is the only way the active profile
        // changes, so it is called here; rewriting profiles.json's activeId alone left every setting and
        // every library/... path resolving against the previous user.
        const QString id=entry.value;
        if(id.isEmpty())break;
        const QString previous=profiles_.activeId();
        // The resolved values of the user being left, read before the switch, so the log line below is
        // the before/after pair rather than a single number either side could be mistaken for.
        const int previousVolume=preferences_.value("audio/volume").toInt();
        const QString previousLibrary=preferences_.value("library/video").toString();
        if(!profiles_.select(id))break;              // failed() already raised the store's own reason
        if(!preferences_.setProfile(id)) {
            // The settings store refused (a read-only or full data directory): put the user record back
            // so profiles.json and preferences.json still name the same user, and keep the session as it
            // was. Preferences::failed already raised the page.
            if(!previous.isEmpty())profiles_.select(previous);
            break;
        }
        // Nothing of the previous user's surface may survive a switch: the console reloads that user's
        // XMB. Playback ends, pages/panels/messages close, and the columns and settings are rebuilt
        // against the new profile's values. rebuildUsers() also runs through Profiles::changed; it is
        // called again because selecting the already-active user emits nothing.
        ++mediaRequest_;++photoRequest_;jellyfinRequest_=0;finishJellyfinPlayback();jellyfinQueue_=false;mediaOpen_=false;
        mediaQueue_.clear();mediaFailures_=0;slideshow_.stop();photo_={};photoRotation_=0;player_->stop();player_->setSource(QUrl{});
        resumePath_.clear();resumePending_=0;mediaDisplayPinned_=false;loading_=false;status_.clear();
        pages_.clear();resetOptions();clearMessage();
        applyPreferences();
        rebuildUsers();
        refreshContent();
        // The switch line carries the resolved per-profile values on both sides, so a scripted run shows
        // that the settings and the library path really changed user (not only activeId).
        qInfo().noquote()<<"shell: user switched from"<<previous
            <<"volume="<<previousVolume
            <<"library/video="<<QDir::toNativeSeparators(previousLibrary)
            <<"to"<<id
            <<"volume="<<preferences_.value("audio/volume").toInt()
            <<"library/video="<<QDir::toNativeSeparators(preferences_.value("library/video").toString())
            <<"library/photo="<<QDir::toNativeSeparators(preferences_.value("library/photo").toString());
        emit changed();
        break;
    }
    case Action::Wallpaper:emit requestWallpaper();break;
    case Action::Theme:emit requestTheme();break;
    case Action::Information: {
        // System Information, the connection status list and the output pages are D5 information pages:
        // the same titled frame with Key: value rows, no dialog chrome (audit/DIALOGS-FIRMWARE.md 2.5).
        // A settings row's own page comes from settings_menu's builder: the host layer's page where it has
        // one (host::rowDetail), otherwise the row's firmware explanation plus the host fact that leaves its
        // value column empty. The three host: payloads keep their measured depth-2 table shape (dialogs.cpp
        // reads the payload, not the text).
        const host::Label label=[this](const QString& id,const QString& fallback){return assets_.text("sysconf_plugin",id,fallback);};
        QString body;
        // The console answers the two network rows with the firmware's own D4 message page, not a report:
        // page-network-connection-settings.png carries the heading msg_internet_connect_test and the body
        // "Please wait..." on the page class's rule pair, while this build drew the D5 info page there
        // (FIDELITY-SURFACES 14.5). The live report stays reachable from the status-list row, which is the
        // row the console shows it on (
        // [TRACED: the page class, both strings, the two row ids. FIT: none.]
        if(entry.value=="host:network" && (entry.id=="net_internet_setting" || entry.id=="net_internet_test")) {
            showMessage(assets_.text("sysconf_plugin","msg_internet_connect_test","Internet Connection Test"),
                        assets_.text("sysconf_plugin","msg_wait","Please wait..."),MessageKind::Page);
            break;
        }
        if(entry.value=="host:network")body=host::networkStatus(label);
        else if(entry.value=="host:display")body=host::displayOutput();
        else if(entry.value=="host:audio")body=host::audioOutput();
        else if(!entry.settingsPage.isEmpty())
            body=settingsRowBody(entry,assets_,renderer_,preferences_.value("system/name").toString());
        if(body.isEmpty())
            body=entry.value.isEmpty()?host::systemInformation(renderer_,label,preferences_.value("system/name").toString()):entry.value;
        showMessage(body,MessageKind::Info);break;
    }
    case Action::Library: {
        // The Video column's Continue Watching row: a page built from this shell's own resume points, not a
        // folder on disk. Keyed by id rather than by a new Action member, so every existing switch over the
        // action enum (labels, kinds, the information page) keeps its meaning.
        if(entry.id=="continue"){openContinueWatching();break;}
        const auto path=preferences_.value("library/"+categories_[category_].id).toString();
        if(!path.isEmpty())openDirectory(path);else emit requestLibrary();break;
    }
    case Action::ChooseLibrary:emit requestLibrary();break;
    case Action::Folder:openDirectory(entry.value);break;
    case Action::File:openFile(entry);break;
    case Action::Launch:launch(entry);break;
    case Action::LaunchSteam:startSteamTracked(entry.value,entry.title);break;
    case Action::SteamFolder:openSteamFolder(entry.value);break;
    case Action::SteamDownloads:openSteamDownloads();break;
    case Action::SteamStoreBrowse: {
        // Sections first: no request until a section is chosen, so opening the store is instant.
        Page page;page.title=QObject::tr("Steam Store");
        const auto section=[&](const QString& id,const QString& title,const QString& detail){
            auto e=item("store-section:"+id,title,"item_tex_plain_folder","explore_plugin_full",
                Action::SteamStoreSection,id);
            e.detail=detail;page.entries.push_back(std::move(e));
        };
        section("top_sellers",QObject::tr("Top Sellers"),QObject::tr("What sells most right now"));
        section("new_releases",QObject::tr("New Releases"),QObject::tr("Just released"));
        section("specials",QObject::tr("Specials"),QObject::tr("Discounted games"));
        pages_.push_back(std::move(page));
        emit changed();
        break;
    }
    case Action::SteamStoreSection:steamStore_.featured(entry.value);break;
    case Action::InstallSteam: {
        // Steam owns the download: opening steam://install/<id> asks the client to install it.
        if(!QDesktopServices::openUrl(QUrl(SteamOwned::installUrl(entry.value))))
            showMessage(entry.title,"Steam could not be asked to install this game.",MessageKind::Page);
        else {
            // Picking up a finished download: rescan once after a while so the row moves from the
            // Not Installed folder to a launchable inline title without a manual F5. The delay can
            // be shortened with XMB_INSTALL_REFRESH_MS for scripted checks.
            bool ok=false;
            const int delay=qEnvironmentVariableIntValue("XMB_INSTALL_REFRESH_MS",&ok);
            QTimer::singleShot(ok && delay>0 ? delay : 120000,this,&Shell::refreshContent);
        }
        break;
    }
    case Action::LaunchApp: {
        const auto app=std::find_if(contentApps_.cbegin(),contentApps_.cend(),[&](const auto& a){return a.id==entry.value;});
        // A registration this build cannot resolve is its own guard, with no OS error to report, so it
        // stays the plain titled page. A launch that fails after that has already raised startTracked's
        // error-code page carrying the OS's own reason; overwriting it here would throw that away.
        if(app==contentApps_.cend() || !QFileInfo(app->executable).isFile())
            showMessage(entry.title,"This application could not be started.",MessageKind::Page);
        else startTracked(app->executable,app->arguments,app->workingDirectory,entry.title);
        break;
    }
    case Action::OpenUrl:
        if(!QDesktopServices::openUrl(QUrl(entry.value)))showMessage(entry.title,"The application could not be opened.",MessageKind::Page);
        break;
    case Action::RefreshContent:refreshContent();break;
    case Action::RefreshFriends:refreshSteamFriends(true);break;
    case Action::ImportApps:emit requestImportApps();break;
    case Action::JellyfinApp:openJellyfin(entry);break;
    case Action::JellyfinFolder:loadJellyfinPage(entry.value,entry.title);break;
    case Action::JellyfinPlay:openJellyfinMedia(entry);break;
    case Action::JellyfinMore:
        // A search page's More row carries the term in value and the page title in detail; a folder's
        // carries its parent id and the page title, exactly as before.
        if(!pages_.isEmpty() && pages_.last().jellyfinSearch)loadJellyfinSearchPage(entry.value,entry.detail,entry.option.toInt());
        else loadJellyfinPage(entry.value,entry.detail,entry.option.toInt());
        break;
    case Action::JellyfinSearch:
        // The keyboard belongs to the view: this asks for it through the one field-edit signal the
        // sign-in page already uses, with "search" as the field key, and the accepted text comes back
        // through setJellyfinSignInField("search", ...). No new view code.
        emit requestJellyfinFieldEdit(QStringLiteral("search"),QObject::tr("Search Jellyfin"),jellyfinSearchTerm_,false);
        break;
    case Action::JellyfinSignOut:signOutJellyfin();break;
    // The sign-in page's own rows. A field asks the view for the XMB keyboard on that field's current value,
    // masked for the password; Submit sends, Cancel leaves the page and aborts a request still running.
    case Action::JellyfinSignInField: {
        const QString field=entry.value;
        const QString initial=field=="server"?signInServer_:field=="username"?signInUser_:signInPassword_;
        emit requestJellyfinFieldEdit(field,entry.title,initial,field=="password");
        break;
    }
    case Action::JellyfinSignInRemember:
        // The explicit opt-in. Turning it off is effective immediately: the stored record is deleted
        // here, not on the next successful sign-in.
        signInRemember_=!signInRemember_;
        if(!signInRemember_)jellyfin_.forgetSession();
        qInfo().noquote()<<"jellyfin: stay signed in"<<(signInRemember_?"on":"off")
            <<(signInRemember_?"(an accepted sign-in will store the session for this Windows user)":"(the stored session record, if any, was deleted)");
        refreshJellyfinSignInPage();emit changed();
        break;
    case Action::JellyfinSignInSubmit:submitJellyfinSignIn();break;
    case Action::JellyfinSignInCancel:cancelJellyfinSignInPage();break;
    case Action::Unavailable:
        // A Continue Watching row whose file has gone says why, instead of the generic line, which would be
        // false: the feature exists, the file does not.
        showMessage(entry.title,entry.id.startsWith("continue:") && !entry.detail.isEmpty()
            ?entry.detail
            :QObject::tr("This feature is not available in the native recreation yet."),MessageKind::Page);
        break;
    case Action::ResetPreferences:
        // The console's own chain for this row (SETTINGS 4.2: page_console_setting_init_top -> proceed ->
        // finish), with the strings that chain uses: msg_setting_init_ask1/ask2, msg_initializing and
        // msg_initialized/msg_error_init. It was a native-worded box before; the write itself is unchanged
        // (restoreDefaultSettings in settings_menu.cpp, one atomic multi-key call, app state only).
        beginResetWrite();
        break;
    case Action::SteamAccounts:openSteamAccounts();break;
    case Action::SteamAccountSwitch:steamAccountSelected(entry.value);break;
    // A letter folder of a page the item menu's Group Content rebuilt: it opens that key's rows as an ordinary
    // page, so Circle returns to the letter list and the item menu's All puts the page back together.
    case Action::GroupFolder:openGroupFolder(entry);break;
    }
    emit changed();
}
// --- PS button -----------------------------------------------------------------------------------
// The console's behaviour, captured from RPCS3 on 2026-10-26 (audit/PS-BUTTON.md): a tap returns to the
// XMB and flashes the controller indicator; a hold opens a black system menu with Turn Off the System and
// Turn Off the Controller (and Quit Game while a title runs). A power action asks once before doing it.
void Shell::psTapped() {
    touch();
    if(psMenu_) { psMenuClose(); return; }
    // A PS tap pulls the user away from whatever surface was up, so a settings write waiting on its ask page
    // is abandoned rather than left half-asked: the value is not written and the store is untouched.
    if(settingsWrite_.stage==WriteStage::Ask)cancelSettingWrite(QStringLiteral("PS tap"));
    if(steamPrompt_)steamSwitchCancel(QStringLiteral("PS tap"));
    // Bring the XMB back over whatever the user was doing; the indicator itself is drawn by the view.
    clearMessage();status_.clear();
    emit psTapIndicator();
    emit changed();
}
void Shell::psHeld() {
    touch();
    if(psMenu_)return;
    psMenu_=true;psConfirmOff_=false;psSelection_=0;
    qInfo().noquote()<<"shell: PS menu opened";
    emit changed();
}
QStringList Shell::psMenuItems() const {
    QStringList items;
    if(gameRunning())items<<QObject::tr("Quit Game");
    items<<QObject::tr("Turn Off the System")<<QObject::tr("Turn Off the Controller");
    return items;
}
void Shell::psMenuMove(int delta) {
    touch();
    if(!psMenu_ || delta==0)return;
    if(psConfirmOff_){psSelection_=0;emit changed();return;}
    const int count=psMenuItems().size();
    psSelection_=(psSelection_+delta%count+count)%count;
    qInfo().noquote()<<"shell: PS menu selection"<<psSelection_;
    emit changed();
}
void Shell::psMenuActivate() {
    touch();
    if(!psMenu_)return;
    if(psConfirmOff_){ emit requestQuit(); return; }
    const auto items=psMenuItems();
    if(items.isEmpty())return;
    const QString item=items.value(std::clamp(psSelection_,0,int(items.size())-1));
    if(item==QObject::tr("Turn Off the System")) { psConfirmOff_=true; emit changed(); return; }
    if(item==QObject::tr("Quit Game")) {
        if(!steamGameId_.isEmpty()) {
            // A Steam title is not our process: ask the client to stop it (audit/STEAM-CLI.md) and let the
            // registry watcher report the return. Killing steam.exe would end the whole client.
            QString stopError;
            if(steamStopApp(steamGameId_,steamRoot_,&stopError))
                qInfo().noquote()<<"shell: asked Steam to stop"<<steamGameId_;
            else
                qWarning().noquote()<<"shell: Steam stop failed:"<<stopError;
        }
        else if(!game_.isNull() && game_->state()!=QProcess::NotRunning) {
            game_->terminate();
            if(!game_->waitForFinished(3000))game_->kill();
            qInfo().noquote()<<"shell: quit game requested from the PS menu";
        }
        showMessage(gameTitle_,QObject::tr("The game was closed."),MessageKind::Page);
        psMenu_=false;
        emit changed();
        return;
    }
    // The console powers the controller off itself; a Bluetooth/USB pad on a PC is owned by Windows, so
    // say so instead of pretending.
    showMessage(QObject::tr("Turn Off the Controller"),QObject::tr("A controller connected to this PC is managed by Windows and stays on. Disconnect or power it off from the system."),MessageKind::Page);
    psMenu_=false;
    emit changed();
}
void Shell::psMenuClose() {
    touch();
    if(!psMenu_ && !psConfirmOff_)return;
    psMenu_=false;psConfirmOff_=false;
    emit changed();
}
// --- Item options (Triangle) ----------------------------------------------------------------------
// The firmware's item menu is explore_plugin's `expage_menu`: rows are installed at run time from an array
// of table keys, Information is always the last row, the other rows appear only under the condition the
// builder checks, and there is no Close row at all - Circle dismisses the list (audit/ITEM-MENU-INFO-FIRMWARE
// A2-A4, A8). This build keeps that shape: every label below is read from explore_plugin_full's English
// text table (A3), and a firmware row whose condition this build cannot evaluate is left out rather than
// invented (the audit's appended phase 12.2 table names each one and the condition that is missing).
OptionLabel Shell::activateLabel(const Entry& entry) const {
    switch(entry.action) {
    case Action::Folder:case Action::SteamFolder:case Action::JellyfinFolder:
    case Action::Library:case Action::SteamStoreBrowse:case Action::SteamStoreSection:
    case Action::SteamDownloads:
        return {"msg_open","Open"};
    case Action::File:
        return categories_[category_].id=="photo"?OptionLabel{"msg_display","View"}:OptionLabel{"msg_play","Play"};
    case Action::JellyfinPlay:return entry.resumeMs>0?OptionLabel{"msg_resume","Resume"}:OptionLabel{"msg_play","Play"};
    case Action::Launch:case Action::LaunchSteam:case Action::LaunchApp:
        return {"msg_game_start","Start"};
    case Action::Information:return {"msg_display","View"};
    default:return {"msg_game_start","Start"};
    }
}
QString Shell::optionLabel(const OptionLabel& label) {
    // The table is the source. The fallback covers a flash image without explore_plugin_full.rco, the same
    // contract Assets::text already gives the shell's other firmware strings.
    if(!label.key || !*label.key)return label.fallback;
    return assets_.text("explore_plugin_full",QString::fromLatin1(label.key),label.fallback);
}
void Shell::addOption(const OptionLabel& label,OptionCommand command,const char* condition,bool enabled,
                      const QString& note,int index) {
    MenuOption row;
    row.title=optionLabel(label);
    row.command=command;
    row.index=index;
    row.enabled=enabled;
    row.note=note;
    row.labelKey=label.key?QString::fromLatin1(label.key):QString{};
    row.condition=QString::fromLatin1(condition);
    options_.push_back(row);
}
qint64 Shell::optionResumePoint(const Entry& entry) const {
    // The two resume stores this build keeps: the provider's own millisecond field for a Jellyfin row, and
    // media/resume for a local playable file (including a Continue Watching row, whose record is the reason
    // it is listed). A game title has neither, which is why Start never turns into Resume.
    if(entry.action==Action::JellyfinPlay)return entry.resumeMs;
    if(entry.action==Action::File && !entry.value.isEmpty())return resumePositionFor(entry.value);
    return 0;
}
bool Shell::optionIsLocalContent(const Entry& entry) const {
    // The firmware gates Copy on the item being copyable and Delete on local storage (A4); this build's
    // equivalent is a row backed by a path on this PC. A Continue Watching row is a resume record rather
    // than a library item, so the page's own remove command stands in for the file commands there.
    if(entry.id.startsWith("continue:"))return false;
    if(entry.action!=Action::File && entry.action!=Action::Folder)return false;
    if(entry.value.isEmpty() || QFileInfo(entry.value).isRelative())return false;
    return QFileInfo::exists(entry.value);
}
bool Shell::optionPageSortable() const {
    return !pages_.isEmpty() && !pages_.last().directory.isEmpty() && entries().size()>1;
}
// True on the row class the captured Game-column menu belongs to: a title that runs (installed, imported or
// Steam). The settled frame is that menu, and it is the only class whose full five-row shape is measured
// (frames/menu-options-open.png: Sort By / Group Content at ink 301-323 / 341-362, then Start / Delete /
// Information at 539-638), so only this class is rebuilt to it. The media and Continue Watching menus keep
// their measured row sets (F3/H2): no frame covers them with this item-menu source.
static bool isGameTitleRow(const Entry& entry) {
    return entry.action==Action::Launch || entry.action==Action::LaunchSteam || entry.action==Action::LaunchApp;
}
bool Shell::optionGroupable() const {
    // Group Content needs a list of titled rows and nothing else: this build groups by first letter, which every
    // ordinary list can do. A form (the sign-in page), a provider page (Steam store, Jellyfin, downloads) and a
    // Continue Watching page are not lists this build will reorder, so the row stays out there rather than
    // appearing over rows it cannot name. A page that is already grouped is still groupable (All / re-group).
    if(pages_.isEmpty() || entries().size()<2)return false;
    const auto& page=pages_.last();
    if(page.signIn || page.downloads || page.jellyfin || page.jellyfinViews || page.jellyfinSearch
       || page.steamAccounts || page.continuePage)return false;
    for(const auto& entry:entries())if(entry.title.trimmed().isEmpty())return false;
    return true;
}
QString Shell::groupLetter(const QString& title) {
    // The group key of one row: its first letter, upper-cased; anything that is not a letter or a digit goes to
    // "#" so no row is lost and no group is invented. QString::toUpper() is used rather than a locale table so
    // the same title always lands in the same folder.
    const QString trimmed=title.trimmed();
    if(trimmed.isEmpty())return QStringLiteral("#");
    const QChar first=trimmed.at(0).toUpper();
    return first.isLetterOrNumber()?QString(first):QStringLiteral("#");
}
QString Shell::entryKind(const Entry& entry) const {
    // A Continue Watching row is always a remembered playable file, so the kind comes from the column it
    // is listed in rather than from the action (which is Unavailable on a stale row, and that is already
    // said in the row's own line and its Information page).
    if(entry.id=="continue")return QObject::tr("In progress");
    if(entry.id.startsWith("continue:"))return categories_[category_].id=="music"?QObject::tr("Music"):QObject::tr("Video");
    switch(entry.action) {
    case Action::Folder:case Action::Library:case Action::SteamFolder:case Action::JellyfinFolder:return QObject::tr("Folder");
    case Action::File: {
        const auto& kind=categories_[category_].id;
        return kind=="photo"?QObject::tr("Image"):kind=="music"?QObject::tr("Music"):QObject::tr("Video");
    }
    case Action::Launch:case Action::LaunchApp:return QObject::tr("Game");
    case Action::LaunchSteam:return QObject::tr("Steam game");
    case Action::SettingsPage:case Action::SettingChoices:return QObject::tr("Settings");
    case Action::Unavailable:return QObject::tr("Unavailable");
    case Action::SteamDownloads:return QObject::tr("Downloads");
    case Action::Information:return entry.id.startsWith("download:")?QObject::tr("Download"):QObject::tr("Item");
    case Action::JellyfinApp:case Action::JellyfinPlay:return QObject::tr("Jellyfin media");
    case Action::JellyfinSearch:return QObject::tr("Search");
    default:return QObject::tr("Item");
    }
}
void Shell::resetOptions() {
    optionsOpen_=optionsNested_=false;optionsSelection_=0;
    options_.clear();optionsParent_.clear();optionsTitle_.clear();
    playerPanel_=false;playFromBeginning_=false;
}
void Shell::buildOptions() {
    resetOptions();
    if(entries().isEmpty())return;
    const auto& entry=entries()[std::clamp(selection(),0,int(entries().size())-1)];
    // ---- The Game-row class: the firmware's five rows, in its two groups -------------------------------
    // The settled 4.93 frame (frames/menu-options-open.png, re-measured this pass) is this menu: Sort By and
    // Group Content at ink 301-323 / 341-362 with a right-pointing chevron at x 1573..1583 on both and the
    // small purple square after Group Content at x 1517..1529 y 345..358, then a gap, then Start, Delete and
    // Information at 539-560 / 581-598 / 621-638. The previous build offered two rows here (Start,
    // Information), no utility group and no break (FIDELITY-SURFACES 6.3/6.4: 2 rows against 5).
    // Only this class is rebuilt: the media and Continue Watching menus keep their measured row sets.
    if(isGameTitleRow(entry)) {
        addOption({"msg_sort","Sort By"},OptionCommand::SortMenu,"game-row-list");
        options_.last().submenu=true;
        // Group Content opens the level the frame's own submenu shows (frames/menu-submenu-group-content.png:
        // By Format / By Album / All for that console's list). This build groups by first letter, so its level
        // offers By Title and All; the row still carries the frame's chevron and its gate badge.
        if(optionGroupable()) {
            addOption({"msg_folder_class","Group Content"},OptionCommand::GroupMenu,"game-row-list");
            options_.last().submenu=true;options_.last().gateBadge=true;
        }
        const int actionGroup=options_.size();
        addOption({"msg_game_start","Start"},OptionCommand::Activate,"game-title");
        // Delete is the firmware's always-present row (A4 row 4). This build has no delete backend for a title:
        // a Steam title's files belong to the client and an imported one to the emulator's install folder, so
        // the row is listed and cannot be chosen and its note says exactly that (the panel's disabled-row rule).
        addOption({"msg_del","Delete"},OptionCommand::Delete,"game-title",false,
                  tr("This native build does not delete a title's files; Steam and the emulator's install "
                     "folder own them."));
        addOption({"msg_info","Information"},OptionCommand::Information,"always-last");
        if(actionGroup>0 && actionGroup<int(options_.size()))options_[actionGroup].dividerBefore=true;
        return;
    }
    const qint64 resumeAt=entry.action==Action::Unavailable?0:optionResumePoint(entry);
    // Two groups, in the order the settled firmware frame shows them (ITEM-MENU-INFO-FIRMWARE G2: Sort By
    // and Group Content at ink 301-323 / 341-362, then Start, Delete, Information at 539-560 / 581-598 /
    // 621-638, with a gap between). Group 0 is the list-level/utility section and is built first, so
    // `actionGroup` below marks the first primary row; only that row carries `dividerBefore`, which is the
    // whole of the group data the view gets. No Display All row: nothing on these pages is grouped or
    // hidden. Group Content has no native store, so it stays out rather than being drawn inert (F3).
    if(optionPageSortable())
        addOption({"msg_sort","Sort By"},OptionCommand::SortMenu,"page-sortable");
    // Group 1 - the item's own actions. Its first row is the item's command (A4); the menu opens with focus
    // on it (the settled frame's bright row is Start), not on the utility row above.
    const int actionGroup=options_.size();
    // Its label comes from the row's kind (A3: Open for a folder/store row, Play for a playable file, View
    // for an image or an information row, Start for a game title) and becomes Resume while a resume record
    // exists.
    if(entry.action!=Action::Unavailable)
        addOption(resumeAt>0?OptionLabel{"msg_resume","Resume"}:activateLabel(entry),OptionCommand::Activate,
                  resumeAt>0?"resume-record":"actionable");
    // msg_play_begin. The firmware's restart row is what a resume record makes different from the title's own
    // command; without a record it would repeat row 0, so it is only offered when there is one.
    if(resumeAt>0)
        addOption({"msg_play_begin","Play from Beginning"},OptionCommand::PlayFromBeginning,"resume-record");
    // msg_copy / msg_del. The firmware's per-item file commands, offered only for local content (A4's Copy
    // condition). This build has no copy or delete backend, so both rows are listed and cannot be chosen:
    // the panel's disabled-row rule puts the reason on screen instead of pretending the command exists.
    if(optionIsLocalContent(entry)) {
        addOption({"msg_copy","Copy"},OptionCommand::Copy,"local-content",false,
                  tr("This native build does not copy files yet."));
        addOption({"msg_del","Delete"},OptionCommand::Delete,"local-content",false,
                  tr("This native build does not delete files yet."));
    }
    // The Continue Watching row is this build's own record, not a firmware item: the firmware has no row
    // for forgetting a resume point. Its label is the firmware's own generic remove word (`msg_eject` =
    // "Remove") rather than an English literal, and the note plus the confirm box say what is removed, so
    // the row cannot be read as deleting the file.
    if(entry.id.startsWith("continue:"))
        addOption({"msg_eject","Remove"},OptionCommand::RemoveResume,"continue-row",true,
                  tr("Removes this title from Continue Watching; the file stays on disk."));
    // Information is always the last row (A4's invariant; msg_info, the builder's own key at :60986).
    addOption({"msg_info","Information"},OptionCommand::Information,"always-last");
    // Mark the break only when there is a utility group to break from. With none, the list is one group and
    // the flag stays false, so the view draws the measured action rows unchanged.
    if(actionGroup>0 && actionGroup<int(options_.size()))options_[actionGroup].dividerBefore=true;
}
void Shell::logOptions(const Entry& entry) {
    QStringList titles,keys,conditions;
    for(const auto& option:options_) {
        titles<<option.title;
        keys<<(option.labelKey.isEmpty()?QStringLiteral("(native)"):option.labelKey);
        conditions<<option.condition;
    }
    // The established panel line (the harness and the audits read this shape), then the row provenance:
    // which RCO key each label came from and which condition put the row in this list.
    qInfo().noquote()<<"shell: options opened category="<<categories_[category_].id<<"depth="<<pages_.size()
        <<"entry="<<entry.id<<"rows="<<options_.size()<<titles.join('|');
    qInfo().noquote()<<"shell: options rows entry="<<entry.id<<"keys="<<keys.join('|')
        <<"conditions="<<conditions.join('|');
    // The group break the view draws from, as data: one line per group with the rows it holds. `break`
    // names the pair the gap sits between; `(none)` means the list is a single group.
    int groups=options_.isEmpty()?0:1,breakAt=-1;
    for(int i=0;i<options_.size();++i)if(options_[i].dividerBefore){++groups;breakAt=i;}
    qInfo().noquote()<<"shell: options groups entry="<<entry.id<<"groups="<<groups
        <<"break="<<(breakAt>0?options_[breakAt-1].title+QStringLiteral(" -> ")+options_[breakAt].title
                             :QStringLiteral("(none)"));
    // The two eject labels the audit corrected from "Eject Disc": msg_eject is now the Continue Watching
    // row's own label (the firmware's generic remove word), while msg_disc_eject still has no row because
    // this build has no optical drive. The check is that the table resolves the firmware's own words.
    qInfo().noquote()<<"shell: options eject labels msg_eject="<<optionLabel({"msg_eject","Remove"})
        <<"msg_disc_eject="<<optionLabel({"msg_disc_eject","Remove Disc"});
    // What each row asks the view to draw beside it (FIDELITY-SURFACES 6.5/6.6): a submenu chevron and the
    // gate badge. Empty on every row that carries neither, so a media-row menu prints an empty tail.
    {
        QStringList submenu,gate;
        for(int i=0;i<options_.size();++i) {
            if(options_[i].submenu)submenu<<options_[i].title;
            if(options_[i].gateBadge)gate<<options_[i].title;
        }
        qInfo().noquote()<<"shell: options flags entry="<<entry.id
            <<"submenu="<<(submenu.isEmpty()?QStringLiteral("(none)"):submenu.join('|'))
            <<"gateBadge="<<(gate.isEmpty()?QStringLiteral("(none)"):gate.join('|'));
    }
    if(!qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES"))return;
    // Every label the audit's map names (A3), resolved whether or not this build has a row for it: a key with
    // no row still proves what the firmware string says and keeps the omission checkable.
    static const OptionLabel probes[]={
        {"msg_open","Open"},{"msg_play","Play"},{"msg_display","View"},{"msg_game_start","Start"},
        {"msg_resume","Resume"},{"msg_play_begin","Play from Beginning"},{"msg_info","Information"},
        {"msg_sort","Sort By"},{"msg_display_all","Display All"},{"msg_folder_class","Group Content"},
        // The six sort pairs the firmware's table carries (A3). The two this build offers and the four
        // it cannot (no source) are all resolved here, so a dump shows the firmware's own label for each
        // and the missing sources are checkable rather than an assertion in a comment.
        {"msg_created_date_time_f","Creation Date"},{"msg_created_date_time_r","Creation Date"},
        {"msg_updated_date_f","Update Date"},{"msg_updated_date_r","Update Date"},
        {"msg_importeddate_f","Import Date"},{"msg_importeddate_r","Import Date"},
        {"msg_sale_year_f","Release Year"},{"msg_sale_year_r","Release Year"},
        {"msg_shootdate_f","Film Date"},{"msg_shootdate_r","Film Date"},
        {"msg_trophy_earned_f","Earned Date"},{"msg_trophy_earned_r","Earned Date"},
        {"msg_add_to_playlist","Add to Playlist"},{"msg_copy","Copy"},{"msg_copy_plural","Copy Multiple"},
        {"msg_del","Delete"},{"msg_del_plural","Delete Multiple"},{"msg_eject","Remove"},
        {"msg_disc_eject","Remove Disc"},{"msg_cancel","Cancel"},{"msg_cancel2","Cancel"},
        {"msg_check_for_update","Check for Update"},{"msg_game_update_history","Update History"},
        {"msg_internet_search","Internet Search"},{"msg_purchase","Purchase"},{"msg_about_game","About this Game"},
        {"msg_psmove_notice","PlayStation(R) Move Safety Notice"},
        {"msg_health_warning_psn","Health Warnings"},
        {"msg_intellectual_property_notices","Intellectual Property Notices"}};
    QJsonArray rows,labels,groupsJson;
    for(const auto& option:options_) {
        QJsonObject row{{"title",option.title},{"command",int(option.command)},{"enabled",option.enabled},
                        {"condition",option.condition},{"dividerBefore",option.dividerBefore},
                        {"submenu",option.submenu},{"gateBadge",option.gateBadge}};
        if(!option.labelKey.isEmpty())row.insert("key",option.labelKey);
        if(!option.note.isEmpty())row.insert("note",option.note);
        rows.push_back(row);
    }
    // The same groups the log line prints, with every row's title in build order: group 0 is the
    // list-level/utility section, group 1 the item actions, and any later group would carry dividerBefore.
    {
        QJsonArray current;
        int groupIndex=0;
        for(const auto& option:options_) {
            if(option.dividerBefore && !current.isEmpty()) {
                groupsJson.push_back(QJsonObject{{"group",groupIndex},{"dividerBefore",groupIndex>0},
                                                 {"rows",current}});
                current=QJsonArray();++groupIndex;
            }
            current.push_back(option.title);
        }
        if(!current.isEmpty())
            groupsJson.push_back(QJsonObject{{"group",groupIndex},{"dividerBefore",groupIndex>0},
                                             {"rows",current}});
    }
    for(const auto& probe:probes) {
        const QString text=optionLabel(probe);
        bool used=false;
        for(const auto& option:options_)if(option.labelKey==QString::fromLatin1(probe.key))used=true;
        labels.push_back(QJsonObject{{"key",QString::fromLatin1(probe.key)},{"text",text},{"row",used}});
    }
    QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".options.json");
    if(file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(QJsonObject{{"category",categories_[category_].id},{"depth",pages_.size()},
            {"entryId",entry.id},{"entryTitle",entry.title},{"rows",rows},{"groups",groupsJson},
            {"count",rows.size()},{"labels",labels}}).toJson());
}
void Shell::toggleOptions() {
    touch();
    if(optionsOpen_){dismissOptions();return;}
    if(!message_.isEmpty() || psMenu_ || busy())return;
    // The sign-in page is a form, not a content list: its five rows have no item-level options, and a panel
    // offering "Start"/"Information" over a password field is exactly the kind of noise a credential page
    // should not have. Triangle therefore does nothing there.
    if(!pages_.isEmpty() && pages_.last().signIn)return;
    if(mediaOpen_) {
        // Triangle during video playback is the console's own control panel: subtitle, audio, repeat.
        // Anything else that reaches the player (music, a photo slideshow) keeps its old binding, so a
        // photo still rotates and a track still cycles Repeat.
        if(!videoOpen())return;
        buildPlayerOptions();
        optionsOpen_=true;
        // Open on the current choice rather than on the first row: a stray Cross on an untouched panel
        // must not turn subtitles off when they are on.
        optionsSelection_=0;
        for(int i=0;i<options_.size();++i)if(options_[i].active){optionsSelection_=i;break;}
        interactionAudio_.play(InteractionAudio::Cue::Options);
        QStringList titles;for(const auto& option:options_)titles<<option.title;
        qInfo().noquote()<<"shell: playback panel opened rows="<<options_.size()<<"subtitles="<<subtitleTracks_.size()
            <<"audio="<<audioTracks_.size()<<titles.join('|');
        emit changed();
        return;
    }
    if(entries().isEmpty())return;
    // A settings choice column already is the options panel; Triangle there adds nothing.
    bool choiceColumn=true;
    for(const auto& e:entries())if(e.action!=Action::SetSetting){choiceColumn=false;break;}
    if(choiceColumn)return;
    buildOptions();
    if(options_.isEmpty())return;
    // The firmware opens the list with the item's own command focused (the settled frame's bright row is
    // Start), while the list-level rows sit above it. Start on the first action row when a utility group
    // exists, otherwise on row 0; Up from there reaches the utility group.
    optionsSelection_=0;
    for(int i=0;i<options_.size();++i)if(options_[i].dividerBefore){optionsSelection_=i;break;}
    optionsOpen_=true;
    interactionAudio_.play(InteractionAudio::Cue::Options);
    const auto& entry=entries()[std::clamp(selection(),0,int(entries().size())-1)];
    logOptions(entry);
    emit changed();
}
void Shell::optionsMove(int delta) {
    touch();
    if(!optionsOpen_ || options_.isEmpty() || delta==0)return;
    const int count=options_.size();
    // The item menu clamps at its ends: the firmware's Down x4 stops on Information and does not wrap or
    // scroll (audit/ITEM-MENU-INFO-FIRMWARE E1). The in-player panel is a different surface with no measured
    // traversal, so it keeps its existing wrap. A move that changes nothing is not a focus move and stays
    // silent, which is also why the boundary does not repeat the cursor cue.
    const int next=playerPanel_?(optionsSelection_+delta%count+count)%count
                                :std::clamp(optionsSelection_+delta,0,count-1);
    if(next==optionsSelection_)return;
    optionsSelection_=next;
    interactionAudio_.play(InteractionAudio::Cue::Move);
    qInfo().noquote()<<"shell: options selection"<<optionsSelection_<<options_[optionsSelection_].title;
    emit changed();
}
void Shell::optionsActivate() {
    touch();
    if(!optionsOpen_ || options_.isEmpty())return;
    const int index=std::clamp(optionsSelection_,0,int(options_.size())-1);
    const auto option=options_[index];
    // A row this build cannot honour says so instead of pretending: it stays listed (so the reason is
    // reachable) but Cross only reports it.
    if(!option.enabled) {
        interactionAudio_.play(InteractionAudio::Cue::Error);
        qInfo().noquote()<<"shell: option unavailable"<<option.title<<option.note;
        emit changed();
        return;
    }
    const auto entry=entries().value(std::clamp(selection(),0,std::max(0,int(entries().size())-1)));
    switch(option.command) {
    case OptionCommand::Activate:
        qInfo().noquote()<<"shell: options activate"<<option.title<<"entry="<<entry.id;
        resetOptions();
        activate(); // the entry's own action, exactly as Cross on the row
        break;
    case OptionCommand::Information:
        resetOptions();
        showEntryInformation(entry);
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        emit changed();
        break;
    case OptionCommand::SortMenu: {
        // The firmware's sort rows are six date pairs with the direction glyph in the string itself
        // (ITEM-MENU-INFO-FIRMWARE A3): msg_created_date_time_f/_r, msg_updated_date_f/_r,
        // msg_importeddate_f/_r, msg_sale_year_f/_r, msg_shootdate_f/_r, msg_trophy_earned_f/_r.
        // Only the two pairs this build has a per-row source for are offered: a local page row is a file
        // or folder, and QFileInfo gives its creation (birthTime, falling back to lastModified) and
        // modification time. The other four have no source at all (see the log line below and the audit's
        // D10 note): Import Date needs an import/first-seen record the scanner does not keep, Release Year
        // needs a per-row year nothing stores, Film Date needs an EXIF/container date parser that does not
        // exist in src/, and Earned Date needs trophy data (no PSN/trophy backend). A label that sorts
        // nothing would be worse than its absence, so they are not listed. There is no Close row: Circle
        // steps back to the item list.
        //
        // Nested level: the firmware's plane_menu1/2 are separate 128x272 plates (A2), while this build draws
        // the nested list in the same full-height band with the parent rows hidden; that is a recorded
        // mismatch (audit H4), not a plate rebuilt in this slice. The submenu's heading position is also
        // unmeasured - no frame shows an open level 1 (G4) - so it stays at the view's existing one-row-pitch
        // placement above the first baseline and is marked a fit in the audit (H3).
        int parentGroups=options_.isEmpty()?0:1;
        for(const auto& row:options_)if(row.dividerBefore)++parentGroups;
        optionsParent_=options_;optionsNested_=true;optionsTitle_=optionLabel({"msg_sort","Sort By"});
        options_.clear();
        addOption({"msg_created_date_time_f",QStringLiteral("Creation Date ")+QChar(0x25B2)},
                  OptionCommand::SortCreatedAscending,"sort-row");
        addOption({"msg_created_date_time_r",QStringLiteral("Creation Date ")+QChar(0x25BC)},
                  OptionCommand::SortCreatedDescending,"sort-row");
        addOption({"msg_updated_date_f",QStringLiteral("Update Date ")+QChar(0x25B2)},
                  OptionCommand::SortUpdatedAscending,"sort-row");
        addOption({"msg_updated_date_r",QStringLiteral("Update Date ")+QChar(0x25BC)},
                  OptionCommand::SortUpdatedDescending,"sort-row");
        // The Game-row class opens this level too (the frame's first utility row), and a title list has no file
        // to read a date from: the rows are then listed and cannot be chosen, with the reason on the row, rather
        // than offered as a sort that would reorder nothing. A local folder page keeps them enabled.
        {
            bool anyDate=false;
            if(!pages_.isEmpty())for(const auto& row:pages_.last().entries) {
                if(row.value.isEmpty())continue;
                const QFileInfo info(row.value);
                if(info.exists() && (info.birthTime().isValid() || info.lastModified().isValid())){anyDate=true;break;}
            }
            if(!anyDate)for(auto& row:options_) {
                row.enabled=false;
                row.note=tr("No row on this page carries a file date, so there is nothing for a date sort to order.");
            }
        }
        optionsSelection_=0;
        interactionAudio_.play(InteractionAudio::Cue::Options);
        qInfo().noquote()<<"shell: options sort menu opened rows="<<options_.size()
            <<"labels=creation/update date pairs groups=1 parentGroups="<<parentGroups
            <<"heading=fit(unmeasured)"
            <<"enabled="<<(options_.isEmpty()?0:(options_.first().enabled?1:0))
            <<"unsupported=msg_importeddate_f/_r(no import record),msg_sale_year_f/_r(no per-row year),"
              "msg_shootdate_f/_r(no EXIF date),msg_trophy_earned_f/_r(no trophy data)";
        // The machine-readable half of the same record: the rows this build offers, and the four firmware
        // pairs it cannot, each with the missing source. Written beside the item-menu dump a scripted run
        // already asks for (XMB_DUMP_ENTRIES), so D10 is checkable without reading the log.
        if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
            QJsonArray rows;
            for(const auto& row:options_)rows.push_back(QJsonObject{{"title",row.title},
                {"key",row.labelKey},{"command",int(row.command)}});
            const QJsonArray unsupported{
                QJsonObject{{"keys","msg_importeddate_f/_r"},
                            {"reason","no import record: a page row is a file or folder and MediaEntry carries only path/title/size/modified, with no first-seen time"}},
                QJsonObject{{"keys","msg_sale_year_f/_r"},
                            {"reason","no per-row year: Entry and MediaEntry have no year field and no row source reads one"}},
                QJsonObject{{"keys","msg_shootdate_f/_r"},
                            {"reason","no EXIF or container date parser exists in src/"}},
                QJsonObject{{"keys","msg_trophy_earned_f/_r"},
                            {"reason","no trophy data: the PSN/trophy services are absent by decision"}}};
            QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".sort.json");
            if(file.open(QIODevice::WriteOnly))
                file.write(QJsonDocument(QJsonObject{{"page",pages_.isEmpty()?QString{}:pages_.last().title},
                    {"rows",rows},{"unsupported",unsupported}}).toJson());
        }
        emit changed();
        break;
    }
    case OptionCommand::SortCreatedAscending:
    case OptionCommand::SortCreatedDescending:
    case OptionCommand::SortUpdatedAscending:
    case OptionCommand::SortUpdatedDescending: {
        const bool created=option.command==OptionCommand::SortCreatedAscending||option.command==OptionCommand::SortCreatedDescending;
        const bool ascending=option.command==OptionCommand::SortCreatedAscending||option.command==OptionCommand::SortUpdatedAscending;
        resetOptions();
        sortPageByDate(ascending,created);
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        emit changed();
        break;
    }
    // Group Content, the frame's second utility row (frames/menu-submenu-group-content.png shows the level it
    // opens: a list of `By ...` keys with All last, focused on the current choice). The level's own rows are
    // built by buildGroupContentMenu(); choosing one rebuilds the page, and Circle steps back to the item list
    // exactly like Sort By.
    case OptionCommand::GroupMenu: {
        buildGroupContentMenu();
        interactionAudio_.play(InteractionAudio::Cue::Options);
        emit changed();
        break;
    }
    case OptionCommand::GroupByTitle:
    case OptionCommand::GroupAll:
        resetOptions();
        groupPageBy(option.command);
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        emit changed();
        break;
    // Forget this row's remembered position and rebuild the page it was listed on. Removing is a
    // destructive change to this build's own state, so it asks first with the firmware's two-button box:
    // buttons msg_ok/msg_cancel on Cross/Circle, snd_decide only when Cross accepts (TRACED 0x00341870).
    // The body is this build's wording - no firmware string covers removing a resume point.
    case OptionCommand::RemoveResume: {
        resetOptions();
        const auto id=entry.id,title=entry.title;
        showConfirm(QObject::tr("Continue Watching"),
            QObject::tr("Do you want to remove this title from Continue Watching?"),
            [this,id,title]{removeContinueEntry(id.mid(QStringLiteral("continue:").size()),title);});
        interactionAudio_.play(InteractionAudio::Cue::Options);
        emit changed();
        break;
    }
    case OptionCommand::PlayFromBeginning:
        // msg_play_begin, the firmware's restart row. A Jellyfin item restarts through its own re-open with
        // the resume bypass; a local file sets the one-shot flag startQueuedMedia reads below.
        resetOptions();
        if(entry.action==Action::JellyfinPlay)openJellyfinMedia(entry,true);
        else if(entry.action==Action::File) {playFromBeginning_=true;openFile(entry);}
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        emit changed();
        break;
    // Listed but not choosable (the copy/delete backend does not exist yet), so this is unreachable until a
    // future backend enables the row and opens its own page here.
    case OptionCommand::Copy:
    case OptionCommand::Delete:
        interactionAudio_.play(InteractionAudio::Cue::Error);
        qInfo().noquote()<<"shell: option unavailable"<<option.title<<option.note;
        emit changed();
        break;
    // The in-player rows. Choosing one keeps the panel open so the marker moves on the spot, which is
    // also what makes the state checkable from a capture; Circle is what closes it.
    case OptionCommand::SubtitlesOff:
        chooseSubtitleTrack(-1);
        refreshPlayerOptions();
        break;
    case OptionCommand::SubtitleTrack:
        chooseSubtitleTrack(option.index);
        refreshPlayerOptions();
        break;
    case OptionCommand::AudioTrack:
        chooseAudioTrack(option.index);
        break;
    case OptionCommand::RepeatMode:
        cycleMediaRepeat();
        refreshPlayerOptions();
        break;
    case OptionCommand::ToggleShuffle:
        toggleMediaShuffle();
        refreshPlayerOptions();
        break;
    }
}
void Shell::optionsClose() {
    touch();
    if(!optionsOpen_)return;
    if(optionsNested_) { // Circle steps back to the item list; the menu stays open
        // optionsParent_ is the same row vector that was on screen, so its `dividerBefore` flag survives the
        // nesting round-trip; the nested level itself is one group and carries no divider (H3).
        options_=optionsParent_;optionsParent_.clear();optionsNested_=false;optionsTitle_.clear();optionsSelection_=0;
        int groups=options_.isEmpty()?0:1;
        for(const auto& row:options_)if(row.dividerBefore)++groups;
        interactionAudio_.play(InteractionAudio::Cue::Back);
        qInfo().noquote()<<"shell: options back to item list groups="<<groups;
        emit changed();
        return;
    }
    dismissOptions();
}
void Shell::dismissOptions() {
    if(!optionsOpen_)return;
    resetOptions();
    interactionAudio_.play(InteractionAudio::Cue::Back);
    qInfo().noquote()<<"shell: options closed";
    emit changed();
}
// ---- The in-player panel: subtitles, audio tracks, repeat and shuffle -------------------------------
// The firmware's own video control panel is a single row of buttons in videoplayer_plugin.rco and it
// includes videoplayer_cp_subtitle_button, videoplayer_cp_audio_change_button and
// videoplayer_cp_repeat_button. Those three things are exactly what this panel offers, drawn with the
// XMB's right-hand options panel language (the item menu's own shape) instead of inventing a second
// overlay: rows, a marker on the current choice, Up/Down + Cross, Circle closes, and it owns input
// while it is up (view.cpp's key and pad blocks return early for it, exactly as for the item menu).
void Shell::buildPlayerOptions() {
    resetOptions();
    playerPanel_=true;
    optionsTitle_=QObject::tr("Playback");
    // A Jellyfin stream's tracks are the server's own MediaStreams; a local file's are the sidecars
    // beside it and what QMediaPlayer reports. The panel keeps the same honesty rule in both cases:
    // a track this build cannot switch is listed with the reason and cannot be chosen.
    const bool streamed=mediaProvider()=="Jellyfin" && !jellyfinPlayback_.streams.isEmpty();
    options_.push_back({QObject::tr("Subtitles Off"),OptionCommand::SubtitlesOff,-1,true,subtitleTrackIndex_<0,{}});
    if(streamed) {
        int seen=0;
        for(const auto& stream:jellyfinPlayback_.streams) {
            if(stream.type!="Subtitle")continue;
            ++seen;
            MenuOption row{QObject::tr("Subtitles %1: %2").arg(seen).arg(stream.title),OptionCommand::SubtitleTrack};
            row.index=-1;row.enabled=false;
            row.note=stream.external
                ?QObject::tr("External subtitle stream: this build does not download subtitle streams.")
                :QObject::tr("Subtitle stream inside the file: the media backend cannot switch stream subtitles.");
            options_.push_back(row);
        }
        if(!seen) {
            MenuOption row{QObject::tr("Subtitles: none in this stream"),OptionCommand::SubtitlesOff};
            row.enabled=false;
            row.note=QObject::tr("The server's PlaybackInfo lists no subtitle stream for this item.");
            options_.push_back(row);
        }
    } else if(subtitleTracks_.isEmpty()) {
        MenuOption row{QObject::tr("Subtitles: none found"),OptionCommand::SubtitlesOff};
        row.enabled=false;
        row.note=mediaProvider()=="Jellyfin"
            ?QObject::tr("Streamed media: sidecar subtitles are read for local video files only.")
            :QObject::tr("No .srt or .vtt file beside this video. Embedded subtitle streams are not switched yet.");
        options_.push_back(row);
    } else {
        for(int i=0;i<subtitleTracks_.size();++i) {
            const auto& track=subtitleTracks_[i];
            MenuOption row{QObject::tr("Subtitles: %1").arg(track.name),OptionCommand::SubtitleTrack};
            row.index=i;row.active=i==subtitleTrackIndex_;
            row.note=QObject::tr("%1 - %2 cues, %3").arg(QFileInfo(track.path).fileName())
                .arg(track.cues.size()).arg(track.skipped>0?QObject::tr("%1 block(s) skipped").arg(track.skipped):QObject::tr("all blocks read"));
            options_.push_back(row);
        }
    }
    // What the player itself reports, never a list this build invents.
    refreshAudioTracks();
    if(streamed) {
        int seen=0;
        for(const auto& stream:jellyfinPlayback_.streams) {
            if(stream.type!="Audio")continue;
            ++seen;
            MenuOption row{QObject::tr("Audio %1: %2").arg(seen).arg(stream.title),OptionCommand::AudioTrack};
            row.index=seen-1;row.active=(seen-1)==activeAudioTrack_;
            if(seen-1>=audioTracks_.size()) {
                row.enabled=false;
                row.note=QObject::tr("The player reports %1 audio track(s) for this stream, so the server's track %2 cannot be selected.")
                    .arg(audioTracks_.size()).arg(seen);
            } else if(!audioTracks_[seen-1].switchable) {
                row.enabled=false;row.note=audioTracks_[seen-1].reason;
            } else if(audioTracks_.size()>1) {
                row.note=QObject::tr("Selects track %1 of the %2 the player reports.").arg(seen).arg(audioTracks_.size());
            }
            options_.push_back(row);
        }
        if(!seen) {
            MenuOption row{QObject::tr("Audio: none in this stream"),OptionCommand::AudioTrack};
            row.enabled=false;
            row.note=QObject::tr("The server's PlaybackInfo lists no audio stream for this item.");
            options_.push_back(row);
        }
    } else if(audioTracks_.isEmpty()) {
        MenuOption row{QObject::tr("Audio: not reported"),OptionCommand::AudioTrack};
        row.enabled=false;
        row.note=QObject::tr("The media backend reports no audio track for this file, so there is nothing to choose.");
        options_.push_back(row);
    } else {
        for(int i=0;i<audioTracks_.size();++i) {
            MenuOption row{QObject::tr("Audio %1: %2").arg(i+1).arg(audioTracks_[i].label),OptionCommand::AudioTrack};
            row.index=i;row.active=i==activeAudioTrack_;
            row.enabled=audioTracks_[i].switchable;row.note=audioTracks_[i].reason;
            if(row.note.isEmpty() && audioTracks_.size()==1)
                row.note=QObject::tr("The player reports one audio track, so there is no second track to switch to.");
            options_.push_back(row);
        }
    }
    // Repeat and Shuffle live here now, which is what frees Triangle for this panel.
    static const char* const repeatNames[]={"Off","One","All"};
    MenuOption repeat{QObject::tr("Repeat: %1").arg(QString::fromLatin1(repeatNames[std::clamp(mediaRepeatMode(),0,2)])),
        OptionCommand::RepeatMode};
    repeat.note=QObject::tr("Also on the R key while a video plays.");
    options_.push_back(repeat);
    MenuOption shuffle{QObject::tr("Shuffle: %1").arg(preferences_.value("media/shuffle").toBool()?QObject::tr("On"):QObject::tr("Off")),
        OptionCommand::ToggleShuffle};
    options_.push_back(shuffle);
}
void Shell::refreshPlayerOptions() {
    if(!optionsOpen_ || !playerPanel_)return;
    const int keepCommand=options_.isEmpty()?-1:int(options_.value(optionsSelection_).command);
    const int keepIndex=options_.value(optionsSelection_).index;
    buildPlayerOptions(); // resets the list (and with it the open flag); the panel stays up
    optionsOpen_=true;
    optionsSelection_=0;
    for(int i=0;i<options_.size();++i)
        if(int(options_[i].command)==keepCommand && options_[i].index==keepIndex){optionsSelection_=i;break;}
    optionsSelection_=std::clamp(optionsSelection_,0,std::max(0,int(options_.size())-1));
    emit changed();
}
// Sidecar subtitles. The file's own sidecar choice is remembered for the session, the cue is looked up
// against the player's position on demand (so it is right even when the backend's position signals are
// coarse), and a change is traced only when the cue actually changes.
void Shell::prepareSubtitles(const QString& path) {
    subtitleTracks_.clear();subtitleTrackIndex_=-1;subtitleCueNumber_=0;subtitleMediaPath_=path;
    audioTracks_.clear();audioRejected_.clear();activeAudioTrack_=-1;
    if(path.isEmpty())return;
    const auto discovery=subtitles::discover(path);
    subtitleTracks_=discovery.tracks;
    for(const auto& reason:discovery.rejected)qWarning().noquote()<<"subtitle: ignored"<<reason;
    if(subtitleLog_) {
        if(subtitleTracks_.isEmpty())qInfo().noquote()<<"subtitle: no sidecar beside"<<QFileInfo(path).fileName();
        for(const auto& track:subtitleTracks_)
            qInfo().noquote()<<"subtitle: sidecar"<<QFileInfo(track.path).fileName()<<track.format<<track.cues.size()
                <<"cues"<<track.bytes<<"bytes; skipped"<<track.skipped<<"; name"<<track.name;
    }
    // The same file plays again: its own choice comes back. A file this session has not seen starts on
    // the default sidecar (the untagged one, or the first), which is what a player does with one
    // matching subtitle file, and never on nothing at all.
    QString chosen;
    if(subtitleChoice_.contains(path)) {
        chosen=subtitleChoice_.value(path);
        if(subtitleLog_)qInfo().noquote()<<"subtitle: session choice for this file is"
            <<(chosen.isEmpty()?"Off":QFileInfo(chosen).fileName());
    } else if(!subtitleTracks_.isEmpty()) {
        chosen=subtitleTracks_.first().path;
        if(subtitleLog_)qInfo().noquote()<<"subtitle: auto-selected"<<QFileInfo(chosen).fileName()
            <<"(first time this file plays; Subtitles Off is remembered if chosen)";
    }
    int index=-1;
    for(int i=0;i<subtitleTracks_.size();++i)if(subtitleTracks_[i].path==chosen){index=i;break;}
    applySubtitleTrack(index);
}
void Shell::applySubtitleTrack(int index) {
    if(index<0 || index>=subtitleTracks_.size())index=-1;
    subtitleTrackIndex_=index;subtitleCueNumber_=0;
    // Per-file memory for this session: a path, or empty for the explicit Off.
    if(!subtitleMediaPath_.isEmpty())
        subtitleChoice_.insert(subtitleMediaPath_,index<0?QString{}:subtitleTracks_[index].path);
    if(subtitleLog_)
        qInfo().noquote()<<(index<0?"subtitle: off":QString("subtitle: track %1 (%2)").arg(
            QFileInfo(subtitleTracks_[index].path).fileName(),subtitleTracks_[index].name));
    emit changed();
}
QString Shell::subtitleFile() const {
    return subtitleTrackIndex_<0?QString{}:subtitleTracks_.value(subtitleTrackIndex_).path;
}
QString Shell::subtitleLabel() const {
    return subtitleTrackIndex_<0?QString{}:subtitleTracks_.value(subtitleTrackIndex_).name;
}
QString Shell::subtitleCueText() {
    if(subtitleTrackIndex_<0 || subtitleTrackIndex_>=subtitleTracks_.size()){subtitleCueNumber_=0;return {};}
    const auto& cues=subtitleTracks_[subtitleTrackIndex_].cues;
    const qint64 at=player_->position();
    const int index=subtitles::cueAt(cues,at);
    const int number=index<0?0:index+1;
    if(number!=subtitleCueNumber_) {
        subtitleCueNumber_=number;
        if(subtitleLog_) {
            if(index<0)qInfo().noquote()<<"subtitle: cue cleared at"<<at<<"ms";
            else qInfo().noquote()<<"subtitle: cue"<<number<<"at"<<at<<"ms"
                <<QString("(%1-%2 ms)").arg(cues[index].start).arg(cues[index].end)
                <<cues[index].text.left(80).replace('\n',QChar(' '));
        }
    }
    return index<0?QString{}:cues[index].text;
}
int Shell::subtitleCueNumber() {
    subtitleCueText(); // the lookup above is the single authority for both the number and the text
    return subtitleCueNumber_;
}
void Shell::chooseSubtitleTrack(int index) {
    if(!mediaOpen_ || photoOpen())return;
    if(index<-1 || index>=subtitleTracks_.size())return;
    if(index==subtitleTrackIndex_)return; // already the choice: nothing to do, nothing to claim
    applySubtitleTrack(index);
    interactionAudio_.play(index<0?InteractionAudio::Cue::Back:InteractionAudio::Cue::Confirm);
}
// Audio tracks. QMediaPlayer reports them and takes setActiveAudioTrack(); whether the backend honours
// it is a fact only the read-back can establish, so a row is marked unavailable the moment the player's
// own activeAudioTrack() does not follow the request, and the reason is carried into the panel.
void Shell::refreshAudioTracks() {
    audioTracks_.clear();activeAudioTrack_=-1;
    if(!mediaOpen_ || photoOpen())return;
    const auto reported=player_->audioTracks();
    for(int i=0;i<reported.size();++i) {
        const auto& data=reported[i];
        QString label=data.value(QMediaMetaData::Language).toString().trimmed();
        const QString codec=data.value(QMediaMetaData::AudioCodec).toString().trimmed();
        if(label.isEmpty())label=data.value(QMediaMetaData::Title).toString().trimmed();
        if(label.isEmpty())label=codec;
        else if(!codec.isEmpty())label+=" ("+codec+")";
        if(label.isEmpty())label=QObject::tr("Track %1").arg(i+1);
        AudioTrackInfo track;
        track.label=label;
        if(audioRejected_.contains(i)){track.switchable=false;track.reason=audioRejected_.value(i);}
        audioTracks_.push_back(track);
    }
    activeAudioTrack_=player_->activeAudioTrack();
    if(activeAudioTrack_>=audioTracks_.size())activeAudioTrack_=-1;
    if(subtitleLog_) {
        QStringList labels;for(const auto& track:audioTracks_)labels<<track.label;
        qInfo().noquote()<<"audio: player reports"<<audioTracks_.size()<<"track(s); active"
            <<(activeAudioTrack_<0?QString("not reported"):QString::number(activeAudioTrack_+1))
            <<labels.join('|');
    }
}
void Shell::chooseAudioTrack(int index) {
    if(!mediaOpen_ || photoOpen() || index<0 || index>=audioTracks_.size())return;
    if(index==activeAudioTrack_)return;
    if(!audioTracks_[index].switchable)return;
    player_->setActiveAudioTrack(index);
    if(player_->activeAudioTrack()==index) {
        activeAudioTrack_=index;
        if(subtitleLog_)qInfo().noquote()<<"audio: switched to track"<<index+1<<audioTracks_[index].label;
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        refreshPlayerOptions();
        return;
    }
    // A backend may apply the selection on its next demux step, so the failure is only declared after
    // one more read rather than on the immediate one. The file is re-checked, so a rejection can never
    // land on a track list that belongs to a different file.
    const QString file=subtitleMediaPath_;
    QTimer::singleShot(400,this,[this,index,file]{
        if(!mediaOpen_ || file!=subtitleMediaPath_ || index>=audioTracks_.size())return;
        const int after=player_->activeAudioTrack();
        if(after==index) {
            activeAudioTrack_=index;
            if(subtitleLog_)qInfo().noquote()<<"audio: switched to track"<<index+1<<audioTracks_[index].label;
        } else {
            const QString reason=QObject::tr("This media backend did not switch: it still reports track %1.").arg(after+1);
            audioRejected_.insert(index,reason);
            audioTracks_[index].switchable=false;audioTracks_[index].reason=reason;
            qWarning().noquote()<<"audio: cannot switch to track"<<index+1<<"- the player still reports"
                <<(after<0?QString("no track"):QString::number(after+1));
        }
        refreshPlayerOptions();
    });
    emit changed();
}
void Shell::showEntryInformation(const Entry& entry) {
    // The firmware's information page (D5; audit/DIALOGS-FIRMWARE.md 2.5) labels its rows with its own
    // strings from explore_plugin_full: Format, Size, Created, Filmed, Resolution, Details, plus Unknown
    // and "-" for values the system does not have. There is no firmware string for a filesystem path or
    // for a "Kind", so the type goes in the Format row and the path is the one row marked native.
    const auto label=[this](const char* key,const char* fallback){return assets_.text("explore_plugin_full",key,fallback);};
    const QString unknown=label("msg_info_unknown","Unknown");
    const auto row=[&label](const char* key,const char* fallback,const QString& value){
        return label(key,fallback)+": "+value;};
    // A Download Management row already carries its own information page (State, Progress, the pause/
    // resume note), so the panel shows that page rather than wrapping it in the generic rows.
    if(entry.id.startsWith("download:")) {
        showMessage(entry.value,MessageKind::Info);
        qInfo().noquote()<<"shell: options information title="<<entry.title<<"(download page)";
        return;
    }
    // Continue Watching carries the facts this page should show from its own record: when the position was
    // written, how long the title is, and - for a row whose file is gone - why it cannot be played.
    if(entry.id=="continue" || entry.id.startsWith("continue:")) {
        QStringList rows;
        rows<<row("msg_format","Format",entryKind(entry));
        if(entry.id=="continue") {
            rows<<QObject::tr("Items: %1").arg(rememberedResumeCount());
            rows<<QObject::tr("Note: a position is kept only while a title is half-watched, and a finished title is removed.");
        } else {
            if(!entry.value.isEmpty())rows<<QStringLiteral("Path (native): ")+QDir::toNativeSeparators(entry.value);
            const auto record=resumeMap(preferences_.value("media/resume").toString())
                .value(entry.id.mid(QStringLiteral("continue:").size())).toObject();
            if(!record.isEmpty()) {
                rows<<QObject::tr("Position: %1").arg(MediaControls::formatTime(qint64(record.value("ms").toDouble(0))));
                if(record.value("dur").toDouble(0)>0)
                    rows<<QObject::tr("Duration: %1").arg(MediaControls::formatTime(qint64(record.value("dur").toDouble(0))));
                const auto at=QDateTime::fromSecsSinceEpoch(qint64(record.value("at").toDouble(0)));
                if(at.isValid())rows<<row("msg_created_date_time","Created",at.toString("yyyy/MM/dd HH:mm"));
                rows<<QObject::tr("Remembered as: %1").arg(entry.id.mid(QStringLiteral("continue:").size()));
            }
            rows<<QObject::tr("State: ")+(QFileInfo::exists(entry.value)
                ?QObject::tr("the file is on disk and resumes at this position")
                :QObject::tr("the file is no longer on disk; Triangle removes this row"));
        }
        showMessage(entry.title,rows.join('\n'),MessageKind::Info);
        qInfo().noquote()<<"shell: options information title="<<entry.title<<"rows="<<rows.size()<<"(continue watching)";
        return;
    }
    QStringList rows;
    rows<<row("msg_format","Format",entryKind(entry));
    // A file's own facts come from the filesystem, which is the same source the console reads a Size and
    // a Created from. Anything this build cannot know stays Unknown rather than being left out, which is
    // what the firmware's own msg_info_unknown is for.
    const QFileInfo file(entry.value);
    const bool onDisk=entry.action==Action::File && !entry.value.isEmpty() && file.isFile();
    rows<<row("msg_size","Size",onDisk?downloadSize(file.size()):unknown);
    rows<<row("msg_created_date_time","Created",onDisk?file.lastModified().toString("yyyy/MM/dd HH:mm"):unknown);
    if(entry.action==Action::File) {
        // Resolution belongs to the media the console reads it from. A still image's size is in its header,
        // so it is read without decoding the picture; a video's is not available to this build.
        QString resolution=unknown;
        if(categories_[category_].id=="photo") {
            QImageReader reader(entry.value);
            if(reader.canRead()){const auto size=reader.size();if(size.isValid())resolution=QStringLiteral("%1 x %2").arg(size.width()).arg(size.height());}
        }
        rows<<row("msg_resolution","Resolution",resolution);
    }
    if(!entry.detail.isEmpty())rows<<row("msg_info_detail","Details",entry.detail);
    if(entry.action==Action::Folder || entry.action==Action::File || entry.action==Action::Library)
        rows<<QStringLiteral("Path (native): ")+QDir::toNativeSeparators(entry.value);
    // Title, blank line, then "Key: value" rows: the information page's own shape.
    showMessage(entry.title,rows.join('\n'),MessageKind::Info);
    qInfo().noquote()<<"shell: options information title="<<entry.title<<"rows="<<rows.size();
}
QStringList Shell::optionsStructure() const {
    // The panel's own shape as data, in build order: a row's title plus the flags it carries, so a scripted run
    // can assert the two groups and the two chevron/badge rows without reading the drawing code.
    QStringList lines;
    for(const auto& option:options_) {
        QString flags;
        if(option.dividerBefore)flags+=QLatin1Char('B');   // the group break starts on this row
        if(option.submenu)flags+=QLatin1Char('>');
        if(option.gateBadge)flags+=QLatin1Char('G');
        if(!option.enabled)flags+=QLatin1Char('X');
        lines<<option.title+(flags.isEmpty()?QString():QStringLiteral("[")+flags+QLatin1Char(']'));
    }
    return lines;
}
void Shell::buildGroupContentMenu() {
    // The frame's level (frames/menu-submenu-group-content.png) lists the group keys with All last and focuses
    // the current choice. The keys a list can offer depend on the list: that console's game list offered By
    // Format and By Album because its rows carry a media format. This build's rows carry a title and nothing
    // else, so the one key it can compute is the first letter - and All, which makes the grouping reversible.
    // The seven other keys the firmware's table carries (msg_album_folder, msg_artist_folder, msg_genre_folder,
    // msg_format_folder, msg_month_folder, msg_year_folder, msg_track_folder, msg_sale_year_folder) would group
    // nothing here, so they are not listed: a label that groups nothing is worse than its absence (F3/H3).
    int parentGroups=options_.isEmpty()?0:1;
    for(const auto& row:options_)if(row.dividerBefore)++parentGroups;
    optionsParent_=options_;optionsNested_=true;optionsTitle_=optionLabel({"msg_folder_class","Group Content"});
    options_.clear();
    const bool grouped=!pages_.isEmpty()&&pages_.last().grouped;
    addOption({"msg_title_folder","By Title"},OptionCommand::GroupByTitle,"group-title");
    options_.last().active=grouped;
    addOption({"msg_all","All"},OptionCommand::GroupAll,"group-all");
    options_.last().active=!grouped;
    optionsSelection_=grouped?0:1;
    interactionAudio_.play(InteractionAudio::Cue::Options);
    QStringList titles;for(const auto& row:options_)titles<<row.title;
    qInfo().noquote()<<"shell: options group menu opened rows="<<options_.size()
        <<"labels="<<titles.join('|')<<"groups=1 parentGroups="<<parentGroups
        <<"current="<<(grouped?QStringLiteral("by-title"):QStringLiteral("all"))
        <<"unlisted=msg_album_folder,msg_artist_folder,msg_genre_folder,msg_format_folder,msg_month_folder,"
          "msg_year_folder,msg_track_folder,msg_sale_year_folder(no row on these pages carries the key)";
    if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
        QJsonArray rows;
        for(const auto& row:options_)rows.push_back(QJsonObject{{"title",row.title},{"key",row.labelKey},
            {"command",int(row.command)},{"active",row.active}});
        QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".group.json");
        if(file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(QJsonObject{{"page",pages_.isEmpty()?QString{}:pages_.last().title},
                {"rows",rows},{"grouped",grouped},
                {"unlisted","msg_album_folder,msg_artist_folder,msg_genre_folder,msg_format_folder,"
                            "msg_month_folder,msg_year_folder,msg_track_folder,msg_sale_year_folder"}}).toJson());
    }
}
void Shell::groupPageBy(OptionCommand command) {
    if(pages_.isEmpty())return;
    auto& page=pages_.last();
    if(command==OptionCommand::GroupAll) {
        if(!page.grouped)return;
        const int count=page.groupedSource.size();
        page.entries=page.groupedSource;page.groupedSource.clear();page.grouped=false;
        page.selection=0;
        qInfo().noquote()<<QStringLiteral("shell: group content restored page=\"%1\" rows=%2")
            .arg(page.title).arg(count);
        return;
    }
    // By Title. The ungrouped rows are kept on the page, so All puts them back without a rescan and a group
    // folder can be opened from them. The folders are ordinary rows of the page, so the cursor, the label
    // column, the value column and the information page all work without a second row class.
    if(!page.grouped)page.groupedSource=page.entries;
    QMap<QString,QVector<Entry>> groups;
    for(const auto& row:page.groupedSource)groups[groupLetter(row.title)].push_back(row);
    page.entries.clear();
    for(auto it=groups.cbegin();it!=groups.cend();++it) {
        auto folder=item(QStringLiteral("group:")+it.key(),it.key(),QStringLiteral("item_tex_plain_folder"),
            QStringLiteral("explore_plugin_full"),Action::GroupFolder,it.key());
        folder.detail=QObject::tr("%1 item(s)").arg(it.value().size());
        folder.source=QStringLiteral("native");
        page.entries.push_back(std::move(folder));
    }
    page.grouped=true;page.selection=0;
    qInfo().noquote()<<QStringLiteral("shell: group content by title page=\"%1\" rows=%2 groups=%3 keys=%4")
        .arg(page.title).arg(page.groupedSource.size()).arg(page.entries.size())
        .arg([&groups]{QStringList keys;for(auto it=groups.cbegin();it!=groups.cend();++it)keys<<it.key();
            return keys.join(QChar(','));}());
}
void Shell::openGroupFolder(const Entry& entry) {
    // A group folder is a page of the rows that share one key: the same entries, in the same order, so a row's
    // action, value and information page are unchanged. Circle returns to the letter list, whose All puts the
    // page back together.
    if(pages_.isEmpty() || !pages_.last().grouped)return;
    Page page;page.title=entry.value;
    for(const auto& row:pages_.last().groupedSource)
        if(groupLetter(row.title)==entry.value)page.entries.push_back(row);
    if(page.entries.isEmpty())return;
    pages_.push_back(std::move(page));
    qInfo().noquote()<<QStringLiteral("shell: group folder opened key=%1 rows=%2")
        .arg(entry.value).arg(int(pages_.last().entries.size()));
}
void Shell::sortPageByDate(bool ascending,bool created) {
    auto& page=pages_.last();
    if(page.entries.size()<2)return;
    const QString selected=page.entries.value(page.selection).id;
    // The firmware's own sort fields are dates (A3: Creation Date / Update Date / Import Date / Release Year /
    // Film Date / Earned Date). A local row carries the two dates this build can read from the file system;
    // a row with no readable time sorts after the ones that have one rather than being given an invented date.
    const auto stamp=[created](const Entry& entry){
        const QFileInfo info(entry.value);
        const QDateTime when=created?info.birthTime():info.lastModified();
        return when.isValid()?when:info.lastModified();
    };
    std::stable_sort(page.entries.begin(),page.entries.end(),[&stamp,ascending](const Entry& left,const Entry& right){
        const QDateTime l=stamp(left),r=stamp(right);
        const bool leftKnown=l.isValid(),rightKnown=r.isValid();
        if(leftKnown!=rightKnown)return leftKnown;
        if(!leftKnown)return false;
        return ascending?l<r:l>r;
    });
    for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==selected){page.selection=i;break;}
    qInfo().noquote()<<"shell: options sorted page"<<page.title<<(created?"creation date":"update date")
        <<(ascending?"ascending":"descending")<<page.entries.size()<<"rows";
}
void Shell::back() {
    touch();
    // The busy page blocks list actions (DIALOGS-FIRMWARE 2.8; FIDELITY-BEHAVIOUR N10), and Circle is a list
    // action: the console has no way out of page_busy and neither does this. The one exception is kept below -
    // a message page is dismissed first, so a result page is never trapped behind a busy flag.
    if(busy_ && message_.isEmpty()) {
        qInfo().noquote()<<"busy: navigation refused (Circle) while the busy page is up";
        return;
    }
    if(!message_.isEmpty() || mediaOpen_ || !pages_.isEmpty())interactionAudio_.play(InteractionAudio::Cue::Back);
    // Circle dismisses any surface; on a box it is the cancel button, so the accepted action is dropped
    // with the message (clearMessage forgets it). snd_cancel (Cue::Back) is the obvious cancel cue but is
    // UNPROVEN for a dialog - nothing local ties it to one.
    if(!message_.isEmpty()) {
        // The cancel path never consults the answer: No, Yes or no answer row, Circle closes without the
        // action and snd_cancel plays above. The log names the focus the box had when it was cancelled.
        if(dialogLog_ && messageKind_==MessageKind::Box)
            qInfo().noquote()<<"dialog: box cancel choice="
                <<(messageBoxChoices_?(messageBoxChoice_==BoxChoice::Yes?"yes":"no"):"none");
        if(settingsWrite_.stage==WriteStage::Ask)cancelSettingWrite(QStringLiteral("Circle on the ask page"));
        if(steamPrompt_)steamSwitchCancel(QStringLiteral("Circle on the prompt"));
        clearMessage();
        if(settingsWrite_.stage==WriteStage::FinishOk || settingsWrite_.stage==WriteStage::FinishFailed)finishSettingWrite();
    }
    else if(mediaOpen_){
        // Leaving playback remembers where it was, so reopening resumes there. Ten seconds in and
        // fifteen from the end are not worth remembering on long media; short clips use a tenth.
        const qint64 at=player_->position(),total=player_->duration();
        bool worth=total>0 && at>std::max<qint64>(10000,total/20) && at<total-std::max<qint64>(15000,total/20);
        if(!worth && total>0 && total<=60000)worth=at>total/10 && at<total*9/10;
        if(!resumePath_.isEmpty() && worth)storeResumePosition(resumePath_,at,total,mediaTitle_);
        else clearResumePosition(resumePath_);
        resumePath_.clear();resumePending_=0;mediaDisplayPinned_=false;
        ++photoRequest_;++mediaRequest_;jellyfinRequest_=0;finishJellyfinPlayback();jellyfinQueue_=false;mediaOpen_=false;mediaQueue_.clear();mediaFailures_=0;slideshow_.stop();photo_={};player_->stop();player_->setSource({});loading_=false;status_.clear();emit mediaFrame({});
        // Nothing is playing, so no subtitle file or audio track is current. The per-file choice stays in
        // subtitleChoice_, which is what makes reopening the same file come back on the same track.
        subtitleTracks_.clear();subtitleTrackIndex_=-1;subtitleCueNumber_=0;subtitleMediaPath_.clear();
        audioTracks_.clear();audioRejected_.clear();activeAudioTrack_=-1;}
    // Circle on a sign-in page is the page's Cancel row: it leaves the page and aborts a request still in
    // flight, so Back can never strand an authentication the user has walked away from.
    else if(!pages_.isEmpty() && pages_.last().signIn)cancelJellyfinSignInPage();
    else if(!pages_.isEmpty()){library_.cancel();scanRequest_=0;jellyfinRequest_=0;loading_=false;status_.clear();pages_.removeLast();}
    emit changed();
}
void Shell::chooseLibrary(const QString& folder) {
    if(folder.isEmpty())return;
    if(preferences_.setValue("library/"+categories_[category_].id,folder)){openDirectory(folder);emit changed();}
}
void Shell::openDirectory(const QString& path) {
    if(pages_.size()>=32){showMessage("The maximum folder depth has been reached.",MessageKind::Page);return;}
    Page page;page.title=QFileInfo(path).fileName();page.directory=path;
    const QString kind=categories_[category_].id;
    pages_.push_back(std::move(page));
    loading_=true;status_=QObject::tr("Reading media folder...");
    scanRequest_=library_.scan(path,kind=="photo"?MediaKind::Photo:kind=="music"?MediaKind::Music:MediaKind::Video);
    // The busy page is a state, not a status line (FIDELITY-BEHAVIOUR N12/X5): from here until the scan's
    // rows land, busy() is true, navigation is refused (N10/N11) and View draws page_busy.
    qInfo().noquote()<<QStringLiteral("busy: page_busy raised (list scan) page=\"%1\" request=%2")
        .arg(QDir::toNativeSeparators(path)).arg(scanRequest_);
}
void Shell::openFile(const Entry& entry) {
    jellyfinQueue_=false;
    if(categories_[category_].id=="photo") {
        const auto request=++photoRequest_;loading_=true;photoRotation_=0;photo_={};emit mediaFrame({});
        auto* watcher=new QFutureWatcher<QImage>(this);
        connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,request]{
            watcher->deleteLater();if(request!=photoRequest_ || !mediaOpen_)return;
            loading_=false;photo_=watcher->result();
            if(photo_.isNull()){slideshow_.stop();mediaOpen_=false;showMessage("This image could not be opened.",MessageKind::Page);}else showPhoto();
            emit changed();
        });
        watcher->setFuture(QtConcurrent::run([path=entry.value]{
            QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();
            if(!size.isValid() || size.width()>65536 || size.height()>65536)return QImage{};
            if(size.width()>3840 || size.height()>2160)reader.setScaledSize(size.scaled(3840,2160,Qt::KeepAspectRatio));
            return reader.read();
        }));
    } else {
        QVector<PlaybackQueue::Item> queue;int selected=-1;
        for(const auto& file:entries())if(file.action==Action::File) {
            if(file.id==entry.id)selected=int(queue.size());
            queue.push_back({file.value,file.title});
        }
        if(selected<0){selected=int(queue.size());queue.push_back({entry.value,entry.title});}
        mediaQueue_.clear();mediaQueue_.setRepeat(static_cast<PlaybackQueue::Repeat>(mediaRepeatMode()));
        mediaQueue_.setShuffle(preferences_.value("media/shuffle").toBool());mediaQueue_.reset(std::move(queue),selected);
        mediaFailures_=0;startQueuedMedia();return;
    }
    mediaOpen_=true;mediaTitle_=entry.title;
}
int Shell::mediaRepeatMode() const {
    // Preserve the earlier Repeat-current preference until the user chooses a new mode.
    if(!preferences_.contains("media/repeatMode") && preferences_.value("media/repeat").toBool())return 1;
    return preferences_.value("media/repeatMode").toInt();
}
qint64 Shell::resumePositionFor(const QString& path) const {
    if(path.isEmpty())return 0;
    // Both shapes answer: a migrated record and a legacy bare number resume identically.
    return resumeMilliseconds(resumeMap(preferences_.value("media/resume").toString()).value(resumeIdFor(path)));
}
void Shell::storeResumePosition(const QString& path,qint64 positionMs,qint64 durationMs,const QString& title) {
    if(path.isEmpty()||positionMs<=0)return;
    auto map=resumeMap(preferences_.value("media/resume").toString());
    const QString id=resumeIdFor(path);
    map.insert(id,resumeRecord(path,positionMs,durationMs,title,resumeSecondsNow()));
    QStringList evicted;boundResumeMap(map,&evicted);
    for(const auto& lost:evicted)qWarning().noquote()<<"continue: dropped resume point"<<lost<<"(the map is capped at 64)";
    preferences_.setValue("media/resume",resumeJson(map));
    qInfo().noquote()<<QString("continue: stored %1 ms%2 for %3")
        .arg(positionMs).arg(durationMs>0?QString(" of %1 ms").arg(durationMs):QString{})
        .arg(title.isEmpty()?QFileInfo(path).fileName():title);
}
void Shell::clearResumePosition(const QString& path) {
    if(path.isEmpty())return;
    auto map=resumeMap(preferences_.value("media/resume").toString());
    const QString id=resumeIdFor(path);
    if(!map.contains(id))return;
    map.remove(id);
    preferences_.setValue("media/resume",resumeJson(map));
    qInfo().noquote()<<QString("continue: cleared the resume point for %1; %2 still remembered")
        .arg(QFileInfo(path).fileName()).arg(map.size());
}
// Migration, run once at load (the Shell constructor). The old map held {id: milliseconds} with no path,
// so nothing could be listed: each such entry is re-keyed as a record with no path (marked pathless),
// resolved against the configured media libraries by hashing their playable files, and dropped with a
// logged reason when no file matches. A data directory that is already in the new shape is not rewritten.
void Shell::migrateResumePoints() {
    const QString key="media/resume";
    const auto raw=preferences_.value(key).toString();
    if(raw.trimmed().isEmpty())return;
    const auto map=resumeMap(raw);
    if(map.isEmpty())return;
    QJsonObject next;QSet<QString> pathless;QStringList unreadable;int legacy=0;
    for(auto it=map.begin();it!=map.end();++it) {
        QJsonObject record;
        if(it.value().isObject())record=it.value().toObject();
        else if(it.value().isDouble() && std::isfinite(it.value().toDouble()) &&
                it.value().toDouble()>=0.0 && it.value().toDouble()<=86400000.0) {
            // The old shape. It carries no path, no title and no time; the time it is given is the
            // migration time, because an invented earlier one would be a history this build does not have.
            record=resumeRecord({},qint64(it.value().toDouble()),0,{},resumeSecondsNow());
            ++legacy;
        } else { unreadable<<it.key();continue; }   // neither shape this build accepts
        if(record.value("path").toString().isEmpty())pathless.insert(it.key());
        next.insert(it.key(),record);
    }
    for(const auto& id:unreadable)
        qWarning().noquote()<<"continue: dropped resume point"<<id
            <<":the value is neither a record nor a 0-24 h millisecond count";
    if(pathless.isEmpty() && unreadable.isEmpty())return;   // nothing to migrate: leave the stored bytes alone
    // The roots to resolve against: the same three media libraries the columns read. Nothing else on disk
    // is touched, and a folder that is not configured is not guessed at.
    QStringList roots;QSet<QString> seen;
    for(const QString& name:{"library/video","library/music","library/photo"}) {
        const auto path=preferences_.value(name).toString().trimmed();
        if(path.isEmpty())continue;
        const QFileInfo info(path);
        QString root=info.canonicalFilePath();
        if(root.isEmpty())root=QDir::cleanPath(QDir::fromNativeSeparators(info.absoluteFilePath()));
        if(root.isEmpty() || !QFileInfo(root).isDir() || seen.contains(root))continue;
        seen.insert(root);roots<<root;
    }
    QHash<QString,QString> resolved;int scanned=0;bool capped=false;
    if(!pathless.isEmpty() && !roots.isEmpty()) {
        QElapsedTimer timer;timer.start();
        resolved=resolveResumePaths(pathless,roots,scanned,capped);
        qInfo().noquote()<<QString("continue: resolving %1 pathless resume point(s) against %2 media library folder(s); %3 file(s) hashed in %4 ms%5")
            .arg(pathless.size()).arg(roots.size()).arg(scanned).arg(timer.elapsed())
            .arg(capped?QString(", stopped at the 5000-file cap"):QString{});
    }
    int kept=0,dropped=0;
    for(const auto& id:pathless) {
        const auto record=next.value(id).toObject();
        const qint64 ms=qint64(record.value("ms").toDouble(0));
        if(resolved.contains(id)) {
            auto filled=record;
            const QString path=resolved.value(id);
            filled.insert("path",path);
            if(filled.value("title").toString().isEmpty())filled.insert("title",QFileInfo(path).completeBaseName());
            next.insert(id,filled);++kept;
            qInfo().noquote()<<"continue: migrated pathless resume point"<<id<<"("+QString::number(ms)+" ms) ->"
                <<QDir::toNativeSeparators(path);
        } else {
            next.remove(id);++dropped;
            const QString reason=roots.isEmpty()
                ?QStringLiteral("no media library is configured, so the file behind this id could not be found")
                :QStringLiteral("%1 file(s) under %2 configured media library folder(s) were hashed and none matched%3")
                    .arg(scanned).arg(roots.size())
                    .arg(capped?QStringLiteral(", and the scan stopped at its 5000-file cap"):QString{});
            qWarning().noquote()<<"continue: dropped pathless resume point"<<id<<"("+QString::number(ms)+" ms):"<<reason;
        }
    }
    QStringList evicted;boundResumeMap(next,&evicted);
    for(const auto& lost:evicted)qWarning().noquote()<<"continue: dropped resume point"<<lost<<"(the map is capped at 64)";
    if(next==map)return;
    if(!preferences_.setValue(key,resumeJson(next)))
        qWarning().noquote()<<"continue: the migrated resume points could not be written:"<<preferences_.error();
    else
        qInfo().noquote()<<QString("continue: resume points migrated - %1 resolved, %2 dropped, %3 remembered%4")
            .arg(kept).arg(dropped).arg(next.size())
            .arg(legacy?QString(" (%1 of them came from the old format)").arg(legacy):QString{});
}
int Shell::rememberedResumeCount() const {
    int count=0;
    for(const auto& point:resumePoints(resumeMap(preferences_.value("media/resume").toString())))
        if(!point.path.isEmpty())++count;
    return count;
}
// The page itself: newest first, at most 20 rows, nothing older than 30 days. An age-hidden entry is
// only hidden - the stored map keeps its own 64-entry bound, so replaying the title refreshes it rather
// than this page quietly deleting a position the user may still want.
QVector<Entry> Shell::continueWatchingRows() const {
    const auto points=resumePoints(resumeMap(preferences_.value("media/resume").toString()));
    const qint64 now=resumeSecondsNow();
    QVector<Entry> rows;int old=0,over=0,missing=0;
    for(const auto& point:points) {
        if(point.path.isEmpty())continue;
        if(point.at>0 && now-point.at>kContinueMaxAgeSeconds){++old;continue;}
        if(rows.size()>=kContinueRowLimit){++over;continue;}
        const bool present=QFileInfo::exists(point.path);
        const QString title=point.title.isEmpty()?QFileInfo(point.path).completeBaseName():point.title;
        auto row=item("continue:"+point.id,title,"video_tex_album_default","explore_plugin_full",
            present?Action::File:Action::Unavailable,point.path);
        row.disabled=!present;
        if(!present) {
            ++missing;
            // The reason is the row's own line, so it is visible on the couch without opening anything;
            // Triangle still offers Remove, which is the only thing a row with no file can do.
            row.detail=QObject::tr("Missing - this file is not on disk any more. Triangle removes it.");
        } else {
            const QString at=MediaControls::formatTime(point.ms);
            row.detail=point.dur>point.ms
                ?QObject::tr("Resuming at %1 of %2 (%3 left)").arg(at,MediaControls::formatTime(point.dur),
                                                                  MediaControls::formatTime(point.dur-point.ms))
                :QObject::tr("Resuming at %1").arg(at);
        }
        rows.push_back(std::move(row));
    }
    if(old||over||missing)
        qInfo().noquote()<<QString("continue watching: %1 row(s) listed; %2 older than 30 days, %3 past the %4-row cap, %5 missing on disk")
            .arg(rows.size()).arg(old).arg(over).arg(kContinueRowLimit).arg(missing);
    return rows;
}
void Shell::openContinueWatching() {
    if(pages_.size()>=32){showMessage(QObject::tr("The maximum folder depth has been reached."),MessageKind::Page);return;}
    auto rows=continueWatchingRows();
    const QString title=QObject::tr("Continue Watching");
    if(rows.isEmpty()) {
        qInfo().noquote()<<"continue watching: nothing to continue";
        // The firmware's empty-list line, drawn as a list row rather than as a dialog (D3).
        showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
        emit changed();
        return;
    }
    qInfo().noquote()<<QString("continue watching: page opened with %1 row(s)").arg(rows.size());
    Page page;page.title=title;page.entries=std::move(rows);page.continuePage=true;
    pages_.push_back(std::move(page));
}
// --- The settings write chain ----------------------------------------------------------------------
// audit/SETTINGS-FIRMWARE.md section 4 names the console's three shapes for applying a setting: a result
// page (page_*_finish), a full confirmation chain (page_*_top -> *_ask -> *_proceed -> *_finish), or nothing
// at all (the value changes on the row and the list redraws). FIDELITY-BEHAVIOUR I77 records that this build
// only ever did the third. The chain below is the second shape, run on the surfaces this app already draws -
// the two-answer Box for the ask, the page_busy state for the proceed, the page message for the result - and
// every sentence is read from sysconf_plugin (the settings plugin) where that plugin ships one. Which stages
// a row gets is a per-class decision, and each class names its evidence:
//
//   host/* writes            ask + finish. sysconf_plugin ships the settings plugin's own accept-a-setting
//                            page (msg_setting_ask_back_prev_setting, "Do you want to accept this setting?"),
//                            its save result (msg_saved, "Save completed.") and its failure
//                            (msg_error_setting, "The setting could not be completed."). A host write is the
//                            one class that leaves this app and changes the computer, and the one whose
//                            failure the user has to see.
//   app-store writes         finish only. The console's value writes were measured to commit from the band
//                            and redraw the row with no ask page: SETTINGS-FIRMWARE 4.3 ("the value changes on
//                            the row and the list redraws ... this is what most of the 16 pages do"),
//                            DIALOGS-FIRMWARE 3.6 ("a successful settings change has no dialog") and the
//                            Color chip commit (SETTINGS-FIRMWARE 16.5 write-test: the band closes and the
//                            page is back at depth 1). So they keep the one-step commit; the stage they gain
//                            is the result page, in the plugin's own words - msg_saved, or
//                            msg_set_datetime_completed ("Date and time set completed.") for the clock rows -
//                            and msg_error_setting plus the store's own error when the write is refused.
//   Restore Default Settings ask + proceed + finish. The console's own chain for this row is
//                            page_console_setting_init_top|proceed|finish with msg_setting_init_ask1/ask2,
//                            msg_initializing ("Restoring default settings...") and msg_initialized. It is
//                            the one multi-key write here, so it is the one row that gets the proceed stage.
//
// Left direct, with the reason: media/repeatMode, media/shuffle and media/repeat are the player panel's own
// cycling rows and the console changes them in place (MEDIA-PANELS-FIRMWARE 4, 11.4); photo/* is this
// build's own page with no console row; display/fullscreen is a native window mode; and Action::SelectUser
// stays a one-step session switch because that is the measured behaviour the audit scores as a match (U1:
// "shell: user switched ... volume= 20 ... to ... volume= 80"). The display-awake hold and the display-off
// request are host writes made from playback state and the idle timer, not from a row, so no row asks for
// them (S1/S2, proved in that section instead).
namespace {
constexpr int kWriteProceedMinMs=900;   // the proceed page's minimum visible time (a fit: untimed in 4.93)
// The first line of a firmware string: the console's ask-page body carries a second line ("If you do not
// press any buttons, the system will automatically go back to the previous setting.") that this build has no
// revert timer to honour, so promising it would be false. The dropped line is named in the log.
QString firstLine(const QString& text) {
    const int at=text.indexOf('\n');
    return at<0?text:text.left(at);
}
}
void Shell::beginSettingWrite(const Entry& entry) {
    SettingsWrite write;
    write.key=entry.setting;write.label=entry.title;write.value=entry.option;
    write.valueText=entry.title;                       // the band row's own label is the chosen item
    write.popBand=!pages_.isEmpty();                    // a band is open: the finish returns to the row list
    write.host=entry.setting.startsWith(QLatin1String("host/"));
    const auto sysconf=[this](const QString& key,const QString& fallback) {
        return assets_.text("sysconf_plugin",key,fallback);
    };
    // The result page's two sentences, and the failure body, are the settings plugin's own.
    write.okText=sysconf("msg_saved",QObject::tr("Save completed."));
    write.failText=sysconf("msg_error_setting",QObject::tr("The setting could not be completed."));
    if(write.key.startsWith(QLatin1String("clock/")))
        write.okText=sysconf("msg_set_datetime_completed",QObject::tr("Date and time set completed."));
    if(write.host) {
        // Accept-a-setting: the settings plugin's own ask body. Its second line is dropped (no revert timer).
        const QString ask=sysconf("msg_setting_ask_back_prev_setting",
            QObject::tr("Do you want to accept this setting?\nIf you do not press any buttons, the system will automatically go back to the previous setting."));
        write.askBody=firstLine(ask);
        if(write.askBody.isEmpty())write.askBody=QObject::tr("Do you want to accept this setting?");
    }
    write.slow=false;
    // Back to the system's theme with no wallpaper of the user's own: the original background returns, so
    // the write is two keys and must stay atomic (the earlier single-frame path did the same).
    write.themeToSystem=entry.setting=="appearance/theme" && entry.option.toString().isEmpty()
        && backgroundMode()==2 && wallpaper().isEmpty();
    settingsWrite_=write;
    qInfo().noquote()<<QStringLiteral("settings: write stage=ask key=%1 row=\"%2\" value=\"%3\" ask=%4 "
        "proceed=no finish=\"%5\"+\"msg_error_setting\" source=sysconf_plugin")
        .arg(write.key,entry.title,write.valueText,
             write.askBody.isEmpty()?QStringLiteral("none"):QStringLiteral("msg_setting_ask_back_prev_setting"),
             write.okText);
    if(!settingsWrite_.askBody.isEmpty()) {
        settingsWrite_.stage=WriteStage::Ask;
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        showConfirm(QObject::tr("Settings"),settingsWrite_.askBody,[this]{startSettingsWritePage();});
        emit changed();
        return;
    }
    startSettingsWritePage();
}
void Shell::beginResetWrite() {
    // page_console_setting_init_top -> proceed -> finish, with the strings that chain uses in sysconf_plugin:
    // msg_setting_init_ask1 ("The settings below will be restored:"), msg_setting_init_ask2 ("Are you sure you
    // want to restore settings?"), msg_initializing ("Restoring default settings...") and msg_initialized.
    // The last sentence of msg_initialized ("Press the %e button to restart.") is dropped: this reset is the
    // app's own store and needs no restart, so quoting it would instruct an action that does not exist.
    const auto sysconf=[this](const QString& key,const QString& fallback) {
        return assets_.text("sysconf_plugin",key,fallback);
    };
    SettingsWrite write;
    write.key=QStringLiteral("settings/restoreDefaults");
    write.label=QObject::tr("Restore Default Settings");
    write.resetAll=true;write.slow=true;write.popBand=false;
    const QString ask1=sysconf("msg_setting_init_ask1",QObject::tr("The settings below will be restored:"));
    const QString ask2=sysconf("msg_setting_init_ask2",QObject::tr("Are you sure you want to restore settings?"));
    write.askBody=ask1+QStringLiteral("\n")+QObject::tr("Appearance, clock, sound, power save and media playback settings.")
        +QStringLiteral("\n\n")+ask2;
    write.proceedText=firstLine(sysconf("msg_initializing",QObject::tr("Restoring default settings...\nDo not turn off the system.")));
    write.okText=firstLine(sysconf("msg_initialized",QObject::tr("Restore completed.\nPress the %e button to restart.")));
    write.failText=sysconf("msg_error_init",QObject::tr("An error occurred during the restore operation."));
    write.stage=WriteStage::Ask;
    settingsWrite_=write;
    qInfo().noquote()<<QStringLiteral("settings: write stage=ask key=%1 ask=msg_setting_init_ask1+ask2 "
        "proceed=msg_initializing finish=msg_initialized+msg_error_init source=sysconf_plugin")
        .arg(write.key);
    interactionAudio_.play(InteractionAudio::Cue::Confirm);
    showConfirm(write.label,write.askBody,[this]{startSettingsWritePage();});
    emit changed();
}
void Shell::startSettingsWritePage() {
    if(settingsWrite_.key.isEmpty())return;
    if(settingsWrite_.slow) {
        // The console's *_proceed page: the busy page holds the screen while the write runs. This build's
        // write is one store call, so the page is held for a minimum visible time (a fit: no local artifact
        // times the console's proceed page) and the write itself runs when it expires.
        settingsWrite_.stage=WriteStage::Proceed;
        busy_=true;busyClock_.start();
        status_=settingsWrite_.proceedText;
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        qInfo().noquote()<<QStringLiteral("settings: write stage=proceed key=%1 page=busy text=\"%2\"")
            .arg(settingsWrite_.key,settingsWrite_.proceedText);
        emit changed();
        QTimer::singleShot(kWriteProceedMinMs,this,&Shell::runSettingWrite);
        return;
    }
    runSettingWrite();
}
void Shell::runSettingWrite() {
    if(settingsWrite_.key.isEmpty())return;
    QString error;
    bool ok=false;
    if(settingsWrite_.resetAll) {
        // Restore Default Settings keeps its one atomic write (settings_menu.cpp) and its own error string;
        // this chain owns the pages around it. The write raises its own page, so the finish page below
        // replaces it in the same turn and no frame shows the intermediate one.
        restoreDefaultSettings();
        ok=preferences_.error().isEmpty();
        if(!ok)error=preferences_.error();
    } else if(settingsWrite_.host) {
        const QString key=settingsWrite_.key;
        ok=key=="host/volume"?host::setMasterVolume(settingsWrite_.value.toInt())
            :key=="host/muted"?host::setMasterMuted(settingsWrite_.value.toBool())
            :host::setHostChoice(key,settingsWrite_.value.toInt());
        if(ok) {
            // The read-back is the write's own proof: the console's row shows the value it stored, so this
            // one does not report success on a call that returned true and changed nothing.
            if(key=="host/volume") {
                const int read=host::masterVolume(),asked=settingsWrite_.value.toInt();
                if(read>=0 && std::abs(read-asked)>1) {
                    ok=false;
                    error=QObject::tr("the endpoint still reads %1%").arg(read);
                }
            } else if(key=="host/muted") {
                if(host::masterMuted()!=settingsWrite_.value.toBool()) {
                    ok=false;
                    error=QObject::tr("the endpoint still reads %1").arg(host::masterMuted()?QObject::tr("muted"):QObject::tr("unmuted"));
                }
            } else {
                const int read=host::hostChoiceIndex(key);
                if(read!=settingsWrite_.value.toInt()) {
                    ok=false;
                    error=QObject::tr("the computer still reports item %1 of the list (asked for %2)")
                        .arg(read).arg(settingsWrite_.value.toInt());
                }
            }
        } else {
            // The host layer answers with a bool, so the observed value is the error: the write was refused
            // and the computer still reports what it had. Naming that reading is more use than "failed".
            const QString read=key=="host/volume"?QString::number(host::masterVolume())+"%"
                :key=="host/muted"?(host::masterMuted()?QObject::tr("On"):QObject::tr("Off"))
                :QString::number(host::hostChoiceIndex(key));
            error=QObject::tr("the computer refused the write (it still reads %1)").arg(read);
        }
    } else {
        if(settingsWrite_.themeToSystem)
            ok=preferences_.setValues({{"appearance/theme",QString{}},{"appearance/background",0}});
        else ok=preferences_.setValue(settingsWrite_.key,settingsWrite_.value);
        if(!ok)error=lastPreferencesError_.isEmpty()?preferences_.error():lastPreferencesError_;
        if(ok)applyPreferences();
    }
    settingsWrite_.result=ok?settingsWrite_.okText:settingsWrite_.failText+QStringLiteral(": ")+error;
    if(settingsWrite_.stage==WriteStage::Proceed) {
        busy_=false;status_.clear();
    }
    settingsWrite_.stage=ok?WriteStage::FinishOk:WriteStage::FinishFailed;
    interactionAudio_.play(ok?InteractionAudio::Cue::Accept:InteractionAudio::Cue::Error);
    qInfo().noquote()<<QStringLiteral("settings: write stage=%1 key=%2 value=\"%3\"%4")
        .arg(ok?QStringLiteral("finish-ok"):QStringLiteral("finish-failed"),settingsWrite_.key,
             settingsWrite_.valueText,
             ok?QString{}:QStringLiteral(" error=\"")+error+QStringLiteral("\""));
    const QString title=settingsWrite_.label.isEmpty()?QObject::tr("Settings"):settingsWrite_.label;
    showMessage(title,ok?settingsWrite_.okText+"\n\n"+settingsWrite_.valueText
                        :settingsWrite_.failText+"\n\n"+error,MessageKind::Page);
    emit changed();
}
void Shell::finishSettingWrite() {
    if(settingsWrite_.stage!=WriteStage::FinishOk && settingsWrite_.stage!=WriteStage::FinishFailed)return;
    const QString key=settingsWrite_.key;
    const bool ok=settingsWrite_.stage==WriteStage::FinishOk;
    // A failed write leaves the list up (the console's refused choice keeps the page and its focus, so the
    // user can pick another item; SETTINGS-FIRMWARE 16.5's disabled-chip run stays at depth 2), and only a
    // successful one returns to the row list with the new value on the row.
    const bool pop=ok && settingsWrite_.popBand && !pages_.isEmpty();
    settingsWrite_=SettingsWrite{};
    if(pop)pages_.removeLast();
    qInfo().noquote()<<QStringLiteral("settings: write stage=done key=%1 result=%2 returned depth=%3 selection=%4")
        .arg(key,ok?QStringLiteral("ok"):QStringLiteral("failed"))
        .arg(pages_.size()).arg(pages_.isEmpty()?selections_[category_]:pages_.last().selection);
    emit changed();
}
void Shell::cancelSettingWrite(const QString& why) {
    if(settingsWrite_.stage==WriteStage::None)return;
    qInfo().noquote()<<QStringLiteral("settings: write stage=cancelled key=%1 (%2)").arg(settingsWrite_.key,why);
    if(settingsWrite_.stage==WriteStage::Proceed) {
        busy_=false;status_.clear();
    }
    settingsWrite_=SettingsWrite{};
}
// Rebuilds the open page in place after a removal, and closes it when the last row goes: the list the
// user is looking at must be the list that exists, and an empty list says so instead of showing zero rows.
void Shell::reloadContinueWatching() {
    if(pages_.isEmpty() || !pages_.last().continuePage)return;
    auto rows=continueWatchingRows();
    if(rows.isEmpty()) {
        pages_.removeLast();
        qInfo().noquote()<<"continue watching: the list is empty; the page closed";
        showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
        emit changed();
        return;
    }
    auto& page=pages_.last();
    const QString previous=page.entries.value(page.selection).id;
    page.entries=std::move(rows);
    page.selection=0;
    for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==previous){page.selection=i;break;}
    qInfo().noquote()<<QString("continue watching: page rebuilt with %1 row(s)").arg(page.entries.size());
}
void Shell::removeContinueEntry(const QString& id,const QString& title) {
    auto map=resumeMap(preferences_.value("media/resume").toString());
    if(!map.contains(id)){qWarning().noquote()<<"continue: nothing to remove for"<<id;return;}
    const QString path=map.value(id).toObject().value("path").toString();
    map.remove(id);
    preferences_.setValue("media/resume",resumeJson(map));
    qInfo().noquote()<<QString("continue: removed %1 (%2) from Continue Watching; %3 still remembered")
        .arg(title.isEmpty()?QFileInfo(path).fileName():title,QDir::toNativeSeparators(path)).arg(map.size());
    reloadContinueWatching();
}
void Shell::startQueuedMedia() {
    if(jellyfinQueue_){startJellyfinMedia();return;}
    const auto* selected=mediaQueue_.current();if(!selected)return;
    const auto item=*selected; // player callbacks must not invalidate the selected queue entry
    ++mediaRequest_;mediaOpen_=true;mediaTitle_=item.title;loading_=true;status_.clear();
    // A saved position resumes here; the seek itself happens on LoadedMedia. msg_play_begin set the one-shot
    // flag that skips this row's own record, and it is consumed here so the next title resumes normally.
    resumePath_=item.path;
    resumePending_=playFromBeginning_?0:resumePositionFor(item.path);
    playFromBeginning_=false;
    if(resumePending_>0) {
        const qint64 seconds=resumePending_/1000;
        status_=QObject::tr("Resuming at %1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0'));
    }
    for(int i=0;i<entries().size();++i)if(entries()[i].action==Action::File && entries()[i].value==item.path){currentSelection()=i;break;}
    emit mediaFrame({});
    const auto source=QUrl::fromLocalFile(item.path);
    if(player_->source()==source){loading_=false;player_->setPosition(0);}else player_->setSource(source);
    player_->play();
    if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"media queue: play"<<mediaQueue_.currentIndex()+1<<"of"<<mediaQueue_.items().size()<<item.title;
    emit changed();
}
void Shell::advanceMedia(bool failed,const QString& error) {
    if(jellyfinQueue_) {
        finishJellyfinPlayback(!failed);
        if(failed){++mediaRequest_;jellyfinRequest_=0;loading_=false;player_->stop();status_="Playback stopped";showMessage("Jellyfin","This media could not be played. Check the server and its transcoding support.",MessageKind::Page);emit changed();return;}
    }
    if(mediaQueue_.empty())return;
    if(failed) {
        if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"media queue: rejected"<<mediaTitle_<<error;
        if(++mediaFailures_>=mediaQueue_.items().size()) {
            ++mediaRequest_;loading_=false;player_->stop();
            showMessage("No playable files remain in this queue.",error,MessageKind::Page);status_="Playback stopped";emit changed();return;
        }
    } else mediaFailures_=0;
    // Errors skip Repeat One; otherwise a broken file could retry forever.
    const int next=mediaQueue_.next(!failed);
    if(next<0) {
        ++mediaRequest_;loading_=false;player_->stop();status_="End of queue";
        if(failed)showMessage("This file could not be played.",error,MessageKind::Page);
        if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"media queue: stopped at end";
        emit changed();return;
    }
    startQueuedMedia();
}
void Shell::skipMedia(int direction) {
    touch();
    touch();if(!mediaOpen_ || !message_.isEmpty() || !direction)return;
    if(photoOpen()){stepPhoto(direction<0?-1:1);return;}
    const int next=direction<0?mediaQueue_.previous():mediaQueue_.next(false);
    if(next>=0){mediaFailures_=0;startQueuedMedia();}
}
void Shell::cycleMediaRepeat() {
    touch();
    if(mediaOpen_ && !photoOpen() && message_.isEmpty())preferences_.setValue("media/repeatMode",(mediaRepeatMode()+1)%3);
}
void Shell::toggleMediaShuffle() {
    touch();
    if(mediaOpen_ && !photoOpen() && message_.isEmpty())preferences_.setValue("media/shuffle",!preferences_.value("media/shuffle").toBool());
}
QString Shell::mediaQueueSummary() const {
    if(!mediaOpen_ || photoOpen() || mediaQueue_.empty())return {};
    QString result=QString("Track %1 of %2").arg(mediaQueue_.currentIndex()+1).arg(mediaQueue_.items().size());
    if(preferences_.value("media/shuffle").toBool())result+=QStringLiteral("  ")+QChar(0x00B7)+QStringLiteral("  Shuffle");
    if(mediaRepeatMode()==1)result+=QStringLiteral("  ")+QChar(0x00B7)+QStringLiteral("  Repeat One");else if(mediaRepeatMode()==2)result+=QStringLiteral("  ")+QChar(0x00B7)+QStringLiteral("  Repeat All");
    if(player_->playbackState()==QMediaPlayer::PausedState)result+=QStringLiteral("  ")+QChar(0x00B7)+QStringLiteral("  Paused");
    if(!status_.isEmpty())result+=QStringLiteral("  ")+QChar(0x00B7)+QStringLiteral("  ")+status_;
    return result;
}
void Shell::rotatePhoto(int delta) {
    touch();
    touch();
    if(!photoOpen() || photo_.isNull() || !delta)return;
    // 90 degrees per press, either way, kept inside 0..270. The console rotates from its photo control
    // panel (MEDIA-PANELS-FIRMWARE 6.2); the panel is not drawn here, so this is the native binding the
    // keyboard keeps (Q/E, and the script's ROTATELEFT/ROTATERIGHT tokens).
    photoRotation_=(photoRotation_+delta*90+360)%360;
    showPhoto();
    qInfo().noquote()<<"photo: rotated to"<<photoRotation_<<"degrees";
    emit changed();
}
bool Shell::photoOpen()const{return mediaOpen_ && !jellyfinQueue_ && categories_[category_].id=="photo";}
void Shell::showPhoto(){if(!photo_.isNull())emit mediaFrame(photo_.transformed(QTransform().rotate(photoRotation_)));}
void Shell::stepPhoto(int delta) {
    if(loading_)return;
    const auto& files=entries();
    for(int i=selection()+delta;i>=0 && i<files.size();i+=delta)if(files[i].action==Action::File){const auto file=files[i];currentSelection()=i;openFile(file);emit changed();return;}
    if(slideshow_.isActive() && preferences_.value("media/repeat").toBool()) {
        for(int i=0;i<files.size();++i)if(files[i].action==Action::File){const auto file=files[i];currentSelection()=i;openFile(file);emit changed();return;}
    }
    slideshow_.stop();
}
void Shell::togglePlayback(){
    touch();if(!mediaOpen_)return;
    if(photoOpen()){if(slideshow_.isActive())slideshow_.stop();else slideshow_.start(preferences_.value("photo/interval").toInt());emit changed();return;}
    if(player_->playbackState()==QMediaPlayer::PlayingState)player_->pause();else if(!player_->source().isEmpty()){status_.clear();if(player_->playbackState()==QMediaPlayer::StoppedState)player_->setPosition(0);player_->play();}
    emit changed();
}
void Shell::touch(){
    // One funnel for "the user did something". Every input entry point calls it, and View's key, mouse,
    // wheel and pad handlers reach it through noteActivity(), so the console's power rule holds: any
    // activity ends the current idle period AND re-arms the display-off request, so a later idle period may
    // ask again (FIDELITY-BEHAVIOUR S2). Before this, host::noteUserActivity() had no caller and the only
    // re-arm was the display-on notification, so an ignored request repeated on the 60 s window.
    idleTimeOverride_=-1;activity_.restart();host::noteUserActivity();
}
QString Shell::appRegistryPath()const{return QDir(dataDirectory_).filePath("apps.json");}
bool Shell::idleDisplayAllowed()const{return !mediaOpen_ && !gameRunning() && message_.isEmpty() && !QApplication::activeModalWidget();}
bool Shell::mediaPaused()const{return photoOpen()?!slideshow_.isActive():player_->playbackState()!=QMediaPlayer::PlayingState;}
bool Shell::videoOpen()const{return mediaOpen_ && player_->hasVideo();}
void Shell::seekMedia(qint64 delta){
    if(!mediaOpen_ || photoOpen() || !player_->isSeekable())return;
    touch();
    const qint64 from=player_->position();
    player_->setPosition(MediaControls::seekTarget(from,player_->duration()>0?player_->duration():-1,delta));
    // XMB_MEDIA_TRACE=1: the seek pair's actual result, so a scripted run can prove what the input did
    // without reading pixels. The video player's L2/R2 binding (the firmware's Fast Reverse/Forward,
    // MEDIA-PANELS-FIRMWARE 3.4/3.6) drives this same method; Left/Right keeps the keyboard binding.
    if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))
        qInfo().noquote()<<"media: seek"<<delta<<"ms from"<<from<<"to"<<player_->position();
    emit changed();
}
void Shell::toggleMediaDisplay() {
    // SELECT in the video container (audit/MEDIA-PANELS-FIRMWARE.md 3.4/3.6; FIDELITY-BEHAVIOUR I48). The
    // evidence for the binding is the container's own text: msg_display_display_with_icon is "Display " +
    // U+F88E, and friendim_plugin's msg_select_gaiji is U+F88E, so the container labels SELECT "Display".
    // What it displays is this player's own chrome - the time bar and the six-slot hint strip - which is what
    // View hides after MEDIA-PANELS 4.3's 3 s. Pinning keeps it up; releasing hands it back to that timer.
    if(!mediaOpen_ || photoOpen())return;
    touch();
    mediaDisplayPinned_=!mediaDisplayPinned_;
    qInfo().noquote()<<"media: display"<<(mediaDisplayPinned_?"pinned":"released")<<"(SELECT)";
    emit changed();
}
void Shell::applyPreferences(){
    audio_->setVolume(preferences_.value("audio/volume").toInt()/100.f);audio_->setMuted(preferences_.value("audio/muted").toBool());
    interactionAudio_.setEnabled(preferences_.value("audio/keyTone").toBool() && !preferences_.value("audio/muted").toBool());
    interactionAudio_.setVolume(preferences_.value("audio/volume").toInt()/100.f);
    if(slideshow_.isActive())slideshow_.setInterval(preferences_.value("photo/interval").toInt());
    mediaQueue_.setRepeat(static_cast<PlaybackQueue::Repeat>(mediaRepeatMode()));
    mediaQueue_.setShuffle(preferences_.value("media/shuffle").toBool());
    // `appearance/tvColumn` is the one preference that changes the category set, so it is applied here with
    // every other stored setting: hiding or restoring the column is an erase/insert of one entry, and the
    // open page stack is untouched because no page belongs to a column.
    syncTvColumn();
    loadTheme();
}
// The tenth column's switch. The console's own rule (category_tv.xml) is that the column exists only when
// something is registered to it: its root view lists nothing but content providers, so an unregistered console
// draws nine categories. This build's TV column is where an imported channel app - and the Jellyfin surface -
// lives, so the preference defaults to ON and turning it off is the console-parity state (nine columns, every
// icon after Video in the console's own cell).
bool Shell::tvColumnVisible() const {
    // 1. The environment override: the capture switch, and the only form available before the store can hold
    //    the key (preferences.cpp's bounded key table is a returned hunk, see the audit section).
    const auto raw=qEnvironmentVariable("XMB_TV_COLUMN").trimmed().toLower();
    if(!raw.isEmpty())return !(raw=="0" || raw=="off" || raw=="false" || raw=="no");
    // 2. The store. An unset key means ON, so a data directory that never wrote it keeps today's XMB.
    const auto stored=preferences_.value("appearance/tvColumn");
    return stored.isValid()?stored.toBool():true;
}
void Shell::syncTvColumn() {
    const bool want=tvColumnVisible();
    int at=-1;
    for(int i=0;i<categories_.size();++i)if(categories_[i].id==QStringLiteral("tv")){at=i;break;}
    if(want) {
        if(at>=0 || tvCategory_.id.isEmpty())return;
        // Back where it was authored: between Video and Game, which is where the bar draws it.
        int before=categories_.size();
        for(int i=0;i<categories_.size();++i)if(categories_[i].id==QStringLiteral("game")){before=i;break;}
        categories_.insert(before,tvCategory_);
        selections_.insert(before,0);
        if(category_>=before)++category_;
        qInfo().noquote()<<QStringLiteral("xmb: tv column restored at %1 (categories %2) preference=appearance/tvColumn")
            .arg(before).arg(int(categories_.size()));
        return;
    }
    if(at<0)return;
    tvCategory_=categories_[at];tvColumnStashed_=true;
    categories_.removeAt(at);
    selections_.removeAt(at);
    if(category_==at)category_=std::max(0,at-1);
    else if(category_>at)--category_;
    qInfo().noquote()<<QStringLiteral("xmb: tv column hidden (categories %1) preference=appearance/tvColumn - "
        "the bar reflows to the console's nine; the Jellyfin and channel rows live in that column and are not "
        "reachable until it is shown again").arg(int(categories_.size()));
}
void Shell::loadTheme() {
    const auto path=preferences_.value("appearance/theme").toString();
    if(path==theme_.path)return;
    theme_={};theme_.path=path; // a theme that can't be read stays chosen and shows the system's own pieces
    interactionAudio_.setThemeSounds({}); // switching to Original or a failed theme clears previous overrides
    if(path.isEmpty())return;
    QString error;const auto p3t=P3t::load(path,&error);
    if(p3t.isNull()){showMessage(QFileInfo(path).fileName(),error,MessageKind::Page);qWarning().noquote()<<"theme:"<<path<<error;return;}
    theme_.name=p3t.name().isEmpty()?QFileInfo(path).completeBaseName():p3t.name();
    theme_.background=p3t.background();theme_.icons=p3t.icons();
    interactionAudio_.setThemeSounds(p3t.sounds());
    qInfo().noquote()<<"theme:"<<theme_.name<<"background"<<theme_.background.size()<<(p3t.dynamic()?"(its animated scene is not drawn)":"")<<"icons"<<theme_.icons.size();
}
qint64 Shell::mediaPosition()const{return player_->position();}
qint64 Shell::mediaDuration()const{return player_->duration();}
void Shell::refreshDirectory(){
    if(!mediaOpen_ && !pages_.isEmpty() && pages_.last().jellyfin) {
        const auto page=pages_.last();loadJellyfinPage(page.jellyfinParent,page.title,page.jellyfinStart,page.jellyfinViews,true);return;
    }
    // The download page has no directory to rescan: F5 is another Steam scan, rebuilt into the same page.
    if(!mediaOpen_ && !pages_.isEmpty() && pages_.last().downloads){reloadSteamDownloads();return;}
    // The Continue Watching page has nothing on disk to rescan either: F5 rebuilds it from the remembered
    // positions, which is also what picks up a file that has reappeared or gone since the page opened.
    if(!mediaOpen_ && !pages_.isEmpty() && pages_.last().continuePage){reloadContinueWatching();emit changed();return;}
    if(pages_.isEmpty() && !mediaOpen_) {
        if(categories_[category_].id=="friends")refreshSteamFriends(true);else refreshContent();
        return;
    }
    if(pages_.isEmpty() || pages_.last().directory.isEmpty() || mediaOpen_)return;
    loading_=true;status_=QObject::tr("Reading media folder...");
    const auto kind=categories_[category_].id;
    // A rescan of a page that is already open (F5 / Refresh Directory) is reported when it lands.
    directoryRefresh_=true;
    scanRequest_=library_.scan(pages_.last().directory,kind=="photo"?MediaKind::Photo:kind=="music"?MediaKind::Music:MediaKind::Video);
    qInfo().noquote()<<QStringLiteral("busy: page_busy raised (list rescan) page=\"%1\" request=%2")
        .arg(QDir::toNativeSeparators(pages_.last().directory)).arg(scanRequest_);
    emit changed();
}
void Shell::rebuildUsers(){
    auto& users=categories_[0].entries;
    users={item("new-user","Create New User","item_tex_NewUser","explore_plugin_full",Action::CreateUser)};
    users[0].themeIcon="icon_newuser";
    for(const auto& profile:profiles_.profiles()) {
        users.push_back(item(profile.id,profile.name,"user_tex_login","explore_plugin_full",Action::SelectUser,profile.id));
        users.last().themeIcon="icon_user";
        if(profile.id==profiles_.activeId())selections_[0]=int(users.size())-1;
    }
    // The Steam account surface (FIDELITY-BEHAVIOUR U4). It is a native-only row: the 4.93 console's account
    // story is the PSN binding, which has no local implementation, and the data layer's two-step switch prompt
    // had no UI at all. It sits last in the column, after the users, because it is about the Steam client on
    // this computer rather than a user of the XMB. The icon is the firmware's own account item art from the
    // PSN category container (item_tex_account), not a new texture.
    {
        auto steam=item("steam-accounts",QObject::tr("Steam Account"),"item_tex_account","explore_category_psn",Action::SteamAccounts);
        steam.themeIcon="icon_accountmanage";
        users.push_back(std::move(steam));
    }
}
// --- Users > Steam Account (FIDELITY-BEHAVIOUR U4) -------------------------------------------------
// The data layer has carried the account list and the honest two-step handoff since audit/STEAM-ACCOUNTS.md
// (scanSteamAccounts, steamSwitchPlanFor, SteamSwitchConfirmation); there was no surface. The console has no
// analogue at all - its account surface is the PSN binding, which this build does not implement - and no
// firmware frame for a console account-switch page exists under C:\dev\out (the captures hold the Users
// column, the create-user page and the PSN column only). So this is NOT a reconstruction of a console page:
// it is this app's ordinary page class - rows, vertical()/activate()/back(), the column cross-fade - carrying
// the data layer's own text. The account-switch confirmation is the existing two-answer box, which is also
// how the console's own Yes/No pages are drawn.
bool Shell::steamAccountsOpen() const {
    for(const auto& page:pages_)if(page.steamAccounts)return true;
    return false;
}
void Shell::openSteamAccounts() {
    steamAccountsError_.clear();
    steamAccounts_=scanSteamAccounts(steamRoot_,&steamAccountsError_);
    const QString title=QObject::tr("Steam Account");
    if(steamAccounts_.isEmpty()) {
        const QString reason=!steamAccountsError_.isEmpty()?steamAccountsError_
            :QObject::tr("The Steam client on this computer remembers no account. Sign in once in Steam, then reopen this page.");
        qWarning().noquote()<<"steam accounts: no page:"<<reason;
        showMessage(title,reason,MessageKind::Page);
        emit changed();
        return;
    }
    const bool anyActive=std::any_of(steamAccounts_.cbegin(),steamAccounts_.cend(),[](const SteamAccount& a){return a.active;});
    Page page;page.title=title;page.steamAccounts=true;
    for(const auto& account:steamAccounts_) {
        auto e=item("steam-account:"+account.steamId64,
            account.personaName.isEmpty()?account.accountName:account.personaName,
            "item_tex_account","explore_category_psn",Action::SteamAccountSwitch,account.steamId64);
        // The current account is marked where every row shows its value: Steam's own active record is the
        // dot the Users column already uses; when the client is signed out, the account its own auto-login
        // marker names is named in words instead, because nothing is signed in to mark.
        if(account.active)e.fieldValue=QString(QChar(0x25cf));
        else if(account.autoLogin && !anyActive)e.fieldValue=QObject::tr("Auto sign-in");
        QString detail=account.accountName;
        if(account.lastLoginUnix>0)
            detail+=" "+QObject::tr("Last used %1").arg(QDateTime::fromSecsSinceEpoch(account.lastLoginUnix).toString("yyyy/MM/dd"));
        else detail+=" "+QObject::tr("No recorded sign-in");
        if(account.active)detail+=" - "+QObject::tr("signed in");
        e.detail=detail;
        page.entries.push_back(std::move(e));
    }
    if(!steamAccountsError_.isEmpty())
        qWarning().noquote()<<"steam accounts: partial list:"<<steamAccountsError_;
    qInfo().noquote()<<QStringLiteral("steam accounts: page opened rows=%1 active=%2 steamExe=%3")
        .arg(page.entries.size()).arg(anyActive?1:0)
        .arg(steamAccounts_.isEmpty()?QString{}:steamSwitchPlanFor(steamAccounts_.first(),steamRoot_).steamExe);
    // Verification hook, beside the item-menu and store dumps a scripted run already asks for: the page's
    // rows and the honest switch plan per account, so U4 is checkable without reading pixels.
    if(qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES")) {
        QJsonArray rows;
        for(const auto& account:steamAccounts_) {
            QString planError;
            const auto prompt=steamSwitchPlanFor(account,steamRoot_,&planError);
            rows.push_back(QJsonObject{{"steamId64",account.steamId64},{"accountName",account.accountName},
                {"personaName",account.personaName},{"active",account.active},{"autoLogin",account.autoLogin},
                {"switchPossible",prompt.possible},{"alreadyActive",prompt.alreadyActive},
                {"clientSignedIn",prompt.clientSignedIn},{"steps",prompt.steps.size()},
                {"blocked",prompt.blocked.body},{"error",planError}});
        }
        QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".steam-accounts.json");
        if(file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(QJsonObject{{"title",title},{"rows",rows},{"count",rows.size()},
                {"error",steamAccountsError_}}).toJson());
    }
    pages_.push_back(std::move(page));
    emit changed();
}
void Shell::steamAccountSelected(const QString& steamId64) {
    const auto it=std::find_if(steamAccounts_.cbegin(),steamAccounts_.cend(),
        [&](const SteamAccount& a){return a.steamId64==steamId64;});
    if(it==steamAccounts_.cend())return;
    QString error;
    steamSwitch_=steamSwitchPlanFor(*it,steamRoot_,&error);
    steamSwitchId_=steamId64;
    if(!steamSwitch_.possible || steamSwitch_.steps.size()!=2) {
        // Not possible, or nothing to switch: the data layer's own blocked text is the result page, and its
        // machine-facing reason goes to the log. Nothing is started.
        const QString body=steamSwitch_.blocked.body.isEmpty()?error:steamSwitch_.blocked.body;
        qWarning().noquote()<<"steam: switch not started:"<<(error.isEmpty()?body:error);
        showMessage(steamSwitch_.blocked.heading.isEmpty()?QObject::tr("Steam Account"):steamSwitch_.blocked.heading,
            body,MessageKind::Page);
        emit changed();
        return;
    }
    // The two-step model starts here; the first box is step 1 and only its accepted answer advances it.
    steamSwitchHold_=qEnvironmentVariableIsSet("XMB_STEAM_SWITCH_HOLD");
    steamSwitchConfirmation_=SteamSwitchConfirmation{};
    steamSwitchConfirmation_.stage=SteamSwitchConfirmation::Stage::ConfirmClose;
    qInfo().noquote()<<QStringLiteral("steam: switch requested account=%1 steps=%2 hold=%3")
        .arg(steamSwitch_.accountLabel).arg(steamSwitch_.steps.size()).arg(steamSwitchHold_?1:0);
    steamSwitchNextStep();
}
void Shell::steamSwitchNextStep() {
    if(steamSwitch_.steps.size()!=2)return;
    const bool first=steamSwitchConfirmation_.stage==SteamSwitchConfirmation::Stage::ConfirmClose;
    const auto& step=steamSwitch_.steps.at(first?0:1);
    qInfo().noquote()<<QStringLiteral("steam: prompt %1 heading=\"%2\" confirm=\"%3\" cancel=\"%4\" "
        "nothing started yet")
        .arg(first?1:2).arg(step.heading,step.confirmLabel,step.cancelLabel);
    showConfirm(step.heading,step.body,[this]{steamSwitchConfirmed();});
    steamPrompt_=true;
    emit changed();
}
void Shell::steamSwitchConfirmed() {
    const auto before=steamSwitchConfirmation_.stage;
    if(!steamSwitchConfirmation_.confirm()) {
        steamSwitchCancel(QStringLiteral("the confirmation model refused the step"));
        return;
    }
    qInfo().noquote()<<QStringLiteral("steam: step %1 accepted (%2/2)")
        .arg(before==SteamSwitchConfirmation::Stage::ConfirmClose?1:2).arg(steamSwitchConfirmation_.accepted);
    if(steamSwitchConfirmation_.ready()){steamSwitchRun();return;}
    steamSwitchNextStep();
}
void Shell::steamSwitchRun() {
    // Both steps are accepted, so the handoff may run: close the client, then start it again with no account
    // argument (steam_accounts.h: the XMB never passes -login, a name, a password or a token). A dry run of
    // the same model is logged first, so the record shows the exact command lines and that nothing ran early.
    const auto account=std::find_if(steamAccounts_.cbegin(),steamAccounts_.cend(),
        [&](const SteamAccount& a){return a.steamId64==steamSwitchId_;});
    if(account==steamAccounts_.cend()) {
        steamSwitchResult(false,QObject::tr("The account is no longer in the Steam client's list."));
        return;
    }
    const auto dry=steamSwitchDryRun(*account,steamRoot_);
    for(const auto& line:dry.commands)qInfo().noquote()<<"steam: dry run command:"<<line;
    const QString exe=steamSwitch_.steamExe;
    if(steamSwitchHold_) {
        qInfo().noquote()<<QStringLiteral("steam: handoff held (XMB_STEAM_SWITCH_HOLD=1): %1 ; %2")
            .arg(dry.commands.value(0),dry.commands.value(1));
        steamSwitchResult(true,QString{});
        return;
    }
    if(exe.isEmpty() || !QFileInfo(exe).isFile()) {
        steamSwitchResult(false,QObject::tr("steam.exe was not found at %1").arg(exe.isEmpty()?QObject::tr("(no path)"):exe));
        return;
    }
    // Step 1: ask the running client to exit. It is a short-lived child, so its own error string is
    // available when the launch fails (a detached start would report only false).
    QProcess close;
    close.start(exe,steamSwitch_.shutdownArguments);
    if(!close.waitForStarted(5000)) {
        steamSwitchResult(false,QObject::tr("Steam could not be asked to close: %1").arg(close.errorString()));
        return;
    }
    close.waitForFinished(10000);
    qInfo().noquote()<<QStringLiteral("steam: close requested (%1) exit=%2").arg(exe).arg(close.exitCode());
    // Step 2: start the client again, detached so it outlives the XMB. Its failure is reported from the
    // launch's own result, and the success text never claims a sign-in: Steam's window does that.
    if(!QProcess::startDetached(exe,steamSwitch_.relaunchArguments)) {
        steamSwitchResult(false,QObject::tr("Steam could not be started again from %1").arg(exe));
        return;
    }
    steamSwitchResult(true,QString{});
}
void Shell::steamSwitchResult(bool ok,const QString& error) {
    steamPrompt_=false;
    const QString title=QObject::tr("Steam Account");
    const QString body=ok?steamSwitch_.outcomeText
        :QObject::tr("The account handoff did not start. %1").arg(error);
    qInfo().noquote()<<QStringLiteral("steam: handoff %1%2").arg(ok?QStringLiteral("started"):QStringLiteral("failed"),
        ok?QString{}:QStringLiteral(": ")+error);
    showMessage(title,body,MessageKind::Page);
    emit changed();
}
void Shell::steamSwitchCancel(const QString& why) {
    steamPrompt_=false;
    if(steamSwitchConfirmation_.stage!=SteamSwitchConfirmation::Stage::Cancelled)
        steamSwitchConfirmation_.cancel();
    qInfo().noquote()<<QStringLiteral("steam: switch cancelled (%1); nothing was started").arg(why);
    steamSwitch_=SteamSwitchPrompt{};
}
void Shell::createUser(const QString& name){profiles_.create(name);}
QString Shell::subtitleText(const Entry& entry) const {
    // The returned hunk from the render pass (audit/FIDELITY-SURFACES.md 14.10 A). A user-scoped row's second
    // line is the firmware's own scope marker, not the active user's name. Measured there: col-game-r00 (Vita,
    // focused), col-game-r03 (Saved Data (minis/PSP)) and col-game-r04 (Saved Data (PS3)) all read "*User" at
    // x 682..730, y 528..542, while the two system-wide rows (Game Data Utility, Memory Card Utility) carry
    // nothing in that slot. This build drew the active profile name there instead, so only the marker was
    // missing. [TRACED: which rows carry it, the string, the rect. FIT: none.]
    static const QSet<QString> userScope{QStringLiteral("vita"),QStringLiteral("psp"),
                                         QStringLiteral("ps3"),QStringLiteral("trophy")};
    if(userScope.contains(entry.id))return QStringLiteral("*User");
    // A Steam account row shows its login name and when Steam last used it: the row's title is the persona
    // name and the detail is the account's own record, which is what a switch decision needs.
    if(entry.action==Action::SteamAccountSwitch)return entry.detail;
    // A Download Management row shows its state and progress under the title, which is the one line the
    // page draws for every row (the row's own detail string). The information page carries the rest.
    if(entry.id.startsWith("download:"))return entry.detail;
    // A Continue Watching row shows its position, or why it cannot be played, in the same place.
    if(entry.id.startsWith("continue:"))return entry.detail;
    return {};
}
// ---- In-XMB Jellyfin sign-in (audit/JELLYFIN-NATIVE.md) -------------------------------------------------
// The one credential surface, and it is a page of the ordinary page/column model: rows are Entries, moves go
// through vertical()/activate()/back(), the column cross-fade and Back work unchanged, and no modal widget is
// created - so view.cpp's pollController never zeroes the pad while someone is signing in.
//
// Row set (the order audit/HTPC-IDEAS.md T1 asks for):
//   Server URL  Cross -> the XMB keyboard on the server this app was registered with
//   Username    Cross -> the XMB keyboard
//   Password    Cross -> the XMB keyboard, composing dots; the row itself draws dots
//   Sign In     sends Users/AuthenticateByName through JellyfinClient
//   Cancel      leaves the page and aborts a request that is still running
//
// Nothing is written to disk. The password is held in this process's memory while the page is up, cleared
// when the page closes, and never appears in a row, a log line or the capture sidecar; the only trace is
// "value masked" plus the field that changed.
namespace {
// The password's on-row and on-keyboard rendering: one bullet per character, never the characters.
QString maskedValue(const QString& value) { return QString(value.size(),QChar(0x2022)); }
}
bool Shell::jellyfinSignInOpen() const {
    for(const auto& page:pages_)if(page.signIn)return true;
    return false;
}
QString Shell::jellyfinSignInState() const {
    if(!jellyfinSignInOpen())return {};
    if(jellyfinSigningIn_)return QStringLiteral("signingIn");
    return signInError_.isEmpty()?QStringLiteral("idle"):QStringLiteral("failed");
}
QString Shell::pageKind() const {
    if(pages_.isEmpty())return QStringLiteral("root");
    const auto& page=pages_.last();
    if(page.signIn)return QStringLiteral("jellyfinSignIn");
    if(page.steamAccounts)return QStringLiteral("steamAccounts");
    if(page.jellyfin)return page.jellyfinViews?QStringLiteral("jellyfinViews"):page.jellyfinSearch?QStringLiteral("jellyfinSearch"):QStringLiteral("jellyfinFolder");
    if(page.downloads)return QStringLiteral("downloads");
    if(page.continuePage)return QStringLiteral("continueWatching");
    if(!page.directory.isEmpty())return QStringLiteral("folder");
    return QStringLiteral("page");
}
QString Shell::focusedRowId() const {
    const auto& rows=entries();
    if(rows.isEmpty())return {};
    return rows.value(std::clamp(selection(),0,int(rows.size())-1)).id;
}
void Shell::openJellyfinSignInPage(QUrl server) {
    if(jellyfinSignInOpen()){refreshJellyfinSignInPage();emit changed();return;}
    signInServer_=server.toString();
    signInUser_.clear();
    signInPassword_.fill(QChar(0));signInPassword_.clear();
    signInError_.clear();status_.clear();
    // The opt-in shows what is actually on disk: a record exists only after an accepted sign-in with
    // the row turned on, so its presence is the remembered choice.
    signInRemember_=jellyfinRemembered();
    Page page;page.title=jellyfinServiceTitle_.isEmpty()?QObject::tr("Jellyfin"):jellyfinServiceTitle_;
    page.signIn=true;
    // Focus starts on Username, not on the server line: the address came from the registration and is usually
    // already right, so the first Cross is the one the couch actually needs.
    page.selection=1;
    pages_.push_back(std::move(page));
    refreshJellyfinSignInPage();
    qInfo().noquote()<<"jellyfin: sign-in page opened rows= Server URL|Username|Password|Stay Signed In|Sign In|Cancel focus= Username";
    emit changed();
}
void Shell::refreshJellyfinSignInPage() {
    int index=-1;
    for(int i=0;i<pages_.size();++i)if(pages_[i].signIn){index=i;break;}
    if(index<0)return;
    auto& page=pages_[index];
    const QString focused=page.entries.isEmpty()?QString{}:page.entries.value(page.selection).id;
    page.entries.clear();
    // A field is drawn like a settings row: the same small round bullet, the label, and the value on the right
    // (Entry::fieldValue). `key` is the Entry::value the activate() branch reads; `shown` is what that value
    // looks like on screen, which for the password is one dot per character.
    const auto field=[&](const QString& id,const QString& title,const QString& key,const QString& help,
                         const QString& shown) {
        auto e=item(id,title,"23n","qgl/icontex",Action::JellyfinSignInField,key);
        e.detail=help;e.fieldValue=shown;page.entries.push_back(std::move(e));
    };
    field(QStringLiteral("jellyfin-signin:server"),QObject::tr("Server URL"),QStringLiteral("server"),
        QObject::tr("Cross to type the server address this app was registered with."),signInServer_);
    field(QStringLiteral("jellyfin-signin:username"),QObject::tr("Username"),QStringLiteral("username"),
        QObject::tr("Cross to type your Jellyfin user name."),signInUser_);
    field(QStringLiteral("jellyfin-signin:password"),QObject::tr("Password"),QStringLiteral("password"),
        QObject::tr("Cross to type your password. It is shown as dots."),maskedValue(signInPassword_));
    {
        // The explicit opt-in. It is a row like every other one, so the pad reaches it; the value on the
        // right is the current choice, and Cross flips it.
        auto e=item(QStringLiteral("jellyfin-signin:remember"),QObject::tr("Stay Signed In"),"23n","qgl/icontex",
            Action::JellyfinSignInRemember);
        e.fieldValue=signInRemember_?QObject::tr("On"):QObject::tr("Off");
        e.detail=QObject::tr("Cross to turn this on: an accepted sign-in then stores the session on this PC, protected for this Windows user.");
        page.entries.push_back(std::move(e));
    }
    {
        auto e=item(QStringLiteral("jellyfin-signin:submit"),QObject::tr("Sign In"),"tex_Sign_In","explore_plugin_full",
            Action::JellyfinSignInSubmit);
        // One place carries the outcome: the page's own status line (drawn under the rows), where the loading
        // line already lives. The focused row keeps its static help, so a failure is stated once, in full.
        e.detail=QObject::tr("Sends the sign-in request to this server.");
        page.entries.push_back(std::move(e));
    }
    {
        auto e=item(QStringLiteral("jellyfin-signin:cancel"),QObject::tr("Cancel"),"23n","qgl/icontex",
            Action::JellyfinSignInCancel);
        e.detail=QObject::tr("Leaves sign-in and cancels a request that is still running.");
        page.entries.push_back(std::move(e));
    }
    if(!focused.isEmpty())for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==focused){page.selection=i;break;}
    page.selection=std::clamp(page.selection,0,std::max(0,int(page.entries.size())-1));
}
void Shell::setJellyfinSignInField(const QString& field,const QString& value) {
    // The one field-edit signal also carries the Jellyfin search term: the row that asks for it sends
    // the key "search", and the accepted text opens a result page. It is not a credential field, so it
    // is kept (for the next keyboard prefill) but only its length is logged.
    if(field==QStringLiteral("search")) {
        const QString term=value.trimmed();
        if(term.isEmpty())return;
        jellyfinSearchTerm_=term;
        qInfo().noquote()<<"jellyfin: search term accepted ("<<term.size()<<" character(s))";
        openJellyfinSearch(term);
        return;
    }
    if(!jellyfinSignInOpen())return;
    if(field=="server")signInServer_=value;
    else if(field=="username")signInUser_=value;
    else if(field=="password")signInPassword_=value;
    else return;
    signInError_.clear();status_.clear();
    // The field and the number of characters only: the value itself never reaches the log.
    qInfo().noquote()<<QString("jellyfin: sign-in field %1 set to %2 character(s), value masked (not logged)")
        .arg(field).arg(value.size());
    refreshJellyfinSignInPage();emit changed();
}
void Shell::submitJellyfinSignIn() {
    if(!jellyfinSignInOpen())return;
    if(jellyfinSigningIn_){qInfo()<<"jellyfin: sign-in ignored, a request is already in flight";return;}
    signInError_.clear();status_=QObject::tr("Signing in to Jellyfin...");
    // No credentials here either: the server, and whether a user name and a password were supplied.
    qInfo().noquote()<<QString("jellyfin: sign-in request to %1 (username %2, password %3, stay signed in %4; values masked)")
        .arg(signInServer_,signInUser_.isEmpty()?QStringLiteral("empty"):QStringLiteral("set"),
             signInPassword_.isEmpty()?QStringLiteral("empty"):QStringLiteral("set"),
             signInRemember_?QStringLiteral("on"):QStringLiteral("off"));
    authenticateJellyfin(QUrl(signInServer_.trimmed(),QUrl::StrictMode),signInUser_,signInPassword_);
    refreshJellyfinSignInPage();emit changed();
}
void Shell::cancelJellyfinSignInPage() {
    if(!jellyfinSignInOpen() && !jellyfinSigningIn_)return;
    qInfo().noquote()<<(jellyfinSigningIn_?"jellyfin: sign-in cancelled, the request in flight is aborted"
                                            :"jellyfin: sign-in cancelled");
    // cancelJellyfinSignIn() drops jellyfinSigningIn_, bumps the client's generation (which invalidates the
    // reply) and signs the client out, so a late answer cannot land on the page the user has left.
    cancelJellyfinSignIn();
    status_.clear();signInError_.clear();
    closeJellyfinSignInPage();
    emit changed();
}
void Shell::closeJellyfinSignInPage() {
    signInPassword_.fill(QChar(0));signInPassword_.clear();
    signInError_.clear();
    for(int i=pages_.size()-1;i>=0;--i)if(pages_[i].signIn){pages_.removeAt(i);break;}
}
}
