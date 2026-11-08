// Spotify in the Music column (audit/SPOTIFY.md). Pages are ordinary XMB pages of rows; nothing here draws.
//
//   Music > Spotify            not configured: one row whose page says how to set the client id
//                              signed out:     "Sign In with Spotify" (opens the browser), and the redirect URI
//                              signed in:      Now Playing, Liked Songs, the user's playlists, Sign Out
//   a playlist / Liked Songs   its tracks; Cross plays from that track on a Spotify Connect device
//   playlist row Triangle      Play (from the start, shuffle off) / Shuffle Play (random start, shuffle on) / Open
//   Play on                    shown only when several devices are available and none is active
//
// Playback is Spotify's: this app sends the command and Spotify plays on the chosen device. Every refusal
// (Premium required, no device, a playlist the API keeps closed) is shown with Spotify's own reason.
#include "shell.h"
#include "assets.h"
#include "media_controls.h"
#include <QDir>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <functional>
#include <QRandomGenerator>
#include <QRegularExpression>
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
// ---- Icons drawn here (no shipped image files): 256 px squares in the icon box -----------------------------
QImage canvas() { QImage image(256,256,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);return image; }
// Spotify's mark: the green disc with three arcs, for the Music column row and the sign-in rows.
const QImage& spotifyLogo() {
    static const QImage logo=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        const double s=256.;
        p.setPen(Qt::NoPen);p.setBrush(QColor(0x1e,0xd7,0x60));p.drawEllipse(QRectF(s*.08,s*.08,s*.84,s*.84));
        const auto arc=[&](QPointF a,QPointF c,QPointF b,double width){
            QPainterPath path(a*s);path.quadTo(c*s,b*s);
            QPen pen(QColor(0x12,0x12,0x12));pen.setWidthF(width*s);pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawPath(path);
        };
        arc({.27,.39},{.52,.30},{.75,.43},.075);
        arc({.29,.53},{.51,.46},{.715,.56},.06);
        arc({.31,.665},{.50,.615},{.68,.69},.047);
        return image;
    }();
    return logo;
}
// Liked Songs: Spotify's own tile for it, a violet-to-mint square with a white heart.
const QImage& likedTile() {
    static const QImage tile=[]{
        QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient gradient(28,28,228,228);gradient.setColorAt(0,QColor(0x45,0x0a,0xf5));gradient.setColorAt(1,QColor(0xc4,0xef,0xd9));
        p.setPen(Qt::NoPen);p.setBrush(gradient);p.drawRoundedRect(QRectF(28,28,200,200),14,14);
        QPainterPath heart;heart.moveTo(128,184);
        heart.cubicTo(76,150,66,118,80,98);heart.cubicTo(95,78,119,82,128,102);
        heart.cubicTo(137,82,161,78,176,98);heart.cubicTo(190,118,180,150,128,184);
        p.setBrush(Qt::white);p.drawPath(heart);
        return image;
    }();
    return tile;
}
// White line glyphs in the XMB's own manner (a soft shadow under a near-white stroke).
QImage glyph(const std::function<void(QPainter&)>& draw) {
    QImage image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::NoBrush);
    QPen shadow(QColor(0,0,0,90));shadow.setWidthF(14);shadow.setCapStyle(Qt::RoundCap);shadow.setJoinStyle(Qt::RoundJoin);
    p.save();p.translate(3,5);p.setPen(shadow);draw(p);p.restore();
    QPen pen(QColor(250,250,250));pen.setWidthF(12);pen.setCapStyle(Qt::RoundCap);pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);draw(p);
    return image;
}
const QImage& deviceGlyph(const QString& type) {
    static const QImage computer=glyph([](QPainter& p){p.drawRoundedRect(QRectF(48,60,160,108),8,8);p.drawLine(QPointF(128,168),QPointF(128,194));p.drawLine(QPointF(92,196),QPointF(164,196));});
    static const QImage phone=glyph([](QPainter& p){p.drawRoundedRect(QRectF(88,44,80,168),14,14);p.drawLine(QPointF(118,190),QPointF(138,190));});
    static const QImage tv=glyph([](QPainter& p){p.drawRoundedRect(QRectF(36,66,184,112),6,6);p.drawLine(QPointF(100,200),QPointF(156,200));});
    static const QImage speaker=glyph([](QPainter& p){p.drawRoundedRect(QRectF(72,40,112,176),18,18);p.drawEllipse(QPointF(128,150),30,30);p.drawEllipse(QPointF(128,80),10,10);});
    const QString t=type.toLower();
    if(t==QLatin1String("computer"))return computer;
    if(t==QLatin1String("smartphone")||t==QLatin1String("tablet"))return phone;
    if(t==QLatin1String("tv")||t==QLatin1String("castvideo")||t==QLatin1String("stb")||t==QLatin1String("gameconsole"))return tv;
    return speaker;
}
const QImage& signOutGlyph() {
    static const QImage image=glyph([](QPainter& p){
        p.drawPolyline(QPolygonF{{120,56},{60,56},{60,200},{120,200}});
        p.drawLine(QPointF(100,128),QPointF(200,128));
        p.drawPolyline(QPolygonF{{168,96},{200,128},{168,160}});
    });
    return image;
}
Entry withIcon(Entry entry,const QImage& icon) { entry.artwork=icon;entry.iconSized=true;return entry; }

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
    spotify_.setImageCache(QDir(dataDirectory_).filePath("spotify/images"));
    // The Music column's Spotify row wears Spotify's mark instead of a folder.
    for(auto& category:categories_)if(category.id==QLatin1String("music"))
        for(auto& entry:category.entries)if(entry.action==Action::SpotifyApp){entry.artwork=spotifyLogo();entry.iconSized=true;}
    connect(&spotify_,&SpotifyClient::imageReady,this,[this](const QString& url,const QImage& image){
        QImage square;
        if(!image.isNull()) {
            const int side=std::min(image.width(),image.height());
            square=image.copy((image.width()-side)/2,(image.height()-side)/2,side,side)
                       .scaled(192,192,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        if(spotifyArt_.size()>=600){spotifyArt_.clear();spotifyArtRequested_.clear();}
        spotifyArt_.insert(url,square);   // a null image is remembered too, so a failed cover is not refetched
        applySpotifyArt();emit changed();
    });
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
        if(request==spotifyCommandRequest_){spotifyCommandRequest_=0;spotifyAfterPlayShuffle_=-1;}
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
            row.texture=QStringLiteral("music_tex_album_pic");row.artworkUrl=track.image;row.iconSized=true;
            if(!track.playable) {
                row.action=Action::Unavailable;row.disabled=true;
                row.detail=QObject::tr("Not playable on Spotify here (a local file or not available in this market).");
            }
            page.entries.push_back(std::move(row));
        }
        qInfo().noquote()<<"spotify:"<<page.title<<"with"<<tracks.size()<<"track(s)";
        applySpotifyArt();
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
        for(const auto& device:usable) {
            // Some Connect devices report only an id as their name; show what they are instead.
            static const QRegularExpression idLike(QStringLiteral("^[0-9a-f]{24,}$"));
            const QString name=idLike.match(device.name).hasMatch()?QObject::tr("Unnamed %1").arg(device.type.isEmpty()?QObject::tr("device"):device.type.toLower()):device.name;
            page.entries.push_back(withIcon(spotifyRow(QStringLiteral("spotify:device:")+device.id,name,Action::SpotifyDevice,device.id,device.type),deviceGlyph(device.type)));
        }
        pages_.push_back(std::move(page));
        emit changed();
    });
    connect(&spotify_,&SpotifyClient::commandDone,this,[this](quint64 request){
        if(request!=spotifyCommandRequest_)return;
        spotifyCommandRequest_=0;
        if(spotifyAfterPlayShuffle_>=0) {
            // Shuffle goes to the device the play went to, a moment after Spotify accepted the play: sent at
            // once, Spotify answers NO_ACTIVE_DEVICE while the transfer settles (measured on a real account).
            // Its result is not tracked, so a refusal only logs; the music is already playing.
            const bool on=spotifyAfterPlayShuffle_==1;const QString device=spotifyPlayDevice_;
            spotifyAfterPlayShuffle_=-1;
            QTimer::singleShot(1000,this,[this,on,device]{if(spotify_.signedIn())spotify_.shuffle(device,on);});
        }
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
        rows.push_back(withIcon(spotifyRow(QStringLiteral("spotify:setup"),QObject::tr("Set Up Spotify"),Action::Unavailable,{},spotify_.configurationError()),spotifyLogo()));
        return rows;
    }
    if(!spotify_.signedIn()) {
        rows.push_back(withIcon(spotifyRow(QStringLiteral("spotify:signin"),QObject::tr("Sign In with Spotify"),Action::SpotifySignIn,{},
            QObject::tr("Opens Spotify's sign-in in the browser; it returns to %1.").arg(spotify_.redirectUri())),spotifyLogo()));
        return rows;
    }
    // Now Playing wears the playing track's cover once it is fetched, Spotify's mark until then.
    auto now=withIcon(spotifyRow(QStringLiteral("spotify:now"),QObject::tr("Now Playing"),Action::SpotifyNowPlaying,{},nowPlayingText(spotifyNowPlaying_)),spotifyLogo());
    now.artworkUrl=spotifyNowPlaying_.image;
    rows.push_back(std::move(now));
    rows.push_back(withIcon(spotifyRow(QStringLiteral("spotify:liked"),QObject::tr("Liked Songs"),Action::SpotifyLiked,{},QObject::tr("Your saved tracks")),likedTile()));
    for(const auto& playlist:spotifyPlaylists_) {
        auto row=spotifyRow(QStringLiteral("spotify:playlist:")+playlist.id,playlist.name,Action::SpotifyPlaylist,playlist.id,
            QObject::tr("%1 tracks").arg(playlist.tracks)+(playlist.owner.isEmpty()?QString():QStringLiteral(" - ")+playlist.owner));
        row.setting=playlist.uri;
        row.texture=QStringLiteral("tex_playlist");row.container=QStringLiteral("explore_category_music");
        row.artworkUrl=playlist.image;row.iconSized=true;
        rows.push_back(std::move(row));
    }
    QString who=spotify_.displayName();
    if(!spotify_.product().isEmpty())who+=(who.isEmpty()?QString():QStringLiteral(" - "))+spotify_.product();
    rows.push_back(withIcon(spotifyRow(QStringLiteral("spotify:signout"),QObject::tr("Sign Out of Spotify"),Action::SpotifySignOut,{},who),signOutGlyph()));
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
    applySpotifyArt();
}

void Shell::applySpotifyArt() {
    // Covers already fetched go on their rows; the rest are asked for once (at most 300 per page at a time).
    for(auto& page:pages_) {
        if(page.spotify.isEmpty())continue;
        int asked=0;
        for(auto& entry:page.entries) {
            if(entry.artworkUrl.isEmpty())continue;
            const auto found=spotifyArt_.constFind(entry.artworkUrl);
            if(found!=spotifyArt_.cend()){if(!found->isNull())entry.artwork=*found;continue;}
            if(spotifyArtRequested_.contains(entry.artworkUrl) || asked>=300)continue;
            spotifyArtRequested_.insert(entry.artworkUrl);++asked;
            spotify_.fetchImage(entry.artworkUrl);
        }
    }
}

void Shell::openSpotify() {
    if(pages_.size()>=32){showMessage(QObject::tr("The maximum folder depth has been reached."),MessageKind::Page);return;}
    Page page;page.title=QStringLiteral("Spotify");page.spotify=QStringLiteral("root");page.entries=spotifyRootRows();
    pages_.push_back(std::move(page));
    applySpotifyArt();
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
    spotifyPendingPosition_=-1;spotifyPendingShuffle_=-1;
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

void Shell::startSpotifyPlaylist(const Entry& entry,bool shuffle) {
    // Play: the playlist from its first track, shuffle off. Shuffle Play: from a random track, shuffle on.
    const auto playlist=std::find_if(spotifyPlaylists_.begin(),spotifyPlaylists_.end(),[&](const SpotifyPlaylist& p){return p.id==entry.value;});
    if(playlist==spotifyPlaylists_.end() || playlist->uri.isEmpty())return;
    spotifyPendingContext_=playlist->uri;spotifyPendingOffset_.clear();spotifyPendingUris_.clear();
    spotifyPendingPosition_=shuffle && playlist->tracks>1?int(QRandomGenerator::global()->bounded(playlist->tracks)):0;
    spotifyPendingShuffle_=shuffle?1:0;
    spotifyPendingTitle_=playlist->name;
    loading_=true;status_=QObject::tr("Finding a Spotify device...");
    spotifyDevicesRequest_=spotify_.devices();
}

void Shell::spotifyPlayOn(const QString& deviceId,const QString& deviceName) {
    if(spotifyPendingContext_.isEmpty() && spotifyPendingUris_.isEmpty())return;
    spotifyCommandNote_=spotifyPendingTitle_;
    spotifyCommandDetail_=QObject::tr("Playing on %1").arg(deviceName);
    qInfo().noquote()<<"spotify: play"<<spotifyPendingTitle_<<"on"<<deviceName
        <<(spotifyPendingContext_.isEmpty()?QStringLiteral("(%1 track queue)").arg(spotifyPendingUris_.size()):QStringLiteral("(playlist context)"));
    spotifyCommandRequest_=spotify_.play(deviceId,spotifyPendingContext_,spotifyPendingOffset_,spotifyPendingUris_,spotifyPendingPosition_);
    spotifyAfterPlayShuffle_=spotifyPendingShuffle_;spotifyPlayDevice_=deviceId;
    spotifyPendingContext_.clear();spotifyPendingOffset_.clear();spotifyPendingUris_.clear();
    spotifyPendingPosition_=-1;spotifyPendingShuffle_=-1;
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
