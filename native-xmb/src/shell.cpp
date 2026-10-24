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
#include <algorithm>

namespace xmb {
namespace {
Entry item(QString id,QString title,QString texture,QString container="explore_plugin_full",Action action=Action::Unavailable,QString value={}) {
    return {std::move(id),std::move(title),{},std::move(container),std::move(texture),action,std::move(value)};
}
}
Shell::Shell(QString dataDirectory,Assets& assets,QObject* parent):QObject(parent),
    settings_(QDir(dataDirectory).filePath("settings.ini"),QSettings::IniFormat),
    player_(std::make_unique<QMediaPlayer>()),audio_(std::make_unique<QAudioOutput>()),video_(std::make_unique<QVideoSink>()) {
    QDir().mkpath(dataDirectory);
    const QString sys="explore_category_sysconf",full="explore_plugin_full";
    categories_={
        {"user","Users",full,"user_tex_load",{item("new-user","Create New User","item_tex_NewUser"),item("user","User","user_tex_login")}},
        {"settings","Settings","qgl/icontex","2n",{
            item("update","System Update","tex_update",sys),item("game","Game Settings","tex_game",sys),
            item("video","Video Settings","tex_bddvd",sys),item("music","Music Settings","tex_music",sys),
            item("chat","Chat Settings","tex_chat",sys),item("system","System Settings","tex_console",sys,Action::Information),
            item("theme","Theme Settings","tex_thema",sys,Action::Theme),item("date","Date and Time Settings","tex_datetime",sys),
            item("power","Power Save Settings","tex_power",sys),item("device","Accessory Settings","tex_pd",sys),
            item("print","Printer Settings","tex_printer",sys),item("display","Display Settings","tex_display",sys),
            item("sound","Sound Settings","tex_sound",sys),item("security","Security Settings","tex_security",sys),
            item("remote","Remote Play Settings","tex_remote",sys),item("network","Network Settings","tex_network",sys)}},
        {"photo","Photo","qgl/icontex","3n",{item("library","Photo Library","photo_tex_album_pic",full,Action::Library)}},
        {"music","Music","qgl/icontex","4n",{item("library","Music Library","music_tex_album_pic",full,Action::Library)}},
        {"video","Video","qgl/icontex","5n",{item("library","Video Library","video_tex_album_default",full,Action::Library)}},
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
    player_->setAudioOutput(audio_.get());player_->setVideoSink(video_.get());audio_->setVolume(.6f);
    connect(video_.get(),&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame& frame){emit mediaFrame(frame.toImage());});
    connect(player_.get(),&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,const QString& error){message_=error;emit changed();});
}
Shell::~Shell()=default;
const QVector<Entry>& Shell::entries()const {return pages_.isEmpty()?categories_[category_].entries:pages_.last().entries;}
const QVector<Entry>& Shell::entriesAt(int depth)const {return depth==0?categories_[category_].entries:pages_.at(depth-1).entries;}
int Shell::selectionAt(int depth)const {return depth==0?selections_[category_]:pages_.at(depth-1).selection;}
void Shell::horizontal(int delta) {
    if(!message_.isEmpty())return;
    if(mediaOpen_) {
        if(categories_[category_].id=="photo") {
            const auto& files=entries();
            for(int i=selection()+delta;i>=0 && i<files.size();i+=delta)if(files[i].action==Action::File) {
                const auto file=files[i];currentSelection()=i;openFile(file);emit changed();break;
            }
        } else player_->setPosition(std::max<qint64>(0,player_->position()+delta*10000));
        return;
    }
    if(!pages_.isEmpty())return;
    category_=std::clamp(category_+delta,0,int(categories_.size())-1);emit changed();
}
void Shell::vertical(int delta) {
    if(mediaOpen_ || !message_.isEmpty())return;
    currentSelection()=std::clamp(selection()+delta,0,std::max(0,int(entries().size())-1));emit changed();
}
void Shell::activate() {
    if(!message_.isEmpty()){message_.clear();emit changed();return;}
    if(mediaOpen_){togglePlayback();return;}
    if(entries().isEmpty())return;
    const auto entry=entries()[selection()];
    switch(entry.action) {
    case Action::Theme: {
        Page page;page.title="Theme Settings";
        page.entries={item("theme","Theme","23n","qgl/icontex"),
            item("colour","Color","23n","qgl/icontex",Action::Colours),
            item("background","Background","23n","qgl/icontex"),
            item("font","Font","23n","qgl/icontex")};
        page.entries[0].detail="Sets for use of a preset combination of elements such as color, background or icons.";
        pages_.push_back(std::move(page));
        break;
    }
    case Action::Colours: {
        Page page;page.title="Theme Settings";page.selection=std::clamp(theme(),0,5);
        for(int i=0;i<6;++i)page.entries.push_back(item("colour"+QString::number(i),QStringList{"Original","Blue","Purple","Red","Green","Silver"}[i],"23n","qgl/icontex",Action::Colour,QString::number(i)));
        pages_.push_back(std::move(page));
        break;
    }
    case Action::Colour:settings_.setValue("appearance/colour",entry.value.toInt());settings_.sync();break;
    case Action::Information:
        message_="System Information\n\nNative XMB recreation\n"+QSysInfo::prettyProductName()+"\n"+QSysInfo::currentCpuArchitecture()+"\n\nIndependent C++ application; PS3 services are being rebuilt.";break;
    case Action::Library: {
        const auto path=settings_.value("library/"+categories_[category_].id).toString();
        if(QDir(path).exists() && !path.isEmpty())openDirectory(path);else emit requestLibrary();break;
    }
    case Action::Folder:openDirectory(entry.value);break;
    case Action::File:openFile(entry);break;
    case Action::Unavailable:message_=entry.title+"\n\nThis feature is not available in the native recreation yet.";break;
    }
    emit changed();
}
void Shell::back() {
    if(!message_.isEmpty())message_.clear();
    else if(mediaOpen_){player_->stop();player_->setSource({});mediaOpen_=false;emit mediaFrame({});}
    else if(!pages_.isEmpty())pages_.removeLast();
    emit changed();
}
void Shell::chooseLibrary(const QString& folder) {
    if(folder.isEmpty())return;
    settings_.setValue("library/"+categories_[category_].id,folder);settings_.sync();openDirectory(folder);emit changed();
}
void Shell::openDirectory(const QString& path) {
    QDir dir(path);
    if(!dir.exists()){message_="This folder is no longer available.";return;}
    Page page;page.title=dir.dirName();
    const QString kind=categories_[category_].id;
    const QStringList extensions=kind=="photo"?QStringList{"png","jpg","jpeg","bmp","webp"}:kind=="music"?QStringList{"mp3","flac","wav","m4a","ogg"}:QStringList{"mp4","mkv","avi","mov","webm"};
    const auto files=dir.entryInfoList(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot,QDir::DirsFirst|QDir::Name);
    for(const auto& file:files) {
        if(page.entries.size()>=2000)break;
        if(!file.isDir() && !extensions.contains(file.suffix().toLower()))continue;
        page.entries.push_back(item(file.absoluteFilePath(),file.fileName(),file.isDir()?"item_tex_plain_folder":kind=="photo"?"photo_tex_album_pic":kind=="music"?"music_tex_album_pic":"video_tex_album_default","explore_plugin_full",file.isDir()?Action::Folder:Action::File,file.absoluteFilePath()));
    }
    if(page.entries.isEmpty())message_="No supported files in this folder.";
    pages_.push_back(std::move(page));
}
void Shell::openFile(const Entry& entry) {
    if(categories_[category_].id=="photo") {
        QImageReader reader(entry.value);reader.setAutoTransform(true);
        const auto size=reader.size();
        if(size.width()>3840 || size.height()>2160)reader.setScaledSize(size.scaled(3840,2160,Qt::KeepAspectRatio));
        const auto frame=reader.read();
        if(frame.isNull()){message_="This image could not be opened.";return;}
        emit mediaFrame(frame);
    } else {emit mediaFrame({});player_->setSource(QUrl::fromLocalFile(entry.value));player_->play();}
    mediaOpen_=true;mediaTitle_=entry.title;
}
void Shell::togglePlayback(){if(!mediaOpen_ || categories_[category_].id=="photo")return;if(player_->playbackState()==QMediaPlayer::PlayingState)player_->pause();else if(!player_->source().isEmpty())player_->play();}
}
