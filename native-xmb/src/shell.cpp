#include "shell.h"
#include "assets.h"
#include "catalog.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QDir>
#include <QFileInfo>
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
}
Shell::Shell(QString dataDirectory,Assets& assets,QObject* parent):QObject(parent),
    assets_(assets),preferences_(dataDirectory),library_(),interactionAudio_(assets.root()),profiles_(dataDirectory),
    player_(std::make_unique<QMediaPlayer>()),audio_(std::make_unique<QAudioOutput>()),video_(std::make_unique<QVideoSink>()) {
    QDir().mkpath(dataDirectory);
    const QString sys="explore_category_sysconf",full="explore_plugin_full";
    categories_={
        {"user","Users",full,"user_tex_load",{item("new-user","Create New User","item_tex_NewUser"),item("user","User","user_tex_login")}},
        {"settings","Settings","qgl/icontex","2n",{
            item("update","System Update","tex_update",sys),item("game","Game Settings","tex_game",sys),
            item("video","Video Settings","tex_bddvd",sys,Action::SettingsPage,"video"),item("music","Music Settings","tex_music",sys,Action::SettingsPage,"music"),
            item("chat","Chat Settings","tex_chat",sys),item("system","System Settings","tex_console",sys,Action::SettingsPage,"system"),
            item("theme","Theme Settings","tex_thema",sys,Action::SettingsPage,"theme"),item("date","Date and Time Settings","tex_datetime",sys,Action::SettingsPage,"date"),
            item("power","Power Save Settings","tex_power",sys,Action::SettingsPage,"power"),item("device","Accessory Settings","tex_pd",sys,Action::SettingsPage,"device"),
            item("print","Printer Settings","tex_printer",sys),item("display","Display Settings","tex_display",sys,Action::SettingsPage,"display"),
            item("sound","Sound Settings","tex_sound",sys,Action::SettingsPage,"sound"),item("security","Security Settings","tex_security",sys),
            item("remote","Remote Play Settings","tex_remote",sys),item("network","Network Settings","tex_network",sys)}},
        {"photo","Photo","qgl/icontex","3n",{item("library","Photo Library","photo_tex_album_pic",full,Action::Library),item("options","Photo Settings","tex_photo",full,Action::SettingsPage,"photo"),item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"music","Music","qgl/icontex","4n",{item("library","Music Library","music_tex_album_pic",full,Action::Library),item("options","Music Settings","tex_music",full,Action::SettingsPage,"music"),item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"video","Video","qgl/icontex","5n",{item("library","Video Library","video_tex_album_default",full,Action::Library),item("options","Video Settings","tex_video",full,Action::SettingsPage,"video"),item("choose","Choose Media Folder","item_tex_plain_folder",full,Action::ChooseLibrary)}},
        {"tv","TV/Video Services",full,"tex_video",{item("services","My Channels","tex_video")}},
        {"game","Game","qgl/icontex","6n",{
            item("vita","PS Vita System Application Utility","item_tex_vitaformat"),item("data","Game Data Utility","item_tex_ps3game_folder"),
            item("memory","Memory Card Utility","item_tex_ps12util"),item("ps2","Saved Data Utility (PS2)","item_tex_ps2game_folder"),
            item("psp","Saved Data Utility (PSP)","item_tex_pspgame_folder"),item("ps3","Saved Data Utility (PS3)","item_tex_ps3sd_folder"),
            item("store","PlayStation Store","item_tex_ps_store")}},
        {"network","Network","qgl/icontex","7n",{
            item("manual","Online Instruction Manuals","tex_browser","explore_category_network"),
            item("remote","Remote Play","tex_premo","explore_category_network"),
            item("browser","Internet Browser","tex_browser","explore_category_network"),
            item("search","Internet Search","tex_browser","explore_category_network"),
            item("downloads","Download Management","tex_dl_manage","explore_category_network")}},
        {"psn","PlayStation Network",full,"tex_psn",{item("signin","Sign In","tex_Sign_In"),item("trophy","Trophy Collection","item_tex_trophy"),item("store","PlayStation Store","item_tex_ps_store")}},
        {"friends","Friends","qgl/icontex","8n",{item("add","Add a Friend","item_tex_NewFriend"),item("chat","Start New Chat","item_tex_NewAvc"),item("players","Players Met","item_tex_Players")}}
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
    selections_.fill(0,categories_.size());
    rebuildUsers();
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
    if(!message_.isEmpty()){message_.clear();emit changed();return;}
    if(mediaOpen_){togglePlayback();return;}
    if(entries().isEmpty() || loading_)return;
    const auto entry=entries()[selection()];
    interactionAudio_.play(entry.action==Action::Unavailable?InteractionAudio::Cue::Error:InteractionAudio::Cue::Confirm);
    switch(entry.action) {
    case Action::SettingsPage:openSettings(entry.value);break;
    case Action::SettingChoices:openChoices(entry.setting,entry.title);break;
    case Action::SetSetting:
        if(entry.setting=="appearance/background" && entry.option.toInt()==2 && wallpaper().isEmpty()){emit requestWallpaper();break;}
        if(preferences_.setValue(entry.setting,entry.option) && !pages_.isEmpty())pages_.removeLast();break;
    case Action::EditName:emit requestSystemName(preferences_.value("system/name").toString());break;
    case Action::CreateUser:emit requestNewUser();break;
    case Action::SelectUser:profiles_.select(entry.value);break;
    case Action::Wallpaper:emit requestWallpaper();break;
    case Action::Information:
        message_=entry.value.isEmpty()?"System Information\n\nNative XMB 0.2.0\n"+QSysInfo::prettyProductName()+"\n"+QSysInfo::currentCpuArchitecture():entry.value;break;
    case Action::Library: {
        const auto path=preferences_.value("library/"+categories_[category_].id).toString();
        if(!path.isEmpty())openDirectory(path);else emit requestLibrary();break;
    }
    case Action::ChooseLibrary:emit requestLibrary();break;
    case Action::Folder:openDirectory(entry.value);break;
    case Action::File:openFile(entry);break;
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
    for(const auto& profile:profiles_.profiles()) {
        users.push_back(item(profile.id,profile.name,"user_tex_login","explore_plugin_full",Action::SelectUser,profile.id));
        if(profile.id==profiles_.activeId())selections_[0]=int(users.size())-1;
    }
}
void Shell::createUser(const QString& name){profiles_.create(name);}
}
