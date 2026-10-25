#include "shell.h"
#include "assets.h"
#include "catalog.h"
#include "host.h"
#include "p3t.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QDir>
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
Shell::Shell(QString dataDirectory,Assets& assets,QObject* parent):QObject(parent),
    assets_(assets),preferences_(dataDirectory),library_(),interactionAudio_(assets.root()),profiles_(dataDirectory),
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
        if(status==QMediaPlayer::EndOfMedia && mediaOpen_ && preferences_.value("media/repeat").toBool()){player_->setPosition(0);player_->play();}
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
    connect(player_.get(),&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,const QString& error){message_=error;emit changed();});
}
Shell::~Shell()=default;
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
    case Action::Unavailable:message_=entry.title+"\n\nThis feature is not available in the native recreation yet.";break;
    }
    emit changed();
}
void Shell::back() {
    touch();
    if(!message_.isEmpty() || mediaOpen_ || !pages_.isEmpty())interactionAudio_.play(InteractionAudio::Cue::Back);
    if(!message_.isEmpty())message_.clear();
    else if(mediaOpen_){++photoRequest_;slideshow_.stop();photo_={};player_->stop();player_->setSource({});mediaOpen_=false;loading_=false;emit mediaFrame({});}
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
    } else {emit mediaFrame({});player_->setSource(QUrl::fromLocalFile(entry.value));player_->play();}
    mediaOpen_=true;mediaTitle_=entry.title;
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
    if(player_->playbackState()==QMediaPlayer::PlayingState)player_->pause();else if(!player_->source().isEmpty())player_->play();
}
void Shell::touch(){activity_.restart();}
void Shell::applyPreferences(){
    audio_->setVolume(preferences_.value("audio/volume").toInt()/100.f);audio_->setMuted(preferences_.value("audio/muted").toBool());
    interactionAudio_.setEnabled(preferences_.value("audio/keyTone").toBool() && !preferences_.value("audio/muted").toBool());
    interactionAudio_.setVolume(preferences_.value("audio/volume").toInt()/100.f);
    if(slideshow_.isActive())slideshow_.setInterval(preferences_.value("photo/interval").toInt());
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
