#include "shell.h"
#include "media_controls.h"
#include <QApplication>
#include "assets.h"
#include "catalog.h"
#include "host.h"
#include "p3t.h"
#include "steam_library.h"
#include "steam_artwork.h"
#include "steam_collections.h"
#include "steam_overrides.h"
#include "steam_family.h"
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
// Launches a child and keeps it parented so the XMB knows when it returns. While it runs the
// idle auto-close is suppressed; on exit the library is rescanned and the column restored,
// which is the return lifecycle a launched game needs (audit/FEATURE-COVERAGE.md, ISC-20).
bool Shell::startTracked(const QString& program,const QStringList& arguments,const QString& workingDirectory,const QString& title) {
    if(!game_.isNull()){message_=QObject::tr("A title is already running.");return false;}
    auto* process=new QProcess(this);
    process->setProgram(program);
    process->setArguments(arguments);
    if(!workingDirectory.isEmpty())process->setWorkingDirectory(workingDirectory);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(process,&QProcess::errorOccurred,this,[this,process,title](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart)return;
        process->deleteLater();game_.clear();
        message_=title+"\n\n"+QObject::tr("This title could not be started.");emit changed();
    });
    connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
            [this,process,title](int code,QProcess::ExitStatus status){
        process->deleteLater();game_.clear();gameTitle_.clear();touch();
        qInfo().noquote()<<"returned from"<<title<<"exit"<<code<<(status==QProcess::NormalExit?"normal":"crashed");
        // The return of a tracked title is a real event this build can report. The console itself has no
        // return pop-up (only launches), so this one is a native event, not a firmware one.
        if(status==QProcess::CrashExit) {
            message_=title+"\n\n"+QObject::tr("This title ended unexpectedly.");
            notify(title,QObject::tr("This title ended unexpectedly."),"tex_notification_caution");
        }
        else notify(title,QObject::tr("Returned to the XMB"),"tex_notification_info");
        emit changed();
        refreshContent();
    });
    process->start();
    if(!process->waitForStarted(4000)){
        process->deleteLater();
        message_=title+"\n\n"+QObject::tr("This title could not be started.");
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
        message_=title+"\n\nSteam could not be started. Refresh the game library and check the Steam installation.";
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
        message_=title+"\n\nSteam did not report this title as running.";
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
        if(emulator.isEmpty())message_=entry.title+"\n\nRPCS3 could not be started.";
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
    if(page.entries.isEmpty()){message_=title+"\n\n"+QObject::tr("This folder is empty.");return;}
    pages_.push_back(std::move(page));
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
        if(!result.error.isEmpty()){message_="App Library\n\n"+result.error;result.apps=contentApps_;}
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
                e.detail=app.kind=="game"?QString{}:app.kind=="jellyfin"?"Jellyfin":"Service app · Opens in your browser";
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
                for(const auto& e:category.entries)
                    entries.push_back(QJsonObject{{"id",e.id},{"title",e.title},{"detail",e.detail},
                        {"action",int(e.action)},{"value",e.value},{"hasArtwork",!e.artwork.isNull()}});
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
    if(!ContentRegistry::importFile(file,QDir(dataDirectory_).filePath("apps.json"),error))message_="Import Games and Apps\n\n"+error;
    else refreshContent();
    emit changed();
}
void Shell::refreshSteamFriends(bool showErrors,bool force) {
    if(friendsRefreshing_ && !force)return;
    showFriendsErrors_=showErrors;friendsRefreshing_=true;steamFriendsError_="Refreshing Steam friends…";rebuildFriends();emit changed();
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
        {"photo","Photo","qgl/icontex","3n",{item("library","Photo Library","photo_tex_album_pic",full,Action::Library),item("options","Photo Settings","tex_photo",full,Action::SettingsPage,"photo"),item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"music","Music","qgl/icontex","4n",{item("library","Music Library","music_tex_album_pic",full,Action::Library),item("options","Music Settings","tex_music",full,Action::SettingsPage,"music"),item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"video","Video","qgl/icontex","5n",{item("library","Video Library","video_tex_album_default",full,Action::Library),item("options","Video Settings","tex_video",full,Action::SettingsPage,"video"),item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"tv","TV/Video Services","qgl/icontex","35n", /* normal-mapped like the others; tex_video is 46 px */{item("services","My Channels","tex_video")}},
        {"game","Game","qgl/icontex","6n",{
            item("vita","PS Vita System Application Utility","item_tex_vitaformat"),item("data","Game Data Utility","item_tex_ps3game_folder"),
            item("memory","Memory Card Utility","item_tex_ps12util"),item("ps2","Saved Data Utility (PS2)","item_tex_ps2game_folder"),
            item("psp","Saved Data Utility (PSP)","item_tex_pspgame_folder"),item("ps3","Saved Data Utility (PS3)","item_tex_ps3sd_folder"),
            item("store","PlayStation Store","item_tex_ps_store")}},
        {"network","Network","qgl/icontex","7n",{
            item("manual","Online Instruction Manuals","tex_onlinemanual","explore_category_network"),
            item("remote","Remote Play","tex_premo","explore_category_network"),
            item("browser","Internet Browser","tex_browser","explore_category_network"),
            item("search","Internet Search","tex_kensaku","explore_category_network"),
            item("downloads","Download Management","tex_dl_manage","explore_category_network")}},
        {"psn","PlayStation Network","xmb_plugin_normal","tex_psn", /* 128x128; explore_plugin_full holds a 46x46 copy */{item("signin","Sign In","tex_Sign_In"),item("trophy","Trophy Collection","item_tex_trophy"),item("store","PlayStation Store","item_tex_ps_store")}},
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
    // Bind the current native settings projection to original XMBML resource
    // keys. Selector/plugin visibility and unmapped operations remain separate.
    const auto document=readXmbDocument(QDir(assets.root()).filePath("vsh/resource/explore/xmb/category_sysconf.xml"));
    if(document.error.isEmpty() && document.views.contains("seg_fixed_items")) {
        const auto& view=document.views["seg_fixed_items"];
        const QMap<QString,QString> aliases{{"system","console"},{"video","bd_dvd"},{"date","date_time"}};
        int resolved=0;
        for(auto& entry:categories_[1].entries) {
            const auto key=aliases.value(entry.id,entry.id);
            const auto table=view.tables.constFind(key);
            if(table==view.tables.cend())continue;
            entry.title=assets.text(sys,table->value("title_rsc"),entry.title);
            entry.detail=assets.text(sys,table->value("info_rsc"));
            entry.texture=table->value("icon_rsc",entry.texture);++resolved;
        }
        qInfo()<<"catalog: settings projection resolved"<<resolved<<"entries from original XMBML and English RCO text";
    } else qWarning().noquote()<<"catalog: settings resource unavailable"<<document.error;
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
        {"network/manual","icon_onlinemanual"},{"network/remote","icon_remoteplay"},{"network/browser","icon_browser"},
        {"network/search","icon_inet_search"},{"network/downloads","icon_download"},{"psn/trophy","icon_trophy"},
        {"friends/add","icon_addfriend"},{"friends/players","icon_playermet"},{"friends/chatroom","icon_chatroom_text"},
        {"friends/chat","icon_chat"},{"friends/messages","icon_mbox"},{"friends/block","icon_blocklist"}};
    for(auto& category:categories_) {
        category.themeIcon=themeCategoryIcons.value(category.id);
        for(auto& entry:category.entries)entry.themeIcon=themeEntryIcons.value(category.id+"/"+entry.id);
    }
    selections_.fill(0,categories_.size());
    rebuildUsers();
    addInstalledApps();
    connect(&steamFriends_,&SteamFriends::loaded,this,[this](const SteamFriendList& friends,const QString& error){
        steamFriendList_=friends;steamFriendsError_=error;friendsRefreshing_=false;rebuildFriends();emit changed();
        if(error.isEmpty())friendsAge_.restart();
        if(showFriendsErrors_ && !error.isEmpty()){message_="Steam Friends\n\n"+error;emit changed();}
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
        if(!error.isEmpty()){message_=QObject::tr("Steam Store")+"\n\n"+error;emit changed();return;}
        Page page;page.title=query.isEmpty()?QObject::tr("Steam Store"):query;
        for(const auto& row:items) {
            auto e=item("store:"+row.appId,row.title,"game_tex_default_ps3","explore_plugin_full",
                Action::InstallSteam,row.appId);
            const QString price=SteamStore::priceText(row);
            e.detail=price.isEmpty()?QObject::tr("Open in Steam"):price;
            page.entries.push_back(std::move(e));
        }
        if(page.entries.isEmpty()){message_=QObject::tr("Steam Store")+"\n\n"+QObject::tr("The store listed nothing.");emit changed();return;}
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
    connect(&profiles_,&Profiles::failed,this,[this](const QString& error){message_=error;emit changed();});
    player_->setAudioOutput(audio_.get());player_->setVideoSink(video_.get());
    connect(&preferences_,&Preferences::failed,this,[this](const QString& error){message_=error;emit changed();});
    connect(&preferences_,&Preferences::changed,this,[this]{applyPreferences();emit changed();});
    connect(&library_,&MediaLibrary::loaded,this,[this](quint64 request,const QString& path,const MediaEntries& files,const QString& error,bool truncated){
        const bool refreshed=directoryRefresh_;directoryRefresh_=false;
        if(request!=scanRequest_ || pages_.isEmpty())return;
        loading_=false;scanRequest_=0;
        auto& page=pages_.last();
        const QString previous=page.entries.isEmpty()?QString{}:page.entries[page.selection].id;
        page.directory=path;page.entries.clear();page.selection=0;
        const QString kind=categories_[category_].id;
        for(const auto& file:files) {
            page.entries.push_back(item(file.path,file.title,file.isDirectory?"item_tex_plain_folder":kind=="photo"?"photo_tex_album_pic":kind=="music"?"music_tex_album_pic":"video_tex_album_default","explore_plugin_full",file.isDirectory?Action::Folder:Action::File,file.path));
            auto& entry=page.entries.last();
            entry.detail=file.isDirectory?QString{}:QString::number(file.size/1024)+" KB  ·  "+file.modified.toString("yyyy/MM/dd");
            if(entry.id==previous)page.selection=int(page.entries.size())-1;
        }
        status_=truncated?"Showing the first 5,000 matching items.":page.entries.isEmpty()?"There are no supported files.":QString{};
        if(!error.isEmpty())message_=error;
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
            if(state==QMediaPlayer::LoadedMedia && resumePending_>0) {
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
    if(!preferences_.error().isEmpty())message_=preferences_.error();
    if(!profiles_.error().isEmpty())message_=profiles_.error();
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
    initJellyfin();
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
void Shell::horizontal(int delta) {
    touch();if(!delta)return;
    if(!message_.isEmpty())return;
    if(mediaOpen_) {
        if(categories_[category_].id=="photo") {
            stepPhoto(delta);
        } else seekMedia(qint64(delta)*10000);
        return;
    }
    if(!pages_.isEmpty())return;
    library_.cancel();scanRequest_=0;loading_=false;status_.clear();
    const int previous=category_;category_=std::clamp(category_+delta,0,int(categories_.size())-1);
    if(previous!=category_ && categories_[category_].id=="friends" && SteamOwned::hasApiKey()
       && (!friendsAge_.isValid() || friendsAge_.elapsed()>30000))refreshSteamFriends();
    if(previous!=category_)interactionAudio_.play(InteractionAudio::Cue::Move);emit changed();
}
void Shell::vertical(int delta) {
    touch();
    if(mediaOpen_){if(photoOpen() && !photo_.isNull()){photoRotation_=(photoRotation_+delta*90+360)%360;showPhoto();}return;}
    if(!message_.isEmpty() || loading_)return;
    const int previous=selection();currentSelection()=std::clamp(selection()+delta,0,std::max(0,int(entries().size())-1));
    if(previous!=selection())interactionAudio_.play(InteractionAudio::Cue::Move);emit changed();
}
void Shell::activate() {
    touch();
    if(!message_.isEmpty()){interactionAudio_.play(InteractionAudio::Cue::Accept);message_.clear();emit changed();return;}
    if(mediaOpen_){togglePlayback();return;}
    if(entries().isEmpty() || loading_)return;
    const auto entry=entries()[selection()];
    interactionAudio_.play(entry.action==Action::Unavailable?InteractionAudio::Cue::Error:
        entry.action==Action::SettingChoices?InteractionAudio::Cue::Options:InteractionAudio::Cue::Confirm);
    switch(entry.action) {
    case Action::SettingsPage:openSettings(entry.value);break;
    case Action::SettingChoices:openChoices(entry.setting,entry.title);break;
    case Action::SetSetting:
        if(entry.setting=="appearance/background" && entry.option.toInt()==2 && wallpaper().isEmpty() && theme_.background.isNull()){emit requestWallpaper();break;}
        if(entry.setting=="appearance/theme" && entry.option.toString().isEmpty() && backgroundMode()==2 && wallpaper().isEmpty()) {
            // Back to the system's theme with no wallpaper of the user's own: the original background returns.
            if(preferences_.setValues({{"appearance/theme",QString{}},{"appearance/background",0}}) && !pages_.isEmpty())pages_.removeLast();
            break;
        }
        if(entry.setting.startsWith("host/")) { // the computer's own settings, written straight to the host
            const bool ok=entry.setting=="host/volume"?host::setMasterVolume(entry.option.toInt()):host::setMasterMuted(entry.option.toBool());
            if(!ok)message_="The system volume could not be changed.";else if(!pages_.isEmpty())pages_.removeLast();
            break;
        }
        if(preferences_.setValue(entry.setting,entry.option) && !pages_.isEmpty())pages_.removeLast();break;
    case Action::EditName:emit requestSystemName(preferences_.value("system/name").toString());break;
    case Action::CreateUser:emit requestNewUser();break;
    case Action::SelectUser:profiles_.select(entry.value);break;
    case Action::Wallpaper:emit requestWallpaper();break;
    case Action::Theme:emit requestTheme();break;
    case Action::Information: {
        const host::Label label=[this](const QString& id,const QString& fallback){return assets_.text("sysconf_plugin",id,fallback);};
        message_=entry.value=="host:network"?host::networkStatus(label):entry.value=="host:display"?host::displayOutput():entry.value=="host:audio"?host::audioOutput():
            entry.value.isEmpty()?host::systemInformation(renderer_,label,preferences_.value("system/name").toString()):entry.value;break;
    }
    case Action::Library: {
        const auto path=preferences_.value("library/"+categories_[category_].id).toString();
        if(!path.isEmpty())openDirectory(path);else emit requestLibrary();break;
    }
    case Action::ChooseLibrary:emit requestLibrary();break;
    case Action::Folder:openDirectory(entry.value);break;
    case Action::File:openFile(entry);break;
    case Action::Launch:launch(entry);break;
    case Action::LaunchSteam:startSteamTracked(entry.value,entry.title);break;
    case Action::SteamFolder:openSteamFolder(entry.value);break;
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
            message_=entry.title+"\n\nSteam could not be asked to install this game.";
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
        if(app==contentApps_.cend() || !QFileInfo(app->executable).isFile())
            message_=entry.title+"\n\nThis application could not be started.";
        else if(!startTracked(app->executable,app->arguments,app->workingDirectory,entry.title))
            message_=entry.title+"\n\nThis application could not be started.";
        break;
    }
    case Action::OpenUrl:
        if(!QDesktopServices::openUrl(QUrl(entry.value)))message_=entry.title+"\n\nThe application could not be opened.";
        break;
    case Action::RefreshContent:refreshContent();break;
    case Action::RefreshFriends:refreshSteamFriends(true);break;
    case Action::ImportApps:emit requestImportApps();break;
    case Action::JellyfinApp:openJellyfin(entry);break;
    case Action::JellyfinFolder:loadJellyfinPage(entry.value,entry.title);break;
    case Action::JellyfinPlay:openJellyfinMedia(entry);break;
    case Action::JellyfinMore:loadJellyfinPage(entry.value,entry.detail,entry.option.toInt());break;
    case Action::JellyfinSignOut:signOutJellyfin();break;
    case Action::Unavailable:message_=entry.title+"\n\nThis feature is not available in the native recreation yet.";break;
    }
    emit changed();
}
// --- PS button -----------------------------------------------------------------------------------
// The console's behaviour, captured from RPCS3 on 2026-10-26 (audit/PS-BUTTON.md): a tap returns to the
// XMB and flashes the controller indicator; a hold opens a black system menu with Turn Off the System and
// Turn Off the Controller (and Quit Game while a title runs). A power action asks once before doing it.
void Shell::psTapped() {
    if(psMenu_) { psMenuClose(); return; }
    // Bring the XMB back over whatever the user was doing; the indicator itself is drawn by the view.
    message_.clear();status_.clear();
    emit psTapIndicator();
    emit changed();
}
void Shell::psHeld() {
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
    if(!psMenu_ || delta==0)return;
    if(psConfirmOff_){psSelection_=0;emit changed();return;}
    const int count=psMenuItems().size();
    psSelection_=(psSelection_+delta%count+count)%count;
    qInfo().noquote()<<"shell: PS menu selection"<<psSelection_;
    emit changed();
}
void Shell::psMenuActivate() {
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
        message_=gameTitle_+"\n\n"+QObject::tr("The game was closed.");
        psMenu_=false;
        emit changed();
        return;
    }
    // The console powers the controller off itself; a Bluetooth/USB pad on a PC is owned by Windows, so
    // say so instead of pretending.
    message_=QObject::tr("Turn Off the Controller")+"\n\n"+QObject::tr("A controller connected to this PC is managed by Windows and stays on. Disconnect or power it off from the system.");
    psMenu_=false;
    emit changed();
}
void Shell::psMenuClose() {
    if(!psMenu_ && !psConfirmOff_)return;
    psMenu_=false;psConfirmOff_=false;
    emit changed();
}
// --- Item options (Triangle) ----------------------------------------------------------------------
// Firmware behaviour (manual): Triangle opens a per-item options list as a panel on the right, not a new
// column; the listed actions vary by category. This build lists only commands it really performs. The list
// is derived from the selected entry, so any content surface gets the panel; a local folder page (Video
// Library) is the first surface proven end to end. Display All is deliberately absent: a page already lists
// every row it has, nothing is grouped or hidden, so the command would be a no-op.
QString Shell::activateLabel(const Entry& entry) const {
    switch(entry.action) {
    case Action::Folder:case Action::SteamFolder:case Action::JellyfinFolder:
    case Action::Library:case Action::SteamStoreBrowse:case Action::SteamStoreSection:
        return QObject::tr("Open");
    case Action::File:
        return categories_[category_].id=="photo"?QObject::tr("View"):QObject::tr("Play");
    case Action::Launch:case Action::LaunchSteam:case Action::LaunchApp:case Action::JellyfinPlay:
        return QObject::tr("Play");
    case Action::Information:return QObject::tr("View");
    default:return QObject::tr("Start");
    }
}
QString Shell::entryKind(const Entry& entry) const {
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
    case Action::JellyfinApp:case Action::JellyfinPlay:return QObject::tr("Jellyfin media");
    default:return QObject::tr("Item");
    }
}
void Shell::resetOptions() {
    optionsOpen_=optionsNested_=false;optionsSelection_=0;
    options_.clear();optionsParent_.clear();optionsTitle_.clear();
}
void Shell::buildOptions() {
    resetOptions();
    if(entries().isEmpty())return;
    const auto& entry=entries()[std::clamp(selection(),0,int(entries().size())-1)];
    // No Play/Start row for a row the shell itself cannot act on; Information stays honest for those.
    if(entry.action!=Action::Unavailable)options_.push_back({activateLabel(entry),OptionCommand::Activate});
    options_.push_back({QObject::tr("Information"),OptionCommand::Information});
    // Sort By reorders the rows actually on this page, which is exactly the firmware's promise.
    if(!pages_.isEmpty() && !pages_.last().directory.isEmpty() && entries().size()>1)
        options_.push_back({QObject::tr("Sort By"),OptionCommand::SortMenu});
    options_.push_back({QObject::tr("Close"),OptionCommand::Close});
}
void Shell::toggleOptions() {
    touch();
    if(optionsOpen_){dismissOptions();return;}
    if(!message_.isEmpty() || mediaOpen_ || psMenu_ || loading_ || entries().isEmpty())return;
    // A settings choice column already is the options panel; Triangle there adds nothing.
    bool choiceColumn=true;
    for(const auto& e:entries())if(e.action!=Action::SetSetting){choiceColumn=false;break;}
    if(choiceColumn)return;
    buildOptions();
    if(options_.isEmpty())return;
    optionsOpen_=true;
    interactionAudio_.play(InteractionAudio::Cue::Options);
    QStringList titles;for(const auto& option:options_)titles<<option.title;
    const auto& entry=entries()[std::clamp(selection(),0,int(entries().size())-1)];
    qInfo().noquote()<<"shell: options opened category="<<categories_[category_].id<<"depth="<<pages_.size()
        <<"entry="<<entry.id<<"rows="<<options_.size()<<titles.join('|');
    emit changed();
}
void Shell::optionsMove(int delta) {
    if(!optionsOpen_ || options_.isEmpty() || delta==0)return;
    const int count=options_.size();
    optionsSelection_=(optionsSelection_+delta%count+count)%count;
    interactionAudio_.play(InteractionAudio::Cue::Move);
    qInfo().noquote()<<"shell: options selection"<<optionsSelection_<<options_[optionsSelection_].title;
    emit changed();
}
void Shell::optionsActivate() {
    if(!optionsOpen_ || options_.isEmpty())return;
    const int index=std::clamp(optionsSelection_,0,int(options_.size())-1);
    const auto option=options_[index];
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
    case OptionCommand::SortMenu:
        optionsParent_=options_;optionsNested_=true;optionsTitle_=QObject::tr("Sort By");
        options_={{QObject::tr("Name (A-Z)"),OptionCommand::SortByNameAscending},
                  {QObject::tr("Name (Z-A)"),OptionCommand::SortByNameDescending},
                  {QObject::tr("Close"),OptionCommand::Close}};
        optionsSelection_=0;
        interactionAudio_.play(InteractionAudio::Cue::Options);
        qInfo().noquote()<<"shell: options sort menu opened";
        emit changed();
        break;
    case OptionCommand::SortByNameAscending:
    case OptionCommand::SortByNameDescending: {
        const bool ascending=option.command==OptionCommand::SortByNameAscending;
        resetOptions();
        sortPageByName(ascending);
        interactionAudio_.play(InteractionAudio::Cue::Confirm);
        emit changed();
        break;
    }
    case OptionCommand::Close:optionsClose();break;
    }
}
void Shell::optionsClose() {
    if(!optionsOpen_)return;
    if(optionsNested_) { // Circle steps back to the item list; the menu stays open
        options_=optionsParent_;optionsParent_.clear();optionsNested_=false;optionsTitle_.clear();optionsSelection_=0;
        interactionAudio_.play(InteractionAudio::Cue::Back);
        qInfo().noquote()<<"shell: options back to item list";
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
void Shell::showEntryInformation(const Entry& entry) {
    QStringList rows;
    rows<<QObject::tr("Kind: ")+entryKind(entry);
    if(!entry.detail.isEmpty())rows<<QObject::tr("Detail: ")+entry.detail;
    if(entry.action==Action::Folder || entry.action==Action::File || entry.action==Action::Library)
        rows<<QObject::tr("Path: ")+QDir::toNativeSeparators(entry.value);
    else if(!entry.value.isEmpty())rows<<QObject::tr("Value: ")+entry.value;
    // Title, blank line, then "Key: value" rows: the existing information-page layout detects this shape.
    message_=entry.title+"\n\n"+rows.join('\n');
    qInfo().noquote()<<"shell: options information title="<<entry.title<<"rows="<<rows.size();
}
void Shell::sortPageByName(bool ascending) {
    if(pages_.isEmpty())return;
    auto& page=pages_.last();
    if(page.entries.size()<2)return;
    const QString selected=page.entries.value(page.selection).id;
    // Folders first, then titles alphabetically; a stable sort keeps equal titles in their existing order.
    std::stable_sort(page.entries.begin(),page.entries.end(),[ascending](const Entry& left,const Entry& right){
        const bool leftFolder=left.action==Action::Folder,rightFolder=right.action==Action::Folder;
        if(leftFolder!=rightFolder)return leftFolder;
        const int order=QString::compare(left.title,right.title,Qt::CaseInsensitive);
        return ascending?order<0:order>0;
    });
    for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==selected){page.selection=i;break;}
    qInfo().noquote()<<"shell: options sorted page"<<page.title<<(ascending?"A-Z":"Z-A")<<page.entries.size()<<"rows";
}
void Shell::back() {
    touch();
    if(!message_.isEmpty() || mediaOpen_ || !pages_.isEmpty())interactionAudio_.play(InteractionAudio::Cue::Back);
    if(!message_.isEmpty())message_.clear();
    else if(mediaOpen_){
        // Leaving playback remembers where it was, so reopening resumes there. Ten seconds in and
        // fifteen from the end are not worth remembering on long media; short clips use a tenth.
        const qint64 at=player_->position(),total=player_->duration();
        bool worth=total>0 && at>std::max<qint64>(10000,total/20) && at<total-std::max<qint64>(15000,total/20);
        if(!worth && total>0 && total<=60000)worth=at>total/10 && at<total*9/10;
        if(!resumePath_.isEmpty() && worth)storeResumePosition(resumePath_,at);
        else clearResumePosition(resumePath_);
        resumePath_.clear();resumePending_=0;
        ++photoRequest_;++mediaRequest_;jellyfinRequest_=0;finishJellyfinPlayback();jellyfinQueue_=false;mediaOpen_=false;mediaQueue_.clear();mediaFailures_=0;slideshow_.stop();photo_={};player_->stop();player_->setSource({});loading_=false;status_.clear();emit mediaFrame({});}
    else if(!pages_.isEmpty()){library_.cancel();scanRequest_=0;jellyfinRequest_=0;loading_=false;status_.clear();pages_.removeLast();}
    emit changed();
}
void Shell::chooseLibrary(const QString& folder) {
    if(folder.isEmpty())return;
    if(preferences_.setValue("library/"+categories_[category_].id,folder)){openDirectory(folder);emit changed();}
}
void Shell::openDirectory(const QString& path) {
    if(pages_.size()>=32){message_="The maximum folder depth has been reached.";return;}
    Page page;page.title=QFileInfo(path).fileName();page.directory=path;
    const QString kind=categories_[category_].id;
    pages_.push_back(std::move(page));
    loading_=true;status_="Reading media folder…";
    scanRequest_=library_.scan(path,kind=="photo"?MediaKind::Photo:kind=="music"?MediaKind::Music:MediaKind::Video);
}
void Shell::openFile(const Entry& entry) {
    jellyfinQueue_=false;
    if(categories_[category_].id=="photo") {
        const auto request=++photoRequest_;loading_=true;photoRotation_=0;photo_={};emit mediaFrame({});
        auto* watcher=new QFutureWatcher<QImage>(this);
        connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,request]{
            watcher->deleteLater();if(request!=photoRequest_ || !mediaOpen_)return;
            loading_=false;photo_=watcher->result();
            if(photo_.isNull()){slideshow_.stop();mediaOpen_=false;message_="This image could not be opened.";}else showPhoto();
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
// Resume points: a small map of file id -> milliseconds in one preferences string. They are cleared
// when a file reaches its end, so a finished title starts from the beginning next time.
namespace {
QString resumeIdFor(const QString& path) {
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha1).toHex().left(24));
}
QJsonObject resumeMap(const QString& json) {
    const auto document=QJsonDocument::fromJson(json.toUtf8());
    return document.isObject()?document.object():QJsonObject{};
}
}
qint64 Shell::resumePositionFor(const QString& path) const {
    if(path.isEmpty())return 0;
    const auto value=resumeMap(preferences_.value("media/resume").toString()).value(resumeIdFor(path));
    return qint64(value.toDouble(0));
}
void Shell::storeResumePosition(const QString& path,qint64 positionMs) {
    if(path.isEmpty()||positionMs<=0)return;
    auto map=resumeMap(preferences_.value("media/resume").toString());
    map.insert(resumeIdFor(path),double(positionMs));
    // Keep the map bounded: drop entries once it grows past a playlist's worth of files.
    while(map.size()>64)map.erase(map.begin());
    preferences_.setValue("media/resume",QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
}
void Shell::clearResumePosition(const QString& path) {
    if(path.isEmpty())return;
    auto map=resumeMap(preferences_.value("media/resume").toString());
    if(!map.contains(resumeIdFor(path)))return;
    map.remove(resumeIdFor(path));
    preferences_.setValue("media/resume",QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
}
void Shell::startQueuedMedia() {
    if(jellyfinQueue_){startJellyfinMedia();return;}
    const auto* selected=mediaQueue_.current();if(!selected)return;
    const auto item=*selected; // player callbacks must not invalidate the selected queue entry
    ++mediaRequest_;mediaOpen_=true;mediaTitle_=item.title;loading_=true;status_.clear();
    // A saved position resumes here; the seek itself happens on LoadedMedia.
    resumePath_=item.path;
    resumePending_=resumePositionFor(item.path);
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
        finishJellyfinPlayback();
        if(failed){++mediaRequest_;jellyfinRequest_=0;loading_=false;player_->stop();status_="Playback stopped";message_="Jellyfin\n\nThis media could not be played. Check the server and its transcoding support.";emit changed();return;}
    }
    if(mediaQueue_.empty())return;
    if(failed) {
        if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"media queue: rejected"<<mediaTitle_<<error;
        if(++mediaFailures_>=mediaQueue_.items().size()) {
            ++mediaRequest_;loading_=false;player_->stop();
            message_="No playable files remain in this queue.\n\n"+error;status_="Playback stopped";emit changed();return;
        }
    } else mediaFailures_=0;
    // Errors skip Repeat One; otherwise a broken file could retry forever.
    const int next=mediaQueue_.next(!failed);
    if(next<0) {
        ++mediaRequest_;loading_=false;player_->stop();status_="End of queue";
        if(failed)message_="This file could not be played.\n\n"+error;
        if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"media queue: stopped at end";
        emit changed();return;
    }
    startQueuedMedia();
}
void Shell::skipMedia(int direction) {
    touch();if(!mediaOpen_ || !message_.isEmpty() || !direction)return;
    if(photoOpen()){stepPhoto(direction<0?-1:1);return;}
    const int next=direction<0?mediaQueue_.previous():mediaQueue_.next(false);
    if(next>=0){mediaFailures_=0;startQueuedMedia();}
}
void Shell::cycleMediaRepeat() {
    if(mediaOpen_ && !photoOpen() && message_.isEmpty())preferences_.setValue("media/repeatMode",(mediaRepeatMode()+1)%3);
}
void Shell::toggleMediaShuffle() {
    if(mediaOpen_ && !photoOpen() && message_.isEmpty())preferences_.setValue("media/shuffle",!preferences_.value("media/shuffle").toBool());
}
QString Shell::mediaQueueSummary() const {
    if(!mediaOpen_ || photoOpen() || mediaQueue_.empty())return {};
    QString result=QString("Track %1 of %2").arg(mediaQueue_.currentIndex()+1).arg(mediaQueue_.items().size());
    if(preferences_.value("media/shuffle").toBool())result+="  ·  Shuffle";
    if(mediaRepeatMode()==1)result+="  ·  Repeat One";else if(mediaRepeatMode()==2)result+="  ·  Repeat All";
    if(player_->playbackState()==QMediaPlayer::PausedState)result+="  ·  Paused";
    if(!status_.isEmpty())result+="  ·  "+status_;
    return result;
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
void Shell::touch(){idleTimeOverride_=-1;activity_.restart();}
QString Shell::appRegistryPath()const{return QDir(dataDirectory_).filePath("apps.json");}
bool Shell::idleDisplayAllowed()const{return !mediaOpen_ && !gameRunning() && message_.isEmpty() && !QApplication::activeModalWidget();}
bool Shell::mediaPaused()const{return photoOpen()?!slideshow_.isActive():player_->playbackState()!=QMediaPlayer::PlayingState;}
bool Shell::videoOpen()const{return mediaOpen_ && player_->hasVideo();}
void Shell::seekMedia(qint64 delta){
    if(!mediaOpen_ || photoOpen() || !player_->isSeekable())return;
    touch();player_->setPosition(MediaControls::seekTarget(player_->position(),player_->duration()>0?player_->duration():-1,delta));emit changed();
}
void Shell::applyPreferences(){
    audio_->setVolume(preferences_.value("audio/volume").toInt()/100.f);audio_->setMuted(preferences_.value("audio/muted").toBool());
    interactionAudio_.setEnabled(preferences_.value("audio/keyTone").toBool() && !preferences_.value("audio/muted").toBool());
    interactionAudio_.setVolume(preferences_.value("audio/volume").toInt()/100.f);
    if(slideshow_.isActive())slideshow_.setInterval(preferences_.value("photo/interval").toInt());
    mediaQueue_.setRepeat(static_cast<PlaybackQueue::Repeat>(mediaRepeatMode()));
    mediaQueue_.setShuffle(preferences_.value("media/shuffle").toBool());
    loadTheme();
}
void Shell::loadTheme() {
    const auto path=preferences_.value("appearance/theme").toString();
    if(path==theme_.path)return;
    theme_={};theme_.path=path; // a theme that can't be read stays chosen and shows the system's own pieces
    interactionAudio_.setThemeSounds({}); // switching to Original or a failed theme clears previous overrides
    if(path.isEmpty())return;
    QString error;const auto p3t=P3t::load(path,&error);
    if(p3t.isNull()){message_=QFileInfo(path).fileName()+"\n\n"+error;qWarning().noquote()<<"theme:"<<path<<error;return;}
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
    if(pages_.isEmpty() && !mediaOpen_) {
        if(categories_[category_].id=="friends")refreshSteamFriends(true);else refreshContent();
        return;
    }
    if(pages_.isEmpty() || pages_.last().directory.isEmpty() || mediaOpen_)return;
    loading_=true;status_="Reading media folder…";
    const auto kind=categories_[category_].id;
    // A rescan of a page that is already open (F5 / Refresh Directory) is reported when it lands.
    directoryRefresh_=true;
    scanRequest_=library_.scan(pages_.last().directory,kind=="photo"?MediaKind::Photo:kind=="music"?MediaKind::Music:MediaKind::Video);emit changed();
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
}
void Shell::createUser(const QString& name){profiles_.create(name);}
QString Shell::subtitleText(const Entry& entry) const {
    // User-scoped data carries the active user's name, as in the reference (Trophy Collection, PS3 saved data).
    return entry.id=="trophy" || entry.id=="ps3"?profiles_.activeName():QString();
}
}
