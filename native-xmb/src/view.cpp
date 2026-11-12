#include "view.h"
#include "live_overlay.h"
#include "gallery_view.h"
#include "app_editor.h"
#include "artwork_settings.h"
#include "host.h"
#include "pad.h"
#include "settings_menu.h"
#include "musicvis.h"
#include "raf_render.h"
#include <QPainter>
#include <QKeyEvent>
#include <QAbstractNativeEventFilter>
#include <QDateTime>
#include <QFileDialog>
#include <QLoggingCategory>
#include <QVector3D>
#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLPaintDevice>
#include <QTextStream>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTime>
#include <cmath>
#include <numbers>
#include <QSet>
#include <QScopedValueRollback>
#include <QWindow>
#include "idle_profile.h"
#include "uniform_cache.h"
#include "design_space.h"
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#endif

namespace xmb {
namespace {
using ProfScope=idleprofile::Scope;
const bool& g_profOn=idleprofile::enabled;
#ifdef Q_OS_WIN
// The window manager's own answer to "who is in front"; Qt's activeWindow follows the same foreground
// window but lags a focus change by an event hop, and the log wants the pre-raise owner precisely.
QString windowProcessName(HWND window) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(!pid)return {};
    const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return {};
    wchar_t path[1024]{};DWORD length=DWORD(std::size(path));
    QString name;
    if(QueryFullProcessImageNameW(process,0,path,&length))name=QFileInfo(QString::fromWCharArray(path,length)).fileName();
    CloseHandle(process);
    return name;
}
QString foregroundDescription() {
    const HWND window=GetForegroundWindow();
    if(!window)return QStringLiteral("none");
    wchar_t title[256]{};GetWindowTextW(window,title,int(std::size(title)));
    return QStringLiteral("process=\"%1\" title=\"%2\"")
        .arg(windowProcessName(window),QString::fromWCharArray(title));
}
#else
QString foregroundDescription() { return QStringLiteral("unsupported"); }
#endif
#ifdef Q_OS_WIN
// Windows delivers one media volume key press in two forms that can both reach this window: WM_APPCOMMAND
// (APPCOMMAND_VOLUME_UP/DOWN/MUTE, which DefWindowProc would hand to the shell) and, on most keyboards, a
// WM_KEYDOWN/WM_SYSKEYDOWN with VK_VOLUME_UP/DOWN/MUTE. Both are taken here so the host applies exactly one
// 5% step (or one mute toggle) and the HUD cannot disagree with the endpoint. A volume key is not a key the
// XMB uses, so swallowing it takes nothing from navigation; when the apply call declines the key (another
// surface owns input), the message is left alone and Windows behaves exactly as it would without this app.
class VolumeKeyFilter final:public QAbstractNativeEventFilter {
public:
    explicit VolumeKeyFilter(std::function<bool(int,const char*)> apply):apply_(std::move(apply)){clock_.start();}
    bool nativeEventFilter(const QByteArray& type,void* message,qintptr* result) override {
        if(type!="windows_generic_MSG" && type!="windows_dispatcher_MSG")return false;
        auto* msg=static_cast<MSG*>(message);
        int action=0;const char* source="";
        if(msg->message==WM_APPCOMMAND) {
            switch(GET_APPCOMMAND_LPARAM(msg->lParam)) {
            case APPCOMMAND_VOLUME_UP:action=1;source="appcommand up";break;
            case APPCOMMAND_VOLUME_DOWN:action=2;source="appcommand down";break;
            case APPCOMMAND_VOLUME_MUTE:action=3;source="appcommand mute";break;
            default:return false;
            }
        } else if(msg->message==WM_KEYDOWN || msg->message==WM_SYSKEYDOWN) {
            switch(msg->wParam) {
            case VK_VOLUME_UP:action=1;source="vk_volume up";break;
            case VK_VOLUME_DOWN:action=2;source="vk_volume down";break;
            case VK_VOLUME_MUTE:action=3;source="vk_volume mute";break;
            default:return false;
            }
        } else return false;
        // The two forms of one press arrive together: the second within 100 ms is the same press and is
        // swallowed without a second step. Two separate presses 100 ms apart are faster than a human.
        const qint64 now=clock_.elapsed();
        if(action==lastAction_ && now-lastAt_<100) {
            if(result)*result=0;
            if(qEnvironmentVariableIsSet("XMB_VOLUME_LOG"))qInfo().noquote()<<QString("volume: trace %1 duplicate of a press %2 ms ago (swallowed)").arg(source).arg(now-lastAt_);
            return true;
        }
        if(!apply_(action,source))return false; // declined: the host keeps its own handling of this key
        lastAction_=action;lastAt_=now;
        if(result)*result=0;
        return true;
    }
private:
    std::function<bool(int,const char*)> apply_;
    QElapsedTimer clock_;
    int lastAction_=0;
    qint64 lastAt_=-1000;
};
#endif
// Opt-in trace for the volume path; the per-change read-back line is logged unconditionally.
bool volumeTrace() { static const bool on=qEnvironmentVariableIsSet("XMB_VOLUME_LOG");return on; }
}
View::View(Shell& shell,Assets& assets,QWidget* parent):QOpenGLWidget(parent),shell_(shell),assets_(assets) {
    if(qEnvironmentVariable("XMB_INGAME_LIVE")==QStringLiteral("1")) {
        auto surface=format();surface.setAlphaBufferSize(8);setFormat(surface);
    }
    idleprofile::startSampler();
    setFocusPolicy(Qt::StrongFocus);setMinimumSize(800,450);
    setMouseTracking(true);qApp->installEventFilter(this);
    // Decode and cache frequently used original assets before animation starts.
    for(const auto& category:shell_.categories()) {
        assets_.image(category.container,category.texture);
        for(const auto& entry:category.entries)assets_.image(entry.container,entry.texture);
    }
    artworkCredit_ = new QLabel("<a style='color:white' href='https://rawg.io/'>Artwork from RAWG</a>", this);
    artworkCredit_->setOpenExternalLinks(true);
    artworkCredit_->setFocusPolicy(Qt::NoFocus);
    artworkCredit_->setStyleSheet("background-color:rgba(0,0,0,110); padding:3px 6px;");
    artworkCredit_->adjustSize();
    syncColumns();
    // The keyboard layout is resolved once, from the firmware, where the assets are already open: one RCO
    // through the P1.1 reader and one decrypted module (audit/OSK-FIRMWARE.md 12). A failed or absent firmware
    // read falls back to the committed table and logs which source it used, so the keyboard always has a
    // layout and the provenance is never guessed.
    oskLayout_=loadOskLayout(assets_,oskObjects_);
    monthImage_=assets_.image("qgl/lines",QString("textures/month_bg/rgb/%1.dds").arg(QDateTime::currentDateTime().date().month(),2,10,QChar('0')));
    connect(&shell_,&Shell::changed,this,[this]{syncInGameWindow();++uiRevision_;syncColumns();loadWallpaper();setColumnSink(shell_.columnSink());update();});
    connect(&shell_,&Shell::mediaFrame,this,[this](const QImage& frame){
        mediaFrame_=frame;
        if(frame.isNull())photoViewport_={};
        else if(shell_.photoOpen())photoViewport_.reset(frame.size(),{1920,1080},shell_.photoFill());
        update();
    });
    connect(&shell_,&Shell::requestPhotoGallery,this,[this]{openGallery();});
    connect(&shell_,&Shell::requestLibrary,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Choose media folder");shell_.noteActivity();if(!path.isEmpty())shell_.chooseLibrary(path);});
    connect(&shell_,&Shell::requestWallpaper,this,[this]{const auto file=QFileDialog::getOpenFileName(this,"Choose Wallpaper",{},"Images (*.png *.jpg *.jpeg *.bmp *.webp)");shell_.noteActivity();if(!file.isEmpty())shell_.chooseWallpaper(file);});
    connect(&shell_,&Shell::requestTheme,this,[this]{const auto file=QFileDialog::getOpenFileName(this,"Choose Theme",{},"PS3 Themes (*.p3t)");shell_.noteActivity();if(!file.isEmpty())shell_.chooseTheme(file);});
    connect(&shell_,&Shell::requestImportApps,this,[this]{AppEditor editor(shell_.appRegistryPath(),this);connect(&editor,&AppEditor::registryChanged,&shell_,&Shell::refreshContent);editor.exec();shell_.noteActivity();});
    connect(&shell_, &Shell::requestArtworkSettings, this, [this] {
        if (configureArtwork(this, shell_.artworkStatus())) shell_.retryArtwork();
        shell_.noteActivity();
    });
    connect(&shell_,&Shell::requestSystemName,this,[this](const QString& value){openOsk("System Name",value,[this](const QString& name){shell_.setSystemName(name);});});
    // Create New User opens user_plugin's page_create first (prompt, prefilled field, OK; drawUserCreatePage), not
    // the keyboard: the keyboard is what Cross on the field opens.
    connect(&shell_,&Shell::requestNewUser,this,[this]{
        userCreate_=UserCreatePage{};userCreate_.open=true;
        // "User 1" with one user on the console (page-user-create.png). The numbering rule is INFERRED: the
        // existing user count, the one data point the frame gives.
        userCreate_.name=QStringLiteral("User %1").arg(std::max(1,shell_.userCount()));
        update();
    });
    // Create New Playlist (playlist_plugin): the keyboard titled with the plugin's own prompt; the name goes to
    // Shell::createPlaylist, which raises the plugin's duplicate-name and limit errors itself.
    connect(&shell_,&Shell::requestPlaylistName,this,[this](QString type){
        openOsk(assets_.text("playlist_plugin","msg_input_playlist_name","Enter a name for the playlist."),QString(),
            [this,type](const QString& name){if(!name.trimmed().isEmpty())shell_.createPlaylist(type,name);});
    });
    // Edit on a playlist row or one of its items opens the two-pane editor (drawPlaylistEditor).
    connect(&shell_,&Shell::requestPlaylistEdit,this,[this](QString id){openPlaylistEditor(id);});
    // Information on a user row renames it on the same page (Shell::requestRenameUser), prefilled with the name.
    connect(&shell_,&Shell::requestRenameUser,this,[this](QString id,QString name){
        userCreate_=UserCreatePage{};userCreate_.open=true;userCreate_.name=name;userCreate_.renameId=id;
        update();
    });
    // The Jellyfin sign-in page's fields (audit/JELLYFIN-NATIVE.md): Cross on one of the page's field rows asks
    // for the XMB keyboard. The password field is the masked case - the keyboard composes dots and its cached
    // UI signature carries the dot count, never the characters - so no credential ever reaches this view's
    // signature string, the frame's pixels, or the capture sidecar. There is no dialog here any more: the
    // sign-in page itself is a normal XMB page, which is what keeps pollController live while it is open.
    connect(&shell_,&Shell::requestJellyfinFieldEdit,this,[this](QString field,QString title,QString initial,bool masked){
        openOsk(title,initial,[this,field](const QString& value){shell_.setJellyfinSignInField(field,value);},masked);
        // Service values are not display names: preserve exact credentials and deliver an empty edit.
        osk_.preserveWhitespace=true;osk_.allowEmpty=true;
        osk_.maxLength=field=="server"?2048:field=="username"?256:4096;
    });
    loadWallpaper();
    clock_.start();timer_.setTimerType(Qt::PreciseTimer);timer_.setInterval(16);
    // The PC's master volume at start-up: logged once (an isolated data directory does not isolate the
    // Windows audio endpoint, so a scripted run has to put this back) and kept for VOLRESTORE.
    volumeLevel_=host::masterVolume();
    volumeMuted_=host::masterMuted();
    volumeStart_=volumeLevel_;volumeStartMuted_=volumeMuted_;
    qInfo().noquote()<<QString("volume: start %1% muted %2 on the default playback endpoint")
        .arg(volumeLevel_<0?QStringLiteral("unavailable"):QString::number(volumeLevel_)).arg(volumeMuted_?1:0);
#ifdef Q_OS_WIN
    // Media keys: WM_APPCOMMAND and VK_VOLUME_* are taken before Qt or DefWindowProc sees them.
    volumeKeys_=std::make_unique<VolumeKeyFilter>([this](int action,const char* source){return volumeKeyAction(action,source);});
    qApp->installNativeEventFilter(volumeKeys_.get());
    if(volumeTrace())qInfo()<<"volume: trace media-key filter installed (WM_APPCOMMAND, VK_VOLUME_*)";
    // Remote and transport keys (PHASES H10, audit/INPUT.md): WM_APPCOMMAND and VK_MEDIA_*/VK_BROWSER_* are
    // taken here, before Qt turns them into key events or DefWindowProc forwards them to the shell.
    remoteKeys_=std::make_unique<MediaKeyFilter>([this](RemoteKey key,const QString& source){return remoteKeyAction(key,source);});
    qApp->installNativeEventFilter(remoteKeys_.get());
#endif
    // Input polling is independent of presentation. A 16 ms repaint timer produced ~62.5 Hz even on a 239 Hz
    // display. Request the next frame after Qt finishes composition/swap; swapInterval=1 remains in effect.
    connect(&timer_,&QTimer::timeout,this,[this]{
        pollController();
        // An opacity-zero Qt owner may stop producing frameSwapped callbacks. The live
        // surface needs its own bounded cadence for animations/input even then.
        if(liveOverlay_ && !renderPaused())update();
    });timer_.start();pad::start();
    // The PS button is watched globally: its own 20 ms HID poll keeps working while a game owns the
    // foreground, unlike pollController which is limited to the active XMB window.
    connect(&psWatcher_,&PsWatcher::pressed,this,&View::psPressed);
    connect(&psWatcher_,&PsWatcher::held,this,&View::psHeldEvent);
    connect(&psWatcher_,&PsWatcher::tapped,this,&View::psTappedEvent);
    connect(&psWatcher_,&PsWatcher::released,this,&View::psReleasedEvent);
    connect(&psWatcher_,&PsWatcher::cancelled,this,[this]{psPressActionable_=false;});
    psWatcher_.start();
    // Phone Remote (D11): the paired LAN remote drives the same key path a scripted or keyboard press takes, so
    // its input behaves identically. The PS button goes through the tap handler.
    connect(&shell_.phoneRemote(),&PhoneRemote::keyRequested,this,&View::replayKeyTap);
    connect(&shell_.phoneRemote(),&PhoneRemote::psRequested,this,[this]{psTappedEvent(120);});
    // Text from the phone lands in the on-screen keyboard while it is open (search, name, sign-in fields); with
    // no keyboard open there is nowhere for it to go, so it is dropped.
    connect(&shell_.phoneRemote(),&PhoneRemote::textRequested,this,[this](const QString& text){if(osk_.open)oskInsert(text);});
    // renderPaused() (ps_watcher.h, H5): while a launched title owns the foreground the loop stops re-arming;
    // TitleSession calls update() to restart it when the XMB is back in front.
    connect(this,&QOpenGLWidget::frameSwapped,this,[this]{++framesSwapped_;if(isVisible() && !renderPaused() && !liveOverlay_)update();},Qt::QueuedConnection);
    frameTracePath_=qEnvironmentVariable("XMB_FRAME_TRACE");
    if(!frameTracePath_.isEmpty()) {
        frameTrace_.reserve(20000);
        connect(this,&QOpenGLWidget::frameSwapped,this,[this]{
            if(frameTrace_.size()<20000)frameTrace_.push_back({clock_.nsecsElapsed(),0,true,false,false});
        });
    }
}
void View::saveFrameTrace() {
    if(frameTracePath_.isEmpty())return;
    QFile file(frameTracePath_);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){qWarning()<<"frame trace:"<<file.errorString();return;}
    QTextStream out(&file);out<<"# Native paint/swap callbacks; CPU dispatch time, not physical presentation or GPU completion. Maximum 20000 samples.\n";
    for(const auto& sample:frameTrace_) {
        if(sample.swap)out<<"frame swap: "<<sample.at<<'\n';
        else out<<"frame paint: "<<sample.at<<' '<<sample.cpu<<' '<<int(sample.rebuilt)<<' '<<int(sample.moving)<<'\n';
    }
}
void View::PafEase::retarget(double target,double milliseconds,int mode,double delayMilliseconds) {
    if(!started_){value=from_=to_=target;started_=true;return;}
    if(target==to_ && (running_ || value==target))return;
    // FUN_003981ec: the duration snaps to whole 60 Hz frames; k by frame count (DAT_0068eb04.. and the 1/(2.2+(n-5)/3) tail).
    const int frames=std::max(1,int(milliseconds*.001*60));
    if(mode==4) { // a 14-entry table, then 1/(3.775+0.217(n-15)); each step grows by 1.0001
        static constexpr double tableK[14]={.995,.905,.7905,.6873,.6042,.5376,.4843,.4396,.4016,.3704,.3431,.3195,.2990,.2809};
        k_=frames<=14?tableK[frames-1]:1./(3.775+.217*(frames-15));growth_=1.0001;
    } else {
        static constexpr double shortK[4]={.9615385,.78125,.6369426,.5291005};
        k_=frames<=4?shortK[frames-1]:1./((frames-5)*.333333+2.2);growth_=1.000001;
    }
    // paf has no start delay; the options panel's entrance is measured with one (UI-ELEMENT O6), so the ease
    // holds its start value for delayMilliseconds and then runs the same 60 Hz curve. 0 everywhere else.
    // elapsed_ is in seconds, so the delay is converted here once.
    delay_=std::max(0.,delayMilliseconds)*.001;
    from_=value;to_=target;progress_=0;elapsed_=0;running_=true;
}
void View::PafEase::advance(double seconds) {
    if(!running_)return;
    // FUN_00398748 modes 4/5 -> FUN_00398540: each canonical frame steps v = ((1-v) k + v) x growth from the stored
    // progress (FUN_003984f8), ignoring the elapsed-time argument. The animation ends only when v >= 1.0 (the float
    // test at 0x3988f0): there is no landing at the duration, which only chose k. A 200 ms mode-5 move is 95% done at
    // its 12th frame and reaches 1 at about frame 50; its last pixels decay instead of snapping (PAGE-MOVE, 2026-11-05).
    const auto step=[&](double v){return std::min(1.,((1.-v)*k_+v)*growth_);};
    // Sample the canonical 60 Hz curve at total elapsed time. Mutating the state with each fractional host tick
    // changes the curve at 120/240 Hz. Interpolation preserves its integer-frame values while allowing smooth
    // host refresh rates; retarget-from-current-value is retained.
    elapsed_+=std::max(0.,seconds);
    const double active=elapsed_-delay_;   // negative while the measured start offset is still running
    if(active<=0.){value=from_;return;}
    const double frames=active*60;const int whole=int(frames);const double fraction=frames-whole;
    progress_=0;
    for(int i=0;i<whole && progress_<1.;++i)progress_=step(progress_);
    if(progress_>=1.){progress_=1;running_=false;}
    else progress_+=(step(progress_)-progress_)*fraction;
    value=from_+(to_-from_)*progress_;
}
void View::resizeEvent(QResizeEvent* event) {
    QOpenGLWidget::resizeEvent(event);
    loadWallpaper();
    if (artworkCredit_) artworkCredit_->move(12, height() - artworkCredit_->height() - 12);
}
void View::syncColumns() {
    const auto& currentRows = shell_.entries();
    artworkCredit_->setVisible(!shell_.mediaOpen() && std::any_of(currentRows.cbegin(), currentRows.cend(),
                                [](const Entry& row) { return row.rawgArtworkVisible; }));
    const int depth=shell_.depth();
    if(previousCategory_!=shell_.category()) {
        outgoing_=columns_;outgoingCategory_=previousCategory_;categoryFade_=outgoing_.isEmpty()?0.:1.;columns_.clear();
        fadeEase_=PafEase();fadeEase_.retarget(categoryFade_);fadeEase_.retarget(0,moveMilliseconds); // the old column cross-fades out
        previousCategory_=shell_.category();
    }
    // Keep the departed child until the return animation finishes. The shell's
    // actual page stack and selections change immediately and remain canonical.
    const QString entryId=shell_.pageTitle()+QLatin1Char('/')+shell_.entries().value(shell_.selection()).id;
    if(const qint64 key=(qint64(shell_.category())*64+depth)*65536+shell_.selectionAt(depth);key!=focusKey_ || entryId!=focusedEntryId_ || !focusClock_.isValid()) {
        focusKey_=key;focusedEntryId_=entryId;focusClock_.restart();
        QString path;
        // A row move inside a Jellyfin page whose backdrop is already up leaves it in place.
        const bool keepPage=focusedBackgroundIsPage_ && focusedBackgroundKey(path)==focusedBackgroundPath_;
        if(!keepPage) {
            if(!focusedBackground_.isNull() && focusedBackgroundOpacity_>0.) {
                departingBackground_=focusedBackground_;departingBackgroundOpacity_=focusedBackgroundOpacity_;
            }
            focusedBackground_={};focusedBackgroundOpacity_=0.;focusedBackgroundPath_.clear();++focusedBackgroundRequest_;
            focusedBackgroundIsPage_=false;
        }
    }
    for(int i=0;i<=depth;++i) {
        if(i>=columns_.size())columns_.push_back(Column{shell_.entriesAt(i),shell_.selectionAt(i),double(shell_.selectionAt(i))});
        else {
            const auto& entries=shell_.entriesAt(i);
            const bool replaced=columns_[i].entries.size()!=entries.size() ||
                columns_[i].entries.value(columns_[i].selection).id!=entries.value(shell_.selectionAt(i)).id;
            columns_[i].entries=entries;
            columns_[i].selection=std::clamp(shell_.selectionAt(i),0,std::max(0,int(entries.size())-1));
            // A list replacement can invalidate the animated position. Ordinary
            // moves keep their easing, including a retained departing page.
            if(replaced && (columns_[i].position<0. || columns_[i].position>std::max(0,int(entries.size())-1))) {
                columns_[i].position=columns_[i].selection;columns_[i].ease=PafEase();
            }
        }
    }
    if(depth>previousDepth_)for(int i=previousDepth_+1;i<=depth;++i){columns_[i].position=columns_[i].selection;columns_[i].ease=PafEase();}
    previousDepth_=depth;
    syncFocusedBackground();
}
// The focused art's identity: the row's own background file when the shell has one (game PIC1, Steam hero,
// RAWG art), else the open Jellyfin page's backdrop, which the shell already holds decoded. Empty = none.
QString View::focusedBackgroundKey(QString& path) {
    path=shell_.focusedBackgroundPath();
    if(!path.isEmpty())return path;
    const QImage& page=shell_.pageBackdrop();
    return page.isNull()?QString{}:QStringLiteral("jellyfin-page:%1").arg(page.cacheKey());
}
void View::syncFocusedBackground() {
    QString path;
    const QString key=focusedBackgroundKey(path);
    if(key==focusedBackgroundPath_)return;
    focusedBackgroundPath_=key;const auto request=++focusedBackgroundRequest_;
    if(key.isEmpty() || focusedBackgroundIsPage_) {
        if(!focusedBackground_.isNull() && focusedBackgroundOpacity_>0.) {
            departingBackground_=focusedBackground_;departingBackgroundOpacity_=focusedBackgroundOpacity_;
        }
        focusedBackground_={};focusedBackgroundOpacity_=0.;focusedBackgroundIsPage_=false;
        if(key.isEmpty())return;
    }
    if(path.isEmpty()) {
        // Jellyfin page backdrop: no decode, and its dwell starts now (leaving the page or starting playback
        // nulls pageBackdrop(), which lands in the branch above and fades it out as departing art).
        focusedBackground_=shell_.pageBackdrop();focusedBackgroundIsPage_=true;pageBackdropClock_.restart();
        ++uiRevision_;update();return;
    }
    auto* watcher=new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,request]{
        watcher->deleteLater();if(request!=focusedBackgroundRequest_)return;
        focusedBackground_=watcher->result();++uiRevision_;update();
    });
    watcher->setFuture(QtConcurrent::run([path]{
        QImageReader reader(path);reader.setDecideFormatFromContent(true);reader.setAutoTransform(true);
        const QSize size=reader.size();
        if(!size.isValid() || size.width()>16384 || size.height()>16384 || quint64(size.width())*quint64(size.height())>64ULL*1024*1024)return QImage{};
        if(size.width()>3840 || size.height()>2160)reader.setScaledSize(size.scaled(3840,2160,Qt::KeepAspectRatio));
        return reader.read();
    }));
}
void View::loadWallpaper() {
    if(shell_.backgroundMode()!=2)return;
    // A theme scene is a static frame for now; on failure retain its still picture like any other theme.
    const auto& scene=shell_.themeSceneBytes();
    if(!scene.isEmpty() || !shell_.themeBackground().isNull()) {
        const QSize pixels=(QSizeF(size())*devicePixelRatioF()).toSize();
        if(!scene.isEmpty() && (pixels.width()<=0 || pixels.height()<=0))return;
        const auto key=QStringLiteral("p3t:")+shell_.themePath()+(scene.isEmpty()?QString{}:QStringLiteral(":%1x%2").arg(pixels.width()).arg(pixels.height()));
        if(key==wallpaperPath_)return;
        wallpaperPath_=key;++wallpaperRequest_;wallpaperImage_=shell_.themeBackground();
        if(!scene.isEmpty()) {
            auto* previous=QOpenGLContext::currentContext();
            auto* surface=previous?previous->surface():nullptr;
            QString error;const auto rendered=renderRafStill(scene,QString{},pixels.width(),pixels.height(),&error);
            if(previous && surface)previous->makeCurrent(surface);
            if(rendered.isNull())qInfo().noquote()<<"theme: scene render failed; using still background:"<<error;
            else wallpaperImage_=rendered;
        }
        return;
    }
    const auto path=shell_.wallpaper();if(path==wallpaperPath_)return;
    wallpaperPath_=path;wallpaperImage_={};const auto request=++wallpaperRequest_;
    if(path.isEmpty())return;
    auto* watcher=new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,request]{
        watcher->deleteLater();if(request!=wallpaperRequest_)return;
        wallpaperImage_=watcher->result();if(wallpaperImage_.isNull())qWarning()<<"Wallpaper could not be decoded";update();
    });
    watcher->setFuture(QtConcurrent::run([path]{
        QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();
        if(!size.isValid() || size.width()>65536 || size.height()>65536)return QImage{};
        if(size.width()>3840 || size.height()>2160)reader.setScaledSize(size.scaled(3840,2160,Qt::KeepAspectRatio));
        return reader.read();
    }));
}
void View::drawBackdrops(QPainter& p) {
    const QRectF window=p.transform().inverted().mapRect(QRectF(0,0,p.device()->width(),p.device()->height())).united(QRectF(0,0,1920,1080));
    const auto draw=[&](const QImage& image,double opacity) {
        if(image.isNull() || opacity<=0.)return;
        const QSizeF size=QSizeF(image.size()).scaled(window.size(),Qt::KeepAspectRatioByExpanding);
        p.save();p.setOpacity(opacity);p.drawImage(QRectF(window.center()-QPointF(size.width()/2,size.height()/2),size),image);p.restore();
    };
    if(shell_.backgroundMode()==2 && !wallpaperImage_.isNull()) {
        draw(wallpaperImage_,1.);p.fillRect(window,QColor(0,0,0,35));
    }
    draw(departingBackground_,departingBackgroundOpacity_);
    draw(focusedBackground_,focusedBackgroundOpacity_);
    // A Jellyfin page backdrop is a film still behind readable rows, so it is veiled (black 0.4, following its fade), as
    // Jellyfin's own clients dim theirs. Game hero art (PIC1, Steam heroes) is drawn as the console draws it.
    if(focusedBackgroundIsPage_ && focusedBackgroundOpacity_>0.)p.fillRect(window,QColor(0,0,0,int(255*.4*focusedBackgroundOpacity_)));
    // The item menu's plate is a light veil (measured 0.21-0.25 white over a dark console wallpaper). Over bright
    // game art or a bright user wallpaper the same veil leaves its white text at ~1.2:1, so the art under the
    // panel is darkened while the menu is open: black up to 0.58, ramping in over 80 px left of the plate
    // (x 1240..1320) and following the panel's own open ease. INFERRED: the console has no bright-art frame of
    // this menu; 0.58 brings a 150-200 luma backdrop to the console's measured ~100 ground under the rows.
    if(optionsDrawn() && !optionsShown_.player) {
        const double amount=.58*std::clamp(optionsOpenEase_.value,0.,1.);
        QLinearGradient shade(1240,0,1320,0);
        shade.setColorAt(0,QColor(0,0,0,0));shade.setColorAt(1,QColor(0,0,0,int(255*amount)));
        p.fillRect(QRectF(1240,window.top(),window.right()-1240,window.height()),shade);
    }
}
View::~View(){
    stopLiveOverlay();
#ifdef Q_OS_WIN
    if(volumeKeys_ && qApp)qApp->removeNativeEventFilter(volumeKeys_.get());
#endif
    saveFrameTrace();if(context()){makeCurrent();liveOverlayResolved_.reset();gallery_.reset();textRenderer_.release(*this);monthTexture_.reset();inGameTexture_.reset();inGameProgram_.removeAllShaders();if(vao_)glDeleteVertexArrays(1,&vao_);background_.removeAllShaders();doneCurrent();}}
void View::initializeGL() {
    uniformcache::invalidate();
    if(!initializeOpenGLFunctions()){qCritical()<<"Native renderer requires OpenGL 3.3";return;}
    textRenderer_.init(*this,QDir(assets_.root()).filePath("data/font"));
    static const char* vertex=R"(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}
)";
    static const char* fragment=R"(#version 330 core
in vec2 uv;out vec4 colour;uniform float seconds;uniform vec3 tint;uniform vec2 resolution;
uniform sampler2D monthPalette;uniform bool useMonthPalette;uniform bool classic;
uniform bool overlay;  // true: draw only the ribbon and particles, additively, over the original background
uniform float dayLight; // 0 night .. 1 day; clock-driven curve measured from reference video (audit/VIDEO-REFERENCES.md)
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
// Ribbon crest as a fraction of height from the top, fitted to the reference: it rests near 0.42,
// a swell crosses the screen left to right in ~8 s once every ~22.5 s and pushes the crest to ~0.62.
float swell(float x,float s){return exp(-pow((x-(-.9+2.8*fract(s/22.5)))/.42,2.0));}
float crestFromTop(float x,float s){return .42+.20*swell(x,s)-.05*swell(x+.55,s)+.018*sin(x*4.0-s*.12)+.012*sin(x*7.0+s*.07);}
uniform bool originalRibbon;     // the firmware ribbon (ribbon.cpp) is drawn: skip ours, particles follow its crest
uniform float ribbonCrest[33];   // its crest, fraction from the top, left to right
float wave(float x,float s){ // uv.y runs bottom to top
    if(originalRibbon){float k=clamp(x,0.,1.)*32.;int i=min(int(k),31);return 1.0-mix(ribbonCrest[i],ribbonCrest[i+1],k-float(i));}
    return 1.0-crestFromTop(x,s);
}
void main(){
    vec2 p=uv;float t=seconds*.12;float fromTop=1.0-p.y;
    // Day: brightest at the top, darkest about halfway down, slightly lighter at the bottom.
    // Night: the upper half falls to black and the remaining colour sits low (about a quarter of day).
    float dayProfile=1.0-.28*smoothstep(0.0,.55,fromTop)+.08*smoothstep(.55,1.0,fromTop);
    vec3 c=useMonthPalette?texture(monthPalette,p).rgb:tint*.62*dayProfile;
    float nightProfile=.25*smoothstep(.42,.95,fromTop);
    c*=mix(nightProfile,1.0,dayLight);
    c+=tint*.045*dayLight*exp(-length((p-vec2(.42,.28))*vec2(.7,1.6))*2.0);
    if(overlay)c=vec3(0.);
    float ribbonLight=mix(.5,1.0,dayLight);
    if(!originalRibbon)for(int i=0;i<7;++i){
        float k=float(i);float line=wave(p.x,seconds)+.019*sin(p.x*4.8+t+k*.50)+k*.0035;
        float d=p.y-line;float w=.010+k*.0016;
        float ribbon=exp(-d*d/(w*w))*.075+exp(-d*d/.008)*.009;
        c+=mix(tint,vec3(.92),mix(.55,.82,dayLight))*ribbon*ribbonLight; // night keeps the theme hue
    }
    vec2 grid=vec2(42.0,24.0);vec2 cell=floor(p*grid);
    if(!classic)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 id=cell+vec2(x,y);float seed=hash(id);vec2 centre=(id+vec2(seed,hash(id+51.7)))/grid;
        centre.y+=.004*sin(t*1.8+seed*40.0);
        float weight=exp(-pow((centre.y-wave(centre.x,seconds))/.16,2.0))*(1.0-.7*centre.x)*ribbonLight;
        vec2 delta=(p-centre)*resolution;float radius=seed>.98?3.3:.50+seed*.65;
        c+=vec3(.90,.84,.66)*exp(-dot(delta,delta)/(radius*radius))*weight*(.20+.60*seed);
    }
    float grain=fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5;
    c+=grain/650.0;colour=vec4(c,1.0);
})";
    glReady_=background_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)&&background_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)&&background_.link();
    if(!glReady_)qCritical().noquote()<<background_.log();
    glGenVertexArrays(1,&vao_);
    initOriginalBackground();
    ribbon_.init(*this,assets_);
    hdr_.init(*this,assets_);
    icons_.init(*this,assets_);
    static const char* compositeVertex=R"(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);})";
    static const char* compositeFragment=R"(#version 330 core
in vec2 uv;out vec4 colour;uniform sampler2D layer;uniform float dimOpacity;uniform float boost;uniform float rgbScale;
void main(){colour=boost>0.0?texture(layer,uv)*boost:(dimOpacity>0.0?vec4(0,0,0,dimOpacity):texture(layer,uv));colour.rgb*=rgbScale;})"; // premultiplied; linear filtering at half scale = 2x2 box filter
    if(!uiComposite_.addShaderFromSourceCode(QOpenGLShader::Vertex,compositeVertex) || !uiComposite_.addShaderFromSourceCode(QOpenGLShader::Fragment,compositeFragment) || !uiComposite_.link())
        qCritical().noquote()<<"UI composite shader failed:"<<uiComposite_.log();
    else {uiComposite_.bind();uiComposite_.setUniformValue("rgbScale",1.f);uiComposite_.release();} // a GLSL uniform starts at 0
    static const char* inGameFragment=R"(#version 330 core
in vec2 uv;out vec4 colour;uniform sampler2D frame;uniform float brightness;uniform vec2 uvScale;
void main(){colour=vec4(texture(frame,(uv-.5)*uvScale+.5).rgb*brightness,1.);})";
    if(!inGameProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex,compositeVertex) ||
       !inGameProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment,inGameFragment) || !inGameProgram_.link())
        qCritical().noquote()<<"In-game backdrop shader failed:"<<inGameProgram_.log();
    particles_.init(*this,assets_);
    if(introEnabled_)coldboot_.init(*this,assets_);
    if(!monthImage_.isNull()) {
#if QT_VERSION >= QT_VERSION_CHECK(6,9,0)
        monthTexture_=std::make_unique<QOpenGLTexture>(monthImage_.flipped(Qt::Vertical));
#else
        monthTexture_=std::make_unique<QOpenGLTexture>(monthImage_.mirrored(false,true));
#endif
        monthTexture_->setMinificationFilter(QOpenGLTexture::Linear);monthTexture_->setMagnificationFilter(QOpenGLTexture::Linear);
        monthTexture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    const QString renderer=QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    qInfo()<<"native renderer:"<<renderer;shell_.setRenderer(renderer);
}
// Background brightness by clock time, 0 = night, 1 = day. Keyframes are the top-band brightness of
// the reference day-cycle video relative to 10:30-16:30 (audit/VIDEO-REFERENCES.md); linear between them.
double View::itemIconSize(int,bool bullet,double focus) {
    // Settings bullets in a page; every other icontex item is a category-column icon (no other level was captured).
    // The 1.1022x second size class (85.554/203.700, audit/ITEM-ICONS.md) is NOT applied: at level>0 the only
    // icontex items in this build are the settings bullets, and the settings page's focused bullet was captured at
    // 85.05 with no factor. A/B on the Video Settings page: it scales that bullet to 93.7 (1463 changed px in the
    // bullet column) and changes nothing on the item rows it is meant to help (0 changed px on the Video Library
    // page), so it was reverted. It waits on the settled deeper-page capture described in the audit.
    return bullet?35.721+(85.05-35.721)*focus:77.616+(184.8-77.616)*focus;
}
double View::dayLight(double hour) {
    static const double keys[][2]={{0,0},{3,0},{4.5,.38},{6,.77},{7.5,.93},{9,.98},{10.5,1},{16.5,1},{18,.82},{19.5,.24},{21,0},{24,0}};
    hour=std::fmod(std::fmod(hour,24.)+24.,24.);
    for(size_t i=1;i<std::size(keys);++i)
        if(hour<=keys[i][0])return keys[i-1][1]+(keys[i][1]-keys[i-1][1])*(hour-keys[i-1][0])/(keys[i][0]-keys[i-1][0]);
    return 0;
}
void View::renderBackground(float seconds,float light,int pixelWidth,int pixelHeight) {
    icons_.setClock(backgroundOverride_.isValid()?backgroundOverride_:QDateTime::currentDateTime());
    static const QVector3D colours[]={{.55f,.39f,.13f},{.12f,.36f,.68f},{.40f,.20f,.60f},{.62f,.17f,.18f},{.16f,.43f,.32f},{.48f,.50f,.54f}};
    const int selectedColour=std::clamp(shell_.theme(),0,5);
    QVector3D iconTint=colours[selectedColour];
    if(selectedColour==0) {
        // Original follows the original month textures, including seasonal interpolation. This tint also works
        // over wallpaper; it changes RGB only and never samples/refraction-darkens the icon interior.
        const auto date=(backgroundOverride_.isValid()?backgroundOverride_:QDateTime::currentDateTime()).date();
        const auto monthTint=[&](int month){
            if(!monthIconTints_.contains(month)) {
                const auto& palette=assets_.image("qgl/lines",QString("textures/month_bg/rgb/%1.dds").arg(month,2,10,QChar('0')));
                const auto c=palette.isNull()?QColor(200,180,120):palette.pixelColor(palette.width()/2,palette.height()/2);
                monthIconTints_.insert(month,QVector3D(c.redF(),c.greenF(),c.blueF()));
            }
            return monthIconTints_.value(month);
        };
        // A theme can pin a month's palette (P3t colour 1..12): that month alone, no seasonal interpolation.
        const int themeMonth=shell_.themeColorMonth();
        if(themeMonth>0)iconTint=monthTint(themeMonth);
        else {
            const float fraction=float(date.day()-1)/date.daysInMonth();
            iconTint=monthTint(date.month())*(1-fraction)+monthTint(date.month()%12+1)*fraction;
        }
    }
    icons_.setFixedTint(iconTint,selectedColour==0?.45f:.12f);
    // Original colour theme: the firmware's own background shader and monthly palettes.
    // The firmware background renders every colour setting (as on the PS3, a fixed colour is the same wave pinned to one
    // palette); only the wallpaper mode bypasses it.
    const bool original=originalReady_ && (forceOriginal_ || shell_.backgroundMode()!=2);
    const bool originalRibbon=original && ribbon_.ready();
    const bool originalParticles=originalRibbon && particles_.ready() && shell_.backgroundMode()!=1; // "classic" hides particles
    GLint target=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);
    const bool hdr=original && hdr_.ready(); // the original background is drawn as HDR and tone mapped with glare
    float presetBlend=1; // day/night preset weight for ribbon, particles and HDR: 0 night .. 1 day
    GLuint scene=GLuint(target);
    if(original) {
        scene=hdr?hdr_.begin(*this,pixelWidth,pixelHeight):GLuint(target);
        QDateTime when=backgroundOverride_.isValid()?backgroundOverride_:QDateTime::currentDateTime();
        // Fixed colours pin the month palette (day 1: that month's textures alone); the clock still drives day/night.
        static const int fixedMonth[]={0,8,6,12,5,1}; // Original, Blue, Purple, Red, Green, Silver
        // A theme's own colour (P3t 1..12) pins the month, but only in the Original/follows-month path: a built-in
        // colour is an explicit user choice that overrides it (INFERRED).
        const int themeMonth=shell_.theme()==0?shell_.themeColorMonth():0;
        if(themeMonth>0)
            when=QDateTime(QDate(when.date().year(),themeMonth,1),when.time());
        else if(shell_.theme()>0 && shell_.theme()<int(std::size(fixedMonth)))
            when=QDateTime(QDate(when.date().year(),fixedMonth[shell_.theme()],1),when.time());
        if(when.date().month()!=texturesMonth_)loadMonthTextures(when.date().month());
        // Fixed colours select a background palette, not a different calendar for
        // amb.bmp. Icon material retains the real/diagnostic date set above.
        BackgroundTime time=backgroundTime(when);
        // Measurement instrument: XMB_DAYTIME overrides the clock-derived _DayTime (audit/PIPELINE.md "The day curve").
        if(qEnvironmentVariableIsSet("XMB_DAYTIME"))time.day=qEnvironmentVariable("XMB_DAYTIME").toFloat();
        time.alpha=backgroundAlpha_;
        constexpr float coldboot1NightBias=.513834f; // BACKGROUND.mnu NIGHT WHIT BIAS, override/coldboot1 (coldboot2 = day)
        const float bootBias=coldboot1NightBias+(time.nightBrightness-coldboot1NightBias)*backgroundToColdboot2_;
        time.nightBrightness=bootBias+(time.nightBrightness-bootBias)*backgroundToClock_;
        constexpr double coldboot1DaySpread=2.73593; // BACKGROUND.mnu DAYSPREAD, override/coldboot1
        if(!qEnvironmentVariableIsSet("XMB_DAYTIME")) {
            const double bootSpread=coldboot1DaySpread+(kDaySpread-coldboot1DaySpread)*backgroundToColdboot2_;
            const QTime clock=when.time();
            const double t=(clock.hour()+clock.minute()/60.+clock.second()/3600.)/24.;
            time.day=float(dayCurve(t,bootSpread+(kDaySpread-bootSpread)*backgroundToClock_));
        }
        presetBlend=float(std::clamp((time.nightDayBlend-(1.-kNightBlend))/kNightBlend,0.,1.));
        {ProfScope prof("origBackground");renderOriginalBackground(time,pixelWidth,pixelHeight);}
        {ProfScope prof("ribbon");if(originalRibbon)ribbon_.render(*this,seconds,when.time().msecsSinceStartOfDay()/86400000.,pixelWidth,pixelHeight,scene);}
    }
    glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
    if(original){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);} else glDisable(GL_BLEND);
    DesignSpace(pixelWidth,pixelHeight).viewport(*this);
    background_.bind();background_.setUniformValue("overlay",original);background_.setUniformValue("originalRibbon",originalRibbon);
    if(originalRibbon)background_.setUniformValueArray("ribbonCrest",ribbon_.crest().data(),int(ribbon_.crest().size()),1);background_.setUniformValue("seconds",seconds);background_.setUniformValue("dayLight",light);
    background_.setUniformValue("tint",colours[std::clamp(shell_.theme(),0,5)]);
    background_.setUniformValue("resolution",QVector2D(float(pixelWidth),float(pixelHeight)));
    background_.setUniformValue("monthPalette",0);background_.setUniformValue("useMonthPalette",shell_.theme()==0 && bool(monthTexture_));
    background_.setUniformValue("classic",shell_.backgroundMode()==1 || originalParticles); // firmware particles replace ours
    if(monthTexture_)monthTexture_->bind(0);
    glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);background_.release();
    if(monthTexture_)monthTexture_->release(0);
    glDisable(GL_BLEND);
    presetBlend_=presetBlend;
    if(hdr && qEnvironmentVariableIsSet("XMB_BG_SCENE")) {
        static bool dumped=false;
        if(!dumped) { dumped=true;
        // Trace the raw HDR scene (background + ribbon + particles) before tone mapping, so scene over-brightness
        // can be told apart from the exposure/tone-map/glare passes. Values are clamped to [0,1] for the PNG;
        // the background never exceeds one, so the dark-region levels are exact.
        glBindFramebuffer(GL_FRAMEBUFFER,GLuint(scene));
        QVector<GLfloat> px(pixelWidth*pixelHeight*4);
        glReadPixels(0,0,pixelWidth,pixelHeight,GL_RGBA,GL_FLOAT,px.data());
        QImage dump(pixelWidth,pixelHeight,QImage::Format_RGB32);
        const auto c8=[](float v){return int(std::clamp(v,0.f,1.f)*255.f+.5f);};
        for(int y=0;y<pixelHeight;++y)for(int x=0;x<pixelWidth;++x) {
            const float* p=px.constData()+((pixelHeight-1-y)*pixelWidth+x)*4;
            dump.setPixel(x,y,qRgb(c8(p[0]),c8(p[1]),c8(p[2])));
        }
        dump.save(QString::fromLocal8Bit(qgetenv("XMB_BG_SCENE")));
        }
    }
    // Particles belong to the HDR scene: the firmware draws particles_quads before the tone map and its
    // radial halo into the glare source (audit/PARTICLES.md), so dust picks up the tone map and the glare.
    if(originalParticles) {
        ProfScope profParticles("particles");
        // The emitter grid: the ribbon's 128 x 128 sheet in the particle world with each point's velocity (FUN_0002b288).
        int side=0;std::vector<QVector3D> sheet,motion;ribbon_.sheetMotion(sheet,motion,side);
        particles_.setEmitter(std::move(sheet),std::move(motion),side);
        // xmb_plugin's cursor-move event drives the wind's direction state (FUN_0000a3e8, audit/PARTICLES.md).
        particles_.trackCursor(shell_.category(),shell_.depth(),shell_.selection());
        glBindFramebuffer(GL_FRAMEBUFFER,GLuint(scene)); particles_.render(*this,seconds,pixelWidth,pixelHeight);
    }
    if(hdr)hdr_.setNoiseClock(seconds);
    ProfScope profHdr("hdr finish");
    if(hdr)hdr_.finish(*this,presetBlend,GLuint(target),[this,seconds,originalParticles](int width,int height){
        // Second firmware pass: the same dust drawn into the glare source so the mip blur blooms it
        // (particles_second). Re-running the sim for the same `seconds` advances no steps.
        if(originalParticles)particles_.renderGlare(*this,seconds,width,height);
        // Measurement switch (audit: Phase 2.5): XMB_UI_GLARE=<scale> also adds the UI layer to the glare
        // source, so text and panels bloom through the same Gaussian/accGlare chain the scene does. The layer
        // texture is the previous frame's resolve (the UI is drawn after renderBackground in paintGL), which is
        // stable in every scripted state, so the capture settles on the same result as a same-frame feed.
        if(const float glareBoost=qEnvironmentVariable("XMB_UI_GLARE").toFloat();glareBoost>0 && uiResolved_ && uiComposite_.isLinked()) {
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,uiResolved_->texture());
            uiComposite_.bind();uiComposite_.setUniformValue("layer",0);uiComposite_.setUniformValue("dimOpacity",0.f);
            uiComposite_.setUniformValue("boost",glareBoost);
            glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
            glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);
            glDisable(GL_BLEND);uiComposite_.release();glBindTexture(GL_TEXTURE_2D,0);
        }
    });
}
// Inputs of qgl/lines back_colours0.fpo, as custom_render_plugin's setter (FUN_00052ad8) derives them from the clock
// as a day fraction t in [0,1] and the month as a float in [0,12] (constants at 0x92e54..0x92e6c, preset hours / 24
// at 0x91f6c), with the override/day BACKGROUND.mnu values.
double View::dayCurve(double t,double k) {
    t=std::clamp(t,0.,1.);
    const double t2=t*t,t3=t2*t;
    return 2400.*((3.*t2-2.*t3)+k*(2.*t3-3.*t2+t));
}
View::BackgroundTime View::backgroundTime(const QDateTime& when) {
    const auto clock=when.time();const double hour=clock.hour()+clock.minute()/60.+clock.second()/3600.,t=hour/24.;
    const auto date=when.date();
    BackgroundTime b;
    // _DayTime (traced, custom_render_plugin FUN_00052ad8 -> FUN_00052a28 -> FUN_0004584c): a cubic Hermite from 0 to 1
    // with both tangents k = the preset's DAYSPREAD, times 2400 (0x92e60). Monotonic over the day and point-symmetric
    // about noon, f(1-t) = 2400 - f(t); the console's 1168.77 at 10:00 is the coldboot1 preset's k (audit/PIPELINE.md
    // "The day curve"). The clock presets day/yoake/higure share k = 2.68038; the boot blend is applied in paintGL.
    b.day=float(dayCurve(t,kDaySpread));
    // _NightTime: (t - 0.5, wrapped) x 2400, i.e. the night palette's own clock peaks at midnight.
    b.night=float(std::fmod(t+.5,1.)*2400.);
    // _MonthTime: frac(month) x 30; the integer part picks this/next month's textures (loadMonthTextures).
    // _MonthTime: frac(month) x 30. The console's captured constant at 10:00 on 10-25 is 23.2258
    // ((day-1)/31*30, day-quantised), but passing that value here renders the wrong palette at the
    // canonical 10-25 10:00 pin - the month phase crosses a boundary and the background turns gold
    // instead of brown, so the whole-frame gate drops to 10.97 dB. The phase convention between the
    // captured constant and this shader's month parameter is unresolved (PIPELINE, second pedestal
    // pass); this time-of-day term is a documented fit kept only until that is proven.
    b.month=float((date.day()-1+t)/date.daysInMonth()*30.);
    b.nightBrightness=.486059f;                                      // NIGHT WHIT BIAS
    // NightDayBlend = 1 + NIGHT BLEND x (s1 (1 - s2) - 1), s1/s2 smoothsteps over NIGHT2DAY and DAY2NIGHT (hours / 24):
    // 1 by day, 1 - NIGHT BLEND (0.5) at night.
    const auto smooth=[](double a,double c,double x){if(x<a)return 0.;if(x>c)return 1.;const double f=(x-a)/(c-a);return f*f*(3.-2.*f);};
    const double s1=smooth(0./24,5.16611/24,t),s2=smooth(18.498/24,20.3312/24,t);
    b.nightDayBlend=float(1.+kNightBlend*(s1*(1.-s2)-1.));
    b.alpha=1.f;
    return b;
}
void View::initOriginalBackground() {
    QByteArray binary;
    try { binary=assets_.qglFile("lines","lib/moyou/back_colours0.fpo"); } catch(const std::exception& e) { qWarning()<<"original background unavailable:"<<e.what();return; }
    QString error;backColours_=CgProgram::load(binary,&error);
    if(!backColours_.valid()){qWarning().noquote()<<"original background translation failed:"<<error;return;}
    static const char* vertex=R"(#version 330 core
out vec4 v_col0, v_col1, v_fog;
out vec4 v_tc[10];
void main(){
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    for(int i=0;i<10;++i)v_tc[i]=vec4(0.);
    v_tc[0]=vec4(p,0.,1.); // the firmware quad's uv: y=0 at the bottom (measured: night colour sits low)
    v_col0=v_col1=v_fog=vec4(0.);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)";
    // The firmware blends the program's output by its alpha (_Alpha) over the cleared scene: 1 except while the cold
    // boot fades the background in.
    QString fragment=backColours_.glsl;
    fragment.replace("fragColor = r[0];","fragColor = vec4(r[0].rgb * r[0].a, r[0].a);");
    if(!original_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) || !original_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !original_.link()) {
        qWarning().noquote()<<"original background shader failed:"<<original_.log();return;
    }
    if(!loadMonthTextures(QDate::currentDate().month()))return;
    originalReady_=true;
    qInfo()<<"original background: back_colours0"<<backColours_.instructionCount<<"instructions,"<<backColours_.constants.size()<<"constants";
}
bool View::loadMonthTextures(int month) {
    const int next=month%12+1;
    const QString names[4]={QString("textures/month_bg/rgb/%1.dds").arg(month,2,10,QChar('0')),QString("textures/month_bg/rgb/%1.dds").arg(next,2,10,QChar('0')),
                            QString("textures/month_bg/night/%1.dds").arg(month,2,10,QChar('0')),QString("textures/month_bg/night/%1.dds").arg(next,2,10,QChar('0'))};
    for(int i=0;i<4;++i) {
        const auto& image=assets_.image("qgl/lines",names[i]);
        if(image.isNull()){qWarning().noquote()<<"original background texture missing:"<<names[i];return false;}
        monthTextures_[i]=std::make_unique<QOpenGLTexture>(image); // RSX images are stored top row first
        monthTextures_[i]->setMinificationFilter(QOpenGLTexture::Linear);monthTextures_[i]->setMagnificationFilter(QOpenGLTexture::Linear);
        monthTextures_[i]->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    texturesMonth_=month;
    return true;
}
void View::renderOriginalBackground(const BackgroundTime& time,int pixelWidth,int pixelHeight) {
    QVector<QVector4D> fc=backColours_.constants;
    const auto set=[&](const char* name,float value){
        const auto it=backColours_.parameters.constFind(name);if(it==backColours_.parameters.cend())return;
        for(int slot:it->constantSlots)if(slot<fc.size())fc[slot]=QVector4D(value,value,value,value);
    };
    set("_DayTime",time.day);set("_NightTime",time.night);set("_MonthTime",time.month);
    set("_NightBrightness",time.nightBrightness);set("_NightDayBlend",time.nightDayBlend);set("_Alpha",time.alpha);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    DesignSpace(pixelWidth,pixelHeight).viewport(*this);
    original_.bind();
    original_.setUniformValueArray("fc",fc.constData(),fc.size());
    for(int i=0;i<4;++i){monthTextures_[i]->bind(i);original_.setUniformValue(QString("tex%1").arg(i).toUtf8().constData(),i);}
    glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);
    for(int i=0;i<4;++i)monthTextures_[i]->release(i);
    original_.release();
}
bool View::measureReference(const QString& path) {
    if(!glReady_)return false;
    makeCurrent();
    constexpr int W=1920,H=1080;
    QOpenGLFramebufferObject fbo(W,H);
    if(!fbo.isValid() || !fbo.bind()){doneCurrent();return false;}
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){fbo.release();doneCurrent();return false;}
    QTextStream out(&file);
    const auto hex=[](double r,double g,double b){return QString("%1%2%3").arg(int(r+.5),2,16,QChar('0')).arg(int(g+.5),2,16,QChar('0')).arg(int(b+.5),2,16,QChar('0'));};
    forceOriginal_=originalReady_; // measure the firmware background regardless of the saved theme
    const QDate measureDate=QDate::currentDate();
    const auto at=[&](double hour){backgroundOverride_=QDateTime(measureDate,QTime(int(hour),int(std::fmod(hour,1.)*60)));};
    out<<"# native background measured like audit/VIDEO-REFERENCES.md; "<<(forceOriginal_?QString("original back_colours0, %1").arg(measureDate.toString(Qt::ISODate)):QString("procedural, theme %1").arg(shell_.theme()))<<"\n";
    out<<"# clock  dayLight  bands 1-12 (hex RGB, top to bottom)  |  band luma relative to the 12:00 top band\n";
    QVector<double> middayLuma;
    const auto bands=[&](const QImage& image,QStringList& hexes,QVector<double>& luma){
        for(int b=0;b<12;++b){double s[3]{};int n=0;
            for(int y=b*H/12;y<(b+1)*H/12;y+=4)for(int x=0;x<W;x+=8){const QRgb p=image.pixel(x,y);s[0]+=qRed(p);s[1]+=qGreen(p);s[2]+=qBlue(p);++n;}
            hexes<<hex(s[0]/n,s[1]/n,s[2]/n);luma<<(.2126*s[0]+.7152*s[1]+.0722*s[2])/n;}
    };
    { at(12);renderBackground(420.f,float(dayLight(12)),W,H);QStringList h;bands(fbo.toImage(),h,middayLuma); }
    for(int step=0;step<16;++step) {
        const double hour=step*1.5;const double light=dayLight(hour);
        at(hour);renderBackground(420.f,float(light),W,H);
        QStringList h;QVector<double> luma;bands(fbo.toImage(),h,luma);
        QStringList rel;for(double l:luma)rel<<QString::number(l/std::max(1.,middayLuma[0]),'f',2);
        out<<QString("%1:%2").arg(int(hour),2,10,QChar('0')).arg(int(std::fmod(hour,1.)*60),2,10,QChar('0'))<<"  "<<QString::number(light,'f',2)<<"  "<<h.join(' ')<<"  |  "<<rel.join(' ')<<"\n";
    }
    // Background-only frames next to the statistics, for visual comparison with the reference video.
    for(const auto& [hour,name]:std::initializer_list<std::pair<double,const char*>>{{12.,"midday"},{22.5,"night"},{19.5,"dusk"}}) {
        at(hour);renderBackground(141.f,float(dayLight(hour)),W,H);
        fbo.toImage().save(QFileInfo(path).dir().filePath(QString("video-reference-%1.png").arg(name)));
    }
    out<<"# ribbon crest, fraction of height from the top, 8 columns, 40 s at 1 s steps (midday)\n";
    for(int i=0;i<40;++i) {
        at(12);renderBackground(float(120+i),float(dayLight(12)),W,H);const QImage image=fbo.toImage();
        QStringList crest;
        for(int k=0;k<8;++k){const int x0=(2*k+1)*W/16;QVector<double> prof(H);
            for(int y=0;y<H;++y){double s=0;for(int x=x0-12;x<=x0+12;x+=3){const QRgb p=image.pixel(x,y);s+=qRed(p)+qGreen(p)+qBlue(p);}prof[y]=s;}
            double best=-1;int by=0;
            for(int y=std::max(10,int(H*.25));y<std::min(H-10,int(H*.9));++y){double s=0;for(int j=-10;j<=10;++j)s+=prof[y+j];if(s>best){best=s;by=y;}}
            crest<<QString::number(double(by)/H,'f',3);}
        if(ribbon_.ready()) { // the firmware ribbon is translucent: report its geometric centre line instead
            crest.clear();
            for(int k=0;k<8;++k){const double x=(2*k+1)/16.*32;const int j=std::min(int(x),31);
                crest<<QString::number(ribbon_.crest()[j]+(ribbon_.crest()[j+1]-ribbon_.crest()[j])*(x-j),'f',3);}
        }
        out<<QString("%1s ").arg(i,2)<<crest.join(' ')<<"\n";
    }
    { // Motion contact sheet: 16 frames 0.5 s apart (August, so particles read against the background), centre crop.
        constexpr int frames=16,columns=4,cropW=960,cropH=540,tileW=480,tileH=270;
        QImage sheet(tileW*columns,tileH*(frames/columns),QImage::Format_RGB32);sheet.fill(Qt::black);
        QPainter painter(&sheet);painter.setPen(Qt::white);
        for(int k=0;k<frames;++k) {
            backgroundOverride_=QDateTime(QDate(measureDate.year(),8,1),QTime(12,0));
            renderBackground(float(300+k*.5),1.f,W,H);
            const QImage crop=fbo.toImage().copy((W-cropW)/2,int(H*.12),cropW,cropH).scaled(tileW,tileH,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            const QPoint cell((k%columns)*tileW,(k/columns)*tileH);
            painter.drawImage(cell,crop);painter.drawText(cell+QPoint(6,16),QString("t+%1 s").arg(k*.5,0,'f',1));
        }
        painter.end();
        sheet.save(QFileInfo(path).dir().filePath("video-reference-motion.png"));
    }
    out<<"# moves: paf accel mode 5 over "<<moveMilliseconds<<" ms; remaining distance after each 60 Hz frame\n";
    {
        PafEase ease;ease.retarget(1);ease.retarget(0,moveMilliseconds);
        for(int frame=1;frame<=13;++frame){ease.advance(1/60.);out<<QString::number(ease.value,'f',4)<<(frame<13?" ":"\n");}
    }
    if(originalReady_) {
        // Firmware background shader on its own: every month's palette at noon, then a raw input sweep of
        // _DayTime = _NightTime (clock hours x100) x NightDayBlend, independent of the clock mapping above.
        out<<"# original back_colours0 by month, 15th 12:00: mean RGB of the top, middle and bottom bands\n";
        for(int m=1;m<=12;++m) {
            backgroundOverride_=QDateTime(QDate(measureDate.year(),m,15),QTime(12,0));
            loadMonthTextures(m);
            renderOriginalBackground(backgroundTime(backgroundOverride_),W,H);
            const QImage image=fbo.toImage();
            const auto mean=[&](int y){long r=0,g=0,b=0,n=0;for(int x=0;x<W;x+=16){const QRgb p=image.pixel(x,y);r+=qRed(p);g+=qGreen(p);b+=qBlue(p);++n;}return QString::asprintf("%02lx%02lx%02lx",r/n,g/n,b/n);};
            out<<QString("%1  %2 %3 %4\n").arg(m,2,10,QChar('0')).arg(mean(H/20)).arg(mean(H/2)).arg(mean(H-H/20));
        }
        // Full frames (background, ribbon, particles, HDR) on the 1st of months with strong texture gradients.
        for(int m:{2,6,8,12}) {
            backgroundOverride_=QDateTime(QDate(measureDate.year(),m,1),QTime(12,0));
            renderBackground(141.f,1.f,W,H);
            fbo.toImage().save(QFileInfo(path).dir().filePath(QString("video-reference-month-%1.png").arg(m,2,10,QChar('0'))));
        }
        loadMonthTextures(measureDate.month());
        out<<"# original back_colours0, this/next month textures, 12:00: _MonthTime 0..30 -> mean RGB top / middle\n";
        for(int day=0;day<=30;day+=3) {
            BackgroundTime b=backgroundTime(QDateTime(measureDate,QTime(12,0)));b.month=float(day);
            renderOriginalBackground(b,W,H);const QImage image=fbo.toImage();
            const auto mean=[&](int y){long r=0,g=0,bl=0,n=0;for(int x=0;x<W;x+=16){const QRgb p=image.pixel(x,y);r+=qRed(p);g+=qGreen(p);bl+=qBlue(p);++n;}return QString::asprintf("%02lx%02lx%02lx",r/n,g/n,bl/n);};
            out<<QString("%1  %2 %3\n").arg(day,2).arg(mean(H/20)).arg(mean(H/2));
        }
        for(float blend:{1.f,0.f}) {
            out<<"# original back_colours0 sweep, NightDayBlend "<<blend<<": rows _DayTime, columns _NightTime 0..2400 step 400, luma top/bottom\n";
            for(int d=0;d<=2400;d+=400) {
                out<<QString("%1 |").arg(d,4);
                for(int n=0;n<=2400;n+=400) {
                    BackgroundTime b=backgroundTime(QDateTime(measureDate,QTime(12,0)));
                    b.day=float(d);b.night=float(n);b.nightDayBlend=blend;
                    renderOriginalBackground(b,W,H);
                    const QImage image=fbo.toImage();
                    const auto luma=[&](int y){double s=0;int k=0;for(int x=0;x<W;x+=16){const QRgb p=image.pixel(x,y);s+=.2126*qRed(p)+.7152*qGreen(p)+.0722*qBlue(p);++k;}return s/k;};
                    out<<QString(" %1/%2").arg(luma(H/20),5,'f',1).arg(luma(H-H/20),5,'f',1);
                }
                out<<"\n";
            }
        }
    }
    fbo.release();doneCurrent();
    forceOriginal_=false;backgroundOverride_={};
    return out.status()==QTextStream::Ok;
}
void View::paintGL() {
    ProfScope profPaint("paintGL total");
    if(g_profOn) {
        static QElapsedTimer window;
        if(!window.isValid())window.start();
        if(window.elapsed()>=5000) {
            QString line=QStringLiteral("idle-profile (5 s):");
            for(int i=0;i<idleprofile::slotCount;++i){line+=QString(" | %1 %2 ms/s (%3 calls)").arg(idleprofile::table[i].name).arg(idleprofile::table[i].ns/1e6/(window.elapsed()/1000.),0,'f',1).arg(idleprofile::table[i].calls);idleprofile::table[i].ns=0;idleprofile::table[i].calls=0;}
            qInfo().noquote()<<line;window.restart();
        }
    }
    const qint64 frameStart=frameTracePath_.isEmpty()?0:clock_.nsecsElapsed();
    const qint64 nowNs=clock_.nsecsElapsed(),now=nowNs/1000000;
    const double dt=std::clamp(double(nowNs-previousFrameNs_)/1.e9,0.,.1);previousFrameNs_=nowNs;
    const qint64 artworkTime=focusedBackgroundIsPage_ && focusTimeOverride_<0?pageBackdropClock_.elapsed():focusTime();
    const bool showArtwork=!focusedBackground_.isNull() && artworkTime>=artworkDwellMs && !shell_.mediaOpen() && !shell_.psMenuOpen();
    const double fadeStep=dt*1000./artworkFadeMs;
    focusedBackgroundOpacity_=std::clamp(focusedBackgroundOpacity_+(showArtwork?fadeStep:-fadeStep),0.,1.);
    departingBackgroundOpacity_=std::max(0.,departingBackgroundOpacity_-fadeStep);
    if(departingBackgroundOpacity_==0.)departingBackground_={};
    const double renderSeconds=secondsOverride_>=0.?secondsOverride_:double(nowNs)/1.e9; // fixed for A/B captures
    // Every move is a firmware paf animation (PafEase): 200 ms in accel mode 5 for the category and item moves and
    // the column cross-fade. The page (depth) move runs on two eases because two evidence sets disagree on it and
    // each drives a different channel (audit/PAGE-MOVE.md 14.1 traced the bar re-layout at 200 ms mode 5; the 4c
    // burst's frame count is 18 frames = 300 ms mode 4 for the page content). See view.h and section 15 of the audit.
    categoryEase_.retarget(shell_.category(),moveMilliseconds);categoryEase_.advance(dt);categoryPosition_=categoryEase_.value;
    if(!outgoing_.isEmpty()){fadeEase_.advance(dt);categoryFade_=fadeEase_.value;if(!fadeEase_.running()){categoryFade_=0;outgoing_.clear();}}
    // Opening a page and closing one are not the same start: the console's close holds still for ~49 ms after the
    // Circle press and its first moved render is at +57 ms, while the open's first visible frame is the page load
    // (PAGE-MOVE 16.2, measured on the 642-frame burst). Both depth channels take the same offset: the burst times
    // the page content, and the bar layer's own start is hidden behind it (its neighbours are already at the end
    // state on the first visible frame), so the bar's share of the hold is a FIT to the content's measured one.
    // `depthSeen_` is the depth the previous painted frame saw, because syncColumns() has already updated
    // `previousDepth_` to the new value by the time this runs.
    const int depthNow=shell_.depth();
    const bool pageClosing=depthSeen_>=0 && depthNow<depthSeen_;
    const bool pluginOpening=depthSeen_>=0 && depthNow>depthSeen_ && shell_.topPageLoadsPlugin();
    depthSeen_=depthNow;
    const double depthDelay=pageClosing?pageCloseDelayMs:pluginOpening?pluginPageOpenFloorMs:0.;
    depthEase_.retarget(shell_.depth(),pageMilliseconds,5,depthDelay);depthEase_.advance(dt);depthPosition_=depthEase_.value;
    barEase_.retarget(shell_.depth(),barMilliseconds,5,depthDelay);barEase_.advance(dt);barDepthPosition_=barEase_.value;
    // Capture-only switch: XMB_DEPTH_PIN=<0..2> holds the depth progress at that value, so a deterministic
    // --render-clock grab can measure one intermediate frame of the page move (the easing itself runs on the
    // real frame clock, which --render-clock does not pin). Both channels are pinned to the same value so a
    // pinned frame stays the one geometry model section 13.4 tabulates. Unset in every normal run, where it
    // changes nothing.
    if(const QString pin=qEnvironmentVariable("XMB_DEPTH_PIN");!pin.isEmpty()) {
        bool ok=false;const double pinned=std::clamp(pin.toDouble(&ok),0.,double(shell_.depth()));
        if(ok){depthPosition_=pinned;barDepthPosition_=pinned;}
    }
    // Preserve the historical diagnostic pin; normal input never increments it.
    if(const QString pin=qEnvironmentVariable("XMB_OVERSCROLL");!pin.isEmpty()) {
        bool ok=false;const int presses=pin.toInt(&ok);
        if(ok)columnSink_=std::clamp(presses,0,8);
    }
    for(auto& column:columns_){column.ease.retarget(column.selection,moveMilliseconds);column.ease.advance(dt);column.position=column.ease.value;}
    // The item-level Triangle options panel: its list slides in from the right and moves between rows on the
    // 200 ms mode-5 curve. A fresh open lands on the current selection without sliding the list. The entrance is
    // NOT the 200 ms key-to-rest the rest of the XMB uses: the console starts it 115..150 ms after the key and
    // ramps 245..255 ms (UI-ELEMENT-GAP-AUDIT O6 [M]), i.e. at rest 360..410 ms; the close starts 9 ms after the
    // key and ramps 200 ms. paf has no delay parameter, so the delay is carried by PafEase itself (a local
    // addition, view.h) rather than by starting the ease late: a late start cannot express the measured 9 ms
    // close offset and would desynchronise the panel's slide from its own opacity.
    if(shell_.optionsOpen()) {
        if(!optionsWasOpen_){optionsOpenEase_=PafEase();optionsOpenEase_.retarget(0);optionsPanelEase_=PafEase();}
        optionsOpenEase_.retarget(1,optionsOpenMs,5,optionsOpenDelayMs);
        optionsPanelEase_.retarget(shell_.optionsSelection(),moveMilliseconds);
        // Latch the content while Shell still holds it: the closing Triangle clears it before the close ramp is
        // over, so the draw reads this copy (PAGE-MOVE 15.4).
        optionsShown_.titles.clear();optionsShown_.titles.reserve(shell_.options().size());
        for(const auto& option:shell_.options())optionsShown_.titles<<option.title;
        optionsShown_.heading=shell_.optionsTitle();
        optionsShown_.rows=shell_.options();
        optionsShown_.selection=shell_.optionsSelection();
        optionsShown_.player=shell_.optionsArePlayer();
        optionsShown_.valid=true;
    } else optionsOpenEase_.retarget(0,moveMilliseconds,5,optionsCloseDelayMs);
    optionsOpenEase_.advance(dt);optionsPanelEase_.advance(dt);
    // The latch ends when the close ramp has run out, so the panel is gone only after its measured fade.
    if(!shell_.optionsOpen() && optionsOpenEase_.value<=0.001)optionsShown_.valid=false;
    if(static const bool motionLog=qEnvironmentVariableIsSet("XMB_MOTION_LOG");motionLog && optionsOpenEase_.running())
        qInfo().noquote()<<"options:"<<now<<"ms open amount"<<QString::number(optionsOpenEase_.value,'f',4);
    optionsWasOpen_=shell_.optionsOpen();
    // Surface B2, the music control panel (audit/MEDIA-PANELS-FIRMWARE.md 4.3): the traced presentation is
    // a 150-unit fade with 0 delay (explore_plugin.prx 0x270a00 / 0x270a04) and a 150/100 move/size
    // animation on the panel base, which drawMusicPanel expresses as the 1.0 -> 1.3 scale (0x270a54). The
    // close has no traced time (the container carries no animations), so it uses the same 150.
    if(!mediaAudioOnly()){musicPanelOpen_=false;musicVisualizerOn_=false;}
    musicPanelEase_.retarget(musicPanelOpen_?1.:0.,musicPanelFadeMs);
    musicPanelEase_.advance(dt);
    // The music page's idle style (MUSIC-PLAYER.md): playing, panel closed and no input for musicIdleMs.
    {
        const bool musicIdle=mediaAudioOnly() && !shell_.mediaPaused() && !shell_.busy() && !musicPanelOpen_
            && shell_.message().isEmpty() && shell_.idleMilliseconds()>musicIdleMs;
        musicIdleEase_.retarget(musicIdle?1.:0.,musicIdleFadeMs);
        musicIdleEase_.advance(dt);
    }
    if(static const bool panelLog=qEnvironmentVariableIsSet("XMB_MEDIA_TRACE");panelLog && musicPanelEase_.running())
        qInfo().noquote()<<"media: music panel fade"<<QString::number(musicPanelEase_.value,'f',3);
    // The notification pop-up: Shell decides when one exists; here it slides in on the shared 200 ms mode-5
    // curve and slides out on the firmware's own 100 ms fade (basic_plugins.prx DAT_00047f94 = 100.0,
    // NOTIFICATIONS.md 11.2). The content is latched so the last frame of the exit still draws the text after
    // Shell has already expired the entry.
    if(const auto* note=shell_.notification())notificationShown_={note->title,note->detail,note->icon};
    notificationEase_.retarget(shell_.notification()?1.:0.,shell_.notification()?moveMilliseconds:notificationHideMilliseconds);
    notificationEase_.advance(dt);
    // Message surfaces (audit/DIALOGS-FIRMWARE.md): the modal box is the only shape with traced open and
    // close motion - 200 units on its label group (TRACED 0x00341BFC, DATA 0x0068C978) and 1000 on the
    // plate child (TRACED 0x00344094, DATA 0x0068CA1C). The hint row's 100-unit per-button fade is the
    // page_btnnavi animation, so every hinted surface uses it. The content is latched while the box fades
    // out, so the exit still has something to draw after Shell has dismissed the message.
    const MessageKind messageKind=shell_.message().isEmpty()?MessageKind::None:shell_.messageKind();
    if(messageKind!=MessageKind::None)messageShown_={shell_.message(),messageKind,shell_.messageActions()};
    const bool boxUp=messageKind==MessageKind::Box;
    const bool surfaced=messageKind==MessageKind::Page || boxUp ||
        messageKind==MessageKind::ErrorCode || messageKind==MessageKind::Info;
    dialogEase_.retarget(boxUp?1.:0.,dialogMilliseconds);
    dialogPlateEase_.retarget(boxUp?1.:0.,dialogPlateMilliseconds);
    dialogHintEase_.retarget(surfaced?1.:0.,dialogHintMilliseconds);
    dialogSettleEase_.retarget(messageKind==MessageKind::ErrorCode?1.:0.,dialogSettleMilliseconds);
    dialogEase_.advance(dt);dialogPlateEase_.advance(dt);dialogHintEase_.advance(dt);dialogSettleEase_.advance(dt);
    // Local diagnosis: XMB_NOTIFY_LOG=1 also traces the composite's own curve, one line per frame while it
    // slides and fades, so the 200 ms mode-5 timing is measurable without scoring pixels.
    if(static const bool notifyLog=qEnvironmentVariableIsSet("XMB_NOTIFY_LOG");notifyLog && notificationEase_.running())
        qInfo().noquote()<<"notify: view slide/fade"<<QString::number(notificationEase_.value,'f',4)
            <<"x"<<QString::number((1.-notificationEase_.value)*48.,'f',2)<<"px";
    // Local diagnosis: XMB_MOTION_LOG=1 logs the focused column's and the depth's eased positions each frame while they move.
    if(static const bool motionLog=qEnvironmentVariableIsSet("XMB_MOTION_LOG");motionLog && !columns_.isEmpty() && columns_.back().ease.running())
        qInfo().noquote()<<"motion:"<<now<<"ms position"<<QString::number(columns_.back().position,'f',4)<<"target"<<columns_.back().selection;
    if(static const bool motionLog=qEnvironmentVariableIsSet("XMB_MOTION_LOG");motionLog && (depthEase_.running() || barEase_.running()))
        qInfo().noquote()<<"depth:"<<now<<"ms position"<<QString::number(depthPosition_,'f',4)
            <<"bar"<<QString::number(barDepthPosition_,'f',4)<<"target"<<shell_.depth();
    if(std::abs(depthPosition_-shell_.depth())<.001) {
        depthPosition_=shell_.depth();
        if(columns_.size()>shell_.depth()+1)columns_.resize(shell_.depth()+1);
    }
    if(std::abs(barDepthPosition_-shell_.depth())<.001)barDepthPosition_=shell_.depth();
    glClearColor(.04f,.04f,.05f,1.f);glClear(GL_COLOR_BUFFER_BIT);
    // Photo Gallery (audit/PHOTO-GALLERY.md): while its area is up it replaces the XMB scene; the photo viewer over it uses
    // the normal path below.
    if(galleryDrawing()){paintGallery(dt,renderSeconds);++frames_;return;}
    // Cold boot: the intro starts once its sound is decoded (or after 1.5 s without it); its clock drives the boot
    // presets of every background layer (BootBG2 / NormalBG2, Ribbon::bootWeights) and hides the UI until ShowGUI.
    if(coldboot_.ready()) {
        if(coldboot_.seconds()==0 && (coldboot_.soundReady() || now>1500))coldboot_.start();
        const double boot=coldboot_.seconds();
        const auto weights=Ribbon::bootWeights(boot,Coldboot::normalBg);
        ribbon_.setBootTime(boot,Coldboot::normalBg);
        hdr_.setBoot(weights.toColdboot2,weights.toClock);
        particles_.setGlobalAlpha(weights.toClock); // PARTICLES skips coldboot2: GLOBAL ALPHA 0 until NormalBG2 (RPCS3 capture)
        const double fade=std::clamp(boot/7.5,0.,1.); // DAT_00091e20, the 7.5 s that BootBG2 passes with the clock preset
        backgroundAlpha_=float(fade*fade*(3-2*fade));
        backgroundToColdboot2_=weights.toColdboot2;backgroundToClock_=weights.toClock;
    } else {
        // No intro: the XMB is already past boot, so the ribbon holds the clock preset like HDR, particles and the
        // background do. Left on its own frame time it would replay coldboot1 -> clock, and under a pinned
        // --render-clock it would stay on coldboot1 (FRESNEL 2, FFD SCALE2 Y 1.0) for good.
        ribbon_.setBootTime(1e9,Coldboot::normalBg);
    }
    // In-game XMB: no wave or particles (xmb_ingame's page_xmb_bg is empty, TRACED F15); the captured game frame,
    // darkened, is the backdrop instead (display-referred estimate: audit/INGAME-FIDELITY-SWEEP.md).
    if(glReady_ && shell_.inGame())drawInGameBackdrop(int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));
    else {
        stopLiveOverlay();inGameBackdrop_=QImage();
        inGameTexture_.reset();inGameTextureKey_=0;
    }
    if(glReady_ && !shell_.inGame()) {
        const auto time=QTime::currentTime();
        {ProfScope prof("renderBackground");renderBackground(float(renderSeconds),float(dayLight(time.hour()+time.minute()/60.+time.second()/3600.)),
            int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()));}
        {ProfScope prof("coldboot render");coldboot_.render(*this,int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF()),defaultFramebufferObject());}
    }
    if(introRunning()){grabIntroFrame();++frames_;return;}
    // UI layer on the GPU: Qt's OpenGL paint engine draws into a 4x multisampled framebuffer at twice the window size;
    // it is resolved, then composited at half scale, where the bilinear fetch averages each 2x2 block. MSAA plus 2x
    // supersampling gives smooth glyph and icon edges. The layer is redrawn only when something visible changed.
    const QSize device(int(width()*devicePixelRatioF()),int(height()*devicePixelRatioF())),pixels=device*2;
    // PIC1 and wallpaper belong to the scene, below the glass icons and all UI.
    // Painting them into the UI cache would cover the separately drawn icon pass.
    if((shell_.backgroundMode()==2 && !wallpaperImage_.isNull()) || focusedBackgroundOpacity_>0. || departingBackgroundOpacity_>0.) {
        glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());glViewport(0,0,device.width(),device.height());
        QOpenGLPaintDevice backgroundDevice(device);
        QPainter backdrop(&backgroundDevice);backdrop.setRenderHint(QPainter::SmoothPixmapTransform);
        backdrop.setTransform(DesignSpace(device).transform());
        drawBackdrops(backdrop);
    }
    // The keyboard's caret blink. 500 ms is the audit's FIT (OSK-FIRMWARE 5.2: the capture rig tops out near
    // 5 fps, so the period was never traced) and lives in OskCaret::blinkMilliseconds, not here. A pinned
    // --render-clock drives the phase as well as the background, so two captures of one state are identical
    // frames; a live run follows the wall clock.
    const qint64 blinkMs=secondsOverride_>=0?qint64(secondsOverride_*1000.):clock_.elapsed();
    if(osk_.open)osk_.caretLit=((blinkMs/qMax(1,oskLayout_.caret.blinkMilliseconds))%2)==0;
    QString signature=QString::asprintf("%.4f %.4f %.4f %d %d %d ",categoryPosition_,depthPosition_,categoryFade_,shell_.category(),shell_.depth(),uiRevision_);
    // The bar layer runs its own (traced) depth ease; it is part of the signature only while the two disagree,
    // so the canonical rest frame's signature string is byte-identical to before the split.
    if(std::abs(barDepthPosition_-depthPosition_)>.0005)
        signature+=QString::asprintf(" bar %.4f ",barDepthPosition_);
    for(const auto& column:columns_)signature+=QString::number(column.position,'f',4)+' ';
    signature+=shell_.clockText()+shell_.message()+shell_.status();
    if(shell_.inGame())signature+=QStringLiteral(" ingame");
    // The message surface's own fades run between shell changes (the box in and out, the hint row, the
    // error page's settle), so their amounts belong in the signature, quantised by 1/256.
    if(dialogEase_.value>.002 || dialogPlateEase_.value>.002 || dialogHintEase_.value>.002 || dialogSettleEase_.value>.002)
        signature+=QString::asprintf(" dialog %d %d %d %d ",
            int(dialogEase_.value*256.),int(dialogPlateEase_.value*256.),int(dialogHintEase_.value*256.),int(dialogSettleEase_.value*256.));
    // The options panel animates (slide-in, row focus) without a shell change on every frame. While the close
    // ramp runs, Shell has already dropped the panel, so the latched copy's identity is what the layer shows;
    // its selection and the ramp amount are in the string, so the fade redraws per frame.
    if(shell_.optionsOpen() || optionsOpenEase_.value>0.)
        signature+=QString::asprintf(" options %d %.4f %.4f %d %d",shell_.optionsOpen()?1:0,optionsOpenEase_.value,
            optionsPanelEase_.value,shell_.optionsSelection(),int(optionsShown_.valid));
    // The end-of-list sink is a projection amount the shell's press count moves; absent at rest (count 0), so
    // the canonical frame's signature string is unchanged.
    if(columnSink_>0)signature+=QString::asprintf(" sink %d ",columnSink_);
    // The PS surfaces' own envelopes advance with the clock, not with a shell change, so their state belongs
    // in the signature or the cached UI layer would freeze on the frame the menu opened: the hold menu's
    // black/text/bloom staging and the tap indicator's fade. Quantised by 1/256, and absent at rest (so the
    // canonical frame's signature string is unchanged).
    if(shell_.psMenuOpen())
        signature+=QString::asprintf(" pshold %d %d %d",int(psHoldTextAlpha(now)*256.),int(psHoldBloomAmount(now)*256.),
            (psPressAt_>0 && now-psPressAt_>=qint64(psHoldBlackMs))?1:0);
    else if(psHudAt_>0)
        signature+=QString::asprintf(" pshud %d",int(psHudOpacity(now)*256.));
    if(focusClock_.isValid()) { // the focused label's breathing glow and the idle label fade, in coarse steps
        const double resting=double(focusTime());
        signature+=QString::asprintf(" glow %d idle %d",int((.5-.5*std::cos(2*std::numbers::pi*resting/3400.))*24),int(std::clamp((resting-4000)/800.,0.,1.)*16));
    }
    // The keyboard's cached UI signature: the focused key, the page, the palette window and the caret phase,
    // plus the text being composed. A masked field contributes its bullet count only, so the plaintext never
    // enters this cache string (which is one bug away from a captured frame - audit/JELLYFIN-NATIVE.md 3).
    if(osk_.open)signature+=QString::asprintf("osk %d %d %d %d %d %d %d %d %d ",
        osk_.row,osk_.column,osk_.cursor,osk_.page,int(osk_.paletteOpen),osk_.paletteRow,osk_.paletteColumn,
        osk_.paletteScroll,int(osk_.caretLit))+
        (osk_.masked?QString(osk_.text.size(),oskLayout_.mask.glyph):osk_.text);
    // The notification composite animates outside a shell change (slide/fade), so its amount and content are
    // part of the signature: quantised by 1/256, which is finer than the 2x layer can show.
    // The operation guide fades on the focus clock alone, so its amount is part of the drawn state.
    if(const double hint=optionHintAmount();hint>.002)signature+=QString::asprintf(" opthint %d",int(hint*256.));
    if(notificationEase_.value>.002 && !notificationShown_.title.isEmpty())
        signature+=QString::asprintf(" notify %d ",int(notificationEase_.value*256.))+notificationShown_.title+'|'+
            notificationShown_.detail+'|'+notificationShown_.icon;
    // The volume HUD fades on its own clock between shell changes, so its opacity (quantised by 1/256, finer
    // than the 2x layer can show) and the value it is drawing belong in the signature. The 150 ms appear edge is
    // a SEPARATE multiplier applied at the draw site (drawScene: `hud.opacity*fadeIn`), and leaving it out of this
    // string is what let a still frame lose the meter: the level and the opacity both change at the moment of the
    // change, so the layer rebuilds once - 4 ms into the fade, at fadeIn 0.027 - and then never again, because
    // nothing else in the signature moves for the rest of the 3 s window. FIDELITY-MEDIA H3 measured exactly that
    // (the meter band unchanged at 43.39 dB / 0.9966 against a no-HUD frame in 5 of 6 pinned captures, while a
    // live run draws it at full strength). The fade amount is part of the drawn state, so it enters the signature
    // quantised by 1/256 like the opacity beside it; at rest the HUD is invisible and the string is unchanged.
    if(const auto hud=volumeHud_.state(clock_.elapsed());hud.visible) {
        const double fadeIn=std::clamp(double(clock_.elapsed()-volumeHudAt_)/volumeMeterFadeMs,0.,1.);
        signature+=QString::asprintf(" vol %d %d %d %d ",int(hud.opacity*256.),int(fadeIn*256.),volumeLevel_,volumeMuted_?1:0);
        // Opt-in trace of the fade itself: one line per frame while it runs, so the 3000/250 ms boundary the
        // video controls are specified at is measurable here without landing a capture inside a 250 ms window.
        if(volumeTrace() && hud.opacity<1.)
            qInfo().noquote()<<QString("volume: trace HUD fade opacity %1 at %2 ms after the change")
                .arg(hud.opacity,0,'f',3).arg(clock_.elapsed()-volumeHudAt_);
    }
    // The PS-tap indicator's fade runs on the same wall clock between shell changes (155/66/2000/66 ms),
    // so its opacity is part of the signature; without it the cached layer would freeze the plate at the
    // opacity of the frame the tap landed on.
    if(psHudAt_>0 && clock_.elapsed()<psHudUntil_)
        signature+=QString::asprintf(" psHud %d ",int(psHudOpacity(clock_.elapsed())*256.));
    if(shell_.mediaOpen())signature+=QString::number(now); // media frames change every frame
    bool rebuild=signature!=uiSignature_;
    if(!uiMsaa_ || uiMsaa_->size()!=pixels) {
        QOpenGLFramebufferObjectFormat msaa;msaa.setSamples(4);msaa.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        uiMsaa_=std::make_unique<QOpenGLFramebufferObject>(pixels,msaa);
        uiResolved_=std::make_unique<QOpenGLFramebufferObject>(pixels);
        glBindTexture(GL_TEXTURE_2D,uiResolved_->texture());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D,0);
        rebuild=true;
    }
    if(rebuild) {
        ProfScope profUi("ui rebuild");
        uiSignature_=signature;
        uiMsaa_->bind();glViewport(0,0,pixels.width(),pixels.height());glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        // Glyphs are rasterised for the layer's own scale (twice the window): at the window's scale they were
        // magnified 2x by bilinear sampling before the 2x2 resolve, which left all text soft.
        textRenderer_.beginFrame(*this,pixels,DesignSpace(pixels).y,2);
        {
            QOpenGLPaintDevice paintDevice(pixels);
            QPainter ui(&paintDevice);
            ui.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
            ui.setTransform(DesignSpace(pixels).transform());
            const qint64 sceneStart=clock_.nsecsElapsed();drawScene(ui);sceneNs_=clock_.nsecsElapsed()-sceneStart;
            particles_.trackWidgets(windSites_,renderSeconds);
        }
        QOpenGLFramebufferObject::blitFramebuffer(uiResolved_.get(),uiMsaa_.get());
    }
    // Category icons: the firmware glass shader, refracting the finished background (each cut before its own draw).
    const qint64 iconStart=clock_.nsecsElapsed();
    // Screensaver state for this frame (see the UI composite below): the XMB bar's icon draws read the same
    // BRIGHTNESS colour as the widgets (FUN_0038fcb4 -> FUN_00396328/00396528), so the icon pass is scaled too.
    const bool screensaver=shell_.screensaverMinutes()>0 && shell_.idleDisplayAllowed()
        && shell_.idleMilliseconds()>qint64(shell_.screensaverMinutes())*60000;
    icons_.setColourScale(screensaver?.2f:1.f);
    {ProfScope prof("icons");if(glReady_ && icons_.ready())icons_.render(*this,iconQueue_,renderSeconds,presetBlend_,device.width(),device.height(),defaultFramebufferObject(),bool(liveOverlay_));}
    iconNs_=clock_.nsecsElapsed()-iconStart;
    // Composite (premultiplied alpha) over the background in the widget's framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());glViewport(0,0,device.width(),device.height());
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    // Screensaver, traced from vsh.elf (audit/FIDELITY-SURFACES-MAP.md S29): the XMB's own client (identify
    // 0xFFFFFFF0, priority 0xFD, property 0x202 = BRIGHTNESS<<8 | ACTIVE) fires once the idle time passes the
    // start time (300/600/1200/1800 s). BRIGHTNESS writes the global colour multiplier straight to
    // (0.2,0.2,0.2,1) with no ramp, and input restores it. Its traced readers are the paf widget renderers and
    // the item list (FUN_0039d4a8 callers, FUN_0038fcb4), i.e. this UI layer: its colour is scaled, its alpha
    // kept. The background, ribbon and particles come from custom_render_plugin, which is not seen reading that
    // colour, so they are left as they are (INFERRED). No host brightness or power changes.
    uiComposite_.bind();uiComposite_.setUniformValue("layer",0);uiComposite_.setUniformValue("dimOpacity",0.f);
    uiComposite_.setUniformValue("boost",0.f);uiComposite_.setUniformValue("rgbScale",screensaver?.2f:1.f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,uiResolved_->texture());
    glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);
    uiComposite_.setUniformValue("rgbScale",1.f);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D,0);uiComposite_.release();glDisable(GL_BLEND);
    double overlayBridgeMs=0.;
    if(liveOverlay_) {
        const qint64 bridgeStart=clock_.nsecsElapsed();
        if(liveOverlayPixels_.size()!=device)liveOverlayPixels_=QImage(device,QImage::Format_ARGB32_Premultiplied);
        if(!liveOverlayResolved_ || liveOverlayResolved_->size()!=device) {
            QOpenGLFramebufferObjectFormat f;f.setInternalTextureFormat(GL_RGBA8);
            liveOverlayResolved_=std::make_unique<QOpenGLFramebufferObject>(device,f);
        }
        if(liveOverlayPixels_.isNull() || !liveOverlayResolved_->isValid()) {
            psLog(QStringLiteral("ingame: live framebuffer allocation failed; captured-frame fallback"));
            glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());stopLiveOverlay();update();return;
        }
        // The widget requests 4x MSAA (main.cpp). Reading it directly is GL_INVALID_OPERATION;
        // resolve explicitly, preserving alpha, before bypassing Qt's opaque top-level blit.
        glBindFramebuffer(GL_READ_FRAMEBUFFER,defaultFramebufferObject());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,liveOverlayResolved_->handle());
        glBlitFramebuffer(0,0,device.width(),device.height(),0,0,device.width(),device.height(),GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,liveOverlayResolved_->handle());
        glPixelStorei(GL_PACK_ALIGNMENT,4);
        glReadPixels(0,0,device.width(),device.height(),GL_BGRA,GL_UNSIGNED_BYTE,liveOverlayPixels_.bits());
        glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());
        QString error;
        const GLenum readError=glGetError();
        if(readError!=GL_NO_ERROR)error=QStringLiteral("OpenGL framebuffer readback: 0x%1").arg(readError,0,16);
        if(readError!=GL_NO_ERROR || !liveOverlay_->present(liveOverlayPixels_.flipped(Qt::Vertical),&error)) {
            psLog(QStringLiteral("ingame: live overlay failed; captured-frame fallback: %1").arg(error));
            stopLiveOverlay();update();
        }
        overlayBridgeMs=double(clock_.nsecsElapsed()-bridgeStart)/1e6;
    } else liveOverlayResolved_.reset();
    grabIntroFrame();
    if(!frameTracePath_.isEmpty() && frameTrace_.size()<20000) {
        bool moving=categoryEase_.running() || depthEase_.running() || fadeEase_.running();
        for(const auto& column:columns_)moving|=column.ease.running();
        frameTrace_.push_back({frameStart,clock_.nsecsElapsed()-frameStart,false,rebuild,moving});
    }
    // Local diagnosis: XMB_TEXT_COST=1 logs any frame whose CPU time passes 10 ms, with the glyph-cache misses
    // (FreeType raster + halo tile) it paid for, so a first-use stall can be told apart from other work.
    if(static const bool textCost=qEnvironmentVariableIsSet("XMB_TEXT_COST");textCost) {
        const auto [misses,missNs]=textRenderer_.takeMissCost();
        const qint64 frameNs=clock_.nsecsElapsed()-nowNs;
        if(frameNs>10'000'000)qInfo().noquote()<<QString("text cost: frame %1 ms, scene %2 ms, glass icons %3 ms, %4 glyph misses %5 ms")
            .arg(frameNs/1e6,0,'f',1).arg(sceneNs_/1e6,0,'f',1).arg(iconNs_/1e6,0,'f',1).arg(misses).arg(missNs/1e6,0,'f',1);
    }
    sceneNs_=0;
    if(shell_.inGame() && qEnvironmentVariableIsSet("XMB_OVERLAY_COST"))
        qInfo().noquote()<<QString("ingame: render cost live=%1 cpuMs=%2 bridgeMs=%3")
            .arg(liveOverlay_?1:0).arg(double(clock_.nsecsElapsed()-nowNs)/1e6,0,'f',3).arg(overlayBridgeMs,0,'f',3);
    ++frames_;if(now-reportAt_>=5000){qInfo()<<"native frames:"<<frames_*1000./double(now-reportAt_)<<"fps; decoded images"<<assets_.decodedImages()<<"missing images"<<assets_.failedImages();frames_=0;reportAt_=now;}
}
// Local verification: XMB_INTRO_GRAB=<dir>@<seconds>,<seconds>,... saves the frame (as drawn) the first time the
// intro clock passes each time, as <dir>/intro_<intro seconds>.png.
void View::grabIntroFrame() {
    static const QString spec=qEnvironmentVariable("XMB_INTRO_GRAB");
    if(spec.isEmpty() || !coldboot_.ready())return;
    static QList<double> pending=[]{QList<double> times;for(const auto& t:spec.section('@',1).split(','))times<<t.toDouble();std::sort(times.begin(),times.end());return times;}();
    const double boot=coldboot_.seconds();
    if(pending.isEmpty() || boot<pending.first() || boot>1e8)return;
    while(!pending.isEmpty() && boot>=pending.first())pending.removeFirst();
    // grabFramebuffer() resolves the multisampled framebuffer; it paints again, so it runs after this frame.
    QTimer::singleShot(0,this,[this,boot]{
        QDir().mkpath(spec.section('@',0,0));
        grabFramebuffer().save(QDir(spec.section('@',0,0)).filePath(QString("intro_%1.png").arg(boot,0,'f',3)));
    });
}
void View::drawImage(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal opacity) {
    if(container=="qgl/icontex") {
        p.save();p.setOpacity(opacity);drawCategory(p,Category{texture,{},container,texture,{}},rect);p.restore();return;
    }
    const auto& image=assets_.bestImage(container,texture);
    if(image.isNull())return;
    // Fit the visible (alpha) bounds, not the whole texture: RCO icons carry transparent padding, and fitting the
    // padded image drew the glyphs at ~60% of the reference size.
    const QString key=container+"/"+texture;
    auto bounds=imageBounds_.value(key);
    if(bounds.isNull()) {
        const QImage argb=image.convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<argb.height();++y){const auto* row=reinterpret_cast<const QRgb*>(argb.constScanLine(y));for(int x=0;x<argb.width();++x)if(qAlpha(row[x])>16)bounds=bounds.united(QRect(x,y,1,1));}
        if(bounds.isNull())bounds=image.rect();
        imageBounds_.insert(key,bounds);
    }
    const QSizeF size=QSizeF(bounds.size()).scaled(rect.size(),Qt::KeepAspectRatio);
    p.save();p.setOpacity(opacity);p.drawImage(QRectF(rect.center()-QPointF(size.width()/2,size.height()/2),size),image,bounds);p.restore();
}
// The item-icon mask, prepared once per texture. The firmware's row art is a white mask whose own alpha IS the
// drawn coverage, so the console composite is a plain source-over of the texel - there is no refraction term and
// no rim pass. Measured on out/rpcs3-allgraphics-2026-10-28/frames/col-game-r01.png (Game Data Utility focused),
// a scanline through the folder body at y=505 reads a FLAT 202..204 from x 505 to x 625 over a 96 background,
// i.e. 0.68 of white; the texture's own body is 195/255 with RGB 237 (median of its covered pixels), and
// source-over gives 96*(1-.765) + 237*.765 = 203.9 - the frame's 204. The unfocused rows agree: the same body
// reads 166 over 54 on rpcs3-day-2026-10-28/frames/c-game-root.png (0.56 of white), which is 54*(1-.765) +
// 237*.765 = 194 for a mask of the same 195 median. So the drawn alpha is the mask's alpha, and the transform
// this function used to apply - rewriting every non-rim texel to (255-alpha)*0.78 - is deleted, not retuned.
// Its premise (a comment claiming the console's folder body reads 0.18 of white) is falsified on BOTH console
// batches: 0.18 is what the rewrite produced, which is why every RCO row icon drew as a hollow outlined
// rectangle at 0.10-0.25 of white with the background wave showing through its interior (FIDELITY-SURFACES
// 3.1a, its top gap 1). [TRACED: the body alpha, the RGB, the two composite readings and their backgrounds.
// FIT: nothing - the texture's own alpha is the drawn coverage, so there is no number left to fit.]
const QImage& View::itemGlyph(const QString& container,const QString& texture) {
    const QString key=container+"/"+texture;
    if(const auto it=itemGlyphs_.constFind(key);it!=itemGlyphs_.constEnd())return it.value();
    const QImage& source=assets_.bestImage(container,texture);
    if(source.isNull())return itemGlyphs_.insert(key,QImage()).value();
    // The cache also normalises the format, so every drawItemGlyph call site hands QPainter an ARGB32 image.
    // Opaque artwork (the PS Store bag at median alpha 255, ICON0 stills, disc art) and translucent masks (the
    // 151..227 median folder set) take the same path: their own alpha already says how much background shows.
    return itemGlyphs_.insert(key,source.convertToFormat(QImage::Format_ARGB32)).value();
}
void View::drawItemGlyph(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal opacity) {
    const auto& image=itemGlyph(container,texture);
    if(image.isNull())return;
    // The mask footprint is the art's own alpha bounds, taken from the source and cached by container/texture;
    // the prepared image carries the same alpha, so one bounds rect serves both (FIDELITY-ROOT-BAR I2/I3: the
    // box the caller passes is the measured ink width already, and drawItemGlyph must not shrink it further).
    const QString key=container+"/"+texture;
    auto bounds=imageBounds_.value(key);
    if(bounds.isNull()) {
        const QImage argb=assets_.bestImage(container,texture).convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<argb.height();++y){const auto* row=reinterpret_cast<const QRgb*>(argb.constScanLine(y));for(int x=0;x<argb.width();++x)if(qAlpha(row[x])>16)bounds=bounds.united(QRect(x,y,1,1));}
        if(bounds.isNull())bounds=assets_.bestImage(container,texture).rect();
        imageBounds_.insert(key,bounds);
    }
    const QSizeF size=QSizeF(bounds.size()).scaled(rect.size(),Qt::KeepAspectRatio);
    p.save();p.setOpacity(opacity);p.drawImage(QRectF(rect.center()-QPointF(size.width()/2,size.height()/2),size),image,bounds);p.restore();
}
void View::drawNineSlice(QPainter& p,const QString& container,const QString& texture,const QRectF& rect,qreal inset,qreal opacity,qreal sourceInset) {
    // tex_notification is a 64x64 tile: a dark fill with a lighter 1 px border and rounded corners. Scaling it
    // to the plaque's aspect would stretch the corner arcs into ellipses, so the 3x3 parts are drawn with the
    // corners at their own pixel size and only the edges and the centre stretched.
    const auto& image=assets_.image(container,texture);
    if(image.isNull())return;
    const qreal i=std::min<qreal>(inset,std::min(image.width(),image.height())/2.);
    if(rect.width()<2*i || rect.height()<2*i)return;
    // A tile may carry its own near-transparent margin inside its 64x64 canvas (the info bar's tex_indi_bg
    // does: a 4% alpha row/column at the top and bottom, FIDELITY-ROOT-BAR F4). Skipping it here keeps the
    // margin out of the drawn plate without changing the 8 px border's 1:1 pixel size.
    const qreal s=std::clamp<qreal>(sourceInset,0.,std::max<qreal>(0.,std::min(image.width(),image.height())/2.-i));
    const qreal sx[4]={s,i+s,qreal(image.width())-i-s,qreal(image.width())-s};
    const qreal sy[4]={s,i+s,qreal(image.height())-i-s,qreal(image.height())-s};
    const qreal dx[4]={rect.left(),rect.left()+i,rect.right()-i,rect.right()};
    const qreal dy[4]={rect.top(),rect.top()+i,rect.bottom()-i,rect.bottom()};
    p.save();p.setOpacity(opacity);
    for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
        const QRectF source(sx[col],sy[row],sx[col+1]-sx[col],sy[row+1]-sy[row]);
        const QRectF target(dx[col],dy[row],dx[col+1]-dx[col],dy[row+1]-dy[row]);
        if(source.width()<=0. || source.height()<=0. || target.width()<=0. || target.height()<=0.)continue;
        p.drawImage(target,image,source);
    }
    p.restore();
}
// The console's notification pop-up, composed from the firmware's own pieces (system_plugin page_notification,
// traced in out/native-reference/rco-behavior/resources.json and basic_plugins.prx FUN_0000773c):
//   notification_a / notification_b  the plaque Plane; its texture is tex_notification (64x64 rounded tile)
//   notification_icon_a / _b         the 100x100 tex_notification_<kind> glyph, one icon width left of the text
//   notification_message_a           the single-line Text, posX 1 (one icon width right of the icon centre)
//   notification_label0_b/_1_b/_2_b  the three-line Text set, posY 0.5 / -0.375 / -0.5
// The plaque's on-screen rect is not traced: paf projects the page from vsh.elf, which is absent here. It is
// fitted to the measured information panel (x 1329..1920, y 75..133), sitting under it with a 12 px gap.
void View::drawNotification(QPainter& p,double amount) {
    const double alpha=std::clamp(amount,0.,1.);
    if(notificationShown_.title.isEmpty() || alpha<=.002)return;
    const bool twoLines=!notificationShown_.detail.isEmpty();
    // Slide in from the right by 48 px while it fades: the console's pop-ups enter from the edge, not from
    // the centre, and the same curve carries them back out.
    const double slide=(1.-alpha)*48.;
    const qreal width=400.,height=twoLines?84.:58.;
    const QRectF plaque(1920.-24.-width+slide,145.,width,height);
    const auto& icon=assets_.bestImage("system_plugin",notificationShown_.icon);
    // The plaque first: the icon and the text sit on it.
    drawNineSlice(p,"system_plugin","tex_notification",plaque,16.,alpha*.94);
    if(!icon.isNull()) {
        // notification_icon_a/_b: the glyph fills the plaque's height less a 6 px margin, its centre 13 px in.
        const qreal side=plaque.height()-12.;
        drawImage(p,"system_plugin",notificationShown_.icon,
            QRectF(plaque.left()+13.,plaque.center().y()-side/2.,side,side),alpha);
    } else if(!notificationTextureWarned_) {
        notificationTextureWarned_=true;
        qWarning().noquote()<<"notify: texture missing system_plugin"<<notificationShown_.icon<<"(drawn without the icon)";
    }
    // Text starts one icon width to the right of the icon (posX 1 in the object tree). Without the glyph the
    // text keeps the plaque's own left margin rather than an invented icon-sized hole.
    const qreal textLeft=plaque.left()+(icon.isNull()?16.:13.+plaque.height()-12.+14.);
    const qreal textRight=plaque.right()-16.;
    const int titleSize=22,detailSize=18;
    QFont titleFont(shell_.fontFamily());titleFont.setPixelSize(titleSize);
    const QString title=QFontMetrics(titleFont).elidedText(notificationShown_.title,Qt::ElideRight,int(textRight-textLeft));
    const auto label=[&](const QRectF& rect,const QString& value,int size,QColor colour) {
        if(textRenderer_.ready()) {
            QFont font(shell_.fontFamily());font.setPixelSize(size);
            textRenderer_.draw(*this,p,rect,value,font,colour,Qt::AlignLeft|Qt::AlignVCenter);return;
        }
        QFont font(shell_.fontFamily());font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
        p.setPen(QColor(0,0,0,38));p.drawText(rect.translated(1,1),Qt::AlignLeft|Qt::AlignVCenter,value);
        p.setPen(colour);p.drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,value);
    };
    if(twoLines) {
        label(QRectF(textLeft,plaque.top()+10.,textRight-textLeft,titleSize+6.),title,titleSize,QColor(245,245,245));
        label(QRectF(textLeft,plaque.top()+10.+titleSize+4.,textRight-textLeft,detailSize+6.),
            notificationShown_.detail,detailSize,QColor(206,206,206));
    } else label(QRectF(textLeft,plaque.top()+8.,textRight-textLeft,plaque.height()-16.),title,titleSize,QColor(245,245,245));
}
// The operation guide behind "Triangle : Options" (audit/FIDELITY-ROOT-BAR.md 6.1 update 3), all TRACED:
//   explore_plugin 0x4D5B4 hands the focused item's hint string (its class getter returns msg_option_with_icon
//     or 0) to xmb_plugin's XMB2 interface slot +0x58 on every focus arrival, and 0 on every focus loss;
//   xmb_plugin 0x99E4 drops the shown guide at once, then arms a 1000 ms timer (DATA 0x18014 = 1000.f) whose
//     callback 0x9B24 creates the guide through vshcommon F55812AE (style 4) and gives it a 3000 ms life
//     (DATA 0x18018 = 3000.f, vshcommon F1918912);
//   vsh 0x346324 fades it in over 200 ms (DATA 0x68CC90) and schedules the hide at life + 200 ms; the hide
//     (0x345B88) fades it out over 200 ms (DATA 0x68CC70).
// So, from the focus arrival: nothing until 1000 ms, a 200 ms fade-in, full until 4200 ms, a 200 ms fade-out.
// The fade curve is drawn linear (INFERRED; the console burst's steps are only slightly accelerating).
// vsh's image font, the source of the firmware's gaiji (vsh.elf names /dev_flash/vsh/resource/imagefont.bin and
// logs "IMAGEFONT &#x%04x not registered."). Layout, read from the file: u16 version 0x0100, u16 count, u32 n (the
// data region's end), the zlib data from 8, then `count` 16-byte entries from n: u32 offset, u16 packed, u16 raw,
// u16 code, u16 w, u16 h, u16 flags. An entry's stream inflates to an 18-byte header and a 256-entry RGBA palette;
// the header's u16 at 8 / 10 is the (offset, packed size) of the glyph's w x h 8-bit index stream. The last entry
// ends exactly at the file's end. Cached per code; a null image when the file or the code is absent.
static QImage imageFontGlyph(const QString& flashRoot,ushort code) {
    static QHash<ushort,QImage> glyphs;static QByteArray file;static bool loaded=false;
    if(const auto hit=glyphs.constFind(code);hit!=glyphs.constEnd())return *hit;
    if(!loaded) {
        loaded=true;
        QFile f(flashRoot+"/vsh/resource/imagefont.bin");
        if(f.open(QIODevice::ReadOnly))file=f.readAll();
        else qWarning().noquote()<<"imagefont: cannot read"<<f.fileName();
    }
    const auto u16=[](const QByteArray& b,qsizetype p){return quint16((uchar(b[p])<<8)|uchar(b[p+1]));};
    const auto u32=[&](const QByteArray& b,qsizetype p){return (quint32(u16(b,p))<<16)|u16(b,p+2);};
    const auto unzip=[](const QByteArray& b,qsizetype p,qsizetype packed,quint32 expanded) {
        if(p<0 || packed<=0 || p+packed>b.size())return QByteArray();
        QByteArray framed(4,'\0');framed[0]=char(expanded>>24);framed[1]=char(expanded>>16);framed[2]=char(expanded>>8);
        framed[3]=char(expanded);framed+=b.mid(p,packed);
        return qUncompress(framed);
    };
    QImage glyph;
    if(file.size()>=8) {
        const quint32 n=u32(file,4);
        for(qsizetype o=n;o+16<=file.size();o+=16) {
            if(u16(file,o+8)!=code)continue;
            const int w=u16(file,o+10),h=u16(file,o+12);
            const QByteArray palette=unzip(file,u32(file,o),u16(file,o+4),u16(file,o+6));
            if(palette.size()<18+256*4)break;
            const QByteArray index=unzip(file,u16(palette,8),u16(palette,10),quint32(w*h));
            if(index.size()!=w*h)break;
            glyph=QImage(w,h,QImage::Format_ARGB32);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const int p=18+uchar(index[y*w+x])*4;
                glyph.setPixel(x,y,qRgba(uchar(palette[p]),uchar(palette[p+1]),uchar(palette[p+2]),uchar(palette[p+3])));
            }
            break;
        }
    }
    if(glyph.isNull())qWarning().noquote()<<"imagefont: no glyph"<<QString::number(code,16);
    glyphs.insert(code,glyph);
    return glyph;
}
double View::optionHintAmount() const {
    if(!shell_.focusedRowHasOptionHint())return 0.;
    const double t=double(focusTime());
    if(t<1000. || t>=4400.)return 0.;
    if(t<1200.)return (t-1000.)/200.;
    if(t<4200.)return 1.;
    return 1.-(t-4200.)/200.;
}
// system_plugin page_notification, vsh 0x345D24: tex_operation_guide_base_shadow (50x50) under
// tex_operation_guide_base (48x48), the shadow 2 px larger (the two textures' size difference), and the text.
// The layout is measured on the console (nav/users-column.png and nav2/game-02-down.png agree to the pixel):
// plate x 1654..1815, y 954..994; the Triangle gaiji's green ink at x 1678..1691, y 969..981; the white
// ": Options" ink at x 1702..1793, y 967..986. Its right/bottom anchor is the style-4 layout row, grid ids
// e7a/e7b = (860, -459) from the screen centre (TRACED, layout_grid_table_1080.txt), the plate ending
// 4-5 px inside it. The plate grows to the left with the text.
void View::drawOptionHint(QPainter& p,double amount,const QString& textOverride) {
    QString text=!textOverride.isEmpty()?textOverride:assets_.text("explore_plugin_full","msg_option_with_icon",QString(QChar(0xF883))+": Options");
    // The string's first character is the firmware's Triangle gaiji (U+F883), drawn from vsh's image font: a
    // 26 x 23 RGBA glyph (dark disc, pale rim, hollow green triangle). The console draws it at the text's
    // 21 px height (rim rows y 966 / 986), centred 17 px left of the text ink.
    const ushort gaijiCode=text.isEmpty()?0:text.front().unicode();
    const bool gaiji=gaijiCode>=0xE000 && gaijiCode<=0xF8FF;
    if(gaiji)text=text.mid(1);
    const int size=21;
    QFont font(shell_.fontFamily());font.setPixelSize(size);
    const double advance=QFontMetricsF(font).horizontalAdvance(text);
    const double plateRight=1816.,top=954.,height=41.,centreY=976.5;
    const double textLeft=plateRight-22.-advance;
    // textLeft is the advance origin; the ':' ink starts ~4 px later (x 1702 on both sides).
    const double discX=textLeft-12.5;
    const double plateLeft=gaiji?textLeft-52.5:textLeft-21.;
    const QRectF plate(plateLeft,top,plateRight-plateLeft,height);
    drawNineSlice(p,"system_plugin","tex_operation_guide_base_shadow",plate.adjusted(-1.,-1.,1.,1.),12.,amount);
    drawNineSlice(p,"system_plugin","tex_operation_guide_base",plate,12.,amount);
    if(gaiji) {
        const QImage glyph=imageFontGlyph(assets_.root(),gaijiCode);
        if(!glyph.isNull()) {
            const double h=size,w=glyph.width()*h/glyph.height();
            p.save();p.setOpacity(amount);p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRectF(discX-w/2.,centreY-h/2.,w,h),glyph);
            p.restore();
        }
    }
    const QRectF textRect(textLeft,top,advance+6.,height);
    if(textRenderer_.ready())
        textRenderer_.draw(*this,p,textRect,text,font,QColor(255,255,255,int(255.*amount)),Qt::AlignLeft|Qt::AlignVCenter);
    else {
        p.save();p.setOpacity(amount);p.setFont(font);p.setPen(QColor(255,255,255));
        p.drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter,text);p.restore();
    }
}
// user_plugin page_create: page_create_seat -> create_title_msg (Text), create_title_user_name (EditBox),
// create_btn_fix (Button). The RCO carries no positions (user_plugin lays them out), so the rects are measured on
// rpcs3-allgraphics-2026-10-28/frames/page-user-create.png:
//   prompt "Enter a user name." ink x 853..1065 (centred on 960), y 306..323
//   field x 555..1368, y 479..518; its text "User 1" ink from x 562, y 489..506
//   OK button x 930..989, y 554..597; "OK" ink x 944..974, y 566..583
//   hints: Cross ink 726..743 / "Edit" from 762, Circle ink 1155..1174 / "Back" from 1192, y 897..916
// Strings are user_plugin's own (msg_username_create, msg_fix, msg_edit, msg_back). The field and button
// textures are the paf defaults: system_plugin tex_default_text_field(_focus) and user_plugin's own tex_button
// (_focus).
void View::drawUserCreatePage(QPainter& p) {
    const auto label=[&](const QRectF& r,const QString& value,int size,int flags) {
        QFont font(shell_.fontFamily());font.setPixelSize(size);
        if(textRenderer_.ready()){textRenderer_.draw(*this,p,r,value,font,QColor(255,255,255),flags);return;}
        p.save();p.setFont(font);p.setPen(Qt::white);p.drawText(r,flags,value);p.restore();
    };
    const auto rco=[&](const char* key,const char* fallback){return assets_.text("user_plugin",key,fallback);};
    label({0,294,1920,42},rco("msg_username_create","Enter a user name."),23,Qt::AlignHCenter|Qt::AlignVCenter);
    // Each texture is drawn out by its own transparent margin so the visible edge lands on the measured one:
    // tex_default_text_field(_focus) is 27x27 with ink 1..25, user_plugin tex_button 60x60 with ink 7..52.
    // tex_button_shadow (opaque, RGB 0..17) is a mask whose paf blend is not traced, so it is not drawn.
    const QRectF field(555,479,814,40);
    drawNineSlice(p,"system_plugin",userCreate_.focus==0?"tex_default_text_field_focus":"tex_default_text_field",
                  field.adjusted(-1,-1,1,1),8.,1.);
    label(field.adjusted(10,0,-8,0),userCreate_.name,22,Qt::AlignLeft|Qt::AlignVCenter);
    const QRectF ok(930,554,60,44);
    drawNineSlice(p,"user_plugin",userCreate_.focus==1?"tex_button_focus":"tex_button",ok.adjusted(-7,-7,7,7),16.,1.);
    label(ok,rco("msg_fix","OK"),23,Qt::AlignHCenter|Qt::AlignVCenter);
    drawImage(p,"system_plugin","tex_cross",{726,898,18,18});
    // The Cross hint names the focused control's action: Edit on the field (the frame); on OK it is this build's
    // msg_enter, INFERRED (no frame shows OK focused).
    label({760,890,140,34},userCreate_.focus==0?rco("msg_edit","Edit"):rco("msg_enter","Enter"),21,Qt::AlignLeft|Qt::AlignVCenter);
    drawImage(p,"system_plugin","tex_circle",{1155,897,20,20});
    label({1190,890,140,34},rco("msg_back","Back"),21,Qt::AlignLeft|Qt::AlignVCenter);
}
bool View::userCreateInput(int action) {
    if(!userCreate_.open)return false;
    switch(action) {
    case 0:case 1:userCreate_.focus=action;break;
    case 2:
        if(userCreate_.focus==0)
            openOsk(assets_.text("user_plugin","msg_user_name","User Name"),userCreate_.name,[this](const QString& name){
                userCreate_.name=name;});
        else {
            const QString name=userCreate_.name.trimmed(),renameId=userCreate_.renameId;
            userCreate_=UserCreatePage{};
            if(!name.isEmpty()){if(renameId.isEmpty())shell_.createUser(name);else shell_.renameUser(renameId,name);}
        }
        break;
    case 3:userCreate_=UserCreatePage{};break;
    default:return false;
    }
    update();
    return true;
}
// ---- playlist_plugin's two-pane editor (audit/PLAYLIST-EDITOR.md) -------------------------------------------
// Evidence. The RCO (playlist_plugin, 11 objects) carries expage_select_list (one Text, msg_select_playlist) and
// expage_edit_list (no children): the two lists are built by plugin code, whose own strings name them edit_src_lst
// and edit_dst_lst, so every position below is INFERRED, not read from the RCO or measured on a frame. The
// textures are the plugin's (tex_playlist, tex_info_playlist, tex_cate_music/photo, tex_split_line, tex_arrow_r,
// tex_arrow_u); the help lines are its strings. Source pane = left, playlist pane = right (INFERRED from the
// order of the names and of msg_playlist_pane).
namespace {
constexpr int kEditorRows=12;
constexpr double kEditorRowHeight=56.;
constexpr double kEditorListTop=252.;
}
void View::openPlaylistEditor(const QString& id) {
    PlaylistEditor next;
    if(!shell_.playlistInfo(id,&next.name,&next.type,&next.items))return;
    next.id=id;next.library=shell_.playlistLibraryFiles(next.type);
    next.work=next.items;next.open=true;
    plEdit_=std::move(next);
    qInfo().noquote()<<"playlists: editor"<<plEdit_.name<<plEdit_.library.size()<<"source file(s)"<<plEdit_.items.size()<<"item(s)";
    update();
}
void View::drawPlaylistEditor(QPainter& p) {
    auto& e=plEdit_;
    const bool photo=e.type=="photo";
    const auto rco=[&](const char* key,const char* fallback){return assets_.text("playlist_plugin",key,fallback);};
    QFont font(shell_.fontFamily());font.setPixelSize(24);
    const auto label=[&](const QRectF& r,const QString& value,int size,int flags,const QColor& colour=Qt::white,bool shadow=true) {
        QFont f(shell_.fontFamily());f.setPixelSize(size);
        const QString shown=QFontMetricsF(f).elidedText(value,Qt::ElideRight,r.width());
        if(textRenderer_.ready()){textRenderer_.draw(*this,p,r,shown,f,colour,flags,0,shadow);return;}
        p.save();p.setFont(f);p.setPen(colour);p.drawText(r,flags,shown);p.restore();
    };
    const auto countText=[&](int n){
        return rco(photo?(n==1?"msg_image_count_value":"msg_images_count_value"):(n==1?"msg_track_count_value":"msg_tracks_count_value"),"%1").arg(n);
    };
    p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,120));   // the wallpaper stays; the wash keeps the lists legible
    const QRectF panes[2]={{140,0,760,0},{1020,0,760,0}};
    const bool working=e.picked;
    const QStringList& rightRows=working?e.work:e.items;
    // Headers: the source pane's kind icon and count; the playlist pane's icon, "Playlist (name)" and count.
    drawImage(p,"playlist_plugin",photo?"tex_cate_photo":"tex_cate_music",{140,138,64,64});
    label({220,140,640,34},rco(photo?"msg_images_count_value":"msg_tracks_count_value","%1").arg(e.library.size()),26,Qt::AlignLeft|Qt::AlignVCenter);
    drawImage(p,"playlist_plugin","tex_info_playlist",{1020,142,56,56});
    label({1096,140,684,34},rco("msg_playlist_playlist_name","Playlist (%1)").arg(e.name),26,Qt::AlignLeft|Qt::AlignVCenter);
    label({1096,176,684,28},countText(int(rightRows.size())),20,Qt::AlignLeft|Qt::AlignVCenter,QColor(255,255,255,170));
    // tex_split_line is 43x1 and fades at its ends, so it is stretched whole rather than fitted to its ink.
    for(const auto& pane:panes)p.drawImage(QRectF(pane.left(),230,pane.width(),2),assets_.image("playlist_plugin","tex_split_line"));
    drawImage(p,"playlist_plugin","tex_arrow_r",{937,520,46,43});   // 31x29 at the 1.5x layout scale, from the source pane towards the playlist
    const QSet<QString> listed(e.items.begin(),e.items.end());
    const auto drawPane=[&](int index,const QStringList& rows,int sel,int top,bool listedDim) {
        const QRectF pane=panes[index];
        const bool active=e.pane==index;
        for(int row=0;row<kEditorRows;++row) {
            const int at=top+row;
            if(at>=rows.size())break;
            QRectF r(pane.left(),kEditorListTop+row*kEditorRowHeight,pane.width(),kEditorRowHeight-6);
            const bool focus=at==sel;
            const bool lifted=focus && active && e.picked && index==1;
            if(lifted)r.translate(10,0);
            if(focus) {
                p.save();p.setPen(Qt::NoPen);
                // The label's own firmware text shadow (textRenderer_, 4/5 px right/down) is what read as a second
                // copy on a bright plate, so the lifted row is told apart by an outline over a plate no brighter
                // than the focus plate.
                p.setBrush(QColor(255,255,255,active?72:26));
                if(lifted)p.setPen(QPen(QColor(255,255,255,200),2));
                p.drawRoundedRect(r,6,6);p.restore();
            }
            const QString path=rows[at];
            const bool missing=index==1 && !QFileInfo::exists(path);
            const bool dim=(listedDim && listed.contains(path)) || missing;
            QString title=QFileInfo(path).completeBaseName();
            if(missing)title+=QStringLiteral("  (")+tr("missing")+QLatin1Char(')');
            label(r.adjusted(18,0,-14,0),title,24,Qt::AlignLeft|Qt::AlignVCenter,QColor(255,255,255,dim?110:255),!focus);   // the firmware text shadow reads as a second copy on the light focus plate
        }
        // More rows above / below: tex_arrow_u (25x12), flipped for the lower edge (INFERRED use).
        if(top>0)drawImage(p,"playlist_plugin","tex_arrow_u",{pane.center().x()-13,236,26,12});
        if(top+kEditorRows<rows.size()) {
            p.save();p.translate(pane.center().x(),kEditorListTop+kEditorRows*kEditorRowHeight+8);p.scale(1,-1);
            drawImage(p,"playlist_plugin","tex_arrow_u",{-13,-6,26,12});p.restore();
        }
    };
    drawPane(0,e.library,e.libSel,e.libTop,true);
    drawPane(1,rightRows,e.listSel,e.listTop,false);
    // The picked-up row's own arrows: it moves up and down until Cross drops it.
    if(e.picked && e.listSel>=e.listTop && e.listSel<e.listTop+kEditorRows) {
        const double y=kEditorListTop+(e.listSel-e.listTop)*kEditorRowHeight;
        drawImage(p,"playlist_plugin","tex_arrow_u",{1780-30,y-12,26,12});
        p.save();p.translate(1780-17,y+kEditorRowHeight-2);p.scale(1,-1);drawImage(p,"playlist_plugin","tex_arrow_u",{-13,-6,26,12});p.restore();
    }
    if(rightRows.isEmpty())
        label({1020,kEditorListTop,760,kEditorRowHeight},rco("msg_error_no_content","There are no content items."),22,Qt::AlignHCenter|Qt::AlignVCenter,QColor(255,255,255,150));
    // Hint line from the plugin's own strings: its first glyph is Cross as %E, Square U+F882 and Triangle U+F883
    // (the same code points explore_plugin pairs with Group Content and Options). U+F88F, the Play/View/Pause line, is
    // not drawn: starting playback from the editor is not implemented here.
    const QString key=e.picked?QStringLiteral("msg_help_enter")
        :e.pane==0?(photo?QStringLiteral("msg_help_add_option_view_playlist"):QStringLiteral("msg_help_add_option_play_playlist"))
                  :(photo?QStringLiteral("msg_help_move_delete_view_playlist"):QStringLiteral("msg_help_move_delete_play_playlist"));
    QString help=assets_.text("playlist_plugin",key.toUtf8().constData(),"");
    help.replace(QStringLiteral("\\n"),QStringLiteral("\n"));
    double x=640;
    for(const QString& line:help.split(QLatin1Char('\n'),Qt::SkipEmptyParts)) {
        const int colon=line.indexOf(QLatin1Char(':'));
        if(colon<0)continue;
        const QString glyph=line.left(colon).trimmed(),words=line.mid(colon+1).trimmed();
        const QChar g=glyph.isEmpty()?QChar():glyph.at(glyph.size()-1);
        const char* texture=glyph.startsWith(QLatin1String("%E"))?"tex_cross":g.unicode()==0xF882?"tex_rectangle":g.unicode()==0xF883?"tex_triangle":nullptr;
        if(!texture)continue;
        drawImage(p,"system_plugin",texture,{x,978,22,22});
        QFont f(shell_.fontFamily());f.setPixelSize(21);
        const double w=QFontMetricsF(f).horizontalAdvance(words);
        label({x+32,972,w+8,34},words,21,Qt::AlignLeft|Qt::AlignVCenter);
        x+=32+w+48;
    }
    drawImage(p,"system_plugin","tex_circle",{x,978,22,22});
    label({x+32,972,200,34},assets_.text("explore_plugin_full","msg_back","Back"),21,Qt::AlignLeft|Qt::AlignVCenter);
    if(!e.status.isEmpty())
        label({140,908,1640,40},e.status,22,Qt::AlignHCenter|Qt::AlignVCenter,QColor(255,224,160));
    // Options plate (Triangle in the source pane): one row, Add All (msg_add_all_playlist). INFERRED look.
    if(e.optionsOpen) {
        const QRectF plate(560,330,340,84);
        drawNineSlice(p,"system_plugin","tex_operation_guide_base",plate.adjusted(-6,-6,6,6),12.,1.);
        p.save();p.setPen(Qt::NoPen);p.setBrush(QColor(255,255,255,80));p.drawRoundedRect(plate.adjusted(10,14,-10,-14),6,6);p.restore();
        label(plate.adjusted(28,0,-20,0),rco("msg_add_all_playlist","Add All"),24,Qt::AlignLeft|Qt::AlignVCenter);
    }
    // msg_quit_playlist_edit_ask with msg_yes / msg_no; No is the default, as showConfirm's boxes start.
    if(e.confirmQuit) {
        p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,140));
        const QRectF plate(560,400,800,240);
        p.save();p.setPen(Qt::NoPen);p.setBrush(QColor(22,20,18,242));p.drawRect(plate);p.restore();   // opaque: the lists must not read through the question
        drawNineSlice(p,"system_plugin","tex_operation_guide_base",plate,12.,1.);
        label(plate.adjusted(30,26,-30,-110),rco("msg_quit_playlist_edit_ask","Do you want to quit editing?"),26,Qt::AlignHCenter|Qt::AlignVCenter);
        const QString answers[2]={rco("msg_no","No"),rco("msg_yes","Yes")};
        for(int i=0;i<2;++i) {
            const QRectF button(plate.left()+160+i*270,plate.bottom()-98,220,56);
            const int idx=i==0?0:1;   // left button No, right button Yes
            if(e.confirmFocus==idx){p.save();p.setPen(Qt::NoPen);p.setBrush(QColor(255,255,255,90));p.drawRoundedRect(button,6,6);p.restore();}
            label(button,answers[i],24,Qt::AlignHCenter|Qt::AlignVCenter);
        }
    }
}
bool View::playlistEditorInput(int action) {
    auto& e=plEdit_;
    if(!e.open)return false;
    e.status.clear();
    const auto clampView=[](int sel,int& top,int count) {
        sel=std::clamp(sel,0,std::max(0,count-1));
        if(sel<top)top=sel;
        if(sel>=top+kEditorRows)top=sel-kEditorRows+1;
        top=std::clamp(top,0,std::max(0,count-kEditorRows));
        return sel;
    };
    const auto reload=[&] {
        QStringList items;
        if(shell_.playlistInfo(e.id,nullptr,nullptr,&items))e.items=items;
        e.work=e.items;
        e.listSel=clampView(e.listSel,e.listTop,int(e.items.size()));
    };
    const auto say=[&](PlaylistStore::Result r){e.status=shell_.playlistResultText(r);};
    if(e.confirmQuit) {
        switch(action) {
        case 2:e.confirmFocus=0;break;case 3:e.confirmFocus=1;break;
        case 4:
            if(e.confirmFocus==1){e=PlaylistEditor{};qInfo()<<"playlists: editor closed";}
            else e.confirmQuit=false;
            break;
        case 5:e.confirmQuit=false;break;
        default:break;
        }
        update();return true;
    }
    if(e.optionsOpen) {
        if(action==4) {
            int added=0;
            const auto result=shell_.playlistEditAdd(e.id,e.library,&added);
            e.optionsOpen=false;
            if(result==PlaylistStore::Result::Ok){reload();e.status=tr("Added %1 of %2 files.").arg(added).arg(e.library.size());}
            else say(result);
        } else if(action==5 || action==6)e.optionsOpen=false;
        update();return true;
    }
    switch(action) {
    case 0:case 1: {
        const int step=action==0?-1:1;
        if(e.pane==0)e.libSel=clampView(e.libSel+step,e.libTop,int(e.library.size()));
        else if(e.picked) {
            // The picked row travels through the working order; nothing is saved until the drop.
            const int to=std::clamp(e.listSel+step,0,int(e.work.size())-1);
            if(to!=e.listSel){e.work.move(e.listSel,to);e.listSel=clampView(to,e.listTop,int(e.work.size()));}
        } else e.listSel=clampView(e.listSel+step,e.listTop,int(e.items.size()));
        break;
    }
    case 2:if(!e.picked)e.pane=0;break;
    case 3:if(!e.picked && !e.items.isEmpty()){e.pane=1;e.listSel=clampView(e.listSel,e.listTop,int(e.items.size()));}break;
    case 4:
        if(e.pane==0) {
            if(e.library.isEmpty())break;
            int added=0;
            const auto result=shell_.playlistEditAdd(e.id,{e.library[e.libSel]},&added);
            if(result==PlaylistStore::Result::Ok)reload();else say(result);
        } else if(e.picked) {
            // Drop: one saved move from where the row was picked up to where it now stands.
            if(e.listSel!=e.pickFrom) {
                const auto result=shell_.playlistEditMove(e.id,e.pickFrom,e.listSel);
                if(result!=PlaylistStore::Result::Ok)say(result);
            }
            e.picked=false;reload();
        } else if(!e.items.isEmpty()){e.picked=true;e.pickFrom=e.listSel;e.work=e.items;}
        break;
    case 5:
        if(e.picked){e.picked=false;e.listSel=clampView(e.pickFrom,e.listTop,int(e.items.size()));e.work=e.items;}
        else{e.confirmQuit=true;e.confirmFocus=0;}
        break;
    case 6:if(e.pane==0 && !e.library.isEmpty())e.optionsOpen=true;break;
    case 7:
        if(e.pane==1 && !e.picked && !e.items.isEmpty()) {
            const auto result=shell_.playlistEditRemove(e.id,e.listSel);
            if(result==PlaylistStore::Result::Ok){reload();if(e.items.isEmpty())e.pane=0;}else say(result);
        }
        break;
    default:return false;
    }
    update();
    return true;
}
void View::drawCategory(QPainter& p,const Category& category,const QRectF& rect) {
    const auto& original=assets_.bestImage(category.container,category.texture);
    if(original.isNull())return;
    // Theme material changes RGB, never the coverage/alpha of the original icon.
    // Full-colour PSN artwork retains its original colours.
    const int theme=std::clamp(shell_.theme(),0,5);
    const QString key=category.container+"/"+category.texture+"/"+QString::number(theme);
    if(!iconMaterials_.contains(key)) {
        QImage material=original.convertToFormat(QImage::Format_ARGB32);
        QRect bounds;
        // Icons are near-white over every palette (reference video): the colour comes from the firmware background.
        const QColor tint(238,240,242);
        for(int y=0;y<material.height();++y) {
            auto* row=reinterpret_cast<QRgb*>(material.scanLine(y));
            for(int x=0;x<material.width();++x) {
                const auto pixel=row[x];
                if(category.container=="qgl/icontex") {
                    // Native material reconstructed from the stored normals.
                    // Original RSX reflection/refraction shaders are not ported.
                    const QVector3D normal(qRed(pixel)/127.5f-1.f,qGreen(pixel)/127.5f-1.f,qBlue(pixel)/127.5f-1.f);
                    const float diffuse=std::max(0.f,QVector3D::dotProduct(normal.normalized(),QVector3D(-.35f,.40f,.85f).normalized()));
                    const float light=.68f+.32f*diffuse;
                    row[x]=qRgba(int(tint.red()*light),int(tint.green()*light),int(tint.blue()*light),qAlpha(pixel));
                } else if(category.id!="psn")row[x]=qRgba(qRed(pixel)*tint.red()/255,qGreen(pixel)*tint.green()/255,qBlue(pixel)*tint.blue()/255,qAlpha(pixel));
                if(qAlpha(pixel)>16)bounds=bounds.united(QRect(x,y,1,1));
            }
        }
        iconMaterials_.insert(key,IconMaterial{std::move(material),bounds});
    }
    const auto& cached=iconMaterials_[key];const auto& image=cached.image;const auto& bounds=cached.bounds;
    if(bounds.isEmpty())return;
    const QSizeF size=QSizeF(bounds.size()).scaled(rect.size(),Qt::KeepAspectRatio);
    p.drawImage(QRectF(rect.center()-QPointF(size.width()/2,size.height()/2),size),image,bounds);
}
namespace {
// An image subtitle (PGS / VobSub picture, image_subtitles.h) over the video. The cue's rect is in the
// stream's own frame (ImageCue::videoSize); that frame is mapped onto the displayed video rectangle:
//   - HD streams are scaled by width and centred vertically on the video, so a 1920x1080 PGS frame over
//     a cropped 1920x1038 (or 1920x800 scope) encode keeps its authored position relative to the picture
//     centre, the same way a player composites it before the crop;
//   - SD (DVD, width <= 720) frames are anamorphic, so they are stretched on both axes to the video.
// When both frames have the same aspect the two rules agree. Only the screen clips the result, so a
// cue authored in the letterbox area stays visible there.
void drawImageSubtitle(QPainter& p,const ImageCue& cue,const QRectF& video) {
    if(cue.image.isNull() || cue.videoSize.isEmpty() || video.isEmpty())return;
    // The upload is the expensive part (palette -> premultiplied ARGB), so it happens once per picture.
    static qint64 key=0;
    static QImage converted;
    if(key!=cue.image.cacheKey() || converted.isNull()) {
        converted=cue.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        key=cue.image.cacheKey();
    }
    const double sx=video.width()/cue.videoSize.width();
    const double sy=cue.videoSize.width()<=720?video.height()/cue.videoSize.height():sx;
    const double frameTop=cue.videoSize.width()<=720?video.top():video.center().y()-cue.videoSize.height()*sy/2;
    const QRectF target(video.left()+cue.rect.x()*sx,frameTop+cue.rect.y()*sy,cue.rect.width()*sx,cue.rect.height()*sy);
    p.save();
    p.setClipRect(QRectF(0,0,1920,1080));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target,converted);
    p.restore();
}
}
void View::drawSubtitle(QPainter& p,const QString& cue) {
    // Console subtitle presentation, in the XMB's own text pipeline: the same GL glyph atlas (shaped by
    // Qt, rasterised through FontRaster/FreeType) the menus use, white on a dark outline so a cue stays
    // readable over a bright frame. The band is centred in the lower third but stops at y 850: the
    // player overlay draws its queue line at 895, the progress bar at 941 and the title at 970, so a cue
    // never lands on the controls, and the long line wraps upward from its bottom edge.
    const QRectF band(210,650,1500,200);
    const int size=32;
    const int flags=Qt::AlignHCenter|Qt::AlignBottom|Qt::TextWordWrap;
    const QColor fill(248,248,248),outline(0,0,0,215);
    if(textRenderer_.ready()) {
        QFont font(shell_.fontFamily());font.setPixelSize(size);
        // Four diagonal passes for the outline, then the fill. Whole-pixel offsets, so the per-phase
        // glyph tiles are reused rather than re-rasterised.
        for(const auto& offset:{QPointF(-2,-2),QPointF(2,-2),QPointF(-2,2),QPointF(2,2)})
            textRenderer_.draw(*this,p,band.translated(offset),cue,font,outline,flags,0,false);
        textRenderer_.draw(*this,p,band,cue,font,fill,flags,0,true);
        return;
    }
    QFont font(shell_.fontFamily());font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
    p.setPen(outline);
    for(const auto& offset:{QPointF(-2,-2),QPointF(2,-2),QPointF(-2,2),QPointF(2,2)})
        p.drawText(band.translated(offset),flags,cue);
    p.setPen(fill);p.drawText(band,flags,cue);
}
namespace {
// --- The firmware band (Phase 11.3) -----------------------------------------------------------------
// One drawing, two parameter sets. On the console a settings choice list and the item options menu are
// the same full-height right-hand band over the page (SETTINGS 9.3a: the band is the firmware's, not
// this build's); they differ in their edge, their row pitch and where their rows sit. Every number
// below is measured on the captured frame named under each set, and the fill stops are the band's own
// white alpha sampled on a row where the page behind is black, so the luma is the alpha
// (audit/SETTINGS-FIRMWARE.md 13.3 records the profiles and the two frame references).
struct BandStop { double at; int alpha; };   // `at` is a fraction of x0..fadeEnd
struct PanelBand {
    bool firmware=false;      // false: this build's earlier options panel, unchanged
    double x0=1330.;          // settled left edge of the fill
    double fadeEnd=1920.;     // geometric zero of the fill
    double pitch=40.;         // row pitch
    bool fixedRows=false;     // rows are anchored at rowCentre and do not travel with focus
    double rowCentre=505.;    // fixed rows: row 0's text centre; scrolling list: the focus row's centre
    double labelInset=16.;    // row text left = x0 + labelInset
    double groupGap=0.;       // fixed rows: distance up to a divider group's origin
    // The submenu chevron's left edge. Two measured columns: the settings bands' 1786 (the Background
    // list's `Brightness` row) and the item menu's 1573 (frames/menu-options-open.png, on both of its
    // submenu rows). 11x10 px, on the row's centre line, both sides.
    double chevronX=1786.;
    int groupBreakRow=0;      // fixed rows: rows before this index are the upper group (a band has no MenuOption)
    double contentDim=1.;     // brightness of the page the band covers, at rest (measured)
    const BandStop* stops=nullptr;
    int stopCount=0;
    // The firmware's own background plate, when the container carries one: the item options menu's fill is
    // `system_plugin:tex_optionmenu_bg` (536x216), not a ramp this build fits. `plateName` null keeps the
    // stop profile. The rect and the object alpha are measured (kOptionsBand's comment).
    const char* plateContainer=nullptr;
    const char* plateName=nullptr;
    double plateX=0.,plateY=0.,plateW=0.,plateH=0.,plateOpacity=1.;
};
// settings/screensaver-list-rest.png, y=100 (the page behind is black there, so the luma is the fill's
// alpha), sampled column by column: 0 at <=1259, 15 at 1260 (the plane's antialiased edge), 32 at 1261
// and 34 at 1262 (its 2 px bright step), 23 at 1263, 27 at 1265, 37 at 1270, 45 at 1275, 50 at 1280,
// 57 at 1290, 60 at 1300, 61 across 1303-1330, 60 at 1340, 53 at 1400, 45 at 1450, 37 at 1500, 25 at
// 1550, 9 at 1600, 2 at 1650, 0 at 1690. The committed hunk's 0/.005 = 0/60 stops would put 60 at x1262
// and over-brighten 1263-1299 by up to 35 luma; the measured profile is used instead and the
// correction is dated in SETTINGS 13.3.
const BandStop kChoiceBandStops[]={
    {0.,15},{.00233,32},{.00465,34},{.00698,23},{.01163,27},{.02326,37},{.03488,45},{.04651,50},
    {.06977,57},{.09302,60},{.1,61},{.13953,61},{.16279,61},{.18605,60},{.32558,53},{.44186,45},
    {.55814,37},{.67442,25},{.7907,9},{.90698,2},{1.,0}};
// dialogs/options-open-01.png, y=100, the same column-by-column read: 15 at 1320, 34 at 1322 (the step),
// 25 at 1324, 30 at 1326, 38 at 1330, 47 at 1336, 51 at 1340, 56 at 1346, 59 at 1352, 61 at 1360,
// 62 across 1366-1388, 61 to 1396, 60 at 1400, 58 at 1420, 55 at 1450, 48 at 1500, 39 at 1550, 28 at
// 1600, 12 at 1650, 4 at 1690, 2 at 1700, 0 at 1742.
const BandStop kOptionsBandStops[]={
    {0.,15},{.00474,34},{.00948,25},{.01422,30},{.0237,38},{.03791,47},{.04739,51},{.06161,56},
    {.07583,59},{.09479,61},{.109,62},{.16588,61},{.18957,60},{.23697,58},{.30806,55},{.42654,48},
    {.54502,39},{.66351,28},{.78199,12},{.87678,4},{.90047,2},{1.,0}};
// The settings choice list: SETTINGS 9.1/U2 (left edge 1260, 40 px pitch, the focused item centred on
// the 505 focus row, item text at 1276, no check and no dot) and 11.6 (the fill's zero at 1690). The page
// behind renders at 0.52 of its own brightness while the band is up: the parent's white text reads 255
// with no band and 136 on batch 1 (0.533) and 131 on the batch-2 Theme band (0.513), while 12.3's
// Screensaver still reads 104 (0.408) - a measured bracket, so 0.52 is a fit inside it. The category band
// and the background are bit-identical with and without the band (the briefcase icon reads 123.2 vs
// 123.1 luma), so the dim belongs to the page and not to a plate over the frame (SETTINGS 13.2).
// The Theme page's own fill profile (SETTINGS 12.3, re-measured on the matched frame): border step
// 31 at 1324-1325, peak 55-56 at about 1400, then 44 @1500, 35 @1560, 27 @1600, 15 @1640, 6 @1680,
// 1 @1720, 0 @1744. It is not the Screensaver band's 61-peak curve, so the two lists do not share
// one ramp; the stops below are the Theme band's own fractions of 1324..1744.
const BandStop kThemeBandStops[]={
    {0.,20},{.00238,31},{.00952,26},{.02381,34},{.04762,42},{.09524,50},{.181,56},
    {.31,53},{.41905,44},{.5619,35},{.65714,27},{.75238,15},{.84762,6},{.94286,1},{1.,0}};
const PanelBand kChoiceBand{.firmware=true,.x0=1260.,.fadeEnd=1690.,.pitch=40.,.fixedRows=false,
    .rowCentre=505.,.labelInset=16.,.contentDim=.40,.stops=kChoiceBandStops,
    .stopCount=int(std::size(kChoiceBandStops))};
// The Theme page's choice bands are NOT the Screensaver band. All four Theme-page lists share ONE
// plane: theme-list, color-list, background-list and font-list give the same fill delta over the
// parent page at y=100 (25 at x1340, 35 at 1380-1420, 33 at 1460, 30 at 1500, 27 at 1540, 38 at
// 1620, 17 at 1700, 6 at 1740, 1-3 to the right edge), so the plane is a page-class band and only
// the ROW ORIGIN differs per list (SETTINGS 12.4e). The plane's own step is at x 1324-1325 (77 -> 85
// -> 97 luma over three columns), its row text at x 1340-1341 (border + 17) and it is 64 px right of
// the Display page's band - 9.1/U2's 1260 is page-specific, which 12.3(a) already records.
// Theme's rows: ink tops 261 (Install), 499 (Original, focused), 541 (Classic) and 578 (Air Paint).
// The utility row sits 239 px above the first theme row - the item menu's group-gap language (E1) -
// and the rows are anchored (they do not travel with focus). SETTINGS 12.4(e) forbids hardcoding
// `505-(n-1)*20`; this anchors the measured origin and keeps the measured 40 px pitch.
const PanelBand kThemeBand{.firmware=true,.x0=1324.,.fadeEnd=1744.,.pitch=40.,.fixedRows=true,
    .rowCentre=505.,.labelInset=17.,.groupGap=240.,.groupBreakRow=1,.contentDim=.40,
    .stops=kThemeBandStops,.stopCount=int(std::size(kThemeBandStops))};
// Colour and Font anchor their first row on the same 505 focus row (Font's ink tops 497/539/579,
// Colour's value line 494), so they are the Theme band without a utility group.
const PanelBand kThemeLowerListBand{.firmware=true,.x0=1324.,.fadeEnd=1744.,.pitch=40.,.fixedRows=true,
    .rowCentre=505.,.labelInset=17.,.contentDim=.40,.stops=kThemeBandStops,
    .stopCount=int(std::size(kThemeBandStops))};
// Background is the one Theme-page list whose block sits lower: its matched frame reads `Original`
// 539, `Classic` 581 and `Wallpaper` 621 as ink tops, against this build's 494/536/576, i.e. +45 px
// on the same 40 px pitch (SETTINGS 12.4(e)'s 43 px difference, re-measured here).
const PanelBand kBackgroundBand{.firmware=true,.x0=1324.,.fadeEnd=1744.,.pitch=40.,.fixedRows=true,
    .rowCentre=545.,.labelInset=17.,.groupGap=160.,.groupBreakRow=1,.contentDim=.40,.stops=kThemeBandStops,
    .stopCount=int(std::size(kThemeBandStops))};
// The settings lists whose band geometry a matched frame measures, by Preferences key (SETTINGS
// 11.3/12.4). Null means "not measured here": that list keeps the band or panel it had, so no other
// page or surface moves.
const PanelBand* pageBandForSetting(const QString& setting) {
    if(setting==QLatin1String("appearance/theme"))return &kThemeBand;
    if(setting==QLatin1String("appearance/colour")||setting==QLatin1String("appearance/font"))
        return &kThemeLowerListBand;
    if(setting==QLatin1String("appearance/background"))return &kBackgroundBand;
    return nullptr;
}
// A firmware-labelled list falls back to kChoiceBand (the Display page's measured band); a list this
// build labels itself keeps its own panel unless a frame measures a Theme-page band for it.
const PanelBand* bandForSetting(const QString& setting) {
    const PanelBand* page=pageBandForSetting(setting);
    return page?page:&kChoiceBand;
}
// The item options menu: ITEM-MENU-INFO-FIRMWARE E1 measures baselines 560/598/636 at a 38 px pitch and
// row text at 1334. E1's band numbers were approximate ("edge ~1330, fading to nothing by ~1690"); the
// frame's own columns are exact - the first lit column is 1320 (a 2 px step, 15 at 1320 and 34 at 1322),
// the fill peaks at 62 and the last lit column is 1741 - so the measured edge and end are used, and the
// text column stays at E1's 1334 (inset 14 from the measured edge, not the 4 the approximate 1330 would
// give). Re-measured on out/rpcs3-allgraphics-2026-10-28/frames/menu-options-open.png this pass: the band's
// left edge is x 1320 (84.8 at 1315 -> 89.9 at 1320 -> 108 peak at 1370) and the label ink starts at x 1336,
// i.e. pen 1334 + the glyph's 2 px bearing on both sides - FIDELITY-SURFACES gap 4's "+66/+73 px" does not
// reproduce on this build (it was measured on the pre-plate binary), while the ROW GRID does move: the
// frame's five rows have ink tops 301/341 (utility group) and 540/581/621 (action group), i.e. centres
// 311.5/351.5 and 549.5/590.5/630.5 - a 40.5 px pitch on the first action row 549.5, not E1's 38 px at 552.
// The group gap that reproduces the frame is therefore 238: 549.5 + 40.5 - 238 = 352 (row 2 of the utility
// group, frame 351.5) and 549.5 - 238 = 311.5 (its row 1). [TRACED: the edge, the end, the pen, the five ink
// tops. FIT: nothing - the three values are the frame's own arithmetic.]
// The rows do not scroll (the firmware's menu carries no scroll object) and are anchored on the
// first baseline; drawing centres the em box 8 px above that baseline, which is the offset measured
// between the rect centre and the ink bottom on this build's capture (SETTINGS 13.5).
// The fill is the firmware's own plate (ICON-GAP-AUDIT B5), not this build's four-stop fit.
// Measured on dialogs/options-open-01.png against the same session's band-less frame
// (options-state-before.png) in a row where the page behind is black: the texture maps 1:1 horizontally at
// x 1320 (536 px per 536 texels; a least-squares fit of the frame's y=100 profile to the texture's row gives
// exactly 1.000 px/texel and x0 1320, residual MSE 0.83 luma) and its 216 rows are stretched over the full
// 1080 height (at y 100 the frame is 62 where texture row 20 peaks at 185/255; at y 280 it is 64 where row
// 56 reads 190). The object alpha is 0.33: the texture's 207 peak * 0.33 = 68, the frame's 68.3 where the
// plate sits on a 6.7 page, and its 46 at x 1320 * 0.33 = 15.2 against the frame's measured 15.3. The stops
// below stay for every other band; the options band no longer uses them.
const PanelBand kOptionsBand{.firmware=true,.x0=1320.,.fadeEnd=1742.,.pitch=40.5,.fixedRows=true,
    .rowCentre=549.5,.labelInset=14.,.groupGap=238.,.chevronX=1573.,.contentDim=.545,.stops=kOptionsBandStops,
    .stopCount=int(std::size(kOptionsBandStops)),.plateContainer="system_plugin",
    .plateName="tex_optionmenu_bg",.plateX=1320.,.plateY=0.,.plateW=536.,.plateH=1080.,
    .plateOpacity=.33};
// The PS menu's selected-entry bloom. The firmware draws the focused row as white glyphs inside a
// Gaussian halo: in the 4.93 frame (a21-ps-menu-1.png) the halo measures 106 luma 3 px above the cap,
// 59 at 6 px, 27 at 9 px and 10 at 12 px, i.e. coverage blurred with sigma ~5 px. The text atlas halo
// (TEXT-RENDERER haloSigma 2.2) is right for the item labels but too tight here, so the menu draws its
// own: the label into an 8-bit mask, three box passes (the standard 3-box Gaussian approximation) and a
// white ARGB image composited under the ink. The mask is rendered at the layer's own scale, so the
// blur is five scene pixels wide whatever the capture size.
void boxBlurGray(QImage& image,int radius) {
    if(radius<1 || image.isNull())return;
    const int w=image.width(),h=image.height(),window=2*radius+1;
    QImage horizontal(image.size(),QImage::Format_Grayscale8);
    for(int y=0;y<h;++y) {
        const uchar* in=image.constScanLine(y);uchar* out=horizontal.scanLine(y);
        int sum=0;
        for(int x=-radius;x<=radius;++x)sum+=in[std::clamp(x,0,w-1)];
        for(int x=0;x<w;++x) {
            out[x]=uchar(sum/window);
            sum-=in[std::clamp(x-radius,0,w-1)];
            sum+=in[std::clamp(x+radius+1,0,w-1)];
        }
    }
    for(int x=0;x<w;++x) {
        int sum=0;
        for(int y=-radius;y<=radius;++y)sum+=horizontal.constScanLine(std::clamp(y,0,h-1))[x];
        for(int y=0;y<h;++y) {
            image.scanLine(y)[x]=uchar(sum/window);
            sum-=horizontal.constScanLine(std::clamp(y-radius,0,h-1))[x];
            sum+=horizontal.constScanLine(std::clamp(y+radius+1,0,h-1))[x];
        }
    }
}
void drawTextBloom(QPainter& p,const QFont& font,const QRectF& rect,const QString& value,double strength) {
    if(rect.isEmpty() || value.isEmpty())return;
    const double scale=std::max(1.,p.transform().m11());
    const int radius=std::max(2,int(std::lround(5.*scale)));
    const int pad=radius*3;
    const QSize size(int(std::ceil(rect.width()*scale))+2*pad,int(std::ceil(rect.height()*scale))+2*pad);
    if(size.width()<=0 || size.height()<=0 || size.width()>8192 || size.height()>2048)return;
    QImage mask(size,QImage::Format_Grayscale8);mask.fill(0);
    {
        QPainter mp(&mask);mp.setRenderHint(QPainter::TextAntialiasing);
        QFont f=font;f.setPixelSize(std::max(1,int(std::lround(double(font.pixelSize())*scale))));
        mp.setFont(f);mp.setPen(Qt::white);
        mp.drawText(QRectF(pad,pad,rect.width()*scale,rect.height()*scale),Qt::AlignLeft|Qt::AlignVCenter,value);
    }
    for(int i=0;i<3;++i)boxBlurGray(mask,radius);
    QImage glow(size,QImage::Format_ARGB32_Premultiplied);
    for(int y=0;y<size.height();++y) {
        const uchar* in=mask.constScanLine(y);auto* out=reinterpret_cast<QRgb*>(glow.scanLine(y));
        for(int x=0;x<size.width();++x) {
            const int alpha=std::clamp(int(double(in[x])*strength),0,255);
            out[x]=qRgba(alpha,alpha,alpha,alpha);
        }
    }
    p.save();p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(rect.x()-double(pad)/scale,rect.y()-double(pad)/scale,
        double(size.width())/scale,double(size.height())/scale),glow);
    p.restore();
}
}
const View::ChoiceBand* View::choiceBandFor(const QString& setting) {
    if(setting.isEmpty())return nullptr;
    if(!choiceBandsBuilt_) {
        // One read of the committed choice model per process (settings_menu.h): every settings page's
        // choice rows with the firmware's own labels, order and values. The model reads
        // sysconf_plugin.rco once and only on a run that draws a choice list, which is why this is lazy
        // rather than part of start-up.
        choiceBandsBuilt_=true;
        int lists=0,items=0;
        const auto catalogue=settingsChoiceCatalogue(assets_);
        for(const auto& list:catalogue) {
            if(list.setting.isEmpty()||list.items.isEmpty()||choiceBands_.contains(list.setting))continue;
            ChoiceBand band;
            band.setting=list.setting;band.pageId=list.pageId;band.rowId=list.rowId;
            band.rcoPage=list.rcoPage;band.source=list.source;band.note=list.note;
            band.scrollable=list.scrollable;
            for(const auto& item:list.items) {
                band.labels<<item.label;band.values<<item.value;band.writable<<item.writes;
                band.swatch<<item.swatch;band.colours<<item.colour;
                band.submenu<<item.submenu;band.notes<<item.note;
            }
            band.windowRows=list.windowRows;
            choiceBands_.insert(list.setting,band);++lists;items+=int(band.labels.size());
        }
        qInfo().noquote()<<QStringLiteral("view: settings choice model -> %1 lists, %2 items (labels from the firmware's own pages)")
            .arg(lists).arg(items);
    }
    const auto found=choiceBands_.constFind(setting);
    return found==choiceBands_.cend()?nullptr:&*found;
}
int View::choiceBandFocus(const ChoiceBand& band,const QVector<Entry>& model,int selection,
                          const QString& currentText) const {
    const int count=int(band.labels.size());
    if(count<=0)return 0;
    if(model.isEmpty())return 0;
    const int picked=std::clamp(selection,0,int(model.size())-1);
    // (1) The model's selected item, by the value it commits. This is what keeps the focused label and
    // the committed value paired when the RCO's authored order differs from choices()'s (Date Format and
    // Time Format: SETTINGS 11.3/11.6), and it is also the resting focus, because openChoices() starts
    // the page's selection on the current value. Phase 11.4 drops the writable gate: a disabled Theme chip
    // is a real row the cursor can reach (its value is the model's own sentinel), and the band must focus
    // it so its reason can be read - `writable` is still what dims it.
    for(int i=0;i<count;++i)
        if(choiceValueMatches(model[picked].option,band.values.value(i)))return i;
    // (2) A row whose stored value is not one of the model's own values - the time zone, whose firmware
    // items are GMT-offset tokens while the store holds a host IANA id or "System". The row's own
    // displayed value names the current item; the model's moves then move the drawn focus from there.
    int rest=-1;
    if(!currentText.isEmpty())
        for(int i=0;i<count;++i)if(band.labels.value(i)==currentText){rest=i;break;}
    if(rest>=0) {
        int dot=-1;
        for(int i=0;i<int(model.size());++i)
            if(shell_.valueText(model[i])==QString(QChar(0x25CF))){dot=i;break;}
        return std::clamp(rest+picked-(dot>=0?dot:0),0,count-1);
    }
    // (3) Nothing in the drawn list is current: draw no focused row. The caller still gets a position
    // for its own motion, but no item may claim to be the stored value (the IANA time-zone case,
    // SETTINGS-FIRMWARE 13.6a: the model reports currentRow -1 and the band must agree).
    (void)picked;
    return -1;
}
// The modal message-page class (view.h): the D4 titled page and the D2 error-code page. This is the one
// class the 2026-10-28 firmware frames measure for both page treatments, so the dim and the info-bar gate
// below read the same predicate. The confirm box and the depth-2 table page are the class's negative
// controls: no dim and no bar on either frame.
bool View::modalMessagePage() const {
    if(shell_.message().isEmpty())return false;
    const MessageKind kind=shell_.messageKind();
    return kind==MessageKind::Page || kind==MessageKind::ErrorCode;
}
// The measured page gain under a modal message page: 0.735, i.e. a black wash at 0.265 = 68/255 (1 - 68/255
// = 0.7333, within 0.002 of the fitted gain). DIALOGS-FIRMWARE 13.7 item 1; the offline reference-gain test
// (work/msgview: the frame with this gain applied) takes the back-hint crop 16.71 -> 19.40 dB.
constexpr int kMessagePageDimAlpha=68;
void View::drawScene(QPainter& p) {
    iconQueue_.clear();windSites_.clear();
    QFont font(shell_.fontFamily());font.setPixelSize(27);p.setFont(font);
    // The message surface is an explicit kind the shell decides (Shell::MessageKind, audit/DIALOGS-FIRMWARE.md
    // section 4.2 item 1): an inline empty-state line is a list row, a titled page / modal box / error page /
    // information page is a full screen the message itself owns. Nothing is inferred from the string any more.
    const MessageKind messageKind=shell_.message().isEmpty()?MessageKind::None:shell_.messageKind();
    const bool messageUp=messageKind!=MessageKind::None;
    const bool inlineMessage=messageKind==MessageKind::Inline;
    // A Page/Box/ErrorCode/Info surface replaces the item list and the category band (the firmware's page is
    // the whole screen: the reference frame's band is off-frame). The modal message pages dim the page they
    // replace - the wash below is the measurement - while the box and the table page dim nothing (14.1).
    // user_plugin's name page also replaces the list, the category band and the info bar (page-user-create.png
    // has neither icons nor the clock).
    const bool messageScreen=(messageUp && !inlineMessage) || userCreate_.open || plEdit_.open;
    auto text=[&](const QRectF& r,const QString& value,int size,QColor colour,int flags=Qt::AlignLeft|Qt::AlignVCenter){
        if(textRenderer_.ready()) {QFont f(shell_.fontFamily());f.setPixelSize(size);textRenderer_.draw(*this,p,r,value,f,colour,flags);return;}
        QFont f(shell_.fontFamily());f.setPixelSize(size);f.setStyleStrategy(QFont::PreferAntialias);f.setHintingPreference(QFont::PreferNoHinting);p.setFont(f);p.setPen(QColor(0,0,0,38));p.drawText(r.translated(1,1),flags,value);p.setPen(colour);p.drawText(r,flags,value);
    };
    // Soft white halo behind a label (the XMB's focused-item glow): the text drawn at low alpha in a ring of offsets.
    auto glow=[&](const QRectF& r,const QString& value,int size,double strength,int flags=Qt::AlignLeft|Qt::AlignVCenter){
        if(textRenderer_.ready()) {QFont f(shell_.fontFamily());f.setPixelSize(size);textRenderer_.draw(*this,p,r,value,f,Qt::white,flags,strength,false,false);return;}
        QFont f(shell_.fontFamily());f.setPixelSize(size);f.setStyleStrategy(QFont::PreferAntialias);p.setFont(f);
        // Reference (3x crop, 64 s video): a luminous halo ~5 px wide around the focused label.
        for(const auto& [radius,alpha]:{std::pair{5.,7},std::pair{2.5,16}})for(int k=0;k<12;++k) {
            const double a=k*std::numbers::pi/6;
            p.setPen(QColor(235,245,255,int(alpha*strength)));p.drawText(r.translated(std::cos(a)*radius,std::sin(a)*radius),flags,value);
        }
    };
    // Options panel (reference frame 45.4 s): a translucent light strip from x~1330 to the right edge with a bright
    // left edge; options 23 px, 40 px apart, centred on the focus row; the focused option glows. Slides in from the right.
    // Shared by the settings choice columns, the item-level Triangle menu and the in-player panel so all three keep
    // one visual language. `rows` switches it to the player panel's form: a marker column on the left for the current
    // choice, dimmed rows for the ones this build cannot honour, and the focused row's note under the list.
    const auto drawOptionsPanel=[&](const QStringList& items,double position,double alpha,double ahead,const QString& heading,
                                    const QVector<MenuOption>* rows=nullptr,const PanelBand* band=nullptr,
                                    const QVector<bool>* writable=nullptr,const QVector<bool>* swatches=nullptr,
                                    const QVector<quint32>* chipColours=nullptr,const QVector<bool>* submenus=nullptr,
                                    const QStringList* notes=nullptr,int windowRows=0) {
        // Phase 11.3: `band` selects the firmware's own form - a settings choice list (SETTINGS 9.1/U2,
        // 11.6, 11.8) or the item options menu (ITEM-MENU-INFO-FIRMWARE E1/F6). Without it this is the
        // panel the surfaces shared before, unchanged.
        const bool firmwareBand=band && band->firmware;
        const double x0=(firmwareBand?band->x0:1330.)+ahead*600;
        const double fadeEnd=firmwareBand?band->fadeEnd:1920.;
        const double pitch=firmwareBand?band->pitch:40.;
        const double rowCount=rows?double(rows->size()):double(items.size());
        const double first=firmwareBand?band->rowCentre:505-(rowCount-1.)*20.;
        // Phase 11.4: a captured list may show fewer rows than it holds (the Color list: 12 of 13). The
        // rows keep the measured grid and only the window moves, one row at a time, when the focus leaves
        // it - the captured list's last step scrolls by exactly one row (SETTINGS 16.2). 0 keeps every
        // other band's rule untouched.
        const int windowStart=windowRows>0?std::clamp(int(std::floor(position+.5))-(windowRows-1),0,
            std::max(0,int(rowCount)-windowRows)):0;
        const bool chipList=swatches && !swatches->isEmpty();
        p.save();p.setOpacity(alpha);
        // Over the XMB's own background the light strip is enough. Over video it is not: white 23 px text
        // on a bright frame is unreadable, so the in-player panel lays the strip on a dark plate (the
        // console's control panel dims the picture behind it for the same reason). The firmware band is
        // never over video and the frame shows no plate, so the plate stays with the in-player panel.
        if(rows && !firmwareBand) {
            QLinearGradient shade(x0,0,1920,0);
            shade.setColorAt(0,QColor(0,0,0,158));shade.setColorAt(1,QColor(0,0,0,120));
            p.fillRect(QRectF(x0,0,1920-x0,1080),shade);
        }
        if(firmwareBand) {
            if(band->plateName && band->plateW>0. && band->plateH>0.) {
                // The firmware's plate. It slides with `x0` (which carries the panel's own `ahead` offset) and
                // takes the panel's fade: the image is drawn at the measured rect and object alpha, so its
                // left step, its peak and its tail are the frame's, not this build's stops. The layer's
                // opacity is the panel's open ease, so a band that has not finished opening is still the
                // same plate at lower alpha.
                p.save();p.setOpacity(p.opacity()*band->plateOpacity);
                p.drawImage(QRectF(x0+band->plateX-band->x0,band->plateY,band->plateW,band->plateH),
                    assets_.image(band->plateContainer,band->plateName));
                p.restore();
            } else {
                // The band's own measured profile (SETTINGS 13.3). It carries the left step as well: the
                // frame's 1260-1262 columns are the plane's antialiased edge and a 2 px bright step, which
                // the earlier panel's separate 14 px edge stroke would overdraw to ~110 luma.
                QLinearGradient fill(x0,0,fadeEnd,0);
                for(int stop=0;stop<band->stopCount;++stop)
                    fill.setColorAt(band->stops[stop].at,QColor(255,255,255,band->stops[stop].alpha));
                p.fillRect(QRectF(x0,0,fadeEnd-x0,1080),fill);
            }
        } else {
            QLinearGradient fill(x0,0,1920,0);fill.setColorAt(0,QColor(255,255,255,46));fill.setColorAt(.08,QColor(255,255,255,30));fill.setColorAt(1,QColor(255,255,255,22));
            p.fillRect(QRectF(x0,0,1920-x0,1080),fill);
            QLinearGradient edge(x0-10,0,x0+4,0);edge.setColorAt(0,QColor(255,255,255,0));edge.setColorAt(.75,QColor(255,255,255,70));edge.setColorAt(1,QColor(255,255,255,0));
            p.fillRect(QRectF(x0-10,0,14,1080),edge);
        }
        // The list scrolls with the focused row, so the heading and the note follow the row they belong
        // to rather than the list's resting box: `first` is only where row 0 sits when focus is at 0. The
        // item menu's band does not move at all (no scroll object in the firmware), so its row 0 is fixed.
        const double focusRow=firmwareBand?band->rowCentre:505.;
        const double rowZero=firmwareBand&&band->fixedRows?band->rowCentre:focusRow-position*pitch;
        if(!heading.isEmpty()) { // a submenu (Sort By) or the player panel names itself above its list
            const double headingY=rows?std::max(56.,rowZero-68.):(firmwareBand?rowZero-48.:first-64);
            text({firmwareBand?x0+band->labelInset:x0+(rows?34:16),headingY,
                  firmwareBand?fadeEnd-x0-band->labelInset-24:1920-x0-40,40},heading,19,QColor(255,255,255,190));
        }
        const double labelX=firmwareBand?x0+band->labelInset:x0+(rows?34:16);
        const double labelWidth=(firmwareBand?fadeEnd:1920.)-labelX-24;
        // The shell's dividerBefore is the firmware's group break: rows before it are the utility group,
        // which starts groupGap px above the action group's own origin. Both origins are this band's
        // measured numbers, so no menu-specific y offset enters the view.
        int groupBreak=0;
        if(rows)for(int i=0;i<int(rows->size());++i)if((*rows)[i].dividerBefore){groupBreak=i;break;}
        // A settings band carries its utility row as a measured index (the Theme list's `Install`),
        // because a choice list has no MenuOption and therefore no dividerBefore flag.
        if(groupBreak==0 && firmwareBand)groupBreak=band->groupBreakRow;
        for(int i=0;i<int(rowCount);++i) {
            if(windowRows>0 && (i<windowStart || i>=windowStart+windowRows))continue;
            const double distance=i-position;
            // Firmware rows are anchored on the measured baselines and do not travel with focus; the
            // settings list keeps its 40 px pitch and its centred-with-focus motion. A windowed list is a
            // fixed grid whose window moves, so its rows are placed from the window start, not from focus.
            const bool aboveBreak=firmwareBand&&band->fixedRows&&groupBreak>0&&i<groupBreak;
            const double y=windowRows>0
                ?focusRow+double(i-windowStart)*pitch
                :(firmwareBand&&band->fixedRows)
                    ?focusRow+double(aboveBreak?i:i-groupBreak)*pitch-(aboveBreak?band->groupGap:0.)
                    :focusRow+distance*pitch;
            if(y<-40 || y>1120)continue;
            const double focus=std::max(0.,1.-std::abs(distance));
            const QRectF rect(labelX,y-20,labelWidth,40);
            const bool enabled=rows?(*rows)[i].enabled:(writable?writable->value(i,true):true);
            const QString title=rows?(*rows)[i].title:items.value(i);
            // A row this build cannot honour still takes the focus halo (weaker), so it is visibly the
            // selected row; its title is dimmed and its note says why.
            if(focus>.5)glow(rect,title,23,(focus-.5)*2*(enabled?1.:.45));
            // The firmware marks the current item by focus only - no check and no dot (SETTINGS U4) - so
            // the dot stays inside the rows path the player panel owns and never draws on a band.
            if(rows && !firmwareBand && (*rows)[i].active) {
                p.save();p.setBrush(QColor(255,255,255,235));p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(x0+18,y),4.5,4.5);p.restore();
            }
            const int level=!enabled?115:int(255*(.78+.22*focus));
            const bool chip=chipList && swatches->value(i,false);
            if(chip) {
                // The console's colour item is a flat swatch, no label anywhere in the list (SETTINGS
                // 12.4b): 27x28 px at the row's text column, the measured fill, and the 1 px outline the
                // captured focused chip carries. A disabled chip is the same swatch at 40 % opacity, and
                // the model's note for it is printed under the list.
                const QRectF swatchRect(labelX,y-14.,27.,28.);
                p.save();if(!enabled)p.setOpacity(p.opacity()*.40);
                p.setPen(Qt::NoPen);p.setBrush(QColor::fromRgb(chipColours->value(i,0)));
                p.drawRoundedRect(swatchRect,1.5,1.5);
                p.restore();
                if(focus>.5) {
                    // The focused chip takes the captured 1 px outline whether or not it can be committed:
                    // the dimmed fill already says disabled, and the outline has to say which row is focused.
                    p.save();p.setBrush(Qt::NoBrush);
                    p.setPen(QPen(QColor(255,255,255,enabled?215:150),1.));
                    p.drawRoundedRect(swatchRect.adjusted(-1.,-1.,1.,1.),2.,2.);p.restore();
                }
            } else {
                text(rect,title,23,QColor(255,255,255,level));
            }
            if(rows && (*rows)[i].gateBadge && !chip) {
                // The item menu's second utility row carries the small purple square the frame shows after its
                // label (frames/menu-options-open.png: 13x14 at x 1517..1529, y 345..358, 14 px after the ink of
                // "Group Content"). It is the firmware's gaiji U+F882 in msg_folder_class_with_icon, which the
                // text path would draw as a missing-glyph box, so the shell names the row with msg_folder_class and
                // the badge is drawn here. The badge is that gaiji itself, from vsh's image font (imageFontGlyph: a 26 x 23 dark disc with
                // the pink Square, whose square ink sits at glyph columns 7..19, rows 5..17). It is drawn at the
                // label's 23 px, i.e. 1:1, placed so the square lands on the measured 1517..1529 / y 346..358.
                QFont badgeFont(shell_.fontFamily());badgeFont.setPixelSize(23);
                const double labelRight=labelX+QFontMetrics(badgeFont).horizontalAdvance(title);
                const QImage glyph=imageFontGlyph(assets_.root(),0xF882);
                if(!glyph.isNull()) {
                    p.save();p.setOpacity(enabled?1.:.45);
                    p.drawImage(QRectF(labelRight+13.-7.,y-6.5-5.,glyph.width(),glyph.height()),glyph);
                    p.restore();
                }
            }
            if(firmwareBand && submenus && submenus->value(i,false)) {
                // The firmware's submenu row carries a right-pointing chevron: an 11x10 px triangle,
                // measured on the Background list's `Brightness` row at x 1786-1796, y 387-396. The item
                // menu's rows carry the same triangle at its own measured column (1573-1583), which is why
                // the band owns the x (FIDELITY-SURFACES 15.8 V1).
                p.save();p.setBrush(QColor(255,255,255,enabled?225:115));p.setPen(Qt::NoPen);
                const double chevronX=firmwareBand?band->chevronX:1786.;
                QPolygonF chevron;chevron<<QPointF(chevronX,y-5.)<<QPointF(chevronX,y+5.)<<QPointF(chevronX+10.,y);
                p.drawPolygon(chevron);p.restore();
            }
            if(rows && focus>.5) {
                // One line under the list while a row has focus: the file behind a subtitle row, or the
                // reason a row cannot be chosen. Never a claim the shell has not measured.
                const QString note=(*rows)[i].note;
                if(!note.isEmpty())
                    text({labelX,std::min(1010.,rowZero+rowCount*pitch+8),labelWidth,90},note,16,QColor(225,225,225,235),
                        Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
            }
        }
        // A disabled choice item gives its reason under the list, in the same voice as the player panel's
        // row note. It is this build's line, not the console's: the firmware has no disabled chip to label.
        if(notes && !notes->isEmpty()) {
            const int focused=int(std::floor(position+.5));
            const QString note=notes->value(focused);
            if(!note.isEmpty()) {
                const double listBottom=windowRows>0?focusRow+double(windowRows-1)*pitch+28.
                                                    :focusRow+double(rowCount-1)*pitch+28.;
                text({labelX,std::min(1004.,listBottom+8.),1920.-labelX-24.,96.},note,15,
                     QColor(225,225,225,235),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
            }
        }
        p.restore();
    };
    if(shell_.mediaOpen()) {
        const auto now=clock_.elapsed();
        if(!previousMediaOpen_ || previousMediaTitle_!=shell_.mediaTitle()) {
            mediaControls_.opened(now);
            videoPanelOpen_=false;videoPanelFocus_=15;videoPanelNotice_.clear();videoPanelStatus_.clear();
        }
        previousMediaOpen_=true;previousMediaTitle_=shell_.mediaTitle();
        mediaControls_.setPaused(shell_.mediaPaused() || shell_.busy(),now);
        // Where the video picture sits on the 1920x1080 frame (letterboxed or pillarboxed); an image
        // subtitle is placed against this rectangle, not the screen. The picture area is always painted
        // black first, so a 4:3 (pillarboxed) or letterboxed stream never shows the XMB wave through its bars.
        QRectF videoRect(0,0,1920,1080);
        if(shell_.videoOpen() || shell_.photoOpen())p.fillRect(QRectF(0,0,1920,1080),Qt::black);
        if(!mediaFrame_.isNull()) {
            const auto size=QSizeF(mediaFrame_.size()).scaled(QSizeF(1920,1080),shell_.photoOpen() && shell_.photoFill()?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);
            videoRect=QRectF(QPointF((1920-size.width())/2,(1080-size.height())/2),size);
            p.save();p.setClipRect(QRectF(0,0,1920,1080));
            p.drawImage(shell_.photoOpen() && photoViewport_.valid()?photoViewport_.imageRect():videoRect,mediaFrame_);
            p.restore();
        }
        // The active sidecar subtitle cue. Drawn over the video, below the controls in z-order, and only
        // while no dialog is up (a dialog's own black plate would otherwise have text over it). The PS
        // menu paints the whole frame black later in this function, so it hides the cue without a
        // special case here.
        if(shell_.videoOpen() && shell_.message().isEmpty()) {
            const auto cue=shell_.subtitleCueText();
            if(!cue.isEmpty())drawSubtitle(p,cue);
            else if(const auto* picture=shell_.subtitleImageCue())drawImageSubtitle(p,*picture,videoRect);
        }
        // Surface B2 owns the bottom band while it is up: the audio player's own chrome (queue line,
        // title, hint sentence, bar, time) fades out with the panel's 150 ms ease, so the panel is not
        // read through a bar that belongs to another surface. Video and photo are untouched (panelDim 1).
        const double panelDim=(shell_.videoOpen()||shell_.photoOpen())?1.:1.-musicPanelEase_.value;
        // SELECT's Display state (Shell::mediaDisplayPinned): the console's Display key keeps the player's own
        // chrome up instead of letting the 3 s hide timer drop it (MEDIA-PANELS-FIRMWARE 4.3/11.5 item 9).
        const double controlOpacity=shell_.mediaOpen() && shell_.mediaDisplayPinned()
            ?1.:(shell_.videoOpen()?mediaControls_.state(now).opacity:1.);
        p.save();p.setOpacity(controlOpacity*panelDim);
        // Vertical order of the chrome, bottom band (surface A, audit 3.5): the time bar (941..961), its
        // 16x16 digit cells (963..979), the title (984..1022) and the direct-key hint strip (1026..1056).
        // The ASCII sentence survives only where this build has no firmware icon set to draw: the photo
        // viewer (surface D, out of this change) and the audio player, whose hints live in its Triangle
        // control panel.
        // The title sits above the hint row. Only the video player needs 984: its 16x16 digit row ends at
        // 979. The photo viewer and the audio player have no such row and keep the 970 this build had, so
        // surface D and the audio chrome are unchanged down to the pixel of their text layout.
        if(!mediaAudioOnly())text({110,shell_.videoOpen()?984.:970.,1500,38},shell_.mediaTitle(),27,Qt::white);
        if(shell_.photoOpen())
            text({110,1020,1700,35},QString("Circle Back   Cross ")+(shell_.slideshowActive()?"Pause":"Slideshow")+"   L1/R1 or +/- Zoom   Square/0 Reset   Triangle/Q/E Rotate   Arrows Pan when zoomed   [/] Photo",21,QColor(230,230,230));
        else if(!shell_.videoOpen() && !mediaAudioOnly())
            text({110,1020,1700,35},"Circle Back   Cross Play/Pause   Left/Right Seek   L1/R1 or [/] Track   Square/M Shuffle   R Repeat   Triangle Control Panel",21,QColor(230,230,230));
        if(shell_.busy()) {
            if(shell_.videoOpen()) {
                text({310,910,1300,36},assets_.text("videoplayer_plugin","msg_buffering"),26,Qt::white);
                const QImage& loading=assets_.image("videoplayer_plugin","tex_loading_icon");
                const QImage& wait=assets_.image("videoplayer_plugin","tex_wait_bar");
                if(!loading.isNull())p.drawImage(QRectF(110,805,90,90),loading);
                if(!wait.isNull())p.drawImage(QRectF(110,895,90,38),wait);
            } else text({110,910,1000,45},"Loading…",26,Qt::white);
        }
        else if(!shell_.photoOpen()) {
            // The queue line's separator arrives doubled ("A-circumflex middot") from
            // Shell::mediaQueueSummary(): shell.cpp is another pass's fence, so the root fix is RETURNED there
            // (audit/FIDELITY-BEHAVIOUR.md 16.11) and this draw site repairs that one sequence, so the frame
            // shows a single U+00B7. ASCII source: both characters are QChar codes, which is this tree's rule,
            // and the container has no separator string of its own (audioplayer_plugin's nearest are
            // msg_track_number_value "Track %1" and msg_tracks_count_value "%1 Tracks" - no "of", no dot), so
            // U+00B7 is this line's own choice rather than a firmware string. The replace is a no-op once the
            // shell side is fixed.
            QString queueLine=shell_.mediaQueueSummary();
            queueLine.replace(QString(QChar(0x00C2))+QChar(0x00B7),QString(QChar(0x00B7)));
            if(!mediaAudioOnly() && !videoPanelOpen_)text({110,895,1700,36},queueLine,21,QColor(230,230,230));
        }
        else if(photoViewport_.valid())text({110,895,1700,36},QString("Zoom %1%").arg(photoViewport_.zoomPercent(),0,'f',0),21,QColor(230,230,230));
        if(shell_.videoOpen() && !shell_.photoOpen()) {
            if(videoPanelOpen_)drawVideoPanel(p);
            // Surface A is the video player's own surface: the firmware's time page and its hint strip.
            if(shell_.mediaDuration()>0)
                drawVideoTimeBar(p,shell_.mediaPosition(),shell_.mediaDuration(),controlOpacity);
            drawVideoHelpStrip(p);
        } else if(mediaAudioOnly()) {
            // Surface B1: the music player's own page replaces the generic bar, time string and hint sentence.
            drawMusicPage(p);
        }
        p.restore();
        // The in-player panel (Triangle): the same right-hand options panel, over the video and over the
        // controls' band, so it reads as an overlay that owns the screen while it is open. It changes
        // nothing about the controls' own clock: mediaControls_ was advanced above exactly as before. The
        // latched content keeps it drawing through its close ramp (PAGE-MOVE 15.4).
        if(optionsDrawn() && optionsShown_.player) {
            drawOptionsPanel(optionsShown_.titles,optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,
                optionsShown_.heading,&optionsShown_.rows);
        }
        // Surface B2: the XMB music control panel (Triangle). It draws over the audio player's own chrome,
        // the way the firmware's panel sits over the XMB, and only while a local audio item is what plays.
        if(mediaAudioOnly() && (musicPanelOpen_ || musicPanelEase_.value>.002))
            drawMusicPanel(p,musicPanelEase_.value);
    } else {
        videoPanelOpen_=false;
        previousMediaOpen_=false;
        const auto& categories=shell_.categories();
        // Two depth channels (audit/PAGE-MOVE.md section 15). `childAmount` is the page content's progress
        // (the icon's 262 px travel, the item columns, the label's fade): 18 rendered frames = 300 ms mode 4,
        // the fit the 4c burst's frame count supports. `barAmount` is the bar layer's: the firmware's bar
        // re-layout on a page change is 200 ms mode 5, traced at the instruction level (PAGE-MOVE 14.1).
        const double childAmount=std::clamp(depthPosition_,0.,1.);
        const double barAmount=std::clamp(barDepthPosition_,0.,1.);
        // Category-band transition (GEOMETRY-ROOT 5.2/5.3, decoded fpdump7 2026-10-28). The bar keeps its
        // world grid (200 wu pitch + 10 focus padding, worldY 250/260) while the camera distance falls
        // 2700 -> 2483.53, so the projection 2700/d turns those into screen pitch 200 -> 217.43, unfocused
        // y 290 -> 268.21 and the bar's focused row 280 -> 257.34. The interpolation is linear in the
        // animated distance (the decoded d series is the eased property), so screen positions follow 1/d,
        // not a screen-space lerp. [TRACED: endpoints and the 210 wu pitch from the decoded vertex
        // constants. The recede is the bar layer's own channel and runs on the traced 200 ms mode 5.]
        const double recedeDistance=2700.-(2700.-2483.53)*barAmount;      // 2700 -> 2483.53
        const double recede=2700./recedeDistance;                        // projection factor 1 -> 1.08716
        // The bar layer's focused category stays on its root world x and only projects: 565 -> 530.57 (the
        // decoded transition draws put it at 530.85 including that frame's 0.27 px offset).
        const double barFocusX=kColumnX-395.*(recede-1.);
        // The bright focused icon is the page's own draw, not the bar layer: it keeps world size 167.328 and
        // y 280 and travels 262 px left (565 -> 303). The captured transition frames show no vertical move
        // of that icon; the 257.34 above is the dim bar-layer row hidden behind it. [TRACED]
        const double pageX=kColumnX-262.*childAmount;
        // The neighbours are the transition-only bar layer. Its material brightness ramps 0.698 -> 0.067
        // (decoded _Ambiant.w) and its presence ends at depth 1, so the settled state draws exactly one
        // icon at 303. The two factors are kept apart: the glass renderer multiplies _Ambiant.w by the
        // layout opacity, so folding both into one would square the root state. [FIT: the captured open
        // frame is already post-recede (its ~700 ms page load outlasts the recede), so only the
        // 0.698/0.067 endpoints bound this curve; the linear presence term is this build's.]
        const double barBrightness=.698+(.067-.698)*barAmount;
        const double barPresence=barAmount>=.999?0.:1.-barAmount;
        for(int i=0;i<categories.size() && !messageScreen;++i) {
            // Firmware geometry (RPCS3 capture at 1920x1080, audit/PIPELINE.md): focus column at x 565, icons
            // 200 px apart with 10 px more either side of the focused one; quads 117 px, focused 167 px;
            // shader brightness 0.698 unfocused, 1 focused. At rest those are exact, so childAmount 0
            // reproduces them and childAmount 1 draws the page's one icon at 303 with no neighbours.
            const auto& cat=categories[i];const double distance=i-categoryPosition_;            const bool open=i==shell_.category();
            const double focus=std::max(0.,1.-std::abs(distance));
            // Projected root grid: the bar anchor plus (200 n + 10 sign n) * 2700/d. The *focused* icon takes
            // the same grid but without the focus padding and with the page's 262 px depth travel: on a
            // category move it arrives from one whole bar step, measured on the console at 632 -> 561 px
            // (PAGE-MOVE 14.2) - i.e. +200 px at p = 0 sliding to 0 at p = 1, and the bar's label under it
            // travels with it (+159 px one frame after the key on the same frames). [TRACED: the 200 px step
            // and the measured positions; the ease it rides is the 200 ms mode-5 category move.]
            const double x=open?pageX+200.*distance*recede:barFocusX+(200.*distance+10.*std::clamp(distance,-1.,1.))*recede;
            // The 10 px focus lift eases on every icon, not only the open one: on a category move the console's old
            // icon sinks while the new one rises (the icon wind's grid, RPCS3 dump: mirrored -53 / +53 cells in the
            // category row, audit/PARTICLES.md "The icon wind"). Lifting only the open icon dropped the old one 10 px
            // in the key frame. At depth the non-open icons have focus 0, so the recede path is unchanged.
            const double y=open?290.-10.*focus:540.-250.*recede-10.*focus*recede;
            const double iconScale=open?1.:recede;
            // No dim behind a message: both measured reference frames keep the XMB at full brightness
            // (audit 4.1 "frame dim behind a message"), so the 0.18 and 135-alpha washes are gone.
            const double opacity=open?1.:barPresence;
            const double material=open?1.:barBrightness;
            if(opacity<=0.)continue;
            windSites_.push_back({(quint64(1)<<60)|quint64(i),QPointF(x,y)}); // the icon wind's widget (Particles::trackWidgets)
            // The Users column's category icon (audit/ICON-GAP-AUDIT.md B1). The shell names the row's asset
            // `explore_plugin_full:user_tex_load`, which this pass verified is the *same* house-with-a-face art
            // as the console's - not a different "loading" glyph - but exported as a flat white mask (RGB 255
            // everywhere, alpha 226 max). Drawn here it is a white sticker; the console draws the same shape
            // through the icon program, i.e. as the brass glass the other categories are. That asset is
            // `qgl/icontex:1n` (128x128 normal map, decodes, no other reference in this build), so the row is
            // mapped to it at the one place the category row draws. The asset choice itself lives in shell.cpp
            // (out of this pass's fence). A theme icon still wins: it replaces the material, not the slot.
            Category drawn=cat;
            if(cat.id=="user" && cat.texture=="user_tex_load") {
                drawn.container=QStringLiteral("qgl/icontex");
                drawn.texture=QStringLiteral("1n");
            }
            p.save();p.setOpacity(opacity);
            if(const auto themed=shell_.themeIcon(cat.themeIcon);!themed.isNull()) {
                // A PS3 theme's icon keeps its own colours and takes the quad of the firmware's glass icon.
                const double size=(117.13+50.2*focus)*iconScale;p.setOpacity(opacity*material);
                p.drawImage(QRectF(x-size/2,y-size/2,size,size),themed);
            }
            else if(drawn.container=="qgl/icontex" && icons_.ready())
                // The glass renderer takes the world quad and the real distance, so the projection (and
                // _Eye.z for the refraction) is the firmware's; the QPainter paths below scale explicitly.
                iconQueue_.push_back({drawn.texture,{x,y},117.13+50.2*focus,material,opacity,open?2700.:recedeDistance});
            else {
                // drawCategory is the visible-glyph fit (4.1 #11), not the captured quad; its focused
                // y stays at the old 290 so this transition change cannot move a settled fallback glyph.
                const double size=(76+focus*16)*iconScale;
                // The fallback glyph's own 0.67 unfocused brightness is a glyph fit, not _Ambiant.w, so the
                // bar ramp is applied as the ratio to the 0.698 state brightness (exactly 1 at the root).
                const double ramp=open?1.:barBrightness/.698;
                p.setOpacity(opacity*(.67+.33*focus)*ramp);drawCategory(p,drawn,{x-size/2,(open?290.:y)-size/2,size,size});
            }
            // The bar's own label, drawn under its own icon - not at a fixed focus slot. The console draws
            // exactly one category label at rest (the focused one) and, during a move, BOTH: the departing
            // category's label slides out under its own icon while it fades, and the entering one brightens
            // in place as it arrives (PAGE-MOVE 14.2 section 3.1; the per-frame ink alpha is in section 15).
            // Measured on `navmove/right1` (Network -> PlayStation Network, key at 540.4 ms), label ink peak
            // luma against a 62 background with a 255 settled peak: outgoing .76/.58/.44/.33/.25/.19/.14 at
            // +26/+42/+54/+65/+89/+102/+113 ms, entering .31/.72/.83/.89 - i.e. the two cross-fade 1-categoryFade_
            // / categoryFade_ and their sum stays within .08 of 1. [TRACED: the fade values and the fact that
            // both labels ride their own icons; FIT: none - the ease is the category move's own 200 ms mode 5.]
            if(i==shell_.category()){p.setOpacity((1.-childAmount)*(1.-categoryFade_));text({x-140,330,280,40},cat.title,21,Qt::white,Qt::AlignCenter);}
            else if(i==outgoingCategory_ && categoryFade_>.001 && !outgoing_.isEmpty())
                {p.setOpacity((1.-childAmount)*categoryFade_);text({x-140,330,280,40},cat.title,21,Qt::white,Qt::AlignCenter);}
            p.restore();
        }
        const auto isChoiceColumn=[](const Column& column) {
            if(column.entries.isEmpty())return false;
            for(const auto& entry:column.entries)if(entry.action!=Action::SetSetting)return false;
            return true;
        };
        // The value the row that opened a choice list displays. That text is the shell's own current-value
        // authority, so the band can rest on the current item without reading the store from here; the
        // time zone needs it because its stored value is a host id the firmware's 110 GMT items cannot
        // match by value (SETTINGS 11.3/11.7a).
        const auto rowValueText=[&](const QVector<Column>& cols,int level,const QString& setting) {
            if(level<1 || level>int(cols.size()))return QString{};
            const auto& parent=cols[level-1];
            if(parent.entries.isEmpty())return QString{};
            const auto& row=parent.entries.value(std::clamp(parent.selection,0,int(parent.entries.size())-1));
            return row.setting==setting?shell_.valueText(row):QString{};
        };
        const auto drawColumns=[&](const QVector<Column>& columns,double opacity,double offset,bool active,
                                   double cover=1.) {
        // An open options panel is not a level of its own: the columns keep the layout of the page under it.
        const double panelOpen=columns.size()>=2 && isChoiceColumn(columns.back())?std::clamp(depthPosition_-(columns.size()-2),0.,1.):0.;
        const double layoutDepth=depthPosition_-panelOpen;
        const int lastLevel=int(columns.size())-1-(panelOpen>0?1:0);
        // The firmware's own list for the column that is open, or null when this build's panel draws it. The
        // lookup is cached by the row's Preferences key, so this is a hash lookup per frame, not an RCO read.
        const ChoiceBand* bandColumn=nullptr;
        if(columns.size()>=2 && isChoiceColumn(columns.back())) {
            bandColumn=choiceBandFor(columns.back().entries.value(0).setting);
            if(bandColumn && (bandColumn->labels.isEmpty()
                || (bandColumn->source!=QLatin1String("rco") && bandColumn->source!=QLatin1String("capture"))))
                bandColumn=nullptr;
        }
        // Phase 11.3: a firmware band covers the page, and the frames show the page's own content at
        // 0.52 (settings list) / 0.545 (item menu) of its brightness while the category band, the clock
        // area and the background are untouched - so the dim belongs to the page, not to a plate over the
        // frame (SETTINGS 13.2). `cover` carries the item menu's dim; the settings band's is per level.
        // A native list drawn by this build's earlier panel is not a firmware band and keeps the page lit.
        const double bandDim=bandColumn?1.-(1.-kChoiceBand.contentDim)*panelOpen:1.;
        for(int level=0;level<columns.size();++level) {
            const auto& column=columns[level];
            const bool panelColumn=isChoiceColumn(column);
            double behind=std::clamp((panelColumn?depthPosition_:layoutDepth)-level,0.,1.);
            const double ahead=std::clamp(level-(panelColumn?depthPosition_:layoutDepth),0.,1.);
            const double pageDim=(panelOpen>0. && !panelColumn)?bandDim:1.;
            const double columnAlpha=(1.-ahead)*opacity*pageDim*cover;
            // A setting's choices open as the PS3 options band on the right instead of a new column.
            if(panelColumn) {
                if(columnAlpha>0) {
                    QStringList titles;titles.reserve(column.entries.size());
                    for(const auto& entry:column.entries)titles<<entry.title;
                    // Phase 11.3: the firmware's own list is drawn as its own band, labels and order from
                    // the committed choice model; a row the model has no firmware list for keeps this
                    // build's options panel and says so once, so no function is lost.
                    const QString setting=column.entries.value(0).setting;
                    if(bandColumn) {
                        titles=bandColumn->labels;
                        const QString current=rowValueText(columns,level,setting);
                        // The drawn rows are the firmware's; the focus is the drawn row that owns the
                        // model's selection, so the focused label and the committed value stay paired.
                        const double whole=std::floor(column.position);
                        const double fraction=column.position-whole;
                        const int from=choiceBandFocus(*bandColumn,column.entries,int(whole),current);
                        double drawn=from;
                        if(fraction>0.) {
                            const int to=choiceBandFocus(*bandColumn,column.entries,int(whole)+1,current);
                            drawn=from+(to-from)*fraction;
                        }
                        if(!choiceBandsLogged_.contains(setting)) {
                            choiceBandsLogged_.insert(setting);
                            qInfo().noquote()<<QStringLiteral("view: settings choice band %1 -> %2 items from %3 (%4), current row %5, %6")
                                .arg(setting).arg(int(bandColumn->labels.size())).arg(bandColumn->source,
                                    bandColumn->rcoPage.isEmpty()?QStringLiteral("no page object"):bandColumn->rcoPage)
                                .arg(from).arg(bandColumn->scrollable?QStringLiteral("scrolls"):QStringLiteral("fits the band"));
                        }
                        drawOptionsPanel(titles,drawn,columnAlpha,ahead,QString(),nullptr,
                                         bandForSetting(setting),&bandColumn->writable,
                                         &bandColumn->swatch,&bandColumn->colours,&bandColumn->submenu,
                                         &bandColumn->notes,bandColumn->windowRows);
                    } else {
                        if(!choiceBandsLogged_.contains(setting)) {
                            choiceBandsLogged_.insert(setting);
                            const ChoiceBand* choice=choiceBandFor(setting);
                            const QString why=!choice?QStringLiteral("the choice model has no list for this row")
                                :choice->source==QLatin1String("native")||choice->source==QLatin1String("rco-page-empty")
                                    ?QStringLiteral("the firmware has no list of its own for this row (%1)").arg(choice->note)
                                    :choice->labels.isEmpty()?QStringLiteral("the firmware list is empty (%1)").arg(choice->note)
                                    :QStringLiteral("this build's list is the only one for this row (%1)").arg(choice->note);
                            qInfo().noquote()<<QStringLiteral("view: settings choice panel %1 keeps this build's options panel: %2")
                                .arg(setting,why);
                        }
                        drawOptionsPanel(titles,column.position,columnAlpha,ahead,QString(),nullptr,
                                         pageBandForSetting(setting));
                    }
                }
                continue;
            }
            // The list under an open options panel stays in place; unfocused rows drop to icons only.
            const double panelAmount=level+1<columns.size() && isChoiceColumn(columns[level+1])?panelOpen:0.;
            if(behind>.999 && level<lastLevel-1)continue;
            // ICON0 cards and glyphs have separate layout classes. A file-backed
            // square icon is still an icon; a wide media thumbnail uses a card.
            const auto isTile=[this](const auto& entry){
                if(entry.iconSized)return false;
                if(entry.gameTile)return true;
                const QImage* image=nullptr;
                if(!entry.artwork.isNull())image=&entry.artwork;
                else if(entry.container=="file")image=&assets_.bestImage(entry.container,entry.texture);
                if(image && !image->isNull())return image->width()>image->height()*1.25;
                const bool albumArt=entry.texture.contains("album_default") || entry.texture.contains("album_pic");
                return albumArt;
            };
            // BG1 is an empty ICON0 placeholder (GEOMETRY-ROOT 12.7). It
            // must not sit behind an already loaded image or a glyph row.
            const auto focusedEntry=column.entries.value(column.selection);
            const bool plateRow=focusedEntry.gameTile && focusedEntry.artwork.isNull() && focusedEntry.container!="file";
            if(level==0 && columnAlpha>0. && plateRow) {
                const double plateDistance=column.selection-column.position;
                const double plateFocus=std::max(0.,1.-std::abs(plateDistance));
                const double plateDepth=behind*(1.-plateFocus);
                const double plateRowOffset=plateDistance*80+std::clamp(plateDistance,0.,1.)*95
                    -std::clamp(-plateDistance,0.,1.)*305;
                const double plateSink=(active && level==int(columns.size())-1)?overscrollStep*columnSink_:0.;
                const double plateY=540+(505+plateRowOffset-540-15*plateDepth*std::clamp(plateDistance,0.,1.))*(1.-.055*plateDepth)+plateSink;
                const QRectF plateRect(kColumnX-160.+ahead*210.+offset,plateY-86.,319.,175.);
                QLinearGradient plateFill(plateRect.topLeft(),plateRect.bottomLeft());
                plateFill.setColorAt(0.,QColor(230,230,230,43));   // 0.170 at the top edge (y 419)
                plateFill.setColorAt(.14,QColor(230,230,230,52));  // 0.205 at y 443
                plateFill.setColorAt(.60,QColor(230,230,230,55));  // 0.215 at y 524
                plateFill.setColorAt(.84,QColor(230,230,230,36));  // 0.140 at y 566
                plateFill.setColorAt(1.,QColor(230,230,230,28));   // 0.110 at the bottom edge (y 594)
                p.save();p.setOpacity(columnAlpha*(1.-behind));
                p.setPen(Qt::NoPen);p.fillRect(plateRect,QBrush(plateFill));
                p.restore();
            }
            for(int i=0;i<column.entries.size();++i) {
                const auto& entry=column.entries[i];
                const double distance=i-column.position;
                // The slot above focus passes above the category band. Mapping
                // position continuously avoids hiding rows as they cross it.
                const double row=distance*80+std::clamp(distance,0.,1.)*95
                    -std::clamp(-distance,0.,1.)*(level==0?305:95);
                const double focus=std::max(0.,1.-std::abs(distance));
                // A column behind an open page (RPCS3 capture): its unfocused rows move back to camera distance 2857.2,
                // so they shrink toward the screen centre (scale 2700/2857.2) and the gap below focus closes by 15;
                // the focused row stays at 2700 and steps 262 px left, the others 109.46 px (GEOMETRY-ROOT 8: the
                // measured parent-unfocused offset; the 262 stays the focused row's own step).
                const double depth=behind*(1.-focus);
                // Only an explicit diagnostic pin offsets a column. Normal
                // boundary presses leave the selection and projection unchanged.
                const double sink=(active && level==int(columns.size())-1)?overscrollStep*columnSink_:0.;
                const double y=540+(505+row-540-15*depth*std::clamp(distance,0.,1.))*(1.-.055*depth)+sink;
                if(y<-100 || y>1050)continue;
                // The icon wind's widget: this row's icon centre, keyed by its column set, level and row.
                windSites_.push_back({(quint64(active?shell_.category():outgoingCategory_)<<40)|(quint64(level)<<20)|quint64(i),
                    QPointF(kColumnX-behind*(109.46+152*focus)+ahead*210+offset,y)});
                // The incoming page column is one slot right and 10.223% larger in the decoded transition
                // (fp_0174..fp_0181: 774.09 = 565+210; 85.554 = 77.616*1.10223, 203.700 = 184.8*1.10227),
                // easing to zero offset and 1.0 with the depth move. The scale is on the icons only: the
                // decoded incoming rows keep their 80 px y grid (fp_0175/0176 at screen 120/40), so `y`
                // and the row pitch are not scaled. [TRACED offset and scale; the ease is the depth PafEase.]
                const double entryScale=1.+.10223*ahead;
                const double iconX=kColumnX-behind*(109.46+152*focus)+ahead*210+offset;
                // Reference (RPCS3 4.x video): square icons ~60 px, ~105 px when focused; utility folders are icons too.
                // Only media thumbnails (album art) keep a wide tile.
                // Setting rows carry the small round settings bullet: 18 px, 39 px focused (reference 95 s).
                // Top-level item ink, measured on the console (FIDELITY-ROOT-BAR I2/I3): the focused row's glyph is
                // 128 px wide inside its 184.8 px firmware quad and the plain folder of an unfocused row is 50 px
                // wide inside its 77.616 px quad, i.e. the console expands the focused icon 2.56x (50 -> 128).
                // drawItemGlyph fits the art's own alpha bounds to the box, so this renderer's ink is the box: the
                // box is set to the two measured ink widths directly. The firmware quads stay on `quad` below, which
                // is what the glass icon path draws, so the category icons and their material are untouched.
                // [TRACED: both ink widths and their quads. FIT: nothing - the box equals the measured ink.]
                const bool bullet=entry.container=="qgl/icontex" && entry.texture=="23n";
                const double size=(bullet?18+focus*21:level==0?50+focus*78:40+focus*16)*entryScale;
                // Firmware folder glyphs: a folder row in the Game column carries the emblem of what it holds - the
                // captured Game Data Utility row is item_tex_ps3game_folder (folder + gamepad, explore_category_game.rco
                // and explore_plugin_full.rco), not the bare storage folder. Steam's All Games / collection / Not
                // Installed rows are the same row, so they take that glyph. The media columns ship no emblem folder
                // at all: explore_category_video.rco's only folder is item_tex_plain_folder, which is what local
                // library folders keep.
                const QString itemTexture=entry.texture=="item_tex_plain_folder" && entry.action==Action::SteamFolder?
                    QStringLiteral("item_tex_ps3game_folder"):entry.texture;
                const bool tile=isTile(entry);
                // A row behind an open page is the parent column, and every captured parent-column row is an item icon
                // (77.616 unfocused / 184.800 focused, 2857.2/0.247 below focus). Keeping the wide album/ICON0 card
                // there drew a 320x180 placeholder on the same spot as the open category icon (Video Library over the
                // film strip), so a behind row is drawn at the item-icon box of its own level, art or not.
                const double quad=itemIconSize(level,bullet,focus)*entryScale;
                // Neighbour cards must fit the 80 px row pitch. The focused
                // ICON0 envelope is 320x176; preserve the source aspect ratio.
                const double cardHeight=72+focus*104;
                double iconWidth=tile?cardHeight*(320./176.)*entryScale:size,iconHeight=tile?cardHeight*entryScale:size;
                if(behind>.001 && tile){iconWidth=size;iconHeight=size;}
                // Captured parent-column brightness (_Ambiant.w 0.698 focused, 0.247 unfocused, RPCS3 fpdump7): it
                // belongs to the column, not to the glass row class, or the card behind a page stays at full strength.
                // A row this build still lists but cannot act on - the Continue Watching entry whose file is
                // gone - is drawn dimmed, so the state reads from the couch; the row's own line says why.
                // Every other row leaves Entry::disabled false, so this multiplies by exactly 1 and the
                // rest of the frame is bit-identical.
                const double dim=entry.disabled?.5:1.;
                const double iconAlpha=columnAlpha*(1.-behind*(1.-(.247+.451*focus)))*dim;
                // icontex items are glass icons like the categories: the firmware draws them with the same shader at
                // brightness 1, behind an open page 0.247 (focused row 0.698) (RPCS3 capture, audit/PIPELINE.md);
                // everything else is RCO or file artwork.
                if(entry.gameTile) {
                    // Native installed PKGs render ICON0 directly in this slot: no launcher card/chrome.
                    const QRectF card(iconX-iconWidth/2,y-iconHeight/2,iconWidth,iconHeight);
                    if(entry.artwork.isNull())drawItemGlyph(p,entry.container,itemTexture,card,iconAlpha);
                    else {
                        // Every installed game has the same ICON0 canvas,
                        // independent of the provider's capsule dimensions.
                        // Crop centrally rather than stretching or letterboxing.
                        QSizeF crop=QSizeF(card.size()).scaled(entry.artwork.size(),Qt::KeepAspectRatio);
                        const QRectF source(QPointF((entry.artwork.width()-crop.width())/2.,(entry.artwork.height()-crop.height())/2.),crop);
                        p.save();p.setOpacity(iconAlpha);p.drawImage(card,entry.artwork,source);p.restore();
                    }
                }
                else if(!entry.artwork.isNull()) {
                    const auto fitted=QSizeF(entry.artwork.size()).scaled({iconWidth,iconHeight},Qt::KeepAspectRatio);
                    p.save();p.setOpacity(iconAlpha);p.drawImage(QRectF(QPointF(iconX-fitted.width()/2,y-fitted.height()/2),fitted),entry.artwork);p.restore();
                }
                else if(const auto themed=shell_.themeIcon(entry.themeIcon);!themed.isNull()) { // a PS3 theme's icon, on the glass icon's quad
                    p.save();p.setOpacity(iconAlpha);p.drawImage(QRectF(iconX-quad/2,y-quad/2,quad,quad),themed);p.restore();
                }
                else if(entry.container=="qgl/icontex" && icons_.ready() && !tile) {
                    const double brightness=1.-behind*(1.-(.247+.451*focus));
                    iconQueue_.push_back({entry.texture,{iconX,y},quad,brightness,columnAlpha,2700+157.2*depth});
                }
                else drawItemGlyph(p,entry.container,itemTexture,{iconX-iconWidth/2,y-iconHeight/2,iconWidth,iconHeight},iconAlpha);
                if(entry.friendRow) {
                    p.save();p.setOpacity(iconAlpha);p.setPen(QPen(QColor(20,20,20,180),1.5));
                    p.setBrush(entry.presenceState==0?QColor(130,130,130):entry.presenceState==7?QColor(110,190,245):QColor(140,220,110));
                    p.drawEllipse(QPointF(iconX+size*.36,y+size*.36),5+focus*2,5+focus*2);p.restore();
                }
                // Observed on the console by MaXMB (MIT, styles.css "labels-idle"; not yet confirmed on our references,
                // whose clips never rest on a row): after 4 s on one row of Photo/Music/Video/Game/Network/PSN, the
                // other rows' titles fade out over 0.8 s.
                static const QSet<QString> idleCategories{"photo","music","video","game","network","psn"};
                const qint64 resting=focusTime();
                const double idleFade=active && level==0 && idleCategories.contains(shell_.categories()[shell_.category()].id)?
                    std::clamp((resting-4000)/800.,0.,1.)*(1.-focus):0.;
                // A Settings page's idle dim (TRACED, sysconf_plugin; audit/PAGE-MOVE.md "The Settings idle dim"):
                // FUN_000a7948 restarts a 3000 ms timer whenever a row takes focus. Its callback FUN_000a70e4 eases
                // property 0x1000018 (the row's text colour; the icon is untouched on the console) of the rows at
                // distance d = 1..13 to 0.2: mode 5, 800-10d ms below the focus and 800-20d above it, after
                // (14-d)*30 ms, so the nearest row starts last. A focus move restores them over 30 ms and restarts
                // the timer, which the focus clock already models. The curve runs from the focus time, sampled as a
                // fresh ease so the value is a function of the rest time alone.
                double settingsIdle=1.;
                if(active && level==shell_.depth() && level>0 && !panelColumn && shell_.topPageLoadsPlugin()
                   && i!=column.selection) {
                    const int d=std::abs(i-column.selection);
                    if(d<=13) {
                        PafEase idleDim;idleDim.retarget(1.);
                        idleDim.retarget(settingsIdleDim,(i>column.selection?800.-10*d:800.-20*d),5,settingsIdleMs+(14-d)*30.);
                        idleDim.advance(resting*.001);settingsIdle=idleDim.value;
                    }
                }
                p.save();p.setOpacity(columnAlpha*(1.-behind)*(1.-panelAmount*(1.-focus))*(1.-idleFade)*settingsIdle);
                // An inline empty-state line stands in for the focused row's own label slot: the firmware's
                // line sits where the list row would be (measured x 630, centre y 504), so the row keeps its
                // icon and focus but not its title. No other row is affected.
                const bool inlineStandIn=inlineMessage && active && level==shell_.depth() && i==column.selection;
                // One label column per level, as on the PS3 (GEOMETRY-ROOT 8: icon centre +113 at the top
                // level - the measured 678 px origin, not the 664 the +99 gave - and +57 inside a page).
                // Wide cards need their own label gutter at every depth,
                // including Continue Watching. Parent rows have receded to
                // icon size and keep the narrow column gutter.
                const double labelX=kColumnX+((tile && behind<.001)?173:level==0?113:60)+ahead*210+offset;
                const double alpha=(.455+.545*focus)*(entry.disabled?.55:1.);
                // A row's own value wins when it sets one (the sign-in page's fields: server URL, user name and
                // the password as dots). Every other row leaves Entry::fieldValue empty, so this falls through to
                // Shell::valueText() exactly as before and no existing frame changes.
                auto value=active?(entry.fieldValue.isEmpty()?shell_.valueText(entry):entry.fieldValue):QString{};
                // The signed-in user is not a value: the console marks it with the firmware's tex_indi_Me diamond left of
                // the avatar and draws nothing in the value column, so the shell's "current" dot moves there. The diamond
                // grows and moves out with the row's focus, measured on two console frames: unfocused 11 px, centre 54.5
                // left of the icon centre (rpcs3-ref nav/users-column-up.png: 510 against 564.5); focused 26 px, 130.5
                // left (nav/users-column.png: 434.5). Interpolated with the same focus the avatar scales by.
                const bool activeUser=entry.action==Action::SelectUser && value==QString(QChar(0x25cf));
                if(activeUser) {
                    value.clear();
                    // The diamond's ink fills 21/26 of the box drawItemGlyph fits it to, so the box is the measured ink x 26/21.
                    const double mark=(11.+15.*focus)*26./21.,markX=iconX-(54.5+76.*focus);
                    drawItemGlyph(p,QStringLiteral("explore_plugin_full"),QStringLiteral("tex_indi_Me"),
                                  {markX-mark/2,y-mark/2,mark,mark},p.opacity());
                }
                QFont labelFont(shell_.fontFamily());labelFont.setPixelSize(32);
                // Elision keeps the firmware's shape (audit/TEXT-RENDERER.md section 4): PAF draws a character
                // range and explore_plugin positions a separate label2_text_ellipsis Text object after the last
                // visible character, so the prefix and the ellipsis are two draws from one metric authority
                // (TextRenderer::elideToWidth, the same unhinted advances that place the glyphs) instead of
                // QFontMetrics substituting U+2026 into the string. The QPainter fallback keeps Qt's elision,
                // because that path also draws the glyphs with Qt.
                const qreal labelLimit=qEnvironmentVariableIsSet("XMB_LABEL_LIMIT")?
                    qreal(qEnvironmentVariableIntValue("XMB_LABEL_LIMIT")):(value.isEmpty()?1050.:640.);
                QString label=entry.title;
                TextRenderer::Elision elided;
                if(textRenderer_.ready()) {
                    elided=textRenderer_.elideToWidth(labelFont,entry.title,labelLimit);
                    if(elided.elided)label=elided.prefix;
                } else label=QFontMetrics(labelFont).elidedText(entry.title,Qt::ElideRight,int(labelLimit));
                const QRectF labelRect(labelX,y-28,value.isEmpty()?1100.:650.,56);
                // The focused label carries a soft white glow that breathes (MaXMB observation, styles.css xmbLabelGlow:
                // 3.4 s ease-in-out from a faint halo, about 0.35 of the peak, to the full bloom, starting faint when
                // the row takes focus). The peak is the static halo measured from the reference crop.
                const double breath=.35+.65*(.5-.5*std::cos(2*std::numbers::pi*double(resting)/3400.));
                if(!inlineStandIn) {
                    if(focus>.5)glow(labelRect,label,32,(focus-.5)*2*breath*(entry.disabled?.45:1.));
                    text(labelRect,label,32,QColor(255,255,255,int(255*alpha)));
                    if(elided.elided)
                        text(QRectF(labelRect.left()+elided.ellipsisX,labelRect.top(),40,labelRect.height()),
                             QString(QChar(0x2026)),27,QColor(255,255,255,int(255*alpha)));
                    if(!value.isEmpty() && panelAmount<.5) {
                        QString shown=value;
                        // Every value is elided before the screen edge: a host reading such as a Windows device name
                        // ran off the frame (Chat's microphone, Sound's headset). The sign-in fields keep their 440 px
                        // field; anything else may run to 1860 (60 px from the edge, the console's widest value - the
                        // Samoa time zone - ends near 1735).
                        const qreal valueWidth=entry.action==Action::JellyfinSignInField?440.:560.;
                        {
                            QFont valueFont(shell_.fontFamily());valueFont.setPixelSize(21);
                            if(textRenderer_.ready()) {
                                const auto clipped=textRenderer_.elideToWidth(valueFont,value,valueWidth);
                                shown=clipped.elided?clipped.prefix+QString(QChar(0x2026)):value;
                            } else shown=QFontMetrics(valueFont).elidedText(value,Qt::ElideRight,int(valueWidth));
                        }
                        text({1300+ahead*210+offset,y-25,440,50},shown,21,QColor(255,255,255,int(230*alpha*(1.-2*panelAmount))));
                    }
                    const auto subtitle=shell_.subtitleText(entry);
                    if(!subtitle.isEmpty())text({labelX,y+20,1100,30},subtitle,17,QColor(230,230,230,int(235*alpha)),Qt::AlignLeft|Qt::AlignTop);
                    // The help line belongs to the selection, not the eased position: on the real XMB it moves to the new
                    // row on the frame the move starts, fully opaque, including on every held-key repeat (RPCS3 capture).
                    // Size and box measured against the console on one shared string (Game Settings' PS - Upscaler help,
                    // rpcs3-ref-2026-10-28/settings/page-game-settings.png; audit/TEXT-RENDERER.md section 9): word
                    // gaps scale 1.057 against 18 px, so 19 px; cap top y 528 (5 px above the old 24 px offset); line 1
                    // wraps after "...to an HD" with ink to x 1805 and "TV." does not fit (box >= 1181), and Chat's line 1 ends at
                    // "...video from" where "the" would reach x 1809 (box < 1184): 1182 is inside that 1181..1184 bound, not a
                    // traced constant. The console's line pitch is 22 px (cap
                    // tops 528 / 550), tighter than the font's own 24 px line height, so the lines are broken here and
                    // drawn one by one on that pitch.
                    else if(i==column.selection && !entry.detail.isEmpty()) {
                        constexpr int helpSize=19,helpPitch=22;constexpr qreal helpWidth=1182;
                        QFont helpFont(shell_.fontFamily());helpFont.setPixelSize(helpSize);
                        helpFont.setHintingPreference(QFont::PreferNoHinting);
                        QTextLayout wrap(entry.detail,helpFont);QTextOption option;
                        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);wrap.setTextOption(option);
                        wrap.beginLayout();
                        for(int k=0;;++k) {
                            auto line=wrap.createLine();if(!line.isValid())break;
                            line.setLineWidth(helpWidth);
                            text({labelX,y+19+k*helpPitch,helpWidth,30},entry.detail.mid(line.textStart(),line.textLength()).trimmed(),
                                 helpSize,QColor(225,225,225),Qt::AlignLeft|Qt::AlignTop);
                        }
                        wrap.endLayout();
                    }
                }
                p.restore();
            }
            if(level>0 && columnAlpha>0 && panelAmount<.999) {
                // The page's parent indicator: a left-pointing triangle between the parent column and the open
                // page. It was the font glyph U+2039 here. The audit's candidate, system_plugin:tex_arrow_left
                // (31x29), was tested against out/rpcs3-ref2-2026-10-28/bands/display-page-rest.png by least
                // squares over the glyph's own luma patch and REFUSED: its ink is 27x23 where the frame's is
                // 26x29, and the best placement leaves MSE 90.8 (RMS 9.5) - a visibly squatter triangle. The
                // same fit finds the frame's glyph in explore_plugin_full:tex_arrow_anim, the 30x420 sheet of
                // 14 left-arrow cells: cell 0 drawn 1:1 as a 30x30 quad with its top-left at (430,490) leaves
                // MSE 2.44 (RMS 1.6), i.e. it reproduces the frame's silhouette, its apex-bright/base-dim
                // gradient and its position. The object alpha is 0.89 (FIT: the least-squares gain; 1.0 puts
                // the peak at 248 against the frame's 238, and 0.89 minimises the residual over the glyph).
                // [TRACED: the rect, and the identification - the fit residual is two orders below the
                // rejected candidate's. FIT: the 0.89 alpha.]
                p.save();p.setOpacity(columnAlpha*(1.-behind)*.89);
                p.drawImage(QRectF(430,490,30,30),assets_.image("explore_plugin_full","tex_arrow_anim"),
                            QRectF(0,0,30,30));
                p.restore();
            }
        }
        };
        // The item columns are replaced by a message screen and kept behind an inline line, which stands
        // in only for the focused row's label (the firmware's line sits in that list slot).
        if(!messageScreen) {
            // On a category move the console cross-fades two row sets, and the two sides are not symmetric
            // (audit/PAGE-MOVE.md "Phase 14.2": the console burst at C:\dev\out\navfeel\navmove\right1,
            // 83.6 fps, key at 540 ms, Network -> PlayStation Network). The outgoing block translates
            // rigidly one bar step left and its alpha falls 1 -> 0 on the move ease, i.e. it is the ghost;
            // the incoming block arrives from about 125 px and is already at about 0.7 alpha on the first
            // frame, so the new column stays legible for the whole move. Measured outgoing offsets
            // -50/-139/-179/-185 px at p .30/.63/.78/.88 (215 px at p = 1), incoming +87/+51/+27/+14/+6 px
            // at the same progress (125 px at p = 0), incoming label peak luma 208/218/232/243/255 in a
            // 255-peak settled frame. [TRACED: 215 px, 125 px, and the four alpha samples; FIT: the
            // incoming alpha line .58+.42p, extrapolated to p = 0 - no captured frame exists before
            // p = .30, so its intercept is the one fit number here.]
            const double outSlide=215.,inSlide=125.;            // px, traced
            const double inAlpha=.58+.42*(1.-categoryFade_);    // .58 -> 1 over the move, fit
            // The item menu's band covers the page too (measured 0.545, ITEM-MENU-INFO-FIRMWARE E1); the
            // in-player panel's frame is unmeasured, so it keeps the page at full brightness. The cover is
            // held by the latch while the close ramp runs, so the page does not pop back to full brightness
            // under a panel that is still fading.
            const double optionsCover=(optionsDrawn() && !optionsShown_.player)?
                1.-(1.-kOptionsBand.contentDim)*optionsOpenEase_.value:1.;
            // XMB_MOVE_DEBUG=1 skips the outgoing set: a capture-only switch that lets a burst measure the
            // incoming block on its own. Unset in every normal run, where it draws both sets.
            static const bool skipOutgoing=qEnvironmentVariableIsSet("XMB_MOVE_DEBUG");
            if(!skipOutgoing && !outgoing_.isEmpty())
                drawColumns(outgoing_,categoryFade_,(outgoingCategory_-categoryPosition_)*outSlide,false,optionsCover);
            drawColumns(columns_,inAlpha,(shell_.category()-categoryPosition_)*inSlide,true,optionsCover);
            // Item-level Triangle options: the same right-hand band over the page it belongs to. Drawn while
            // Shell reports it open OR while the latched close ramp still has content (PAGE-MOVE 15.4); the
            // panel never becomes a level of the column stack.
            if(optionsDrawn() && !optionsShown_.player) {
                // Phase 11.3: the item menu is the firmware's own band (ITEM-MENU-INFO-FIRMWARE E1/F6): its
                // measured edge, 38 px pitch, fixed rows and 1334 text column, and its MenuOption rows so a
                // row this build cannot honour is dimmed with its reason. No dot: the band marks by focus.
                // The two list-level rows carry the frame's own flags (FIDELITY-SURFACES 15.4): a right-pointing
                // chevron on Sort By and Group Content, and the purple gate badge on Group Content.
                QVector<bool> submenus;submenus.reserve(optionsShown_.rows.size());
                for(const auto& row:optionsShown_.rows)submenus<<row.submenu;
                drawOptionsPanel(optionsShown_.titles,optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,
                                 optionsShown_.heading,&optionsShown_.rows,&kOptionsBand,nullptr,nullptr,nullptr,
                                 &submenus);
            }
        }
        // The busy page carries the status line itself (drawBusySurface, dialogs.cpp); the loose line would
        // draw the same text twice on that page.
        if(!messageScreen && !busyPageUp() && !shell_.status().isEmpty())text({680,850,1100,50},shell_.status(),26,QColor(230,230,230));
        // The modal message page's dim (DIALOGS-FIRMWARE 13.7 item 1). The console does not replace the page
        // when a message page opens, it dims it: page-network-test-error and page-network-connection-settings
        // read 47.0-51.0 where the same patch on a message-free frame reads 73-88, a whole-page least-squares
        // gain of 0.735. The confirm box's top band reads 73.6 against the root's 73.8 and the depth-2 table
        // page 73.5, so the wash is per class - View::modalMessagePage() is the class and nothing else on the
        // frame takes it. The stack order is page -> dim -> chrome: the wash draws here, after the
        // page/background composite and before the info bar and the message surface, so the bar's measured
        // ramp alphas and the chrome's own alphas land over the dimmed page (the message page's y75 plate ramp
        // measures the same 0.35 alpha over its 50.6 background that the root's measures over 71.0).
        // The console's wait page is the D4 class over the dimmed page: it takes the same wash, and the
        // same measured clock face (drawPageChrome suppresses the page clock while busyPageUp(), dialogs.cpp).
        const bool modalPage=modalMessagePage()||busyPageUp();
        if(modalPage)p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,kMessagePageDimAlpha));
        // Info bar: the information panel's own material, xmb_plugin_normal:tex_indi_bg, a 24x24 nine-slice
        // tile with an 8 px white alpha ramp on every side around a black interior at alpha 13/255 (5.1%).
        // It frames the clock in the top-right corner; the clock's centre sits at y~104 as in the reference
        // video. A modal message page keeps the WHOLE bar, not only the face (DIALOGS-FIRMWARE 13.7 item 2):
        // both network message frames keep the plate's ramps (y75 122.4 over the dimmed 50.6 background = the
        // same 0.35 alpha as the root's, y132 121.9, left edge x1329), the clock text (574 ink pixels in
        // x1690..1770) and the filled clock face. The bar is chrome, so it draws after the wash above and
        // keeps its own alphas; the D5 information page has no 1080p frame of its fit and keeps drawPageChrome's
        // clock with no bar, and the box/table pages keep neither.
        // A firmware band owns the screen: the captured frames have neither the plate nor the analog clock
        // (SETTINGS 11.6 measured luma 0 at x1750..1918, y100 on screensaver-list-rest.png; options-open-01
        // shows the same), and the band does not reach the plate's area, so the absence is a state and not
        // an occlusion. The Theme page's four bands measure the same absence (their clock-box maximum is 99
        // luma against this build's 255), so a list with a measured Theme-page band suppresses the bar even
        // when its labels are this build's own. A native list drawn by the earlier panel keeps the bar.
        const bool choiceBandUp=columns_.size()>=2 && isChoiceColumn(columns_.back())
            && depthPosition_>double(columns_.size()-2)+0.001
            && [&] {
                if(pageBandForSetting(columns_.back().entries.value(0).setting))return true;
                const ChoiceBand* choice=choiceBandFor(columns_.back().entries.value(0).setting);
                return choice && !choice->labels.isEmpty()
                    && (choice->source==QLatin1String("rco")||choice->source==QLatin1String("capture"));
            }();
        // The item menu's band is up (from the latch, so it stays up through its close ramp) or on its way in.
        const bool optionsBandUp=optionsDrawn() && !optionsShown_.player;
        if((!messageScreen || modalPage) && !choiceBandUp && !optionsBandUp) {
            // Measured from the saved 1920x1080 firmware frames (audit/ITEM-MENU-INFO-FIRMWARE.md B3/E4):
            // the visible plate is 591x58 at (1329,75), flush to the right screen edge. The tile's own 1 px
            // transparent margin lies just outside that core (x1328/y74) and its right border falls past
            // x1919, which is why the firmware plate shows top, bottom and left ramps but no right edge.
            // The 9-slice keeps the 8 px border at its native pixel size, so the measured ramp is
            // reproduced instead of being smeared by stretching the whole tile.
            const QRectF infoBar(1329,75,591,58);
            const QImage& plate=assets_.image("xmb_plugin_normal","tex_indi_bg");
            if(!plate.isNull()) {
                // FIDELITY-ROOT-BAR F4/F2 (2026-10-29). The tile's 1 px margin row/column is not transparent but
                // ~4% alpha, and drawing it as the slice's first row put a 0.041-alpha overshoot one pixel outside
                // the plate on every edge (measured y74 95.74, y133 94.13, x1328 102.3 on the canonical frame). The
                // source grid now starts one pixel in, so the plate's core is exactly rows 75..132 and columns
                // 1329..1919 with nothing outside it. [TRACED: the core rect and the tile's own margin.]
                drawNineSlice(p,"xmb_plugin_normal","tex_indi_bg",QRectF(1329,75,600,58),8.,1.,1.);
                // Both ramps then still measure 17% weak (F2: alpha 0.291 top / 0.292 bottom against the console's
                // 0.351 / 0.352). The deficit is one row per edge: the console's ramp peaks on its first row and
                // decays over ~6 rows, and this tile's own first row is that peak at 0.291. A 1 px white row per
                // edge closes it: 1-(1-0.291)(1-0.086) = 0.352 and 1-(1-0.292)(1-0.086) = 0.353, i.e. the measured
                // 0.351/0.352 within a thousandth. [TRACED: the target alphas and their difference. FIT: expressing
                // the difference as a 1 px top-up row rather than a brighter tile.]
                p.save();p.setPen(Qt::NoPen);
                p.fillRect(QRectF(1329,75,591,1),QColor(255,255,255,22));
                p.fillRect(QRectF(1329,132,591,1),QColor(255,255,255,22));
                p.restore();
            } else {
                // The decoder reads this tile (audit/TEXTURE-COVERAGE.md: 3174/3174); if a future flash
                // cannot, keep the drawing this block shipped before and say so once.
                static bool plateFallbackWarned=false;
                if(!plateFallbackWarned){plateFallbackWarned=true;qWarning().noquote()<<"view: info bar plate xmb_plugin_normal/tex_indi_bg did not decode; drawing the tex_infobar fallback";}
                p.save();p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,20));p.drawRoundedRect(infoBar,4,4);
                p.setOpacity(.12);p.drawImage(infoBar,assets_.image("system_plugin","tex_infobar"));
                p.setOpacity(1);p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(255,255,255,90),1));p.drawRoundedRect(infoBar.adjusted(.5,.5,-.5,-.5),4,4);p.restore();
            }
            text({1240,82,525,45},shell_.clockText(),27,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
            // The analog clock face: one geometry for every page. FIDELITY-ROOT-BAR F6/F7 measure the console's
            // root face at 1774,88 -> 1805,119 (32x32, 3 px ring ink, centre 1789.5/103.5) with a filled interior,
            // and the message page's own frames measure the same face (ring ink 1774..1776 and 1803..1805, rows
            // 88..119, interior 0..22 over a dimmed 45..70 plate). This build used to draw a smaller root fit
            // (centre 1791/104, 2.7 px stroke, no disc, 10/7 px hands), which left the plate showing through the
            // face (the 6x6 block at (1778,106) read 151.7) and put the ring 2 px small. The two branches therefore
            // collapse into one: the measured geometry, at two disc alphas.
            const bool measuredFace=modalPage;
            const QPointF centre(1789.5,103.5);
            // The console's ring ink occupies x1774..1776 and 1803..1805 on row 103 and rows 88..119 vertically
            // (F6): a 3 px stroke whose centre line is 14 px from the centre, not the 2.7 px / 14 px this build
            // drew at 1791. The radius is therefore unchanged and the stroke and centre carry the whole 2 px.
            const double ringRadius=14.;
            const double ringStroke=3.;
            const double minuteLength=13.;
            const double hourLength=9.5;
            const auto time=shell_.clockDateTime().time();
            const auto hand=[&](double angle,double length){const double a=angle*std::numbers::pi/180.;return QPointF(std::sin(a)*length,-std::cos(a)*length);};
            p.save();
            // The dark disc inside the ring: the root frame's own face is filled too (F7 - the 6x6 block at
            // (1778,106), which sits 11.8 px from the face centre and therefore inside the ring's inner edge, reads
            // 28.1 over a 71.4 plate interior, i.e. black at 1-28.1/71.4 = 0.606). One disc draw for both page
            // classes, each at its own measured alpha: 0.606 at the root, 0.8 on a message page (interior 0..22 over
            // a 45..70 dimmed plate). Its radius is the ring's inner edge, 14-3/2 = 12.5.
            // [TRACED: the interiors, the ring geometry and the sample's distance from the centre. FIT: the alpha
            // expressed as an 8-bit disc.]
            const int discAlpha=measuredFace?204:int(std::lround(.606*255.));
            const double discRadius=ringRadius-ringStroke/2.;
            p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,discAlpha));
            p.drawEllipse(centre,discRadius,discRadius);p.setBrush(Qt::NoBrush);
            for(int shadow=1;shadow>=0;--shadow) {
                const auto c=centre+QPointF(shadow*1.5,shadow*1.5);p.setPen(QPen(shadow?QColor(0,0,0,150):QColor(255,255,255),ringStroke,Qt::SolidLine,Qt::RoundCap));
                p.drawEllipse(c,ringRadius,ringRadius);p.drawLine(c,c+hand(time.minute()*6,minuteLength));p.drawLine(c,c+hand((time.hour()%12)*30+time.minute()*.5,hourLength));
            }
            p.restore();
            // The firmware's indicator row (page_xmb_indicator in xmb_plugin_normal.rco, B1): five 32x32
            // slots at fixed offsets from the clock, inside the same plate. The whole group is authored at
            // alpha 0 and only the slots the console has state for appear, so each slot below is drawn only
            // while its own state is active - never as a permanent placeholder.
            // The offsets are the RCO's own, measured from the clock ink's right edge (1760, B3/E4); the
            // 32 px row is centred on the plate (y 104, so its top is 88). What this build can source:
            //   -244 tex_indi_message  unread messages. No message store exists here (the Friends column's
            //                          Message Box row is the firmware's empty state), so the slot stays dark.
            //   -274 tex_indi_chat     an in-XMB text-chat session. This build has none, so it stays dark.
            //   -342 tex_indi_online   Steam friends' presence: the one real source, with online_num.
            //   -376 avatar            the RCO object's image is null - the firmware resolves the signed-in
            //                          user's PSN avatar at run time. No frame in the local corpus shows the
            //                          slot lit, and this build has no PSN account, so nothing is drawn: no
            //                          rect, mask or placeholder for it is evidenced, and inventing one
            //                          would fabricate an account surface.
            //   -410 tex_indi_plus     PlayStation Plus membership. No such state exists here; stays dark.
            // The console frames (out/rpcs3-day-2026-10-28/frames/c-*.png, and the stored root reference)
            // show the row empty apart from the clock and its face, which is what the inactive slots draw.
            int onlineFriends=0;
            for(const auto& category:shell_.categories()) {
                if(category.id!="friends")continue;
                for(const auto& entry:category.entries)
                    if(entry.friendRow && entry.presenceState!=0)++onlineFriends; // persona state 0 = Offline
                break;
            }
            const double iconAnchorX=1760.; // measured clock ink right edge (B3/E4)
            // One drawing rule for all five slots: the RCO offset from the clock ink's right edge, the 32x32
            // slot at y 88, the texture, and `active` - the slot's own state. A slot with no state is not
            // drawn at all, which is the row's visibility rule; an active slot with a count also gets its
            // online_num text.
            const auto infoSlot=[&](double offset,const char* texture,bool active,int count) {
                if(!active)return;
                const QRectF slot(iconAnchorX+offset,88,32,32);
                p.drawImage(slot,assets_.image(QStringLiteral("xmb_plugin_normal"),QLatin1String(texture)));
                if(count<=0)return;
                // online_num sits at +14/+2 inside the slot. Its RCO size 10 has no measured px mapping
                // (the clock text is also size 10 but renders a 22 px cap), so 15 px is a fit that keeps
                // the count inside the 32 px slot.
                text(QRectF(slot.left()+14,slot.top()+2,18,18),QString::number(count),15,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            };
            infoSlot(-244.,"tex_indi_message",false,0);                  // no unread-message source
            infoSlot(-274.,"tex_indi_chat",false,0);                     // no text-chat session
            infoSlot(-342.,"tex_indi_online",onlineFriends>0,onlineFriends);
            infoSlot(-410.,"tex_indi_plus",false,0);                     // no PlayStation Plus membership
        }
    }
    // Message surfaces (dialogs.cpp): the kind decides the shape. An inline line is drawn on the normal
    // frame; the other four own the screen. A box keeps drawing while its traced 200/1000-unit exit fade
    // runs after Shell has dismissed it.
    if(messageUp)drawMessageSurface(p);
    // The busy page outranks a box's exit fade: the console replaces the ask page with the proceed page, so a
    // dismissed box must not keep drawing its plate and rule pair over the new surface. The dialogs pass's
    // frame-measured surface owns this shape (dialogs.cpp); the shell publishes the two values it takes, both
    // -1 for the measured indeterminate wait page (no progress row, no D8 ring) and 0..1 once an operation
    // knows its total or its ring phase.
    else if(busyPageUp())drawBusySurface(p,shell_.busyProgress(),shell_.busySpinnerPhase());
    else if(messageShown_.kind==MessageKind::Box && (dialogEase_.value>.002 || dialogPlateEase_.value>.002))drawMessageBox(p);
    if(userCreate_.open)drawUserCreatePage(p);
    if(plEdit_.open)drawPlaylistEditor(p);
    if(osk_.open)drawOsk(p);
    // The transient volume HUD. It is a timed composite, not a panel: it appears on every change and fades on
    // the MediaControls model, takes no input (nothing here reads a key or a button), and draws only in the
    // frames that have room for it - the PS menu blacks the frame, an information/dialog page replaces it, and
    // the options panel and the on-screen keyboard own the keys that could change the level anyway.
    {
        const auto hud=volumeHud_.state(clock_.elapsed());
        if(volumeTrace() && hud.visible!=volumeHudVisible_) {
            volumeHudVisible_=hud.visible;
            qInfo().noquote()<<QString("volume: trace HUD %1 (level %2 muted %3, %4 ms after the change)")
                .arg(hud.visible?"visible":"expired").arg(volumeLevel_).arg(volumeMuted_?1:0)
                .arg(clock_.elapsed()-volumeHudAt_);
        }
        const bool room=!shell_.psMenuOpen() && shell_.message().isEmpty() && !shell_.optionsOpen() && !osk_.open;
        // The appearing edge is the firmware's own fade: 150 with 0 delay, read from explore_plugin.prx at
        // 0x270a00 / 0x270a04 (audit/MEDIA-PANELS-FIRMWARE.md 4.3, 5.4). MediaControls still owns the
        // visible window under it, so a held change keeps the window it always had; only the 250 ms
        // invented fade-in is gone.
        const double fadeIn=std::clamp(double(clock_.elapsed()-volumeHudAt_)/volumeMeterFadeMs,0.,1.);
        if(volumeTrace() && hud.visible && fadeIn<1.)
            qInfo().noquote()<<QString("volume: trace HUD fade-in %1 at %2 ms (150 ms, delay 0)")
                .arg(fadeIn,0,'f',3).arg(clock_.elapsed()-volumeHudAt_);
        if(room && hud.visible && hud.opacity>.002) drawVolumeHud(p,hud.opacity*fadeIn);
    }
    // The console's notification pop-up, drawn last so it sits above the XMB, and never while the PS menu is
    // up: that menu blacks the frame and owns the screen. It takes no input (nothing here reads keys), so it
    // cannot steal a press from whatever is underneath.
    // The operation guide sits on the same system_plugin page as the notification, under it. It belongs to
    // the XMB's own focus, so no overlay that takes the focus (PS menu, item menu, media, a message) shows it.
    if(!shell_.psMenuOpen() && !shell_.optionsOpen() && !optionsDrawn() && !shell_.mediaOpen() &&
       shell_.message().isEmpty() && !osk_.open && !userCreate_.open && !plEdit_.open) {
        // In-game, Circle at the column root returns to the game: xmb_ingame:msg_help_back_to_game (TRACED string).
        // The firmware shows it as the operation guide; its placement beside the Triangle guide is INFERRED, so it
        // takes the guide's plate and replaces the Options hint while it is up.
        if(shell_.inGame() && shell_.depth()==0)drawOptionHint(p,1.,shell_.inGameHelpText());
        else if(const double hint=optionHintAmount();hint>.002)drawOptionHint(p,hint);
    }
    if(!shell_.psMenuOpen() && notificationEase_.value>.002 && !notificationShown_.title.isEmpty())
        drawNotification(p,notificationEase_.value);
    // PS button: the console's system menu, re-measured on the 4.93 day frame (a21-ps-menu-1.png, the
    // corpus's ps-menu reference). The frame goes black except the clock over a 1 px rule, two entries
    // left-aligned on a 40 px pitch with the selected one one size larger inside a ~5 px Gaussian bloom,
    // the controller/charge panel at y 803-865 and the Enter/Back legend with its own rule at the bottom.
    // A hold opens it; "Turn Off the System" asks once before quitting.
    // The entrance is measured, not a frame: on the console's hold burst (PARITY-SCORECARD 16.3, 206.6 fps,
    // 300,470,1320,240) the XMB is untouched until the frame goes FULLY black 1052 ms after the press; the
    // entries' first ink is 1279 ms and their text is at full opacity 1326 ms; the selected row's bloom then
    // keeps growing until 1840 ms - the measured "staged" appearance. Before 1052 this draws nothing, so the
    // XMB stays up for the 52 ms between the watcher's 1000 ms rule and the console's envelope.
    if(shell_.psMenuOpen()) {
        const qint64 held=psPressAt_>0?clock_.elapsed()-psPressAt_:-1;
        const bool black=held<0 || held>=qint64(psHoldBlackMs);
        if(static const bool motionLog=qEnvironmentVariableIsSet("XMB_MOTION_LOG");motionLog)
            qInfo().noquote()<<"pshold:"<<held<<"ms pressAt"<<psPressAt_<<"black"<<(black?1:0)
                <<"text"<<QString::number(psHoldTextAlpha(clock_.elapsed()),'f',3)
                <<"bloom"<<QString::number(psHoldBloomAmount(clock_.elapsed()),'f',3);
        if(black) {
            const double content=psHoldTextAlpha(clock_.elapsed());
            const double bloom=psHoldBloomAmount(clock_.elapsed());
            p.save();
            p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0));   // the console blacks the whole frame
            // Everything after the plate takes the envelope's staged alpha: the clock and both rules are
            // outside the burst's crop, so they ride the entries' text (1279..1326) rather than a curve of
            // their own.
            p.setOpacity(content);
            // The menu's own clock: ink x 1613..1802, y 166..185, with a 1 px luma-175 rule under it at y 199.
            text({1383,153,420,45},shell_.clockText(),23,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
            p.fillRect(QRectF(0,199,1920,1),QColor(255,255,255,175));
            const auto psItems=shell_.psMenuItems();
            // Both entries start at the same left edge (measured ink x 831/832) and the selected one is ~7%
            // larger: the shared prefix "Turn Off the " measures 156 px on the focused row and 146 px on the
            // other, and the caps are 18 px and 17 px. The 43 px "pitch" in SCORECARD 16.5 is band tops:
            // the bloom adds 2 rows above the focused cap, the baselines themselves are 40 px apart.
            const double psTop=506.5;
            for(int i=0;i<psItems.size();++i) {
                const bool focused=!shell_.psMenuConfirmingOff() && i==shell_.psMenuSelection();
                const int size=focused?24:23;
                const QString label=shell_.psMenuConfirmingOff() && i==0?QObject::tr("Turn Off the System?"):psItems[i];
                const QRectF row(831,psTop+i*40.,1000,34);
                // Bloom strength 1.8 is a FIT by side-by-side crops against ps-menu__original__day__1080p: at 3.6 the
                // clamped mask filled the letters into one white block, at 1.4 the halo was fainter than the console's.
                if(focused && bloom>.01) {
                    QFont bloomFont(shell_.fontFamily());bloomFont.setPixelSize(size);
                    drawTextBloom(p,bloomFont,row,label,1.8*bloom);
                }
                // The label's drop shadow is a dark copy offset down-right: over the bloom it printed as a second,
                // dark "Turn Off the System" behind the white one. The console's PS menu has no shadow on either
                // entry (ps-menu__original__day__1080p: clean ink inside a soft halo), so it is drawn without one.
                if(textRenderer_.ready()) {
                    QFont f(shell_.fontFamily());f.setPixelSize(size);
                    textRenderer_.draw(*this,p,row,label,f,Qt::white,Qt::AlignLeft|Qt::AlignVCenter,0,false);
                } else text(row,label,size,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            }
            if(shell_.psMenuConfirmingOff())
                text({830,psTop+80,1000,34},QObject::tr("X turns the XMB off; the PC stays on   O cancels"),20,QColor(170,170,170),Qt::AlignLeft|Qt::AlignVCenter);
            // Controller/charge panel: numbers above 17 px slots (x 132 + 44.33*i 1..4), labels at x 311, the
            // pad glyph between "Controller" and "1", the charge dash centred on x 488.
            p.setPen(Qt::NoPen);
            for(int i=0;i<4;++i) {
                const double x=132.+i*44.333;
                p.setBrush(i==0?QColor(160,13,13):QColor(30,30,30));
                p.drawRect(QRectF(x,828,17,17));
                text({x-2,801,21,22},QString::number(i+1),19,QColor(215,215,215),Qt::AlignCenter);
            }
            text({311,795,220,34},QObject::tr("Controller"),24,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            drawImage(p,"system_plugin","tex_controller",{448,799,31,31});
            text({494,795,60,34},"1",23,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            text({311,835,220,34},QObject::tr("Charge Level"),24,QColor(215,215,215),Qt::AlignLeft|Qt::AlignVCenter);
            text({468,835,40,34},"-",24,QColor(215,215,215),Qt::AlignHCenter|Qt::AlignVCenter);
            // Footer legend (console ink x 726/762 and 1155/1192, y 897-916) with the console's 1 px rule at y 880.
            p.fillRect(QRectF(0,880,1920,1),QColor(255,255,255,175));
            drawImage(p,"system_plugin","tex_cross",{726,898,18,18});
            text({760,890,140,34},QObject::tr("Enter"),21,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            drawImage(p,"system_plugin","tex_circle",{1155,897,20,20});
            text({1190,890,140,34},QObject::tr("Back"),21,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            p.restore();
        }
    }
    else if(psHudAt_>0) {
        // A PS tap raises the controller indicator beside the clock. Measured on the console's tap burst
        // (rpcs3-gaps MANIFEST 4 / PS-BUTTON.md): a 134x46 rounded plate at (1191,81), flat RGB 53,49,41
        // in the settled frame, that starts 155 ms after the press, is full 66 ms later, holds for about
        // 2.0 s and leaves 66 ms after that. The pad glyph, "1" and "-" sit inside the plate at the
        // reference frame's own ink boxes (x 1200-1229, 1239-1245 and 1294-1302).
        const double alpha=psHudOpacity(clock_.elapsed());
        if(alpha>0.) {
            p.save();p.setOpacity(alpha);
            p.setPen(Qt::NoPen);p.setBrush(QColor(53,49,41));
            p.drawRoundedRect(QRectF(1191,81,134,46),4.5,4.5);
            drawImage(p,"system_plugin","tex_controller",{1199,88,31,34});
            text({1237,81,26,46},"1",23,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
            text({1278,81,40,46},"-",23,QColor(235,235,235),Qt::AlignHCenter|Qt::AlignVCenter);
            p.restore();
        }
    }
}
// The console's tap indicator window: 155 ms delay, 66 ms fade in, ~2.0 s hold, 66 ms fade out, as
// measured on the burst (luma 80.47 at rest, 68.85 from 321 ms to 2322 ms, 80.47 again by 2388 ms).
double View::psHudOpacity(qint64 now) const {
    if(psHudAt_<=0)return 0.;
    const qint64 t=now-psHudAt_;
    if(t<155 || t>=2288)return 0.;
    if(t<221)return double(t-155)/66.;
    if(t<2222)return 1.;
    return 1.-double(t-2222)/66.;
}
// The PS-hold menu's staged entrance, from the console's hold burst (PARITY-SCORECARD 16.3, 206.6 fps,
// C:\dev\out\rpcs3-gaps-2026-10-28\burst\ps-menu-entries, PS pressed 150 ms into the burst). Frame means
// over the 1320x240 entries crop: 65.85 at 1190 ms (XMB), 0.00 from 1202 (= press + 1052) to 1411, first
// ink 1429 (peak 63), peak 148/207/255 at 1436/1452/1476, and the focused row's bright-pixel count still
// growing 3903 -> 6236 from 1476 to 1986. So the text ramps 1279..1326 and the bloom 1326..1840.
// [TRACED: black at 1052, first ink 1279, text full 1326, bloom settled ~1840. FIT: the two ramps are
// linear - the burst separates their endpoints but not their shape between them (BEHAVIOUR C7).]
double View::psHoldTextAlpha(qint64 now) const {
    if(psPressAt_<=0)return 1.;                       // no press seen: the menu is a steady state
    const double t=double(now-psPressAt_);
    if(t<=psHoldTextStartMs)return 0.;
    if(t>=psHoldTextMs)return 1.;
    return (t-psHoldTextStartMs)/(psHoldTextMs-psHoldTextStartMs);
}
double View::psHoldBloomAmount(qint64 now) const {
    if(psPressAt_<=0)return 1.;
    const double t=double(now-psPressAt_);
    if(t<=psHoldTextMs)return 0.;
    if(t>=psHoldBloomMs)return 1.;
    return (t-psHoldTextMs)/(psHoldBloomMs-psHoldTextMs);
}
// ---- Couch volume ----------------------------------------------------------------------------------------
// The PC's master volume, driven by the pad's analog triggers and by the Windows media keys, with one
// transient HUD. Every change goes through host::setMasterVolume / host::setMasterMuted and then logs what
// Windows reports afterwards, so the endpoint - not this process's cache - is the record of what happened.
//
// Input mapping, in one place:
//   L2 held                    one 5% step down on the press, then a step every 120 ms (60 ms past 0.75)
//   R2 held                    the same upwards
//   volume up/down/mute keys   WM_APPCOMMAND and VK_VOLUME_* taken by the native filter, Qt::Key_Volume*
//                              taken in keyPressEvent; nothing else on the XMB uses them
//   VOLUP/VOLDOWN/MUTE         --script tokens that drive the key path
//   VOLSET=<0..100>            --script: an exact level, so a capture can pin 30/60/90
//   VOLKEY=up|down|mute        --script: posts the real WM_APPCOMMAND, i.e. the Windows path end to end
//   VOLKEY=vkup|vkdown|vkmute  --script: posts the WM_KEYDOWN VK_VOLUME_* form of the same key
//   VOLRESTORE                 --script: the level and mute state read at start-up
// A surface that owns the keyboard or the frame takes the keys too: while it is up the input is declined
// (so a media key keeps its normal Windows behaviour) and the HUD stays hidden.
bool View::volumeInputBlocked() const {
    return introRunning() || osk_.open || shell_.psMenuOpen() || shell_.optionsOpen() ||
        !shell_.message().isEmpty() || QApplication::activeModalWidget()!=nullptr;
}
void View::volumeApply(int percent,const QString& source) {
    const int requested=std::clamp(percent,0,100);
    const bool set=host::setMasterVolume(requested);
    const int reported=host::masterVolume();
    volumeMuted_=host::masterMuted();
    volumeLevel_=reported>=0?reported:requested;
    volumeHud_.opened(clock_.elapsed());
    volumeHudAt_=clock_.elapsed();
    if(!set && volumeTrace())qInfo()<<"volume: trace setMasterVolume failed (the endpoint refused the write)";
    // The read-back is the point of this line: the HUD draws what Windows reports, not what was asked for.
    qInfo().noquote()<<QString("volume: %1 request %2% -> Windows reports %3% (muted %4)")
        .arg(source).arg(requested)
        .arg(reported<0?QStringLiteral("unavailable"):QString::number(reported)).arg(volumeMuted_?1:0);
    update();
}
void View::volumeStep(int direction,const QString& source) {
    // The step is taken from the endpoint's own level, not from the cache, so a change made outside the app
    // (another program's mixer, a headset's own buttons) cannot accumulate a drift.
    const int base=host::masterVolume();
    if(base<0) {
        if(!volumeWarned_) {
            volumeWarned_=true;
            qWarning()<<"volume: no default playback endpoint; the volume controls do nothing";
        }
        return;
    }
    volumeApply(base+(direction>0?5:-5),source);
}
void View::volumeToggleMute(const QString& source) {
    const bool was=host::masterMuted();
    const bool set=host::setMasterMuted(!was);
    const int reported=host::masterVolume();
    volumeMuted_=host::masterMuted();
    volumeLevel_=reported>=0?reported:volumeLevel_;
    volumeHud_.opened(clock_.elapsed());
    volumeHudAt_=clock_.elapsed();
    if(!set && volumeTrace())qInfo()<<"volume: trace setMasterMuted failed (the endpoint refused the write)";
    qInfo().noquote()<<QString("volume: %1 mute %2 -> Windows reports muted %3, level %4%")
        .arg(source).arg(was?0:1).arg(volumeMuted_?1:0)
        .arg(reported<0?QStringLiteral("unavailable"):QString::number(reported));
    update();
}
bool View::volumeKeyAction(int action,const char* source) {
    const QString who=QString::fromLatin1(source);
    if(volumeInputBlocked()) {
        if(volumeTrace())qInfo().noquote()<<"volume: trace"<<who<<"ignored (another surface owns the input)";
        return false;
    }
    if(action==3)volumeToggleMute(who);
    else volumeStep(action==1?1:-1,who);
    return true;
}
void View::setMasterVolumePercent(int percent) {
    volumeApply(percent,QStringLiteral("VOLSET"));
}
// ---- Remote and media keys (PHASES H10, audit/INPUT.md) ----------------------------------------------------
// Every remote form (WM_APPCOMMAND, VK_MEDIA_*, Qt::Key_Media*/Key_Back/Key_HomePage/Key_Info, the Media Center
// keyboard shortcuts) arrives here as one RemoteKey. Navigation keys become the key or button the XMB already
// binds (Back = Circle/Escape, Info = Triangle, Channel = PageUp/PageDown, Home = the PS hold menu), so each
// surface keeps its own rules; transport keys act on the open player only. A key that does nothing in the
// current state is declined (returns false), so the native filter leaves the message to Windows - a Play/Pause
// pressed at the XMB with nothing playing still reaches whatever media app Windows routes it to.
// Every decision is one "remote:" log line; the transport keys add the player's state 400 ms later.
bool View::remoteKeyAction(RemoteKey key,const QString& source) {
    const QString name=QString::fromLatin1(remoteKeyName(key));
    const auto decide=[&](bool taken,const QString& what){
        qInfo().noquote()<<QString("remote: %1 from %2 -> %3").arg(name,source,what);
        // A declined native message goes on to Qt, which turns WM_APPCOMMAND into the same key as a key event:
        // remembered so keyPressEvent passes that echo on silently instead of deciding twice.
        if(!taken && !source.startsWith(QLatin1String("qt key"))){remoteDeclined_=key;remoteDeclinedAt_=clock_.elapsed();}
        return taken;
    };
    if(QApplication::activeModalWidget())return decide(false,"declined (a modal dialog owns input)");
    if(!isVisible())return decide(false,"declined (the XMB window is not shown)");
    shell_.noteActivity();mediaControls_.activity(clock_.elapsed());
    if(introRunning()){coldboot_.skip();return decide(true,"skipped the intro");}
    const auto pressKey=[this](int qtKey){
        QScopedValueRollback<bool> replay(replayingInput_,true);
        QKeyEvent press(QEvent::KeyPress,qtKey,Qt::NoModifier);keyPressEvent(&press);
        QKeyEvent release(QEvent::KeyRelease,qtKey,Qt::NoModifier);keyReleaseEvent(&release);
    };
    switch(key) {
    case RemoteKey::Back:
        pressKey(Qt::Key_Escape);
        return decide(true,QString("Circle (back); depth %1 mediaOpen %2").arg(shell_.depth()).arg(shell_.mediaOpen()?1:0));
    case RemoteKey::ChannelUp:case RemoteKey::ChannelDown: {
        if(osk_.open || userCreate_.open || plEdit_.open)return decide(false,"declined (a text page owns input)");
        const int before=shell_.selection();
        pressKey(key==RemoteKey::ChannelUp?Qt::Key_PageUp:Qt::Key_PageDown);
        return decide(true,QString("page %1; selection %2 -> %3").arg(key==RemoteKey::ChannelUp?"up":"down").arg(before).arg(shell_.selection()));
    }
    case RemoteKey::Info:
        return triangleAction(source);
    case RemoteKey::Home:
        if(osk_.open || userCreate_.open || plEdit_.open)return decide(false,"declined (a text page owns input)");
        if(shell_.psMenuOpen()){shell_.psMenuClose();update();return decide(true,"closed the system menu");}
        psPressAt_=0;   // no PS press precedes it: the menu is drawn in its steady state, not the hold envelope
        shell_.psHeld();update();
        return decide(true,QString("opened the system menu (PS hold); rows %1").arg(shell_.psMenuItems().size()));
    case RemoteKey::None:return false;
    default:break;
    }
    // Transport keys: the player only.
    if(!shell_.mediaOpen())return decide(false,"declined (no media is open)");
    // With media open the key is meant for this player even while another surface owns input: it is taken and
    // does nothing, rather than handed to Windows, which would route it to some other media application.
    if(osk_.open || userCreate_.open || plEdit_.open || shell_.psMenuOpen())return decide(true,"ignored (another surface owns input)");
    QString what;
    switch(key) {
    case RemoteKey::PlayPause:shell_.togglePlayback();what="toggle playback";break;
    case RemoteKey::Play:
        if(shell_.photoOpen()?shell_.slideshowActive():!shell_.mediaPaused())what="already playing";
        else {shell_.togglePlayback();what="play";}
        break;
    case RemoteKey::Pause:
        if(shell_.photoOpen()?!shell_.slideshowActive():shell_.mediaPaused())what="already paused";
        else {shell_.togglePlayback();what="pause";}
        break;
    case RemoteKey::Stop:
        // Stop leaves the player the way Circle does (the resume point is remembered there), after taking down
        // any panel over it so the one press really stops.
        if(musicPanelOpen_)closeMusicPanel();
        if(shell_.optionsOpen())shell_.toggleOptions();
        shell_.back();what=QString("stop; mediaOpen now %1").arg(shell_.mediaOpen()?1:0);
        break;
    case RemoteKey::Next:case RemoteKey::Previous:
        shell_.skipMedia(key==RemoteKey::Next?1:-1);what=key==RemoteKey::Next?"next item":"previous item";break;
    case RemoteKey::FastForward:case RemoteKey::Rewind: {
        if(!shell_.mediaSeekable())return decide(false,"declined (the open media is not seekable)");
        const qint64 before=shell_.mediaPosition();
        // The same 10 s step as L2/R2 and Left/Right (MediaControls::seekTarget clamps it).
        shell_.seekMedia(key==RemoteKey::FastForward?10000:-10000);
        what=QString("seek %1 10 s from %2 ms").arg(key==RemoteKey::FastForward?"+":"-").arg(before);
        break;
    }
    default:return false;
    }
    update();
    decide(true,what);
    QTimer::singleShot(400,this,[this,name]{
        qInfo().noquote()<<QString("remote: after %1 mediaOpen %2 paused %3 position %4 ms title \"%5\"")
            .arg(name).arg(shell_.mediaOpen()?1:0).arg(shell_.mediaPaused()?1:0).arg(shell_.mediaPosition()).arg(shell_.mediaTitle());
    });
    return true;
}
// The pad's Triangle on each surface (pollController's Y branches), for a remote's Info / More-info key.
bool View::triangleAction(const QString& source) {
    const auto decide=[&](bool taken,const QString& what){
        qInfo().noquote()<<QString("remote: info from %1 -> %2").arg(source,what);
        return taken;
    };
    if(shell_.psMenuOpen())return decide(false,"declined (the system menu binds no Triangle)");
    if(osk_.open || userCreate_.open || plEdit_.open)return decide(false,"declined (a text page owns input)");
    if(shell_.optionsOpen()){shell_.toggleOptions();update();return decide(true,"Triangle: options closed");}
    if(shell_.videoOpen()){toggleVideoPanel();return decide(true,"Triangle: video control matrix");}
    if(musicPanelOpen_){closeMusicPanel();update();return decide(true,"Triangle: music panel closed");}
    if(shell_.photoOpen()){shell_.rotatePhoto(1);update();return decide(true,"Triangle: photo rotated");}
    if(!shell_.mediaOpen() || shell_.videoOpen()){shell_.toggleOptions();update();return decide(true,QString("Triangle: options %1").arg(shell_.optionsOpen()?"opened":"not available here"));}
    if(mediaAudioOnly()){toggleMusicPanel();update();return decide(true,"Triangle: music panel");}
    shell_.cycleMediaRepeat();update();
    return decide(true,"Triangle: repeat cycled");
}
void View::restoreMasterVolume() {
    if(volumeStart_<0) {
        qWarning()<<"volume: VOLRESTORE has no start-up level to put back (the endpoint was unavailable at start)";
        return;
    }
    const bool muteSet=host::setMasterMuted(volumeStartMuted_);
    volumeApply(volumeStart_,QStringLiteral("VOLRESTORE"));
    qInfo().noquote()<<QString("volume: restored the start-up state %1% muted %2 -> Windows reports %3% muted %4 (mute call ok %5)")
        .arg(volumeStart_).arg(volumeStartMuted_?1:0).arg(volumeLevel_).arg(volumeMuted_?1:0).arg(muteSet?1:0);
}
void View::pollTriggerSeek(float down,float up) {
    // L2/R2 are the video container's own seek pair: the hint art carries tex_cp_help_l2_left / _r2_right
    // with msg_fast_r_with_icon / msg_fast_f_with_icon ("Fast Reverse" / "Fast Forward",
    // audit/MEDIA-PANELS-FIRMWARE.md 3.4, 3.6). The console binds no volume to them (BEHAVIOUR-GAP-AUDIT
    // A11), so in the video player this is the only thing they do. One 10 s step per press - the same step
    // Left/Right uses through Shell::seekMedia - because no local artifact times a held trigger and an
    // acceleration curve must not be invented.
    // The triggers are analog axes, not D-pad bits: nothing here touches `buttons`, `repeatAt_` or the
    // navigation paths, so a trigger cannot move the XMB or consume a D-pad repeat.
    constexpr float kPress=.35f;      // a resting DualShock 3/4 trigger reads 0.0; this is the press threshold
    const bool pressed=std::max(down,up)>=kPress;
    if(pressed==triggerHeld_)return;  // only the threshold crossing is a press: no repeat while held
    triggerHeld_=pressed;
    // H10: the same pair seeks a local or Jellyfin music track too (any seekable, non-photo media); the music
    // container's own hint art is unverified for L2/R2, so this follows the video container (INFERRED).
    if(!pressed || volumeInputBlocked() || !shell_.mediaSeekable())return;
    const int direction=up>down?1:-1;
    shell_.seekMedia(qint64(direction)*10000);
    if(volumeTrace())qInfo().noquote()<<QString("volume: trace trigger seek %1 -> %2 ms")
        .arg(direction>0?"R2":"L2").arg(shell_.mediaPosition());
}
// The firmware's volume display (audit/MEDIA-PANELS-FIRMWARE.md section 5): one page holding a single paf
// PhLevelMeter and nothing else that draws. The segment art is the console's default level-meter set in
// system_plugin.rco - tex_default_lvmeter_on / _off / _focus, 20x40 each - over the 48x48
// tex_default_lvmeter_bg backing, with no caption, no percentage, no plate of its own and no border (5.3).
// The widget's screen rect and its segment count are in no container (5.4, spec-only), so both are FITS
// here: ten 20 px segments (200x40) centred on x 960 in the frame's bottom band, which keeps the meter clear
// of the information panel (y 75..133), the notification plaque (y 145..229) and the media chrome above it.
// The fade is the traced 150 ms with 0 delay (explore_plugin.prx 0x270a00 / 0x270a04); the caller applies it,
// so the appearing edge is the firmware's while the visible window stays MediaControls'.
void View::drawVolumeHud(QPainter& p,double opacity) {
    const double alpha=std::clamp(opacity,0.,1.);
    if(alpha<=.002)return;
    const auto& lit=assets_.image("system_plugin","tex_default_lvmeter_on");
    const auto& dim=assets_.image("system_plugin","tex_default_lvmeter_off");
    const auto& focus=assets_.image("system_plugin","tex_default_lvmeter_focus");
    const auto& backing=assets_.image("system_plugin","tex_default_lvmeter_bg");
    if(lit.isNull()||dim.isNull()||focus.isNull()||backing.isNull()) {
        // The decoder reads these (audit/TEXTURE-COVERAGE.md: 3174/3174); if a future flash cannot, the
        // pre-meter HUD is drawn exactly as this build shipped it, and says so once.
        static bool meterWarned=false;
        if(!meterWarned) {
            meterWarned=true;
            qWarning().noquote()<<"view: volume meter art system_plugin/tex_default_lvmeter_* did not decode;"
                " drawing the 222x58 information-bar HUD";
        }
        const QRectF panel(1000,75,222,58);
        p.save();p.setOpacity(alpha);
        const QImage& plate=assets_.image("xmb_plugin_normal","tex_indi_bg");
        if(plate.isNull()) {
            p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,20));p.drawRoundedRect(panel,4,4);
            p.setOpacity(.12*alpha);p.drawImage(panel,assets_.image("system_plugin","tex_infobar"));
            p.setOpacity(alpha);p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(255,255,255,90),1));
            p.drawRoundedRect(panel.adjusted(.5,.5,-.5,-.5),4,4);
        } else drawNineSlice(p,"xmb_plugin_normal","tex_indi_bg",panel,8.,alpha);
        mediaPanelText(p,QRectF(panel.left()+12,panel.top()+7,150,18),
            assets_.text("videoplayer_plugin","msg_volume_control","Volume Control"),15,QColor(220,220,220),
            Qt::AlignLeft|Qt::AlignVCenter);
        const int level=std::clamp(volumeLevel_,0,100);
        mediaPanelText(p,QRectF(panel.right()-122,panel.top()+5,110,24),
            volumeMuted_?QStringLiteral("Mute"):QStringLiteral("%1%").arg(level),19,
            volumeMuted_?QColor(200,200,200):Qt::white,Qt::AlignRight|Qt::AlignVCenter);
        const QRectF track(panel.left()+12,panel.top()+37,panel.width()-24,6);
        p.setPen(Qt::NoPen);p.setBrush(QColor(255,255,255,55));p.drawRect(track);
        if(level>0)p.fillRect(QRectF(track.left(),track.top(),track.width()*level/100.,track.height()),
            volumeMuted_?QColor(150,150,150):QColor(255,255,255));
        p.restore();
        return;
    }
    // Level 0 lights no segment, 100 lights all ten. A muted endpoint keeps the level Windows reports but
    // shows it unlit: the firmware's volume page carries no mute state of its own (5.3), so silence is drawn
    // as silence rather than by inventing a second material.
    const int level=std::clamp(volumeLevel_,0,100);
    const int litCount=volumeMuted_?0:(level*volumeMeterSegments+50)/100;
    constexpr qreal segmentWidth=20.,segmentHeight=40.;   // tex_default_lvmeter_* are 20x40 (FC)
    const qreal meterWidth=segmentWidth*volumeMeterSegments;
    const QRectF meter(960.-meterWidth/2.,1008.,meterWidth,segmentHeight);   // FIT (audit 5.4)
    p.save();
    // The plate and the segments must fade together: drawNineSlice has its own opacity argument, so passing
    // 1.0 left the backing fully opaque while the segments faded with p.setOpacity(alpha), which is why the
    // HUD showed a raised plate with no segments at any level (SETTINGS-COVERAGE section 8.8 item 3). Both now
    // take the same alpha.
    drawNineSlice(p,"system_plugin","tex_default_lvmeter_bg",meter.adjusted(-8,-8,8,8),16.,alpha);
    p.setOpacity(alpha);
    for(int i=0;i<volumeMeterSegments;++i) {
        // Which segment paf draws with _focus is not traced (5.4); the boundary segment - the one at the
        // level - is the only state this surface has that could be called focused, so that is the one.
        const QImage& art=i<litCount?lit:(i==litCount-1?focus:dim);
        if(art.isNull())continue;
        p.drawImage(QRectF(meter.left()+i*segmentWidth,meter.top(),segmentWidth,segmentHeight),art);
    }
    p.restore();
}
// The shared label path for the three media panels. The GL glyph atlas is the XMB's own text pipeline, so
// the panels' labels keep the firmware's shadow (4/-5 at 0.25) through the renderer's default material.
void View::mediaPanelText(QPainter& p,const QRectF& rect,const QString& value,int size,const QColor& colour,int flags) {
    if(textRenderer_.ready()) {
        QFont font(shell_.fontFamily());font.setPixelSize(size);
        textRenderer_.draw(*this,p,rect,value,font,colour,flags);return;
    }
    QFont font(shell_.fontFamily());font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
    p.setPen(QColor(0,0,0,38));p.drawText(rect.translated(1,1),flags,value);
    p.setPen(colour);p.drawText(rect,flags,value);
}
// Surface A, the time bar (audit/MEDIA-PANELS-FIRMWARE.md 3.5): the firmware's time page is a Progress stack
// (base / playable_time / playing) plus a Scroll and a jump-marker Plane, and the container carries the
// progression art - tex_progress_bar 30x20 with the tex_progress_bar_in fill, and tex_progress_bar_jump_indi
// 20x12 on videoplayer_display_time_jump_time_bar_marker. None of the three Progress objects declares a rect
// or an image of its own (their template fields are zero/unset; the plugin publishes them at run time), so
// the screen rect here is a FIT: it keeps this build's bottom band (x 110, 1700 wide) at the texture's own
// 20 px height. The readout keeps the firmware's per-glyph unit - 16x16 cells, one glyph per cell - with the
// digits from MediaControls::formatTime.
void View::drawVideoTimeBar(QPainter& p,qint64 position,qint64 duration,double opacity) {
    const QImage& base=assets_.image("videoplayer_plugin","tex_progress_bar");
    const QImage& fill=assets_.image("videoplayer_plugin","tex_progress_bar_in");
    const QRectF bar(110,941,1700,20);                                  // FIT
    const double ratio=duration>0?std::clamp(double(position)/double(duration),0.,1.):0.;
    if(base.isNull()||fill.isNull()) {
        // One line, once: the flat 4 px bar and the single MM:SS / MM:SS string this build shipped.
        static bool barWarned=false;
        if(!barWarned) {
            barWarned=true;
            qWarning().noquote()<<"view: videoplayer_plugin/tex_progress_bar(_in) did not decode; drawing the"
                " flat 4 px bar and the single time string";
        }
        p.fillRect(QRectF(110,941,1700,4),QColor(255,255,255,60));
        p.fillRect(QRectF(110,941,1700*ratio,4),Qt::white);
        mediaPanelText(p,QRectF(1450,963,360,45),MediaControls::formatTime(position)+" / "+MediaControls::formatTime(duration),
            23,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
        return;
    }
    // The track: the 30x20 progression tile nine-sliced (8 px caps keep the end caps at their own width).
    drawNineSlice(p,"videoplayer_plugin","tex_progress_bar",bar,8.,opacity);
    if(ratio>0.) {
        const QRectF played(bar.left(),bar.top(),bar.width()*ratio,bar.height());
        if(played.width()>=16.)drawNineSlice(p,"videoplayer_plugin","tex_progress_bar_in",played,8.,opacity);
        else{p.save();p.setOpacity(opacity);p.drawImage(played,fill);p.restore();}
    }
    // The jump indicator marks a position on the bar (its own 20x12 art). This build seeks
    // instantaneously and has no scrub state, so there is no pending jump target to mark: it marks the
    // played position - the one position the bar's jump indicator can denote here.
    const QImage& marker=assets_.image("videoplayer_plugin","tex_progress_bar_jump_indi");
    if(!marker.isNull()) {
        const double x=std::clamp(bar.left()+bar.width()*ratio-marker.width()/2.,
            bar.left(),bar.right()-marker.width());
        p.save();p.setOpacity(opacity);
        p.drawImage(QRectF(x,bar.top()+(bar.height()-marker.height())/2.,marker.width(),marker.height()),marker);
        p.restore();
    }
    // Two 8-cell groups, as the firmware lays them out (videoplayer_display_playing_time_h1..c2 and
    // play_time_h1..c2, all 16x16): the playing time on the left, the duration on the right.
    const auto time=[&](qint64 ms) {
        QString value=MediaControls::formatTime(ms);
        if(shell_.videoOpen() && ms>=0) {
            if(value.size()==5)value.prepend(QStringLiteral("00:"));
            else if(value.size()==7)value.prepend(QLatin1Char('0'));
        }
        return value;
    };
    drawTimeCells(p,time(position),bar.left(),bar.bottom()+2.,false,opacity);
    drawTimeCells(p,time(duration),bar.right(),bar.bottom()+2.,true,opacity);
}
void View::drawTimeCells(QPainter& p,const QString& value,qreal edge,qreal top,bool rightAligned,double opacity) {
    constexpr qreal cell=16.;                 // FC: every time digit cell is 16x16
    constexpr int cellSize=19;                // FIT: RCO size=10 has no px equivalent (audit 9.5); 19 px
                                              // puts the digit cap (about 0.82 x the size) inside 16 px
    const int count=int(value.size());
    if(count<=0)return;
    const qreal left=rightAligned?edge-count*cell:edge;
    for(int i=0;i<count;++i)
        mediaPanelText(p,QRectF(left+i*cell,top,cell,cell),value.mid(i,1),cellSize,QColor(255,255,255,int(std::lround(255.*opacity))),Qt::AlignCenter);
}
// Surface A, the direct-key hint strip. The firmware's panel hint is one 86x30 tex_cp_help_* key icon plus
// the focused control's own name from videoplayer_plugin's text table (videoplayer_cp_help and
// videoplayer_cp_button_name, audit 3.2/3.4/3.6); this build has six bindings, so six pairs are drawn in
// one row. The native keys and the firmware's key icons do not all coincide: this build seeks with
// Left/Right where the firmware's fast reverse/forward are L2/R2, and the video container carries no d-pad
// hint art (3.4 lists circle, cross, l1, l2_left, r1, r2_right, select, start), so the L2/R2 icons carry the
// seek bindings. Circle's label is msg_quit_playback: this container has no Back/Cancel string, and Circle
// here leaves playback, which is what that string says.
void View::drawVideoHelpStrip(QPainter& p) {
    if(videoPanelOpen_) {
        // All eight direct-key materials provided by this RCO. Pixel spacing is a fit in 110..1810;
        // the template help plane has no image/rect and its runtime composition is not available.
        const char* keys[]={"circle","cross","l1","l2_left","r1","r2_right","select","start"};
        for(int i=0;i<8;++i) {
            const QImage& art=assets_.image("videoplayer_plugin",QStringLiteral("tex_cp_help_")+keys[i]);
            if(!art.isNull())p.drawImage(QRectF(110.+i*(1700.-86.)/7.,1026.,86.,30.),art);
        }
        return;
    }
    struct Help { const char* icon; QString label; };
    const bool paused=shell_.mediaPaused()||shell_.busy();
    const auto text=[&](const char* key,const char* fallback) {
        // The firmware's *_with_icon strings end with the control's own gaiji after a space
        // (msg_play_with_icon = "Play " U+F88F, msg_fast_r_with_icon = "Fast Reverse " U+F893, ...): the
        // trailing glyph IS the icon the name refers to. This strip draws that icon itself as the 86x30
        // key texture, so the private-use glyph is dropped instead of drawing as a missing-glyph box.
        QString value=assets_.text("videoplayer_plugin",key,fallback);
        while(!value.isEmpty() && value.back().unicode()>=0xE000 && value.back().unicode()<=0xF8FF)
            value.chop(1);
        return value.trimmed();
    };
    const QVector<Help> help{
        {"tex_cp_help_circle",text("msg_quit_playback","Quit Playback")},
        {"tex_cp_help_cross",paused?text("msg_play_with_icon","Play"):text("msg_pause_with_icon","Pause")},
        {"tex_cp_help_l2_left",text("msg_fast_r_with_icon","Fast Reverse")},
        {"tex_cp_help_r2_right",text("msg_fast_f_with_icon","Fast Forward")},
        {"tex_cp_help_l1",text("msg_prev_with_icon","Previous")},
        {"tex_cp_help_r1",text("msg_next_with_icon","Next")}};
    if(assets_.image("videoplayer_plugin","tex_cp_help_circle").isNull()) {
        static bool helpWarned=false;
        if(!helpWarned) {
            helpWarned=true;
            qWarning().noquote()<<"view: videoplayer_plugin/tex_cp_help_* did not decode; drawing the ASCII"
                " advice sentence";
        }
        mediaPanelText(p,QRectF(110,1020,1700,35),
            "Circle Back   Cross Play/Pause   Left/Right Seek   L1/R1 or [/] Track   Square/M Shuffle"
            "   R Repeat   Triangle Options",21,QColor(230,230,230),Qt::AlignLeft|Qt::AlignVCenter);
        return;
    }
    // FIT: six equal 283 px slots across the same 110..1810 band the bar uses; the icon keeps its 86x30
    // size and the label takes the rest of its slot at 19 px (the size-10 fit above).
    constexpr qreal stripLeft=110.,stripWidth=1700.,iconWidth=86.,iconHeight=30.;
    const qreal slot=stripWidth/double(help.size());
    for(int i=0;i<help.size();++i) {
        const qreal x=stripLeft+i*slot;
        drawImage(p,"videoplayer_plugin",help[i].icon,QRectF(x,1026.,iconWidth,iconHeight),p.opacity());
        mediaPanelText(p,QRectF(x+iconWidth+6.,1026.,slot-iconWidth-10.,iconHeight),help[i].label,19,
            QColor(238,238,238),Qt::AlignLeft|Qt::AlignVCenter);
    }
}
namespace {
enum class VideoCommand { Unavailable, Audio, Subtitle, Volume, Display, Beginning, Next, SeekBack,
    SeekForward, Play, Pause, Stop, Repeat };
struct VideoButton {
    int x,y;
    const char* icon;
    const char* label;
    VideoCommand command;
    const char* reason=nullptr;
};
// videoplayer_plugin.rco @0xc1e0..0xd740. Preserve the iconless dummy, not a fabricated 25th icon.
const VideoButton videoButtons[]={
    {-6,1,nullptr,nullptr,VideoCommand::Unavailable,"The firmware dummy slot has no control."},
    {-5,1,"tex_cp_scene_search","msg_scene_search",VideoCommand::Unavailable,"Scene thumbnails and scene search are not implemented by this player."},
    {-4,1,"tex_cp_jump","msg_goto",VideoCommand::Unavailable,"This player has bounded relative seeking, but no time-entry or chapter selector."},
    {-3,1,"tex_cp_audio","msg_audio_change",VideoCommand::Audio},
    {-2,1,"tex_cp_subtitle","msg_subtitle_change",VideoCommand::Subtitle},
    {-1,1,"tex_cp_volume","msg_volume_control",VideoCommand::Volume},
    {0,1,"tex_cp_av_settings","msg_audio_video_settings",VideoCommand::Unavailable,"PS3 AV output settings have no equivalent in this video backend."},
    {1,1,"tex_cp_screen_mode","msg_viewmode",VideoCommand::Unavailable,"Video is aspect-preserving and letterboxed; no video screen-mode selector exists."},
    {2,1,"tex_cp_change_icon","msg_icon_change",VideoCommand::Unavailable,"The PC library has no PS3 video-icon database to update."},
    {3,1,"tex_cp_delete","msg_del",VideoCommand::Unavailable,"The media library does not expose file deletion."},
    {4,1,"tex_cp_display","msg_display_display_with_icon",VideoCommand::Display},
    {-6,0,"tex_cp_prev","msg_beginning_with_icon",VideoCommand::Beginning},
    {-5,0,"tex_cp_next","msg_next_with_icon",VideoCommand::Next},
    {-4,0,"tex_cp_search_b","msg_fast_r_with_icon",VideoCommand::SeekBack},
    {-3,0,"tex_cp_search_f","msg_fast_f_with_icon",VideoCommand::SeekForward},
    {-2,0,"tex_cp_play","msg_play_with_icon",VideoCommand::Play},
    {-1,0,"tex_cp_pause","msg_pause_with_icon",VideoCommand::Pause},
    {0,0,"tex_cp_stop","msg_stop",VideoCommand::Stop},
    {1,0,"tex_cp_flash_b","msg_flash_r",VideoCommand::SeekBack},
    {2,0,"tex_cp_flash_f","msg_flash_f",VideoCommand::SeekForward},
    {3,0,"tex_cp_slow_b","msg_slow_r",VideoCommand::Unavailable,"Qt playback has no implemented reverse slow-motion transport."},
    {4,0,"tex_cp_slow_f","msg_slow_f",VideoCommand::Unavailable,"This player has no slow-motion rate control."},
    {5,0,"tex_cp_step_b","msg_oneframe_r",VideoCommand::Unavailable,"Qt seeking is not frame-accurate; reverse frame stepping cannot be promised."},
    {6,0,"tex_cp_step","msg_oneframe_f",VideoCommand::Unavailable,"Qt seeking is not frame-accurate; frame advance cannot be promised."},
    {0,-1,"tex_cp_repeat","msg_repeat",VideoCommand::Repeat}
};
QString videoButtonLabel(Assets& assets,const VideoButton& button) {
    QString value=assets.text("videoplayer_plugin",button.label,QString::fromLatin1(button.label));
    while(!value.isEmpty() && value.back().unicode()>=0xE000 && value.back().unicode()<=0xF8FF)value.chop(1);
    return value.trimmed();
}
}
void View::toggleVideoPanel() {
    if(!shell_.videoOpen() || !shell_.message().isEmpty())return;
    videoPanelOpen_=!videoPanelOpen_;
    mediaControls_.activity(clock_.elapsed());
    videoPanelNotice_.clear();
    qInfo()<<"media: video control matrix"<<(videoPanelOpen_?"opened":"closed");
    update();
}
void View::videoPanelMove(int dx,int dy) {
    const auto& current=videoButtons[videoPanelFocus_];
    int best=-1,score=10000;
    for(int i=1;i<int(std::size(videoButtons));++i) {
        const auto& candidate=videoButtons[i];
        const int x=candidate.x-current.x,y=candidate.y-current.y;
        if(dx && (y!=0 || x*dx<=0))continue;
        if(dy && y*dy<=0)continue; // RCO +y points up
        const int distance=dx?std::abs(x):std::abs(y)*20+std::abs(x); // 20 > the grid's maximum 12-cell x distance
        if(distance<score){score=distance;best=i;}
    }
    if(best>=0)videoPanelFocus_=best;
    videoPanelNotice_.clear();mediaControls_.activity(clock_.elapsed());update();
}
void View::videoPanelActivate() {
    const auto& button=videoButtons[videoPanelFocus_];
    const bool seek=button.command==VideoCommand::SeekBack || button.command==VideoCommand::SeekForward || button.command==VideoCommand::Beginning;
    const QString reason=button.reason?QString::fromLatin1(button.reason):
        seek && !shell_.mediaSeekable()?QStringLiteral("This source is not seekable."):
        button.command==VideoCommand::Volume && volumeLevel_<0?QStringLiteral("No Windows audio endpoint is available."):QString{};
    if(!reason.isEmpty()) {
        // Same parity classification as XMB rows. No fake successful action and no file/system mutation.
        Entry refused;refused.action=Action::Unavailable;refused.title=videoButtonLabel(assets_,button);refused.detail=reason;
        videoPanelNotice_=refused.detail;
        qInfo().noquote()<<"media: Action::Unavailable"<<refused.title<<"-"<<refused.detail;
        update();return;
    }
    qInfo().noquote()<<"media: video button"<<videoButtonLabel(assets_,button);
    videoPanelStatus_.clear();
    if(seek)videoPanelStatus_=videoPanelFocus_==18?"tex_i_flash_b":videoPanelFocus_==19?"tex_i_flash_f":"tex_i_jump";
    else if(button.command==VideoCommand::Next)videoPanelStatus_="tex_i_next";
    // FIT: the action indication shares the existing 3000 ms native control hold, never a speed badge.
    videoPanelStatusUntil_=clock_.elapsed()+3000;
    switch(button.command) {
    case VideoCommand::Audio:shell_.openMediaOption("audio");break;
    case VideoCommand::Subtitle:shell_.openMediaOption("subtitle");break;
    case VideoCommand::Volume:volumeApply(volumeLevel_,QStringLiteral("VideoPanel"));break;
    case VideoCommand::Display:shell_.toggleMediaDisplay();break;
    case VideoCommand::Beginning:shell_.seekMedia(-shell_.mediaPosition());break;
    case VideoCommand::Next:shell_.skipMedia(1);break;
    case VideoCommand::SeekBack:shell_.seekMedia(-10000);break;
    case VideoCommand::SeekForward:shell_.seekMedia(10000);break;
    case VideoCommand::Play:if(shell_.mediaPaused())shell_.togglePlayback();videoPanelOpen_=false;break;
    case VideoCommand::Pause:if(!shell_.mediaPaused())shell_.togglePlayback();break;
    case VideoCommand::Stop:videoPanelOpen_=false;shell_.back();break;
    case VideoCommand::Repeat:shell_.cycleMediaRepeat();break;
    default:break;
    }
    mediaControls_.activity(clock_.elapsed());update();
}
void View::drawVideoPanel(QPainter& p) {
    // FIT: 100 px pitch bounds the 70 px icons (30 px separation), 13 columns span 1270 px.
    // Anchor: x=960, row +1 at y=650, leaving 80 px before the existing y=941 time bar.
    // No tex_cp_panel exists here: use this container's under-bar, not the music player's plate.
    const QImage& under=assets_.image("videoplayer_plugin","tex_i_under_bar");
    if(!under.isNull())p.drawImage(QRectF(110,610,1700,320),under);
    const double inherited=p.opacity();
    for(int i=1;i<int(std::size(videoButtons));++i) {
        const auto& button=videoButtons[i];
        const QString name=QString::fromLatin1(button.icon);
        const QImage& art=assets_.image("videoplayer_plugin",name);
        const QRectF box(960.+button.x*100.-(i==24?49.:35.),650.+(1-button.y)*70.,i==24?98.:70.,70.);
        // Reuse the XMB's existing disabled-row icon dim, not a claimed firmware availability alpha.
        p.save();p.setOpacity(inherited*(button.command==VideoCommand::Unavailable?.5:1.));
        const QImage& shadow=assets_.image("videoplayer_plugin",name+"_shadow");
        if(!shadow.isNull())p.drawImage(box,shadow);
        if(!art.isNull())p.drawImage(box,art);
        p.restore();
        if(i==videoPanelFocus_) {
            const QImage& focus=assets_.image("videoplayer_plugin",name+"_focus");
            if(!focus.isNull())p.drawImage(box,focus);
        }
    }
    const auto& button=videoButtons[videoPanelFocus_];
    mediaPanelText(p,QRectF(560,864,800,28),videoButtonLabel(assets_,button),19,Qt::white,Qt::AlignCenter);
    QString note=videoPanelNotice_;
    if(note.isEmpty() && button.reason)note=QString::fromLatin1(button.reason);
    if(note.isEmpty() && (button.command==VideoCommand::SeekBack || button.command==VideoCommand::SeekForward))
        note=QStringLiteral("10-second bounded seek; continuous speed scanning is not implemented.");
    if(!note.isEmpty() && !shell_.busy())mediaPanelText(p,QRectF(210,897,1500,30),note,17,QColor(225,225,225),Qt::AlignCenter);
    // The 14 tex_i_* transport variants share status_icon; only actual states are published.
    // No slow/search/step badge is manufactured for an instantaneous seek.
    const QString statusName=!videoPanelStatus_.isEmpty() && clock_.elapsed()<videoPanelStatusUntil_
        ?videoPanelStatus_:QString(shell_.mediaPaused()?"tex_i_pause":"tex_i_play");
    const QImage& status=assets_.image("videoplayer_plugin",statusName);
    if(!status.isNull())p.drawImage(QRectF(110,530,74,74),status);
    if(shell_.mediaRepeatMode()!=0) {
        const QImage& repeat=assets_.image("videoplayer_plugin","tex_i_repeat");
        if(!repeat.isNull())p.drawImage(QRectF(194,530,98,74),repeat);
        if(shell_.mediaRepeatMode()==1) {
            const QImage& title=assets_.image("videoplayer_plugin","tex_i_title_repeat");
            if(!title.isNull())p.drawImage(QRectF(207,604,72,36),title);
        }
    }
    mediaPanelText(p,QRectF(310,550,1400,36),shell_.mediaTitle(),23,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
}
// Surface B2, the XMB music control panel (audit/MEDIA-PANELS-FIRMWARE.md 4.2-4.3): explore_plugin_full's
// control_panel_page - a 72x72 tex_cp_panel plate scaled 1.3 (0x270a54), ten status planes, a 200x6
// progress_bar_playing_time with label_playing_time, and seven 90x60 buttons sharing tex_cp_focus. The page,
// the plate and the button group all carry zero geometry (4.1); the anchor and the internal layout are FITS,
// while every object size, cell and texture name is the container's own. The panel opens on Triangle, where
// this build used to cycle Repeat (7.2); R keeps the Repeat binding.
void View::drawMusicPanel(QPainter& p,double opacity) {
    const double alpha=std::clamp(opacity,0.,1.);
    if(alpha<=.002)return;
    static const char* const icons[musicPanelButtons]={
        "tex_cp_prev","tex_cp_next","tex_cp_pause","tex_cp_stop","tex_cp_repeat","tex_cp_shuffle","tex_cp_volume",nullptr};
    constexpr int visualPlayerButton=musicPanelButtons-1;   // audioplayer_plugin msg_visualizer_with_icon_button
    const QImage& plate=assets_.image("explore_plugin_full","tex_cp_panel");
    QString missing=plate.isNull()?QStringLiteral("tex_cp_panel"):QString{};
    for(int i=0;i<visualPlayerButton && missing.isEmpty();++i)
        if(assets_.image("explore_plugin_full",QString::fromLatin1(icons[i])).isNull())
            missing=QString::fromLatin1(icons[i]);
    if(!missing.isEmpty()) {
        // One line, once: the panel this change adds is the only surface lost, so the player keeps the
        // chrome it had and no half-drawn plate appears.
        static bool panelWarned=false;
        if(!panelWarned) {
            panelWarned=true;
            qWarning().noquote()<<"view: explore_plugin_full/"<<missing
                <<"did not decode; the music control panel is not drawn";
        }
        return;
    }
    // The firmware's own object sizes (FC): 7 buttons of 90x60, status planes of 74x74 (98x74 for
    // repeat/shuffle, 72x36 for the "1" badge), progress_bar_playing_time 200x6, label_playing_time size 10.
    // The 12 px padding around the content is a FIT.
    constexpr qreal buttonWidth=90.,buttonHeight=60.,statusHeight=74.,padding=12.;
    constexpr qreal barWidth=200.,barHeight=6.;
    const qreal contentWidth=buttonWidth*musicPanelButtons;
    const qreal contentHeight=statusHeight+16.+buttonHeight;
    const qreal plateWidth=(contentWidth+2.*padding)*musicPanelScale;
    const qreal plateHeight=(contentHeight+2.*padding)*musicPanelScale;
    // FIT (audit 9.2): the panel's screen anchor is a runtime projection. Centred on x 960 with its bottom
    // edge at y 1000 - the frame's bottom band, above the volume meter and clear of the info bar.
    const QRectF resting((1920.-plateWidth)/2.,1000.-plateHeight,plateWidth,plateHeight);
    p.save();
    p.setOpacity(alpha);
    // The traced 150/100 move/size animation on the base (0x270a58 / 0x270a5c, alpha 1 at 0x270a50) is the
    // plate growing from its own size to 1.3 x that size, upward from its resting bottom edge. The 100-unit
    // delay is dropped: PafEase has no delay parameter and the unit is unproven (audit 9.4).
    const double grow=1./musicPanelScale+(1.-1./musicPanelScale)*alpha;
    const QRectF drawn(resting.center().x()-plateWidth*grow/2.,resting.bottom()-plateHeight*grow,
        plateWidth*grow,plateHeight*grow);
    // tex_cp_panel is a 72x72 stretchable plate: nine-sliced at a 20 px inset so the corners keep their pixels.
    drawNineSlice(p,"explore_plugin_full","tex_cp_panel",drawn,20.,1.);
    const qreal contentLeft=drawn.center().x()-contentWidth/2.;
    // FIT: the content is centred in the scaled plate, which is what leaves the 1.3 scaling its even margin.
    const qreal contentTop=drawn.center().y()-contentHeight/2.;
    // control_play_status_group: one plane per state. This build reports playing and paused, plus the
    // repeat and shuffle modes; the transport planes the shell has no state for (prev/next/searchb/searchf/
    // stop) stay undrawn rather than fabricated.
    const bool paused=shell_.mediaPaused()||shell_.busy();
    const QImage& status=assets_.image("explore_plugin_full",
        paused?QStringLiteral("tex_pause_status"):QStringLiteral("tex_play_status"));
    qreal cursor=contentLeft;
    if(!status.isNull()) {
        p.drawImage(QRectF(cursor,contentTop,status.width(),status.height()),status);
        cursor+=status.width()+8.;
    }
    QStringList badges;
    const int repeat=shell_.mediaRepeatMode();
    if(repeat==1)badges<<"tex_1repeat_status";
    else if(repeat==2)badges<<"tex_repeat_status";
    // Shell names the repeat mode directly; the shuffle flag has no accessor inside this change's write
    // fence, so the shell's own queue-summary line is the authority for it.
    if(shell_.mediaQueueSummary().contains(QLatin1String("Shuffle")))badges<<"tex_shuffle_status";
    for(const auto& name:badges) {
        const QImage& art=assets_.image("explore_plugin_full",name);
        if(art.isNull())continue;
        p.drawImage(QRectF(cursor,contentTop,art.width(),art.height()),art);
        cursor+=art.width()+8.;
    }
    // progress_bar_playing_time (200x6) and label_playing_time. The Progress object carries no image in this
    // container, so the widget draws paf's default progression art from system_plugin
    // (tex_default_progress_base / tex_default_progress_slider, 24x20) - the same "the defaults live in
    // system_plugin" rule the volume meter follows (5.2).
    const qint64 position=shell_.mediaPosition(),total=shell_.mediaDuration();
    const QRectF barRect(contentLeft+contentWidth-barWidth,contentTop+56.,barWidth,barHeight);
    mediaPanelText(p,QRectF(barRect.left(),contentTop+26.,barWidth,22.),
        MediaControls::formatTime(position)+" / "+MediaControls::formatTime(total),19,
        QColor(240,240,240),Qt::AlignRight|Qt::AlignVCenter);
    const QImage& barBase=assets_.image("system_plugin","tex_default_progress_base");
    const QImage& barFill=assets_.image("system_plugin","tex_default_progress_slider");
    if(!barBase.isNull()&&!barFill.isNull()&&total>0) {
        const double ratio=std::clamp(double(position)/double(total),0.,1.);
        p.drawImage(barRect,barBase);
        if(ratio>0.)p.drawImage(QRectF(barRect.left(),barRect.top(),barRect.width()*ratio,barRect.height()),barFill);
    }
    // The 7-button group in the firmware's table order (cells 0, -1 ... -6), each icon at its own 90x60,
    // with the shared tex_cp_focus drawn under the focused button.
    const QImage& focus=assets_.image("explore_plugin_full","tex_cp_focus");
    for(int i=0;i<musicPanelButtons;++i) {
        const QRectF box(contentLeft+i*buttonWidth,contentTop+statusHeight+16.,buttonWidth,buttonHeight);
        if(i==musicPanelFocus_ && !focus.isNull())p.drawImage(box,focus);
        if(i==visualPlayerButton) {
            // The Visual Player button's own art (audioplayer_plugin tex_cp_vislizer, 70x70) centred in the 90x60 cell;
            // it lights while the visualizer is on.
            const QImage& art=assets_.image("audioplayer_plugin","tex_cp_vislizer");
            if(!art.isNull()) {
                p.save();
                p.setOpacity(alpha*(musicVisualizerOn_||i==musicPanelFocus_?1.:.8));
                p.drawImage(QRectF(box.center().x()-art.width()/2.,box.center().y()-art.height()/2.,art.width(),art.height()),art);
                p.restore();
            }
            continue;
        }
        p.drawImage(box,assets_.image("explore_plugin_full",QString::fromLatin1(icons[i])));
    }
    // The focused Visual Player button names itself with the plugin's own string, under the plate.
    if(musicPanelFocus_==visualPlayerButton) {
        // The string ends in a button gaiji (private-use code point) that only the image font can draw; the label is the words.
        QString label=assets_.text("audioplayer_plugin","msg_visualizer_with_icon","Visual Player ");
        label.removeIf([](QChar ch){return ch.unicode()>=0xE000 && ch.unicode()<=0xF8FF;});
        label=label.trimmed();
        mediaPanelText(p,QRectF(contentLeft+visualPlayerButton*buttonWidth+buttonWidth/2.-150.,resting.bottom()+6.,300.,30.),
            label,21,QColor(238,238,238),Qt::AlignHCenter|Qt::AlignVCenter);
    }
    p.restore();
}
// Surface B1 (audit/MUSIC-PLAYER.md). Every object name and size below is audioplayer_plugin's own (tree
// dump): title_information_group (plane_jacket_music_cover over tex_track_frame 240x240 with a 162x162
// cover, plane_track_number1/2 at 46x68, text_contents_title_name, text_artist_name), play_progress_group
// (text_track_number, audioplayer_display_time_bar height 20, plane_codec), play_status_group (planes of
// 74x74 / 98x74 / 72x36) and msg_help_control_panel_home_menu. The container's objects all carry zero
// position (the plugin places them at run time), so every rect is a FIT (INFERRED) read against the
// frame's own proportions; the album line is this build's addition for the tag the console's page lacks.
// The Visual Player stand-in. The firmware's visualizer is a separate engine the container does not carry
// (audit/MUSIC-PLAYER.md), so this is a faithful-in-spirit simplification driven by real levels: 40 log-spaced
// spectrum bars on a baseline under the track block, peak caps, and a soft glow behind the cover frame that follows
// loudness. Bars attack in ~40 ms and fall over ~180 ms; the idle style brightens and heightens them.
void View::drawMusicVisualizer(QPainter& p,double idle) {
    QElapsedTimer cost;cost.start();
    const qint64 now=clock_.elapsed();
    const double dt=std::clamp((now-musicVisAt_)/1000.,0.,.1);
    musicVisAt_=now;
    MusicTap* tap=shell_.musicTap();
    const bool playing=tap && musicVisualizerOn_ && !shell_.mediaPaused() && !shell_.busy();
    MusicSpectrum spectrum;
    bool have=false;
    // The tap analyses only while the Visual Player is on and playing: no request, no FFT.
    if(playing) {
        tap->request();
        have=tap->sample(shell_.mediaPosition(),spectrum);
    }
    // XMB_MUSICVIS_FIXED=1 (local verification, the pinned-state gate): a fixed, audio-independent spectrum stands in
    // for the tap while the Visual Player is on and playing, so the bars and glow settle to the same pixels every run.
    if(playing && qEnvironmentVariableIntValue("XMB_MUSICVIS_FIXED")!=0) {
        for(int i=0;i<int(spectrum.band.size());++i)spectrum.band[i]=.15f+.7f*float(std::abs(std::sin(.37*i+.4)))*float(1.-.012*i);
        spectrum.rms=.5f;have=true;
    }
    const int n=int(musicBars_.size());
    std::array<float,40> target{};
    if(have) {
        for(int i=0;i<n;++i) {   // a 3-tap blend keeps one noisy bin from spiking a lone bar
            const float l=spectrum.band[std::max(0,i-1)],r=spectrum.band[std::min(n-1,i+1)];
            target[i]=std::pow(.25f*l+.5f*spectrum.band[i]+.25f*r,1.25f);
        }
    }
    const double up=1.-std::exp(-dt/.04),down=1.-std::exp(-dt/.18);
    float peakLevel=0.f;
    for(int i=0;i<n;++i) {
        const float t=target[i];
        musicBars_[i]+=float((t>musicBars_[i]?up:down)*(t-musicBars_[i]));
        musicPeaks_[i]=std::max(musicBars_[i],musicPeaks_[i]-float(dt*.45));
        peakLevel=std::max(peakLevel,musicBars_[i]);
    }
    const float rmsTarget=have?spectrum.rms:0.f;
    musicLevel_+=float((rmsTarget>musicLevel_?up:down)*(rmsTarget-musicLevel_));
    // Fade the whole thing in on the first levels and out on pause / stop.
    const float alphaTarget=playing?1.f:0.f;
    musicVisAlpha_+=(alphaTarget-musicVisAlpha_)*float(1.-std::exp(-dt/.25));
    if(musicVisAlpha_<.01f && peakLevel<.01f)return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing,false);
    const double strength=musicVisAlpha_*(.34+.5*idle);
    // Glow behind the cover frame (frame centre 480,480): loudness widens and brightens it.
    {
        const double radius=200.+musicLevel_*110.;
        QRadialGradient glow(QPointF(480,480),radius);
        glow.setColorAt(0.,QColor(210,228,255,int(255.*musicVisAlpha_*(.05+.30*musicLevel_)*(1.+.5*idle))));
        glow.setColorAt(1.,QColor(210,228,255,0));
        p.setPen(Qt::NoPen);p.setBrush(glow);
        p.drawEllipse(QPointF(480,480),radius,radius);
    }
    const double left=110.,span=1700.,baseline=850.,maxHeight=240.*(1.+.45*idle),pitch=span/n,barWidth=pitch*.72;
    QLinearGradient fill(0,baseline,0,baseline-maxHeight);
    fill.setColorAt(0.,QColor(255,255,255,int(255.*strength)));
    fill.setColorAt(1.,QColor(150,200,255,int(255.*strength*.35)));
    p.setPen(Qt::NoPen);p.setBrush(fill);
    for(int i=0;i<n;++i) {
        const double h=std::max(3.,double(musicBars_[i])*maxHeight);
        p.drawRect(QRectF(left+i*pitch+(pitch-barWidth)/2.,baseline-h,barWidth,h));
    }
    p.setBrush(QColor(255,255,255,int(255.*std::min(1.,strength*1.6))));
    for(int i=0;i<n;++i) {
        if(musicPeaks_[i]<.03f)continue;
        const double y=baseline-double(musicPeaks_[i])*maxHeight-7.;
        p.drawRect(QRectF(left+i*pitch+(pitch-barWidth)/2.,y,barWidth,3.));
    }
    p.restore();
    if(static const bool trace=qEnvironmentVariableIsSet("XMB_MUSICVIS_TRACE");trace) {
        const double ms=cost.nsecsElapsed()/1e6;
        auto& s=musicVisStats_;
        ++s.frames;s.drawMs+=ms;s.worstMs=std::max(s.worstMs,ms);
        if(now-s.since>=2000) {
            float mean=0.f;for(float v:musicBars_)mean+=v;
            qInfo().noquote()<<QString("musicvis: %1 frames in %2 ms, draw avg %3 ms worst %4 ms, tap analysed %5 dropped %6, mean bar %7 rms %8 sample=%9")
                .arg(s.frames).arg(now-s.since).arg(s.drawMs/std::max(1,s.frames),0,'f',3).arg(s.worstMs,0,'f',3)
                .arg(tap?tap->buffersAnalysed():0).arg(tap?tap->buffersDropped():0).arg(mean/n,0,'f',3).arg(musicLevel_,0,'f',3).arg(have?1:0);
            s={now,0,0,0};
        }
    }
}
void View::drawMusicPage(QPainter& p) {
    const double idle=std::clamp(musicIdleEase_.value,0.,1.);
    const double chrome=1.-idle;
    const qint64 now=clock_.elapsed();
    const QString plugin=QStringLiteral("audioplayer_plugin");
    // back_screen_plane (tex_back_screen 1x1, template alphaScale 0): the black the idle style fades in.
    if(idle>.002)p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,int(std::lround(255.*musicIdleDim*idle))));
    drawMusicVisualizer(p,idle);
    p.save();p.setRenderHint(QPainter::SmoothPixmapTransform);
    // title_information_group: the 240x240 frame with the cover inside it.
    constexpr qreal frameSize=240.,coverSize=162.,left=360.,top=360.;
    const QRectF frame(left,top,frameSize,frameSize);
    const QRectF coverBox(frame.center().x()-coverSize/2.,frame.center().y()-coverSize/2.,coverSize,coverSize);
    const auto blit=[&](const char* name,const QRectF& rect,double amount=1.) {
        // The firmware draws each plane at its texture's own size, transparent margin included, so this is a
        // plain draw (View::drawImage would refit the alpha bounds to the rect and enlarge the art).
        const QImage& art=assets_.image(plugin,QString::fromLatin1(name));
        if(art.isNull())return;
        p.save();p.setOpacity(amount);p.drawImage(rect,art);p.restore();
    };
    p.setOpacity(1.);
    blit("tex_track_frame",frame);
    const QImage& jacket=shell_.mediaCoverArt();
    if(!jacket.isNull()) {
        const QSizeF fitted=QSizeF(jacket.size()).scaled(coverBox.size(),Qt::KeepAspectRatio);
        p.drawImage(QRectF(coverBox.center().x()-fitted.width()/2.,coverBox.center().y()-fitted.height()/2.,
            fitted.width(),fitted.height()),jacket);
    } else blit("tex_default_music_cover",coverBox);
    // plane_track_number1/2: the queue position in the firmware's 46x68 digit art.
    const int number=shell_.mediaTrackNumber();
    constexpr qreal textLeft=660.,digitW=46.,digitH=68.;
    if(number>0) {
        const QString digits=QString::number(number%100);
        for(int i=0;i<digits.size();++i)
            blit((QStringLiteral("tex_track_number")+digits[i]).toLatin1().constData(),
                QRectF(textLeft+i*(digitW+2.),top,digitW,digitH));
    }
    const auto elided=[&](const QString& value,int size,qreal width) {
        QFont font(shell_.fontFamily());font.setPixelSize(size);
        return QFontMetricsF(font).elidedText(value,Qt::ElideRight,width);
    };
    constexpr qreal textWidth=1100.;
    p.setOpacity(1.);
    mediaPanelText(p,QRectF(textLeft,top+84.,textWidth,46.),elided(shell_.mediaTrackTitle(),36,textWidth),36,Qt::white,
        Qt::AlignLeft|Qt::AlignVCenter);                                          // text_contents_title_name
    if(!shell_.mediaArtist().isEmpty())
        mediaPanelText(p,QRectF(textLeft,top+134.,textWidth,36.),elided(shell_.mediaArtist(),27,textWidth),27,
            QColor(240,240,240),Qt::AlignLeft|Qt::AlignVCenter);                  // text_artist_name
    if(!shell_.mediaAlbum().isEmpty())
        mediaPanelText(p,QRectF(textLeft,top+174.,textWidth,32.),elided(shell_.mediaAlbum(),24,textWidth),24,
            QColor(220,220,220),Qt::AlignLeft|Qt::AlignVCenter);
    // plane_codec: the container's codec labels, chosen by file suffix (wav/aiff are linear PCM here).
    const QString codec=shell_.mediaCodec();
    const char* codecTexture=codec==QLatin1String("mp3")?"tex_codec_mp3":
        (codec==QLatin1String("wav")||codec==QLatin1String("aif")||codec==QLatin1String("aiff"))?"tex_pcm":
        (codec==QLatin1String("aac")||codec==QLatin1String("m4a"))?"tex_aac":
        codec==QLatin1String("wma")?"tex_wma":nullptr;
    if(codecTexture) {
        const QImage& art=assets_.image(plugin,QString::fromLatin1(codecTexture));
        if(!art.isNull())p.drawImage(QRectF(textLeft,top+frameSize-art.height(),art.width(),art.height()),art);
    }
    p.restore();
    // The bottom chrome and the status planes belong to the page's own control state: they fade with the idle
    // style and with the XMB panel (B2) that takes the band over, which the caller's opacity already carries.
    const double chromeOpacity=chrome*(1.-std::clamp(musicPanelEase_.value,0.,1.));
    if(chromeOpacity<=.002)return;
    p.save();p.setOpacity(chromeOpacity);
    // play_status_group: the pause plane while paused; the play plane for 1.5 s after a start or track change,
    // then it fades over 400 ms (INFERRED timing: the container carries no animations). Repeat / shuffle show
    // while their mode is on.
    const bool paused=shell_.mediaPaused()||shell_.busy();
    const QString key=QString::number(shell_.mediaTrackNumber())+(paused?QLatin1Char('p'):QLatin1Char('r'));
    if(key!=musicStatusKey_){musicStatusKey_=key;musicStatusSince_=now;}
    const double playFade=paused?1.:std::clamp(1.-double(now-musicStatusSince_-1500)/400.,0.,1.);
    qreal cursor=1810.;
    const auto statusPlane=[&](const char* name,double amount) {
        if(amount<=.002)return;
        const QImage& art=assets_.image(plugin,QString::fromLatin1(name));
        if(art.isNull())return;
        cursor-=art.width();
        p.save();p.setOpacity(chromeOpacity*amount);
        p.drawImage(QRectF(cursor,top+(74.-art.height())/2.,art.width(),art.height()),art);
        p.restore();
        cursor-=10.;
    };
    statusPlane(paused?"tex_pause_status":"tex_play_status",playFade);
    if(shell_.mediaShuffle())statusPlane("tex_shuffle_status",1.);
    if(shell_.mediaRepeatMode()==1)statusPlane("tex_1repeat_status",1.);
    else if(shell_.mediaRepeatMode()==2)statusPlane("tex_repeat_status",1.);
    // play_progress_group: text_track_number ("Track %1", msg_track_number_value) and the track count, over the bar.
    const auto tr=[&](const char* name,const char* fallback){return assets_.text(plugin,name,fallback);};
    if(number>0) {
        mediaPanelText(p,QRectF(110,895,700,36),tr("msg_track_number_value","Track %1").arg(number),21,
            QColor(230,230,230),Qt::AlignLeft|Qt::AlignVCenter);
        const int count=shell_.mediaTrackCount();
        mediaPanelText(p,QRectF(1110,895,700,36),
            (count==1?tr("msg_track_count_value","%1 Track"):tr("msg_tracks_count_value","%1 Tracks")).arg(count),21,
            QColor(230,230,230),Qt::AlignRight|Qt::AlignVCenter);
    }
    if(shell_.mediaDuration()>0)drawVideoTimeBar(p,shell_.mediaPosition(),shell_.mediaDuration(),chromeOpacity);
    // msg_help_control_panel_home_menu: two lines, each led by a button gaiji (Triangle: Control Panel,
    // PS: Home Menu), drawn from the image font like the XMB's own Options hint.
    const QStringList lines=tr("msg_help_control_panel_home_menu",": Control Panel\n: Home Menu").split(QLatin1Char('\n'));
    qreal x=110.;
    QFont font(shell_.fontFamily());font.setPixelSize(21);
    for(QString line:lines) {
        const ushort code=line.isEmpty()?0:line.front().unicode();
        const bool gaiji=code>=0xE000 && code<=0xF8FF;
        if(gaiji) {
            line=line.mid(1);
            const QImage glyph=imageFontGlyph(assets_.root(),code);
            if(!glyph.isNull()) {
                const qreal gh=21.,gw=glyph.width()*gh/glyph.height();
                p.drawImage(QRectF(x,1041.-gh/2.,gw,gh),glyph);   // the painter's opacity is chromeOpacity
                x+=gw+4.;
            }
        }
        const qreal width=QFontMetricsF(font).horizontalAdvance(line)+8.;
        mediaPanelText(p,QRectF(x,1026,width,30),line,21,QColor(238,238,238),Qt::AlignLeft|Qt::AlignVCenter);
        x+=width+56.;
    }
    p.restore();
}
// A local audio item is what the XMB panel belongs to: no video track, and not the photo viewer.
bool View::mediaAudioOnly() const {
    return shell_.mediaOpen() && !shell_.videoOpen() && !shell_.photoOpen();
}
void View::toggleMusicPanel() {
    if(!mediaAudioOnly())return;
    musicPanelOpen_=!musicPanelOpen_;
    if(musicPanelOpen_) {
        musicPanelFocus_=0;
        // The firmware plays its module-action cue on panel open (explore_plugin_all.c:38936); the native
        // cue map calls that sound Cue::Accept, not Cue::Confirm (audit MEDIA-PANELS-FIRMWARE 7.2/11.7).
        shell_.playCue(InteractionAudio::Cue::Accept);
        // A fresh open starts the fade from nothing: the ease is reset so the 150 ms runs from 0.
        musicPanelEase_=PafEase();
        musicPanelEase_.retarget(0.);
        qInfo().noquote()<<"media: music control panel opened (explore_plugin_full control_panel_page;"
            " fade 150 ms, delay 0; scale 1.3)";
    } else qInfo().noquote()<<"media: music control panel closed";
    update();
}
void View::closeMusicPanel() {
    if(!musicPanelOpen_)return;
    musicPanelOpen_=false;
    qInfo().noquote()<<"media: music control panel closed";
    update();
}
void View::musicPanelMove(int delta) {
    if(!musicPanelOpen_ || delta==0)return;
    const int next=(musicPanelFocus_+delta%musicPanelButtons+musicPanelButtons)%musicPanelButtons;
    if(next==musicPanelFocus_)return;
    musicPanelFocus_=next;
    update();
}
void View::musicPanelActivate() {
    if(!musicPanelOpen_)return;
    // Chrome only: each button runs the binding the player already had (audit 7.2 keeps the direct keys).
    switch(musicPanelFocus_) {
    case 0:shell_.skipMedia(-1);break;                    // msg_prev_with_icon_button
    case 1:shell_.skipMedia(1);break;                     // msg_next_with_icon_button
    case 2:shell_.togglePlayback();break;                 // msg_pause_button
    case 3:shell_.back();break;                           // msg_stop_button: ends the session like Stop
    case 4:shell_.cycleMediaRepeat();break;               // msg_repeat_button
    case 5:shell_.toggleMediaShuffle();break;             // msg_shuffle_button
    case 7:                                               // msg_visualizer_with_icon_button: OnVisualizerButtonPush
        musicVisualizerOn_=!musicVisualizerOn_;
        if(musicVisualizerOn_ && shell_.musicTap())shell_.musicTap()->reset();   // no stale windows from an earlier on-period
        qInfo().noquote()<<"media: Visual Player"<<(musicVisualizerOn_?"on":"off");
        break;
    case 6:                                               // msg_volume_control_button: raise the meter
        if(volumeLevel_>=0)volumeApply(volumeLevel_,QStringLiteral("MusicPanel"));
        else qWarning().noquote()<<"media: the volume button has no endpoint to show";
        break;
    default:break;
    }
    update();
}
void View::zoomPhoto(int direction) {
    if(!shell_.photoOpen() || !photoViewport_.valid())return;
    if(shell_.slideshowActive())shell_.togglePlayback();
    if(photoViewport_.zoom(direction)) {
        if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"photo viewport: zoom"<<photoViewport_.zoomPercent()<<photoViewport_.imageRect();
        update();
    }
}
void View::resetPhotoView() {
    if(!shell_.photoOpen())return;
    if(shell_.slideshowActive())shell_.togglePlayback();
    photoViewport_.resetView();
    if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"photo viewport: reset"<<photoViewport_.zoomPercent()<<photoViewport_.imageRect();
    update();
}
void View::panPhoto(int dx,int dy) {
    if(!shell_.photoOpen())return;
    if(shell_.slideshowActive())shell_.togglePlayback();
    if(photoViewport_.pan(dx,dy)) {
        if(qEnvironmentVariableIsSet("XMB_MEDIA_TRACE"))qInfo()<<"photo viewport: pan"<<photoViewport_.imageRect();
        update();
    }
}
namespace {
// D-pad bit (XInput layout) for an arrow key, else 0.
quint16 arrowBit(int key) {
    switch(key){case Qt::Key_Up:return 0x0001;case Qt::Key_Down:return 0x0002;case Qt::Key_Left:return 0x0004;case Qt::Key_Right:return 0x0008;default:return 0;}
}
}
void View::replayKeyTap(int key) {
    QScopedValueRollback<bool> replay(replayingInput_,true);
    // A scripted arrow tap is pressed and released inside one frame: it reaches the particles' d-pad wind as one tick.
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier);keyPressEvent(&press);
    QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier);keyReleaseEvent(&release);
}
// Scripted PS tokens feed the global watcher exactly like a HID press: the hold keeps the button down
// long enough for the watcher's 1 s rule to fire on its own. A tap cannot go through the watcher in a
// scripted run: the pad is gated, so no HID sample ever reaches poll(), and poll() deliberately holds
// the last state across a missing sample (a controller dropout is not a release). The injected release
// is therefore never observed and the token became a 1001 ms hold that opened the system menu instead
// of flashing the indicator (probe 2026-10-28, out/psfix/tap-probe-ps.log). The token drives the tap
// handler the watcher would call, at the press instant, so the indicator's 155 ms delay is measured
// from the press exactly as the console burst measures it. The watcher's own tap path stays exercised
// by a real pad and by the pshold token.
void View::replayPsTap() {
    psLog("script: token ps (tap)");
    psPressActionable_=!QApplication::activeModalWidget() && (xmbOwnsForeground()||titleRunning());
    psLog(QString("script: tap driven directly xmbActive=%1 xmbForeground=%2 titleRunning=%3 modal=%4 foreground=%5")
        .arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(titleRunning()?1:0)
        .arg(QApplication::activeModalWidget()?1:0).arg(foregroundDescription()));
    psTappedEvent(120);   // the script stands in for the console's 60-120 ms press
}
void View::setSuspended(bool suspended) {
    timer_.setInterval(suspended?250:16);
    if(!suspended){buttons_=keyHeld_=keyPressed_=0;triggerHeld_=false;update();}
}
void View::replayPsHold() {
    psLog("script: token pshold (hold)");
    psWatcher_.replayPress();
    QTimer::singleShot(1200,this,[this]{psWatcher_.replayRelease();});
}
bool View::titleRunning() const {
    return shell_.titleRunning();
}
bool View::xmbOwnsForeground() const {
    // Qt's activeWindow follows the Win32 foreground window with an event hop; after a programmatic
    // raise the two can disagree for a frame. The window manager is the authority here.
#ifdef Q_OS_WIN
    QWidget* top=window();
    if(top && GetForegroundWindow()==reinterpret_cast<HWND>(top->winId()))return true;
#endif
    return isActiveWindow();
}
void View::psPressed() {
    const bool modal=QApplication::activeModalWidget()!=nullptr;
    psPressActionable_=!modal && (xmbOwnsForeground() || titleRunning());
    // The hold menu's envelope is measured from this instant (PARITY-SCORECARD 16.3: black 1052 ms after the
    // press). The watcher's own 1000 ms rule is unchanged; the envelope is what the console spends after it.
    if(psPressActionable_)psPressAt_=clock_.elapsed();
    psLog(QString("event: press xmbActive=%1 xmbForeground=%2 titleRunning=%3 menuOpen=%4 modal=%5 foreground=%6")
        .arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(titleRunning()?1:0)
        .arg(shell_.psMenuOpen()?1:0).arg(modal?1:0).arg(foregroundDescription()));
    if(!psPressActionable_)psLog(modal?"event: press not actionable (a modal dialog owns input)"
        :"event: press not actionable (XMB not in front and no tracked title running)");
}
void View::psHeldEvent() {
    const bool running=titleRunning();
    psLog(QString("event: hold xmbActive=%1 xmbForeground=%2 titleRunning=%3 foreground=%4")
        .arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(running?1:0).arg(foregroundDescription()));
    if(QApplication::activeModalWidget()) { psLog("event: hold ignored (a modal dialog owns input)"); return; }
    if(!psPressActionable_) { psLog("event: hold ignored (not actionable)"); return; }
    // With no tracked title the old rule still holds at the moment of the hold: the XMB must still be in
    // front. A press that began in the XMB and lost the foreground before 1 s must not open a menu behind
    // another application.
    if(!running && !xmbOwnsForeground()) { psLog("event: hold ignored (XMB lost the foreground before the hold completed)"); return; }
    // The console only opens its menu over the game; the XMB is brought forward first so the user can
    // see and drive the menu. Quitting remains a separate, explicit selection.
    if(running && !shell_.inGame())enterInGame("PS hold while a tracked title is running");
    else if(running)bringXmbForward("PS hold while a tracked title is running");
    if(shell_.psMenuOpen()){psLog("event: system menu already open");return;}
    shell_.psHeld();
    psLog(QString("event: system menu opened rows=%1 quitGameOffered=%2 foreground=%3")
        .arg(shell_.psMenuItems().size()).arg(running?1:0).arg(foregroundDescription()));
    update();
}
void View::psTappedEvent(qint64 durationMs) {
    const bool running=titleRunning();
    psLog(QString("event: tap %1 ms xmbActive=%2 xmbForeground=%3 titleRunning=%4 foreground=%5 at=%6 ms")
        .arg(durationMs).arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(running?1:0)
        .arg(foregroundDescription()).arg(clock_.elapsed()));
    if(!psPressActionable_) { psLog("event: tap ignored (not actionable)"); return; }
    if(QApplication::activeModalWidget()) { psLog("event: tap ignored (a modal dialog owns input)"); return; }
    if(!running && !xmbOwnsForeground()) { psLog("event: tap ignored (XMB lost the foreground before the tap completed)"); return; }
    // Step 8 (INGAME-XMB-DESIGN): when the owner leaves the PS tap to the launcher (input/psArbitration=launcher)
    // for an RPCS3 or Steam title, the XMB does not react to a tap so the launcher's own Home Menu / Guide gets it;
    // a hold still opens the XMB's system menu. The return-to-game tap above (already in-game) is unaffected.
    if(running && !shell_.inGame() && shell_.launcherOwnsPsTap() && (shell_.runningTitleIsSteam() || shell_.runningTitleIsRpcs3())) {
        psLog("event: tap left to the launcher (input/psArbitration=launcher)");
        return;
    }
    // A second tap while the XMB is already over the game goes back to the game (INGAME-XMB-DESIGN step 6).
    if(running && shell_.inGame() && xmbOwnsForeground() && !shell_.psMenuOpen()) {
        const bool surface=shell_.depth()>0 || !shell_.message().isEmpty() || shell_.optionsOpen() || shell_.mediaOpen() || osk_.open || userCreate_.open || plEdit_.open;
        if(!surface){returnToGame("PS tap while in-game");return;}
    }
    if(running && !shell_.inGame())enterInGame("PS tap while a tracked title is running");
    else if(running)bringXmbForward("PS tap while a tracked title is running");
    psHudAt_=clock_.elapsed();
    psHudUntil_=psHudAt_+2288;   // the console's own window (PS-BUTTON.md, MANIFEST 4)
    shell_.psTapped();   // closes the menu if open, otherwise clears status and flashes the indicator
    psLog(QString("event: tap handled; no title was quit (titleRunning=%1) foreground=%2")
        .arg(titleRunning()?1:0).arg(foregroundDescription()));
    update();
}
void View::psReleasedEvent(qint64 durationMs) {
    psLog(QString("event: release after %1 ms (hold, no tap)").arg(durationMs));
}
void View::bringXmbForward(const QString& reason) {
    QWidget* top=window();
    if(!top)return;
    psLog(QString("focus: bring-to-front begin reason=\"%1\" xmbActive=%2 xmbForeground=%3 minimized=%4 foreground=%5")
        .arg(reason).arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(top->isMinimized()?1:0).arg(foregroundDescription()));
    if(top->isMinimized()) {
        if(shell_.fullscreen())top->showFullScreen();
        else top->showNormal();
    }
    if(shell_.fullscreen() && !top->isFullScreen())top->showFullScreen();
    top->raise();
    top->activateWindow();
    if(auto* handle=top->windowHandle())handle->requestActivate();
    setFocus(Qt::OtherFocusReason);
#ifdef Q_OS_WIN
    const HWND window=reinterpret_cast<HWND>(top->winId());
    if(window) {
        const HWND foreground=GetForegroundWindow();
        if(foreground!=window) {
            // A process that is not in front cannot normally call SetForegroundWindow; attaching to the
            // current foreground thread's input queue for the call is the documented, non-invasive way
            // through the foreground lock. Nothing is injected into the game process.
            const DWORD foregroundThread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
            const DWORD thisThread=GetCurrentThreadId();
            const bool attached=foregroundThread && foregroundThread!=thisThread && AttachThreadInput(thisThread,foregroundThread,TRUE);
            SetForegroundWindow(window);
            BringWindowToTop(window);
            SetActiveWindow(window);
            if(attached)AttachThreadInput(thisThread,foregroundThread,FALSE);
        }
    }
#endif
    psLog(QString("focus: bring-to-front end xmbActive=%1 xmbForeground=%2 foreground=%3")
        .arg(isActiveWindow()?1:0).arg(xmbOwnsForeground()?1:0).arg(foregroundDescription()));
}
// ---- In-game XMB (plan F5, audit/INGAME-XMB-DESIGN.md steps 4-6) ----------------------------------------------
namespace {
#ifdef Q_OS_WIN
// The documented way through the foreground lock (the same as View::bringXmbForward): attach to the current
// foreground thread's input queue for the call. Nothing is injected into any process.
void takeForeground(HWND window) {
    if(!window)return;
    const HWND foreground=GetForegroundWindow();
    if(foreground==window)return;
    const DWORD foregroundThread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
    const DWORD thisThread=GetCurrentThreadId();
    const bool attached=foregroundThread && foregroundThread!=thisThread && AttachThreadInput(thisThread,foregroundThread,TRUE);
    SetForegroundWindow(window);BringWindowToTop(window);SetActiveWindow(window);
    if(attached)AttachThreadInput(thisThread,foregroundThread,FALSE);
}
// The Xbox Game Bar is a topmost window: DWM composites it over an FSO (borderless-emulated) fullscreen game, so it
// stays above the game even when the game is itself HWND_TOPMOST. The in-game XMB does the same while it is over a
// title, and drops back to a normal window on return. SWP_NOACTIVATE so the z-order changes without stealing focus.
void setTopmost(HWND window, bool on) {
    if(!window)return;
    SetWindowPos(window, on?HWND_TOPMOST:HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
#endif
// Sony's firmware-2.40 filmed-TV demo retains approximately 18-20 % of game-region luminance after
// exposure normalization (audit/INGAME-FIDELITY-SWEEP.md). This is an estimate, not a recovered firmware
// blend constant; XMB_INGAME_DIM=<0..1> overrides it for direct-capture comparisons.
double inGameDim() {
    bool ok=false;const double v=qEnvironmentVariable("XMB_INGAME_DIM").toDouble(&ok);
    return ok?std::clamp(v,0.,1.):.8;
}
}
void View::enterInGame(const QString& reason) {
    if(!titleRunning()){
        psLog(QStringLiteral("ingame: entry ignored (no running title)"));
        return;
    }
    quintptr hwnd=0;
    GameWindowInfo info;
    GameCaptureResult capture;
#ifdef Q_OS_WIN
    const quint32 root=shell_.runningTitlePid();
    QWidget* top=window();
    const HWND self=top?reinterpret_cast<HWND>(top->winId()):nullptr;
    // 1. The foreground window, when it is the title's: a window of the tracked process tree, or - for a Steam title,
    //    which is not our process - a monitor-covering window (the only kind a game is; a window the user alt-tabbed
    //    to is never captured). 2. Otherwise the tracked tree's largest visible window.
    const HWND foreground=GetForegroundWindow();
    if(foreground && foreground!=self) {
        DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);
        if(root && pidInTree(pid,root))hwnd=quintptr(foreground);
        else if(shell_.runningTitleIsSteam()) {
            const auto candidate=classifyGameWindow(quintptr(foreground));
            if(candidate.mode==GameWindowMode::Borderless || candidate.mode==GameWindowMode::Exclusive)hwnd=quintptr(foreground);
        }
    }
    if(!hwnd && root)hwnd=findMainWindow(root);
    if(hwnd) {
        info=classifyGameWindow(hwnd);
        // TRACED limits (design 2.1): an exclusive-fullscreen frame bypasses DWM, a minimised window has no content.
        // Capture is still tried for Exclusive - it reports black or fails, never throws - but not for Minimized.
        if(info.mode!=GameWindowMode::Minimized && info.mode!=GameWindowMode::Invalid)capture=captureGameWindow(hwnd,1500);
        else capture.detail=QStringLiteral("not captured (%1)").arg(info.reason);
        psLog(gameCaptureLogLine(info,capture));
    } else psLog(QStringLiteral("ingame: no title window found (root pid %1, foreground %2)").arg(root).arg(foregroundDescription()));
#endif
    inGameBackdrop_=capture.status==GameCaptureStatus::Ok?capture.image:QImage();
    inGameHwnd_=hwnd;inGamePid_=info.pid;inGameMode_=info.mode;
    // A fullscreen game that was minimised by the focus change: the recorded window may need SW_RESTORE on return.
    shell_.setInGame(true);
    if(!shell_.inGame())return;   // the title may have ended while its frame was being captured
    inGameWindowActive_=true;
    psLog(QStringLiteral("ingame: enter reason=\"%1\" mode=%2 backdrop=%3").arg(reason,QLatin1String(gameWindowModeName(info.mode)))
        .arg(inGameBackdrop_.isNull()?QStringLiteral("plain dark"):QStringLiteral("captured %1x%2").arg(inGameBackdrop_.width()).arg(inGameBackdrop_.height())));
    bringXmbForward(reason);
#ifdef Q_OS_WIN
    // Game Bar style: stay above the game while the in-game XMB is up (INGAME-XMB-DESIGN, "always there").
    setTopmost(reinterpret_cast<HWND>(window()->winId()), true);
    if(qEnvironmentVariable("XMB_INGAME_LIVE")==QStringLiteral("1")) {
        if(inGameMode_==GameWindowMode::Windowed || inGameMode_==GameWindowMode::Borderless) {
            auto overlay=std::make_unique<LiveOverlay>();QString error;
            if(overlay->create(quintptr(window()->winId()),info.monitorRect,&error)) {
                liveOverlayOwnerOpacity_=window()->windowOpacity();
                liveOverlay_=std::move(overlay);
                window()->setWindowOpacity(0.);
                psLog(QStringLiteral("ingame: DirectComposition live overlay started (live=1, Qt owner retains focus)"));
            } else psLog(QStringLiteral("ingame: live overlay creation failed; captured-frame fallback: %1").arg(error));
        } else psLog(QStringLiteral("ingame: live overlay declined mode=%1; captured-frame fallback").arg(QLatin1String(gameWindowModeName(inGameMode_))));
    }
#endif
    update();
}
bool View::inGameBack() {
    if(!shell_.inGame())return false;
    // Only the column root: inside a page, Circle is the ordinary Back (design step 5/6).
    if(shell_.depth()>0 || !shell_.message().isEmpty() || shell_.optionsOpen() || shell_.mediaOpen() || shell_.psMenuOpen() ||
       shell_.busy() || osk_.open || userCreate_.open || plEdit_.open)return false;
    returnToGame("Circle at the column root");
    return true;
}
void View::returnToGame(const QString& reason) {
    const quintptr recorded=inGameHwnd_;
    stopLiveOverlay();
    shell_.setInGame(false);
    inGameBackdrop_=QImage();
#ifdef Q_OS_WIN
    // Drop the Game Bar-style topmost band so the XMB behaves like a normal window again once the game is in front.
    setTopmost(reinterpret_cast<HWND>(window()->winId()), false);
    HWND target=reinterpret_cast<HWND>(recorded);
    if(!target || !IsWindow(target)) {
        // The recorded window went away (a title that recreates its window on mode switch): take the tree's largest.
        const quint32 root=shell_.runningTitlePid();
        target=root?reinterpret_cast<HWND>(findMainWindow(root)):nullptr;
    }
    if(!target) {
        psLog(QStringLiteral("ingame: return reason=\"%1\" found no title window; leaving the XMB in front").arg(reason));
        update();return;
    }
    const bool iconic=IsIconic(target)!=0;
    if(iconic)ShowWindow(target,SW_RESTORE);
    takeForeground(target);
    psLog(QStringLiteral("ingame: return reason=\"%1\" hwnd=0x%2 restored=%3 foreground=%4")
        .arg(reason).arg(quintptr(target),0,16).arg(iconic?1:0).arg(foregroundDescription()));
    // The foreground lock can still refuse once; one retry after 400 ms (design step 6), then Alt-Tab is the fallback.
    QTimer::singleShot(400,this,[this,target,reason]{
        if(shell_.inGame() || !IsWindow(target))return;
        if(GetForegroundWindow()==target){psLog(QStringLiteral("ingame: return confirmed, the title holds the foreground"));return;}
        takeForeground(target);
        psLog(QStringLiteral("ingame: return retry after 400 ms foreground=%1").arg(foregroundDescription()));
    });
#endif
    update();
}
void View::syncInGameWindow() {
    if(shell_.inGame() || !inGameWindowActive_)return;
    // A title can close itself while its overlay is open, without going through returnToGame().
    // Do not leave a topmost, non-interactive XMB covering whichever app receives the foreground next.
    stopLiveOverlay();
    inGameBackdrop_=QImage();inGameHwnd_=0;inGamePid_=0;inGameWindowActive_=false;
#ifdef Q_OS_WIN
    setTopmost(reinterpret_cast<HWND>(window()->winId()),false);
#endif
    psLog(QStringLiteral("ingame: cleared window state and topmost band"));
}
void View::stopLiveOverlay() {
    if(!liveOverlay_)return;
    liveOverlay_.reset();liveOverlayPixels_=QImage();
    window()->setWindowOpacity(liveOverlayOwnerOpacity_);
    psLog(QStringLiteral("ingame: DirectComposition live overlay stopped"));
}
void View::drawInGameBackdrop(int width,int height) {
    glBindFramebuffer(GL_FRAMEBUFFER,defaultFramebufferObject());glViewport(0,0,width,height);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
    if(liveOverlay_) {
        // Premultiplied black dim: DWM supplies the live game, not a captured texture.
        glClearColor(0.f,0.f,0.f,float(inGameDim()));glClear(GL_COLOR_BUFFER_BIT);return;
    }
    glActiveTexture(GL_TEXTURE0);
    // No painter, image conversion, or texture upload on unchanged frames. Capture callbacks only replace the
    // QImage; GL allocation and deletion stay here, where the widget's context is current.
    if(inGameTextureKey_!=inGameBackdrop_.cacheKey()) {
        inGameTexture_.reset();inGameTextureKey_=inGameBackdrop_.cacheKey();
        if(!inGameBackdrop_.isNull()) {
            inGameTexture_=std::make_unique<QOpenGLTexture>(inGameBackdrop_.flipped(Qt::Vertical),QOpenGLTexture::DontGenerateMipMaps);
            inGameTexture_->setMinificationFilter(QOpenGLTexture::Linear);
            inGameTexture_->setMagnificationFilter(QOpenGLTexture::Linear);
            inGameTexture_->setWrapMode(QOpenGLTexture::ClampToEdge);
            psLog(QStringLiteral("ingame: backdrop texture uploaded %1x%2 key=%3")
                .arg(inGameBackdrop_.width()).arg(inGameBackdrop_.height()).arg(inGameTextureKey_));
        }
    }
    if(inGameTexture_ && inGameProgram_.isLinked()) {
        inGameProgram_.bind();inGameProgram_.setUniformValue("frame",0);
        inGameProgram_.setUniformValue("brightness",1.f-float(int(255.*inGameDim()))/255.f);
        const float aspect=float(width)*inGameBackdrop_.height()/(float(height)*inGameBackdrop_.width());
        inGameProgram_.setUniformValue("uvScale",QVector2D(std::min(1.f,aspect),std::min(1.f,1.f/aspect)));
        inGameTexture_->bind();glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);
        glBindVertexArray(0);inGameTexture_->release();inGameProgram_.release();
    } else {
        // Capture failure/black/minimised is plain dark, never the home wave or a stale game texture.
        glClearColor(8.f/255.f,8.f/255.f,12.f/255.f,1.f);glClear(GL_COLOR_BUFFER_BIT);
    }
}
void View::reassertForeground(const QString& reason) {
    if(xmbOwnsForeground())psLog(QString("focus: XMB already holds the foreground after %1 (%2)").arg(reason,foregroundDescription()));
    else bringXmbForward(reason);
    // The killed title's window is destroyed asynchronously; check once more after Windows has finished
    // handing the foreground to its successor.
    QTimer::singleShot(400,this,[this,reason]{
        if(!xmbOwnsForeground())bringXmbForward(reason+" (retry after the title's window was destroyed)");
        else psLog(QString("focus: XMB holds the foreground after %1 (%2)").arg(reason,foregroundDescription()));
    });
}
void View::activatePsMenuSelection() {
    const bool quitting=titleRunning() && !shell_.psMenuConfirmingOff() && shell_.psMenuSelection()==0;
    psLog(QString("event: PS menu activate selection=%1 quitting=%2").arg(shell_.psMenuSelection()).arg(quitting?1:0));
    shell_.psMenuActivate();
    if(quitting)psLog(QString("event: Quit Game selected; confirmation shown=%1 (the title is closed only on Yes)").arg(shell_.message().isEmpty()?0:1));
    update();
}
bool View::eventFilter(QObject* watched,QEvent* event) {
    // Focus transitions of the XMB's own top-level window, for XMB_PS_LOG runs: together with the
    // foreground window named on every PS event this shows whether a press arrived while the game owned
    // the foreground.
    if(event->type()==QEvent::WindowActivate || event->type()==QEvent::WindowDeactivate) {
        QWidget* top=window();
        if(top && (watched==top || (top->windowHandle() && watched==top->windowHandle())))
            psLog(QString("focus: XMB %1 xmbActive=%2 foreground=%3")
                .arg(event->type()==QEvent::WindowActivate?"activated":"deactivated")
                .arg(isActiveWindow()?1:0).arg(foregroundDescription()));
    }
    const auto* widget=qobject_cast<QWidget*>(watched);
    if(widget && (widget==this || widget->window()==window() || isAncestorOf(widget))) {
        if(event->type()==QEvent::KeyPress || event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove || event->type()==QEvent::Wheel) {shell_.noteActivity();mediaControls_.activity(clock_.elapsed());}
    }
    return QOpenGLWidget::eventFilter(watched,event);
}
void View::keyPressEvent(QKeyEvent* e) {
    shell_.noteActivity();mediaControls_.activity(clock_.elapsed());
    if(introRunning()){coldboot_.skip();e->accept();return;}
    // Remote, media and Media Center keys (media_keys.h). On Windows the native filter normally takes the
    // WM_APPCOMMAND/VK form first; this is the path for any that arrive as Qt key events (and for scripts).
    if(const RemoteKey remote=remoteKeyForQtKey(e->key(),e->modifiers());remote!=RemoteKey::None) {
        if(remote==remoteDeclined_ && clock_.elapsed()-remoteDeclinedAt_<200){e->ignore();return;}
        if(e->isAutoRepeat() || remoteKeyAction(remote,QStringLiteral("qt key 0x%1").arg(e->key(),0,16))){e->accept();return;}
        e->ignore();return;
    }
    // The PS system menu owns the keyboard while it is open: arrows move, Return confirms, Escape backs out.
    if(shell_.psMenuOpen()) {
        if(e->key()==Qt::Key_Up)shell_.psMenuMove(-1);
        else if(e->key()==Qt::Key_Down)shell_.psMenuMove(1);
        else if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter)activatePsMenuSelection();
        else if(e->key()==Qt::Key_Escape)shell_.psMenuBack();
        e->accept();return;
    }
    // The item options panel owns the keyboard while open: Up/Down move inside it, Cross (Return) runs the
    // option, Circle (Escape/Backspace) steps back or closes, Triangle (T) hides the whole menu. The XMB
    // behind the panel receives none of these keys.
    if(shell_.optionsOpen()) {
        if(e->key()==Qt::Key_Up)shell_.optionsMove(-1);
        else if(e->key()==Qt::Key_Down)shell_.optionsMove(1);
        else if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter)shell_.optionsActivate();
        else if(e->key()==Qt::Key_Escape || e->key()==Qt::Key_Backspace)shell_.optionsClose();
        else if(e->key()==Qt::Key_T)shell_.toggleOptions();
        e->accept();return;
    }
    // Keyboard substitute for the PS button (controllers send 0x400 over HID): F10 opens the system menu.
    if(e->key()==Qt::Key_F10){shell_.psHeld();e->accept();return;}
    // The Photo Gallery owns the keys while its area is up (gallery_chrome.cpp). A real keyboard's arrows go through
    // pollController's repeat like the XMB's; scripted taps and non-Windows arrows act at once.
    if(galleryOwnsInput()) {
        const bool arrow=e->key()==Qt::Key_Left || e->key()==Qt::Key_Right || e->key()==Qt::Key_Up || e->key()==Qt::Key_Down;
        const bool mapped=arrow || e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter || e->key()==Qt::Key_Escape || e->key()==Qt::Key_Backspace ||
            e->key()==Qt::Key_T || e->key()==Qt::Key_S || e->key()==Qt::Key_Space || e->key()==Qt::Key_Plus || e->key()==Qt::Key_Equal ||
            e->key()==Qt::Key_Minus || e->key()==Qt::Key_PageUp || e->key()==Qt::Key_PageDown;
#ifdef Q_OS_WIN
        const bool viaPoll=arrow && !replayingInput_;
#else
        const bool viaPoll=false;
#endif
        if(mapped && !viaPoll){if(!e->isAutoRepeat() || arrow)galleryPress(e->key());e->accept();return;}
        if(mapped && viaPoll) {
            if(const auto bit=arrowBit(e->key());bit && !e->isAutoRepeat()){keyHeld_|=bit;keyPressed_|=bit;}
            e->accept();return;
        }
    }
    // The Windows media volume keys. The native filter takes the WM_APPCOMMAND/VK_VOLUME form before Qt or
    // DefWindowProc sees it; this is the path for a Key_Volume* that does arrive as a key event, and it is the
    // path the VOLUP/VOLDOWN/MUTE script tokens drive. The XMB binds no volume key, so handling it here takes
    // nothing away from navigation.
    if(e->key()==Qt::Key_VolumeUp || e->key()==Qt::Key_VolumeDown || e->key()==Qt::Key_VolumeMute) {
        volumeKeyAction(e->key()==Qt::Key_VolumeMute?3:(e->key()==Qt::Key_VolumeUp?1:2),"keys");
        e->accept();return;
    }
#ifdef Q_OS_WIN
    // Arrows go through pollController's repeat (the firmware's, not the OS's), so auto-repeats are dropped here.
    if(const auto bit=arrowBit(e->key());bit && !replayingInput_) {
        if(!e->isAutoRepeat()){keyHeld_|=bit;keyPressed_|=bit;}
        e->accept();return;
    }
#endif
    if(osk_.open) { // the keyboard owns input: arrows move, Return presses the key (Ctrl+Return accepts), Esc cancels
        switch(e->key()) {
        case Qt::Key_Left:oskMove(-1,0);break;case Qt::Key_Right:oskMove(1,0);break;
        case Qt::Key_Up:oskMove(0,-1);break;case Qt::Key_Down:oskMove(0,1);break;
        case Qt::Key_Return:case Qt::Key_Enter:if(e->modifiers()&Qt::ControlModifier)oskClose(true);else oskPress();break;
        // Back is one layer at a time: the palette first, then the keyboard (audit OSK-FIRMWARE 4.3).
        case Qt::Key_Escape:
            if(osk_.paletteOpen){osk_.paletteOpen=false;update();}
            else oskClose(false);
            break;
        case Qt::Key_Backspace:oskBackspace();break;
        default: {
            const auto scalars=e->text().toUcs4();
            if(!scalars.isEmpty() && QChar::isPrint(scalars.first()))oskInsert(e->text());
            break;
        }
        }
        return;
    }
    if(plEdit_.open) { // the playlist editor owns input until it closes (arrows reach here only from a script)
        switch(e->key()) {
        case Qt::Key_Up:playlistEditorInput(0);break;case Qt::Key_Down:playlistEditorInput(1);break;
        case Qt::Key_Left:playlistEditorInput(2);break;case Qt::Key_Right:playlistEditorInput(3);break;
        case Qt::Key_Return:case Qt::Key_Enter:playlistEditorInput(4);break;
        case Qt::Key_Escape:case Qt::Key_Backspace:playlistEditorInput(5);break;
        case Qt::Key_T:playlistEditorInput(6);break;
        case Qt::Key_Delete:playlistEditorInput(7);break;   // Square
        default:break;
        }
        e->accept();return;
    }
    if(userCreate_.open) { // the name page owns input until it closes
        switch(e->key()) {
        case Qt::Key_Up:userCreateInput(0);break;case Qt::Key_Down:userCreateInput(1);break;
        case Qt::Key_Return:case Qt::Key_Enter:userCreateInput(2);break;
        case Qt::Key_Escape:case Qt::Key_Backspace:userCreateInput(3);break;
        default:break;
        }
        e->accept();return;
    }
    // The music control panel owns its keys while it is up: Left/Right walk the seven buttons, Cross runs
    // the focused one, Circle/Triangle close it. Nothing else in the frame reads them in this state.
    if(musicPanelOpen_) {
        switch(e->key()) {
        case Qt::Key_Left:musicPanelMove(-1);e->accept();return;
        case Qt::Key_Right:musicPanelMove(1);e->accept();return;
        case Qt::Key_Return:case Qt::Key_Enter:musicPanelActivate();e->accept();return;
        case Qt::Key_Escape:case Qt::Key_Backspace:closeMusicPanel();e->accept();return;
        case Qt::Key_T:toggleMusicPanel();e->accept();return;
        default:break;
        }
    }
    if(videoPanelOpen_ && shell_.videoOpen() && shell_.message().isEmpty()) {
        switch(e->key()) {
        case Qt::Key_Left:videoPanelMove(-1,0);e->accept();return;
        case Qt::Key_Right:videoPanelMove(1,0);e->accept();return;
        case Qt::Key_Up:videoPanelMove(0,1);e->accept();return;
        case Qt::Key_Down:videoPanelMove(0,-1);e->accept();return;
        case Qt::Key_Return:case Qt::Key_Enter:videoPanelActivate();e->accept();return;
        case Qt::Key_Escape:case Qt::Key_Backspace:case Qt::Key_T:case Qt::Key_S:toggleVideoPanel();e->accept();return;
        default:break;
        }
    }
    if(shell_.photoOpen() && shell_.message().isEmpty()) {
        switch(e->key()) {
        case Qt::Key_Plus:case Qt::Key_Equal:zoomPhoto(1);e->accept();return;
        case Qt::Key_Minus:zoomPhoto(-1);e->accept();return;
        case Qt::Key_0:resetPhotoView();e->accept();return;
        case Qt::Key_Q:shell_.rotatePhoto(-1);e->accept();return;
        case Qt::Key_E:shell_.rotatePhoto(1);e->accept();return;
        case Qt::Key_Left:if(photoViewport_.canPan()){panPhoto(-1,0);e->accept();return;}break;
        case Qt::Key_Right:if(photoViewport_.canPan()){panPhoto(1,0);e->accept();return;}break;
        case Qt::Key_Up:if(photoViewport_.canPan()){panPhoto(0,-1);e->accept();return;}break;
        case Qt::Key_Down:if(photoViewport_.canPan()){panPhoto(0,1);e->accept();return;}break;
        default:break;
        }
    }
    switch(e->key()) {
    case Qt::Key_Left:shell_.horizontal(-1);break;case Qt::Key_Right:shell_.horizontal(1);break;
    case Qt::Key_Up:shell_.vertical(-1);break;case Qt::Key_Down:shell_.vertical(1);break;
    case Qt::Key_Return:case Qt::Key_Enter:shell_.activate();break;
    case Qt::Key_Escape:case Qt::Key_Backspace:if(!inGameBack())shell_.back();break;
    case Qt::Key_Space:shell_.togglePlayback();break;
    case Qt::Key_BracketLeft:shell_.skipMedia(-1);break;
    case Qt::Key_BracketRight:shell_.skipMedia(1);break;
    case Qt::Key_M:shell_.toggleMediaShuffle();break;
    case Qt::Key_R:shell_.cycleMediaRepeat();break;
    case Qt::Key_PageUp:shell_.vertical(-10);break;case Qt::Key_PageDown:shell_.vertical(10);break;
    case Qt::Key_F5:shell_.refreshDirectory();break;
    // Triangle on the XMB: the selected entry's options (keyboard substitute; controllers send Y). While a
    // local audio item plays it opens the firmware's own music control panel instead (audit 7.2: the console
    // opens control_panel_page there; this build used to cycle Repeat on this key - R still does).
    case Qt::Key_T:if(shell_.videoOpen())toggleVideoPanel();else if(mediaAudioOnly())toggleMusicPanel();else shell_.toggleOptions();break;
    // SELECT/START keyboard substitutes while media is open (the console's own keys have no keyboard
    // equivalent): I is the Display key, S the control panel. Neither key does anything at the XMB root.
    case Qt::Key_I:if(shell_.mediaOpen())shell_.toggleMediaDisplay();break;
    case Qt::Key_S:if(shell_.videoOpen())toggleVideoPanel();else if(shell_.mediaOpen())shell_.toggleOptions();break;
    case Qt::Key_F11:shell_.setFullscreen(!window()->isFullScreen());break;
    default:QOpenGLWidget::keyPressEvent(e);return;
    }
    e->accept();
}
void View::keyReleaseEvent(QKeyEvent* e) {
    if(const auto bit=arrowBit(e->key())){if(!e->isAutoRepeat())keyHeld_&=~bit;e->accept();return;}
    QOpenGLWidget::keyReleaseEvent(e);
}
void View::focusOutEvent(QFocusEvent* e) {
    keyHeld_=keyPressed_=0;
    QOpenGLWidget::focusOutEvent(e);
}
void View::pollController() {
#ifdef Q_OS_WIN
    if(!xmbOwnsForeground() || QApplication::activeModalWidget()){buttons_=keyHeld_=keyPressed_=0;triggerHeld_=false;return;}
    // A --script run is the script's own input: every scripted key goes through replayKeyTap, which dispatches the
    // same handlers the pad drives. A physical controller is therefore not read at all, so a capture cannot depend
    // on a pad lying on its d-pad (which used to walk the keyboard focus off the default key between two grabs).
    static const bool scriptedInput=QCoreApplication::arguments().contains(QStringLiteral("--script"));
    // XInput slots 0-3 (pad::xinputLatest, merged) and any PlayStation controller over HID (pad.cpp), merged;
    // the left stick doubles as the D-pad.
    pad::XInputState xstate;const bool xinput=!scriptedInput && pad::xinputLatest(xstate);
    pad::State hid;const bool playstation=!scriptedInput && pad::latest(hid);
    quint16 buttons=quint16((xinput?xstate.buttons:0)|(playstation?hid.buttons:0)|keyHeld_);
    // PS is consumed by the global watcher (ps_watcher.cpp), which also covers a focused game; it must
    // never reach the XMB navigation bits below.
    buttons=quint16(buttons&~0x400);
    // The particles' d-pad wind reads the held direction (the plugin's layer+0x1c0 state), whatever owns navigation.
    // L2/R2 are analog triggers rather than D-pad bits: they are read here and never merged into `buttons`,
    // so a held trigger cannot move the XMB, cannot press Cross, and cannot disturb repeatAt_. Both the pad's
    // own report (pad.cpp l2/r2) and an XInput pad's triggers feed the same ramp; whichever is pressed harder
    // wins when both controllers are attached.
    const float triggerDown=std::max(xinput?xstate.l2:0.f,playstation?hid.l2:0.f);
    const float triggerUp=std::max(xinput?xstate.r2:0.f,playstation?hid.r2:0.f);
    pollTriggerSeek(triggerDown,triggerUp);
    if(shell_.psMenuOpen()) {
        const auto edgePs=quint16((buttons&~buttons_)|keyPressed_);keyPressed_=0;buttons_=buttons;
        if(edgePs&XINPUT_GAMEPAD_DPAD_UP)shell_.psMenuMove(-1);
        if(edgePs&XINPUT_GAMEPAD_DPAD_DOWN)shell_.psMenuMove(1);
        if(edgePs&XINPUT_GAMEPAD_A)activatePsMenuSelection();
        if(edgePs&XINPUT_GAMEPAD_B)shell_.psMenuBack();
        return;
    }
    // The item options panel owns the pad while open: D-pad moves, Cross runs, Circle/Triangle dismiss it.
    if(shell_.optionsOpen()) {
        const auto edgePs=quint16((buttons&~buttons_)|keyPressed_);keyPressed_=0;buttons_=buttons;
        if(edgePs&XINPUT_GAMEPAD_DPAD_UP)shell_.optionsMove(-1);
        if(edgePs&XINPUT_GAMEPAD_DPAD_DOWN)shell_.optionsMove(1);
        if(edgePs&XINPUT_GAMEPAD_A)shell_.optionsActivate();
        if(edgePs&XINPUT_GAMEPAD_B)shell_.optionsClose();
        if(edgePs&XINPUT_GAMEPAD_Y)shell_.toggleOptions();
        return;
    }
    // The music control panel owns the pad while it is up, exactly as the item panel does above.
    if(musicPanelOpen_) {
        const auto edgePs=quint16((buttons&~buttons_)|keyPressed_);keyPressed_=0;buttons_=buttons;
        if(edgePs&XINPUT_GAMEPAD_DPAD_LEFT)musicPanelMove(-1);
        if(edgePs&XINPUT_GAMEPAD_DPAD_RIGHT)musicPanelMove(1);
        if(edgePs&XINPUT_GAMEPAD_A)musicPanelActivate();
        if(edgePs&(XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_Y))closeMusicPanel();
        return;
    }
    // The Photo Gallery owns the pad while its area is up: d-pad and keyboard arrows hop between piles or cells with the
    // XMB's repeat, the left stick moves the free focus, the right stick (a DualShock/HID pad) and L1/R1 zoom.
    if(galleryOwnsInput()) {
        const auto edgeG=quint16((buttons&~buttons_)|keyPressed_);keyPressed_=0;const auto nowG=clock_.elapsed();
        if(buttons || edgeG){shell_.noteActivity();mediaControls_.activity(nowG);}
        const bool repeatG=nowG>=repeatAt_;
        const qint64 nextG=std::max(repeatAt_,nowG-67)+67;
        const auto moveG=quint16(edgeG|(repeatG?buttons:0));
        const float stickX=xinput?xstate.lx:(playstation?hid.lx:0.f),stickY=xinput?xstate.ly:(playstation?hid.ly:0.f);
        const float zoomY=playstation?hid.ry:0.f;
        if(moveG&XINPUT_GAMEPAD_DPAD_LEFT)galleryPress(Qt::Key_Left);
        if(gallery_ && (moveG&XINPUT_GAMEPAD_DPAD_RIGHT))galleryPress(Qt::Key_Right);
        if(gallery_ && (moveG&XINPUT_GAMEPAD_DPAD_UP))galleryPress(Qt::Key_Up);
        if(gallery_ && (moveG&XINPUT_GAMEPAD_DPAD_DOWN))galleryPress(Qt::Key_Down);
        if(gallery_ && (edgeG&XINPUT_GAMEPAD_A))galleryPress(Qt::Key_Return);
        if(gallery_ && (edgeG&XINPUT_GAMEPAD_B))galleryPress(Qt::Key_Escape);
        if(gallery_ && (edgeG&XINPUT_GAMEPAD_Y))galleryPress(Qt::Key_T);
        if(gallery_ && (edgeG&XINPUT_GAMEPAD_START))galleryPress(Qt::Key_S);
        if(gallery_ && (edgeG&XINPUT_GAMEPAD_RIGHT_SHOULDER))galleryPress(Qt::Key_Plus);
        if(gallery_ && (edgeG&XINPUT_GAMEPAD_LEFT_SHOULDER))galleryPress(Qt::Key_Minus);
        if(gallery_ && gallery_->controller)gallery_->controller->setSticks(stickX,stickY,zoomY);
        if(edgeG&0x000f)repeatAt_=nowG+300;else if(moveG&0x000f)repeatAt_=nextG;
        buttons_=buttons;return;
    }
    if(xinput || playstation) {
        const float lx=xinput?xstate.lx:hid.lx,ly=xinput?xstate.ly:hid.ly; // +y down, as HID
        if(lx<-.5f)buttons|=XINPUT_GAMEPAD_DPAD_LEFT;if(lx>.5f)buttons|=XINPUT_GAMEPAD_DPAD_RIGHT;
        if(ly<-.5f)buttons|=XINPUT_GAMEPAD_DPAD_UP;if(ly>.5f)buttons|=XINPUT_GAMEPAD_DPAD_DOWN;
    }
    // A key tapped between two polls still counts as a press.
    const auto edge=quint16((buttons&~buttons_)|keyPressed_);keyPressed_=0;const auto now=clock_.elapsed();
    if(buttons || edge){shell_.noteActivity();mediaControls_.activity(now);}
    if(introRunning()){if(edge)coldboot_.skip();buttons_=buttons;return;}
    // Held-direction repeat as the real XMB does it (RPCS3 capture, 2026-10-25): the first move on the press, the
    // second 300 ms (18 frames) later, then one every 4 frames (66.7 ms), with no acceleration however long it is held.
    constexpr qint64 repeatDelay=300,repeatInterval=67;
    const bool repeat=now>=repeatAt_;
    // Counted from the last deadline, not from this poll, so the 16 ms poll does not stretch the interval to 80 ms.
    const qint64 nextRepeat=std::max(repeatAt_,now-repeatInterval)+repeatInterval;
    const auto movement=quint16(edge|(repeat?buttons:0));
    if(osk_.open) { // X press, Circle cancel, Square backspace, Triangle space, START accept (the PS3 keyboard's bindings)
        if(movement&XINPUT_GAMEPAD_DPAD_LEFT)oskMove(-1,0);if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)oskMove(1,0);
        if(movement&XINPUT_GAMEPAD_DPAD_UP)oskMove(0,-1);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)oskMove(0,1);
        if(edge&XINPUT_GAMEPAD_A)oskPress();
        // Circle is Back: it closes the symbol palette first and the keyboard second, like Escape (audit 4.3).
        if(edge&XINPUT_GAMEPAD_B){if(osk_.paletteOpen){osk_.paletteOpen=false;update();}else oskClose(false);}
        if(edge&XINPUT_GAMEPAD_X)oskBackspace();if(edge&XINPUT_GAMEPAD_Y)oskInsert(" ");
        if(edge&XINPUT_GAMEPAD_START)oskClose(true);
        if(edge&0x000f)repeatAt_=now+repeatDelay;else if(movement&0x000f)repeatAt_=nextRepeat;
        buttons_=buttons;return;
    }
    if(plEdit_.open) {
        if(movement&XINPUT_GAMEPAD_DPAD_UP)playlistEditorInput(0);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)playlistEditorInput(1);
        if(edge&XINPUT_GAMEPAD_DPAD_LEFT)playlistEditorInput(2);if(edge&XINPUT_GAMEPAD_DPAD_RIGHT)playlistEditorInput(3);
        if(edge&XINPUT_GAMEPAD_A)playlistEditorInput(4);if(edge&XINPUT_GAMEPAD_B)playlistEditorInput(5);
        if(edge&XINPUT_GAMEPAD_Y)playlistEditorInput(6);if(edge&XINPUT_GAMEPAD_X)playlistEditorInput(7);
        if(edge&0x000f)repeatAt_=now+repeatDelay;else if(movement&0x000f)repeatAt_=nextRepeat;
        buttons_=buttons;return;
    }
    if(userCreate_.open) {
        if(edge&XINPUT_GAMEPAD_DPAD_UP)userCreateInput(0);if(edge&XINPUT_GAMEPAD_DPAD_DOWN)userCreateInput(1);
        if(edge&XINPUT_GAMEPAD_A)userCreateInput(2);if(edge&XINPUT_GAMEPAD_B)userCreateInput(3);
        buttons_=buttons;return;
    }
    if(shell_.photoOpen() && shell_.message().isEmpty()) {
        if(photoViewport_.canPan()) {
            if(movement&XINPUT_GAMEPAD_DPAD_LEFT)panPhoto(-1,0);if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)panPhoto(1,0);
            if(movement&XINPUT_GAMEPAD_DPAD_UP)panPhoto(0,-1);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)panPhoto(0,1);
        } else {
            if(movement&XINPUT_GAMEPAD_DPAD_LEFT)shell_.horizontal(-1);if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)shell_.horizontal(1);
            if(movement&XINPUT_GAMEPAD_DPAD_UP)shell_.vertical(-1);if(movement&XINPUT_GAMEPAD_DPAD_DOWN)shell_.vertical(1);
        }
        if(edge&XINPUT_GAMEPAD_LEFT_SHOULDER)zoomPhoto(-1);if(edge&XINPUT_GAMEPAD_RIGHT_SHOULDER)zoomPhoto(1);
        if(edge&XINPUT_GAMEPAD_X)resetPhotoView();if(edge&XINPUT_GAMEPAD_Y)shell_.rotatePhoto(1);
        if(edge&XINPUT_GAMEPAD_A)shell_.activate();if(edge&XINPUT_GAMEPAD_B)shell_.back();
        if(edge&0x000f)repeatAt_=now+repeatDelay;else if(movement&0x000f)repeatAt_=nextRepeat;
        buttons_=buttons;return;
    }
    if(videoPanelOpen_ && shell_.videoOpen() && shell_.message().isEmpty()) {
        if(movement&XINPUT_GAMEPAD_DPAD_LEFT)videoPanelMove(-1,0);
        if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)videoPanelMove(1,0);
        if(movement&XINPUT_GAMEPAD_DPAD_UP)videoPanelMove(0,1);
        if(movement&XINPUT_GAMEPAD_DPAD_DOWN)videoPanelMove(0,-1);
        if(edge&XINPUT_GAMEPAD_A)videoPanelActivate();
        if(edge&(XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_START))toggleVideoPanel();
        if(edge&XINPUT_GAMEPAD_BACK)shell_.toggleMediaDisplay();
        if(edge&XINPUT_GAMEPAD_X)shell_.toggleMediaShuffle();
        if(edge&XINPUT_GAMEPAD_LEFT_SHOULDER)shell_.skipMedia(-1);
        if(edge&XINPUT_GAMEPAD_RIGHT_SHOULDER)shell_.skipMedia(1);
        if(edge&0x000f)repeatAt_=now+repeatDelay;else if(movement&0x000f)repeatAt_=nextRepeat;
        buttons_=buttons;return;
    }
    if(movement&XINPUT_GAMEPAD_DPAD_LEFT)shell_.horizontal(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_RIGHT)shell_.horizontal(1);
    if(movement&XINPUT_GAMEPAD_DPAD_UP)shell_.vertical(-1);
    if(movement&XINPUT_GAMEPAD_DPAD_DOWN)shell_.vertical(1);
    if(edge&XINPUT_GAMEPAD_A)shell_.activate();if(edge&XINPUT_GAMEPAD_B){if(!inGameBack())shell_.back();}
    // Triangle: the item options panel on a content row, the in-player panel (subtitles, audio tracks,
    // repeat, shuffle) while a video plays, and the firmware's music control panel (explore_plugin_full
    // control_panel_page) while a local track plays - which is where the console puts it (audit 7.2). Repeat
    // moved off this key and keeps R (and the panel's own Repeat button), so nothing lost a binding.
    if(edge&XINPUT_GAMEPAD_Y) {
        if(shell_.videoOpen())toggleVideoPanel();
        else if(!shell_.mediaOpen())shell_.toggleOptions();
        else if(mediaAudioOnly())toggleMusicPanel();
        else shell_.cycleMediaRepeat();
    }
    // L1/R1 off the player page a list ten rows at a time, the pad's form of PageUp/PageDown (H10; the key
    // binding's step - the console's own L1/R1 list step is not traced, INFERRED). In the player they skip.
    if(!shell_.mediaOpen()) {
        if(edge&XINPUT_GAMEPAD_LEFT_SHOULDER)shell_.vertical(-10);
        if(edge&XINPUT_GAMEPAD_RIGHT_SHOULDER)shell_.vertical(10);
    }
    if(shell_.mediaOpen()) {
        if(edge&XINPUT_GAMEPAD_LEFT_SHOULDER)shell_.skipMedia(-1);
        if(edge&XINPUT_GAMEPAD_RIGHT_SHOULDER)shell_.skipMedia(1);
        if(edge&XINPUT_GAMEPAD_X)shell_.toggleMediaShuffle();
        // SELECT and START are both in the video container's direct-key hint set (tex_cp_help_select and
        // tex_cp_help_start, 86x30 each). SELECT's function is proved by the container's own text:
        // msg_display_display_with_icon is "Display " + U+F88E and friendim_plugin's msg_select_gaiji is the
        // same code point, so SELECT is the Display key. START has no string of its own in the container (no
        // *_with_icon carries a START glyph), so it opens the one surface the container's keys reach - the
        // control panel Triangle opens - instead of staying dead (FIDELITY-BEHAVIOUR I48).
        if(edge&XINPUT_GAMEPAD_BACK)shell_.toggleMediaDisplay();
        if(edge&XINPUT_GAMEPAD_START){if(shell_.videoOpen())toggleVideoPanel();else shell_.toggleOptions();}
    }
    if(edge&0x000f)repeatAt_=now+repeatDelay;else if(movement&0x000f)repeatAt_=nextRepeat;
    buttons_=buttons;
#endif
}
// ---- On-screen keyboard -----------------------------------------------------------------------------------------
// The keyboard is the firmware's (audit/OSK-FIRMWARE.md): a 934x500 panel at 493,290 with a title bar, an entry
// field and a footer; a 10x6 grid of 72x54 cells from 496,406 with 66x50 key faces; a prediction column at 1216; a
// 7x7 symbol palette with five visible rows; black at alpha 0.70 behind all of it. Every rect, caption, colour and
// texture name comes from oskLayout_, which the constructor resolves from oskfullkeypanel_plugin.rco and the
// decrypted module. Each element draws its firmware texture when the name is present AND decoded and falls back to
// the measured colour/text otherwise, so a name the asset loader cannot read yet switches over on its own (the
// badges and focus rings did exactly that when the GIM decoder landed - audit 12.4, 14).
namespace {
int oskAdjacent(const QString& text,int cursor,bool forward) {
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme,text);
    boundaries.setPosition(cursor);
    const int next=forward?boundaries.toNextBoundary():boundaries.toPreviousBoundary();
    return next<0?(forward?int(text.size()):0):next;
}
// The measured key-face colours at four heights of the 66x50 face - 0%, 40%, 75% and 100% - taken from
// s1/f0060-0257.png as a median over each key's left inset so no caption ink lands in the sample. The profile is not
// a straight two-stop ramp: the face brightens to a plateau at 40-75% and then drops to a dark lower lip. They are
// NOT the RCO's BUTTON_BASE_* images: those are pre-composite artwork whose gradient runs the other way
// (BUTTON_BASE_WHITE reads 165/219/239 where the captured face reads 169/196/190/152), and the per-key row
// assignment lives in the module at run time, not in the RCO (audit 2.1). So the faces are drawn from this geometry.
struct OskFace { QColor at0,at40,at75,at100; };
OskFace oskFace(const QString& role) {
    if(role==QStringLiteral("olive"))return {QColor(152,157,117),QColor(175,183,144),QColor(179,183,156),QColor(122,125,129)};
    if(role==QStringLiteral("blue"))return {QColor(121,152,160),QColor(145,177,182),QColor(152,174,179),QColor(129,129,131)};
    if(role==QStringLiteral("mint"))return {QColor(109,151,132),QColor(137,175,158),QColor(148,175,164),QColor(110,107,109)};
    if(role==QStringLiteral("arrow"))return {QColor(116,150,156),QColor(140,169,177),QColor(140,161,167),QColor(110,107,109)};
    return {QColor(164,168,164),QColor(196,195,196),QColor(190,188,190),QColor(152,150,152)};  // letters and digits
}
// Where a key's caption sits. The capture's character keys are not caption-centred: every one of the ten columns
// puts its glyph centre 53.5 px right of the cell's left edge (measured on all four character rows), while the
// function-row captions are centred on the key. Both are measured, not fitted (audit 4.6).
constexpr double oskCharacterCaptionCentre=53.5;
}
QString View::oskCharacter(int row,int column) const {
    // One source for a key's character: the resolved page, never a table in this file.
    const OskKey* key=oskLayout_.keyAt(osk_.page,row,column);
    return key?key->label:QString();
}
void View::openOsk(const QString& title,const QString& initial,std::function<void(QString)> done,bool masked) {
    osk_=Osk{};osk_.open=true;osk_.title=title;osk_.text=initial;osk_.cursor=int(initial.size());osk_.done=std::move(done);
    osk_.masked=masked;
    // The firmware's default focus: row 2, column 4 = `g`, in two independent boots (audit 5, 12.2). The layout
    // carries it, so a different layout can start somewhere else without touching this call.
    osk_.page=0;
    osk_.row=oskLayout_.defaultFocusRow;
    osk_.column=oskLayout_.defaultFocusColumn;
    update();
}
void View::oskClose(bool accept) {
    auto done=std::move(osk_.done);
    const auto value=osk_.preserveWhitespace?osk_.text:osk_.text.trimmed();
    const bool allowEmpty=osk_.allowEmpty;
    osk_=Osk{};
    if(accept && done && (allowEmpty || !value.isEmpty()))done(value);
    update();
}
void View::oskInsert(const QString& value) {
    // Limit only the inserted text. Editing a full field must never discard its existing suffix.
    int count=std::min(int(value.size()),std::max(0,osk_.maxLength-int(osk_.text.size())));
    if(count>0 && count<value.size() && value[count-1].isHighSurrogate() && value[count].isLowSurrogate())--count;
    osk_.text.insert(osk_.cursor,value.left(count));osk_.cursor+=count;update();
}
void View::oskBackspace() {
    if(osk_.cursor>0) {
        const int before=oskAdjacent(osk_.text,osk_.cursor,false);
        osk_.text.remove(before,osk_.cursor-before);osk_.cursor=before;
    }
    update();
}
void View::oskMove(int dx,int dy) {
    // The palette owns the arrows while it is open: it is a 7x5 window over the symbol list (audit 4.3).
    if(osk_.paletteOpen) { oskPaletteMove(dx,dy);return; }
    const OskPage* page=oskLayout_.page(osk_.page);
    if(!page || page->rows.isEmpty())return;
    const int rowCount=int(page->rows.size());
    osk_.row=(osk_.row+dy+rowCount)%rowCount;   // up/down wrap over the six rows, keeping the column
    if(dx!=0) {
        // Left/right land on keys, not cells: a wide key occupies its span, so a horizontal move jumps the whole
        // span and wraps inside the row (audit 5, "wide keys occupy their span").
        const OskKey* current=page->keyAt(osk_.row,osk_.column);
        int target=current?(dx>0?current->column+current->span:current->column-1):osk_.column+dx;
        if(target>=oskLayout_.columns)target=0;
        if(target<0)target=oskLayout_.columns-1;
        if(const OskKey* next=page->keyAt(osk_.row,target))osk_.column=next->column;
        else osk_.column=target;
    }
    // A vertical move can land inside a wide key or on a blank cell; the key's own left column is the focus.
    if(const OskKey* key=page->keyAt(osk_.row,osk_.column))osk_.column=key->column;
    update();
}
void View::oskPaletteMove(int dx,int dy) {
    const OskPalette& palette=oskLayout_.palette;
    if(palette.columns<=0 || palette.visibleRows<=0)return;
    osk_.paletteColumn=(osk_.paletteColumn+dx+palette.columns)%palette.columns;
    int row=osk_.paletteRow+dy;
    // 32 symbols fill four rows of seven plus four cells, so the 5-row window has nothing to scroll on this page;
    // the arithmetic is here because the module string continues with accented symbols after the ASCII run.
    const int symbolRows=(int(palette.symbols.size())+palette.columns-1)/palette.columns;
    const int maxScroll=std::max(0,symbolRows-palette.visibleRows);
    if(row<0) {
        if(osk_.paletteScroll>0) { --osk_.paletteScroll;row=0; }
        else row=0;
    } else if(row>=palette.visibleRows) {
        if(osk_.paletteScroll<maxScroll) { ++osk_.paletteScroll;row=palette.visibleRows-1; }
        else row=palette.visibleRows-1;
    }
    osk_.paletteRow=row;
    update();
}
void View::oskPalettePress() {
    const QChar symbol=oskLayout_.palette.symbolAtCell(osk_.paletteRow,osk_.paletteColumn,osk_.paletteScroll);
    if(!symbol.isNull())oskInsert(QString(symbol));
    // F: the capture never shows a symbol being accepted, so closing the popup after the insert is a fit, not a
    // trace. Back (Escape/Circle) is the traced way out.
    osk_.paletteOpen=false;
    update();
}
void View::oskPress() {
    if(osk_.paletteOpen) { oskPalettePress();return; }
    const OskKey* key=oskLayout_.keyAt(osk_.page,osk_.row,osk_.column);
    if(!key)return;
    // Every action the layout can carry is dispatched, and the three that the native build cannot honour say so
    // in the log instead of doing nothing silently (audit 8: no IME modules, no dictionary, no XMB clipboard).
    switch(key->action) {
    case OskAction::Character:oskInsert(key->label);break;
    case OskAction::Backspace:oskBackspace();break;
    case OskAction::Space:oskInsert(QStringLiteral(" "));break;
    case OskAction::Shift:osk_.page=osk_.page==0?1:0;break;
    case OskAction::Accent:qInfo().noquote()<<"osk: accent page is not captured; the accent key is inert";break;
    case OskAction::Palette:osk_.paletteOpen=true;osk_.paletteRow=0;osk_.paletteColumn=0;osk_.paletteScroll=0;break;
    case OskAction::Enter:oskClose(true);return;
    case OskAction::MoveUp:oskMove(0,-1);break;
    case OskAction::MoveDown:oskMove(0,1);break;
    case OskAction::CaretLeft:osk_.cursor=oskAdjacent(osk_.text,osk_.cursor,false);break;
    case OskAction::CaretRight:osk_.cursor=oskAdjacent(osk_.text,osk_.cursor,true);break;
    case OskAction::Mode:qInfo().noquote()<<"osk: Input Mode is unavailable (no IME modules; audit 8.3)";break;
    case OskAction::Copy:case OskAction::Paste:qInfo().noquote()<<"osk: copy/paste is not wired to a clipboard yet";break;
    case OskAction::None:break;
    }
    update();
}
void View::drawOsk(QPainter& p) {
    const OskPage* page=oskLayout_.page(osk_.page);
    if(!page)return;
    const OskLayout& l=oskLayout_;
    const QString container=QStringLiteral("oskfullkeypanel_plugin");
    // The text renderer is the one metric authority (audit/TEXT-RENDERER.md); the QPainter pen is the fallback for a
    // run where FreeType did not initialise.
    const auto label=[&](const QRectF& r,const QString& value,int size,QColor colour,int flags=Qt::AlignCenter,bool bold=false) {
        if(value.isEmpty())return;
        QFont f(shell_.fontFamily());f.setPixelSize(size);f.setBold(bold);
        if(textRenderer_.ready()){textRenderer_.draw(*this,p,r,value,f,colour,flags,0,false);return;}
        f.setHintingPreference(QFont::PreferNoHinting);p.setFont(f);p.setPen(colour);p.drawText(r,flags,value);
    };
    // The decoded RCO image for a name, or null when the name is absent or its GIM pixel format is not decoded yet.
    // Assets caches pixels and failures alike, so this is a map lookup after the startup load.
    const auto texture=[&](const QString& name)->const QImage& {
        static const QImage none;
        if(name.isEmpty())return none;
        return assets_.image(container,name);
    };
    // A 3x3 nine-slice from nine named RCO images, each border's size taken from the image itself (the panel's are
    // 10/20 wide and 15/62 tall, the candidate window's 10/19 and 9/18 - audit 2.1). False when any piece is missing,
    // so the caller draws its measured fallback instead.
    const auto slice9=[&](const QString& tl,const QString& tc,const QString& tr,
                          const QString& ml,const QString& mc,const QString& mr,
                          const QString& bl,const QString& bc,const QString& br,const QRectF& r) {
        const QImage* piece[9]={&texture(tl),&texture(tc),&texture(tr),&texture(ml),&texture(mc),
                                &texture(mr),&texture(bl),&texture(bc),&texture(br)};
        for(const QImage* image:piece)if(image->isNull())return false;
        const qreal left=piece[0]->width(),right=piece[2]->width();
        const qreal top=piece[0]->height(),bottom=piece[6]->height();
        if(r.width()<left+right || r.height()<top+bottom)return false;
        const qreal dx[4]={r.left(),r.left()+left,r.right()-right,r.right()};
        const qreal dy[4]={r.top(),r.top()+top,r.bottom()-bottom,r.bottom()};
        p.save();
        for(int row=0;row<3;++row)for(int column=0;column<3;++column) {
            const QImage& image=*piece[row*3+column];
            const QRectF target(dx[column],dy[row],dx[column+1]-dx[column],dy[row+1]-dy[row]);
            if(target.width()<=0. || target.height()<=0.)continue;
            p.drawImage(target,image,QRectF(0,0,image.width(),image.height()));
        }
        p.restore();
        return true;
    };
    p.save();
    // The dim behind the panel: black at the measured 0.70, not the 120/255 the free-hand keyboard used (4.1).
    QColor dim=l.bands.dim;dim.setAlphaF(l.bands.dimAlpha);
    p.fillRect(QRectF(0,0,1920,1080),dim);
    // The panel plate: the middle band's own measured colour first (157,154,157 - the plate and key-seam colour the
    // capture shows), then the RCO's nine-slice (oskpanel_base1x0..3x2) over it. The slice's right-hand pieces are a
    // 34%-alpha shadow (tbase1x2/tbase2x3 sample black at alpha 87), so drawing them straight onto the page made the
    // panel's right edge dark instead of a shaded plate - over the fill they shade it, as the capture shows.
    const QRectF panelRect(l.bands.panel);
    p.fillRect(panelRect,QColor(157,154,157));
    if(!slice9(l.slices.value(QStringLiteral("panel/1x0")),l.slices.value(QStringLiteral("panel/1x1")),
               l.slices.value(QStringLiteral("panel/1x2")),l.slices.value(QStringLiteral("panel/2x0")),
               l.slices.value(QStringLiteral("panel/2x1")),l.slices.value(QStringLiteral("panel/2x3")),
               l.slices.value(QStringLiteral("panel/3x0")),l.slices.value(QStringLiteral("panel/3x1")),
               l.slices.value(QStringLiteral("panel/3x2")),panelRect)) {
        p.setPen(QPen(QColor(30,30,30),1));p.setBrush(Qt::NoBrush);
        p.drawRect(panelRect.adjusted(.5,.5,-.5,-.5));
    }
    // Title bar: flat 141,142,140 with the sheen at its bottom edge (4.1). No texture - the RCO's plane carries
    // width=0 and the module assigns it at run time (12.1).
    QLinearGradient title(0,l.bands.titleBar.top(),0,l.bands.titleBar.bottom());
    title.setColorAt(0,l.bands.titleTop);title.setColorAt(1,l.bands.titleBottom);
    p.fillRect(QRectF(l.bands.titleBar),title);
    label(QRectF(l.bands.titleIcon.left()-6,l.bands.titleBar.top()+2,24,34),QString(QChar(0x270e)),22,l.bands.titleTextColour);
    label(QRectF(double(l.bands.titleText.left()),double(l.bands.titleBar.top()),double(l.bands.titleText.width()),
                 double(l.bands.titleBar.height())),osk_.title,27,l.bands.titleTextColour,Qt::AlignLeft|Qt::AlignVCenter);
    // Entry field: the measured 86 -> 62 -> 47 vertical gradient, one line, clipped at its own edges (4.1, 5.1).
    QLinearGradient field(0,l.bands.field.top(),0,l.bands.field.bottom());
    field.setColorAt(0,l.bands.fieldTop);field.setColorAt(.5,l.bands.fieldMid);field.setColorAt(1,l.bands.fieldBottom);
    p.fillRect(QRectF(l.bands.field),field);
    QFont fieldFont(shell_.fontFamily());fieldFont.setPixelSize(22);
    // A masked field draws one bullet per character (the layout's mask glyph) and measures the caret against those
    // bullets, so the position stays right without the characters being drawn anywhere (5.4).
    const QString composed=osk_.masked?QString(osk_.text.size(),l.mask.glyph):osk_.text;
    const auto advance=[&](const QString& text) {
        return textRenderer_.ready()?textRenderer_.measure(fieldFont,text):QFontMetricsF(fieldFont).horizontalAdvance(text);
    };
    const double inset=20.;                                  // 5.1: text origin x 494+20
    const double textX=l.bands.field.left()+inset;
    const double textWidth=l.bands.field.width()-2*inset;
    const double before=advance(composed.left(osk_.cursor));
    const double scroll=std::max(0.,before-(textWidth-4.));
    p.save();
    p.setClipRect(QRectF(textX,double(l.bands.field.top()),textWidth,double(l.bands.field.height())),Qt::IntersectClip);
    label({textX-scroll,double(l.bands.field.top()),std::max(textWidth,advance(composed)+4.),
           double(l.bands.field.height())},composed,22,l.bands.fieldTextColour,Qt::AlignLeft|Qt::AlignVCenter);
    // The caret is two marks, not one (5.2): a 2 px I-beam with 12 px serifs at the pen advance, in the measured
    // 130,249,12 green; and, immediately to its right, the cursor marker - a hollow white triangle that points
    // LEFT. The I-beam blinks (the phase comes from OskCaret::blinkMilliseconds in paintGL); the marker does not.
    // Measured this pass on the three console OSK frames (out/rpcs3-allgraphics-2026-10-28/frames/osk-default.png,
    // osk-default-2.png, osk-key-down.png - identical in this band): the I-beam's green shaft is x 578..579 at
    // rows 346..378, and the marker's ink is x 582..594 x y 353..369, i.e. a 13 x 17 px triangle whose apex is at
    // x 582..583 (4 px right of the caret) and whose vertical base is at x 593..594, on a 71,71,71 field. Its ink
    // reads 215,215,215 on the base and 229 at the apex - not the 255 pure white this build drew (127 px of pure
    // white in the same band). The triangle points left because the apex is at its LEFT x: the frame's zoomed
    // crop shows the two diagonal strokes running right from the apex into the vertical base.
    // [TRACED: the bbox, the 4 px offset, the orientation and the 215 ink on all three frames.
    // FIT (with the module trace behind it): that the marker does NOT blink. All three console frames are in the
    // I-beam's lit phase, so no local frame samples the dark phase; OSK-FIRMWARE 5.2 records the triangle as
    // "present whether or not the I-beam is lit" and FIDELITY-MEDIA O6 repeats it, and drawing it unconditionally
    // is also what keeps a pinned capture (which lands on one blink phase) from losing the marker entirely.]
    static constexpr double oskMarkerInk=215.;      // TRACED: the console frames' marker ink, 215,215,215
    static constexpr double oskMarkerInset=4.;      // TRACED: apex x = caret x + 4 (582 against 578)
    static constexpr double oskMarkerWidth=13.;     // TRACED: x 582..594
    static constexpr double oskMarkerHeight=17.;    // TRACED: y 353..369
    if(osk_.caretLit) {
        const double x=textX+before-scroll;
        const double y=l.bands.field.top()+9;
        p.fillRect(QRectF(x,y,l.caret.shaftWidth,l.caret.height),l.caret.colour);
        const double serifX=x-(l.caret.serifWidth-l.caret.shaftWidth)/2.;
        p.fillRect(QRectF(serifX,y,l.caret.serifWidth,2),l.caret.colour);
        p.fillRect(QRectF(serifX,y+l.caret.height-2,l.caret.serifWidth,2),l.caret.colour);
    }
    {
        // The marker sits on the field's vertical centre, which the frame puts 7 px below the I-beam's own top
        // (marker y 353 against the caret's 346) - half of the caret's 33 px shaft is 16.5, so the marker's 17 px
        // height centres on the same axis. It is drawn whether or not the I-beam is lit.
        const double x=textX+before-scroll+oskMarkerInset;
        const double y=l.bands.field.top()+9;
        const double top=y+(l.caret.height-oskMarkerHeight)/2.;
        const double apexY=top+oskMarkerHeight/2.;
        p.save();
        p.setPen(QPen(QColor(int(oskMarkerInk),int(oskMarkerInk),int(oskMarkerInk)),2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(x+oskMarkerWidth,top),QPointF(x+oskMarkerWidth,top+oskMarkerHeight)); // the base, on the right
        p.drawLine(QPointF(x+oskMarkerWidth,top),QPointF(x,apexY));                              // upper stroke to the apex
        p.drawLine(QPointF(x+oskMarkerWidth,top+oskMarkerHeight),QPointF(x,apexY));               // lower stroke to the apex
        p.restore();
    }
    p.restore();
    // The key grid. A key's cell is its span x 72 by 54; the face is inset by 3/2, so a one-cell face is the measured
    // 66x50 and a two-cell one 138x50 (4.1, 4.2).
    for(const OskRow& row:page->rows) {
        for(const OskKey& key:row.keys) {
            const QRectF face(double(l.gridOrigin.x()+key.column*l.cell.width()+l.faceInset.x()),
                              double(l.gridOrigin.y()+row.row*l.cell.height()+l.faceInset.y()),
                              double(key.span*l.cell.width()-2*l.faceInset.x()),
                              double(l.cell.height()-2*l.faceInset.y()));
            const bool focused=osk_.row==row.row && osk_.column>=key.column && osk_.column<key.column+key.span;
            // The focused face brightens instead of changing colour: measured on s3/f0020-0078.png the focused a/A
            // reads 152,159,140 / 206,219,140 / 210,220,183 against 157,163,124 / 172,178,139 / 162,165,138 at rest.
            const auto lit=[&](const QColor& colour) {
                if(!focused)return colour;
                const auto mix=[&](int channel){return int(channel+(255-channel)*.35);};
                return QColor(mix(colour.red()),mix(colour.green()),mix(colour.blue()));
            };
            // The palette opener in the function row is drawn on a mint plate in the capture (measured
            // 116,157,141 at 784,676, the same as the blank key beside it), while the layout's inferred role for it
            // reads "blue"; the capture wins.
            const OskFace colours=oskFace(key.icon==QStringLiteral("grid")?QStringLiteral("mint"):key.face);
            QLinearGradient gradient(face.topLeft(),face.bottomLeft());
            gradient.setColorAt(0,lit(colours.at0));gradient.setColorAt(.4,lit(colours.at40));
            gradient.setColorAt(.75,lit(colours.at75));gradient.setColorAt(1,lit(colours.at100));
            p.setPen(Qt::NoPen);p.setBrush(gradient);
            p.drawRoundedRect(face,l.keyCornerRadius,l.keyCornerRadius);
            // The button badge: the module's LABEL_* pill, drawn at the measured 20 px height its 74x28 artwork
            // carries (the pill itself fills the left 44 px of it). LABEL_UP/DOWN/LEFT/RIGHT are the arrow keys' whole
            // face art, so those keys draw their glyph caption instead of a pill.
            const QImage& badge=texture(key.badgeTexture);
            const bool hasBadge=!badge.isNull();
            if(hasBadge) {
                const double badgeWidth=badge.width()*(20./badge.height());
                p.drawImage(QRectF(face.left()+2,face.top()-3,badgeWidth,20),badge);
            } else if(!key.badge.isEmpty()) {
                QFont badgeFont(shell_.fontFamily());badgeFont.setPixelSize(13);
                const double badgeWidth=std::max(18.,QFontMetricsF(badgeFont).horizontalAdvance(key.badge)+12.);
                const QRectF pill(face.left()+2,face.top()-1,badgeWidth,18);
                p.setPen(Qt::NoPen);p.setBrush(QColor(64,70,80,235));
                p.drawRoundedRect(pill,9,9);
                label(pill,key.badge,13,QColor(240,240,240));
            }
            // Captions. A character key's glyph centre is the measured cell.left + 53.5, 2 px below the face centre,
            // at 24 px; a function-row caption is centred on the key, 6 px below the face centre (the badge shares the
            // top of the face) and is drawn at 19 px, which is what the capture's 93 px "Backspace" and 53 px "Space"
            // inks give. Sizes are measured, not fitted (audit 4.6: cap 19 and 18).
            const bool character=row.row<4;
            const QRectF caption=character
                ?QRectF(double(l.gridOrigin.x()+key.column*l.cell.width())+oskCharacterCaptionCentre-59.,face.top()+4.,120.,face.height())
                :face.adjusted(0,12,0,0);
            label(caption,key.label,character?24:19,QColor(30,40,50));
            // The palette opener carries a drawn glyph, not a caption: four columns by three rows of 5 px squares on a
            // 7 px pitch (a 26x19 block), centred on the face like the capture's.
            if(key.icon==QStringLiteral("grid")) {
                p.setPen(Qt::NoPen);p.setBrush(QColor(30,40,50));
                for(int line=0;line<3;++line)for(int square=0;square<4;++square)
                    p.drawRect(QRectF(face.center().x()-12.5+square*7.,face.center().y()-10.5+line*7.,5.,5.));
            }
            // The focus ring is the firmware's own: SBTN_FOCUS 84x66, or LBTN_FOCUS 166x66 for a wide key, centred on
            // the face (66+18 by 50+16, i.e. 9/8 px of ring outside it - 4.5).
            if(focused) {
                const QImage& ring=texture(key.span>1?QStringLiteral("LBTN_FOCUS"):QStringLiteral("SBTN_FOCUS"));
                if(!ring.isNull())
                    p.drawImage(QRectF(face.center().x()-ring.width()/2.,face.center().y()-ring.height()/2.,
                                       ring.width(),ring.height()),ring);
                else {p.setPen(QPen(QColor(255,255,255,190),2));p.setBrush(Qt::NoBrush);
                      p.drawRoundedRect(face.adjusted(-6,-6,6,6),7,7);}
            }
        }
    }
    // The prediction column: the candidate list, its header, the mode and count cells, then the two buttons outside
    // the 10-column grid (4.4). The list stays empty: the console's candidates come from ime.prx's dictionary, which a
    // PC build does not have, and fabricating them would be a different feature (8.2).
    // The list's own nine-slice is pre-composite artwork (CANDIDATE_WINDOW0x0 reads 129,130,129 where the frame
    // measures 110,110,110), so the measured plate is drawn: fill 110 with the slice's light top and dark bottom edge.
    p.setPen(Qt::NoPen);p.setBrush(QColor(110,110,110));
    p.drawRoundedRect(QRectF(l.bands.candidateList),3,3);
    p.fillRect(QRectF(double(l.bands.candidateList.left())+3,double(l.bands.candidateList.top()),
                      double(l.bands.candidateList.width())-6,1),QColor(151,151,151));
    p.fillRect(QRectF(double(l.bands.candidateList.left())+3,double(l.bands.candidateList.bottom()-1),
                      double(l.bands.candidateList.width())-6,1),QColor(100,100,100));
    // Header: the measured 92 -> 80 gradient with a light line along its bottom edge.
    QLinearGradient header(0,l.bands.predictionHeader.top(),0,l.bands.predictionHeader.bottom());
    header.setColorAt(0,QColor(92,92,92));header.setColorAt(1,QColor(80,80,80));
    p.fillRect(QRectF(l.bands.predictionHeader),header);
    p.fillRect(QRectF(double(l.bands.predictionHeader.left()),double(l.bands.predictionHeader.bottom()-1),
                      double(l.bands.predictionHeader.width()),1),QColor(120,124,125));
    label(QRectF(l.bands.predictionHeader.left()+14.,double(l.bands.predictionHeader.top()),
                 double(l.bands.predictionHeader.width())-46.,double(l.bands.predictionHeader.height())),
          l.captions.prediction,16,QColor(235,235,235),Qt::AlignLeft|Qt::AlignVCenter);
    // The mode and count cells are their own darker wells (85,85,85) inside the header strip.
    p.fillRect(QRectF(l.bands.modeCell),QColor(85,85,85));
    p.fillRect(QRectF(l.bands.countCell),QColor(85,85,85));
    label(QRectF(l.bands.modeCell),page->modeCaption,18,QColor(237,237,237));
    label(QRectF(l.bands.countCell),QStringLiteral("0"),18,QColor(237,237,237));
    for(const OskPredictionButton& button:l.predictionButtons) {
        const QRectF rect(button.rect);
        QLinearGradient gradient(rect.topLeft(),rect.bottomLeft());
        gradient.setColorAt(0,QColor(137,169,175));gradient.setColorAt(1,QColor(112,145,151));
        p.setPen(Qt::NoPen);p.setBrush(gradient);p.drawRoundedRect(rect,l.keyCornerRadius,l.keyCornerRadius);
        // KEYTOP_COPY / KEYTOP_PASTE carry the glyph as a white mask (24x24 of ink inside a 72x54 transparent card) for
        // paf's icon material, so painting them raw draws a white block on the light key. The mask is tinted to the
        // measured ink colour and cached per texture; the card is drawn at the button's own size, which puts the glyph
        // where the card has it.
        static QHash<QString,QImage> tintedCards;
        const auto card=[&](const QString& name,const QColor& ink)->const QImage& {
            static const QImage none;
            const QImage& source=texture(name);
            if(source.isNull())return none;
            const QString key=name+QChar('/')+ink.name();
            if(const auto it=tintedCards.constFind(key);it!=tintedCards.constEnd())return it.value();
            QImage tinted=source.convertToFormat(QImage::Format_ARGB32);
            for(int y=0;y<tinted.height();++y) {
                auto* line=reinterpret_cast<QRgb*>(tinted.scanLine(y));
                for(int x=0;x<tinted.width();++x)line[x]=qRgba(ink.red(),ink.green(),ink.blue(),qAlpha(line[x]));
            }
            return tintedCards.insert(key,tinted).value();
        };
        const QImage& glyph=card(button.texture,QColor(30,50,60,button.enabled?255:120));
        if(!glyph.isNull()) {
            p.save();p.setOpacity(button.enabled?1.:.55);p.drawImage(rect,glyph);p.restore();continue;
        }
        // The fallback shapes, for a run without the RCO: two offset plates for copy, a clipboard for paste.
        p.setPen(QPen(QColor(30,50,60,button.enabled?255:120),2));
        p.setBrush(QColor(30,50,60,button.enabled?255:120));
        if(button.action==OskAction::Copy) {
            p.drawRect(QRectF(rect.left()+19,rect.top()+16,17,17));
            p.drawRect(QRectF(rect.left()+26,rect.top()+23,17,17));
        } else {
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(rect.left()+21,rect.top()+20,20,24));
            p.drawRect(QRectF(rect.left()+27,rect.top()+15,8,6));
        }
    }
    // Footer: (70,70,70) with the capture's 2 px light top edge and its own bottom line (4.1).
    p.fillRect(QRectF(l.bands.footer),QColor(70,70,70));
    p.fillRect(QRectF(double(l.bands.footer.left()),double(l.bands.footer.top()),
                      double(l.bands.footer.width()),2),QColor(102,100,102));
    p.fillRect(QRectF(double(l.bands.footer.left()),double(l.bands.footer.bottom()-1),
                      double(l.bands.footer.width()),1),l.bands.footerBottom);
    // The symbol palette, last so it covers the left half of the keyboard: the RCO's BASE_SYMBOL plate as a
    // nine-slice (its interior reads 112,111,112 against the measured 114,114,114), a 7x5 window of 53x53 cells on a
    // 48 px pitch, and FOCUS_SYMBOL on the focused cell (4.3).
    if(osk_.paletteOpen) {
        const OskPalette& palette=l.palette;
        const QImage& plate=texture(QStringLiteral("BASE_SYMBOL"));
        if(!plate.isNull())drawNineSlice(p,container,QStringLiteral("BASE_SYMBOL"),QRectF(palette.frame),12.,1.);
        else p.fillRect(QRectF(palette.frame),QColor(114,114,114));
        for(int row=0;row<palette.visibleRows;++row)for(int column=0;column<palette.columns;++column) {
            const QChar symbol=palette.symbolAtCell(row,column,osk_.paletteScroll);
            if(symbol.isNull())continue;
            const QRect cell=palette.cellRect(row,column);
            if(row==osk_.paletteRow && column==osk_.paletteColumn) {
                const QImage& focus=texture(QStringLiteral("FOCUS_SYMBOL"));
                if(!focus.isNull())p.drawImage(QRectF(cell),focus);
                else {p.setPen(QPen(Qt::white,2));p.setBrush(Qt::NoBrush);p.drawRect(cell);}
            }
            label(QRectF(cell),QString(symbol),22,QColor(235,235,235));
        }
        // The scroll strip only has a knob when the module's symbol list runs past the five visible rows.
        const int symbolRows=(int(palette.symbols.size())+qMax(1,palette.columns)-1)/qMax(1,palette.columns);
        if(symbolRows>palette.visibleRows) {
            const QRectF track(palette.frame.right()-16.,double(palette.gridOrigin.y()),
                               12.,double(palette.visibleRows*palette.pitch.height()));
            const QImage& base=texture(QStringLiteral("SCROLL_BASE_SYMBOL"));
            if(!base.isNull())p.drawImage(track,base); else p.fillRect(track,QColor(90,90,90));
            const double knobHeight=std::max(12.,track.height()*palette.visibleRows/symbolRows);
            const double maxScroll=std::max(1,symbolRows-palette.visibleRows);
            const double knobTop=track.top()+(track.height()-knobHeight)*osk_.paletteScroll/maxScroll;
            const QImage& knob=texture(QStringLiteral("SCROLL_KNOB_SYMBOL"));
            const QRectF knobRect(track.left(),knobTop,track.width(),knobHeight);
            if(!knob.isNull())p.drawImage(knobRect,knob); else p.fillRect(knobRect,QColor(200,200,200));
        }
    }
    p.restore();
}
}
