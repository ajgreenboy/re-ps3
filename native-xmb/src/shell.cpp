#include "shell.h"
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
void Shell::launch(const Entry& entry) {
    const QString emulator=emulatorPath();
    if(emulator.isEmpty() || !QProcess::startDetached(emulator,{"--no-gui",entry.value},QFileInfo(emulator).absolutePath())) {
        message_=entry.title+"\n\nRPCS3 could not be started.";return;
    }
    qInfo().noquote()<<"launch:"<<emulator<<"--no-gui"<<entry.value;
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
// Rows inside a Steam library folder. Built on demand so a 280-title library does not
// cost anything until the folder is opened; only a bounded number of cached thumbnails
// are decoded here.
QVector<Entry> Shell::steamFolderEntries(const QString& mode) const {
    QVector<Entry> rows;
    const QString cache=QDir(dataDirectory_).filePath("cache/steam");
    int decoded=0;
    const auto add=[&](const QString& appId,const QString& title,const QString& detail,Action action) {
        auto e=item("steam:"+appId,title,"game_tex_default_ps3","explore_plugin_full",action,appId);
        e.gameTile=true;e.detail=detail;
        if(decoded<64) {
            QImageReader reader(SteamArtwork::cachePath(cache,appId));
            if(reader.canRead()) {
                const auto size=reader.size();
                if(size.isValid() && size.width()<=16384 && size.height()<=16384) {
                    reader.setScaledSize(size.scaled(512,288,Qt::KeepAspectRatio));
                    const auto image=reader.read();
                    if(!image.isNull()){e.artwork=image;++decoded;}
                }
            }
        }
        rows.push_back(std::move(e));
    };
    if(mode=="installed") {
        for(const auto& game:steamInstalled_.games) {
            const QString label=collectionLabel(game.appId);
            add(game.appId,game.title,label.isEmpty()?QObject::tr("Installed"):label,Action::LaunchSteam);
        }
    } else if(mode=="library") {
        for(const auto& game:ownedGames_) {
            if(game.installed)continue;
            const QString label=collectionLabel(game.appId);
            add(game.appId,game.title,label.isEmpty()?QObject::tr("Not installed"):label+" - Not installed",Action::InstallSteam);
        }
    } else if(mode=="family") {
        for(const auto& game:familyGames_)add(game.appId,game.title,QObject::tr("Family shared"),Action::InstallSteam);
    }
    return rows;
}
void Shell::openSteamFolder(const QString& mode) {
    const QString title=mode=="installed"?QObject::tr("Installed Games")
        :mode=="family"?QObject::tr("Family Shared"):QObject::tr("Not Installed");
    Page page;page.title=title;page.entries=steamFolderEntries(mode);
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
                return e.id.startsWith("steam:") || e.id.startsWith("steam-folder-") || e.id.startsWith("custom:");
            }),category.entries.end());
            QVector<Entry> providers;
            if(category.id=="game") {
                // Steam lives in folders so the column is not a 280-row wall: installed titles,
                // the rest of the owned library, and family-shared titles when provided.
                const int installedCount=int(result.steam.games.size());
                const int ownedNotInstalled=int(std::count_if(ownedGames_.cbegin(),ownedGames_.cend(),
                    [](const OwnedGame& game){return !game.installed;}));
                if(installedCount>0) {
                    auto e=item("steam-folder-installed",QObject::tr("Installed Games"),"item_tex_ps3game_folder",
                        "explore_plugin_full",Action::SteamFolder,"installed");
                    e.detail=QString::number(installedCount);providers.push_back(std::move(e));
                }
                if(ownedNotInstalled>0) {
                    auto e=item("steam-folder-library",QObject::tr("Not Installed"),"item_tex_plain_folder",
                        "explore_plugin_full",Action::SteamFolder,"library");
                    e.detail=QString::number(ownedNotInstalled);providers.push_back(std::move(e));
                }
                if(!familyGames_.isEmpty()) {
                    auto e=item("steam-folder-family",QObject::tr("Family Shared"),"item_tex_plain_folder",
                        "explore_plugin_full",Action::SteamFolder,"family");
                    e.detail=QString::number(familyGames_.size());providers.push_back(std::move(e));
                }
            }
            for(const auto& app:contentApps_)if(app.category==category.id) {
                auto e=item("custom:"+app.id,app.title,app.kind=="game"?"game_tex_default_ps3":"tex_video","explore_plugin_full",
                    app.kind=="game"?Action::LaunchApp:Action::OpenUrl,app.kind=="game"?app.id:app.url);
                e.detail=app.kind=="game"?QString{}:"Service app · Opens in your browser";
                e.artwork=result.art.value("custom:"+app.id);e.gameTile=app.kind=="game";providers.push_back(std::move(e));
            }
            category.entries=providers+category.entries;
            selections_[c]=0;
            for(int i=0;i<category.entries.size();++i)if(category.entries[i].id==selected){selections_[c]=i;break;}
            if(!contentLoaded_ && category.id=="game" && !providers.isEmpty())selections_[c]=0;
        }
        contentLoaded_=true;
        if(accountChanged){steamFriendList_.clear();friendsAge_.invalidate();rebuildFriends();}
        if(SteamOwned::hasApiKey())refreshSteamFriends(false,accountChanged);
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
        const QString setup="Steam Friends\n\nFriends and presence come from Steam.\nSet STEAM_WEB_API_KEY in your launch environment and restart XMB.\nYour most recent local Steam account is selected automatically.\nUse XMB_STEAM_ID to select another numeric Steam ID.\nThe API key is never stored in XMB settings.\nPrivate lists and unavailable connections are reported here.";
        entries.push_back(item("steam-friends:setup","Steam Connection","item_tex_Profile_Onlines","explore_category_friend",Action::Information,setup));
        selections_[c]=0;for(int i=0;i<entries.size();++i)if(entries[i].id==selected){selections_[c]=i;break;}
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
        for(auto& category:categories_)for(auto& e:category.entries)if(e.id=="steam:"+id)e.artwork=image;
        emit changed();
    });
    connect(&steamRefresh_,&QTimer::timeout,this,[this]{
        if(categories_[category_].id=="friends" && SteamOwned::hasApiKey())refreshSteamFriends();
    });
    steamRefresh_.start(60000);
    steamFriendsError_="Connect Steam friends with STEAM_WEB_API_KEY, or open the Steam friends window.";
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
        emit changed();
    });
    connect(&slideshow_,&QTimer::timeout,this,[this]{stepPhoto(1);});
    connect(player_.get(),&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus status){
        if(mediaOpen_ && !photoOpen()) {
            loading_=status==QMediaPlayer::LoadingMedia;
            if(status==QMediaPlayer::EndOfMedia) {
                const auto request=mediaRequest_;
                QTimer::singleShot(0,this,[this,request]{if(request==mediaRequest_ && mediaOpen_ && !photoOpen())advanceMedia(false);});
            }
        }
        emit changed();
    });
    connect(&idle_,&QTimer::timeout,this,[this]{
        const int minutes=preferences_.value("power/idleMinutes").toInt();
        if(minutes && !mediaOpen_ && activity_.elapsed()>=qint64(minutes)*60000)emit requestQuit();
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
}
Shell::~Shell(){disconnect(&steamFriends_,nullptr,this,nullptr);disconnect(steamArtwork_.get(),nullptr,this,nullptr);}
const QVector<Entry>& Shell::entries()const {return pages_.isEmpty()?categories_[category_].entries:pages_.last().entries;}
const QVector<Entry>& Shell::entriesAt(int depth)const {return depth==0?categories_[category_].entries:pages_.at(depth-1).entries;}
int Shell::selectionAt(int depth)const {return depth==0?selections_[category_]:pages_.at(depth-1).selection;}
void Shell::horizontal(int delta) {
    touch();if(!delta)return;
    if(!message_.isEmpty())return;
    if(mediaOpen_) {
        if(categories_[category_].id=="photo") {
            stepPhoto(delta);
        } else player_->setPosition(std::max<qint64>(0,player_->position()+delta*10000));
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
            entry.value.isEmpty()?host::systemInformation(renderer_,label):entry.value;break;
    }
    case Action::Library: {
        const auto path=preferences_.value("library/"+categories_[category_].id).toString();
        if(!path.isEmpty())openDirectory(path);else emit requestLibrary();break;
    }
    case Action::ChooseLibrary:emit requestLibrary();break;
    case Action::Folder:openDirectory(entry.value);break;
    case Action::File:openFile(entry);break;
    case Action::Launch:launch(entry);break;
    case Action::LaunchSteam: {
        const QString exe=QDir(steamRoot_).filePath("steam.exe");
        if(!QFileInfo(exe).isFile() || !QProcess::startDetached(exe,{"-applaunch",entry.value},steamRoot_))
            message_=entry.title+"\n\nSteam could not be started. Refresh the game library and check the Steam installation.";
        break;
    }
    case Action::SteamFolder:openSteamFolder(entry.value);break;
    case Action::InstallSteam: {
        // Steam owns the download: opening steam://install/<id> asks the client to install it.
        if(!QDesktopServices::openUrl(QUrl(SteamOwned::installUrl(entry.value))))
            message_=entry.title+"\\n\\nSteam could not be asked to install this game.";
        break;
    }
    case Action::LaunchApp: {
        const auto app=std::find_if(contentApps_.cbegin(),contentApps_.cend(),[&](const auto& a){return a.id==entry.value;});
        if(app==contentApps_.cend() || !QFileInfo(app->executable).isFile() ||
           !QProcess::startDetached(app->executable,app->arguments,app->workingDirectory))message_=entry.title+"\n\nThis application could not be started.";
        break;
    }
    case Action::OpenUrl:
        if(!QDesktopServices::openUrl(QUrl(entry.value)))message_=entry.title+"\n\nThe application could not be opened.";
        break;
    case Action::RefreshContent:refreshContent();break;
    case Action::RefreshFriends:refreshSteamFriends(true);break;
    case Action::ImportApps:emit requestImportApps();break;
    case Action::Unavailable:message_=entry.title+"\n\nThis feature is not available in the native recreation yet.";break;
    }
    emit changed();
}
void Shell::back() {
    touch();
    if(!message_.isEmpty() || mediaOpen_ || !pages_.isEmpty())interactionAudio_.play(InteractionAudio::Cue::Back);
    if(!message_.isEmpty())message_.clear();
    else if(mediaOpen_){++photoRequest_;++mediaRequest_;mediaOpen_=false;mediaQueue_.clear();mediaFailures_=0;slideshow_.stop();photo_={};player_->stop();player_->setSource({});loading_=false;status_.clear();emit mediaFrame({});}
    else if(!pages_.isEmpty()){library_.cancel();scanRequest_=0;loading_=false;status_.clear();pages_.removeLast();}
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
void Shell::startQueuedMedia() {
    const auto* selected=mediaQueue_.current();if(!selected)return;
    const auto item=*selected; // player callbacks must not invalidate the selected queue entry
    ++mediaRequest_;mediaOpen_=true;mediaTitle_=item.title;loading_=true;status_.clear();
    for(int i=0;i<entries().size();++i)if(entries()[i].action==Action::File && entries()[i].value==item.path){currentSelection()=i;break;}
    emit mediaFrame({});
    const auto source=QUrl::fromLocalFile(item.path);
    if(player_->source()==source){loading_=false;player_->setPosition(0);}else player_->setSource(source);
    player_->play();
    if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"media queue: play"<<mediaQueue_.currentIndex()+1<<"of"<<mediaQueue_.items().size()<<item.title;
    emit changed();
}
void Shell::advanceMedia(bool failed,const QString& error) {
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
bool Shell::photoOpen()const{return mediaOpen_ && categories_[category_].id=="photo";}
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
void Shell::touch(){activity_.restart();}
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
    if(pages_.isEmpty() && !mediaOpen_) {
        if(categories_[category_].id=="friends")refreshSteamFriends(true);else refreshContent();
        return;
    }
    if(pages_.isEmpty() || pages_.last().directory.isEmpty() || mediaOpen_)return;
    loading_=true;status_="Reading media folder…";
    const auto kind=categories_[category_].id;
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
