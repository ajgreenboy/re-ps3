#include "shell.h"
#include <QMediaPlayer>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
namespace xmb {
namespace {
Entry jfEntry(QString id,QString title,Action action,QString value={},bool folder=false) {
    Entry e;e.id=std::move(id);e.title=std::move(title);e.action=action;e.value=std::move(value);
    e.container="explore_plugin_full";e.texture=folder?"item_tex_plain_folder":"tex_video";return e;
}
bool sameService(QUrl a,QUrl b){return a.adjusted(QUrl::StripTrailingSlash|QUrl::NormalizePathSegments)==b.adjusted(QUrl::StripTrailingSlash|QUrl::NormalizePathSegments);}
bool playable(const QString& type){return QStringList{"Movie","Episode","Video","MusicVideo","Audio","Trailer","Recording"}.contains(type);}
}
void Shell::initJellyfin() {
    connect(&jellyfin_,&JellyfinClient::authenticated,this,[this]{
        if(!jellyfinSigningIn_)return;
        jellyfinSigningIn_=false;emit jellyfinSignInFinished(true,{});
        loadJellyfinPage({},jellyfinServiceTitle_,0,true);
    });
    connect(&jellyfin_,&JellyfinClient::failed,this,[this](quint64 request,const QString& error){
        if(request==0){if(jellyfinSigningIn_){jellyfinSigningIn_=false;emit jellyfinSignInFinished(false,error);}return;}
        if(request!=jellyfinRequest_)return;
        jellyfinRequest_=0;loading_=false;status_.clear();
        if(jellyfinQueue_ && mediaOpen_){finishJellyfinPlayback();player_->stop();}
        message_="Jellyfin\n\n"+error;emit changed();
    });
    connect(&jellyfin_,&JellyfinClient::pageReady,this,[this](quint64 request,const JellyfinPage& result){
        if(request!=jellyfinRequest_ || pages_.size()!=jellyfinRequestDepth_ || pages_.isEmpty() || !pages_.last().jellyfin)return;
        jellyfinRequest_=0;loading_=false;status_.clear();auto& page=pages_.last();
        const QString selected=page.entries.isEmpty()?QString{}:page.entries.value(page.selection).id;
        page.entries.clear();page.selection=0;
        for(const auto& media:result.items) {
            const bool folder=page.jellyfinViews || media.folder;
            auto e=jfEntry("jellyfin:"+media.id,media.title,folder?Action::JellyfinFolder:playable(media.type)?Action::JellyfinPlay:Action::Unavailable,media.id,folder);
            e.detail=folder?QString{}:media.type=="Audio"?QString("Music"):media.type=="Movie"?QString("Movie"):media.type=="Episode"?QString("Episode"):QString{};e.option=media.type=="Audio";
            if(media.type=="Audio")e.texture="tex_music";
            if(e.id==selected)page.selection=int(page.entries.size());page.entries.push_back(std::move(e));
        }
        const qint64 next=qint64(page.jellyfinStart)+result.items.size();
        if(!page.jellyfinViews && !result.items.empty() && next<result.total && next<=1000000){auto more=jfEntry("jellyfin:more","More…",Action::JellyfinMore,page.jellyfinParent,true);more.option=int(next);more.detail=page.title;page.entries.push_back(std::move(more));}
        if(page.jellyfinViews){auto out=jfEntry("jellyfin:signout","Sign Out of Jellyfin",Action::JellyfinSignOut);out.texture="tex_Sign_In";page.entries.push_back(std::move(out));}
        if(page.entries.empty())status_="There are no supported items in this library.";
        emit changed();
    });
    connect(&jellyfin_,&JellyfinClient::playbackReady,this,[this](quint64 request,const JellyfinPlayback& playback){
        if(request!=jellyfinRequest_ || !jellyfinQueue_ || !mediaOpen_)return;
        jellyfinRequest_=0;jellyfinPlayback_=playback;
        const auto local=jellyfinStream_.open(playback.url,jellyfin_.authorizationHeader());
        if(!local.isValid() || local.isEmpty()){advanceMedia(true,{});return;}
        player_->setSource(local);player_->play();emit changed();
    });
    connect(&jellyfinStream_,&JellyfinStream::failed,this,[this](const QString&){
        if(!jellyfinQueue_ || !mediaOpen_)return;const auto request=mediaRequest_;
        QTimer::singleShot(0,this,[this,request]{if(request==mediaRequest_ && mediaOpen_ && jellyfinQueue_)advanceMedia(true,{});});
    });
    connect(player_.get(),&QMediaPlayer::playbackStateChanged,this,[this](QMediaPlayer::PlaybackState state){
        if(!jellyfinQueue_ || !mediaOpen_ || jellyfinPlayback_.itemId.isEmpty())return;
        if(state==QMediaPlayer::PlayingState && !jellyfinStarted_){jellyfinStarted_=true;jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),false,"start");}
        else if(jellyfinStarted_ && state!=QMediaPlayer::StoppedState)jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),state==QMediaPlayer::PausedState,"progress");
    });
    jellyfinProgress_.setInterval(10000);
    connect(&jellyfinProgress_,&QTimer::timeout,this,[this]{if(jellyfinQueue_ && mediaOpen_ && jellyfinStarted_)jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),mediaPaused(),"progress");});
    jellyfinProgress_.start();
}
void Shell::openJellyfin(const Entry& entry) {
    const auto app=std::find_if(contentApps_.cbegin(),contentApps_.cend(),[&](const ContentApp& candidate){return candidate.id==entry.value && candidate.kind=="jellyfin";});
    if(app==contentApps_.cend()){message_="This Jellyfin registration could not be found.";return;}
    jellyfinServiceTitle_=app->title;jellyfinRootDepth_=int(pages_.size());
    const QUrl server(app->url);
    if(jellyfin_.signedIn() && sameService(server,jellyfin_.serverUrl()))loadJellyfinPage({},jellyfinServiceTitle_,0,true);
    else {jellyfinRequest_=0;jellyfin_.signOut();emit requestJellyfinSignIn(server);}
}
void Shell::authenticateJellyfin(QUrl server,QString username,QString password){jellyfinSigningIn_=true;jellyfin_.signIn(server,std::move(username),std::move(password));}
void Shell::cancelJellyfinSignIn(){jellyfinSigningIn_=false;jellyfinRequest_=0;jellyfin_.signOut();loading_=false;status_.clear();emit changed();}
void Shell::loadJellyfinPage(const QString& parent,const QString& title,int start,bool views,bool replace) {
    if(!jellyfin_.signedIn()){message_="Jellyfin\n\nSign out and reopen the app to sign in again.";emit changed();return;}
    if(!replace && pages_.size()>=32){message_="The maximum folder depth has been reached.";emit changed();return;}
    if(!replace){Page page;page.title=title;pages_.push_back(std::move(page));}
    if(pages_.empty())return;
    auto& page=pages_.last();page.jellyfin=true;page.jellyfinViews=views;page.jellyfinParent=parent;page.jellyfinStart=start;
    loading_=true;status_="Loading Jellyfin…";jellyfinRequestDepth_=int(pages_.size());
    jellyfinRequest_=views?jellyfin_.views():jellyfin_.items(parent,start);emit changed();
}
void Shell::openJellyfinMedia(const Entry& entry) {
    QVector<PlaybackQueue::Item> queue;int selected=-1;jellyfinAudio_.clear();
    for(const auto& row:entries())if(row.action==Action::JellyfinPlay){if(row.id==entry.id)selected=int(queue.size());queue.push_back({row.value,row.title});jellyfinAudio_.insert(row.value,row.option.toBool());}
    if(selected<0)return;
    jellyfinQueue_=true;mediaQueue_.clear();mediaQueue_.setRepeat(static_cast<PlaybackQueue::Repeat>(mediaRepeatMode()));
    mediaQueue_.setShuffle(preferences_.value("media/shuffle").toBool());mediaQueue_.reset(std::move(queue),selected);mediaFailures_=0;startJellyfinMedia();
}
void Shell::startJellyfinMedia() {
    const auto* current=mediaQueue_.current();if(!current)return;const auto media=*current;
    ++mediaRequest_;jellyfinRequest_=0;finishJellyfinPlayback();player_->stop();player_->setSource({});
    mediaOpen_=true;mediaTitle_=media.title;loading_=true;status_.clear();emit mediaFrame({});
    for(int i=0;i<entries().size();++i)if(entries()[i].value==media.path && entries()[i].action==Action::JellyfinPlay){currentSelection()=i;break;}
    jellyfinRequest_=jellyfin_.playback(media.path,jellyfinAudio_.value(media.path));emit changed();
}
void Shell::finishJellyfinPlayback() {
    if(!jellyfinPlayback_.itemId.isEmpty())jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),mediaPaused(),"stop");
    jellyfinPlayback_={};jellyfinStarted_=false;jellyfinStream_.close();
}
void Shell::signOutJellyfin() {
    jellyfinRequest_=0;finishJellyfinPlayback();jellyfin_.signOut();jellyfinQueue_=false;loading_=false;status_.clear();
    while(pages_.size()>jellyfinRootDepth_)pages_.removeLast();emit changed();
}
}
