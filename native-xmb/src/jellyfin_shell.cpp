#include "shell.h"
#include "media_controls.h"
#include <QMediaPlayer>
#include <QDir>
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
// The row's one detail line: what it is, where it stands, how long it is. Kept to printable ASCII
// because the label renderer is the firmware's and the evidence crops are ASCII text.
QString progressDetail(const Entry& entry) {
    QStringList parts;
    if(!entry.mediaKind.isEmpty())parts<<entry.mediaKind;
    if(entry.resumeMs>0)parts<<QObject::tr("Resume at %1").arg(MediaControls::formatTime(entry.resumeMs));
    else if(entry.played)parts<<QObject::tr("Watched");
    if(entry.durationMs>0)parts<<MediaControls::formatTime(entry.durationMs);
    return parts.join(QStringLiteral(" - "));
}
// Posters are fetched for the rows a page can show without scrolling far. One request per row per
// page open; the client caches each picture under the data directory, keyed by the server's tag.
constexpr int ArtworkRowsPerPage = 60;
}
void Shell::initJellyfin() {
    jellyfin_.setImageCache(QDir(dataDirectory_).filePath("jellyfin/images"));
    jellyfin_.setSessionStore(QDir(dataDirectory_).filePath("jellyfin/session.bin"));
    connect(&jellyfin_,&JellyfinClient::authenticated,this,[this]{
        if(!jellyfinSigningIn_)return;
        jellyfinSigningIn_=false;emit jellyfinSignInFinished(true,{});
        // The opt-in is the page's Remember row, and the stored record's existence is the only
        // durable form of it: turning the row off deletes the record on the way out.
        if(signInRemember_) {
            const bool saved=jellyfin_.saveSession();
            qInfo().noquote()<<(saved?"jellyfin: remembered sign-in stored (DPAPI, this Windows user; no password kept)"
                                      :"jellyfin: could not store the remembered sign-in; this session only");
        } else {
            jellyfin_.forgetSession();
            qInfo()<<"jellyfin: stay-signed-in is off; nothing was written";
        }
        loadJellyfinPage({},jellyfinServiceTitle_,0,true);
    });
    connect(&jellyfin_,&JellyfinClient::failed,this,[this](quint64 request,const QString& error){
        if(request==0){if(jellyfinSigningIn_){jellyfinSigningIn_=false;emit jellyfinSignInFinished(false,error);}return;}
        if(request!=jellyfinRequest_)return;
        jellyfinRequest_=0;loading_=false;status_.clear();
        // A remembered session the server no longer accepts: forget it and show the sign-in page,
        // because that is the surface that can fix it.
        if(jellyfinRestoring_) {
            jellyfinRestoring_=false;
            jellyfin_.signOut();
            jellyfin_.forgetSession();
            qWarning().noquote()<<"jellyfin: the remembered session was refused; it has been forgotten, signing in again";
            openJellyfinSignInPage(jellyfinServiceServer_);
            return;
        }
        if(jellyfinQueue_ && mediaOpen_){finishJellyfinPlayback();player_->stop();}
        message_="Jellyfin\n\n"+error;emit changed();
    });
    connect(&jellyfin_,&JellyfinClient::pageReady,this,[this](quint64 request,const JellyfinPage& result){
        if(request!=jellyfinRequest_ || pages_.size()!=jellyfinRequestDepth_ || pages_.isEmpty() || !pages_.last().jellyfin)return;
        jellyfinRequest_=0;loading_=false;status_.clear();jellyfinRestoring_=false;auto& page=pages_.last();
        const QString selected=page.entries.isEmpty()?QString{}:page.entries.value(page.selection).id;
        page.entries.clear();page.selection=0;
        int posters=0;
        QString backdrop;
        for(const auto& media:result.items) {
            const bool folder=page.jellyfinViews || media.folder;
            auto e=jfEntry("jellyfin:"+media.id,media.title,folder?Action::JellyfinFolder:playable(media.type)?Action::JellyfinPlay:Action::Unavailable,media.id,folder);
            e.mediaKind=folder?QString{}:media.type=="Audio"?QString("Music"):media.type=="Movie"?QString("Movie"):media.type=="Episode"?QString("Episode"):QString("Video");e.option=media.type=="Audio";
            if(!folder){e.resumeMs=media.resumeMs;e.durationMs=media.durationMs;e.played=media.played;e.detail=progressDetail(e);}
            if(media.type=="Audio")e.texture="tex_music";
            if(!folder && e.action!=Action::Unavailable) {
                if(!media.primaryTag.isEmpty() && posters<ArtworkRowsPerPage) {
                    ++posters;
                    jellyfin_.artwork(media.id,media.primaryTag,false);
                }
                if(backdrop.isEmpty() && !media.backdropTag.isEmpty()) {
                    backdrop=media.id;
                    jellyfinBackdropId_=media.id;
                    jellyfin_.artwork(media.id,media.backdropTag,true);
                }
            }
            if(e.id==selected)page.selection=int(page.entries.size());page.entries.push_back(std::move(e));
        }
        const qint64 next=qint64(page.jellyfinStart)+result.items.size();
        if(!page.jellyfinViews && !result.items.empty() && next<result.total && next<=1000000){auto more=jfEntry("jellyfin:more","More...",Action::JellyfinMore,page.jellyfinParent,true);more.option=int(next);more.detail=page.title;page.entries.push_back(std::move(more));}
        if(page.jellyfinSearch) {
            auto again=jfEntry("jellyfin:search",tr("Search Again"),Action::JellyfinSearch);
            again.detail=tr("Type another title, episode or album name.");
            page.entries.push_back(std::move(again));
        }
        if(page.jellyfinViews) {
            auto search=jfEntry("jellyfin:search",tr("Search Jellyfin"),Action::JellyfinSearch);
            search.detail=tr("Type a title, an episode or an album name to search the server.");
            page.entries.push_back(std::move(search));
            auto out=jfEntry("jellyfin:signout","Sign Out of Jellyfin",Action::JellyfinSignOut);out.texture="tex_Sign_In";page.entries.push_back(std::move(out));
        }
        if(page.entries.empty())status_=page.jellyfinSearch?"No Jellyfin items match that search.":"There are no supported items in this library.";
        qInfo().noquote()<<QString("jellyfin: %1 page with %2 row(s); %3 poster request(s)%4")
            .arg(page.jellyfinViews?QStringLiteral("library"):page.jellyfinSearch?QStringLiteral("search"):QStringLiteral("folder"))
            .arg(page.entries.size()).arg(posters)
            .arg(backdrop.isEmpty()?QString{}:QStringLiteral(" and one backdrop"));
        emit changed();
    });
    connect(&jellyfin_,&JellyfinClient::artworkReady,this,[this](quint64,const QString& itemId,bool backdrop,const QImage& image){
        if(image.isNull())return;
        bool updated=false;
        if(backdrop) {
            if(itemId==jellyfinBackdropId_ && jellyfinBackdrop_!=image){jellyfinBackdrop_=image;updated=true;}
        } else {
            for(auto& page:pages_)if(page.jellyfin)for(auto& row:page.entries) {
                if(row.value!=itemId || row.action!=Action::JellyfinPlay)continue;
                row.artwork=image;updated=true;
            }
        }
        if(updated)emit changed();
    });
    connect(&jellyfin_,&JellyfinClient::playbackReady,this,[this](quint64 request,const JellyfinPlayback& playback){
        if(request!=jellyfinRequest_ || !jellyfinQueue_ || !mediaOpen_)return;
        jellyfinRequest_=0;jellyfinPlayback_=playback;
        QStringList streams;
        for(const auto& stream:playback.streams)streams<<stream.type+":"+stream.title;
        qInfo().noquote()<<"jellyfin: playback"<<playback.itemId<<playback.playMethod<<playback.streams.size()
            <<"server stream(s)"<<streams.join('|');
        const auto local=jellyfinStream_.open(playback.url,jellyfin_.authorizationHeader());
        if(!local.isValid() || local.isEmpty()){advanceMedia(true,{});return;}
        player_->setSource(local);player_->play();emit changed();
    });
    connect(&jellyfinStream_,&JellyfinStream::failed,this,[this](const QString&){
        if(!jellyfinQueue_ || !mediaOpen_)return;const auto request=mediaRequest_;
        QTimer::singleShot(0,this,[this,request]{if(request==mediaRequest_ && mediaOpen_ && jellyfinQueue_)advanceMedia(true,{});});
    });
    connect(player_.get(),&QMediaPlayer::playbackStateChanged,this,[this]{
        if(jellyfinResumePending_>0)applyJellyfinResume();else reportJellyfinPlaybackState();
    });
    connect(player_.get(),&QMediaPlayer::seekableChanged,this,[this]{applyJellyfinResume();});
    connect(player_.get(),&QMediaPlayer::durationChanged,this,[this]{applyJellyfinResume();});
    connect(player_.get(),&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus state){
        if(state!=QMediaPlayer::LoadedMedia && state!=QMediaPlayer::BufferedMedia)return;
        applyJellyfinResume();
        if(jellyfinQueue_ && jellyfinResumePending_>0) {
            const auto request=mediaRequest_;
            QTimer::singleShot(1500,this,[this,request]{
                if(request!=mediaRequest_ || !jellyfinQueue_ || !mediaOpen_ || jellyfinResumePending_<=0)return;
                applyJellyfinResume();
                if(jellyfinResumePending_>0 && !player_->isSeekable()) {
                    qWarning().noquote()<<"jellyfin: this stream cannot seek; playing from the beginning instead of resuming at"
                        <<jellyfinResumePending_<<"ms";
                    jellyfinResumePending_=0;status_=tr("This stream cannot seek; playing from the beginning.");
                    reportJellyfinPlaybackState();emit changed();
                }
            });
        }
    });
    jellyfinProgress_.setInterval(10000);
    connect(&jellyfinProgress_,&QTimer::timeout,this,[this]{
        if(!jellyfinQueue_ || !mediaOpen_ || !jellyfinStarted_)return;
        qInfo().noquote()<<"jellyfin: progress report at"<<player_->position()<<"ms";
        jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),mediaPaused(),"progress");
    });
    jellyfinProgress_.start();
}
void Shell::openJellyfin(const Entry& entry) {
    const auto app=std::find_if(contentApps_.cbegin(),contentApps_.cend(),[&](const ContentApp& candidate){return candidate.id==entry.value && candidate.kind=="jellyfin";});
    if(app==contentApps_.cend()){message_="This Jellyfin registration could not be found.";return;}
    jellyfinServiceTitle_=app->title;jellyfinRootDepth_=int(pages_.size());
    const QUrl server(app->url);
    jellyfinServiceServer_=server;
    if(jellyfin_.signedIn() && sameService(server,jellyfin_.serverUrl()))loadJellyfinPage({},jellyfinServiceTitle_,0,true);
    // A stored session is the explicit "stay signed in" opt-in from the sign-in page; it is only
    // used for the registration it was made for.
    else if(jellyfin_.hasStoredSession() && jellyfin_.restoreSession() && sameService(server,jellyfin_.serverUrl())) {
        qInfo().noquote()<<"jellyfin: using the remembered session for"<<server.toString()<<"(DPAPI record, no password)";
        jellyfinRestoring_=true;
        loadJellyfinPage({},jellyfinServiceTitle_,0,true);
    } else {jellyfinRequest_=0;jellyfin_.signOut();emit requestJellyfinSignIn(server);}
}
void Shell::authenticateJellyfin(QUrl server,QString username,QString password){jellyfinSigningIn_=true;jellyfin_.signIn(server,std::move(username),std::move(password));}
void Shell::cancelJellyfinSignIn(){jellyfinSigningIn_=false;jellyfinRequest_=0;jellyfin_.signOut();loading_=false;status_.clear();emit changed();}
void Shell::loadJellyfinPage(const QString& parent,const QString& title,int start,bool views,bool replace) {
    if(!jellyfin_.signedIn()){message_="Jellyfin\n\nSign out and reopen the app to sign in again.";emit changed();return;}
    if(!replace && pages_.size()>=32){message_="The maximum folder depth has been reached.";emit changed();return;}
    if(!replace){Page page;page.title=title;pages_.push_back(std::move(page));}
    if(pages_.empty())return;
    auto& page=pages_.last();page.jellyfin=true;page.jellyfinViews=views;page.jellyfinParent=parent;page.jellyfinStart=start;
    jellyfinBackdrop_={};jellyfinBackdropId_.clear();
    loading_=true;status_="Loading Jellyfin...";jellyfinRequestDepth_=int(pages_.size());
    jellyfinRequest_=views?jellyfin_.views():jellyfin_.items(parent,start);emit changed();
}
void Shell::openJellyfinSearch(const QString& term) {
    const QString query=term.trimmed();
    if(query.isEmpty())return;
    if(!jellyfin_.signedIn()){message_="Jellyfin\n\nSign out and reopen the app to sign in again.";emit changed();return;}
    if(pages_.size()>=32){message_="The maximum folder depth has been reached.";emit changed();return;}
    // One search page at a time: a second search replaces the first result page instead of stacking.
    if(!pages_.isEmpty() && pages_.last().jellyfinSearch)pages_.removeLast();
    const QString title=tr("Search: %1").arg(query);
    Page page;page.title=title;
    page.jellyfin=true;page.jellyfinSearch=true;page.jellyfinParent=query;
    pages_.push_back(std::move(page));
    loadJellyfinSearchPage(query,title,0,true);
}
void Shell::loadJellyfinSearchPage(const QString& term,const QString& title,int start,bool replace) {
    if(!jellyfin_.signedIn()){message_="Jellyfin\n\nSign out and reopen the app to sign in again.";emit changed();return;}
    if(!replace){Page page;page.title=title;page.jellyfinSearch=true;pages_.push_back(std::move(page));}
    if(pages_.empty())return;
    auto& page=pages_.last();page.jellyfin=true;page.jellyfinSearch=true;page.jellyfinParent=term;page.jellyfinStart=start;
    jellyfinBackdrop_={};jellyfinBackdropId_.clear();
    loading_=true;status_=tr("Searching Jellyfin...");jellyfinRequestDepth_=int(pages_.size());
    qInfo().noquote()<<"jellyfin: search"<<term<<"from"<<start;
    jellyfinRequest_=jellyfin_.search(term,start);emit changed();
}
void Shell::openJellyfinMedia(const Entry& entry,bool fromBeginning) {
    QVector<PlaybackQueue::Item> queue;int selected=-1;jellyfinAudio_.clear();jellyfinResume_.clear();
    for(const auto& row:entries())if(row.action==Action::JellyfinPlay){if(row.id==entry.id)selected=int(queue.size());queue.push_back({row.value,row.title});jellyfinAudio_.insert(row.value,row.option.toBool());jellyfinResume_.insert(row.value,row.resumeMs);}
    if(selected<0)return;
    if(fromBeginning)jellyfinResume_.insert(entry.value,0);
    qInfo().noquote()<<QString("jellyfin: %1 \"%2\" (server position %3 ms)")
        .arg(fromBeginning?QStringLiteral("play from the beginning"):QStringLiteral("play"))
        .arg(entry.title).arg(fromBeginning?0:entry.resumeMs);
    jellyfinQueue_=true;mediaQueue_.clear();mediaQueue_.setRepeat(static_cast<PlaybackQueue::Repeat>(mediaRepeatMode()));
    mediaQueue_.setShuffle(preferences_.value("media/shuffle").toBool());mediaQueue_.reset(std::move(queue),selected);mediaFailures_=0;startJellyfinMedia();
}
void Shell::startJellyfinMedia() {
    const auto* current=mediaQueue_.current();if(!current)return;const auto media=*current;
    ++mediaRequest_;jellyfinRequest_=0;finishJellyfinPlayback();player_->stop();player_->setSource({});
    resumePath_.clear();resumePending_=0;
    jellyfinResumePending_=jellyfinResume_.value(media.path);
    mediaOpen_=true;mediaTitle_=media.title;loading_=true;status_.clear();emit mediaFrame({});
    if(jellyfinResumePending_>0) {
        status_=tr("Resuming at %1").arg(MediaControls::formatTime(jellyfinResumePending_));
        qInfo().noquote()<<"jellyfin: resume pending at"<<jellyfinResumePending_<<"ms for"<<media.title;
    }
    for(int i=0;i<entries().size();++i)if(entries()[i].value==media.path && entries()[i].action==Action::JellyfinPlay){currentSelection()=i;break;}
    jellyfinRequest_=jellyfin_.playback(media.path,jellyfinAudio_.value(media.path));emit changed();
}
void Shell::applyJellyfinResume() {
    if(!jellyfinQueue_ || !mediaOpen_ || jellyfinPlayback_.itemId.isEmpty() || jellyfinResumePending_<=0 || jellyfinApplyingResume_ || !player_->isSeekable())return;
    if(player_->mediaStatus()!=QMediaPlayer::LoadedMedia && player_->mediaStatus()!=QMediaPlayer::BufferedMedia)return;
    const qint64 reported=jellyfinResumePending_;
    const qint64 target=player_->duration()>0?std::min(reported,std::max<qint64>(0,player_->duration()-1)):reported;
    jellyfinResumePending_=0;jellyfinApplyingResume_=true;player_->setPosition(target);jellyfinApplyingResume_=false;
    status_.clear();
    qInfo().noquote()<<QString("jellyfin: resume applied at %1 ms (server reported %2 ms, duration %3 ms)")
        .arg(target).arg(reported).arg(player_->duration());
    reportJellyfinPlaybackState();emit changed();
}
void Shell::reportJellyfinPlaybackState() {
    if(!jellyfinQueue_ || !mediaOpen_ || jellyfinPlayback_.itemId.isEmpty() || jellyfinResumePending_>0 || jellyfinApplyingResume_)return;
    const auto state=player_->playbackState();
    if(state==QMediaPlayer::PlayingState && !jellyfinStarted_){
        jellyfinStarted_=true;
        qInfo().noquote()<<"jellyfin: start report at"<<player_->position()<<"ms";
        jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),false,"start");
    } else if(jellyfinStarted_ && state!=QMediaPlayer::StoppedState)
        jellyfin_.reportPlayback(jellyfinPlayback_,player_->position(),state==QMediaPlayer::PausedState,"progress");
}
void Shell::finishJellyfinPlayback(bool completed) {
    if(!jellyfinPlayback_.itemId.isEmpty()) {
        const auto id=jellyfinPlayback_.itemId;
        const qint64 duration=player_->duration();
        // A completed title is reported at its end position even when the backend has already
        // rewound to zero, so the server can mark it played; an abandoned one is reported where it
        // actually stopped.
        const qint64 at=completed?(duration>0?duration:player_->position())
            :(jellyfinStarted_?player_->position():jellyfinResume_.value(id));
        // A finished title is not "paused": the stop is the completion report, not a pause.
        jellyfin_.reportPlayback(jellyfinPlayback_,at,completed?false:mediaPaused(),"stop");
        qInfo().noquote()<<QString("jellyfin: stop report item=%1 at %2 ms%3").arg(id).arg(at)
            .arg(completed?" (reached the end)":"");
        if(jellyfinStarted_ || completed) {
            const qint64 resume=completed?0:at;
            jellyfinResume_.insert(id,resume);
            for(auto& page:pages_)if(page.jellyfin)for(auto& row:page.entries)if(row.action==Action::JellyfinPlay && row.value==id) {
                row.resumeMs=resume;if(duration>0)row.durationMs=duration;row.played=completed;row.detail=progressDetail(row);
                if(completed)qInfo().noquote()<<"jellyfin: row now reads"<<row.detail;
            }
        }
    }
    jellyfinPlayback_={};jellyfinStarted_=false;jellyfinResumePending_=0;jellyfinStream_.close();
}
void Shell::signOutJellyfin() {
    jellyfinRequest_=0;finishJellyfinPlayback();jellyfin_.signOut();jellyfin_.forgetSession();
    qInfo()<<"jellyfin: signed out; the remembered session was forgotten";
    jellyfinResume_.clear();jellyfinQueue_=false;loading_=false;status_.clear();
    while(pages_.size()>jellyfinRootDepth_)pages_.removeLast();emit changed();
}
bool Shell::jellyfinRemembered() const { return jellyfin_.hasStoredSession(); }
}
