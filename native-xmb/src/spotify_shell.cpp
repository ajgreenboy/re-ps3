// Spotify in the Music column (audit/SPOTIFY.md). Pages are ordinary XMB pages of rows; nothing here draws.
//
//   Music > Spotify            not configured: one row whose page says how to set the client id
//                              signed out:     "Sign In with Spotify" (opens the browser), and the redirect URI
//                              signed in:      Now Playing, Liked Songs, the user's playlists, Sign Out
//   a playlist / Liked Songs   its tracks; Cross plays from that track on a Spotify Connect device
//   Play on                    shown only when several devices are available and none is active
//
// Playback is Spotify's: this app sends the command and Spotify plays on the chosen device. Every refusal
// (Premium required, no device, a playlist the API keeps closed) is shown with Spotify's own reason.
#include "shell.h"
#include "assets.h"
#include "media_controls.h"
#include <QDir>
#include <QTimer>
#include <algorithm>

namespace xmb {
namespace {
Entry spotifyRow(QString id,QString title,Action action,QString value={},QString detail={}) {
    Entry entry;entry.id=std::move(id);entry.title=std::move(title);entry.action=action;entry.value=std::move(value);
    entry.detail=std::move(detail);entry.container=QStringLiteral("explore_plugin_full");entry.texture=QStringLiteral("item_tex_plain_folder");
    entry.source=QStringLiteral("native");
    return entry;
}
QString nowPlayingText(const SpotifyNowPlaying& state) {
    if(state.track.isEmpty())return QObject::tr("Nothing is playing on Spotify.");
    QString text=state.track;
    if(!state.artists.isEmpty())text+=QStringLiteral(" - ")+state.artists;
    text+=QStringLiteral(" - ")+(state.playing?QObject::tr("playing"):QObject::tr("paused"));
    if(!state.device.isEmpty())text+=QObject::tr(" on %1").arg(state.device);
    return text;
}
}

void Shell::initSpotify() {
    spotify_.configure(dataDirectory_);
    spotify_.setSessionStore(QDir(dataDirectory_).filePath("spotify/session.bin"));
    if(spotify_.configured())spotify_.restoreSession();
    connect(&spotify_,&SpotifyClient::signedInChanged,this,[this]{
        // A sign-in that finished while the Spotify page is open fills it now.
        const bool rootOpen=std::any_of(pages_.begin(),pages_.end(),[](const Page& p){return p.spotify==QLatin1String("root");});
        if(spotify_.signedIn() && rootOpen && spotifyPlaylists_.isEmpty() && !spotifyPlaylistsRequest_) {
            spotifyPlaylistsRequest_=spotify_.playlists();
            spotify_.nowPlaying();
        }
        if(!spotify_.signedIn()){spotifyPlaylists_.clear();spotifyNowPlaying_={};}
        refreshSpotifyRoot();emit changed();
    });
    connect(&spotify_,&SpotifyClient::failed,this,[this](quint64 request,const QString& message){
        // A failure of the page being loaded, of a play, or of the sign-in itself is shown; a failed background
        // refresh (Now Playing) only logs, so it never interrupts browsing.
        if(request!=0 && request!=spotifyRequest_ && request!=spotifyDevicesRequest_ && request!=spotifyCommandRequest_)return;
        if(request==spotifyRequest_)spotifyRequest_=0;
        if(request==spotifyDevicesRequest_)spotifyDevicesRequest_=0;
        if(request==spotifyCommandRequest_)spotifyCommandRequest_=0;
        loading_=false;status_.clear();
        showMessage(message,MessageKind::Box);
        emit changed();
    });
    connect(&spotify_,&SpotifyClient::playlistsReady,this,[this](quint64 request,const QVector<SpotifyPlaylist>& lists){
        if(request!=spotifyPlaylistsRequest_)return;
        spotifyPlaylistsRequest_=0;spotifyPlaylists_=lists;
        qInfo().noquote()<<"spotify:"<<lists.size()<<"playlist(s)";
        refreshSpotifyRoot();emit changed();
    });
    connect(&spotify_,&SpotifyClient::nowPlayingReady,this,[this](quint64,const SpotifyNowPlaying& state){
        spotifyNowPlaying_=state;refreshSpotifyRoot();emit changed();
    });
    connect(&spotify_,&SpotifyClient::tracksReady,this,[this](quint64 request,const QVector<SpotifyTrack>& tracks){
        if(request!=spotifyRequest_)return;
        spotifyRequest_=0;loading_=false;status_.clear();
        if(pages_.isEmpty() || !pages_.last().spotify.startsWith(QLatin1String("list:")))return;
        auto& page=pages_.last();
        page.entries.clear();
        for(int i=0;i<tracks.size();++i) {
            const auto& track=tracks[i];
            QStringList detail;
            if(!track.artists.isEmpty())detail<<track.artists;
            if(!track.album.isEmpty())detail<<track.album;
            if(track.durationMs>0)detail<<MediaControls::formatTime(track.durationMs);
            auto row=spotifyRow(QStringLiteral("spotify:track:%1").arg(i),track.name,Action::SpotifyTrack,track.uri,detail.join(QStringLiteral(" - ")));
            row.texture=QStringLiteral("music_tex_album_pic");
            if(!track.playable) {
                row.action=Action::Unavailable;row.disabled=true;
                row.detail=QObject::tr("Not playable on Spotify here (a local file or not available in this market).");
            }
            page.entries.push_back(std::move(row));
        }
        qInfo().noquote()<<"spotify:"<<page.title<<"with"<<tracks.size()<<"track(s)";
        if(page.entries.isEmpty()) {
            pages_.removeLast();
            showMessage(assets_.text("explore_plugin_full","msg_error_no_content","There are no content items."),MessageKind::Inline);
        }
        emit changed();
    });
    connect(&spotify_,&SpotifyClient::devicesReady,this,[this](quint64 request,const QVector<SpotifyDevice>& devices){
        if(request!=spotifyDevicesRequest_)return;
        spotifyDevicesRequest_=0;loading_=false;status_.clear();
        QVector<SpotifyDevice> usable;
        for(const auto& device:devices)if(!device.restricted && !device.id.isEmpty())usable.push_back(device);
        qInfo().noquote()<<"spotify:"<<devices.size()<<"device(s),"<<usable.size()<<"usable";
        if(usable.isEmpty()) {
            showMessage(QObject::tr("No Spotify device is available. Open Spotify on this PC, a phone or a speaker, then try again."),MessageKind::Box);
            emit changed();return;
        }
        // The active device, else the only one; with several and none active, the user chooses.
        const auto active=std::find_if(usable.begin(),usable.end(),[](const SpotifyDevice& d){return d.active;});
        if(active!=usable.end()){spotifyPlayOn(active->id,active->name);return;}
        if(usable.size()==1){spotifyPlayOn(usable.first().id,usable.first().name);return;}
        if(pages_.size()>=32)return;
        Page page;page.title=QObject::tr("Play on");page.spotify=QStringLiteral("devices");
        for(const auto& device:usable)
            page.entries.push_back(spotifyRow(QStringLiteral("spotify:device:")+device.id,device.name,Action::SpotifyDevice,device.id,device.type));
        pages_.push_back(std::move(page));
        emit changed();
    });
    connect(&spotify_,&SpotifyClient::commandDone,this,[this](quint64 request){
        if(request!=spotifyCommandRequest_)return;
        spotifyCommandRequest_=0;
        if(!spotifyCommandNote_.isEmpty())notify(spotifyCommandNote_,spotifyCommandDetail_);
        spotifyCommandNote_.clear();spotifyCommandDetail_.clear();
        // Spotify applies the command asynchronously; read the player back a moment later for Now Playing.
        QTimer::singleShot(1200,this,[this]{if(spotify_.signedIn())spotify_.nowPlaying();});
        emit changed();
    });
}

QVector<Entry> Shell::spotifyRootRows() {
    QVector<Entry> rows;
    if(!spotify_.configured()) {
        rows.push_back(spotifyRow(QStringLiteral("spotify:setup"),QObject::tr("Set Up Spotify"),Action::Unavailable,{},spotify_.configurationError()));
        return rows;
    }
    if(!spotify_.signedIn()) {
        rows.push_back(spotifyRow(QStringLiteral("spotify:signin"),QObject::tr("Sign In with Spotify"),Action::SpotifySignIn,{},
            QObject::tr("Opens Spotify's sign-in in the browser; it returns to %1.").arg(spotify_.redirectUri())));
        return rows;
    }
    rows.push_back(spotifyRow(QStringLiteral("spotify:now"),QObject::tr("Now Playing"),Action::SpotifyNowPlaying,{},nowPlayingText(spotifyNowPlaying_)));
    rows.push_back(spotifyRow(QStringLiteral("spotify:liked"),QObject::tr("Liked Songs"),Action::SpotifyLiked));
    for(const auto& playlist:spotifyPlaylists_) {
        auto row=spotifyRow(QStringLiteral("spotify:playlist:")+playlist.id,playlist.name,Action::SpotifyPlaylist,playlist.id,
            QObject::tr("%1 tracks").arg(playlist.tracks)+(playlist.owner.isEmpty()?QString():QStringLiteral(" - ")+playlist.owner));
        row.setting=playlist.uri;
        row.texture=QStringLiteral("tex_playlist");row.container=QStringLiteral("explore_category_music");
        rows.push_back(std::move(row));
    }
    QString who=spotify_.displayName();
    if(!spotify_.product().isEmpty())who+=(who.isEmpty()?QString():QStringLiteral(" - "))+spotify_.product();
    rows.push_back(spotifyRow(QStringLiteral("spotify:signout"),QObject::tr("Sign Out of Spotify"),Action::SpotifySignOut,{},who));
    return rows;
}

void Shell::refreshSpotifyRoot() {
    for(auto& page:pages_) {
        if(page.spotify!=QLatin1String("root"))continue;
        const QString focused=page.entries.value(page.selection).id;
        page.entries=spotifyRootRows();
        page.selection=0;
        for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==focused){page.selection=i;break;}
    }
}

void Shell::openSpotify() {
    if(pages_.size()>=32){showMessage(QObject::tr("The maximum folder depth has been reached."),MessageKind::Page);return;}
    Page page;page.title=QStringLiteral("Spotify");page.spotify=QStringLiteral("root");page.entries=spotifyRootRows();
    pages_.push_back(std::move(page));
    if(spotify_.signedIn()) {
        spotifyPlaylistsRequest_=spotify_.playlists();
        spotify_.nowPlaying();
    }
    qInfo().noquote()<<"spotify: page opened;"<<(spotify_.configured()?(spotify_.signedIn()?"signed in":"signed out"):"not configured");
}

void Shell::openSpotifyList(const QString& kind,const QString& id,const QString& title,const QString& contextUri) {
    if(pages_.size()>=32){showMessage(QObject::tr("The maximum folder depth has been reached."),MessageKind::Page);return;}
    Page page;page.title=title;page.spotify=QStringLiteral("list:")+kind;page.spotifyContext=contextUri;
    pages_.push_back(std::move(page));
    loading_=true;status_=QObject::tr("Reading from Spotify...");
    spotifyRequest_=kind==QLatin1String("liked")?spotify_.likedTracks():spotify_.playlistTracks(id);
}

void Shell::startSpotifyPlay(const Entry& entry) {
    if(pages_.isEmpty())return;
    const auto& page=pages_.last();
    spotifyPendingContext_.clear();spotifyPendingOffset_.clear();spotifyPendingUris_.clear();
    if(!page.spotifyContext.isEmpty()) {
        // A playlist plays as its own context from the chosen track, so Spotify continues through it.
        spotifyPendingContext_=page.spotifyContext;spotifyPendingOffset_=entry.value;
    } else {
        // Liked Songs has no context URI: the queue is the chosen track and the playable ones after it (max 100).
        bool from=false;
        for(const auto& row:page.entries) {
            if(row.id==entry.id)from=true;
            if(from && row.action==Action::SpotifyTrack)spotifyPendingUris_<<row.value;
            if(spotifyPendingUris_.size()>=100)break;
        }
    }
    spotifyPendingTitle_=entry.title;
    loading_=true;status_=QObject::tr("Finding a Spotify device...");
    spotifyDevicesRequest_=spotify_.devices();
}

void Shell::spotifyPlayOn(const QString& deviceId,const QString& deviceName) {
    if(spotifyPendingContext_.isEmpty() && spotifyPendingUris_.isEmpty())return;
    spotifyCommandNote_=spotifyPendingTitle_;
    spotifyCommandDetail_=QObject::tr("Playing on %1").arg(deviceName);
    qInfo().noquote()<<"spotify: play"<<spotifyPendingTitle_<<"on"<<deviceName
        <<(spotifyPendingContext_.isEmpty()?QStringLiteral("(%1 track queue)").arg(spotifyPendingUris_.size()):QStringLiteral("(playlist context)"));
    spotifyCommandRequest_=spotify_.play(deviceId,spotifyPendingContext_,spotifyPendingOffset_,spotifyPendingUris_);
    spotifyPendingContext_.clear();spotifyPendingOffset_.clear();spotifyPendingUris_.clear();
    // The device page, when one was shown, has done its job.
    if(!pages_.isEmpty() && pages_.last().spotify==QLatin1String("devices"))pages_.removeLast();
    emit changed();
}

bool Shell::activateSpotify(const Entry& entry) {
    switch(entry.action) {
    case Action::SpotifyApp:openSpotify();return true;
    case Action::SpotifySignIn:
        notify(QObject::tr("Spotify"),QObject::tr("Finish signing in in the browser."));
        spotify_.signIn();
        return true;
    case Action::SpotifySignOut:
        showConfirm(QObject::tr("Do you want to sign out of Spotify on this PC?"),QString(),[this]{
            spotify_.signOut();spotifyPlaylists_.clear();spotifyNowPlaying_={};
            qInfo().noquote()<<"spotify: signed out; the remembered sign-in was removed";
            emit changed();
        });
        return true;
    case Action::SpotifyNowPlaying:spotify_.nowPlaying();return true;
    case Action::SpotifyLiked:openSpotifyList(QStringLiteral("liked"),{},entry.title,{});return true;
    case Action::SpotifyPlaylist:openSpotifyList(QStringLiteral("playlist"),entry.value,entry.title,entry.setting);return true;
    case Action::SpotifyTrack:startSpotifyPlay(entry);return true;
    case Action::SpotifyDevice:spotifyPlayOn(entry.value,entry.title);return true;
    default:return false;
    }
}

void Shell::spotifyCommand(OptionCommand command) {
    QString note;
    switch(command) {
    case OptionCommand::SpotifyPause:spotifyCommandRequest_=spotify_.pause({});note=QObject::tr("Paused");break;
    case OptionCommand::SpotifyResume:spotifyCommandRequest_=spotify_.play({},{},{},{});note=QObject::tr("Resumed");break;
    case OptionCommand::SpotifyNext:spotifyCommandRequest_=spotify_.next({});note=QObject::tr("Next track");break;
    case OptionCommand::SpotifyPrevious:spotifyCommandRequest_=spotify_.previous({});note=QObject::tr("Previous track");break;
    default:return;
    }
    spotifyCommandNote_=QStringLiteral("Spotify");spotifyCommandDetail_=note;
}

}
